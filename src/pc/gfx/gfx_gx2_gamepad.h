#ifndef GFX_GX2_GAMEPAD_H
#define GFX_GX2_GAMEPAD_H
#include <stdint.h>
#include <gx2/surface.h>
#include "../wiiu_gamepad_layout.h"

bool gfx_gx2_gamepad_ready(void);
GX2ColorBuffer *gfx_gx2_gamepad_compose(const GX2ColorBuffer *overlay,
    const uint8_t *map, uint32_t generation, const WiiUMapView& view, const WiiUMapRect& map_rect);
void gfx_gx2_gamepad_free(void);
#endif
