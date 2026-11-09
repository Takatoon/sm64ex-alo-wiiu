#ifndef WIIU_GAMEPAD_LAYOUT_H
#define WIIU_GAMEPAD_LAYOUT_H

#include <stdbool.h>
#include <math.h>

enum { WIIU_MAP_SIZE = 480, WIIU_MAP_TITLE_MARGIN = 12 };

struct WiiUMapRect {
    int x, y, width, height;
};

static const struct WiiUMapRect WIIU_MAP_CLASSIC_RECT = { 0, 0, 480, 480 };
static const struct WiiUMapRect WIIU_MAP_FRAMED_RECT = { 25, 48, 448, 394 };
static const struct WiiUMapRect WIIU_MAP_ZOOM_BUTTON = { 434, 403, 33, 33 };

static inline int wiiu_map_sample(int pixel, int pixels, int origin, int scale) {
    return origin + (2 * pixel + 1) * (WIIU_MAP_SIZE / scale) / (2 * pixels);
}

struct WiiUMapView {
    int x, y, scale;
};

static inline struct WiiUMapView wiiu_map_view(bool zoomed, float mario_x, float mario_y) {
    struct WiiUMapView view = { 0, 0, zoomed ? 2 : 1 };
    if (zoomed) {
        const int extent = WIIU_MAP_SIZE / view.scale;
        view.x = (int)floorf(mario_x - extent / 2);
        view.y = (int)floorf(mario_y - extent / 2);
        if (view.x < 0) view.x = 0;
        if (view.y < 0) view.y = 0;
        if (view.x > WIIU_MAP_SIZE - extent) view.x = WIIU_MAP_SIZE - extent;
        if (view.y > WIIU_MAP_SIZE - extent) view.y = WIIU_MAP_SIZE - extent;
    }
    return view;
}

/* Width and draw positions must use this same scale, including HD glyphs. */
static inline float wiiu_map_title_scale(float width, float height) {
    if (width <= 0 || height <= 0) return 0;
    const float available = WIIU_MAP_SIZE - 2 * WIIU_MAP_TITLE_MARGIN;
    float scale = 48.0f / height;
    if (width * scale > available) scale = available / width;
    return scale;
}

static inline int wiiu_map_title_x(int width) {
    return WIIU_MAP_SIZE - WIIU_MAP_TITLE_MARGIN - width;
}

#endif
