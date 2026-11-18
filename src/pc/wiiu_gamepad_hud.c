#include "wiiu_gamepad_hud.h"

static bool s_touch_down;
static bool s_touch_press;

void wiiu_gamepad_set_touch(bool touched) {
    if (touched && !s_touch_down) s_touch_press = !s_touch_press;
    s_touch_down = touched;
}

bool wiiu_gamepad_take_touch_press(void) {
    bool pressed = s_touch_press;
    s_touch_press = false;
    return pressed;
}

#ifdef WIIU_MINIMAP

#include <string.h>

#include "audio/external.h"
#include "game/game_init.h"
#include "game/ingame_menu.h"
#include "game/level_update.h"
#include "seq_ids.h"

bool wiiu_gamepad_hud_get_frame(struct WiiUGamePadHudFrame *frame) {
    if (frame == NULL) {
        return false;
    }

    memset(frame, 0, sizeof(*frame));
    frame->lives = gHudDisplay.lives;
    frame->coins = gHudDisplay.coins;
    frame->stars = gHudDisplay.stars;
    frame->wedges = gHudDisplay.wedges;
    frame->keys = gHudDisplay.keys;
    frame->timer = gHudDisplay.timer;
    frame->flags = (uint16_t) gHudDisplay.flags;
    frame->red_coins = gRedCoinsCollected;
    frame->red_coin_frame = (gGlobalTimer & 6) >> 1;

    /* Match the 3DS implementation: display the sequence which is actually
       playing, including temporary boss, cap and event music. */
    u16 sequence = get_current_background_music();
    u8 sequence_id = sequence & 0x7F;
    if (sequence != 0xFFFF && sequence_id < SEQ_COUNT) {
        frame->music_id = sequence_id;
        frame->music_valid = true;
    }
    return gHudDisplay.flags != HUD_DISPLAY_NONE;
}

#else

bool wiiu_gamepad_hud_get_frame(struct WiiUGamePadHudFrame *frame) {
    (void) frame;
    return false;
}

#endif
