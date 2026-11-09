#ifdef WIIU_MINIMAP
#include <string.h>
#include <coreinit/cache.h>
#include <sndcore2/device.h>
#include <whb/log.h>
#include "audio_wiiu_gamepad.h"

/* WUT currently declares this callback's argument as void*. The ABI is
 * described by decaf-emu's sndcore2_device.h (layout facts only):
 * https://github.com/decaf-emu/decaf-emu/blob/master/src/libdecaf/src/cafe/libraries/sndcore2/sndcore2_device.h
 * Samples are planar signed 32-bit values in the PCM16 amplitude range. */
typedef struct GamePadFinalMix {
    int32_t **data;
    uint16_t channels, samples, devices, channels_out;
} GamePadFinalMix;
WUT_CHECK_OFFSET(GamePadFinalMix, 0x4, channels);
WUT_CHECK_OFFSET(GamePadFinalMix, 0x6, samples);
WUT_CHECK_SIZE(GamePadFinalMix, 0xc);

#define ALERT_QUEUE_FRAMES 4096u
static int16_t sSamples[ALERT_QUEUE_FRAMES][2];
/* One audio producer and one AX consumer. Each owns its index; aligned 32-bit
 * loads/stores plus release/acquire barriers avoid RMW atomics on Espresso. */
static volatile uint32_t sWritten, sRead, sModeGeneration;
static volatile bool sAlertsOnly, sForeground = true;
static bool sInstalled;
static uint32_t sConsumerGeneration, sFraction;
static AXDeviceFinalMixCallback sPreviousCallback;

static void gamepad_final_mix(void *argument) {
    struct GamePadFinalMix *mix = argument;
    if (sPreviousCallback) sPreviousCallback(argument);
    uint32_t written = sWritten;
    uint32_t generation = sModeGeneration;
    OSMemoryBarrier();
    if (generation != sConsumerGeneration || !sAlertsOnly || !sForeground) {
        sRead = written;
        sFraction = 0;
        sConsumerGeneration = generation;
    }
    if (!sAlertsOnly) return; /* Mirror TV keeps SDL's original mix. */
    if (!mix || !mix->data || !mix->channels || mix->channels > 4 ||
        !mix->devices || mix->devices > 2 ||
        (mix->samples != 96 && mix->samples != 144)) return;

    uint32_t read = sRead;
    for (unsigned sample = 0; sample < mix->samples; ++sample) {
        int32_t stereo[2] = {0, 0};
        if (sForeground && read != written) {
            unsigned slot = read % ALERT_QUEUE_FRAMES;
            unsigned next = written - read > 1 ? (read + 1) % ALERT_QUEUE_FRAMES : slot;
            for (unsigned channel = 0; channel < 2; ++channel) {
                int32_t first = sSamples[slot][channel];
                stereo[channel] = first +
                    (sSamples[next][channel] - first) * (int32_t)sFraction / mix->samples;
            }
            /* AX consumes 3ms per callback: 96 frames at 32kHz, or 144 at
             * 48kHz. Keep exact rational phase across callbacks. */
            sFraction += 96;
            read += sFraction / mix->samples;
            sFraction %= mix->samples;
        } else {
            sFraction = 0; /* Underruns are silence, never a repeated alarm. */
        }
        for (unsigned device = 0; device < mix->devices; ++device)
            for (unsigned channel = 0; channel < mix->channels; ++channel)
                mix->data[device * mix->channels + channel][sample] =
                    device == 0 && channel < 2 ? stereo[channel] : 0;
    }
    OSMemoryBarrier();
    sRead = read;
}

void wiiu_gamepad_audio_init(bool alerts_only) {
    sWritten = sRead = sModeGeneration = sConsumerGeneration = sFraction = 0;
    sForeground = true;
    sAlertsOnly = alerts_only;
    sPreviousCallback = NULL;
    if (AXGetDeviceFinalMixCallback(AX_DEVICE_TYPE_DRC, &sPreviousCallback) != 0 ||
        AXRegisterDeviceFinalMixCallback(AX_DEVICE_TYPE_DRC, gamepad_final_mix) != 0) {
        WHBLogPrint("[gamepad-audio] final mix unavailable");
        return;
    }
    sInstalled = true;
    WHBLogPrint("[gamepad-audio] DRC filter ready; Status Panel plays drowning alert only");
}

void wiiu_gamepad_audio_set_mode(bool alerts_only) {
    if (sAlertsOnly == alerts_only) return;
    sAlertsOnly = alerts_only;
    OSMemoryBarrier();
    ++sModeGeneration;
    WHBLogPrintf("[gamepad-audio] output=%s", alerts_only ? "drowning-alert-only" : "mirror");
}

bool wiiu_gamepad_audio_alerts_only(void) {
    return sInstalled && sAlertsOnly;
}

void wiiu_gamepad_audio_queue(const int16_t *samples, size_t frames) {
    if (!sInstalled || !sAlertsOnly || !sForeground || !samples) return;
    uint32_t read = sRead, written = sWritten;
    OSMemoryBarrier();
    if (frames > ALERT_QUEUE_FRAMES - (written - read)) return;
    for (size_t frame = 0; frame < frames; ++frame) {
        unsigned slot = (written + frame) % ALERT_QUEUE_FRAMES;
        sSamples[slot][0] = samples[frame * 2];
        sSamples[slot][1] = samples[frame * 2 + 1];
    }
    OSMemoryBarrier();
    sWritten = written + frames;
}

void wiiu_gamepad_audio_set_foreground(bool foreground) {
    sForeground = foreground;
    OSMemoryBarrier();
    ++sModeGeneration;
}

void wiiu_gamepad_audio_shutdown(void) {
    if (!sInstalled) return;
    AXDeviceFinalMixCallback current;
    if (AXGetDeviceFinalMixCallback(AX_DEVICE_TYPE_DRC, &current) == 0 && current == gamepad_final_mix)
        AXRegisterDeviceFinalMixCallback(AX_DEVICE_TYPE_DRC, sPreviousCallback);
    sInstalled = false;
}
#endif
