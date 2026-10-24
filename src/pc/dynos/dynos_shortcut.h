#ifndef DYNOS_SHORTCUT_H
#define DYNOS_SHORTCUT_H
/* Physical controller state; distinct from N64 L and camera bindings. */
#define DYNOS_KEY_MINUS 1
#define DYNOS_KEY_LEFT 2
#define DYNOS_KEY_RIGHT 4
static inline int dynos_shortcut_step(unsigned previous,unsigned current) {
    unsigned direction=current&(DYNOS_KEY_LEFT|DYNOS_KEY_RIGHT);
    if(!(current&DYNOS_KEY_MINUS)||direction==0||direction==(DYNOS_KEY_LEFT|DYNOS_KEY_RIGHT))return 0;
    if((previous&DYNOS_KEY_MINUS)&&(previous&direction))return 0;
    return direction==DYNOS_KEY_LEFT?-1:1;
}
#endif
