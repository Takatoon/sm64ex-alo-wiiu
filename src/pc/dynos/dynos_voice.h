#ifndef PC_DYNOS_VOICE_H
#define PC_DYNOS_VOICE_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Loading is confined to model selection, never the audio callback. */
void dynos_voice_select(const char *pack, const char *model, uint64_t budget);
void dynos_voice_clear(void);
uint64_t dynos_voice_resident_bytes(void);
bool dynos_voice_event(uint32_t bits, const float *source, bool start, float gain, float pan, float pitch);
void dynos_voice_stop(uint32_t bits, const float *source);
void dynos_voice_stop_source(const float *source);
void dynos_voice_stop_bank(unsigned bank);
void dynos_voice_set_muted(bool muted);
void dynos_voice_mix(int16_t *stereo, unsigned frames, float master_gain);
#ifdef __cplusplus
}
#endif
#endif
