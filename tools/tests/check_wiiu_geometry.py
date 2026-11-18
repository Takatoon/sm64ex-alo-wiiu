#!/usr/bin/env python3
"""Compare real vertex/triangle code with a pre-change source snapshot.

Run in the Wii U build container (host cc is sufficient):
python3 tools/tests/check_wiiu_geometry.py --reference build/geometry-validation/gfx_pc_before.c
The backend is recorded rather than rendered. No timing claims are made here.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, name):
    # Find the declaration's actual beginning, independent of return type.
    start = source.rfind("\nstatic ", 0, source.index(name + "(")) + 1
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


PRELUDE = r'''
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#define TARGET_WII_U 1
#define F3DEX_GBI_2 1
#define F3DEX_GBI 1
#define _LANGUAGE_C 1
#include "PR/gbi.h"
#include "pc/gfx/gfx_pc.h"
#include "pc/gfx/gfx_cc.h"
#include "pc/gfx/gfx_rendering_api.h"
#include "pc/gfx/wiiu_vertex_format.h"
#include "pc/gfx/wiiu_vertex_cache.h"
#define MAX_LIGHTS 2
#define MAX_VERTICES 64
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define HALF_SCREEN_WIDTH 160
#define HALF_SCREEN_HEIGHT 120
'''

STUBS = r'''
static unsigned shader_queries;
static unsigned configFiltering=1;
static unsigned reserve_calls;
static bool direct_fails;
static bool packed_mode, indexed_mode, packed_batch, batch_texture, batch_fog;
static size_t indexed_reuses, indexed_emitted;
static bool gfx_gx2_indexed(void) { return indexed_mode; }
static unsigned batch_inputs;
static unsigned texture_mask, input_count;
static bool gfx_gx2_packed_colors(void) { return packed_mode; }
static float direct_buffer[VERTEX_BUFFER_SIZE];
static float *gfx_gx2_reserve_vertices(size_t n) {
    assert(n <= VERTEX_BUFFER_SIZE); ++reserve_calls;
    packed_batch=packed_mode;
    batch_texture=texture_mask!=0;
    batch_fog=(rdp.other_mode_l>>30)==G_BL_CLR_FOG;
    batch_inputs=input_count;
    return direct_fails ? NULL : direct_buffer;
}
static bool zero_to_one;
static struct ColorCombiner test_comb;
static struct TextureHashmapNode textures[2];
static bool rollover;
static size_t emitted_bytes;
static void emit(const void *p, size_t n) {
    assert(n <= 1000000 && emitted_bytes + n <= 100000000);
    emitted_bytes += n;
    assert(fwrite(p, 1, n, stdout) == n);
}
static bool z_range(void) { return zero_to_one; }
static void shader(struct ShaderProgram *p) { (void)p; }
static void flag(bool b) { (void)b; }
static void rect(int a,int b,int c,int d) { (void)a;(void)b;(void)c;(void)d; }
static void sampler(int a,bool b,uint32_t c,uint32_t d) { (void)a;(void)b;(void)c;(void)d; }
static void info(struct ShaderProgram *p,uint8_t *n,bool used[2]) {
    (void)p; ++shader_queries;
    *n=input_count; used[0]=(texture_mask&1)!=0; used[1]=(texture_mask&2)!=0;
}
static struct GfxRenderingAPI api = {
    .z_is_from_0_to_1=z_range, .unload_shader=shader, .load_shader=shader,
    .shader_get_info=info, .set_depth_test=flag, .set_depth_mask=flag,
    .set_zmode_decal=flag, .set_viewport=rect, .set_scissor=rect,
    .set_use_alpha=flag, .set_sampler_parameters=sampler,
};
#define gfx_gx2_api api
static struct ColorCombiner *gfx_lookup_or_create_color_combiner(uint32_t id) {
    static unsigned programs[16];
    test_comb.cc_id=id;
    test_comb.prg=(struct ShaderProgram *)&programs[(id>>24)&15];
    return &test_comb;
}
static void import_texture(int i) {
    rendering_state.textures[i]=&textures[i];
    if (rollover && i==1) {
        rollover=false; ++gfx_texture_cache_generation;
        rdp.textures_changed[0]=true;
    }
}
static void gfx_flush(void) {
    if (!buf_vbo_num_tris) return;
    emit("D",1);
    emit(&buf_vbo_num_tris,sizeof(buf_vbo_num_tris));
    if (!packed_batch) {
        emit(&buf_vbo_len,sizeof(buf_vbo_len));
        emit(buf_vbo,buf_vbo_len*sizeof(float));
    } else {
        size_t base=4+(batch_texture?4:0);
        size_t colors=batch_inputs+(batch_fog?1:0);
        size_t expanded=(base+colors*4)*buf_vbo_num_tris*3;
        assert(buf_vbo_len==(base+colors)*(buf_vbo_indexed ? buf_unique_vertices : buf_vbo_num_tris*3));
        if (buf_vbo_indexed) { indexed_reuses += buf_vbo_num_tris*3 - buf_unique_vertices; indexed_emitted += buf_unique_vertices; }
        emit(&expanded,sizeof(expanded));
        for(size_t i=0;i<buf_vbo_num_tris*3;i++) {
            size_t index=buf_vbo_indexed ? buf_indices[i] : i;
            assert(index < buf_vbo_len/(base+colors));
            const float *p=buf_vbo+index*(base+colors);
            emit(p,base*sizeof(float));p+=base;
            for(size_t c=0;c<colors;c++,p++) {
                const uint8_t *rgba=(const uint8_t *)p;
                float normalized[4];
                for(unsigned k=0;k<4;k++) normalized[k]=rgba[k]/255.0f;
                emit(normalized,sizeof(normalized));
            }
        }
    }
    buf_vbo_len=buf_vbo_num_tris=0;
}
'''

TEST = r'''
static uint32_t seed=0x671331u;
static unsigned random_u(void) { seed=seed*1664525u+1013904223u; return seed; }
static void record_vertices(void) {
    // Record the vertex fields without structure padding.
    emit("V",1);
    for (int i=0;i<64;i++) {
        struct LoadedVertex *v=&rsp.loaded_vertices[i];
        emit(&v->x,6*sizeof(float)); emit(&v->color,4); emit(&v->clip_rej,1);
    }
}
int main(void) {
    gfx_rapi=&api;
    test_comb.prg=(struct ShaderProgram *)1;
    for (int trial=0;trial<240;trial++) {
        for (int j=0;j<4;j++) {
            static const uint8_t mapping[]={CC_SHADE,CC_PRIM,CC_ENV,CC_LOD,CC_0,CC_TEXEL0,CC_TEXEL1};
            test_comb.shader_input_mapping[0][j]=mapping[(j+trial)%7];
            test_comb.shader_input_mapping[1][j]=mapping[(3-j+trial)%7];
        }
        direct_fails=trial%7==0;
        texture_mask=trial%4; input_count=trial%5;
        configFiltering=(trial&4)!=0; zero_to_one=(trial&8)!=0;
        invalidate(G_MTX);
        memset(&rsp,0,sizeof(rsp)); memset(&rdp,0,sizeof(rdp));
        memset(&rendering_state,0,sizeof(rendering_state));
        gfx_current_dimensions.width=(trial&1)?854:640;
        gfx_current_dimensions.height=480;
        rsp.modelview_matrix_stack_size=1;
        for(int i=0;i<4;i++) rsp.modelview_matrix_stack[0][i][i]=1;
        rsp.MP_matrix[0][0]=0.004f; rsp.MP_matrix[1][1]=0.004f;
        rsp.MP_matrix[2][2]=0.001f; rsp.MP_matrix[2][3]=0.002f;
        rsp.MP_matrix[3][3]=(trial%3==0)?1.0f:(trial%3==1)?4500.0f:8000.0f;
        rsp.current_num_lights=1+trial%3; rsp.lights_changed=true;
        for(int i=0;i<3;i++) {
            for(int j=0;j<3;j++) {
                rsp.current_lights[i].dir[j]=30+random_u()%90;
                rsp.current_lights[i].col[j]=random_u()%128;
                rsp.current_lights[i].colc[j]=random_u()%128;
            }
        }
        rsp.geometry_mode=G_ZBUFFER;
        if(trial&1) rsp.geometry_mode|=G_LIGHTING;
        if(trial&2) rsp.geometry_mode|=G_TEXTURE_GEN;
        if(trial&4) rsp.geometry_mode|=G_FOG;
        switch(trial%4) {
            case 1:rsp.geometry_mode|=G_CULL_BACK;break;
            case 2:rsp.geometry_mode|=G_CULL_FRONT;break;
            case 3:rsp.geometry_mode|=G_CULL_BOTH;break;
        }
        rsp.texture_scaling_factor.s=65535; rsp.texture_scaling_factor.t=32768;
        rsp.fog_mul=100; rsp.fog_offset=60;
        rdp.other_mode_l=(trial&8)?(G_BL_A_MEM<<18):0;
        if(trial&16) rdp.other_mode_l|=(uint32_t)G_BL_CLR_FOG<<30;
        if(trial&32) rdp.other_mode_l|=CVG_X_ALPHA;
        rdp.other_mode_h=(trial&64)?G_TF_POINT:G_TF_BILERP;
        rdp.prim_color=(struct RGBA){122,160,255,70};
        rdp.env_color=(struct RGBA){55,140,180,255};
        rdp.fog_color=(struct RGBA){40,120,200,255};
        rdp.texture_tile.lrs=124; rdp.texture_tile.lrt=252;
        rdp.texture_tile.uls=4; rdp.texture_tile.ult=8;
        rdp.textures_changed[0]=rdp.textures_changed[1]=true;
        rollover=true;
        Vtx vertices[64]; memset(vertices,0,sizeof(vertices));
        for(int i=0;i<64;i++) {
            for(int j=0;j<3;j++) vertices[i].v.ob[j]=(int)(random_u()%1000)-500;
            for(int j=0;j<2;j++) vertices[i].v.tc[j]=random_u()%8192;
            for(int j=0;j<4;j++) vertices[i].v.cn[j]=random_u()%256;
        }
        gfx_sp_vertex(64,0,vertices); record_vertices();
        // Reuse vertices, cross a capacity flush, then reload a partial range.
        for(int i=0;i<2300;i++) {
            if(i==410) { gfx_sp_vertex(17,3,vertices+9); record_vertices(); }
            if(i==600) {
                invalidate(G_SETENVCOLOR);
                rdp.env_color.r=201;
                rdp.textures_changed[0]=true;
            }
            if(i==650) {
                invalidate(G_SETPRIMCOLOR);
                rdp.prim_color=(struct RGBA){255,0,74,30};
            }
            if(i==700) {
                invalidate(G_SETFOGCOLOR);
                rdp.fog_color=(struct RGBA){80,160,240,255};
            }
            if(i==750) {
                invalidate(G_SETCOMBINE);
                for(int j=0;j<4;j++) {
                    test_comb.shader_input_mapping[0][j]=(j&1)?CC_ENV:CC_SHADE;
                    test_comb.shader_input_mapping[1][j]=(j&1)?CC_SHADE:CC_PRIM;
                }
            }
            if(i==800) {
                invalidate(G_SETOTHERMODE_L);
                rdp.other_mode_l^=(uint32_t)G_BL_CLR_FOG<<30;
            }
            invalidate(G_TRI1);
            unsigned a=random_u()%64;
            unsigned b=random_u()%64;
            unsigned c=random_u()%64;
            gfx_sp_tri1(a,b,c);
        }
        // Rectangles temporarily override viewport, geometry and COPY filter.
        invalidate(G_TEXRECT);
        rdp.other_mode_h=G_CYC_COPY;
        gfx_draw_rectangle(0,0,640,480);
        invalidate(G_TRI1);
        gfx_sp_tri1(1,2,3);
        gfx_flush();
    }
    fprintf(stderr,"shader queries: %u, vertex reservations: %u\n",shader_queries,reserve_calls);
    return 0;
}
'''


def make_harness(source):
    # Use the renderer's actual declarations and functions, not reimplementations.
    declarations = source[source.index("struct RGBA {"):source.index("struct TextureHashmapNode {")]
    declarations += source[source.index("struct TextureHashmapNode {"):source.index("static struct {", source.index("struct TextureHashmapNode {"))]
    declarations += "static uint32_t gfx_texture_cache_generation;\n"
    declarations += source[source.index("struct ColorCombiner {"):source.index("static struct ColorCombiner color_combiner_pool")]
    declarations += source[source.index("static struct RSP {"):source.index("static bool dropped_frame;")]
    declarations += "static float buf_vbo_fallback[VERTEX_BUFFER_SIZE];\nstatic float *buf_vbo=buf_vbo_fallback;\nstatic size_t buf_vbo_len,buf_vbo_num_tris;\nstatic struct GfxRenderingAPI *gfx_rapi;\n"
    invalidate = "static void invalidate(unsigned opcode) {(void)opcode;}\n"
    marker = "        // Only vertex loads and triangles leave all material state unchanged."
    if marker in source:
        start = source.index(marker)
        end = source.index("\n#endif", source.index("triangle_state.valid = false;", start))
        invalidate = "static void invalidate(unsigned opcode) {\n" + source[start:end] + "\n}\n"
    names = ["gfx_normalize_vector", "gfx_transposed_matrix_mul", "calculate_normal_dir",
             "gfx_adjust_x_for_aspect_ratio", "gfx_sp_vertex", "gfx_sp_tri1", "gfx_draw_rectangle"]
    if "static void gfx_prepare_material_values(" in source:
        names.insert(names.index("gfx_sp_tri1"), "gfx_prepare_material_values")
    if "static struct WiiUVertexCache vertex_reuse;" not in declarations:
        declarations += "static bool buf_vbo_indexed;\nstatic uint16_t buf_indices[MAX_BUFFERED_TRIANGLES*3];\nstatic size_t buf_unique_vertices;\n"
    return PRELUDE + declarations + STUBS + invalidate + "\n".join(function(source,n) for n in names) + TEST


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--fast-math", action="store_true", help="Also enable the Wii U build's floating-point optimization flag")
    parser.add_argument("--writer", choices=["baseline", "packed", "indexed"], default="packed")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--batch-limit",type=int,choices=(256,512,1024),default=256)
    args = parser.parse_args()
    sources = [args.reference.read_text(), (ROOT/"src/pc/gfx/gfx_pc.c").read_text()]
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        outputs = []
        for index, source in enumerate(sources):
            c = temp/f"geometry{index}.c"
            exe = temp/f"geometry{index}"
            harness=make_harness(source)
            if index==0:
                harness=harness.replace('#include "pc/gfx/gfx_pc.h"',
                    '#include "pc/gfx/gfx_pc.h"\n#undef MAX_BUFFERED_TRIANGLES\n#define MAX_BUFFERED_TRIANGLES '+str(args.batch_limit))
            elif 'static unsigned buf_triangle_limit' in harness:
                harness=harness.replace('gfx_rapi=&api;', 'gfx_rapi=&api; buf_triangle_limit='+str(args.batch_limit)+';')
            if index==1 and args.writer in ("packed", "indexed"):
                harness=harness.replace("gfx_rapi=&api;", "gfx_rapi=&api; packed_mode=true;")
            if index==1 and args.writer=="indexed":
                harness=harness.replace("packed_mode=true;", "packed_mode=true; indexed_mode=true;")
                harness=harness.replace("return 0;", "if(indexed_mode) { assert(indexed_reuses>0); fprintf(stderr, \"indexed_reuses=%zu indexed_emitted=%zu\\n\",indexed_reuses,indexed_emitted); } return 0;")
            c.write_text(harness)
            flags = ["-ffast-math"] if args.fast_math else []
            if args.sanitize:
                flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie"]
            subprocess.run(["cc", "-O2", *flags, "-I"+str(ROOT/"include"), "-I"+str(ROOT/"src"), "-I"+str(ROOT/"src/pc/gfx"), str(c), "-lm", "-o", str(exe)], check=True)
            result = subprocess.run([str(exe)], check=True, capture_output=True, timeout=20)
            print(("reference: " if index == 0 else "optimized: ") + result.stderr.decode().strip())
            outputs.append(result.stdout)
        assert outputs[0] == outputs[1], "Vertex data, rejection or draw batches changed"
        print(f"PASS: {len(outputs[0]):,} bytes identical across 240 geometry/state scenarios (writer={args.writer}, fast_math={args.fast_math}, sanitize={args.sanitize}, batch_limit={args.batch_limit})")


if __name__ == "__main__":
    main()
