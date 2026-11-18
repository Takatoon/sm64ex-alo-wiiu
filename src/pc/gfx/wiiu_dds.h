#ifndef WIIU_DDS_H
#define WIIU_DDS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum WiiUDdsFormat { WIIU_DDS_RGBA8, WIIU_DDS_BC1, WIIU_DDS_BC3 };

struct WiiUDdsImage {
    const uint8_t *data;
    size_t row_stride;
    uint32_t width, height;
    enum WiiUDdsFormat format;
};

/* Narrow DDS subset: BC1, BC3 or tightly packed RGBA8, one 2D image/level.
 * Parse fields bytewise: Wii U is big endian and input may be unaligned. */
static inline uint32_t wiiu_dds_u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static inline bool wiiu_dds_parse(const void *data, size_t size, struct WiiUDdsImage *out) {
    if (!data || !out || size < 128) return false;
    const uint8_t *p = (const uint8_t *)data;
    if (memcmp(p,"DDS ",4) || wiiu_dds_u32(p+4)!=124 || wiiu_dds_u32(p+76)!=32 ||
        wiiu_dds_u32(p+24)>1 ||
        wiiu_dds_u32(p+28)>1 || wiiu_dds_u32(p+112)!=0) return false;
    struct WiiUDdsImage view = {0};
    view.width = wiiu_dds_u32(p+16); view.height = wiiu_dds_u32(p+12);
    if (!view.width || !view.height || view.width > 4096 || view.height > 4096)
        return false;
    uint32_t flags = wiiu_dds_u32(p+80);
    if (flags == 4 && !memcmp(p+84,"DXT1",4)) view.format = WIIU_DDS_BC1;
    else if (flags == 4 && !memcmp(p+84,"DXT5",4)) view.format = WIIU_DDS_BC3;
    else if (flags == 0x41 && wiiu_dds_u32(p+84)==0 && wiiu_dds_u32(p+88)==32 &&
             wiiu_dds_u32(p+92)==0xff && wiiu_dds_u32(p+96)==0xff00 &&
             wiiu_dds_u32(p+100)==0xff0000 && wiiu_dds_u32(p+104)==0xff000000 &&
             (wiiu_dds_u32(p+8)&8) && wiiu_dds_u32(p+20)==(uint64_t)view.width*4)
        view.format = WIIU_DDS_RGBA8;
    else return false; /* Reject BGRA, padded rows and ambiguous masks. */
    const bool rgba = view.format == WIIU_DDS_RGBA8;
    const size_t rows = rgba ? view.height : (view.height + 3) / 4;
    view.row_stride = rgba ? view.width * 4 :
        ((view.width + 3) / 4) * (view.format == WIIU_DDS_BC1 ? 8 : 16);
    if (size - 128 != view.row_stride * rows) return false;
    view.data = p + 128;
    *out = view;
    return true;
}
#endif
