#ifndef GFX_PC_H
#define GFX_PC_H

#define MAX_BUFFERED_TRIANGLES 256
#ifdef TARGET_WII_U
#define VERTEX_BUFFER_SIZE MAX_BUFFERED_TRIANGLES * 28 * 3 // 3 vertices in a triangle and 28 floats per vtx
#else
#define VERTEX_BUFFER_SIZE MAX_BUFFERED_TRIANGLES * 26 * 3 // 3 vertices in a triangle and 26 floats per vtx
#endif

struct GfxRenderingAPI;
struct GfxWindowManagerAPI;

struct GfxDimensions {
    uint32_t width, height;
    float aspect_ratio;
};

extern struct GfxDimensions gfx_current_dimensions;

#ifdef __cplusplus
extern "C" {
#endif

void gfx_init(struct GfxWindowManagerAPI *wapi, struct GfxRenderingAPI *rapi, const char *window_title);
struct GfxRenderingAPI *gfx_get_current_rendering_api(void);
uint16_t *get_framebuffer();
void gfx_start_frame(void);
void gfx_run(Gfx *commands);
void gfx_end_frame(void);
void gfx_precache_textures(void);
#ifdef TARGET_WII_U
void gfx_precache_startup_textures(void);
void gfx_precache_level_textures(s16 level_num);
#ifdef WIIU_LOAD_TIMING_PROFILE
void gfx_load_timing_begin_level(s16 level_num, const char *source);
void gfx_load_timing_begin_init(s16 level_num);
void gfx_load_timing_level_ready(s16 level_num);
void gfx_load_timing_save_confirmed(void);
#endif
#endif
void gfx_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
