#ifndef AUDIO_WIIU_GAMEPAD_H
#define AUDIO_WIIU_GAMEPAD_H

#ifdef WIIU_MINIMAP
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void wiiu_gamepad_audio_init(bool alerts_only);
void wiiu_gamepad_audio_set_mode(bool alerts_only);
bool wiiu_gamepad_audio_alerts_only(void);
void wiiu_gamepad_audio_queue(const int16_t *samples, size_t frames);
void wiiu_gamepad_audio_set_foreground(bool foreground);
void wiiu_gamepad_audio_shutdown(void);
#endif
#endif
