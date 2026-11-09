#ifndef WIIU_GAMEPAD_FONT_H
#define WIIU_GAMEPAD_FONT_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

struct WiiUGamePadGlyphBounds {
    int left, right;
    float width;
};

/* SM64's dialog glyph PNGs are stored transversely (also in HD packs).
   Measure upright alpha bounds, normalized to the original 16-pixel height. */
static inline struct WiiUGamePadGlyphBounds wiiu_gamepad_glyph_bounds(
        const uint8_t *pixels, int width, int height) {
    struct WiiUGamePadGlyphBounds bounds = { height, -1, 0 };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (pixels[(y * width + x) * 4 + 3]) {
                int upright_x = height - 1 - y;
                if (upright_x < bounds.left) bounds.left = upright_x;
                if (upright_x > bounds.right) bounds.right = upright_x;
            }
        }
    }
    if (bounds.right >= bounds.left) bounds.width = (bounds.right - bounds.left + 1) * 16.0f / width;
    return bounds;
}

/* Render to a transparent, cached title strip, not to the map. The same
   upright geometry works for native and HD textures without stretching. */
static inline void wiiu_gamepad_draw_glyph(uint8_t *dest, int dest_width, int dest_height,
        const uint8_t *source, int width, int height,
        struct WiiUGamePadGlyphBounds bounds, int x, int y, int draw_width, int draw_height,
        bool shadow) {
    if (draw_width <= 0 || draw_height <= 0) return;
    for (int dy = 0; dy < draw_height; ++dy) {
        int out_y = y + dy;
        if (out_y < 0 || out_y >= dest_height) continue;
        int sx = width - 1 - dy * width / draw_height;
        for (int dx = 0; dx < draw_width; ++dx) {
            int out_x = x + dx;
            if (out_x < 0 || out_x >= dest_width) continue;
            int sy = height - 1 - (bounds.left + dx * (bounds.right - bounds.left + 1) / draw_width);
            const uint8_t *pixel = source + (sy * width + sx) * 4;
            unsigned alpha = pixel[3];
            if (!alpha) continue;
            uint8_t *out = dest + (out_y * dest_width + out_x) * 4;
            unsigned old_alpha = out[3];
            unsigned new_alpha = alpha + old_alpha * (255 - alpha) / 255;
            for (int c = 0; c < 3; ++c) {
                unsigned color = shadow ? 0 : pixel[c];
                out[c] = (color * alpha + out[c] * old_alpha * (255 - alpha) / 255) / new_alpha;
            }
            out[3] = new_alpha;
        }
    }
}
#endif
