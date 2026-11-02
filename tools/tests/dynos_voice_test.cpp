#include "pc/dynos/dynos_voice.h"
#include "pc/dynos/dynos_voice_manifest.h"
#include <vorbis/vorbisenc.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
extern "C" {
#include "sm64.h"
#include "sounds.h"
#include "pc/fs/fs.h"
}
namespace fs=std::filesystem;
static void write_text(const fs::path &file,const char *text){std::ofstream(file)<<text;}
static void encode(const fs::path &file,unsigned rate,unsigned channels,unsigned frames) {
    vorbis_info info;vorbis_info_init(&info);assert(!vorbis_encode_init_vbr(&info,channels,rate,0.1f));
    vorbis_comment comment;vorbis_comment_init(&comment);
    vorbis_dsp_state dsp;assert(!vorbis_analysis_init(&dsp,&info));vorbis_block block;assert(!vorbis_block_init(&dsp,&block));
    ogg_stream_state stream;assert(!ogg_stream_init(&stream,197));
    ogg_packet h,c,b;assert(!vorbis_analysis_headerout(&dsp,&comment,&h,&c,&b));
    ogg_stream_packetin(&stream,&h);ogg_stream_packetin(&stream,&c);ogg_stream_packetin(&stream,&b);
    std::ofstream out(file,std::ios::binary);ogg_page page;
    auto flush=[&](){while(ogg_stream_flush(&stream,&page)){out.write(reinterpret_cast<char*>(page.header),page.header_len);out.write(reinterpret_cast<char*>(page.body),page.body_len);}};
    flush();
    for(unsigned i=0;i<frames;) {
        unsigned count=std::min(1024u,frames-i);float **buffer=vorbis_analysis_buffer(&dsp,count);
        for(unsigned channel=0;channel<channels;++channel)for(unsigned j=0;j<count;++j)buffer[channel][j]=0.6f*std::sin((i+j)*440.0f*6.2831853f/rate);
        vorbis_analysis_wrote(&dsp,count);i+=count;
        while(vorbis_analysis_blockout(&dsp,&block)==1){vorbis_analysis(&block,nullptr);vorbis_bitrate_addblock(&block);ogg_packet packet;while(vorbis_bitrate_flushpacket(&dsp,&packet)){ogg_stream_packetin(&stream,&packet);flush();}}
    }
    vorbis_analysis_wrote(&dsp,0);
    while(vorbis_analysis_blockout(&dsp,&block)==1){vorbis_analysis(&block,nullptr);vorbis_bitrate_addblock(&block);ogg_packet packet;while(vorbis_bitrate_flushpacket(&dsp,&packet)){ogg_stream_packetin(&stream,&packet);flush();}}
    ogg_stream_clear(&stream);vorbis_block_clear(&block);vorbis_dsp_clear(&dsp);vorbis_comment_clear(&comment);vorbis_info_clear(&info);
}
static bool nonzero(const std::vector<int16_t> &data){for(auto sample:data)if(sample)return true;return false;}
int main(int argc,char **argv) {
    assert(argc==2||argc==3);
    if(argc==3) {
        const char *ro[]={nullptr};assert(fs_init(ro,"absent",argv[1]));
        assert(fs_mount((fs::path(argv[1])/"voices.zip").string().c_str()));
        assert(fs_set_texture_pack("")); // model/voice ZIPs remain available with base textures
        dynos_voice_select("Packed","mario_geo",16*1024*1024);
        assert(dynos_voice_resident_bytes()>0);float source[3]{};
        assert(dynos_voice_event(SOUND_MARIO_HERE_WE_GO,source,true,1,0.5f,1));
        std::vector<int16_t> pcm(256);dynos_voice_mix(pcm.data(),128,1);assert(nonzero(pcm));
        dynos_voice_clear();assert(dynos_voice_resident_bytes()==0);return 0;
    }
    fs::path root=argv[1],pack=root/"dynos/packs/Voice";fs::create_directories(pack/"sound");
    encode(pack/"sound/jump.ogg",44100,2,44100/5);
    write_text(pack/"sound/bad.ogg","not an Ogg stream");
    write_text(pack/"main.lua",R"(local MODEL = smlua_model_util_get_id('mario_geo')
local V = {
 [CHAR_SOUND_YAH_WAH_HOO] = {'jump.ogg','missing.ogg','bad.ogg'},
 [CHAR_SOUND_HERE_WE_GO] = 'sound/jump.ogg',
 [CHAR_SOUND_LETS_A_GO] = audio_sample_load('jump.ogg')
}
_G.charSelect.character_add_voice(MODEL, V)
)");
    const char *ro[]={nullptr};assert(fs_init(ro,"absent",argv[1]));
    const char *path="dynos/packs/Voice";float source[3]{},other[3]{};
    dynos_voice_select(path,"mario_geo",16*1024*1024);
    assert(dynos_voice_resident_bytes()==44100/5*2); // shared clip decoded only once
    auto event=[&](unsigned bits,bool start=true,float gain=1,float pan=0.5f,float pitch=1){return dynos_voice_event(bits,source,start,gain,pan,pitch);};
    auto mix=[&](unsigned frames=128,float gain=1){std::vector<int16_t> buffer(frames*2);dynos_voice_mix(buffer.data(),frames,gain);return buffer;};
    assert(event(SOUND_MARIO_YAH_WAH_HOO));auto pcm=mix();assert(nonzero(pcm));for(size_t i=0;i<pcm.size();i+=2)assert(pcm[i]==pcm[i+1]);
    assert(!event(SOUND_MARIO_YAH_WAH_HOO+(1u<<16)));assert(!nonzero(mix())); // missing variant: native fallback
    assert(!event(SOUND_MARIO_YAH_WAH_HOO+(2u<<16)));assert(!nonzero(mix())); // corrupt variant
    assert(!event(SOUND_ACTION_TERRAIN_JUMP));assert(!event(0x2428ff81)); // effects and Peach unchanged
    assert(!event(SOUND_MARIO_DYING)); // omitted key
    assert(event(SOUND_MARIO_HERE_WE_GO));auto same=mix();assert(same==pcm);
    assert(event(SOUND_MARIO_HERE_WE_GO,false));auto following=mix();assert(following!=pcm); // update must not restart clip
    assert(event(SOUND_MARIO_HERE_WE_GO,true,1,0));pcm=mix();for(size_t i=0;i<pcm.size();i+=2)assert(pcm[i+1]==0);assert(nonzero(pcm));
    assert(event(SOUND_MARIO_HERE_WE_GO,true,0.5f));auto quiet=mix();assert(event(SOUND_MARIO_HERE_WE_GO));auto loud=mix();for(size_t i=0;i<quiet.size();++i)assert(std::abs(int(quiet[i])*2-loud[i])<=2);
    assert(event(SOUND_MARIO_HERE_WE_GO));assert(!nonzero(mix(128,0)));
    assert(event(SOUND_MARIO_HERE_WE_GO));dynos_voice_set_muted(true);assert(!nonzero(mix()));dynos_voice_set_muted(false);assert(mix()==loud); // muted cursor freezes
    assert(event(SOUND_MARIO_HERE_WE_GO));dynos_voice_stop(SOUND_MARIO_HERE_WE_GO,other);assert(nonzero(mix()));dynos_voice_stop(SOUND_MARIO_HERE_WE_GO,source);assert(!nonzero(mix()));
    assert(event(SOUND_MARIO_HERE_WE_GO));dynos_voice_stop_source(other);assert(nonzero(mix()));dynos_voice_stop_source(source);assert(!nonzero(mix()));
    assert(event(SOUND_MENU_STAR_SOUND_LETS_A_GO));dynos_voice_stop_bank(SOUND_BANK_VOICE);assert(nonzero(mix()));dynos_voice_stop_bank(SOUND_BANK_MENU);assert(!nonzero(mix()));
    assert(event(SOUND_MARIO_HERE_WE_GO));std::vector<int16_t> saturated(256,32000);dynos_voice_mix(saturated.data(),128,1);bool clipped=false;for(auto x:saturated)if(x==32767)clipped=true;assert(clipped);
    assert(event(SOUND_MARIO_HERE_WE_GO));mix(6500);assert(!nonzero(mix()));assert(event(SOUND_MARIO_HERE_WE_GO,false)); // native tail stays silent
    assert(event(SOUND_MARIO_HERE_WE_GO,true,1,0.5f,2));mix(3250);assert(!nonzero(mix())); // output-rate resampling plus pitch
    assert(event(SOUND_MARIO_HERE_WE_GO));dynos_voice_clear();assert(dynos_voice_resident_bytes()==0);assert(!nonzero(mix()));assert(event(SOUND_MARIO_HERE_WE_GO,false));
    dynos_voice_select(path,"luigi_geo",16*1024*1024);assert(dynos_voice_resident_bytes()==0);assert(!event(SOUND_MARIO_HERE_WE_GO));
    dynos_voice_select(path,"mario_geo",2);assert(dynos_voice_resident_bytes()==0);assert(!event(SOUND_MARIO_HERE_WE_GO)); // budget before decoding
    write_text(pack/"main.lua","local V={[CHAR_SOUND_HERE_WE_GO]='../outside.ogg'}");dynos_voice_select(path,"mario_geo",16*1024*1024);assert(dynos_voice_resident_bytes()==0);
    assert(dynos::voice_sound_key(SOUND_MARIO_OOOF2)=="CHAR_SOUND_OOOF2");
    assert(dynos::voice_sound_key(SOUND_MARIO_UH_LEDGE_CLIMB_FAST)=="CHAR_SOUND_UH2_2");
    assert(dynos::voice_sound_key(SOUND_MARIO_UH_LONG_JUMP_LAND)=="CHAR_SOUND_UH2");
    dynos_voice_clear();
}
