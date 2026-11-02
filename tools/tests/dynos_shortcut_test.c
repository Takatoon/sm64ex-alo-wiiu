#include <assert.h>
#include "pc/dynos/dynos_shortcut.h"
int main(void) {
    assert(dynos_shortcut_step(0,2)==0);
    assert(dynos_shortcut_step(0,1)==0);
    assert(dynos_shortcut_step(1,3)==-1);
    assert(dynos_shortcut_step(3,3)==0);
    assert(dynos_shortcut_step(1,5)==1);
    assert(dynos_shortcut_step(5,5)==0);
    assert(dynos_shortcut_step(0,7)==0);
    assert(dynos_shortcut_step(2,3)==-1);
    assert(dynos_shortcut_step(3,5)==1);
    assert(dynos_shortcut_step(3,0)==0);
    assert(dynos_shortcut_step(0,3)==-1);
    return 0;
}
