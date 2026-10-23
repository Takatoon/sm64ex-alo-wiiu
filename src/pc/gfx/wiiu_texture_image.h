#ifndef WIIU_TEXTURE_IMAGE_H
#define WIIU_TEXTURE_IMAGE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* CPU-side, borrowed image. No GX2 objects and no ownership transfer. The
 * renderer consumes this view synchronously; the caller may free data after
 * upload returns. This view describes decoded RGBA8 pixels, not a file format. */
enum WiiUTextureFormat { WIIU_TEXTURE_RGBA8 };
struct WiiUTextureImage {
    const uint8_t *data;
    size_t size;
    size_t row_stride;
    uint32_t width, height;
    enum WiiUTextureFormat format;
};

/* Copy CPU rows into GX2 linear-aligned storage. Validate every span before
 * touching the destination; padding is initialized, not read from the PNG. */
static inline bool wiiu_texture_copy_rows(void *destination, size_t size, size_t pitch,
                                          const struct WiiUTextureImage *image) {
    if (!image || !destination || !image->data || !image->width || !image->height ||
        image->format != WIIU_TEXTURE_RGBA8 || image->width > SIZE_MAX / 4) return false;
    size_t row = (size_t)image->width * 4;
    if (image->row_stride < row || pitch < row ||
        image->height > size / pitch ||
        (image->height - 1) > (SIZE_MAX - row) / image->row_stride ||
        image->size < (image->height - 1) * image->row_stride + row) return false;
    memset(destination, 0, size);
    for (uint32_t y = 0; y < image->height; ++y)
        memcpy((uint8_t *)destination + y * pitch, image->data + y * image->row_stride, row);
    return true;
}
#endif
