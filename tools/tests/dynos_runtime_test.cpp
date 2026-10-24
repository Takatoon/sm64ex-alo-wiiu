#include <cassert>
#include <cstdlib>
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
MarioBodyState gBodyStates[2];
MarioState *gMarioState=nullptr;
static int saves;
void configfile_save(const char *){++saves;}
const char *configfile_name(void){return "test-config";}
AllocOnlyPool *alloc_only_pool_init(void){static int pool;return reinterpret_cast<AllocOnlyPool*>(&pool);}
GraphNode *process_geo_layout(AllocOnlyPool *,void *){
    auto node=static_cast<GraphNode*>(calloc(1,sizeof(GraphNode)));
    node->next=node->prev=node;return node;
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
int main(int argc,char **argv) {
    assert(argc==2);const char *ro[]={nullptr};assert(fs_init(ro,"absent",argv[1]));
    dynos_init();assert(dynos_player_count()==2);dynos_update();
    GraphNode root=part(),a=part(),b=part();
    root.next=root.prev=&root;root.children=&a;a.next=&b;b.next=&a;
    dynos_bind_model(42,toad_geo,&root);
    GraphNode *replacement=dynos_npc_graph(&root);assert(replacement!=&root);
    dynos_clear_npc_bindings();assert(dynos_npc_graph(&root)==&root);
    dynos_bind_model(42,toad_geo,&root);assert(dynos_npc_graph(&root)==replacement);
    dynos_bind_model(42,nullptr,&a);assert(dynos_npc_graph(&root)==&root);
    dynos_bind_model(43,black_bobomb_geo,&root);
    const Animation *alternate=nullptr;
    assert(dynos_npc_graph_with_animation(&root,&walk,&alternate)!=&root);
    assert(alternate&&alternate->unusedBoneCount==4&&alternate->loopEnd==4);
    assert(walk.loopEnd==8&&walk.unusedBoneCount==3);
    assert(dynos_npc_graph_with_animation(&root,&missing,&alternate)==&root&&alternate==nullptr);
    assert(dynos_npc_graph_with_animation(&root,nullptr,&alternate)==&root);
    dynos_clear_npc_bindings();
    assert(dynos_npc_graph_with_animation(&root,&walk,&alternate)==&root);
    dynos_request_player_step(1);dynos_update();assert(dynos_player_choice==1&&configDynosModel!=0&&saves==1);
    dynos_update();assert(saves==1);
    dynos_request_player_step(-1);dynos_update();assert(dynos_player_choice==0&&configDynosModel==0&&saves==2);
    dynos_request_player_step(-1);dynos_update();assert(dynos_player_choice==1&&saves==3);
    dynos_shutdown();return 0;
}
