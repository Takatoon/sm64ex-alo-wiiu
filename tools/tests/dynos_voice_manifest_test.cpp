#include "pc/dynos/dynos_voice_manifest.h"
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc,char **argv) {
    if(argc!=3)return 1;
    std::ifstream file(argv[1]);std::string lua((std::istreambuf_iterator<char>(file)),{});
    dynos::VoiceManifest manifest;std::string error;
    if(!dynos::voice_manifest(lua,argv[2],manifest,error)){std::cerr<<error;return 2;}
    for(const auto &entry:manifest){std::cout<<entry.first;for(const auto &path:entry.second)std::cout<<'\t'<<path;std::cout<<'\n';}
}
