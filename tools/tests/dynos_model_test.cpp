#include "pc/dynos/dynos_model.h"
#include <fstream>
#include <iostream>
#include <iterator>
static uintptr_t resolver(unsigned index) {
    return index<=10||(index>=12&&index<=15)||(index>=36&&index<=49&&index!=46)?0x1000+index:0;
}
int main(int argc,char **argv) {
    if(argc<2||argc>4)return 1;
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),{});
    std::vector<int> hierarchy;
    if(argc>=3){std::ifstream expected(argv[2]);int parent;while(expected>>parent)hierarchy.push_back(parent);}
    std::string error;auto model=dynos::load(data.data(),data.size(),"test",argc==4?argv[3]:"mario",resolver,error,argc>=3?&hierarchy:nullptr,argc==4);
    if(!model){std::cerr<<error<<"\n";return 2;}
    // The command opcode must occupy the first byte in native memory on both
    // little-endian hosts and the big-endian PPC build.
    unsigned op=reinterpret_cast<const uint8_t*>(model->root->layout.data())[0];
    if(op!=0x0b&&op!=0x16&&op!=0x20)return 3;
    auto ambient=model->records.find("test_ambient");
    if(ambient!=model->records.end()) {
        const auto &b=ambient->second->light;
        if(b[0]!=1||b[1]!=2||b[2]!=3||b[3]!=0||b[4]!=4||b[5]!=5||b[6]!=6)return 4;
    }
    auto vertex=model->records.find("test_vertex");
    if(vertex!=model->records.end()) {
        if(vertex->second->vertices.size()!=1)return 11;
        const auto &v=vertex->second->vertices[0].n;
        if(v.ob[0]!=-3||v.ob[1]!=17||v.ob[2]!=-200||v.tc[0]!=-50||v.tc[1]!=31||v.n[0]!=-128)return 5;
    }
    auto alias=model->records.find("test_alias");
    if(alias!=model->records.end()) {
        const auto &target=*model->records.at("test_texture");
        const auto &dl=model->records.at("test_alias_dl")->display_list;
        if(alias->second->texture_path!=target.texture_path||target.texture_path.empty()||model->texture_count!=1)return 12;
        if((dl[0].words.w0>>24)!=0xfd||dl[0].words.w1!=reinterpret_cast<uintptr_t>(alias->second->texture_path.c_str()))return 13;
    }
    auto secondary=model->records.find("test_unused_secondary");
    if(secondary!=model->records.end()&&secondary->second->display_list[2].words.w0!=0)return 14;
    auto palette=model->records.find("test_palette");
    if(palette!=model->records.end()) {
        const auto &dl=palette->second->display_list;
        if(dl.size()!=5)return 6;
        for(unsigned i:{0u,2u})if((dl[i].words.w0>>24)!=0xdb||dl[i].words.w1!=24)return 7;
        if(dl[1].words.w1!=reinterpret_cast<uintptr_t>(&model->palette[0].l[0])||
           dl[3].words.w1!=reinterpret_cast<uintptr_t>(&model->palette[0].a))return 8;
    }
    auto real_palette=model->records.find("test_real_palette");
    if(real_palette!=model->records.end()) {
        const auto &dl=real_palette->second->display_list;
        if(dl.size()!=9)return 9;
        for(unsigned part=0;part<2;part++) {
            if(dl[part*4+1].words.w1!=reinterpret_cast<uintptr_t>(&model->palette[part].l[0])||
               dl[part*4+3].words.w1!=reinterpret_cast<uintptr_t>(&model->palette[part].a))return 10;
        }
    }
    std::cout<<"accepted vertices="<<model->vertex_count<<" textures="<<model->texture_count<<" bytes="<<model->memory_bytes<<"\n";
    return 0;
}
