#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pc/fs/fs.h"
static char first(const char *path){uint64_t size=0;char *bytes=fs_load_file(path,&size);assert(bytes&&size);char c=bytes[0];free(bytes);return c;}
int main(int argc,char **argv){
    assert(argc==2);const char *ro[]={NULL};assert(fs_init(ro,"absent",argv[1]));
    char base[2048],a[2048],b[2048],models[2048],folder[2048];
    snprintf(base,sizeof(base),"%s/base.zip",argv[1]);snprintf(a,sizeof(a),"%s/A.zip",argv[1]);
    snprintf(b,sizeof(b),"%s/B.zip",argv[1]);snprintf(models,sizeof(models),"%s/models.zip",argv[1]);
    snprintf(folder,sizeof(folder),"%s/0.v1",argv[1]);
    assert(fs_mount(b)&&fs_mount(a)&&fs_mount(base)&&fs_mount(models)&&fs_mount(folder));
    fs_pathlist_t packs=fs_texture_packs();assert(packs.numpaths==3);
    for(int i=0;i<packs.numpaths;++i)assert(strcmp(packs.paths[i],base)&&strcmp(packs.paths[i],models));
    fs_pathlist_free(&packs);
    assert(fs_set_texture_pack(NULL));assert(first("gfx/shared.bin")=='B');
    assert(first("gfx/only_a.bin")=='A');assert(first("gfx/base_only.bin")=='0');
    assert(first("gfx/extra.bin")=='F');
    assert(fs_set_texture_pack(folder));assert(first("gfx/shared.bin")=='0');assert(first("gfx/extra.bin")=='F');
    assert(fs_set_texture_pack(a));assert(first("gfx/shared.bin")=='A');assert(first("gfx/base_only.bin")=='0');
    fs_file_t *open=fs_open("gfx/shared.bin");assert(open);
    assert(fs_set_texture_pack(""));assert(first("gfx/shared.bin")=='0');
    assert(!fs_is_file("gfx/only_a.bin"));char c;assert(fs_read(open,&c,1)==1&&c=='A');fs_close(open);
    assert(first("dynos/packs/Zip/mario_geo.bin")=='M');
    assert(!fs_set_texture_pack("missing.zip"));assert(first("gfx/shared.bin")=='0');
    for(int i=0;i<50;++i){assert(fs_set_texture_pack(b));assert(first("gfx/shared.bin")=='B');assert(fs_set_texture_pack(""));assert(first("gfx/shared.bin")=='0');}
    return 0;
}
