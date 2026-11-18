#ifndef WIIU_DDS_RGBA_H
#define WIIU_DDS_RGBA_H

#include <stdlib.h>
#include <string.h>
#include "wiiu_dds.h"

/* CPU counterpart of the GX2 DDS upload, used only for small UI assets.
   Decode once on load; the software GamePad compositor needs RGBA pixels. */
static inline void wiiu_dds_rgb565(uint16_t value, uint8_t *out) {
    unsigned r = value >> 11, g = (value >> 5) & 63, b = value & 31;
    out[0] = (r << 3) | (r >> 2);
    out[1] = (g << 2) | (g >> 4);
    out[2] = (b << 3) | (b >> 2);
    out[3] = 255;
}

static inline uint8_t *wiiu_dds_decode_rgba(const void *data, size_t size, int *width, int *height) {
    struct WiiUDdsImage image;
    if (!wiiu_dds_parse(data, size, &image))
        return NULL;
    size_t bytes = (size_t)image.width * image.height * 4;
    uint8_t *pixels = (uint8_t *)malloc(bytes);
    if (!pixels) return NULL;
    *width = image.width;
    *height = image.height;
    if (image.format == WIIU_DDS_RGBA8) {
        memcpy(pixels, image.data, bytes);
        return pixels;
    }
    const size_t block_size = image.format == WIIU_DDS_BC1 ? 8 : 16;
    for (unsigned by = 0; by < image.height; by += 4) {
        for (unsigned bx = 0; bx < image.width; bx += 4) {
            const uint8_t *block = (const uint8_t *)image.data +
                (by / 4) * image.row_stride + (bx / 4) * block_size;
            const uint8_t *color = block + block_size - 8;
            uint16_t c0 = color[0] | (uint16_t)color[1] << 8;
            uint16_t c1 = color[2] | (uint16_t)color[3] << 8;
            uint8_t colors[4][4] = {{0}};
            wiiu_dds_rgb565(c0, colors[0]);
            wiiu_dds_rgb565(c1, colors[1]);
            bool four_colors = c0 > c1 || image.format == WIIU_DDS_BC3;
            for (int channel = 0; channel < 3; ++channel) {
                colors[2][channel] = four_colors ? (2 * colors[0][channel] + colors[1][channel]) / 3
                                                 : (colors[0][channel] + colors[1][channel]) / 2;
                colors[3][channel] = four_colors ? (colors[0][channel] + 2 * colors[1][channel]) / 3 : 0;
            }
            colors[2][3] = 255;
            colors[3][3] = four_colors ? 255 : 0;
            uint32_t indices = wiiu_dds_u32(color + 4);
            uint8_t alphas[8] = {block[0], block[1], 0, 0, 0, 0, 0, 255};
            uint64_t alpha_indices = 0;
            if (image.format == WIIU_DDS_BC3) {
                for (int i = 0; i < 6; ++i) alpha_indices |= (uint64_t)block[i + 2] << (i * 8);
                if (alphas[0] > alphas[1]) {
                    for (int i = 2; i < 8; ++i)
                        alphas[i] = ((8 - i) * alphas[0] + (i - 1) * alphas[1]) / 7;
                } else {
                    for (int i = 2; i < 6; ++i)
                        alphas[i] = ((6 - i) * alphas[0] + (i - 1) * alphas[1]) / 5;
                }
            }
            for (unsigned py = 0; py < 4; ++py) {
                for (unsigned px = 0; px < 4; ++px) {
                    unsigned i = py * 4 + px;
                    if (bx + px >= image.width || by + py >= image.height) continue;
                    uint8_t *out = pixels + ((size_t)(by + py) * image.width + bx + px) * 4;
                    memcpy(out, colors[(indices >> (2 * i)) & 3], 4);
                    if (image.format == WIIU_DDS_BC3) out[3] = alphas[(alpha_indices >> (3 * i)) & 7];
                }
            }
        }
    }
    return pixels;
}
#endif
