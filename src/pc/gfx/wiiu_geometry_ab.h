#ifndef WIIU_GEOMETRY_AB_H
#define WIIU_GEOMETRY_AB_H

#include <stdbool.h>
#include <stdint.h>

#define WIIU_AB_SETTLE_US 3000000ULL
#define WIIU_AB_BLOCK_US 20000000ULL
#define WIIU_AB_DISCARD_US 1000000ULL

enum WiiUAbEvent { WIIU_AB_NONE, WIIU_AB_RESET, WIIU_AB_START, WIIU_AB_NEXT, WIIU_AB_DONE };
struct WiiUGeometryAb {
    uint64_t last_us, stable_us, block_us;
    unsigned block;
    bool initialized, running, complete;
};

/* Called once per simulation/render pair, after the preceding pair completed.
 * Long gaps (e.g. HOME) and scene changes invalidate the comparison. */
static inline enum WiiUAbEvent wiiu_geometry_ab_step(struct WiiUGeometryAb *s,
        uint64_t now, bool eligible, bool scene_changed) {
    bool gap = s->initialized && (now < s->last_us || now - s->last_us > 500000);
    s->last_us = now;
    if (!eligible || scene_changed || gap || !s->initialized) {
        bool was_running = s->running || s->complete;
        s->initialized = eligible;
        s->running = s->complete = false;
        s->stable_us = now;
        s->block = 0;
        return was_running ? WIIU_AB_RESET : WIIU_AB_NONE;
    }
    if (!s->running && !s->complete && now - s->stable_us >= WIIU_AB_SETTLE_US) {
        s->running = true;
        s->block_us = now;
        return WIIU_AB_START;
    }
    if (s->running && now - s->block_us >= WIIU_AB_BLOCK_US) {
        if (s->block == 3) {
            s->running = false;
            s->complete = true;
            return WIIU_AB_DONE;
        }
        ++s->block;
        s->block_us = now;
        return WIIU_AB_NEXT;
    }
    return WIIU_AB_NONE;
}

/* Select the experimental implementation in odd blocks; default to it after
 * completion. Storage stays direct in both sides of the writer experiment. */
static inline bool wiiu_geometry_ab_alternate(const struct WiiUGeometryAb *s) {
    return !s->running || (s->block & 1) != 0;
}

static inline bool wiiu_geometry_ab_measure(const struct WiiUGeometryAb *s, uint64_t now) {
    return s->running && now >= s->block_us && now - s->block_us >= WIIU_AB_DISCARD_US;
}
#endif
