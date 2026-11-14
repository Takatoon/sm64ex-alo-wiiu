#!/usr/bin/env python3
"""Check actual generated fetch layouts and canonical RGBA bytes on the host."""
from pathlib import Path
import subprocess
import tempfile
import re

ROOT=Path(__file__).resolve().parents[2]

def function(source,name):
    match=re.search(r'^(?:int|void) '+name+r'\(',source,re.M)
    assert match,name
    start=match.start();pos=start
    brace=source.index('{',pos);end=brace+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]

STUBS=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include "pc/gfx/wiiu_vertex_format.h"
enum {GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32=1,GX2_ATTRIB_FORMAT_FLOAT_32_32,
GX2_ATTRIB_FORMAT_UNORM_8_8_8_8,GX2_ATTRIB_INDEX_PER_VERTEX,
GX2_ENDIAN_SWAP_DEFAULT,GX2_ENDIAN_SWAP_NONE,GX2_FETCH_SHADER_TESSELLATION_NONE,
GX2_TESSELLATION_MODE_DISCRETE,GX2_INVALIDATE_MODE_CPU_SHADER};
#define GX2_COMP_SEL(a,b,c,d) 123
#define GX2_SHADER_PROGRAM_ALIGNMENT 64
typedef struct {uint32_t location,buffer,offset,format,type,aluDivisor,mask,endianSwap;} GX2AttribStream;
struct Shader {void *program;uint32_t size;};
struct Fetch {void *program;uint32_t size,count;GX2AttribStream attrs[7];};
struct ShaderGroup {struct Shader vertexShader,pixelShader;struct Fetch fetchShader,packedFetchShader;uint32_t packedVertexStride,numAttributes;GX2AttribStream attributes[7];};
struct CCFeatures {bool used_textures[2],opt_fog;int num_inputs;};
static int alloc_number,fail_alloc;
static void *test_memalign(size_t a,size_t n) {return ++alloc_number==fail_alloc?NULL:memalign(a,n);}
#define memalign test_memalign
static int generatePixelShader(struct Shader *s,struct CCFeatures *f) {(void)f;s->program=malloc(64);s->size=64;return 0;}
static int generateVertexShader(struct Shader *s,struct CCFeatures *f) {return generatePixelShader(s,f);}
static uint32_t GX2CalcFetchShaderSizeEx(unsigned n,int a,int b) {(void)a;(void)b;return n*64;}
static void GX2InitFetchShaderEx(struct Fetch *f,void *p,unsigned n,GX2AttribStream *attrs,int a,int b) {
 (void)p;(void)a;(void)b;f->count=n;memcpy(f->attrs,attrs,n*sizeof(*attrs));
}
static void GX2Invalidate(int a,void *p,size_t n) {(void)a;(void)p;(void)n;}
void gx2FreeShaderGroup(struct ShaderGroup *g);
'''
TEST=r'''
int main(void) {
 for(unsigned v=0;v<256;v++) {
  float word;wiiu_vertex_store_rgba8(&word,v,255-v,37,v^42);
  const uint8_t *p=(const uint8_t *)&word;
  assert(p[0]==v&&p[1]==255-v&&p[2]==37&&p[3]==(v^42));
 }
 for(unsigned textures=0;textures<4;textures++) for(unsigned fog=0;fog<2;fog++) for(int inputs=0;inputs<=4;inputs++) {
  struct CCFeatures f={{textures&1,textures&2},fog,inputs};struct ShaderGroup g;
  assert(gx2GenerateShaderGroup(&g,&f)==0);
  unsigned offset=0;
  for(unsigned i=0;i<g.numAttributes;i++) {
   GX2AttribStream a=g.packedFetchShader.attrs[i];
   assert(a.offset==offset&&a.location==g.attributes[i].location);
   assert(g.fetchShader.attrs[i].offset==16*i);
   if(a.location>=2) {
    assert(a.format==GX2_ATTRIB_FORMAT_UNORM_8_8_8_8&&a.endianSwap==GX2_ENDIAN_SWAP_NONE);offset+=4;
   } else {
    assert(a.endianSwap==GX2_ENDIAN_SWAP_DEFAULT);
    assert(a.format==(a.location==0?GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32:GX2_ATTRIB_FORMAT_FLOAT_32_32));offset+=16;
   }
  }
  assert(offset==g.packedVertexStride);
  assert(offset==16+(textures?16:0)+4*(fog+inputs));
  gx2FreeShaderGroup(&g);
 }
 for(int fail=1;fail<=2;fail++) {
  struct CCFeatures f={{1,0},1,4};struct ShaderGroup g;
  alloc_number=0;fail_alloc=fail;
  assert(gx2GenerateShaderGroup(&g,&f)==-1); // sanitizer verifies failure cleanup
 }
 puts("PASS: 40 fetch-layout combinations, RGBA byte order and allocation failure cleanup");
}
'''
def main():
    source=(ROOT/'src/pc/gfx/gx2_shader_gen.c').read_text()
    with tempfile.TemporaryDirectory() as temp:
        p=Path(temp);c=p/'layout.c';exe=p/'layout'
        c.write_text(STUBS+function(source,'gx2GenerateShaderGroup')+function(source,'gx2FreeShaderGroup')+TEST)
        subprocess.run(['cc','-O2','-fsanitize=address,undefined','-no-pie','-I'+str(ROOT/'src'),str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
