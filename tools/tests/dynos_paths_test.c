#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pc/fs/fs.h"
extern fs_packtype_t fs_packtype_dir;
static char resources[2048];
static unsigned walks;
static fs_walk_result_t (*real_walk)(void *,const char *,walk_fn_t,void *,bool);
static fs_walk_result_t scoped_walk(void *pack,const char *base,walk_fn_t callback,void *user,bool recursive){
    assert(!strcmp((const char*)pack,resources));
    assert(!strcmp(base,"dynos/packs")&&recursive);++walks;
    return real_walk(pack,base,callback,user,recursive);
}
static fs_walk_result_t script_walk(void *pack,const char *base,walk_fn_t callback,void *user,bool recursive){
    assert(!strcmp((const char*)pack,resources));
    assert(!strcmp(base,"dynos/packs/Loose")&&!recursive);++walks;
    return real_walk(pack,base,callback,user,recursive);
}
static char first(const char *path){
    fs_file_t *file=fs_open_model(path);assert(file);char c;assert(fs_read(file,&c,1)==1);fs_close(file);return c;
}
int main(int argc,char **argv){
    assert(argc==2);const char *ro[]={NULL};
#ifdef WIIU_LEGACY_PATHS
    snprintf(resources,sizeof(resources),"%s/sm64ex_res",argv[1]);
#else
    snprintf(resources,sizeof(resources),"%s",argv[1]);
#endif
    assert(fs_init(ro,FS_BASEDIR,argv[1]));
#ifndef WIIU_LEGACY_PATHS
    char archive[2048];
    snprintf(archive,sizeof(archive),"%s/mods/Actors.zip",argv[1]);assert(fs_mount(archive));
    snprintf(archive,sizeof(archive),"%s/mods/Texture.zip",argv[1]);assert(fs_mount(archive));
    snprintf(archive,sizeof(archive),"%s/base.zip",argv[1]);assert(fs_mount(archive));
    // Match the Wii U initializer: the writable application root mounts last.
    assert(fs_unmount(argv[1])&&fs_mount(argv[1]));
#endif
    real_walk=fs_packtype_dir.walk;fs_packtype_dir.walk=scoped_walk;
    fs_pathlist_t files=fs_enumerate_models();fs_packtype_dir.walk=real_walk;
    assert(walks==1&&files.numpaths==3);
    unsigned loose=0,zip=0,manta=0;
    for(int i=0;i<files.numpaths;++i){
        if(!strcmp(files.paths[i],"dynos/packs/Loose/mario_geo.bin"))++loose;
        else if(!strcmp(files.paths[i],"Classic/luigi_geo.bin"))++zip;
        else if(!strcmp(files.paths[i],"dynos/packs/Sea/manta_seg5_geo_05008D14.bin"))++manta;
        else assert(0);
    }
    assert(loose==1&&zip==1&&manta==1);fs_pathlist_free(&files);
    assert(first("dynos/packs/Loose/mario_geo.bin")=='L');
    assert(first("dynos/packs/Loose/main.lua")=='S');
    walks=0;fs_packtype_dir.walk=script_walk;
    fs_pathlist_t scripts=fs_enumerate_model_scripts("dynos/packs/Loose");fs_packtype_dir.walk=real_walk;
    assert(walks==1&&scripts.numpaths==1&&!strcmp(scripts.paths[0],"dynos/packs/Loose/main.lua"));fs_pathlist_free(&scripts);
    scripts=fs_enumerate_model_scripts("Classic");assert(scripts.numpaths==1&&!strcmp(scripts.paths[0],"Classic/main.lua"));fs_pathlist_free(&scripts);
    assert(first("Classic/sound/jump.ogg")=='V');
    scripts=fs_enumerate_model_scripts("../escape");assert(scripts.numpaths==0);fs_pathlist_free(&scripts);
    assert(first("dynos/packs/Loose/sound/jump.ogg")=='V');
    assert(first("Classic/luigi_geo.bin")=='Z');
    assert(!fs_open_model("outside/luigi_geo.bin"));
    assert(!fs_open_model("../unrelated/toad_geo.bin"));
    assert(!fs_open_model("dynos/packs/../../../sm64config.txt"));
    assert(!fs_open_model("sd:/unrelated/toad_geo.bin"));
#ifdef WIIU_LEGACY_PATHS
    assert(!fs_open_model("dynos/packs/OnlyRoot/wario_geo.bin"));
    assert(!fs_open("dynos/packs/OnlyRoot/wario_geo.bin"));
    assert(!fs_open("./dynos/packs/OnlyRoot/wario_geo.bin"));
    assert(!fs_is_file("dynos/packs/OnlyRoot/wario_geo.bin"));
    assert(!fs_is_dir("dynos/packs/OnlyRoot"));
    fs_file_t *canonical=fs_open("dynos/packs/Loose/mario_geo.bin");assert(canonical);
    char c;assert(fs_read(canonical,&c,1)==1&&c=='L');fs_close(canonical);
    fs_pathlist_t general=fs_enumerate("",true);
    for(int i=0;i<general.numpaths;++i){
        assert(!strstr(general.paths[i],"OnlyRoot"));
        assert(strncmp(general.paths[i],"sm64ex_res/",12));
    }
    fs_pathlist_free(&general);
#endif
    assert(!fs_is_file("packs/Loose/mario_geo.bin"));
    fs_file_t *config=fs_open("sm64config.txt");assert(config);fs_close(config);
    assert(fs_set_texture_pack(NULL));
#ifdef WIIU_LEGACY_PATHS
    assert(!fs_is_file("gfx/only_root.bin"));
    fs_pathlist_t textures=fs_texture_packs();assert(textures.numpaths==1);fs_pathlist_free(&textures);
#endif
    return 0;
}
