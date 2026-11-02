#include "dynos_voice.h"
#include "dynos_voice_manifest.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <map>
#include <set>
#include <vector>
#include <vorbis/vorbisfile.h>
extern "C" {
#include "pc/fs/fs.h"
#include "sounds.h"
#ifdef TARGET_WII_U
#include <whb/log.h>
#define VOICE_LOG(...) WHBLogPrintf(__VA_ARGS__)
#else
#define VOICE_LOG(...) do {printf(__VA_ARGS__);printf("\n");fflush(stdout);} while(0)
#endif
}
namespace {
constexpr size_t voice_limit=16*1024*1024;
struct Clip {std::vector<int16_t> pcm;unsigned rate=0;};
struct Memory {const unsigned char *data;size_t length,position;};
std::map<std::string,std::vector<std::shared_ptr<Clip>>> voices;
size_t resident;
struct Muted {uint32_t key=0;const float *source=nullptr;bool active=false;};
Muted native[2];
std::shared_ptr<Clip> playing;
const float *playing_source;
uint32_t playing_key;
double cursor;
float gain=1,pan=0.5f,pitch=1;
bool muted;
uint32_t random_state=0x4353564f;
uint32_t sound_key(uint32_t bits){return bits&(SOUNDARGS_MASK_BANK|SOUNDARGS_MASK_SOUNDID|SOUNDARGS_MASK_PRIORITY);}
int channel(uint32_t bits) {
    unsigned bank=(bits&SOUNDARGS_MASK_BANK)>>SOUNDARGS_SHIFT_BANK;
    if(bank==SOUND_BANK_VOICE)return 0;
    if(bank==SOUND_BANK_MENU&&((bits>>SOUNDARGS_SHIFT_SOUNDID)&255)==0x24)return 1;
    return -1;
}
float bounded(float value,float lo,float hi,float fallback) {
    uint32_t bits;memcpy(&bits,&value,sizeof(bits));
    if((bits&0x7f800000u)==0x7f800000u)return fallback;
    return std::max(lo,std::min(hi,value));
}
bool read_file(const std::string &path,size_t limit,std::vector<unsigned char> &data) {
    fs_file_t *file=fs_open_model(path.c_str());if(!file)return false;
    auto size=fs_size(file);bool ok=size>0&&uint64_t(size)<=limit;
    if(ok){data.resize(size_t(size));ok=fs_read(file,data.data(),data.size())==size;}
    fs_close(file);if(!ok)data.clear();return ok;
}
size_t read_memory(void *out,size_t size,size_t count,void *opaque) {
    auto &memory=*static_cast<Memory*>(opaque);if(!size)return 0;
    count=std::min(count,(memory.length-memory.position)/size);
    memcpy(out,memory.data+memory.position,size*count);memory.position+=size*count;return count;
}
int seek_memory(void *opaque,ogg_int64_t offset,int whence) {
    auto &memory=*static_cast<Memory*>(opaque);
    ogg_int64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?ogg_int64_t(memory.position):whence==SEEK_END?ogg_int64_t(memory.length):-1;
    if(base<0||offset < -base||offset > ogg_int64_t(memory.length)-base)return -1;
    memory.position=size_t(base+offset);return 0;
}
long tell_memory(void *opaque){return long(static_cast<Memory*>(opaque)->position);}
std::shared_ptr<Clip> decode(const std::vector<unsigned char> &data,size_t budget) {
    Memory memory{data.data(),data.size(),0};OggVorbis_File file{};
    ov_callbacks callbacks{read_memory,seek_memory,nullptr,tell_memory};
    if(ov_open_callbacks(&memory,&file,nullptr,0,callbacks))return {};
    struct Close {OggVorbis_File *file;~Close(){ov_clear(file);}} close{&file};
    vorbis_info *info=ov_info(&file,0);ogg_int64_t length=ov_pcm_total(&file,0);
    if(ov_streams(&file)!=1||!info||info->channels<1||info->channels>2||info->rate<8000||info->rate>96000||length<=0||length>info->rate*20||uint64_t(length)>budget/sizeof(int16_t))return {};
    auto clip=std::make_shared<Clip>();clip->rate=unsigned(info->rate);clip->pcm.reserve(size_t(length));
    int section=0;
    while(true) {
        float **pcm=nullptr;long frames=ov_read_float(&file,&pcm,2048,&section);
        if(!frames)break;
        if(frames<0||section!=0||clip->pcm.size()+frames>size_t(length))return {};
        for(long i=0;i<frames;++i) {
            float sample=pcm[0][i];if(info->channels==2)sample=(sample+pcm[1][i])*0.5f;
            clip->pcm.push_back(int16_t(bounded(sample,-1,1,0)*32767.0f));
        }
    }
    if(clip->pcm.size()!=size_t(length))return {};
    return clip;
}
}
void dynos_voice_clear(void) {
    playing.reset();voices.clear();resident=0;cursor=0;playing_source=nullptr;playing_key=0;
    // Keep already-suppressed native events silent until their next start.
    // Otherwise switching costumes could reveal the old native note's tail.
}
uint64_t dynos_voice_resident_bytes(void){return resident;}
void dynos_voice_select(const char *pack,const char *model,uint64_t budget) {
    dynos_voice_clear();budget=std::min<uint64_t>(budget,voice_limit);
    if(!pack||!model||!budget)return;
    fs_pathlist_t files=fs_enumerate_model_scripts(pack);
    std::set<std::string> paths;for(int i=0;i<files.numpaths;++i)paths.insert(files.paths[i]);fs_pathlist_free(&files);
    if(paths.empty())return;
    std::string lua;
    for(const auto &path:paths) {
        std::vector<unsigned char> data;
        if(!read_file(path,1024*1024-lua.size(),data)){VOICE_LOG("DYNOS_VOICE_REJECT path=%s reason=Lua_read_or_size_limit",path.c_str());return;}
        lua.append(data.begin(),data.end());lua+='\n';
        if(lua.size()>1024*1024){VOICE_LOG("DYNOS_VOICE_REJECT path=%s reason=Lua_size_limit",path.c_str());return;}
    }
    dynos::VoiceManifest manifest;std::string error;
    if(!dynos::voice_manifest(lua,model,manifest,error)){VOICE_LOG("DYNOS_VOICE_REJECT pack=%s reason=%s",pack,error.c_str());return;}
    std::map<std::string,std::shared_ptr<Clip>> cache;
    std::set<std::string> supported;
    for(unsigned id=0;id<256;++id)for(unsigned priority:{0x80u,0xd0u,0xf0u}){
        auto key=dynos::voice_sound_key((SOUND_BANK_VOICE<<SOUNDARGS_SHIFT_BANK)|(id<<SOUNDARGS_SHIFT_SOUNDID)|(priority<<SOUNDARGS_SHIFT_PRIORITY));
        if(!key.empty())supported.insert(key);
    }
    supported.insert("CHAR_SOUND_LETS_A_GO");
    for(const auto &event:manifest) {
        if(!supported.count(event.first))continue;
        std::vector<std::shared_ptr<Clip>> choices;
        for(const auto &name:event.second) {
            std::string relative=name.compare(0,6,"sound/")?"sound/"+name:name;
            auto known=cache.find(relative);
            if(known==cache.end()) {
                if(cache.size()==128){choices.push_back({});continue;}
                std::shared_ptr<Clip> clip;std::vector<unsigned char> data;
                if(name.size()>4&&name.substr(name.size()-4)==".ogg") {
                    std::string path=std::string(pack)+"/"+relative;
                    if(read_file(path,8*1024*1024,data))clip=decode(data,budget-resident);
                }
                if(clip)resident+=clip->pcm.size()*sizeof(int16_t);
                else VOICE_LOG("DYNOS_VOICE_FILE_REJECT pack=%s file=%s",pack,name.c_str());
                known=cache.emplace(relative,std::move(clip)).first;
            }
            // Keep NULL variant positions so a missing clip falls back to the
            // matching native sound, rather than reassigning another variant.
            choices.push_back(known->second);
        }
        voices.emplace(event.first,std::move(choices));
    }
    VOICE_LOG("DYNOS_VOICES_LOADED pack=%s model=%s events=%u files=%u bytes=%u",pack,model,unsigned(voices.size()),unsigned(cache.size()),unsigned(resident));
}
bool dynos_voice_event(uint32_t bits,const float *source,bool start,float volume,float position,float frequency) {
    int index=channel(bits);if(index<0)return false;
    auto &state=native[index];uint32_t key=sound_key(bits);
    if(start) {
        playing.reset();cursor=0;state={key,source,false};
        auto found=voices.find(dynos::voice_sound_key(bits));
        if(found!=voices.end()&&!found->second.empty()) {
            unsigned id=(bits>>SOUNDARGS_SHIFT_SOUNDID)&255;unsigned variant=0;
            if(id<3)variant=id;
            else if(id>=0x2b&&id<=0x2f)variant=id-0x2b;
            else {random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;variant=random_state;}
            playing=found->second[variant%found->second.size()];
            state.active=bool(playing);playing_key=key;playing_source=source;
        }
    }
    if(state.active&&state.key==key&&state.source==source) {
        if(playing&&playing_key==key&&playing_source==source){gain=bounded(volume,0,1,1);pan=bounded(position,0,1,0.5f);pitch=bounded(frequency,0.25f,4,1);}
        return true;
    }
    return false;
}
void dynos_voice_stop(uint32_t bits,const float *source) {
    unsigned match=SOUNDARGS_MASK_BANK|SOUNDARGS_MASK_SOUNDID;
    if(playing&&((playing_key^bits)&match)==0&&playing_source==source)playing.reset();
}
void dynos_voice_stop_source(const float *source){if(playing_source==source)playing.reset();}
void dynos_voice_stop_bank(unsigned bank){if(((playing_key&SOUNDARGS_MASK_BANK)>>SOUNDARGS_SHIFT_BANK)==bank)playing.reset();}
void dynos_voice_set_muted(bool value){muted=value;}
void dynos_voice_mix(int16_t *stereo,unsigned frames,float master_gain) {
    if(!playing||muted)return;
    master_gain=bounded(master_gain,0,1,0);
    double step=double(playing->rate)*pitch/32000.0;
    // Mono voice follows the native pan. Center retains the sample's gain.
    float left=gain*master_gain*std::min(1.0f,2*(1-pan));
    float right=gain*master_gain*std::min(1.0f,2*pan);
    for(unsigned i=0;i<frames;++i) {
        size_t frame=size_t(cursor);if(frame>=playing->pcm.size()){playing.reset();break;}
        size_t next=std::min(frame+1,playing->pcm.size()-1);
        float fraction=float(cursor-frame);
        float sample=playing->pcm[frame]+(playing->pcm[next]-playing->pcm[frame])*fraction;
        stereo[2*i]=int16_t(std::max(-32768,std::min(32767,int(stereo[2*i])+int(sample*left))));
        stereo[2*i+1]=int16_t(std::max(-32768,std::min(32767,int(stereo[2*i+1])+int(sample*right))));
        cursor+=step;
    }
}
