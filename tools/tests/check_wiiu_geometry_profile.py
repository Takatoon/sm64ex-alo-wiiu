#!/usr/bin/env python3
"""Exercise actual GX2 profiling paths with a recording backend and fake clock.

Run with python3 inside the Wii U development container. This verifies timer
partitioning and buffer lifecycle; it does not measure hardware performance.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def extract(source, name):
    match = re.search(r'^(?:static |extern "C" )[^\n]*\b' + name + r'\(', source, re.M)
    assert match, name
    begin = source.index("{", match.start())
    depth, end = 1, begin + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <malloc.h>
#define WIIU_LOAD_TIMING_PROFILE 1
#include "pc/gfx/gfx_gx2.h"
#include "pc/gfx/wiiu_vertex_format.h"
#include "pc/gfx/wiiu_vertex_cache.h"
using std::isfinite;
using OSTime = int64_t;
static OSTime clock_tick;
static OSTime OSGetTime() { return ++clock_tick; }
static unsigned invalid_logs, publications, bindings, draws, waits;
static unsigned frame_count;
static bool fail_allocation;
static void *test_memalign(size_t alignment,size_t bytes) {
    return fail_allocation ? nullptr : memalign(alignment,bytes);
}
#define memalign test_memalign
static const void *last_bound;
static size_t last_stride;
static void WHBLogPrintf(const char*, ...) { ++invalid_logs; }
static const int GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER=1;
static const int GX2_PRIMITIVE_MODE_TRIANGLES=2;
static const int GX2_INDEX_TYPE_U16=4;
static void GX2Invalidate(int,void*,size_t) { ++publications; }
static void GX2SetAttribBuffer(int,size_t,size_t stride,void *p) { ++bindings; last_bound=p;last_stride=stride; }
static void GX2DrawEx(int,size_t,int,int) {
    // A new buffer publication and binding must precede every draw.
    assert(publications>draws && bindings==draws+1); ++draws;
}
static unsigned indexed_draws;
static const uint16_t *last_indices;
static void GX2DrawIndexedEx(int mode,size_t count,int type,const void *indices,int offset,int instances) {
    assert(type==GX2_INDEX_TYPE_U16 && indices);
    last_indices=static_cast<const uint16_t*>(indices);
    ++indexed_draws;
    GX2DrawEx(mode,count,offset,instances);
}
static void GX2DrawDone() { ++waits; }
struct ShaderProgram { uint32_t shader_id; size_t num_floats; struct {size_t packedVertexStride;} group; };
static bool packed_colors;
static ShaderProgram shader = {1,8};
static ShaderProgram *current_shader_program=&shader;
struct VboBlock { uint8_t *data; size_t capacity,used; };
static std::vector<VboBlock> vbo_blocks;
static size_t current_vbo_block;
static const size_t VBO_BLOCK_SIZE=1024;
WiiUGx2FrameProfile gfx_gx2_frame_profile={};
'''

TEST = r'''
int main() {
    WiiUVertexCache cache{};
    uint16_t found;
    wiiu_vertex_cache_batch(&cache);
    wiiu_vertex_cache_material(&cache);
    assert(!wiiu_vertex_cache_find(&cache,67,0,&found));
    wiiu_vertex_cache_store(&cache,67,17,7);
    assert(wiiu_vertex_cache_find(&cache,67,17,&found)&&found==7);
    assert(!wiiu_vertex_cache_find(&cache,67,18,&found));
    wiiu_vertex_cache_reload(&cache,0,64);
    assert(wiiu_vertex_cache_find(&cache,67,17,&found));
    wiiu_vertex_cache_reload(&cache,67,1);
    assert(!wiiu_vertex_cache_find(&cache,67,17,&found));
    wiiu_vertex_cache_store(&cache,0,0,0);
    cache.material=UINT32_MAX;
    wiiu_vertex_cache_material(&cache);
    assert(cache.material==1&&!wiiu_vertex_cache_find(&cache,0,0,&found));
    wiiu_vertex_cache_store(&cache,0,0,0);
    cache.batch=UINT32_MAX;
    wiiu_vertex_cache_batch(&cache);
    assert(cache.batch==1&&!wiiu_vertex_cache_find(&cache,0,0,&found));
    float vertices[480];
    for (float &v : vertices) v=1.0f;
    gfx_gx2_draw_triangles(vertices,24,1);
    gfx_gx2_draw_triangles(vertices,24,1);
    assert(draws==2 && gfx_gx2_frame_profile.allocations==1);
    assert(gfx_gx2_frame_profile.vbo_bytes==192);
    assert(vbo_blocks[0].used==224); // second draw aligned to 64 bytes
    gfx_gx2_draw_triangles(vertices,480,20);
    assert(draws==3 && gfx_gx2_frame_profile.allocations==2);
    assert(gfx_gx2_frame_profile.draw_calls==3);
    assert(gfx_gx2_frame_profile.vbo_bytes==2112);
    // A malformed batch contributes validation time, but is never submitted.
    gfx_gx2_draw_triangles(vertices,24,2);
    assert(draws==3 && invalid_logs==1);
    assert(gfx_gx2_frame_profile.ticks[WIIU_GX2_VALIDATE]==4);
    for (unsigned i=WIIU_GX2_STORAGE;i<=WIIU_GX2_DRAW;++i)
        assert(gfx_gx2_frame_profile.ticks[i]==3);
    gfx_gx2_end_frame();
    assert(gfx_gx2_frame_profile.ticks[WIIU_GX2_BOUNDARY]==1);
    assert(publications==5); // 3 draws + 2 used block publications
    gfx_gx2_finish_render();
    assert(waits==1 && current_vbo_block==0);
    assert(gfx_gx2_frame_profile.ticks[WIIU_GX2_FINISH]==1);
    assert(gfx_gx2_frame_profile.ticks[WIIU_GX2_RECYCLE]==1);
    for (const auto &b : vbo_blocks) assert(b.used==0);
    gfx_gx2_draw_triangles(vertices,24,1);
    assert(gfx_gx2_frame_profile.allocations==2); // storage reused after completion
    assert(gfx_gx2_frame_profile.copied_draws==4);
    // Reserve a full batch but commit only the submitted bytes. The second
    // direct batch must not overwrite the first while it is still in flight.
    float *first=gfx_gx2_reserve_vertices(100);
    assert(first && (reinterpret_cast<uintptr_t>(first)&63)==0);
    for(int i=0;i<24;i++) first[i]=2.0f;
    const uint64_t copy_ticks=gfx_gx2_frame_profile.ticks[WIIU_GX2_COPY];
    gfx_gx2_draw_triangles(first,24,1);
    assert(last_bound==first && gfx_gx2_frame_profile.direct_draws==1);
    assert(gfx_gx2_frame_profile.ticks[WIIU_GX2_COPY]==copy_ticks);
    float *second=gfx_gx2_reserve_vertices(100);
    assert(second && second!=first);
    for(int i=0;i<24;i++) second[i]=3.0f;
    gfx_gx2_draw_triangles(second,24,1);
    assert(last_bound==second && gfx_gx2_frame_profile.direct_draws==2);
    for(int i=0;i<24;i++) assert(first[i]==2.0f && second[i]==3.0f);
    assert(gfx_gx2_frame_profile.allocations==2);
    // Exhaust remaining capacity and grow the block vector without invalidating
    // either in-flight data pointer.
    float *third=gfx_gx2_reserve_vertices(700);
    for(int i=0;i<24;i++) third[i]=4.0f;
    gfx_gx2_draw_triangles(third,24,1);
    assert(gfx_gx2_frame_profile.direct_draws==3);
    for(int i=0;i<24;i++) assert(first[i]==2.0f && second[i]==3.0f && third[i]==4.0f);
    const size_t block_before_failure=current_vbo_block;
    fail_allocation=true;
    assert(gfx_gx2_reserve_vertices(10000)==nullptr);
    assert(current_vbo_block==block_before_failure);
    fail_allocation=false;
    // An invalid direct batch never consumes the reserved region.
    float *rejected=gfx_gx2_reserve_vertices(100);
    gfx_gx2_draw_triangles(rejected,24,2);
    assert(gfx_gx2_reserve_vertices(100)==rejected);
    assert(gfx_gx2_frame_profile.direct_draws==3);
    gfx_gx2_finish_render();
    float *reused=gfx_gx2_reserve_vertices(100);
    assert(reused==reinterpret_cast<float*>(vbo_blocks[0].data));
    assert(gfx_gx2_reserve_vertices(SIZE_MAX)==nullptr);
    assert(gfx_gx2_reserve_vertices(0)==nullptr);
    packed_colors=true;shader.group.packedVertexStride=20;
    float rgba_batch[15];for(float &v:rgba_batch)v=1.0f;
    for(unsigned i=4;i<15;i+=5) wiiu_vertex_store_rgba8(&rgba_batch[i],255,255,255,255);
    unsigned old_draws=draws,old_invalid=invalid_logs;
    gfx_gx2_draw_triangles(rgba_batch,15,1);
    assert(draws==old_draws+1&&invalid_logs==old_invalid&&last_stride==20);
    gfx_gx2_finish_render();
    float quad[20]; for(float &v:quad) v=1.0f;
    for(unsigned i=4;i<20;i+=5) wiiu_vertex_store_rgba8(&quad[i],255,0,128,255);
    uint16_t indices[6]={0,1,2,1,3,2};
    auto before_vertices=gfx_gx2_frame_profile.submitted_vertices;
    auto before_indices=gfx_gx2_frame_profile.ibo_bytes;
    gfx_gx2_draw_indexed(quad,20,indices,2);
    assert(indexed_draws==1 && gfx_gx2_frame_profile.indexed_draws==1);
    assert(gfx_gx2_frame_profile.submitted_vertices==before_vertices+4);
    assert(gfx_gx2_frame_profile.ibo_bytes==before_indices+12);
    assert(!memcmp(last_indices,indices,sizeof(indices)));
    const uint16_t *retained=last_indices;
    indices[0]=3; // Backend owns the in-flight copy, not this scratch array.
    assert(retained[0]==0);
    const float *retained_vertices=static_cast<const float*>(last_bound);
    gfx_gx2_draw_indexed(quad,20,indices,2);
    assert(last_indices!=retained&&last_bound!=retained_vertices);
    assert(retained[0]==0&&!memcmp(retained_vertices,quad,sizeof(quad)));
    old_draws=draws;
    indices[0]=4;
    gfx_gx2_draw_indexed(quad,20,indices,2);
    assert(draws==old_draws); // out-of-range index never reaches GX2
    gfx_gx2_finish_render();
    float *direct_indexed=gfx_gx2_reserve_vertices(160);
    assert(direct_indexed);
    for(unsigned i=0;i<160;i++) direct_indexed[i]=1.0f;
    uint16_t many_indices[768];
    for(unsigned i=0;i<768;i++) many_indices[i]=i%32;
    auto copied_before=gfx_gx2_frame_profile.copied_draws;
    auto direct_before=gfx_gx2_frame_profile.direct_draws;
    gfx_gx2_draw_indexed(direct_indexed,160,many_indices,256);
    assert(last_bound==direct_indexed);
    assert(gfx_gx2_frame_profile.copied_draws==copied_before);
    assert(gfx_gx2_frame_profile.direct_draws==direct_before+1);
    assert(!memcmp(last_indices,many_indices,sizeof(many_indices)));
    const uint64_t validation=gfx_gx2_frame_profile.ticks[WIIU_GX2_VALIDATE];
    current_shader_program=nullptr;
    gfx_gx2_draw_triangles(vertices,24,1);
    assert(gfx_gx2_frame_profile.ticks[WIIU_GX2_VALIDATE]==validation);
    for (auto &b : vbo_blocks) free(b.data);
    puts("PASS: direct/copy paths, timers, publication, partial commits and in-flight VBO lifetime");
}
'''


def main():
    source = (ROOT/"src/pc/gfx/gfx_gx2.cpp").read_text()
    names = ["gfx_gx2_reserve_vbo", "gfx_gx2_reserve_vertices", "gfx_gx2_submit_triangles", "gfx_gx2_draw_triangles", "gfx_gx2_draw_indexed", "gfx_gx2_flush_vbo_cache",
             "gfx_gx2_end_frame", "gfx_gx2_finish_render"]
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        path = temp/"profile.cpp"
        path.write_text(HARNESS + "\n".join(extract(source,n) for n in names) + TEST)
        exe = temp/"profile"
        subprocess.run(["c++", "-std=c++17", "-O2", "-fsanitize=address,undefined", "-no-pie", "-fno-omit-frame-pointer", "-I"+str(ROOT/"src"), str(path), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
