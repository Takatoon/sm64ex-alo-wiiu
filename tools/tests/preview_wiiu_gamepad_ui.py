"""Render the actual CPU compositor with PNG files and stubbed GX2 scanout.

Run in the development container. No game build or hardware is required.
Images are layout previews; this does not test GX2 timing or real VPAD input.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "build/minimap-previews"

PREFIX = r'''
#define TARGET_WII_U 1
#define WIIU_MINIMAP 1
#define EXTERNAL_DATA 1
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "src/pc/platform.h"
#include "src/pc/wiiu_gamepad_hud.h"
#include "src/pc/wiiu_minimap.h"
#include "src/pc/wiiu_gamepad_layout.h"
#include "src/pc/gfx/wiiu_gamepad_font.h"
#include "src/pc/gfx/wiiu_dds_rgba.h"
#include "include/seq_ids.h"
struct GX2ColorBuffer { struct { unsigned pitch, imageSize; } surface; };
enum { GX2_INVALIDATE_MODE_CPU };
static unsigned flush_calls, flush_bytes;
static bool gpu_ready;
static unsigned dirty_store_bytes;
static bool gfx_gx2_gamepad_ready() { return gpu_ready; }
static void DCStoreRangeNoSync(void*, unsigned bytes) { dirty_store_bytes += bytes; }
static void OSMemoryBarrier() {}
static void GX2Invalidate(int, void*, unsigned bytes) {
    ++flush_calls;
    flush_bytes += bytes;
}
#define WHBLogPrintf(...) ((void)0)
static WiiUMinimapFrame fixture = {
    "gfx/textures/minimap/castle_grounds.png", 0x0c0e18, 240, 300, 0, false
};
static WiiUGamePadHudFrame hud = {
    4, 12, 25, 7, 0, 0,
    WIIU_GAMEPAD_HUD_LIVES | WIIU_GAMEPAD_HUD_COINS | WIIU_GAMEPAD_HUD_STARS | WIIU_GAMEPAD_HUD_POWER,
    3, 0, SEQ_LEVEL_INSIDE_CASTLE, true
};
static bool fixture_playable = false;
static WiiUMinimapMenuScreen menu_screen = WIIU_MINIMAP_MENU_PRESS_START;
WiiUMinimapMenuScreen wiiu_minimap_get_menu_screen() { return menu_screen; }
bool wiiu_minimap_get_frame(WiiUMinimapFrame* frame) { *frame = fixture; return fixture_playable; }
size_t wiiu_minimap_get_preload_paths(const char* const** paths) { *paths = nullptr; return 0; }
bool wiiu_gamepad_hud_get_frame(WiiUGamePadHudFrame* frame) { *frame = hud; return true; }
static unsigned reads;
static uint32_t texture_pack_generation;
static uint32_t fs_texture_pack_generation() { return texture_pack_generation; }
static void* fs_load_file_variant(const char*, const char*, uint64_t*, uint64_t) { return nullptr; }
static void* fs_load_file(const char* path, uint64_t* size) {
    ++reads;
    const char* prefix = "gfx/textures/minimap/";
    const char* filename = !strncmp(path, "gfx/", 4) ? path + 4 : path;
    char mapped[256];
    if (!strncmp(path, prefix, strlen(prefix)) && !strchr(path + strlen(prefix), '/')) {
        snprintf(mapped, sizeof(mapped), "build/minimap/%s", path + strlen(prefix));
        filename = mapped;
    }
    if (texture_pack_generation && !strcmp(filename, "textures/segment2/segment2.05A00.rgba16.png"))
        filename = "textures/segment2/segment2.05C00.rgba16.png";
    FILE* file = fopen(filename, "rb");
    if (!file) return nullptr;
    fseek(file, 0, SEEK_END); *size = ftell(file); rewind(file);
    void* bytes = malloc(*size);
    assert(fread(bytes, 1, *size, file) == *size);
    fclose(file);
    return bytes;
}
'''

SUFFIX = r'''
static std::vector<uint8_t> panel_pixels() {
    auto data = (uint8_t*)g_drc_panel_image_data;
    return std::vector<uint8_t>(data, data + g_drc_panel_color_buffer.surface.imageSize);
}
static void check_gpu_quad(const WiiUMapView& view) {
    float vertices[48];
    const auto& rect = gfx_gx2_drc_map_rect();
    set_quad(vertices, rect, view, true);
    for (int x : {0,1,rect.width/2,rect.width-1})
    for (int y : {0,1,rect.height/2,rect.height-1}) {
        // Interpolate the emitted quad at pixel centres, independently of the
        // CPU cache. Point sampling must agree even for odd crop origins.
        float ndc=-1+2*(rect.x+x+0.5f)/854;
        float t=(ndc-vertices[0])/(vertices[8]-vertices[0]);
        float u=vertices[4]+t*(vertices[12]-vertices[4]);
        float v=vertices[5]+((y+0.5f)/rect.height)*(vertices[21]-vertices[5]);
        assert((int)floorf(u*480+0.0001f)==wiiu_map_sample(x,rect.width,view.x,view.scale));
        assert((int)floorf(v*480+0.0001f)==wiiu_map_sample(y,rect.height,view.y,view.scale));
    }
}
static void check_gpu_composition(const std::vector<uint8_t>& reference,
                                 const std::vector<uint8_t>& overlay, const WiiUMapView& view) {
    check_gpu_quad(view);
    const auto& rect = gfx_gx2_drc_map_rect();
    for(int y=0;y<480;++y)for(int x=0;x<854;++x){
        auto ui=overlay.data()+((size_t)y*864+x)*4;
        auto expected=reference.data()+((size_t)y*864+x)*4;
        const uint8_t *map=gfx_gx2_drc_contains(rect,x,y)?g_drc_map_pixels+
            (wiiu_map_sample(y-rect.y,rect.height,view.y,view.scale)*480+
             wiiu_map_sample(x-rect.x,rect.width,view.x,view.scale))*4:nullptr;
        for(int c=0;c<3;++c){
            int blended=ui[c]+(map?map[c]*(255-ui[3])/255:0);
            assert(abs(blended-expected[c])<=3); // integer premultiplied rounding
        }
    }
}
static void save(const char* name) {
    char path[256];
    snprintf(path, sizeof(path), "build/minimap-previews/%s", name);
    assert(stbi_write_png(path, 854, 480, 4, g_drc_panel_image_data,
                         g_drc_panel_color_buffer.surface.pitch * 4));
}
static std::vector<uint8_t> sidebar() {
    std::vector<uint8_t> pixels;
    for (int y = 0; y < 480; ++y) {
        auto row = gfx_gx2_drc_pixel((uint8_t*)g_drc_panel_image_data, 480, y);
        pixels.insert(pixels.end(), row, row + (854 - 480) * 4);
    }
    return pixels;
}
static void check_map_pixels() {
    // Compare the entire crop with the original unscaled image, independently
    // of the doubled-row cache; include odd origins and the four map edges.
    const auto view = g_drc_map_view;
    for (int y = 0; y < 480; ++y) for (int x = 0; x < 480; ++x) {
        const auto expected = g_drc_map_pixels +
            ((view.y + y / view.scale) * 480 + view.x + x / view.scale) * 4;
        assert(!memcmp(expected, gfx_gx2_drc_pixel(g_drc_panel_base, x, y), 4));
    }
}
static void prepare_one_flush(bool touch = false) {
    flush_calls = flush_bytes = 0;
    assert(gfx_gx2_prepare_drc_panel(touch));
    assert(flush_calls == 1 && flush_bytes == g_drc_panel_color_buffer.surface.imageSize);
    assert(!g_drc_panel_full_flush_pending);
    check_map_pixels();
}
int main() {
    g_drc_prefer_framed_layout = false;
    g_drc_panel_color_buffer.surface.pitch = 864; // exercise nontrivial GX2 pitch
    g_drc_panel_color_buffer.surface.imageSize = 864 * 480 * 4;
    g_drc_panel_base = (uint8_t*)malloc(864 * 480 * 4);
    g_drc_panel_image_data = malloc(864 * 480 * 4);
    assert(gfx_gx2_prepare_drc_panel(true));
    assert(!g_drc_panel_texture_path && !g_drc_map_zoomed);
    save("press-start.png");
    unsigned idle_reads = reads;
    assert(gfx_gx2_prepare_drc_panel(false) && reads == idle_reads);
    // Star/debug selection must mirror TV instead of keeping the idle screen.
    menu_screen = WIIU_MINIMAP_MENU_MIRROR_TV;
    assert(!gfx_gx2_prepare_drc_panel(true) && !g_drc_map_zoomed);
    fixture_playable = true;
    prepare_one_flush();
    assert(gfx_gx2_dialog_glyph('A'));
    assert(gfx_gx2_load_minimap_texture(fixture.texture_path));
    assert(gfx_gx2_load_minimap_texture(g_hud_mario_path));
    save("full.png");
    auto full_sidebar = sidebar();
    std::vector<uint8_t> full_base(g_drc_panel_base, g_drc_panel_base + 864 * 480 * 4);
    unsigned cached_reads = reads;
    gfx_gx2_prepare_drc_panel(false);
    assert(reads == cached_reads); // repeated frames must not read the SD
    prepare_one_flush(true);
    assert(g_drc_map_view.scale == 2 && g_drc_marker_x == 240 && g_drc_marker_y == 240);
    assert(sidebar() == full_sidebar);
    save("zoom.png");
    fixture.mario_x += 30; fixture.mario_y -= 30;
    prepare_one_flush();
    assert(g_drc_marker_x == 240 && g_drc_marker_y == 240);
    assert(sidebar() == full_sidebar && reads == cached_reads);
    for (const auto& position : std::vector<std::pair<int, int>>{
             {0, 0}, {479, 0}, {479, 479}, {0, 479}, {121, 123}, {241, 299}}) {
        fixture.mario_x = position.first;
        fixture.mario_y = position.second;
        prepare_one_flush();
        assert(sidebar() == full_sidebar && reads == cached_reads);
    }
    // A map transition while zoomed must replace the cache and redraw the HUD
    // even when no HUD values changed.
    fixture.texture_path = "gfx/textures/minimap/bob.png";
    prepare_one_flush();
    assert(sidebar() == full_sidebar);
    fixture.texture_path = "gfx/textures/minimap/castle_grounds.png";
    prepare_one_flush();
    prepare_one_flush(true);
    assert(!memcmp(full_base.data(), g_drc_panel_base, full_base.size()));
    // All titles, including punctuation and the longest name, use glyphs.
    for (const auto& title : g_music_titles) {
        memset(g_music_title_pixels, 0, sizeof(g_music_title_pixels));
        assert(gfx_gx2_cache_music_text(title.text));
        int left = 480, right = -1;
        for (int y = 0; y < 56; ++y) for (int x = 0; x < 480; ++x) {
            if (g_music_title_pixels[(y * 480 + x) * 4 + 3]) {
                if (x < left) left = x;
                if (x > right) right = x;
            }
        }
        assert(left >= 12 && right == 469); // ink 467 + two-pixel shadow
    }
    // Marker clipping must not damage the HUD at the eastern edge of a map.
    fixture.mario_x = 479; fixture.mario_y = 40;
    gfx_gx2_prepare_drc_panel(false);
    assert(sidebar() == full_sidebar);
    unsigned missing_reads = reads;
    gfx_gx2_load_minimap_texture("gfx/missing-ui-test.png");
    gfx_gx2_load_minimap_texture("gfx/missing-ui-test.png");
    assert(reads == missing_reads + 1);
    // Compare GPU map + premultiplied overlay to the original CPU composite.
    fixture.mario_x=241;fixture.mario_y=263;
    hud.music_id=SEQ_LEVEL_INSIDE_CASTLE;
    g_music_title_sequence=-2;
    g_drc_map_zoomed=true;
    gpu_ready=false;
    assert(gfx_gx2_prepare_drc_panel(false));
    auto reference=panel_pixels();
    gpu_ready=true;
    assert(gfx_gx2_prepare_drc_panel(false) && g_drc_gpu_zoom);
    check_gpu_composition(reference,panel_pixels(),g_drc_map_view);
    auto transparent_base=std::vector<uint8_t>(g_drc_panel_base,
                                              g_drc_panel_base+864*480*4);
    unsigned generation=g_drc_map_generation;
    dirty_store_bytes=flush_calls=flush_bytes=0;
    fixture.mario_x=243;fixture.mario_y=265;
    assert(gfx_gx2_prepare_drc_panel(false));
    assert(g_drc_map_generation==generation && flush_calls==0);
    assert(dirty_store_bytes>0 && dirty_store_bytes<300000);
    assert(!memcmp(transparent_base.data(),g_drc_panel_base,transparent_base.size()));
    // Traversing all edges must not leave an old marker baked in the overlay.
    for(auto point:std::vector<std::pair<float,float>>{{0,0},{479,0},{0,479},{479,479},{241,263}}){
        fixture.mario_x=point.first;fixture.mario_y=point.second;
        gfx_gx2_prepare_drc_panel(false);
        auto overlay=panel_pixels();auto view=g_drc_map_view;
        gpu_ready=false;gfx_gx2_prepare_drc_panel(false);reference=panel_pixels();
        check_gpu_composition(reference,overlay,view);
        gpu_ready=true;gfx_gx2_prepare_drc_panel(false);
    }
    puts("PASS: GPU crop coordinates, premultiplied map/UI composition and edges; panning leaves the map base untouched and publishes only small UI edits");
    // Return to title after gameplay, then reacquire the foreground: redraw
    // PRESS START instead of retaining stale map pixels or a cleared surface.
    fixture_playable = false;
    menu_screen = WIIU_MINIMAP_MENU_PRESS_START;
    assert(gfx_gx2_prepare_drc_panel(false) && !g_drc_panel_texture_path);
    std::vector<uint8_t> idle((uint8_t*)g_drc_panel_image_data,
                             (uint8_t*)g_drc_panel_image_data + 864 * 480 * 4);
    memset(g_drc_panel_image_data, 0x44, 864 * 480 * 4);
    g_drc_panel_base_valid = false;
    assert(gfx_gx2_prepare_drc_panel(false));
    assert(!memcmp(idle.data(), g_drc_panel_image_data, idle.size()));
    puts("PASS: actual compositor, pixel-identical zoom/edges/map transitions, one GX2 flush per pan, cached assets, menu/foreground restore and fonts");

    fixture_playable = true;
    fixture.texture_path = "gfx/textures/minimap/bob.png";
    fixture.course_name = "BOB-OMB BATTLEFIELD";
    fixture.course_number = 1;
    fixture.mario_x = 158; fixture.mario_y = 343;
    hud.music_id = SEQ_LEVEL_GRASS;
    hud.red_coins = 3;
    g_drc_prefer_framed_layout = true;
    g_drc_map_zoomed = false;
    assert(gfx_gx2_prepare_drc_panel(false) && g_drc_framed_layout);
    save("framed.png");
    const auto coins = panel_pixels();
    hud.flags |= WIIU_GAMEPAD_HUD_TIMER;
    hud.timer = 2003;
    assert(gfx_gx2_prepare_drc_panel(false));
    const auto race = panel_pixels();
    assert(coins != race);
    for (int y = 0; y < 480; ++y)
        if (y < 393 || y >= 427)
            assert(!memcmp(coins.data() + y * 864 * 4, race.data() + y * 864 * 4, 854 * 4));
    save("framed-race.png");
    hud.flags &= ~WIIU_GAMEPAD_HUD_TIMER;
    assert(gfx_gx2_prepare_drc_panel(false));
    assert(coins == panel_pixels());
    gpu_ready = false;
    assert(gfx_gx2_prepare_drc_panel(true));
    reference = panel_pixels();
    save("framed-zoom.png");
    gpu_ready = true;
    assert(gfx_gx2_prepare_drc_panel(false) && g_drc_gpu_zoom);
    check_gpu_composition(reference, panel_pixels(), g_drc_map_view);
    generation = g_drc_map_generation;
    cached_reads = reads;
    dirty_store_bytes = flush_calls = 0;
    fixture.mario_x += 6;
    assert(gfx_gx2_prepare_drc_panel(false));
    assert(g_drc_map_generation == generation && reads == cached_reads && flush_calls == 0);
    assert(dirty_store_bytes > 0 && dirty_store_bytes < 40000);
    const auto old_sidebar = sidebar();
    ++texture_pack_generation;
    assert(gfx_gx2_prepare_drc_panel(false));
    assert(sidebar() != old_sidebar && reads > cached_reads);
    cached_reads = reads;
    assert(gfx_gx2_prepare_drc_panel(false) && reads == cached_reads);
    puts("PASS: framed layout, counter/timer swap, and GPU follow without reloading the map");
}
'''


def main():
    source = (ROOT / "src/pc/gfx/gfx_gx2_window.cpp").read_text()
    declarations = source[source.index("static GX2ColorBuffer   g_drc_panel_color_buffer;"):
                          source.index("#endif\nstatic MEMHeapHandle    g_mem1_heap_handle")]
    compositor = source[source.index("static inline uint8_t* gfx_gx2_drc_pixel"):
                        source.index("#endif\n\nstatic bool gfx_gx2_window_foreground_acquire_callback(void)\n{")]
    renderer = (ROOT/'src/pc/gfx/gfx_gx2_gamepad.cpp').read_text()
    quad = renderer[renderer.index('static void set_quad('):
                    renderer.index('GX2ColorBuffer *gfx_gx2_gamepad_compose(')]
    OUTPUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="wiiu-gamepad-preview-") as directory:
        cpp = Path(directory) / "preview.cpp"
        binary = Path(directory) / "preview"
        cpp.write_text(PREFIX + declarations + compositor + quad + SUFFIX)
        subprocess.run(["g++", "-std=c++17", "-O2", "-I.", "-Iinclude", "-Isrc/pc/gfx",
                        str(cpp), "-o", str(binary)],
                       cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=30)
    print(OUTPUT)


if __name__ == "__main__":
    main()
