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

#ifdef __cplusplus
}
#endif

#endif
