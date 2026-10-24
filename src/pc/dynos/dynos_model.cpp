/* DynOS binary format reference: Render96/Render96ex (alpha),
 * data/dynos_gfx_load.cpp; coop-deluxe/sm64coopdx, data/dynos_bin_*.cpp.
 * This independent reader builds native pointers/commands, including PPC
 * byte order. Only validated Mario-compatible model geometry is accepted.
 */
#include "dynos_model.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <set>
#include <functional>
#include <cmath>
extern "C" {
#include <tinfl.h>
#include "platform_info.h"
}

namespace dynos {
namespace {
constexpr size_t MAX_BYTES = 32 * 1024 * 1024;
constexpr uint32_t PNTR = 0x52544e50, FUNC = 0x434e5546;
void require(bool condition, const std::string &error) {
    if (!condition) throw std::runtime_error(error);
}
struct Reader {
    const uint8_t *data;
    size_t size, pos = 0;
    const uint8_t *take(size_t n) {
        require(n <= size - pos, "truncated binary at " + std::to_string(pos));
        const uint8_t *p = data + pos; pos += n; return p;
    }
    uint8_t u8() { return *take(1); }
    uint16_t u16() { const auto p = take(2); return p[0] | (uint16_t(p[1]) << 8); }
    uint32_t u32() { const auto p = take(4); return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
    float f32() {
        uint32_t bits=u32();
        // Wii U uses -ffast-math, which can optimize away isfinite checks.
        require((bits&0x7f800000u)!=0x7f800000u,"non-finite vertex coordinate");
        float value;memcpy(&value,&bits,sizeof(value));return value;
    }
    uint32_t count(size_t maximum) { uint32_t n = u32(); require(n <= maximum, "record size limit at "+std::to_string(pos)+" ("+std::to_string(n)+" > "+std::to_string(maximum)+")"); return n; }
    std::string name() {
        auto n = u8(); const auto p = take(n);
        require(n != 0 && !memchr(p, 0, n), "empty or invalid name");
        return std::string(reinterpret_cast<const char *>(p), n);
    }
    Word word() {
        Word w; w.value = u32();
        if (w.value == PNTR) { w.kind = 1; w.name = name(); w.offset = u32(); }
        else if (w.value == FUNC) { w.kind = 2; w.value = u32(); }
        else require(w.value != 0x5641554c && w.value != 0x52584554, "Lua/external symbol reference unsupported");
        return w;
    }
};
Record &record(Model &model, const Word &w, unsigned type) {
    require(w.kind == 1, "expected named reference");
    auto it = model.records.find(w.name);
    require(it != model.records.end() && it->second->type == type, "invalid reference: " + w.name);
    return *it->second;
}
uintptr_t bbh(uint32_t w) {
#if IS_BIG_ENDIAN
    uintptr_t v = ((w & 255) << 24) | ((w & 0xff00) << 8) | (w >> 16);
#if IS_64_BIT
    v <<= 32;
#endif
    return v;
#else
    return w;
#endif
}
uintptr_t hh(uint32_t w) {
#if IS_BIG_ENDIAN
    uintptr_t v = (w << 16) | (w >> 16);
#if IS_64_BIT
    v <<= 32;
#endif
    return v;
#else
    return w;
#endif
}
uintptr_t full_word(uint32_t w) {
#if IS_BIG_ENDIAN && IS_64_BIT
    return uintptr_t(w) << 32;
#else
    return w;
#endif
}
void palette(Model &m, const std::string &slot) {
    uint32_t colors[9] = {0x0000ff,0xff0000,0xffffff,0x721c0e,0x730600,0xfec179,0xff0000,0xffffff,0xffffff};
    if (slot == "luigi") { colors[0]=0x0000a0; colors[1]=colors[6]=0x00a000; }
    if (slot == "wario") { colors[0]=0x8000a0; colors[1]=colors[6]=0xffd000; }
    if (slot == "waluigi") { colors[0]=0x201030; colors[1]=colors[6]=0x8000a0; }
    for (unsigned i=0;i<9;i++) {
        uint8_t *p = reinterpret_cast<uint8_t *>(&m.palette[i]);
        for (unsigned c=0;c<3;c++) {
            auto v = uint8_t(colors[i] >> (16-8*c));
            p[c]=p[c+4]=v/2; p[c+8]=p[c+12]=v;
        }
        p[16]=p[17]=p[18]=40;
    }
}
void read_records(Model &m, Reader &r, const std::string &key) {
    size_t records=0, commands=0, geo_words=0;
    while (r.pos < r.size) {
        require(++records <= 8192, "too many records");
        unsigned type = r.u8();
        // Legacy files may terminate with a single zero byte.
        if (!type) { require(r.pos==r.size,"trailing binary data"); break; }
        auto name = r.name();
        if (type == 8) { // Animations retained in the file, not installed.
            r.take(16); auto n=r.count(MAX_BYTES/2); r.take(size_t(n)*2);
            n=r.count(MAX_BYTES/2); r.take(size_t(n)*2); continue;
        }
        if (type == 9) continue; // Animation-table entry's name is its payload.
        if (type == 10) { r.take(5); continue; } // Legacy dynamic-command marker.
        std::unique_ptr<Record> ptr(new Record);
        Record &n=*ptr; n.type=type; n.name=name;
        require(m.records.count(name)==0, "duplicate symbol: "+name);
        switch (type) {
            case 1: case 24: memcpy(n.light.data(),r.take(24),24); break;
            case 18: memcpy(n.light.data(),r.take(12),12); break;
            case 19: {
                auto data=r.take(6);
                memcpy(n.light.data(),data,3);memcpy(n.light.data()+4,data+3,3);
                break;
            }
            case 2: {
                auto length=r.u32();
                if(length==0x52584554) { n.texture_alias=r.name(); break; }
                require(length<=8*1024*1024,"texture size limit");
                const auto p=r.take(length); n.bytes.assign(p,p+length);
                if (length) {
                    require(length>=33 && memcmp(p,"\x89PNG\r\n\x1a\n",8)==0, "texture is not embedded PNG: "+name);
                    auto be=[](const uint8_t *b){return (uint32_t(b[0])<<24)|(uint32_t(b[1])<<16)|(uint32_t(b[2])<<8)|b[3];};
                    auto width=be(p+16),height=be(p+20);
                    require(width && height && width<=2048 && height<=2048, "texture dimensions exceed limit");
                    m.memory_bytes+=size_t(width)*height*4;
                    n.texture_path="__dynos/"+key+"/"+std::to_string(m.texture_count++);
                }
                break;
            }
            case 3: {
                auto count=r.count(262144); bool floating=false;
                n.vertices.reserve(count);
                for (uint32_t index=0;index<count;index++) {
                    Vtx v{};
                    for(int i=0;i<3;i++) {
                        if(floating) {
                            float coordinate=r.f32();
                            float rounded=std::round(coordinate);
                            require(rounded>=-32768.f&&rounded<=32767.f,"vertex coordinate outside native range");
                            v.n.ob[i]=int16_t(rounded);
                        } else v.n.ob[i]=int16_t(r.u16());
                    }
                    v.n.flag=r.u16(); v.n.tc[0]=int16_t(r.u16());v.n.tc[1]=int16_t(r.u16());
                    for(int i=0;i<3;i++)v.n.n[i]=int8_t(r.u8()); v.n.a=r.u8();
                    if(!floating&&v.n.ob[0]==0x3346&&v.n.ob[1]==0x5632&&v.n.ob[2]==0x5854) {
                        floating=true; // Format marker counts as an entry, not as geometry.
                    } else {
                        require(++m.vertex_count<=262144,"vertex limit");
                        n.vertices.push_back(v);
                    }
                }
                break;
            }
            case 4: {
                auto count=r.count(262144); commands+=count; require(commands<=262144,"display-list limit");
                // Reserve native expansion space before resolving any pointer.
                // Palette copies need a NUMLIGHT command to dirty light state.
                n.display_list.resize(size_t(count)*2); n.words.reserve(size_t(count)*2);
                for(uint32_t i=0;i<count;i++) { Word w;w.value=r.u32(); n.words.push_back(w); n.words.push_back(r.word()); }
                break;
            }
            case 5: {
                auto count=r.count(65536);geo_words+=count;require(geo_words<=65536,"geometry limit");
                n.layout.resize(count);for(uint32_t i=0;i<count;i++)n.words.push_back(r.word());
                m.root=&n;break;
            }
            default: throw std::runtime_error("unsupported DynOS record type "+std::to_string(type));
        }
        m.records.emplace(name,std::move(ptr));
    }
    require(m.root!=nullptr,"no model geometry");
    std::set<Record*> active;
    std::function<void(Record&,unsigned)> resolve_texture=[&](Record &texture,unsigned depth) {
        if(texture.texture_alias.empty())return;
        require(depth<32&&active.insert(&texture).second,"cyclic texture alias");
        auto target=m.records.find(texture.texture_alias);
        require(target!=m.records.end()&&target->second->type==2,"invalid texture alias");
        resolve_texture(*target->second,depth+1);
        texture.texture_path=target->second->texture_path;
        texture.texture_alias.clear();active.erase(&texture);
    };
    for(auto &entry:m.records)if(entry.second->type==2)resolve_texture(*entry.second,0);
}
void fix_display_lists(Model &m) {
    static const std::set<unsigned> safe={0x00,0x01,0x03,0x05,0x06,0x07,0xd2,0xd7,0xd9,0xdb,0xdc,0xde,0xdf,
        0xe2,0xe3,0xe6,0xe7,0xe8,0xe9,0xed,0xf0,0xf2,0xf3,0xf4,0xf5,0xf7,0xf8,0xf9,0xfa,0xfb,0xfc,0xfd};
    for(auto &entry:m.records) {
        Record &r=*entry.second;if(r.type!=4)continue;
        bool is_palette=false; int paired_part=-1;
        uint32_t sizes[2]={0,0};bool has_size[2]={false,false};
        require(!r.display_list.empty(),"empty display list");
        size_t count=r.words.size()/2,output=0;
        bool has_combiner=false,uses_secondary=false;
        for(size_t i=0;i<count;i++) {
            uint32_t a=r.words[i*2].value,b=r.words[i*2+1].value;
            if((a>>24)!=0xfc)continue;
            has_combiner=true;
            for(unsigned value:{(a>>20)&15,(b>>28)&15,(b>>15)&7,
                                (a>>5)&15,(b>>24)&15,(b>>6)&7,
                                (a>>12)&7,(a>>9)&7,(b>>12)&7,(b>>9)&7,
                                (b>>21)&7,(b>>18)&7,(b>>3)&7,b&7})
                uses_secondary|=value==2;
            for(unsigned value:{(a>>15)&31,a&31})uses_secondary|=value==2||value==9;
        }
        const bool unused_secondary=has_combiner&&!uses_secondary;
        unsigned load_slot=0;
        for(size_t i=0;i<count;i++) {
            uint32_t w0=r.words[i*2].value;Word &w=r.words[i*2+1];uintptr_t w1=w.value;
            unsigned op=w0>>24;require(safe.count(op),"unsupported graphics opcode "+std::to_string(op));
            if(op!=0xd2)paired_part=-1;
            if(op==0x01) {
                auto &v=record(m,w,3);unsigned n=(w0>>12)&255,end=(w0>>1)&127;
                require(n && end>=n && end<=64 && w.offset<=v.vertices.size() && n<=v.vertices.size()-w.offset,"invalid vertex batch");
                w1=reinterpret_cast<uintptr_t>(v.vertices.data()+w.offset);
            } else if(op==0xde) {
                auto &dl=record(m,w,4);require(w.offset==0,"display-list offset unsupported");
                w1=reinterpret_cast<uintptr_t>(dl.display_list.data());
            } else if(op==0xfd) {
                if(w.kind==0) {require(w.value==0,"raw texture address unsupported");w1=0;is_palette=false;}
                else {
                    auto &tex=record(m,w,2);require(!w.offset,"texture offset unsupported");
                    is_palette=tex.texture_path.empty();w1=reinterpret_cast<uintptr_t>(tex.texture_path.c_str());
                    if(is_palette)w0=0,w1=0;
                }
            } else if(op==0xdc) {
                require((w0&255)==G_MV_LIGHT,"unsupported matrix/viewport reference");
                require(w.kind==1,"invalid light pointer");auto it=m.records.find(w.name);
                require(it!=m.records.end(),"missing light");auto &light=*it->second;
                require(light.type==1||light.type==24||light.type==18||light.type==19,"invalid light type");
                require(w.offset>=1&&w.offset<=3,"invalid light selector");
                unsigned offset=0;
                if(light.type==1||light.type==24){require(w.offset<=2,"invalid Lights selector");offset=w.offset==1?8:0;}
                else offset=(w.offset-1)*4;
                w1=reinterpret_cast<uintptr_t>(light.light.data()+offset);
            } else if(op==0xd2) {
                require(w.kind==0,"invalid light copy");
                int src=int((w0>>16)&255)*8/24-1,dst=int((w0>>8)&255)*8/24-1;
                int part=src/2-1;require(part>=0&&part<8&&(dst==1||dst==2),"unsupported palette copy");
                // A player-part copy consists of directional (destination 1)
                // then ambient (destination 2). Decode the part from the first
                // command: the second source can round into the NEXT part.
                if(dst==1)paired_part=part;
                else if(paired_part>=0){part=paired_part;paired_part=-1;}
                else if(src==15)part=8;
                Gfx number=gsSPNumLights(NUMLIGHTS_1);
                r.display_list[output++]=number;
                if(dst==1) { Gfx g=gsSPLight(&m.palette[part].l[0],1);w0=g.words.w0;w1=g.words.w1; }
                else { Gfx g=gsSPLight(&m.palette[part].a,2);w0=g.words.w0;w1=g.words.w1; }
            } else {
                require(w.kind==0,"pointer in non-pointer graphics command");
                paired_part=-1;
            }
            if(op==0xf5) {
                unsigned tile=(w1>>24)&7,tmem=w0&511;
                if(is_palette)w0=0,w1=0;
                else {
                    if(tile==6) {tile=7;w1=(w1&~(uintptr_t(7)<<24))|(uintptr_t(7)<<24);tmem=256;}
                    if(tile==1)tmem=256;
                    if(tile==7)require(tmem==0||tmem==256,"unsupported texture slot");
                    if(tile==7)load_slot=tmem/256;
                    if(tile==0)require(((w1>>20)&15)==0,"indexed palette selection unsupported");
                    w0=(w0&~511u)|tmem;
                }
            }
            if(op==0xf0 && is_palette)w0=0,w1=0;
            if(op==0xf3||op==0xf4) {
                unsigned tile=(w1>>24)&7;
                require((w0&0xffffff)==0,"texture load requires zero origin");
                require(tile==7||tile==6||tile==1,"unsupported load tile");
                if(tile==6)w1=(w1&~(uintptr_t(7)<<24))|(uintptr_t(7)<<24);
                // An unused secondary load must not overwrite the renderer's
                // shared UV bounds (gfx_dp_load_tile updates those bounds).
                if(unused_secondary&&(load_slot==1||tile==6||tile==1))w0=0,w1=0;
            }
            if(op==0xf2) {
                unsigned tile=(w1>>24)&7;
                if(tile<2){ sizes[tile]=w1&0xffffff;has_size[tile]=true; }
            }
            if(op==0x05||op==0x06||op==0x07) {
                auto check=[](uint32_t v){for(int s:{16,8,0})require(((v>>s)&255)<128,"triangle vertex outside cache");};
                check(w0);if(op!=0x05)check(uint32_t(w1));
            }
            if(op==0xdb && ((w0>>16)&255)==G_MW_NUMLIGHT)require(w1<=24*7 && w1%24==0,"too many lights");
            r.display_list[output].words.w0=w0;r.display_list[output++].words.w1=w1;
        }
        r.display_list.resize(output); // Shrinks without moving pointer targets.
        require(!has_size[1]||!has_size[0]||sizes[0]==sizes[1]||unused_secondary,"independent secondary texture coordinates unsupported");
        unsigned last=r.words[r.words.size()-2].value>>24;
        require(last==0xdf||(last==0xde&&((r.words[r.words.size()-2].value>>16)&1)),"unterminated display list");
    }
    // Reject cyclic/over-deep call chains before they reach gfx_run_dl.
    std::set<Record*> active;size_t visits=0;
    std::function<void(Record&,unsigned)> visit=[&](Record &r,unsigned depth){
        require(depth<24&&++visits<262144&&active.insert(&r).second,"cyclic or excessive display-list calls");
        for(size_t i=0;i<r.words.size();i+=2)if((r.words[i].value>>24)==0xde)visit(record(m,r.words[i+1],4),depth+1);
        active.erase(&r);
    };
    for(auto &e:m.records)if(e.second->type==4)visit(*e.second,0);
}
unsigned geo_length(uint32_t w) {
    unsigned op=w&255,p=(w>>8)&255;
    switch(op) {
        case 0:case 2:case 0x0d:case 0x0e:case 0x15:case 0x16:case 0x18:return 2;
        case 1:case 3:case 4:case 5:case 0x0b:case 0x20:return 1;
        case 0x10:{unsigned form=(p>>4)&7;require(form<=3,"unsupported transform");return (form==0?4:form==3?1:2)+(!!(p&128));}
        case 0x11:case 0x12:case 0x14:return 2+(!!(p&128));
        case 0x13:case 0x1c:return 3;
        case 0x1d:require(!(p&0x40),"extended scale unsupported");return 2+(!!(p&128));
        default:throw std::runtime_error("unsupported model geometry opcode "+std::to_string(op));
    }
}
int pointer_index(unsigned op,uint32_t w,unsigned length) {
    if(op==0||op==2||op==0x0e||op==0x15||op==0x18)return 1;
    if(op==0x13||op==0x1c)return 2;
    if((op==0x10||op==0x11||op==0x12||op==0x14||op==0x1d)&&(w&0x8000))return length-1;
    return -1;
}
struct Tree { unsigned op;std::vector<Tree> children; };
void fix_geometry(Model &m,CallbackResolver resolver,const std::vector<int> *expected_hierarchy) {
    for(auto &entry:m.records) {
        auto &r=*entry.second;if(r.type!=5)continue;
        size_t at=0;unsigned last=0;bool ended=false;
        while(at<r.words.size()) {
            auto &header=r.words[at];require(!header.kind,"pointer in geometry header");
            unsigned op=header.value&255,n=geo_length(header.value);last=op;
            require(n<=r.words.size()-at,"truncated geometry command");
            int ptr=pointer_index(op,header.value,n);r.layout[at]=bbh(header.value);
            for(unsigned i=1;i<n;i++) {
                const Word &w=r.words[at+i];uintptr_t v=0;
                if(int(i)==ptr) {
                    if(op==0x0e||op==0x18||op==0x1c)
                        require(w.kind==2,"missing geometry callback");
                    if(w.kind==0)require(w.value==0,"raw model pointer unsupported");
                    else if(op==0x0e||op==0x18||op==0x1c) {
                        require(w.kind==2,"named Lua callback unsupported");v=resolver(w.value);
                        require(v,"unsupported geometry callback "+std::to_string(w.value));
                        bool valid=op==0x0e?(w.value==1||w.value==2||w.value==5||w.value==7||w.value==8||w.value==15||w.value==40||w.value==41||w.value==48):
                            op==0x1c?w.value==10:(w.value==0||w.value==3||w.value==4||w.value==6||w.value==9||w.value==12||w.value==13||w.value==14||w.value==36||w.value==37||w.value==38||w.value==39||(w.value>=42&&w.value<=47)||w.value==49);
                        require(valid,"geometry callback context mismatch");
                    } else if(op==0||op==2) {
                        auto &target=record(m,w,5);require(!w.offset,"geometry offset unsupported");v=reinterpret_cast<uintptr_t>(target.layout.data());
                    } else {
                        auto &target=record(m,w,4);require(!w.offset,"display-list offset unsupported");v=reinterpret_cast<uintptr_t>(target.display_list.data());
                    }
                } else {require(!w.kind,"pointer in geometry parameter");v=op==0x1d?full_word(w.value):hh(w.value);}
                r.layout[at+i]=v;
            }
            at+=n;
            if(op==1||op==3||(op==2&&((header.value>>8)&255)==0)){ended=true;require(at==r.words.size(),"commands after geometry return");}
        }
        require(ended&&(last==1||last==3||last==2),"unterminated geometry");
    }
    // Expand only references, never execute file-supplied callbacks to validate
    // all switch alternatives, including non-default hands/caps/LOD branches.
    std::vector<Tree> tree;std::vector<std::vector<Tree>*> stack{&tree};
    std::set<Record*> active;size_t node_count=0;
    std::function<void(Record&,unsigned)> expand=[&](Record &r,unsigned depth){
        require(depth<7&&active.insert(&r).second,"cyclic or excessive geometry calls");
        size_t initial=stack.size();Tree *last=nullptr;
        for(size_t i=0;i<r.words.size();) {
            uint32_t w=r.words[i].value;unsigned op=w&255,n=geo_length(w);
            if(op==4){require(last&&stack.size()<30,"invalid geometry nesting");stack.push_back(&last->children);last=nullptr;}
            else if(op==5){require(stack.size()>initial,"unbalanced geometry close");stack.pop_back();last=nullptr;}
            else if(op==0||op==2){expand(record(m,r.words[i+1],5),depth+1);last=nullptr;}
            else if(op!=1&&op!=3){require(++node_count<32768,"expanded geometry limit");stack.back()->push_back({op,{}});last=&stack.back()->back();}
            i+=n;
        }
        require(stack.size()==initial,"unbalanced geometry open");active.erase(&r);
    };
    expand(*m.root,0);
    m.memory_bytes+=node_count*256; // Conservative graph arena/alignment estimate.
    std::function<void(const std::vector<Tree>&,unsigned)> check_matrices;
    check_matrices=[&](const std::vector<Tree> &nodes,unsigned depth){
        for(auto &node:nodes){
            unsigned next=depth+((node.op>=0x10&&node.op<=0x14)||node.op==0x1d);
            require(next<=20,"model matrix-stack depth exceeds limit");
            check_matrices(node.children,next);
        }
    };
    check_matrices(tree,0);
    std::function<std::vector<int>(const std::vector<Tree>&,int,int)> signature;
    size_t optional_actor_size=0;
    signature=[&](const std::vector<Tree> &nodes,int parent,int base){
        std::vector<int> result;
        for(size_t i=0;i<nodes.size();i++) {
            const Tree &n=nodes[i];int joint=parent;
            if(n.op==0x13){joint=base+int(result.size());result.push_back(parent);}
            std::vector<int> sub;
            if(n.op==0x0e){
                require(!n.children.empty(),"empty geometry switch");
                bool first=true,empty_variant=false;
                for(auto &child:n.children){
                    auto next=signature({child},joint,base+int(result.size()));
                    if(expected_hierarchy&&joint==-1&&base==0&&result.empty()&&next.empty()){empty_variant=true;continue;}
                    if(first)sub=next,first=false;else require(next==sub,"switch changes animation hierarchy");
                }
                // Bowser's third render state draws only its shadow. An empty
                // alternative is safe only when it skips the entire skeleton.
                if(empty_variant&&!sub.empty()){
                    require(!optional_actor_size||optional_actor_size==sub.size(),"partial empty actor switch");
                    optional_actor_size=sub.size();
                }
            } else sub=signature(n.children,joint,base+int(result.size()));
            if(n.op==0x0d){ // Adjacent LOD ranges describe alternatives.
                while(i+1<nodes.size()&&nodes[i+1].op==0x0d){auto next=signature(nodes[++i].children,parent,base+int(result.size()));require(next==sub,"LOD changes animation hierarchy");}
            }
            result.insert(result.end(),sub.begin(),sub.end());require(result.size()<=64,"animation hierarchy limit");
        }
        return result;
    };
    const std::vector<int> expected={-1,0,1,2,2,4,5,6,2,8,9,10,1,12,13,14,1,16,17,18};
    m.animation_parents=signature(tree,-1,0);
    require(!optional_actor_size||optional_actor_size==m.animation_parents.size(),"partial empty actor switch");
    require(m.animation_parents==(expected_hierarchy?*expected_hierarchy:expected),
            expected_hierarchy?"model does not match original actor animation hierarchy":"model does not match Mario's 20-joint animation hierarchy");
}
}

std::unique_ptr<Model> load(const uint8_t *data,size_t size,const std::string &key,
                          const std::string &slot,CallbackResolver resolver,std::string &error,
                          const std::vector<int> *expected_hierarchy) {
    try {
        require(data&&size&&size<=MAX_BYTES,"binary file size limit");
        std::vector<uint8_t> uncompressed;
        if(size>=8&&!memcmp(data,"DYNOSBIN",8)) {
            require(size>=16,"truncated compression header");Reader header{data+8,8};
            uint64_t expected=header.u32();expected|=uint64_t(header.u32())<<32;
            require(expected&&expected<=MAX_BYTES,"decompressed size limit");uncompressed.resize(size_t(expected));
            tinfl_decompressor inflater;tinfl_init(&inflater);
            size_t input=size-16,output=uncompressed.size();
            auto status=tinfl_decompress(&inflater,data+16,&input,uncompressed.data(),uncompressed.data(),&output,
                                        TINFL_FLAG_PARSE_ZLIB_HEADER|TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
            require(status==TINFL_STATUS_DONE&&output==expected&&input==size-16,"invalid compressed DynOS binary");
            data=uncompressed.data();size=uncompressed.size();
        }
        std::unique_ptr<Model> m(new Model);palette(*m,slot);Reader reader{data,size};
        read_records(*m,reader,key);fix_display_lists(*m);fix_geometry(*m,resolver,expected_hierarchy);
        for(auto &entry:m->records){auto &r=*entry.second;m->memory_bytes+=r.bytes.capacity()+r.vertices.capacity()*sizeof(Vtx)+r.display_list.capacity()*sizeof(Gfx)+r.layout.capacity()*sizeof(uintptr_t)+sizeof(Record);r.words.clear();r.words.shrink_to_fit();}
        require(m->memory_bytes<=64*1024*1024,"model memory limit");error.clear();return m;
    } catch(const std::exception &e){error=e.what();return nullptr;}
}
}
