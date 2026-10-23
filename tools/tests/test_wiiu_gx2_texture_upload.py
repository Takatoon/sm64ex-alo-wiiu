"""Exercise actual GX2 upload/retirement code; platform calls are mocked."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(text, name):
    match = re.search(r'^(?:extern "C" |static )?(?:void|bool) ' + name + r'\([^\n]*\)\s*\{', text, re.M)
    assert match, name
    end = text.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end]


class TextureUpload(unittest.TestCase):
    def test_source_ownership_replacement_and_allocation_failure(self):
        source = (ROOT / 'src/pc/gfx/gfx_gx2.cpp').read_text()
        functions = '\n'.join(function(source, name) for name in (
            'gfx_gx2_reclaim_barrier', 'gfx_gx2_release_image',
            'gfx_gx2_reset_texture_storage', 'gfx_gx2_upload_resource'))
        code = r'''
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "src/pc/gfx/wiiu_texture_image.h"
enum { GX2_SURFACE_USE_TEXTURE=1, GX2_SURFACE_DIM_TEXTURE_2D=1,
 GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8=26, GX2_AA_MODE1X=0,
 GX2_TILE_MODE_LINEAR_ALIGNED=1, GX2_INVALIDATE_MODE_CPU_TEXTURE=1 };
struct Surface { unsigned use,dim,width,height,depth,mipLevels,format,aa,tileMode;
 unsigned swizzle,alignment,pitch,imageSize,mipLevelOffset[13]; void *image; };
struct GX2Texture { Surface surface; unsigned viewFirstMip,viewNumMips,viewFirstSlice,viewNumSlices,compMap,regs[5]; };
struct Texture { GX2Texture texture; bool texture_uploaded,sampler_set; };
static std::vector<Texture> gx2_textures(1);
static size_t texture_images;
static bool gpu_available=true,fail_allocate;
static unsigned waits,invalidations;
static unsigned current_texture_ids[2];
struct Shader { bool used_textures[2]; unsigned samplers_location[2]; };
static Shader *current_shader_program;
static void GX2DrawDone() { ++waits; }
static void GX2InitTextureRegs(GX2Texture*) {}
static void GX2SetPixelTexture(GX2Texture*,unsigned) {}
static void GX2Invalidate(unsigned,void*,unsigned) { ++invalidations; }
static void GX2CalcSurfaceSizeAndAlignment(Surface *s) {
 assert(s->tileMode==GX2_TILE_MODE_LINEAR_ALIGNED);
 s->pitch=(s->width+7)/8*8; s->imageSize=s->pitch*4*s->height; s->alignment=256;
}
static void *allocate(size_t alignment,size_t bytes) {
 if(fail_allocate)return nullptr;
 void *p=nullptr;assert(posix_memalign(&p,alignment,bytes)==0);return p;
}
#define memalign allocate
''' + functions + r'''
int main() {
 uint8_t pixels[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
 WiiUTextureImage image={pixels,sizeof(pixels),8,2,2,WIIU_TEXTURE_RGBA8};
 assert(gfx_gx2_upload_resource(0,&image));
 assert(texture_images==1 && !waits && invalidations==1);
 auto &resident=gx2_textures[0];
 auto *copy=(uint8_t*)resident.texture.surface.image;
 assert(!memcmp(copy,pixels,8) && !memcmp(copy+32,pixels+8,8));
 for(unsigned i=8;i<32;++i)assert(!copy[i]&&!copy[32+i]);
 pixels[0]=99;assert(copy[0]==1); // No borrowed source after return.
 void *previous=copy;
 image.size=15;assert(!gfx_gx2_upload_resource(0,&image));
 assert(resident.texture.surface.image==previous && texture_images==1 && !waits);
 image.size=16;fail_allocate=true;assert(!gfx_gx2_upload_resource(0,&image));
 assert(resident.texture.surface.image==previous && texture_images==1 && !waits);
 fail_allocate=false;assert(gfx_gx2_upload_resource(0,&image));
 assert(texture_images==1 && waits==1);
 assert(((uint8_t*)resident.texture.surface.image)[0]==99);
 gfx_gx2_reset_texture_storage();assert(!texture_images && waits==2);
 assert(!resident.texture_uploaded && !resident.texture.surface.image);
 // Foreground release already retired the GPU: CPU cleanup must not queue work.
 assert(gfx_gx2_upload_resource(0,&image));gpu_available=false;
 gfx_gx2_reset_texture_storage();assert(!texture_images && waits==2);
 assert(!gfx_gx2_upload_resource(1,&image));
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / 'upload.cpp').write_text(code)
            executable = path / 'upload'
            subprocess.run(['g++', '-std=c++11', '-I', str(ROOT),
                            str(path / 'upload.cpp'), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True, timeout=15)


if __name__ == '__main__':
    unittest.main()
