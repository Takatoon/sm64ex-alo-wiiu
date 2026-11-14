#ifndef GFX_GX2_H
#define GFX_GX2_H

#ifdef __cplusplus
extern "C" {
#endif

#include "gfx_window_manager_api.h"
#include "gfx_rendering_api.h"

extern struct GfxWindowManagerAPI gfx_gx2_window;
extern struct GfxRenderingAPI gfx_gx2_api;

extern uint32_t g_window_width;
extern uint32_t g_window_height;
extern uint32_t g_render_offset_x;
extern uint32_t g_render_offset_y;

void gfx_gx2_free_vbo(void);
/* Render thread only. Uncommitted space for one frontend batch; submit through
 * draw_triangles before reserving again. Never retained across finish_render. */
float *gfx_gx2_reserve_vertices(size_t float_capacity);
/* Switch only between completed render pairs, never with a pending batch. */
void gfx_gx2_set_packed_colors(bool enabled);
bool gfx_gx2_packed_colors(void);
void gfx_gx2_set_indexed(bool enabled);
bool gfx_gx2_indexed(void);
/* Indices are native-endian uint16_t. Both streams are retained in the
 * backend arena and published before drawing, until finish_render retires. */
void gfx_gx2_draw_indexed(float *vertices, size_t float_count,
                        const uint16_t *indices, size_t triangle_count);
void gfx_gx2_flush_vbo_cache(void);
void gfx_gx2_free(void);
struct WiiUTextureImage;
/* Main/render thread only. Borrowed CPU data is consumed before return. */
bool gfx_gx2_upload_resource(uint32_t texture_id, const struct WiiUTextureImage *image);
/* Flush frontend batches first. Waits for GPU before reclaiming storage;
 * preserves texture IDs so the frontend can reuse its bounded slot pool. */
void gfx_gx2_reset_texture_storage(void);
void gfx_wiiu_clear_texture_cache(void);
void gfx_wiiu_shutdown_texture_cache(void);
void gfx_gx2_set_gpu_available(bool available);
bool gfx_gx2_gpu_available(void);
bool gfx_gx2_prepare_options_frame(bool menu_open);
void gfx_gx2_capture_options_background(void);

#ifdef WIIU_LOAD_TIMING_PROFILE
/* Render-thread counters in native OS clock ticks. Converted only on report. */
enum WiiUGx2ProfileMetric {
    WIIU_GX2_VALIDATE, WIIU_GX2_STORAGE, WIIU_GX2_COPY,
    WIIU_GX2_PUBLISH, WIIU_GX2_DRAW, WIIU_GX2_BOUNDARY,
    WIIU_GX2_FINISH, WIIU_GX2_RECYCLE, WIIU_GX2_TV,
    WIIU_GX2_DRC, WIIU_GX2_SWAP, WIIU_GX2_PRESENT,
    WIIU_GX2_VIDEO_MODE, WIIU_GX2_PROFILE_COUNT
};
struct WiiUGx2FrameProfile {
    uint64_t ticks[WIIU_GX2_PROFILE_COUNT];
    uint64_t vbo_bytes;
    uint64_t ibo_bytes, submitted_vertices;
    uint32_t allocations, draw_calls, vsync_waits, direct_draws, copied_draws;
    uint32_t indexed_draws;
};
extern struct WiiUGx2FrameProfile gfx_gx2_frame_profile;
uint64_t gfx_gx2_profile_ticks_to_us(uint64_t ticks);
#endif

#ifdef __cplusplus
}
#endif

#endif
