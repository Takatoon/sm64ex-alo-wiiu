"""Host checks for real row-copy and frontend cache rollover code; no GPU emulation."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

ROWS = r'''
#include <assert.h>
#include <stdlib.h>
#include "src/pc/gfx/wiiu_texture_image.h"
int main(void) {
    const uint8_t pixels[] = {1,2,3,4,5,6,7,8, 9,10,11,12,13,14,15,16};
    uint8_t result[64];
    struct WiiUTextureImage image = {pixels, sizeof(pixels), 8, 2, 2, WIIU_TEXTURE_RGBA8};
    assert(wiiu_texture_copy_rows(result, sizeof(result), 32, &image));
    assert(!memcmp(result, pixels, 8));
    assert(!memcmp(result+32, pixels+8, 8));
    for (int i=8; i<32; i++) assert(result[i] == 0 && result[i+32] == 0);
    image.size--;
    memset(result, 0xAB, sizeof(result));
    assert(!wiiu_texture_copy_rows(result, sizeof(result), 32, &image));
    for (unsigned i=0; i<sizeof(result); i++) assert(result[i] == 0xAB);
    image.size++;
    assert(!wiiu_texture_copy_rows(result, 63, 32, &image));
    assert(!wiiu_texture_copy_rows(result, sizeof(result), 7, &image));
    image.row_stride = SIZE_MAX;
    assert(!wiiu_texture_copy_rows(result, sizeof(result), 32, &image));
    image.row_stride = 8;
    image.width = 0;
    assert(!wiiu_texture_copy_rows(result, sizeof(result), 32, &image));
    assert(!wiiu_texture_copy_rows(result, sizeof(result), 32, NULL));
    return 0;
}
'''

CACHE = r'''
#include "src/pc/gfx/gfx_pc.c"
#include "src/pc/gfx/gfx_cc.c"
unsigned int configFiltering;
void sys_fatal(const char *fmt, ...) { (void)fmt; abort(); }
static unsigned ids, resets, draws;
static uint32_t selected[2];
static bool z_range(void) { return false; }
static uint32_t new_texture(void) { return ids++; }
static void select_texture(int tile, uint32_t id) { selected[tile] = id; }
static void sampler(int t, bool l, uint32_t s, uint32_t r) { (void)t;(void)l;(void)s;(void)r; }
static void draw(float *p, size_t n, size_t t) { (void)p;(void)n;(void)t; draws++; }
static void upload(const uint8_t *p, int w, int h) { (void)p; assert(w>0 && h>0); }
static void shader_info(struct ShaderProgram *p, uint8_t *n, bool used[2]) {
    (void)p; *n=0; used[0]=used[1]=true;
}
void gfx_gx2_reset_texture_storage(void) { assert(buf_vbo_len == 0); resets++; }
int main(void) {
    struct GfxRenderingAPI api = { .new_texture=new_texture, .select_texture=select_texture,
        .z_is_from_0_to_1=z_range,
        .set_sampler_parameters=sampler, .draw_triangles=draw,
        .upload_texture=upload, .shader_get_info=shader_info };
    gfx_rapi = &api;
    struct TextureHashmapNode *node;
    /* Fill the real bounded cache, then force a rollover on tile 1 while
     * tile 0 still references an old slot. Pending geometry must flush first. */
    for (unsigned i=0; i<MAX_CACHED_TEXTURES; i++) {
        assert(!gfx_texture_cache_lookup(0,&node,(const uint8_t *)(uintptr_t)(i+1),0,0,0,0));
        assert(node->texture_id == i);
    }
    assert(ids == MAX_CACHED_TEXTURES);
    rendering_state.textures[0] = &gfx_texture_cache.pool[17];
    rdp.textures_changed[0] = rdp.textures_changed[1] = false;
    buf_vbo_len=4; buf_vbo_num_tris=1;
    assert(!gfx_texture_cache_lookup(1,&rendering_state.textures[1],(const uint8_t *)0x10000,0,0,0,0));
    assert(draws == 1 && resets == 1);
    assert(rendering_state.textures[0] == NULL);
    assert(rdp.textures_changed[0] && rdp.textures_changed[1]);
    assert(rendering_state.textures[1]->texture_id == 0);
    assert(gfx_texture_cache_generation == 1);
    assert(ids == MAX_CACHED_TEXTURES);
    /* IDs must remain bounded across repeated full cache generations. */
    for (unsigned cycle=0; cycle<3; cycle++) {
        for (unsigned i=0; i<MAX_CACHED_TEXTURES; i++)
            gfx_texture_cache_lookup(0,&node,(const uint8_t *)(uintptr_t)(0x20000+cycle*MAX_CACHED_TEXTURES+i),0,0,0,0);
        assert(ids == MAX_CACHED_TEXTURES);
    }
    gfx_wiiu_clear_texture_cache();
    assert(gfx_texture_cache.pool_pos == 0);
    assert(rendering_state.textures[0] == NULL && rendering_state.textures[1] == NULL);
    /* Drive the actual triangle path: tile 0 consumes the last free slot,
     * tile 1 rolls over, then both bindings must be recovered before emit. */
    for (unsigned i=0; i<MAX_CACHED_TEXTURES-1; i++)
        gfx_texture_cache_lookup(0,&node,(const uint8_t *)(uintptr_t)(i+1),0,0,0,0);
    uint32_t generation = gfx_texture_cache_generation;
    static uint8_t pixels0[8], pixels1[8];
    rdp.loaded_texture[0].addr=pixels0; rdp.loaded_texture[1].addr=pixels1;
    rdp.loaded_texture[0].size_bytes=rdp.loaded_texture[1].size_bytes=8;
    rdp.texture_tile.fmt=G_IM_FMT_I; rdp.texture_tile.siz=G_IM_SIZ_8b;
    rdp.texture_tile.line_size_bytes=1;
    rdp.other_mode_l=G_BL_A_MEM << 18;
    color_combiner_pool_size=1;
    color_combiner_pool[0].cc_id=0;
    color_combiner_pool[0].prg=(struct ShaderProgram *)1;
    rendering_state.shader_program=(struct ShaderProgram *)1;
    rsp.loaded_vertices[0].w=rsp.loaded_vertices[1].w=rsp.loaded_vertices[2].w=1;
    gfx_sp_tri1(0,1,2);
    assert(gfx_texture_cache_generation == generation+1);
    assert(buf_vbo_num_tris == 1);
    assert(rendering_state.textures[0] != rendering_state.textures[1]);
    assert(selected[0] == rendering_state.textures[0]->texture_id);
    assert(selected[1] == rendering_state.textures[1]->texture_id);
    assert(rendering_state.textures[0]->texture_addr == pixels0);
    assert(rendering_state.textures[1]->texture_addr == pixels1);
    gfx_wiiu_clear_texture_cache();
    /* Exit must discard CPU batches rather than draw after losing GX2. */
    unsigned draws_before_exit = draws;
    buf_vbo_len=4; buf_vbo_num_tris=1;
    gfx_wiiu_shutdown_texture_cache();
    assert(draws == draws_before_exit && buf_vbo_len == 0 && buf_vbo_num_tris == 0);
    return 0;
}
'''


class Resources(unittest.TestCase):
    def compile_run(self, source, flags):
        with tempfile.TemporaryDirectory() as tmp:
            code = pathlib.Path(tmp) / "test.c"
            code.write_text(source)
            exe = str(pathlib.Path(tmp) / "test")
            subprocess.run(["gcc", "-std=gnu11", "-ffunction-sections", "-fdata-sections",
                            "-Wl,--gc-sections", "-I", str(ROOT), "-I", str(ROOT / "include"),
                            "-I", str(ROOT / "src"), *flags, str(code), "-lm", "-o", exe], check=True)
            subprocess.run([exe], check=True, timeout=15)

    def test_pitched_copy_and_invalid_spans(self):
        self.compile_run(ROWS, [])

    def test_real_cache_rollover(self):
        self.compile_run(CACHE, ["-DTARGET_WII_U=1", "-DVERSION_US=1", "-DF3DEX_GBI_2=1"])


if __name__ == "__main__":
    unittest.main()
