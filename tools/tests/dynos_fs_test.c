/* Exercise the actual virtual filesystem used for DynOS discovery/loading. */
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "pc/fs/fs.h"
int main(int argc,char **argv) {
    assert(argc==3);
    const char *ro[]={NULL};
    assert(fs_init(ro,"absent",argv[1]));
    assert(fs_mount(argv[2]));
    fs_pathlist_t list=fs_enumerate("",true);
    unsigned loose=0,zip=0;
    for(int i=0;i<list.numpaths;i++) {
        const char *path=list.paths[i];
        while(*path=='/')path++;
        if(!strcmp(path,"dynos/packs/Loose/mario_geo.bin"))loose++;
        else if(!strcmp(path,"ZipMario/mario_geo.bin"))zip++;
        else continue;
        fs_file_t *file=fs_open(path);assert(file);
        assert(fs_size(file)==12);
        char data[12];assert(fs_read(file,data,sizeof(data))==12);
        assert(!memcmp(data,"MODEL_BINARY",12));fs_close(file);
    }
    assert(loose==1&&zip==1);
    fs_pathlist_free(&list);
    return 0;
}
