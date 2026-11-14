#ifndef WIIU_COLOR_INPUT_H
#define WIIU_COLOR_INPUT_H
#include <stdbool.h>

#define WIIU_COLOR_MINUS 1u
#define WIIU_COLOR_LEFT 2u
#define WIIU_COLOR_RIGHT 4u
struct WiiUColorButton { bool held, chord; };

/* A standalone tap toggles once on release. Minus+left/right remains the
 * DynOS model shortcut and never toggles the color format. */
static inline bool wiiu_color_button_step(struct WiiUColorButton *s,
        unsigned keys, bool connected) {
    if (!connected) { s->held=s->chord=false; return false; }
    bool down=(keys&WIIU_COLOR_MINUS)!=0;
    if (down) {
        if (!s->held) s->chord=false;
        if (keys&(WIIU_COLOR_LEFT|WIIU_COLOR_RIGHT)) s->chord=true;
    }
    bool toggle=s->held&&!down&&!s->chord;
    s->held=down;
    if (!down) s->chord=false;
    return toggle;
}
#endif
