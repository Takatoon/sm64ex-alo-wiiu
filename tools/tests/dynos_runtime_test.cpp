#include <cassert>
#include <cstdlib>
#include <vector>
#include "pc/dynos/dynos.h"
extern "C" {
#include <PR/mbi.h>
#include "engine/geo_layout.h"
#include "engine/graph_node.h"
#include "pc/fs/fs.h"
#include "pc/configfile.h"
extern const GeoLayout toad_geo[]={0};
extern const GeoLayout black_bobomb_geo[]={1};
static const Animation walk={0,0,0,0,8,3,nullptr,nullptr,0};
static const Animation missing={0,0,0,0,12,3,nullptr,nullptr,0};
extern const Animation *const bobomb_seg8_anims_0802396C[]={&walk,&missing,nullptr};
unsigned int configDynosModel=0;
bool configModsEnabled=true;
unsigned int configDynosTexturePack=1,configDynosNpcPack=0;
MarioBodyState gBodyStates[2];
MarioState *gMarioState=nullptr;
static int saves,clears,ends;
static bool gpu_ready=true;
static std::vector<GraphNode*> graphs;
static bool begin_reload(){return gpu_ready;}
static void end_reload(){++ends;}
void configfile_save(const char *){++saves;}
const char *configfile_name(void){return "test-config";}
AllocOnlyPool *alloc_only_pool_init(void){static int pool;return reinterpret_cast<AllocOnlyPool*>(&pool);}
void alloc_only_pool_clear(AllocOnlyPool*){for(auto n:graphs)free(n);graphs.clear();++clears;}
GraphNode *process_geo_layout(AllocOnlyPool *,void *){
    auto node=static_cast<GraphNode*>(calloc(1,sizeof(GraphNode)));
    node->next=node->prev=node;graphs.push_back(node);return node;
}
void dynos_test_callback(void){}
}
/* Rendering callbacks are resolved but not executed by this runtime test. */
#define STUB(name) asm(".global " #name "\n.set " #name ",dynos_test_callback");
STUB(geo_mirror_mario_set_alpha) STUB(geo_switch_mario_stand_run)
STUB(geo_switch_mario_eyes) STUB(geo_mario_tilt_torso)
STUB(geo_mario_head_rotation) STUB(geo_switch_mario_hand)
STUB(geo_mario_hand_foot_scaler) STUB(geo_switch_mario_cap_effect)
STUB(geo_switch_mario_cap_on_off) STUB(geo_mario_rotate_wing_cap_wings)
STUB(geo_switch_mario_hand_grab_pos) STUB(geo_mirror_mario_backface_culling)
STUB(geo_update_projectile_pos_from_parent) STUB(geo_update_layer_transparency)
STUB(geo_switch_anim_state) STUB(geo_move_mario_part_from_parent)
STUB(geo_cannon_circle_base) STUB(geo_bits_bowser_coloring)
STUB(geo_update_body_rot_from_parent) STUB(geo_switch_bowser_eyes)
STUB(geo_switch_tuxie_mother_eyes) STUB(geo_update_held_mario_pos)
STUB(geo_snufit_move_mask) STUB(geo_snufit_scale_body)
STUB(geo_scale_bowser_key) STUB(geo_offset_klepto_held_object)
STUB(geo_switch_peach_eyes)
static GraphNode part(){GraphNode n{};n.type=GRAPH_NODE_TYPE_ANIMATED_PART;return n;}
static void apply(){dynos_request_apply();dynos_update();assert(!dynos_apply_pending());}
int main(int argc,char **argv) {
    assert(argc==2);const char *ro[]={nullptr};assert(fs_init(ro,"absent",argv[1]));
    dynos_set_reload_hooks(begin_reload,end_reload);
    dynos_init();assert(dynos_player_count()==2);assert(dynos_npc_pack_count()==4);dynos_update();
    GraphNode root=part(),a=part(),b=part();
    root.next=root.prev=&root;root.children=&a;a.next=&b;b.next=&a;
    dynos_bind_model(42,toad_geo,&root);assert(dynos_npc_graph(&root)==&root);
    dynos_npc_choice=2;dynos_update();assert(dynos_npc_graph(&root)==&root); // Staged only.
    gpu_ready=false;dynos_request_apply();dynos_update();
    assert(dynos_apply_pending()&&dynos_resident_bytes()==0&&clears==0&&saves==0);
    gpu_ready=true;dynos_update();assert(!dynos_apply_pending());
    assert(dynos_npc_graph(&root)!=&root&&dynos_resident_bytes()>0&&clears==1&&ends==1);
    unsigned bytes=dynos_resident_bytes();unsigned savedNpc=configDynosNpcPack;
    assert(savedNpc!=0);
    dynos_npc_choice=0;apply();assert(dynos_npc_graph(&root)==&root&&dynos_resident_bytes()==0);
    dynos_npc_choice=2;apply();assert(dynos_npc_graph(&root)!=&root&&dynos_resident_bytes()==bytes);
    dynos_npc_choice=3;apply();assert(dynos_npc_graph(&root)!=&root);
    dynos_npc_choice=1;apply(); // Broken pack must not borrow models from B or C.
    assert(dynos_npc_graph(&root)==&root&&dynos_resident_bytes()==0);
    dynos_npc_choice=2;apply();
    dynos_mods_enabled=0;dynos_player_choice=1;apply();
    assert(dynos_npc_graph(&root)==&root&&dynos_player_graph(&root)==&root&&dynos_resident_bytes()==0);
    assert(!configModsEnabled&&configDynosModel!=0&&configDynosNpcPack==savedNpc);
    dynos_mods_enabled=1;apply();
    assert(dynos_npc_graph(&root)!=&root&&dynos_player_graph(&root)!=&root);
    dynos_texture_choice=0; // Shortcut must not apply this staged setting.
    dynos_request_player_step(-1);dynos_update();
    assert(dynos_player_choice==0&&configDynosModel==0&&configDynosTexturePack==1);
    assert(dynos_texture_choice==0);
    GraphNode r2=part(),a2=part(),b2=part();r2.next=r2.prev=&r2;r2.children=&a2;a2.next=&b2;b2.next=&a2;
    dynos_bind_model(43,black_bobomb_geo,&r2);
    const Animation *alternate=nullptr;
    assert(dynos_npc_graph_with_animation(&r2,&walk,&alternate)!=&r2);
    assert(alternate&&alternate->unusedBoneCount==4&&alternate->loopEnd==4);
    assert(walk.loopEnd==8&&walk.unusedBoneCount==3);
    assert(dynos_npc_graph_with_animation(&r2,&missing,&alternate)==&r2&&alternate==nullptr);
    assert(dynos_npc_graph_with_animation(&r2,nullptr,&alternate)==&r2);
    for(int i=0;i<10;++i){
        dynos_mods_enabled=0;apply();assert(dynos_resident_bytes()==0&&graphs.empty());
        dynos_mods_enabled=1;apply();assert(dynos_npc_graph(&root)!=&root&&dynos_npc_graph(&r2)!=&r2);
    }
    dynos_clear_npc_bindings();
    apply();assert(dynos_npc_graph(&root)==&root&&dynos_npc_graph(&r2)==&r2&&dynos_resident_bytes()==0);
    dynos_bind_model(42,toad_geo,&root);assert(dynos_npc_graph(&root)!=&root);
    dynos_bind_model(42,nullptr,&a);apply();assert(dynos_npc_graph(&root)==&root);
    dynos_shutdown();assert(graphs.empty());
    dynos_init();assert(dynos_npc_choice==2&&dynos_texture_choice==0&&dynos_player_choice==0);
    dynos_update();dynos_bind_model(42,toad_geo,&root);assert(dynos_npc_graph(&root)!=&root);
    dynos_mods_enabled=0;dynos_player_choice=1;apply();dynos_shutdown();
    dynos_init();assert(dynos_mods_enabled==0&&dynos_player_choice==1&&dynos_npc_choice==2);
    dynos_update();dynos_bind_model(42,toad_geo,&root);
    assert(dynos_npc_graph(&root)==&root&&dynos_player_graph(&root)==&root&&dynos_resident_bytes()==0);
    dynos_mods_enabled=1;apply();
    assert(dynos_npc_graph(&root)!=&root&&dynos_player_graph(&root)!=&root);
    dynos_shutdown();return 0;
}
