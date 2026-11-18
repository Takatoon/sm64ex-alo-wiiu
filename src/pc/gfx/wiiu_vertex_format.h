#ifndef WIIU_VERTEX_FORMAT_H
#define WIIU_VERTEX_FORMAT_H
#include <stdint.h>

/* Byte stores avoid host-endian and strict-aliasing assumptions. GX2 fetch
 * uses UNORM_8_8_8_8 with no endian swap for this canonical RGBA byte order. */
static inline void wiiu_vertex_store_rgba8(float *word,
        uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint8_t *bytes = (uint8_t *)word;
    bytes[0] = r; bytes[1] = g; bytes[2] = b; bytes[3] = a;
}
#endif
