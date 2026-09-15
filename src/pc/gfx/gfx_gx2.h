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
bool gfx_gx2_prepare_options_frame(bool menu_open);
void gfx_gx2_capture_options_background(void);

#ifdef __cplusplus
}
#endif

#endif
