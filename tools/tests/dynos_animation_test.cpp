#include <cassert>
#include "pc/dynos/dynos_animation.h"
int main(){
    Animation source{},target{};source.loopEnd=18;target.loopEnd=17;
    assert(dynos_animation_frame(&source,&target,-1)==0);
    assert(dynos_animation_frame(&source,&target,0)==0);
    assert(dynos_animation_frame(&source,&target,17)==16);
    assert(dynos_animation_frame(&source,&target,40)==16);
    for(int i=0;i<18;++i){int frame=dynos_animation_frame(&source,&target,i);assert(frame>=0&&frame<17);}
    source.loopStart=4;source.loopEnd=12;target.loopStart=2;target.loopEnd=6;
    assert(dynos_animation_frame(&source,&target,0)==0);
    assert(dynos_animation_frame(&source,&target,3)==1);
    assert(dynos_animation_frame(&source,&target,4)==2);
    assert(dynos_animation_frame(&source,&target,11)==5);
    return 0;
}
