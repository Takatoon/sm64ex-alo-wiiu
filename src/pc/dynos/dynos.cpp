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

extern "C" {
extern const struct Animation *const bobomb_seg8_anims_0802396C[] __attribute__((weak));
extern const struct Animation *const chuckya_seg8_anims_0800C070[] __attribute__((weak));
extern const struct Animation *const skeeter_seg6_anims_06007DE0[] __attribute__((weak));
}
unsigned int dynos_player_choice;
unsigned int dynos_mods_enabled=1,dynos_texture_choice=1,dynos_npc_choice;
namespace {
struct Entry {
    std::string path, slot, pack;
    uint32_t id;
    std::array<unsigned char, 40> label{};
    std::unique_ptr<dynos::Model> model;
    GraphNode *graph=nullptr;
    bool failed=false;
    bool logged_animation_fallback=false;
    std::map<const Animation*,const Animation*> animation_overrides;
};
std::vector<std::unique_ptr<Entry>> entries;
std::map<std::string,std::vector<std::unique_ptr<Entry>>> npc_entries;
struct Binding { unsigned model; Entry *entry; };
std::map<GraphNode*,Binding> npc_bindings;
struct Registered {unsigned model;const void *layout;GraphNode *original;};
std::map<unsigned,Registered> registered_models;
struct Pack {std::string path;uint32_t id;std::array<unsigned char,40> label{};};
std::vector<Pack> texture_packs,npc_packs;
const unsigned char *texture_labels[66],*npc_labels[65];
unsigned char base_label[40],all_label[40],vanilla_label[40];
struct Selection {bool enabled;unsigned texture,npc,player;};
Selection active{true,1,0,0},requested{true,1,0,0};
bool apply_requested=false,first_update=true;
bool (*begin_reload)(void);
void (*end_reload)(void);

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
void encode_text(std::array<unsigned char,40> &label,const std::string &text) {
    size_t i=0;for(unsigned char c:text){if(i>=label.size()-1)break;
        if(c>='a'&&c<='z')c-=32;
        label[i++]=(c>='A'&&c<='Z')?c-'A'+10:(c>='0'&&c<='9')?c-'0':c=='.'?0x3f:0x9e;
    }label[i]=0xff;
}
std::string pack_path(const std::string &file) {
    auto slash=file.find_last_of('/');if(slash==std::string::npos)return ".";
    std::string parent=file.substr(0,slash);
    if(parent.size()>=7&&parent.substr(parent.size()-7)=="/actors")parent.resize(parent.size()-7);
    return parent;
}
std::string pack_name(const std::string &path) {
    auto slash=path.find_last_of('/');return path.substr(slash==std::string::npos?0:slash+1);
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
struct NativeAnimations { const Animation *const *table;unsigned count; };
NativeAnimations native_animations(const std::string &actor) {
    if(actor=="black_bobomb_geo"||actor=="bobomb_buddy_geo")return {bobomb_seg8_anims_0802396C,2};
    if(actor=="chuckya_geo")return {chuckya_seg8_anims_0800C070,6};
    if(actor=="skeeter_geo")return {skeeter_seg6_anims_06007DE0,4};
    return {nullptr,0};
}
bool load_entry(Entry &entry,const std::vector<int> *hierarchy=nullptr) {
    if(entry.failed)return false;
    if(entry.model)return !hierarchy||entry.model->compatible_hierarchy==*hierarchy;
    auto bank=native_animations(entry.slot);
    fs_file_t *file=fs_open(entry.path.c_str());std::string error;
    if(file) {
        auto size=fs_size(file);
        if(size>0&&size<=32*1024*1024) {
            std::vector<uint8_t> data(size_t(size),uint8_t(0));
            if(fs_read(file,data.data(),data.size())==size)
                entry.model=dynos::load(data.data(),data.size(),std::to_string(entry.id),entry.slot,callback,error,hierarchy,hierarchy&&bank.table);
            else error="short read";
        } else error="binary size exceeds limit";
        fs_close(file);
    } else error="file cannot be opened";
    if(entry.model&&entry.model->custom_animation_hierarchy) {
        for(unsigned i=0;bank.table&&i<bank.count&&i<entry.model->animation_table.size();++i){
            auto alternate=entry.model->animation_table[i];const Animation *native=bank.table[i];
            if(!alternate||!native)continue;
            const Animation *replacement=&alternate->native;
            if(native->flags==replacement->flags&&native->animYTransDivisor==replacement->animYTransDivisor)
                entry.animation_overrides[native]=replacement;
        }
        if(entry.animation_overrides.empty()){entry.model.reset();error="no compatible native animation slots";}
    }
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
    entry.animation_overrides.clear();entry.failed=true;encode_label(entry,"ERROR ");
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
        std::unique_ptr<Entry> entry(new Entry);entry->path=path;entry->id=id;entry->pack=pack_path(path);
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
    std::set<std::string> npc_paths;
    for(auto &actor:npc_entries)for(auto &entry:actor.second)npc_paths.insert(entry->pack);
    for(auto &path:npc_paths)if(npc_packs.size()<64){Pack pack;pack.path=path;pack.id=stable_id(path);encode_text(pack.label,pack_name(path));npc_packs.push_back(std::move(pack));}
    fs_pathlist_t overlays=fs_texture_packs();std::set<std::string> texture_paths;
    for(int i=0;i<overlays.numpaths;++i)texture_paths.insert(overlays.paths[i]);fs_pathlist_free(&overlays);
    std::vector<std::string> sorted(texture_paths.begin(),texture_paths.end());
    std::sort(sorted.begin(),sorted.end(),[](const std::string &a,const std::string &b){int order=sys_strcasecmp(pack_name(a).c_str(),pack_name(b).c_str());return order?order<0:a<b;});
    for(auto &path:sorted)if(texture_packs.size()<64){Pack pack;pack.path=path;pack.id=stable_id(path);if(pack.id<=1)pack.id+=2;encode_text(pack.label,pack_name(path));texture_packs.push_back(std::move(pack));}
    std::array<unsigned char,40> label;
    encode_text(label,"BASE.ZIP");memcpy(base_label,label.data(),40);
    encode_text(label,"ALL A-Z");memcpy(all_label,label.data(),40);
    encode_text(label,"VANILLA");memcpy(vanilla_label,label.data(),40);
    texture_labels[0]=base_label;texture_labels[1]=all_label;npc_labels[0]=vanilla_label;labels[0]=vanilla_label;
    dynos_texture_choice=configDynosTexturePack==1?1:0;dynos_npc_choice=0;
    for(size_t i=0;i<texture_packs.size();++i){texture_labels[i+2]=texture_packs[i].label.data();if(texture_packs[i].id==configDynosTexturePack)dynos_texture_choice=i+2;}
    for(size_t i=0;i<npc_packs.size();++i){npc_labels[i+1]=npc_packs[i].label.data();if(npc_packs[i].id==configDynosNpcPack)dynos_npc_choice=i+1;}
    dynos_mods_enabled=configModsEnabled?1:0;
    active={configModsEnabled,dynos_texture_choice,dynos_npc_choice,dynos_player_choice};
    fs_set_texture_pack(!active.enabled||!active.texture?"":active.texture==1?nullptr:texture_packs[active.texture-2].path.c_str());
    DYNOS_LOG("DYNOS_INIT version=4 models=%u npc_packs=%u texture_packs=%u scope=mods_menu star_hand_trace=1",unsigned(entries.size()),unsigned(npc_packs.size()),unsigned(texture_packs.size()));
}

namespace {
void select_player(unsigned choice) {
    selected_graph=nullptr;applied_choice=choice;
    if(active.enabled&&choice&&choice<=entries.size()){
        Entry &entry=*entries[choice-1];load_entry(entry);selected_graph=entry.graph;
    }
}
void release_models(void) {
    selected_graph=nullptr;npc_bindings.clear();textures.clear();
    alloc_only_pool_clear(graph_pool);
    auto reset=[](Entry &entry){entry.graph=nullptr;entry.animation_overrides.clear();entry.model.reset();entry.failed=false;entry.logged_animation_fallback=false;};
    for(auto &entry:entries){reset(*entry);encode_label(*entry);}
    for(auto &actor:npc_entries)for(auto &entry:actor.second)reset(*entry);
    resident_bytes=0;
}
void save_selection(void) {
    configModsEnabled=active.enabled;
    configDynosTexturePack=active.texture<2?active.texture:texture_packs[active.texture-2].id;
    configDynosNpcPack=active.npc?npc_packs[active.npc-1].id:0;
    configDynosModel=active.player?entries[active.player-1]->id:0;
    if(active.enabled&&active.player&&entries[active.player-1]->failed)configDynosModel=0;
    configfile_save(configfile_name());
}
}
void dynos_set_reload_hooks(bool (*begin)(void),void (*end)(void)){begin_reload=begin;end_reload=end;}
void dynos_request_apply(void) {
    requested={dynos_mods_enabled!=0,dynos_texture_choice,dynos_npc_choice,dynos_player_choice};
    if(requested.texture>=dynos_texture_count())requested.texture=0;
    if(requested.npc>=dynos_npc_pack_count())requested.npc=0;
    if(requested.player>=dynos_player_count())requested.player=0;
    apply_requested=true;
}
bool dynos_apply_pending(void){return apply_requested;}
void dynos_update(void) {
    if(first_update){first_update=false;select_player(active.player);}
    if(pending_step){
        if(active.enabled&&!apply_requested){
            unsigned count=entries.size()+1;
            active.player=(active.player+(pending_step>0?1:count-1))%count;
            dynos_player_choice=active.player;select_player(active.player);save_selection();
            // Keep the original fast shortcut: retain old graphs until the
            // next explicit Apply instead of reloading the level texture set.
            DYNOS_LOG("DYNOS_PLAYER_SHORTCUT choice=%u",active.player);
        }
        pending_step=0;
    }
    if(!apply_requested)return;
    // The renderer must retire both normal and interpolated GPU work and all
    // texture-cache keys before the graph arena or BIN storage can be freed.
    if(!begin_reload||!begin_reload())return;
    const char *path=!requested.enabled||!requested.texture?"":requested.texture==1?nullptr:texture_packs[requested.texture-2].path.c_str();
    if(!fs_set_texture_pack(path)){DYNOS_LOG("MODS_APPLY_FAILED reason=texture_pack");apply_requested=false;return;}
    release_models();active=requested;
    std::vector<Registered> models;for(auto &pair:registered_models)models.push_back(pair.second);
    for(auto &model:models)dynos_bind_model(model.model,model.layout,model.original);
    select_player(active.player);
    if(end_reload)end_reload();
    save_selection();apply_requested=false;
    DYNOS_LOG("MODS_APPLIED enabled=%d texture=%u npc=%u player=%u resident_bytes=%u",active.enabled,active.texture,active.npc,active.player,unsigned(resident_bytes));
}

unsigned int dynos_texture_count(void){return unsigned(texture_packs.size()+2);}
const unsigned char **dynos_texture_labels(void){return texture_labels;}
unsigned int dynos_npc_pack_count(void){return unsigned(npc_packs.size()+1);}
const unsigned char **dynos_npc_pack_labels(void){return npc_labels;}
uint64_t dynos_resident_bytes(void){return resident_bytes;}

void dynos_request_player_step(int direction){pending_step=direction;}
void dynos_clear_npc_bindings(void){npc_bindings.clear();registered_models.clear();}
void dynos_bind_model(unsigned model,const void *layout,GraphNode *original) {
    for(auto it=npc_bindings.begin();it!=npc_bindings.end();) {
        if(it->second.model==model||it->first==original)it=npc_bindings.erase(it);else ++it;
    }
    for(auto it=registered_models.begin();it!=registered_models.end();){
        if(it->first==model||it->second.original==original)it=registered_models.erase(it);else ++it;
    }
    if(!layout||!original)return;
    registered_models[model]={model,layout,original};
    if(!active.enabled||!active.npc)return;
    const std::string &selected_pack=npc_packs[active.npc-1].path;
    std::vector<Entry*> candidates;const char *actor=nullptr;
    for(const auto &native:native_actors) {
        if(!native.layout||native.layout!=layout)continue;
        auto found=npc_entries.find(native.name);if(found==npc_entries.end())continue;
        actor=native.name;
        for(auto &entry:found->second)if(entry->pack==selected_pack)candidates.push_back(entry.get());
    }
    if(candidates.empty())return;
    std::sort(candidates.begin(),candidates.end(),[](Entry *a,Entry *b){return a->path<b->path;});
    std::vector<int> hierarchy;
    if(!dynos::native_hierarchy(original,hierarchy)){DYNOS_LOG("DYNOS_NPC_REJECT actor=%s reason=native_hierarchy",actor);return;}
    for(auto entry:candidates)if(load_entry(*entry,&hierarchy)) {
        npc_bindings[original]={model,entry};
        DYNOS_LOG("DYNOS_NPC_BOUND actor=%s path=%s alternatives=%u animation_slots=%u static_joints=%u",actor,entry->path.c_str(),unsigned(candidates.size()),unsigned(entry->animation_overrides.size()),entry->model->static_joint_count);
        return;
    }
}
GraphNode *dynos_npc_graph(GraphNode *original) {
    auto found=npc_bindings.find(original);
    return found==npc_bindings.end()?original:found->second.entry->graph;
}

GraphNode *dynos_npc_graph_with_animation(GraphNode *original,const Animation *native,const Animation **replacement) {
    *replacement=nullptr;auto found=npc_bindings.find(original);
    if(found==npc_bindings.end())return original;
    Entry &entry=*found->second.entry;
    if(entry.model->custom_animation_hierarchy){
        auto alternate=entry.animation_overrides.find(native);
        if(alternate==entry.animation_overrides.end()){
            if(!entry.logged_animation_fallback){
                DYNOS_LOG("DYNOS_NPC_ANIMATION_FALLBACK path=%s reason=unmapped_native_animation",entry.path.c_str());
                entry.logged_animation_fallback=true;
            }
            return original;
        }
        *replacement=alternate->second;
    }
    return entry.graph;
}

GraphNode *dynos_player_graph(GraphNode *original) {return active.enabled&&selected_graph?selected_graph:original;}
unsigned int dynos_player_count(void) {return unsigned(entries.size()+1);}
const unsigned char **dynos_player_labels(void) {return labels;}

void *dynos_texture_read(const char *path,uint64_t *size) {
    auto it=textures.find(path);if(it==textures.end())return nullptr;
    const auto &bytes=it->second->bytes;void *copy=malloc(bytes.size());
    if(copy){memcpy(copy,bytes.data(),bytes.size());*size=bytes.size();}return copy;
}
void dynos_shutdown(void) {
    // Must follow gfx_shutdown: texture-cache keys point into these records.
    release_models();registered_models.clear();entries.clear();npc_entries.clear();texture_packs.clear();npc_packs.clear();apply_requested=false;first_update=true;applied_choice=~0u;dynos_player_choice=0;pending_step=0;
}
