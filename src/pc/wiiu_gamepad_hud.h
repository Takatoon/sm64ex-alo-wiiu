#ifndef WIIU_GAMEPAD_HUD_H
#define WIIU_GAMEPAD_HUD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum WiiUGamePadHudFlag {
    WIIU_GAMEPAD_HUD_LIVES = 0x0001,
    WIIU_GAMEPAD_HUD_COINS = 0x0002,
    WIIU_GAMEPAD_HUD_STARS = 0x0004,
    WIIU_GAMEPAD_HUD_POWER = 0x0008,
    WIIU_GAMEPAD_HUD_TIMER = 0x0040,
};

struct WiiUGamePadHudFrame {
    int16_t lives;
    int16_t coins;
    int16_t stars;
    int16_t wedges;
    int16_t keys;
    uint16_t timer;
    uint16_t flags;
    int8_t red_coins;
    uint8_t red_coin_frame;
    uint8_t music_id;
    bool music_valid;
};

bool wiiu_gamepad_hud_get_frame(struct WiiUGamePadHudFrame *frame);

#ifdef __cplusplus
}
#endif

#endif
