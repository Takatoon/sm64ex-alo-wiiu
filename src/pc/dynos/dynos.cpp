#include "dynos.h"
#include "dynos_model.h"
#include "dynos_graph.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <set>
#include <functional>
extern "C" {
#include "sm64.h"
#include "engine/geo_layout.h"
#include "engine/graph_node.h"
#include "game/mario_misc.h"
#include "game/behavior_actions.h"
#include "game/object_helpers.h"
#include "game/mario_actions_cutscene.h"
#include "game/mario.h"
#include "game/level_update.h"
#include "game/screen_transition.h"
#include "pc/configfile.h"
#include "pc/fs/fs.h"
#ifdef TARGET_WII_U
#include <whb/log.h>
#define DYNOS_LOG(...) WHBLogPrintf(__VA_ARGS__)
#else
#define DYNOS_LOG(...) do { printf(__VA_ARGS__); printf("\n"); fflush(stdout); } while(0)
#endif
}
#include "dynos_actor_registry.inc.h"

unsigned int dynos_player_choice;
namespace {
struct Entry {
    std::string path, slot;
    uint32_t id;
    std::array<unsigned char, 40> label{};
    std::unique_ptr<dynos::Model> model;
    GraphNode *graph=nullptr;
    bool failed=false;
};
std::vector<std::unique_ptr<Entry>> entries;
std::map<std::string,std::vector<std::unique_ptr<Entry>>> npc_entries;
struct Binding { unsigned model; Entry *entry; };
std::map<GraphNode*,Binding> npc_bindings;
int pending_step;
const unsigned char *labels[65];
unsigned char original_label[]={0x18,0x1b,0x12,0x10,0x12,0x17,0x0a,0x15,0xff}; // ORIGINAL
std::map<std::string,const dynos::Record*> textures;
AllocOnlyPool *graph_pool;
size_t resident_bytes;
unsigned int applied_choice=~0u;
GraphNode *selected_graph;

Gfx *player_hand(s32 context,GraphNode *node,void *matrix) {
    Gfx *result=geo_switch_mario_hand(context,node,reinterpret_cast<Mat4*>(matrix));
    if(context!=GEO_CONTEXT_RENDER||!gMarioState)return result;
    auto hand=reinterpret_cast<GraphNodeSwitchCase*>(node);
    unsigned side=hand->numCases==0?0:1;
    static int previous_phase[2]={-1,-1};
    static GraphNode *previous_graph[2]={nullptr,nullptr};
    bool celebrating=gMarioState->action==ACT_STAR_DANCE_EXIT||gMarioState->action==ACT_STAR_DANCE_NO_EXIT||gMarioState->action==ACT_STAR_DANCE_WATER;
    if(!celebrating||!selected_graph){previous_phase[side]=-1;return result;}
    unsigned phase=gMarioState->actionTimer<40?0:gMarioState->actionTimer<62?1:2;
    if(previous_phase[side]==int(phase)&&previous_graph[side]==selected_graph)return result;
    previous_phase[side]=phase;previous_graph[side]=selected_graph;
    GraphNode *first=node->children,*chosen=first;unsigned children=0;
    if(first){auto child=first;do{++children;child=child->next;}while(child&&child!=first&&children<32);}
    for(int i=0;chosen&&i<hand->selectedCase;i++)chosen=chosen->next;
    const char *mesh="none";unsigned visits=0;
    std::function<bool(GraphNode*,unsigned,bool)> find_mesh=[&](GraphNode *start,unsigned depth,bool siblings){
        if(!start||depth>16||++visits>256)return false;
        auto current=start;
        do {
            void *display=nullptr;
            if(current->type==GRAPH_NODE_TYPE_ANIMATED_PART)display=reinterpret_cast<GraphNodeAnimatedPart*>(current)->displayList;
            else if(current->type==GRAPH_NODE_TYPE_DISPLAY_LIST)display=reinterpret_cast<GraphNodeDisplayList*>(current)->displayList;
            if(applied_choice>0&&applied_choice<=entries.size())
                for(auto &record:entries[applied_choice-1]->model->records)
                    if(display&&record.second->type==4&&display==record.second->display_list.data()){mesh=record.first.c_str();return true;}
            if(find_mesh(current->children,depth+1,true))return true;
            current=current->next;
        }while(siblings&&current&&current!=start&&++visits<256);
        return false;
    };
    find_mesh(chosen,0,false);
    const char *path=applied_choice>0&&applied_choice<=entries.size()?entries[applied_choice-1]->path.c_str():"unknown";
    DYNOS_LOG("DYNOS_STAR_HAND path=%s timer=%u phase=%u side=%d state=%d mario_state=%d selected=%d children=%u mesh=%s anim=%d",
        path,unsigned(gMarioState->actionTimer),phase,hand->numCases,int(gBodyStates[0].handState),
        gMarioState->marioBodyState?int(gMarioState->marioBodyState->handState):-1,
        hand->selectedCase,children,mesh,gMarioState->marioObj?int(gMarioState->marioObj->header.gfx.animInfo.animID):-1);
    return result;
}
Gfx *no_colors(s32,GraphNode*,void*) { return nullptr; }
Gfx *mirror_culling(s32 context,GraphNode *node,void *matrix) {
    auto generated=reinterpret_cast<GraphNodeGenerated*>(node);
    // Additional CoopDX per-layer copies are redundant in this renderer.
    if(generated->parameter>1)return nullptr;
    return geo_mirror_mario_backface_culling(context,node,reinterpret_cast<Mat4*>(matrix));
}
uintptr_t callback(unsigned index) {
    switch(index) {
#define CALLBACK(n,f) case n:return reinterpret_cast<uintptr_t>(f)
        CALLBACK(0,geo_mirror_mario_set_alpha);
        CALLBACK(1,geo_switch_mario_stand_run);
        CALLBACK(2,geo_switch_mario_eyes);
        CALLBACK(3,geo_mario_tilt_torso);
        CALLBACK(4,geo_mario_head_rotation);
        CALLBACK(5,player_hand);
        CALLBACK(6,geo_mario_hand_foot_scaler);
        CALLBACK(7,geo_switch_mario_cap_effect);
        CALLBACK(8,geo_switch_mario_cap_on_off);
        CALLBACK(9,geo_mario_rotate_wing_cap_wings);
        CALLBACK(10,geo_switch_mario_hand_grab_pos);
        CALLBACK(12,mirror_culling);
        CALLBACK(13,geo_update_projectile_pos_from_parent);
        CALLBACK(14,geo_update_layer_transparency);
        CALLBACK(15,geo_switch_anim_state);
        CALLBACK(37,geo_move_mario_part_from_parent);
        CALLBACK(36,geo_cannon_circle_base);
        CALLBACK(38,geo_bits_bowser_coloring);
        CALLBACK(39,geo_update_body_rot_from_parent);
        CALLBACK(40,geo_switch_bowser_eyes);
        CALLBACK(41,geo_switch_tuxie_mother_eyes);
        CALLBACK(42,geo_update_held_mario_pos);
        CALLBACK(43,geo_snufit_move_mask);
        CALLBACK(44,geo_snufit_scale_body);
        CALLBACK(45,geo_scale_bowser_key);
        CALLBACK(47,geo_offset_klepto_held_object);
        CALLBACK(48,geo_switch_peach_eyes);
        CALLBACK(49,no_colors);
#undef CALLBACK
        default:return 0;
    }
}
uint32_t stable_id(const std::string &path) {
    uint32_t hash=2166136261u;for(unsigned char c:path)hash=(hash^c)*16777619u;return hash?hash:1;
}
void encode_label(Entry &entry,const std::string &prefix="") {
    std::string path=entry.path;
    auto end=path.find_last_of('/');std::string pack=end==std::string::npos?"":path.substr(0,end);
    auto start=pack.find_last_of('/');if(start!=std::string::npos)pack=pack.substr(start+1);
    if(pack=="actors") {
        auto parent=path.substr(0,end);auto previous=parent.find_last_of('/');
        if(previous!=std::string::npos){parent=parent.substr(0,previous);pack=parent.substr(parent.find_last_of('/')+1);}
    }
    if(pack=="actors"||pack=="packs"||pack.empty())pack=entry.slot;
    std::string lower=pack;
    for(char &c:lower)if(c>='A'&&c<='Z')c+=32;
    std::string text=prefix+pack;
    if(lower.find(entry.slot)==std::string::npos)text+=" "+entry.slot;
    size_t i=0;for(unsigned char c:text){if(i>=entry.label.size()-1)break;
        if(c>='a'&&c<='z')c-=32;
        entry.label[i++]=(c>='A'&&c<='Z')?c-'A'+10:(c>='0'&&c<='9')?c-'0':0x9e;
    }entry.label[i]=0xff;
}
bool load_entry(Entry &entry,const std::vector<int> *hierarchy=nullptr) {
    if(entry.failed)return false;
    if(entry.model)return !hierarchy||entry.model->animation_parents==*hierarchy;
    fs_file_t *file=fs_open(entry.path.c_str());std::string error;
    if(file) {
        auto size=fs_size(file);
        if(size>0&&size<=32*1024*1024) {
            std::vector<uint8_t> data(size_t(size),uint8_t(0));
            if(fs_read(file,data.data(),data.size())==size)
                entry.model=dynos::load(data.data(),data.size(),std::to_string(entry.id),entry.slot,callback,error,hierarchy);
            else error="short read";
        } else error="binary size exceeds limit";
        fs_close(file);
    } else error="file cannot be opened";
    if(entry.model&&resident_bytes+entry.model->memory_bytes>128*1024*1024) {
        entry.model.reset();error="resident model memory budget reached";
    }
    if(entry.model) {
        entry.graph=process_geo_layout(graph_pool,entry.model->root->layout.data());
        if(!entry.graph){entry.model.reset();error="graph allocation failed";}
    }
    if(entry.model) {
        resident_bytes+=entry.model->memory_bytes;
        for(auto &record:entry.model->records)if(record.second->type==2&&!record.second->bytes.empty())
            textures["gfx/"+record.second->texture_path+".png"]=record.second.get();
        DYNOS_LOG("DYNOS_LOADED path=%s vertices=%u textures=%u bytes=%u",entry.path.c_str(),unsigned(entry.model->vertex_count),unsigned(entry.model->texture_count),unsigned(entry.model->memory_bytes));
        return true;
    }
    entry.failed=true;encode_label(entry,"ERROR ");
    DYNOS_LOG("DYNOS_REJECT path=%s reason=%s",entry.path.c_str(),error.c_str());
    return false;
}
}

void dynos_init(void) {
    labels[0]=original_label;
    graph_pool=alloc_only_pool_init();
    // Includes loose dynos/packs folders and ZIPs mounted by the existing mods
    // filesystem. Enumeration is sorted/deduplicated before assigning labels.
    fs_pathlist_t files=fs_enumerate("",true);std::set<std::string> candidates;
    for(int i=0;i<files.numpaths;i++) {
        std::string path=files.paths[i];
        while(!path.empty()&&path[0]=='/')path.erase(0,1);
        auto slash=path.find_last_of('/');
        std::string file=path.substr(slash==std::string::npos?0:slash+1);
        if(file.size()>4&&file.substr(file.size()-4)==".bin")candidates.insert(path);
    }
    fs_pathlist_free(&files);std::set<uint32_t> ids;
    for(auto &path:candidates) {
        uint32_t id=stable_id(path);if(!ids.insert(id).second)continue;
        std::unique_ptr<Entry> entry(new Entry);entry->path=path;entry->id=id;
        auto slash=path.find_last_of('/');std::string file=path.substr(slash==std::string::npos?0:slash+1);
        bool player=false;
        for(const char *slot:{"mario","luigi","wario","waluigi","toad_player","sunshine"})
            if(file==std::string(slot)+"_geo.bin"){entry->slot=slot;player=true;break;}
        if(!player) {
            std::string actor=file.substr(0,file.size()-4);bool known=false;
            for(const auto &native:native_actors)if(native.layout&&actor==native.name){known=true;break;}
            if(known&&npc_entries[actor].size()<16&&npc_entries.size()<=256){entry->slot=actor;npc_entries[actor].push_back(std::move(entry));}
            continue;
        }
        if(entries.size()>=64)continue;
        encode_label(*entry);
        if(configDynosModel==id)dynos_player_choice=entries.size()+1;
        labels[entries.size()+1]=entry->label.data();entries.push_back(std::move(entry));
    }
    DYNOS_LOG("DYNOS_INIT version=2 models=%u npc_targets=%u scope=native_actor_animations star_hand_trace=1",unsigned(entries.size()),unsigned(npc_entries.size()));
}

void dynos_update(void) {
    bool shortcut=pending_step!=0;
    if(shortcut){int count=int(entries.size()+1);dynos_player_choice=(int(dynos_player_choice)+(pending_step>0?1:count-1))%count;pending_step=0;}
    if(dynos_player_choice>entries.size())dynos_player_choice=0;
    if(applied_choice==dynos_player_choice)return;
    selected_graph=nullptr;applied_choice=dynos_player_choice;
    if(!applied_choice){configDynosModel=0;if(shortcut)configfile_save(configfile_name());return;}
    Entry &entry=*entries[applied_choice-1];
    load_entry(entry);
    if(entry.graph){selected_graph=entry.graph;configDynosModel=entry.id;}
    // Keep the ERROR choice visible so the next menu press can move past an
    // incompatible pack; rendering and saved preference fall back to original.
    else configDynosModel=0;
    if(shortcut){configfile_save(configfile_name());DYNOS_LOG("DYNOS_PLAYER_SHORTCUT path=%s",entry.path.c_str());}
}

void dynos_request_player_step(int direction){pending_step=direction;}
void dynos_clear_npc_bindings(void){npc_bindings.clear();}
void dynos_bind_model(unsigned model,const void *layout,GraphNode *original) {
    for(auto it=npc_bindings.begin();it!=npc_bindings.end();) {
        if(it->second.model==model||it->first==original)it=npc_bindings.erase(it);else ++it;
    }
    if(!layout||!original)return;
    std::vector<Entry*> candidates;const char *actor=nullptr;
    for(const auto &native:native_actors) {
        if(!native.layout||native.layout!=layout)continue;
        auto found=npc_entries.find(native.name);if(found==npc_entries.end())continue;
        actor=native.name;
        for(auto &entry:found->second)candidates.push_back(entry.get());
    }
    if(candidates.empty())return;
    std::sort(candidates.begin(),candidates.end(),[](Entry *a,Entry *b){return a->path<b->path;});
    std::vector<int> hierarchy;
    if(!dynos::native_hierarchy(original,hierarchy)){DYNOS_LOG("DYNOS_NPC_REJECT actor=%s reason=native_hierarchy",actor);return;}
    for(auto entry:candidates)if(load_entry(*entry,&hierarchy)) {
        npc_bindings[original]={model,entry};
        DYNOS_LOG("DYNOS_NPC_BOUND actor=%s path=%s alternatives=%u",actor,entry->path.c_str(),unsigned(candidates.size()));
        return;
    }
}
GraphNode *dynos_npc_graph(GraphNode *original) {
    auto found=npc_bindings.find(original);
    return found==npc_bindings.end()?original:found->second.entry->graph;
}

GraphNode *dynos_player_graph(GraphNode *original) {return selected_graph?selected_graph:original;}
unsigned int dynos_player_count(void) {return unsigned(entries.size()+1);}
const unsigned char **dynos_player_labels(void) {return labels;}

void *dynos_texture_read(const char *path,uint64_t *size) {
    auto it=textures.find(path);if(it==textures.end())return nullptr;
    const auto &bytes=it->second->bytes;void *copy=malloc(bytes.size());
    if(copy){memcpy(copy,bytes.data(),bytes.size());*size=bytes.size();}return copy;
}
void dynos_shutdown(void) {
    // Must follow gfx_shutdown: texture-cache keys point into these records.
    selected_graph=nullptr;npc_bindings.clear();textures.clear();entries.clear();npc_entries.clear();resident_bytes=0;
}
