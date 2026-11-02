#include "pc/dynos/dynos_model.h"
#include "pc/dynos/dynos_animation.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <functional>
extern "C" {
#include "engine/geo_layout.h"
#include "engine/graph_node.h"
void *alloc_only_pool_alloc(AllocOnlyPool*,s32 n){return calloc(1,n);}
void *segmented_to_virtual(const void *p){return const_cast<void*>(p);}
Gfx *noop(s32,GraphNode*,void*){return nullptr;}
}
namespace bobomb {
#include "actors/bobomb/anims/data.inc.c"
#include "actors/bobomb/anims/table.inc.c"
}
namespace chuckya {
#include "actors/chuckya/anims/data.inc.c"
#include "actors/chuckya/anims/table.inc.c"
}
namespace skeeter {
#include "actors/skeeter/anims/data.inc.c"
#include "actors/skeeter/anims/table.inc.c"
}
uintptr_t callback(unsigned){return reinterpret_cast<uintptr_t>(noop);}
int main(int argc,char **argv){
 assert(argc==4);std::string actor=argv[3];std::ifstream f(argv[1],std::ios::binary),parents(argv[2]);
 std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),{});std::vector<int> expected;int parent;while(parents>>parent)expected.push_back(parent);
 bool custom=actor!="scuttlebug_geo";std::string error;
 auto model=dynos::load(data.data(),data.size(),"probe",actor,callback,error,&expected,custom);
 if(!model){std::cerr<<error<<"\n";return 1;}
 auto root=process_geo_layout(reinterpret_cast<AllocOnlyPool*>(1),model->root->layout.data());
 unsigned joints=0;
 std::function<void(GraphNode*)> scan=[&](GraphNode *first){if(!first)return;auto p=first;do{if(p->type==GRAPH_NODE_TYPE_ANIMATED_PART)++joints;scan(p->children);p=p->next;}while(p!=first);};
 if(!custom){scan(root);assert(joints==expected.size());assert(model->static_joint_count==1);std::cout<<"static joint: native graph has "<<joints<<" animated joints\n";return 0;}
 const Animation *const *bank=nullptr;unsigned slots=0;
 if(actor=="chuckya_geo"){bank=chuckya::chuckya_seg8_anims_0800C070;slots=5;}
 else if(actor=="skeeter_geo"){bank=skeeter::skeeter_seg6_anims_06007DE0;slots=4;}
 else {bank=bobomb::bobomb_seg8_anims_0802396C;slots=2;}
 for(unsigned i=0;i<slots;++i){
  assert(i<model->animation_table.size()&&model->animation_table[i]);const Animation *a=&model->animation_table[i]->native,*n=bank[i];
  assert(a->flags==n->flags&&a->animYTransDivisor==n->animYTransDivisor);
  for(int frame=0;frame<n->loopEnd;++frame){
   int mapped=dynos_animation_frame(n,a,frame);assert(mapped>=0&&mapped<a->loopEnd);
   for(unsigned channel=0;channel<model->animation_table[i]->indices.size();channel+=2){
    auto &idx=model->animation_table[i]->indices;size_t offset=idx[channel+1]+std::min<unsigned>(mapped,idx[channel]-1);
    assert(offset<model->animation_table[i]->values.size());volatile int value=a->values[offset];(void)value;
   }
  }
 }
 std::cout<<actor<<" native slots="<<slots<<" all frames/channels validated\n";
}
