#ifdef WAPI_GX2

#include <macros.h>

#include <coreinit/foreground.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memfrmheap.h>
#include <coreinit/memheap.h>
#include <gx2/clear.h>
#include <gx2/context.h>
#include <gx2/display.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/state.h>
#include <gx2/swap.h>
#include <padscore/kpad.h>
#include <proc_ui/procui.h>
#include <sndcore2/core.h>

#include <whb/crash.h>
#include <whb/log.h>
#include <whb/log_cafe.h>
#include <whb/log_udp.h>

#ifdef WIIU_MINIMAP
#include <coreinit/cache.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern "C" {
#include <stb/stb_image.h>
#include "../fs/fs.h"
}
#include "seq_ids.h"
#include "../wiiu_gamepad_hud.h"
#include "../wiiu_minimap.h"
#include "../wiiu_gamepad_layout.h"
#include "wiiu_dds_rgba.h"
#include "wiiu_gamepad_font.h"
#include <coreinit/time.h>
#include "gfx_gx2_gamepad.h"
#endif

#include "gfx_window_manager_api.h"
#include "gfx_gx2.h"
#include "../configfile.h"
#include "../pc_main.h"

uint32_t g_window_width = 0;
uint32_t g_window_height = 0;
uint32_t g_render_offset_x = 0;
uint32_t g_render_offset_y = 0;
static uint32_t g_tv_width = 0;
static uint32_t g_tv_height = 0;
static uint32_t g_render_target_width = 0;
static uint32_t g_render_target_height = 0;

static bool is_running = false;
static bool g_procui_initialized;

struct WiiUVideoMode
{
    uint32_t resolution;
    uint32_t aspect;
    uint32_t surface_width;
    uint32_t surface_height;
    uint32_t content_width;
    uint32_t content_height;
    uint32_t offset_x;
    uint32_t offset_y;
};

static WiiUVideoMode g_requested_mode = {};
static WiiUVideoMode g_active_mode = {};

// Marks the beginning of the replaceable colour/depth allocations in MEM1.
static const uint32_t RENDER_BUFFERS_HEAP_TAG = 0x534D3634; // "SM64"

static void*            g_cmd_list                = nullptr;
static GX2ContextState* g_context                 = nullptr;
static void*            g_tv_scan_buffer          = nullptr;
static void*            g_drc_scan_buffer         = nullptr;
static GX2ColorBuffer   g_color_buffer;
static void*            g_color_buffer_image_data = nullptr;
static GX2DepthBuffer   g_depth_buffer;
static void*            g_depth_buffer_image_data = nullptr;
static GX2Surface       g_options_background_surface;
static void*            g_options_background_image_data = nullptr;
static bool             g_options_background_valid = false;
static bool             g_restore_options_background = false;
#ifdef WIIU_MINIMAP
static GX2ColorBuffer   g_drc_panel_color_buffer;
static void*            g_drc_panel_image_data = nullptr;
static uint8_t*         g_drc_panel_base = nullptr;
static size_t           g_drc_panel_base_size = 0;
static bool             g_drc_panel_base_valid = false;
static const char*      g_drc_panel_texture_path = nullptr;
static bool             g_drc_marker_valid = false;
static int              g_drc_marker_x = 0;
static int              g_drc_marker_y = 0;
static bool             g_drc_hud_valid = false;
static bool             g_drc_hud_visible = false;
static WiiUGamePadHudFrame g_drc_hud_frame = {};

static const uint32_t DRC_WIDTH = 854;
static const uint32_t DRC_HEIGHT = 480;
static const uint32_t DRC_MAP_SIZE = 480;
static uint8_t* g_drc_map_pixels = nullptr;
static uint8_t* g_drc_map_zoom_rows = nullptr;
static bool g_drc_panel_full_flush_pending = false;
static bool g_drc_map_zoomed = false;
static bool g_drc_gpu_zoom = false;
static uint32_t g_drc_map_generation = 0;
static uint16_t g_drc_dirty_left[480], g_drc_dirty_right[480];
static WiiUMapView g_drc_map_view = {0, 0, 1};
static bool g_drc_framed_layout = false;

#ifdef EXTERNAL_DATA
static bool g_drc_prefer_framed_layout = true;
static bool g_drc_theme_labels_valid = false;
static int g_drc_theme_counter = -1;
struct WiiUMinimapTextureCache {
    const char* path;
    uint8_t* pixels;
    int width;
    int height;
};

static WiiUMinimapTextureCache g_minimap_texture_cache[256] = {};
static size_t g_minimap_texture_cache_count = 0;
static bool g_minimap_textures_preloaded = false;
#endif
#endif
static MEMHeapHandle    g_mem1_heap_handle        = nullptr;
static MEMHeapHandle    g_fg_heap_handle          = nullptr;

static void gfx_gx2_window_foreground_release_callback(void)
{
    //WHBLogPrint("To release foreground");

    // Drain before relinquishing foreground-only heaps and GX2 access.
    if (gfx_gx2_gpu_available())
    {
        GX2DrawDone();
        gfx_gx2_set_gpu_available(false);
    }

    if (g_fg_heap_handle)
    {
        MEMFreeToFrmHeap(g_fg_heap_handle, MEM_FRM_HEAP_FREE_ALL);
        g_fg_heap_handle = nullptr;
    }
    g_tv_scan_buffer = nullptr;
    g_drc_scan_buffer = nullptr;

    if (g_mem1_heap_handle)
    {
        MEMFreeToFrmHeap(g_mem1_heap_handle, MEM_FRM_HEAP_FREE_ALL);
        g_mem1_heap_handle = nullptr;
    }
    g_color_buffer_image_data = nullptr;
    g_depth_buffer_image_data = nullptr;
    g_options_background_image_data = nullptr;
    g_options_background_valid = false;
    g_restore_options_background = false;
#ifdef WIIU_MINIMAP
    g_drc_panel_image_data = nullptr;
    g_drc_panel_base_valid = false;
    g_drc_marker_valid = false;
    g_drc_hud_valid = false;
#endif
    if (is_running)
        ProcUIDrawDoneRelease();
}

extern "C" void OSBlockThreadsOnExit(void);
extern "C" void _Exit(int);
extern "C" void KPADShutdown();

static void gfx_gx2_window_exit_callback(void)
{
    is_running = false;
    game_deinit();

    // Exit may follow foreground release. Discard pending CPU batches and
    // retire GPU resources while their owning APIs are still available.
    gfx_wiiu_shutdown_texture_cache();
    gfx_gx2_free();
#ifdef WIIU_MINIMAP
    gfx_gx2_gamepad_free();
#ifdef EXTERNAL_DATA
    for (size_t i = 0; i < g_minimap_texture_cache_count; ++i)
        stbi_image_free(g_minimap_texture_cache[i].pixels);
    g_minimap_texture_cache_count = 0;
#endif
    free(g_drc_panel_base);
    free(g_drc_map_pixels);
    free(g_drc_map_zoom_rows);
    g_drc_map_zoom_rows = nullptr;
    g_drc_map_pixels = nullptr;
    g_drc_panel_base = nullptr;
    g_drc_panel_base_size = 0;

#endif
    if (gfx_gx2_gpu_available())
    {
        GX2Shutdown();
        gfx_gx2_set_gpu_available(false);
    }

    KPADShutdown();
    AXQuit();
    gfx_gx2_window_foreground_release_callback();
    if (g_cmd_list)
    {
        MEMFreeToDefaultHeap(g_cmd_list);
        g_cmd_list = nullptr;
    }
    if (g_context)
    {
        MEMFreeToDefaultHeap(g_context);
        g_context = nullptr;
    }
    // No GX2 operation may follow ProcUI shutdown.
    if (g_procui_initialized)
    {
        ProcUIShutdown();
        g_procui_initialized = false;
    }
    WHBLogCafeDeinit();
    WHBLogUdpDeinit();
    OSBlockThreadsOnExit();
    _Exit(-1);
}

extern "C" GX2AspectRatio GX2GetSystemTVAspectRatio(void);

static WiiUVideoMode gfx_gx2_window_calculate_video_mode(uint32_t resolution,
                                                         uint32_t aspect)
{
    WiiUVideoMode mode = {};
    mode.resolution = resolution < WIIU_RESOLUTION_COUNT
        ? resolution : WIIU_RESOLUTION_AUTO;
    mode.aspect = aspect < WIIU_ASPECT_RATIO_COUNT
        ? aspect : WIIU_ASPECT_RATIO_16_9;

    uint32_t internal_height = g_tv_height;
    if (mode.resolution == WIIU_RESOLUTION_720P)
        internal_height = 720;
    else if (mode.resolution == WIIU_RESOLUTION_480P)
        internal_height = 480;

    // The scanout copy expands the whole render surface to the TV. Keep that
    // surface at the TV aspect and centre the selected game aspect within it.
    mode.surface_height = internal_height;
    if (g_tv_width * 3 == g_tv_height * 4)
        mode.surface_width = internal_height * 4 / 3;
    else
        mode.surface_width = internal_height == 480 ? 854 : internal_height * 16 / 9;

    mode.content_height = internal_height;
    mode.content_width = mode.aspect == WIIU_ASPECT_RATIO_4_3
        ? internal_height * 4 / 3
        : (internal_height == 480 ? 854 : internal_height * 16 / 9);

    // A 16:9 image on a physical 4:3 output needs letterboxing.
    if (mode.content_width > mode.surface_width)
    {
        mode.content_width = mode.surface_width;
        mode.content_height = mode.aspect == WIIU_ASPECT_RATIO_4_3
            ? mode.content_width * 3 / 4
            : mode.content_width * 9 / 16;
    }

    mode.offset_x = (mode.surface_width - mode.content_width) / 2;
    mode.offset_y = (mode.surface_height - mode.content_height) / 2;
    return mode;
}

static void gfx_gx2_window_refresh_requested_mode(void)
{
    g_requested_mode = gfx_gx2_window_calculate_video_mode(
        configWiiUInternalResolution, configWiiUAspectRatio);
    configWiiUInternalResolution = g_requested_mode.resolution;
    configWiiUAspectRatio = g_requested_mode.aspect;
}

static void gfx_gx2_window_activate_mode(const WiiUVideoMode &mode)
{
    g_active_mode = mode;
    g_render_target_width = mode.surface_width;
    g_render_target_height = mode.surface_height;
    g_window_width = mode.content_width;
    g_window_height = mode.content_height;
    g_render_offset_x = mode.offset_x;
    g_render_offset_y = mode.offset_y;
}

static bool gfx_gx2_window_mode_changed(void)
{
    return g_requested_mode.resolution != g_active_mode.resolution ||
           g_requested_mode.aspect != g_active_mode.aspect;
}

static bool gfx_gx2_window_mode_needs_new_buffers(void)
{
    return g_requested_mode.surface_width != g_active_mode.surface_width ||
           g_requested_mode.surface_height != g_active_mode.surface_height;
}

#ifdef WIIU_MINIMAP
static bool gfx_gx2_window_create_drc_panel_buffer(void)
{
    g_drc_panel_color_buffer = {};
    g_drc_panel_color_buffer.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    g_drc_panel_color_buffer.surface.width = DRC_WIDTH;
    g_drc_panel_color_buffer.surface.height = DRC_HEIGHT;
    g_drc_panel_color_buffer.surface.depth = 1;
    g_drc_panel_color_buffer.surface.mipLevels = 1;
    g_drc_panel_color_buffer.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    g_drc_panel_color_buffer.surface.aa = GX2_AA_MODE1X;
    g_drc_panel_color_buffer.surface.use = GX2_SURFACE_USE_TEXTURE_COLOR_BUFFER_TV;
    g_drc_panel_color_buffer.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
    g_drc_panel_color_buffer.viewMip = 0;
    g_drc_panel_color_buffer.viewFirstSlice = 0;
    g_drc_panel_color_buffer.viewNumSlices = 1;
    GX2CalcSurfaceSizeAndAlignment(&g_drc_panel_color_buffer.surface);

    g_drc_panel_image_data = MEMAllocFromFrmHeapEx(
        g_mem1_heap_handle,
        g_drc_panel_color_buffer.surface.imageSize,
        g_drc_panel_color_buffer.surface.alignment
    );
    if (!g_drc_panel_image_data)
    {
        WHBLogPrint("[gamepad-panel] disabled: color-buffer allocation failed");
        return false;
    }

    g_drc_panel_color_buffer.surface.image = g_drc_panel_image_data;
    GX2InitColorBufferRegs(&g_drc_panel_color_buffer);

    if (g_drc_panel_base_size < g_drc_panel_color_buffer.surface.imageSize)
    {
        uint8_t* new_base = (uint8_t*)realloc(
            g_drc_panel_base, g_drc_panel_color_buffer.surface.imageSize);
        if (!new_base)
        {
            WHBLogPrint("[gamepad-panel] disabled: backing-buffer allocation failed");
            g_drc_panel_image_data = nullptr;
            return false;
        }
        g_drc_panel_base = new_base;
        g_drc_panel_base_size = g_drc_panel_color_buffer.surface.imageSize;
    }

    g_drc_panel_base_valid = false;
    g_drc_panel_full_flush_pending = false;
    g_drc_marker_valid = false;
    g_drc_hud_valid = false;
    g_drc_panel_texture_path = nullptr;
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU, g_drc_panel_image_data,
                  g_drc_panel_color_buffer.surface.imageSize);
    WHBLogPrintf("[gamepad-panel] color buffer: %ux%u pitch=%u size=%u",
                 DRC_WIDTH, DRC_HEIGHT,
                 g_drc_panel_color_buffer.surface.pitch,
                 g_drc_panel_color_buffer.surface.imageSize);
    gfx_gx2_gamepad_ready();
    return true;
}

#endif

static bool gfx_gx2_window_create_render_buffers(void)
{
    g_color_buffer = {};
    g_color_buffer.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    g_color_buffer.surface.width = g_render_target_width;
    g_color_buffer.surface.height = g_render_target_height;
    g_color_buffer.surface.depth = 1;
    g_color_buffer.surface.mipLevels = 1;
    g_color_buffer.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    g_color_buffer.surface.aa = GX2_AA_MODE1X;
    g_color_buffer.surface.use = GX2_SURFACE_USE_TEXTURE_COLOR_BUFFER_TV;
    const bool needs_tv_scaling = g_render_target_width != g_tv_width ||
                                  g_render_target_height != g_tv_height;
    g_color_buffer.surface.tileMode = needs_tv_scaling
        ? GX2_TILE_MODE_LINEAR_ALIGNED : GX2_TILE_MODE_DEFAULT;
    g_color_buffer.viewMip = 0;
    g_color_buffer.viewFirstSlice = 0;
    g_color_buffer.viewNumSlices = 1;
    GX2CalcSurfaceSizeAndAlignment(&g_color_buffer.surface);

    g_color_buffer_image_data = MEMAllocFromFrmHeapEx(
        g_mem1_heap_handle, g_color_buffer.surface.imageSize,
        g_color_buffer.surface.alignment);
    if (!g_color_buffer_image_data)
        return false;
    g_color_buffer.surface.image = g_color_buffer_image_data;
    GX2InitColorBufferRegs(&g_color_buffer);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU, g_color_buffer_image_data,
                  g_color_buffer.surface.imageSize);

    g_depth_buffer = {};
    g_depth_buffer.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    g_depth_buffer.surface.width = g_render_target_width;
    g_depth_buffer.surface.height = g_render_target_height;
    g_depth_buffer.surface.depth = 1;
    g_depth_buffer.surface.mipLevels = 1;
    g_depth_buffer.surface.format = GX2_SURFACE_FORMAT_FLOAT_D24_S8;
    g_depth_buffer.surface.aa = GX2_AA_MODE1X;
    g_depth_buffer.surface.use = GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_DEPTH_BUFFER;
    g_depth_buffer.surface.tileMode = GX2_TILE_MODE_DEFAULT;
    g_depth_buffer.viewMip = 0;
    g_depth_buffer.viewFirstSlice = 0;
    g_depth_buffer.viewNumSlices = 1;
    g_depth_buffer.depthClear = 1.0f;
    g_depth_buffer.stencilClear = 0;
    GX2CalcSurfaceSizeAndAlignment(&g_depth_buffer.surface);

    g_depth_buffer_image_data = MEMAllocFromFrmHeapEx(
        g_mem1_heap_handle, g_depth_buffer.surface.imageSize,
        g_depth_buffer.surface.alignment);
    if (!g_depth_buffer_image_data)
        return false;
    g_depth_buffer.surface.image = g_depth_buffer_image_data;
    GX2InitDepthBufferRegs(&g_depth_buffer);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU, g_depth_buffer_image_data,
                  g_depth_buffer.surface.imageSize);

    // Preserve one clean copy of the paused world. Later menu frames restore
    // it and execute only the options interface display-list suffix.
    g_options_background_surface = g_color_buffer.surface;
    g_options_background_image_data = MEMAllocFromFrmHeapEx(
        g_mem1_heap_handle, g_options_background_surface.imageSize,
        g_options_background_surface.alignment);
    if (g_options_background_image_data)
    {
        g_options_background_surface.image = g_options_background_image_data;
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU,
                      g_options_background_image_data,
                      g_options_background_surface.imageSize);
    }
    else
    {
        WHBLogPrint("[options-cache] disabled: background allocation failed");
    }
    g_options_background_valid = false;
    g_restore_options_background = false;
#ifdef WIIU_MINIMAP
    gfx_gx2_window_create_drc_panel_buffer();
#endif
    return true;
}

extern "C" bool gfx_gx2_prepare_options_frame(bool menu_open)
{
    if (!menu_open)
    {
        g_options_background_valid = false;
        g_restore_options_background = false;
        return false;
    }

    g_restore_options_background = g_options_background_valid &&
                                   g_options_background_image_data != nullptr;
    return g_restore_options_background;
}

extern "C" void gfx_gx2_capture_options_background(void)
{
    if (!g_options_background_image_data || g_options_background_valid)
        return;

    // Finish the clean 3D prefix once and preserve it before the translucent
    // menu is drawn into the main colour buffer.
    gfx_gx2_flush_vbo_cache();
    GX2Flush();
    GX2DrawDone();
    GX2CopySurface(&g_color_buffer.surface, 0, 0,
                   &g_options_background_surface, 0, 0);
    GX2DrawDone();
    GX2SetContextState(g_context);
    GX2SetColorBuffer(&g_color_buffer, GX2_RENDER_TARGET_0);
    GX2SetDepthBuffer(&g_depth_buffer);
    g_options_background_valid = true;
    WHBLogPrintf("[options-cache] captured %ux%u",
                 g_render_target_width, g_render_target_height);
}

#ifdef WIIU_MINIMAP
static inline uint8_t* gfx_gx2_drc_pixel(uint8_t* buffer, int x, int y)
{
    return buffer + ((size_t)y * g_drc_panel_color_buffer.surface.pitch + x) * 4;
}

static const WiiUMapRect& gfx_gx2_drc_map_rect(void)
{
    return g_drc_framed_layout ? WIIU_MAP_FRAMED_RECT : WIIU_MAP_CLASSIC_RECT;
}

static bool gfx_gx2_drc_contains(const WiiUMapRect& rect, int x, int y)
{
    return x >= rect.x && y >= rect.y &&
           x < rect.x + rect.width && y < rect.y + rect.height;
}

static void gfx_gx2_drc_put_pixel(int x, int y,
                                  uint8_t red, uint8_t green,
                                  uint8_t blue, uint8_t alpha)
{
    if (!g_drc_panel_image_data || x < 0 || y < 0 ||
        x >= (int)DRC_WIDTH || y >= (int)DRC_HEIGHT)
        return;
    uint8_t* pixel = gfx_gx2_drc_pixel((uint8_t*)g_drc_panel_image_data, x, y);
    pixel[0] = red;
    pixel[1] = green;
    pixel[2] = blue;
    pixel[3] = alpha;
}

static void gfx_gx2_drc_flush_rect(int left, int top, int right, int bottom)
{
    if (!g_drc_panel_image_data || g_drc_panel_full_flush_pending)
        return;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right >= (int)DRC_WIDTH) right = DRC_WIDTH - 1;
    if (bottom >= (int)DRC_HEIGHT) bottom = DRC_HEIGHT - 1;
    if (left > right || top > bottom)
        return;

    if (g_drc_gpu_zoom) {
        for (int y = top; y <= bottom; ++y) {
            if (left < g_drc_dirty_left[y]) g_drc_dirty_left[y] = left;
            if (right > g_drc_dirty_right[y]) g_drc_dirty_right[y] = right;
        }
        return;
    }

    for (int y = top; y <= bottom; ++y)
    {
        uint8_t* row = gfx_gx2_drc_pixel((uint8_t*)g_drc_panel_image_data,
                                         left, y);
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU, row,
                      (right - left + 1) * 4);
    }
}

#ifdef EXTERNAL_DATA
static WiiUMinimapTextureCache* gfx_gx2_find_minimap_texture(const char* path)
{
    for (size_t i = 0; i < g_minimap_texture_cache_count; ++i)
    {
        if (!strcmp(g_minimap_texture_cache[i].path, path))
            return &g_minimap_texture_cache[i];
    }
    return nullptr;
}

static WiiUMinimapTextureCache* gfx_gx2_load_minimap_texture(const char* path)
{
    WiiUMinimapTextureCache* cached = gfx_gx2_find_minimap_texture(path);
    if (cached)
        return cached->pixels ? cached : nullptr;
    if (g_minimap_texture_cache_count >=
        sizeof(g_minimap_texture_cache) / sizeof(g_minimap_texture_cache[0]))
        return nullptr;

    /* Keep failed lookups too. Optional HD glyphs/backgrounds must not cause
       repeated SD reads on every frame. Paths must have static storage. */
    WiiUMinimapTextureCache* entry =
        &g_minimap_texture_cache[g_minimap_texture_cache_count++];
    entry->path = path;
    int width = 0, height = 0;
    uint8_t* pixels = nullptr;
    uint64_t encoded_size = 0;
    uint8_t* encoded = nullptr;
    char dds_path[SYS_MAX_PATH];
    size_t length = strlen(path);
    if (length > 4 && length < sizeof(dds_path)) {
        memcpy(dds_path, path, length + 1);
        memcpy(dds_path + length - 4, ".dds", 5);
        encoded = (uint8_t*)fs_load_file_variant(path, dds_path, &encoded_size, 64ULL * 1024 * 1024 + 128);
        if (encoded) pixels = wiiu_dds_decode_rgba(encoded, encoded_size, &width, &height);
        free(encoded);
    }
    encoded = pixels ? nullptr : (uint8_t*)fs_load_file(path, &encoded_size);
    if (encoded) {
        pixels = stbi_load_from_memory(encoded, encoded_size, &width, &height, nullptr, 4);
        free(encoded);
    }
    if (!pixels)
    {
        WHBLogPrintf("[gamepad-panel] missing/invalid texture: %s", path);
        return nullptr;
    }
    entry->pixels = pixels;
    entry->width = width;
    entry->height = height;
    WHBLogPrintf("[gamepad-panel] cached minimap: %s %dx%d bytes=%llu",
                 path, width, height, (unsigned long long)encoded_size);
    return entry;
}

static void gfx_gx2_preload_minimap_textures(void)
{
    if (g_minimap_textures_preloaded)
        return;
    const char* const* paths = nullptr;
    size_t count = wiiu_minimap_get_preload_paths(&paths);
    for (size_t i = 0; i < count; ++i)
        gfx_gx2_load_minimap_texture(paths[i]);
    g_minimap_textures_preloaded = true;
}

static const char* const g_drc_theme_background = "gfx/textures/minimap/theme/fondo.png";
static const char* const g_drc_theme_frame = "gfx/textures/minimap/theme/marco-mapa.png";
static void gfx_gx2_compose_drc_theme_base(void);
static void gfx_gx2_draw_drc_theme_map(const WiiUMapView& view);
static void gfx_gx2_draw_drc_theme_trim(const WiiUMapRect& clip);
#endif

static void gfx_gx2_compose_drc_panel(const struct WiiUMinimapFrame* frame)
{
    if (!g_drc_panel_image_data || !g_drc_panel_base)
        return;

    uint32_t background = frame ? frame->background_color : 0x0C0E18;
    uint8_t bg_r = (background >> 16) & 0xFF;
    uint8_t bg_g = (background >> 8) & 0xFF;
    uint8_t bg_b = background & 0xFF;
    uint32_t pitch = g_drc_panel_color_buffer.surface.pitch;

    for (uint32_t y = 0; y < DRC_HEIGHT; ++y)
    {
        for (uint32_t x = 0; x < pitch; ++x)
        {
            uint8_t* pixel = gfx_gx2_drc_pixel(g_drc_panel_base, x, y);
            const bool side_panel = x >= DRC_MAP_SIZE && x < DRC_WIDTH;
            pixel[0] = side_panel ? 18 : bg_r;
            pixel[1] = side_panel ? 20 : bg_g;
            pixel[2] = side_panel ? 32 : bg_b;
            pixel[3] = 255;
        }
    }

#ifdef EXTERNAL_DATA
    WiiUMinimapTextureCache* panel_texture = g_drc_framed_layout ? nullptr : gfx_gx2_load_minimap_texture(
        "gfx/textures/inside/inside_castle_textures.0B000.rgba16.png");
    if (panel_texture) {
        /* Tile at a fixed logical size even when an HD replacement is loaded.
           Tint the stone navy; it is a background, not another HUD element. */
        for (uint32_t y = 0; y < DRC_HEIGHT; ++y) {
            for (uint32_t x = DRC_MAP_SIZE + 3; x < DRC_WIDTH; ++x) {
                int sx = ((x - DRC_MAP_SIZE) % 96) * panel_texture->width / 96;
                int sy = (y % 96) * panel_texture->height / 96;
                const uint8_t* source = panel_texture->pixels +
                    ((size_t)sy * panel_texture->width + sx) * 4;
                int shade = (source[0] * 3 + source[1] * 6 + source[2]) / 10;
                uint8_t* dest = gfx_gx2_drc_pixel(g_drc_panel_base, x, y);
                dest[0] = 10 + shade / 8;
                dest[1] = 14 + shade / 7;
                dest[2] = 24 + shade / 6;
            }
        }
    }
    if (frame && frame->texture_path)
    {
        WiiUMinimapTextureCache* texture =
            gfx_gx2_load_minimap_texture(frame->texture_path);
        if (texture)
        {
            for (uint32_t y = 0; y < DRC_MAP_SIZE; ++y)
            {
                int source_y = y * texture->height / DRC_MAP_SIZE;
                for (uint32_t x = 0; x < DRC_MAP_SIZE; ++x)
                {
                    int source_x = x * texture->width / DRC_MAP_SIZE;
                    const uint8_t* source = texture->pixels +
                        ((size_t)source_y * texture->width + source_x) * 4;
                    uint8_t* dest = gfx_gx2_drc_pixel(g_drc_panel_base, x, y);
                    uint32_t alpha = source[3];
                    dest[0] = (source[0] * alpha + dest[0] * (255 - alpha)) / 255;
                    dest[1] = (source[1] * alpha + dest[1] * (255 - alpha)) / 255;
                    dest[2] = (source[2] * alpha + dest[2] * (255 - alpha)) / 255;
                }
            }
        }
    }
#endif

    /* Retain the flattened map and horizontally doubled rows. Reuse each
       doubled row twice while panning: no per-pixel scaling in presentation,
       and half the memory of a fully expanded 960x960 image. */
    if (!g_drc_map_pixels)
        g_drc_map_pixels = (uint8_t*)malloc(DRC_MAP_SIZE * DRC_MAP_SIZE * 4);
    if (g_drc_map_pixels) {
        for (uint32_t y = 0; y < DRC_MAP_SIZE; ++y)
            memcpy(g_drc_map_pixels + y * DRC_MAP_SIZE * 4,
                   gfx_gx2_drc_pixel(g_drc_panel_base, 0, y), DRC_MAP_SIZE * 4);
        if (!g_drc_framed_layout && !g_drc_map_zoom_rows) {
            g_drc_map_zoom_rows = (uint8_t*)malloc(DRC_MAP_SIZE * 2 * DRC_MAP_SIZE * 4);
            WHBLogPrintf("[gamepad-panel] zoom rows cache=%u bytes",
                         g_drc_map_zoom_rows ? DRC_MAP_SIZE * 2 * DRC_MAP_SIZE * 4 : 0);
        }
        if (!g_drc_framed_layout && g_drc_map_zoom_rows) {
            for (uint32_t y = 0; y < DRC_MAP_SIZE; ++y) {
                const uint8_t* source = g_drc_map_pixels + y * DRC_MAP_SIZE * 4;
                uint8_t* dest = g_drc_map_zoom_rows + y * DRC_MAP_SIZE * 8;
                for (uint32_t x = 0; x < DRC_MAP_SIZE; ++x) {
                    memcpy(dest + x * 8, source + x * 4, 4);
                    memcpy(dest + x * 8 + 4, source + x * 4, 4);
                }
            }
        }
    }
    ++g_drc_map_generation;
    if (!g_drc_framed_layout && g_drc_gpu_zoom && g_drc_map_pixels) {
        /* The GPU draws the map behind this premultiplied UI layer. Panning
         * never modifies or recopies these transparent map pixels. */
        for (uint32_t y = 0; y < DRC_MAP_SIZE; ++y)
            memset(gfx_gx2_drc_pixel(g_drc_panel_base, 0, y), 0, DRC_MAP_SIZE * 4);
    }
    g_drc_map_view = {0, 0, 1};

    /* A subtle divider reserves the right-hand 374 pixels for the HUD panel. */
    for (uint32_t y = 0; !g_drc_framed_layout && y < DRC_HEIGHT; ++y)
    {
        for (uint32_t x = DRC_MAP_SIZE; x < DRC_MAP_SIZE + 3; ++x)
        {
            uint8_t* pixel = gfx_gx2_drc_pixel(g_drc_panel_base, x, y);
            pixel[0] = 68;
            pixel[1] = 73;
            pixel[2] = 105;
            pixel[3] = 255;
        }
    }

#ifdef EXTERNAL_DATA
    if (g_drc_framed_layout)
        gfx_gx2_compose_drc_theme_base();
#endif
    memcpy(g_drc_panel_image_data, g_drc_panel_base,
           g_drc_panel_color_buffer.surface.imageSize);
    /* Flush once after the HUD, title and marker have also been composed. */
    g_drc_panel_full_flush_pending = true;
    g_drc_panel_base_valid = true;
    /* The new base overwrote both dynamic overlays even when their values did
       not change, so force them to be drawn again. */
    g_drc_marker_valid = false;
    g_drc_hud_valid = false;
    g_drc_panel_texture_path = frame ? frame->texture_path : nullptr;
}

static WiiUMapView gfx_gx2_update_drc_map_view(const WiiUMinimapFrame& frame)
{
    const bool cpu_zoom_available = g_drc_framed_layout ? g_drc_map_pixels != nullptr
                                                        : g_drc_map_zoom_rows != nullptr;
    WiiUMapView view = wiiu_map_view(g_drc_map_zoomed && (g_drc_gpu_zoom || cpu_zoom_available),
                                     frame.mario_x, frame.mario_y);
    if (view.x == g_drc_map_view.x && view.y == g_drc_map_view.y &&
        view.scale == g_drc_map_view.scale)
        return view;

    if (g_drc_gpu_zoom) {
        /* Only UV coordinates change. Retain the old marker rectangle so
         * the compositor can erase it even when the marker leaves an edge. */
        g_drc_map_view = view;
        return view;
    }

#ifdef EXTERNAL_DATA
    if (g_drc_framed_layout) {
        gfx_gx2_draw_drc_theme_map(view);
        const WiiUMapRect& rect = WIIU_MAP_FRAMED_RECT;
        gfx_gx2_draw_drc_theme_trim(rect);
        for (int y = rect.y; y < rect.y + rect.height; ++y)
            memcpy(gfx_gx2_drc_pixel((uint8_t*)g_drc_panel_image_data, rect.x, y),
                   gfx_gx2_drc_pixel(g_drc_panel_base, rect.x, y), rect.width * 4);
    } else
#endif
    for (uint32_t y = 0; y < DRC_MAP_SIZE; ++y) {
        uint8_t* dest = gfx_gx2_drc_pixel(g_drc_panel_base, 0, y);
        const uint8_t* source = view.scale == 1
            ? g_drc_map_pixels + y * DRC_MAP_SIZE * 4
            : g_drc_map_zoom_rows + ((view.y + y / 2) * DRC_MAP_SIZE * 2 + view.x * 2) * 4;
        memcpy(dest, source, DRC_MAP_SIZE * 4);
        memcpy(gfx_gx2_drc_pixel((uint8_t*)g_drc_panel_image_data, 0, y),
               dest, DRC_MAP_SIZE * 4);
    }
    g_drc_map_view = view;
    g_drc_marker_valid = false;
    /* Row-by-row GX2Invalidate calls here and in the overlays made the old
       zoom expensive. Suppress them and issue one contiguous flush at the end. */
    g_drc_panel_full_flush_pending = true;
    return view;
}

#ifdef EXTERNAL_DATA
static const char* const g_hud_digit_paths[] = {
    "gfx/textures/segment2/segment2.00000.rgba16.png",
    "gfx/textures/segment2/segment2.00200.rgba16.png",
    "gfx/textures/segment2/segment2.00400.rgba16.png",
    "gfx/textures/segment2/segment2.00600.rgba16.png",
    "gfx/textures/segment2/segment2.00800.rgba16.png",
    "gfx/textures/segment2/segment2.00A00.rgba16.png",
    "gfx/textures/segment2/segment2.00C00.rgba16.png",
    "gfx/textures/segment2/segment2.00E00.rgba16.png",
    "gfx/textures/segment2/segment2.01000.rgba16.png",
    "gfx/textures/segment2/segment2.01200.rgba16.png",
};

static const char* const g_hud_multiply_path =
    "gfx/textures/segment2/segment2.05600.rgba16.png";
static const char* const g_hud_coin_path =
    "gfx/textures/segment2/segment2.05800.rgba16.png";
static const char* const g_hud_mario_path =
    "gfx/textures/segment2/segment2.05A00.rgba16.png";
static const char* const g_hud_star_path =
    "gfx/textures/segment2/segment2.05C00.rgba16.png";
static const char* const g_minimap_arrow_path =
    "gfx/textures/segment2/segment2.081D0.rgba16.png";
static const char* const g_hud_apostrophe_path =
    "gfx/textures/segment2/segment2.04800.rgba16.png";
static const char* const g_hud_quote_path =
    "gfx/textures/segment2/segment2.04A00.rgba16.png";
static const char* const g_power_left_path =
    "gfx/actors/power_meter/power_meter_left_side.rgba16.png";
static const char* const g_power_right_path =
    "gfx/actors/power_meter/power_meter_right_side.rgba16.png";
static const char* const g_power_segment_paths[] = {
    "gfx/actors/power_meter/power_meter_one_segment.rgba16.png",
    "gfx/actors/power_meter/power_meter_two_segments.rgba16.png",
    "gfx/actors/power_meter/power_meter_three_segments.rgba16.png",
    "gfx/actors/power_meter/power_meter_four_segments.rgba16.png",
    "gfx/actors/power_meter/power_meter_five_segments.rgba16.png",
    "gfx/actors/power_meter/power_meter_six_segments.rgba16.png",
    "gfx/actors/power_meter/power_meter_seven_segments.rgba16.png",
    "gfx/actors/power_meter/power_meter_full.rgba16.png",
};

static const char* const g_red_coin_paths[] = {
    "gfx/actors/coin/coin_front.ia16.png",
    "gfx/actors/coin/coin_tilt_right.ia16.png",
    "gfx/actors/coin/coin_side.ia16.png",
    "gfx/actors/coin/coin_tilt_left.ia16.png",
};

struct WiiUMusicTitle {
    uint8_t sequence;
    const char* text;
};

/* Sequence names from the 3DS minimap, drawn with the active game font. */
#define MUSIC_TITLE(id, text) { id, text }
static const WiiUMusicTitle g_music_titles[] = {
    MUSIC_TITLE(SEQ_SOUND_PLAYER, "Silence"),
    MUSIC_TITLE(SEQ_EVENT_CUTSCENE_COLLECT_STAR, "Course Clear"),
    MUSIC_TITLE(SEQ_MENU_TITLE_SCREEN, "Title Theme"),
    MUSIC_TITLE(SEQ_LEVEL_GRASS, "Super Mario 64 Main Theme"),
    MUSIC_TITLE(SEQ_LEVEL_INSIDE_CASTLE, "Inside the Castle Walls"),
    MUSIC_TITLE(SEQ_LEVEL_WATER, "Dire, Dire Docks"),
    MUSIC_TITLE(SEQ_LEVEL_HOT, "Lethal Lava Land"),
    MUSIC_TITLE(SEQ_LEVEL_BOSS_KOOPA, "Koopa's Theme"),
    MUSIC_TITLE(SEQ_LEVEL_SNOW, "Snow Mountain"),
    MUSIC_TITLE(SEQ_LEVEL_SLIDE, "Slider"),
    MUSIC_TITLE(SEQ_LEVEL_SPOOKY, "Haunted House"),
    MUSIC_TITLE(SEQ_EVENT_PIRANHA_PLANT, "Piranha Plant's Lullaby"),
    MUSIC_TITLE(SEQ_LEVEL_UNDERGROUND, "Cave Dungeon"),
    MUSIC_TITLE(SEQ_MENU_STAR_SELECT, "Star Select"),
    MUSIC_TITLE(SEQ_EVENT_POWERUP, "Powerful Mario"),
    MUSIC_TITLE(SEQ_EVENT_METAL_CAP, "Metallic Mario"),
    MUSIC_TITLE(SEQ_EVENT_KOOPA_MESSAGE, "Koopa's Message"),
    MUSIC_TITLE(SEQ_LEVEL_KOOPA_ROAD, "Koopa's Road"),
    MUSIC_TITLE(SEQ_EVENT_HIGH_SCORE, "High Score"),
    MUSIC_TITLE(SEQ_EVENT_MERRY_GO_ROUND, "Merry-Go-Round"),
    MUSIC_TITLE(SEQ_EVENT_RACE, "Race Fanfare"),
    MUSIC_TITLE(SEQ_EVENT_CUTSCENE_STAR_SPAWN, "Power Star"),
    MUSIC_TITLE(SEQ_EVENT_BOSS, "Stage Boss"),
    MUSIC_TITLE(SEQ_EVENT_CUTSCENE_COLLECT_KEY, "Koopa Clear"),
    MUSIC_TITLE(SEQ_EVENT_ENDLESS_STAIRS, "Looping Steps"),
    MUSIC_TITLE(SEQ_LEVEL_BOSS_KOOPA_FINAL, "Ultimate Koopa"),
    MUSIC_TITLE(SEQ_EVENT_CUTSCENE_CREDITS, "Staff Roll"),
    MUSIC_TITLE(SEQ_EVENT_SOLVE_PUZZLE, "Correct Solution"),
    MUSIC_TITLE(SEQ_EVENT_TOAD_MESSAGE, "Toad's Message"),
    MUSIC_TITLE(SEQ_EVENT_PEACH_MESSAGE, "Peach's Message"),
    MUSIC_TITLE(SEQ_EVENT_CUTSCENE_INTRO, "Opening"),
    MUSIC_TITLE(SEQ_EVENT_CUTSCENE_VICTORY, "Course Clear"),
    MUSIC_TITLE(SEQ_EVENT_CUTSCENE_ENDING, "Ending Demo"),
    MUSIC_TITLE(SEQ_MENU_FILE_SELECT, "File Select"),
    MUSIC_TITLE(SEQ_EVENT_CUTSCENE_LAKITU, "Lakitu"),
};
#undef MUSIC_TITLE

struct WiiUDialogGlyph {
    char path[80];
    WiiUMinimapTextureCache* texture;
    WiiUGamePadGlyphBounds bounds;
    bool loaded;
};
static WiiUDialogGlyph g_dialog_glyphs[128] = {};
static uint8_t g_music_title_pixels[WIIU_MAP_SIZE * 56 * 4];
static WiiUMinimapTextureCache g_music_title_texture = {
    nullptr, g_music_title_pixels, WIIU_MAP_SIZE, 56
};
static int g_music_title_sequence = -2;

static WiiUDialogGlyph* gfx_gx2_music_note_glyph()
{
    /* Same pixel silhouette as render_note() in the 3DS title generator.
       Store it transversely so it shares the dialog glyph drawing path. */
    static const char* rows[] = {
        "................", "................", "................", "................",
        "................", "................", ".........####...", ".....####...#...",
        ".....#......#...", ".....#......#...", ".....#...####...", "..####..#####...",
        ".#####..####....", ".####...........", "................", "................",
    };
    static uint8_t pixels[16 * 16 * 4] = {};
    static WiiUMinimapTextureCache texture = {nullptr, pixels, 16, 16};
    static WiiUDialogGlyph glyph = {};
    if (!glyph.loaded) {
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                if (rows[y][x] == '#')
                    memset(pixels + ((15 - x) * 16 + (15 - y)) * 4, 255, 4);
        glyph.texture = &texture;
        glyph.bounds = wiiu_gamepad_glyph_bounds(pixels, 16, 16);
        glyph.loaded = true;
    }
    return &glyph;
}

static WiiUDialogGlyph* gfx_gx2_dialog_glyph(unsigned char ch)
{
    if (ch >= 128 || ch == ' ') return nullptr;
    WiiUDialogGlyph* glyph = &g_dialog_glyphs[ch];
    if (!glyph->loaded) {
        glyph->loaded = true;
        int offset = 0;
        if (ch >= '0' && ch <= '9') offset = 0x5900 + (ch - '0') * 0x40;
        else if (ch >= 'A' && ch <= 'Z') offset = 0x5B80 + (ch - 'A') * 0x40;
        else if (ch >= 'a' && ch <= 'z') offset = 0x6200 + (ch - 'a') * 0x40;
        else if (ch == '\'') offset = 0x6B80;
        else if (ch == ',') offset = 0x6B40;
        else if (ch == '-') offset = 0x6A40;
        else if (ch == '.') offset = 0x6A80;
        if (!offset) return nullptr;
        snprintf(glyph->path, sizeof(glyph->path),
                 "gfx/textures/segment2/font_graphics.%05X.ia4.png", offset);
        glyph->texture = gfx_gx2_load_minimap_texture(glyph->path);
        if (glyph->texture) glyph->bounds = wiiu_gamepad_glyph_bounds(
            glyph->texture->pixels, glyph->texture->width, glyph->texture->height);
    }
    return glyph->texture && glyph->bounds.width > 0 ? glyph : nullptr;
}

static bool gfx_gx2_cache_music_text(const char* text)
{
    WiiUDialogGlyph* note = gfx_gx2_music_note_glyph();
    const float note_advance = note->bounds.width + 3;
    float width = note_advance;
    for (const char* ch = text; *ch; ++ch) {
        if (*ch == ' ') { width += 5; continue; }
        WiiUDialogGlyph* glyph = gfx_gx2_dialog_glyph((unsigned char)*ch);
        if (!glyph) return false;
        width += glyph->bounds.width + 1;
    }
    width -= 1; // no trailing glyph spacing
    if (width <= 0) return false;
    float scale = wiiu_map_title_scale(width, 16);
    int pixel_width = (int)ceilf(width * scale);
    int height = (int)(16 * scale);
    int start = wiiu_map_title_x(pixel_width);
    /* Cache shadow then ink. Its two-pixel offset stays inside the map margin. */
    for (int pass = 0; pass < 2; ++pass) {
        float pen = note_advance;
        int offset = pass == 0 ? 2 : 0;
        wiiu_gamepad_draw_glyph(g_music_title_pixels, WIIU_MAP_SIZE, 56,
            note->texture->pixels, 16, 16, note->bounds, start + offset, 4 + offset,
            (int)ceilf(note->bounds.width * scale), height, pass == 0);
        for (const char* ch = text; *ch; ++ch) {
            if (*ch == ' ') { pen += 5; continue; }
            WiiUDialogGlyph* glyph = gfx_gx2_dialog_glyph((unsigned char)*ch);
            int x = (int)floorf(pen * scale);
            int right = (int)ceilf((pen + glyph->bounds.width) * scale);
            wiiu_gamepad_draw_glyph(g_music_title_pixels, WIIU_MAP_SIZE, 56,
                glyph->texture->pixels, glyph->texture->width, glyph->texture->height,
                glyph->bounds, start + x + offset, 4 + offset, right - x, height, pass == 0);
            pen += glyph->bounds.width + 1;
        }
    }
    return true;
}

static void gfx_gx2_cache_music_title(int sequence)
{
    if (g_music_title_sequence == sequence) return;
    g_music_title_sequence = sequence;
    memset(g_music_title_pixels, 0, sizeof(g_music_title_pixels));
    const WiiUMusicTitle* title = nullptr;
    for (const WiiUMusicTitle& candidate : g_music_titles)
        if (candidate.sequence == sequence) { title = &candidate; break; }
    if (title && !gfx_gx2_cache_music_text(title->text))
        memset(g_music_title_pixels, 0, sizeof(g_music_title_pixels));
}

static void gfx_gx2_drc_blend(uint8_t* dest, uint8_t red, uint8_t green, uint8_t blue, unsigned alpha)
{
    dest[0] = (red * alpha + dest[0] * (255 - alpha)) / 255;
    dest[1] = (green * alpha + dest[1] * (255 - alpha)) / 255;
    dest[2] = (blue * alpha + dest[2] * (255 - alpha)) / 255;
    dest[3] = alpha + dest[3] * (255 - alpha) / 255;
}

static void gfx_gx2_blit_drc_image(uint8_t* target, const WiiUMinimapTextureCache* texture,
                                   int x, int y, int width, int height, const WiiUMapRect& clip,
                                   unsigned opacity = 255, bool red_coin = false)
{
    if (!texture || width <= 0 || height <= 0)
        return;

    for (int dest_y = 0; dest_y < height; ++dest_y)
    {
        int panel_y = y + dest_y;
        if (panel_y < clip.y || panel_y >= clip.y + clip.height)
            continue;
        int source_y = dest_y * texture->height / height;
        for (int dest_x = 0; dest_x < width; ++dest_x)
        {
            int panel_x = x + dest_x;
            if (panel_x < clip.x || panel_x >= clip.x + clip.width)
                continue;
            int source_x = dest_x * texture->width / width;
            const uint8_t* source = texture->pixels +
                ((size_t)source_y * texture->width + source_x) * 4;
            if (source[3] == 0)
                continue;
            gfx_gx2_drc_blend(gfx_gx2_drc_pixel(target, panel_x, panel_y),
                              source[0], red_coin ? 0 : source[1], red_coin ? 0 : source[2],
                              source[3] * opacity / 255);
        }
    }
}

static const WiiUMapRect g_drc_screen_rect = { 0, 0, DRC_WIDTH, DRC_HEIGHT };

static void gfx_gx2_draw_drc_image(const WiiUMinimapTextureCache* texture, int x, int y,
                                    int width, int height, const WiiUMapRect& clip = g_drc_screen_rect)
{
    gfx_gx2_blit_drc_image((uint8_t*)g_drc_panel_image_data, texture, x, y, width, height, clip);
}

static void gfx_gx2_draw_drc_theme_trim(const WiiUMapRect& clip)
{
    const WiiUMapRect& button = WIIU_MAP_ZOOM_BUTTON;
    gfx_gx2_blit_drc_image(g_drc_panel_base, gfx_gx2_load_minimap_texture(g_drc_theme_frame),
                           20, 43, 458, 404, clip);
    gfx_gx2_blit_drc_image(g_drc_panel_base,
                           gfx_gx2_load_minimap_texture("gfx/textures/minimap/theme/lupa.png"),
                           button.x, button.y, button.width, button.height, clip);
}

static void gfx_gx2_draw_drc_theme_map(const WiiUMapView& view)
{
    if (!g_drc_map_pixels)
        return;
    const WiiUMapRect& rect = WIIU_MAP_FRAMED_RECT;
    int source_x[WIIU_MAP_SIZE];
    for (int x = 0; x < rect.width; ++x)
        source_x[x] = wiiu_map_sample(x, rect.width, view.x, view.scale);
    for (int y = 0; y < rect.height; ++y) {
        const int source_y = wiiu_map_sample(y, rect.height, view.y, view.scale);
        const uint8_t* source = g_drc_map_pixels + source_y * WIIU_MAP_SIZE * 4;
        uint8_t* dest = gfx_gx2_drc_pixel(g_drc_panel_base, rect.x, rect.y + y);
        for (int x = 0; x < rect.width; ++x)
            memcpy(dest + x * 4, source + source_x[x] * 4, 4);
    }
}

static void gfx_gx2_compose_drc_theme_base(void)
{
    const WiiUMinimapTextureCache* background = gfx_gx2_load_minimap_texture(g_drc_theme_background);
    for (unsigned y = 0; y < DRC_HEIGHT; ++y)
        memcpy(gfx_gx2_drc_pixel(g_drc_panel_base, 0, y),
               background->pixels + y * DRC_WIDTH * 4, DRC_WIDTH * 4);

    const WiiUMapRect& rect = WIIU_MAP_FRAMED_RECT;
    if (g_drc_gpu_zoom) {
        for (int y = rect.y; y < rect.y + rect.height; ++y)
            memset(gfx_gx2_drc_pixel(g_drc_panel_base, rect.x, y), 0, rect.width * 4);
    } else if (g_drc_map_pixels) {
        // Draw the map before its translucent rim and zoom button.
        gfx_gx2_draw_drc_theme_map({0, 0, 1});
    }
    gfx_gx2_draw_drc_theme_trim(g_drc_screen_rect);
    g_drc_theme_labels_valid = false;
    g_drc_theme_counter = -1;
}

static void gfx_gx2_draw_drc_texture(const char* path, int x, int y, int width, int height,
                                     const WiiUMapRect& clip = g_drc_screen_rect)
{
    gfx_gx2_draw_drc_image(gfx_gx2_load_minimap_texture(path), x, y, width, height, clip);
}

static void gfx_gx2_draw_drc_texture_rotated(const char* path,
                                              int center_x, int center_y,
                                              int size, float angle)
{
    WiiUMinimapTextureCache* texture = gfx_gx2_load_minimap_texture(path);
    if (!texture || size <= 0)
        return;

    const int half = size / 2;
    const float sine = sinf(angle);
    const float cosine = cosf(angle);
    for (int dest_y = -half; dest_y < size - half; ++dest_y)
    {
        for (int dest_x = -half; dest_x < size - half; ++dest_x)
        {
            /* Inverse of a clockwise screen-space rotation. */
            float source_xf = cosine * dest_x - sine * dest_y + half;
            float source_yf = sine * dest_x + cosine * dest_y + half;
            int source_x = (int)(source_xf * texture->width / size);
            int source_y = (int)(source_yf * texture->height / size);
            if (source_x < 0 || source_y < 0 ||
                source_x >= texture->width || source_y >= texture->height)
                continue;

            const uint8_t* source = texture->pixels +
                ((size_t)source_y * texture->width + source_x) * 4;
            if (source[3] == 0)
                continue;
            int panel_x = center_x + dest_x;
            int panel_y = center_y + dest_y;
            if (!gfx_gx2_drc_contains(gfx_gx2_drc_map_rect(), panel_x, panel_y))
                continue;

            uint8_t* dest = gfx_gx2_drc_pixel(
                (uint8_t*)g_drc_panel_image_data, panel_x, panel_y);
            uint32_t alpha = source[3];
            dest[0] = (source[0] * alpha + dest[0] * (255 - alpha)) / 255;
            dest[1] = (source[1] * alpha + dest[1] * (255 - alpha)) / 255;
            dest[2] = (source[2] * alpha + dest[2] * (255 - alpha)) / 255;
            dest[3] = alpha + dest[3] * (255 - alpha) / 255;
        }
    }
}

static int gfx_gx2_hud_number_width(int value, int size)
{
    int digits = 1;
    if (value < 0)
        value = 0;
    while (value >= 10)
    {
        value /= 10;
        ++digits;
    }
    return digits * size;
}

static void gfx_gx2_draw_hud_number(int value, int x, int y, int size)
{
    int divisor = 1;
    if (value < 0)
        value = 0;
    if (value > 9999)
        value = 9999;
    while (value / divisor >= 10)
        divisor *= 10;

    do
    {
        int digit = value / divisor;
        gfx_gx2_draw_drc_texture(g_hud_digit_paths[digit], x, y, size, size);
        x += size;
        value %= divisor;
        divisor /= 10;
    } while (divisor != 0);
}

static void gfx_gx2_draw_drc_hud_row(const char* icon_path, int value, int y)
{
    if (g_drc_framed_layout) {
        gfx_gx2_draw_drc_texture(icon_path, 606, y, 40, 40);
        gfx_gx2_draw_drc_texture(g_hud_multiply_path, 657, y + 10, 20, 20);
        const int size = value > 999 ? 32 : 42;
        gfx_gx2_draw_hud_number(value, 690, y + (40 - size) / 2, size);
        return;
    }
    const int icon_x = DRC_MAP_SIZE + 58;
    const int multiply_x = DRC_MAP_SIZE + 126;
    const int number_right = DRC_WIDTH - 32;
    const int number_size = 32;
    int number_x = number_right - gfx_gx2_hud_number_width(value, number_size);

    gfx_gx2_draw_drc_texture(icon_path, icon_x, y, 40, 40);
    gfx_gx2_draw_drc_texture(g_hud_multiply_path, multiply_x, y + 10, 20, 20);
    gfx_gx2_draw_hud_number(value, number_x, y + 4, number_size);
}

static void gfx_gx2_draw_drc_hud_separator(int y)
{
    if (g_drc_framed_layout)
        return;
    for (int separator_y = y; separator_y < y + 2; ++separator_y)
    {
        for (int x = DRC_MAP_SIZE + 24; x < (int)DRC_WIDTH - 24; ++x)
            gfx_gx2_drc_put_pixel(x, separator_y, 39, 43, 65, 255);
    }
}

static void gfx_gx2_draw_drc_power_meter(int wedges, const WiiUMapRect& clip)
{
    const int size = g_drc_framed_layout ? 208 : 200;
    const int x = 667 - size / 2;
    const int y = g_drc_framed_layout ? 201 : 230;
    const int half_width = size / 2;
    gfx_gx2_draw_drc_texture(g_power_left_path, x, y, half_width, size, clip);
    gfx_gx2_draw_drc_texture(g_power_right_path, x + half_width, y,
                              half_width, size, clip);
    if (wedges > 0)
    {
        if (wedges > 8)
            wedges = 8;
        gfx_gx2_draw_drc_texture(g_power_segment_paths[wedges - 1],
                                  x + size / 4, y + size / 4,
                                  size / 2, size / 2, clip);
    }
}

static void gfx_gx2_draw_drc_timer(uint16_t timer)
{
    const int size = 32;
    int minutes = timer / 1800;
    int seconds = (timer % 1800) / 30;
    int tenths = (timer % 30) / 3;
    const int width = gfx_gx2_hud_number_width(minutes, size) + 5 * size;
    const int y = g_drc_framed_layout ? 394 : 440;
    int x = 667 - width / 2;

    gfx_gx2_draw_hud_number(minutes, x, y, size);
    x += gfx_gx2_hud_number_width(minutes, size);
    gfx_gx2_draw_drc_texture(g_hud_apostrophe_path, x, y, 32, 32);
    gfx_gx2_draw_hud_number(seconds / 10, x + 32, y, size);
    gfx_gx2_draw_hud_number(seconds % 10, x + 64, y, size);
    gfx_gx2_draw_drc_texture(g_hud_quote_path, x + 96, y, 32, 32);
    gfx_gx2_draw_hud_number(tenths, x + 128, y, size);
}

static void gfx_gx2_refresh_drc_texture_pack(void)
{
    static uint32_t previous_generation;
    const uint32_t generation = fs_texture_pack_generation();
    if (previous_generation == generation)
        return;
    previous_generation = generation;
    for (size_t i = 0; i < g_minimap_texture_cache_count; ++i)
        stbi_image_free(g_minimap_texture_cache[i].pixels);
    g_minimap_texture_cache_count = 0;
    g_minimap_textures_preloaded = false;
    memset(g_dialog_glyphs, 0, sizeof(g_dialog_glyphs));
    g_music_title_sequence = -2;
    g_drc_panel_base_valid = false;
}

static void gfx_gx2_draw_drc_map_status(void)
{
    const int title_strip_height = 56;
    const int coin_strip_top = 442;
    WiiUGamePadHudFrame frame = {};
    wiiu_gamepad_hud_get_frame(&frame);

    /* These overlays animate independently of the static map. Restore only
       their narrow strips instead of recomposing the 480x480 background. */
    for (int y = 0; y < title_strip_height; ++y)
        memcpy(gfx_gx2_drc_pixel((uint8_t*)g_drc_panel_image_data, 0, y),
               gfx_gx2_drc_pixel(g_drc_panel_base, 0, y), DRC_MAP_SIZE * 4);
    for (int y = coin_strip_top; y < (int)DRC_MAP_SIZE; ++y)
        memcpy(gfx_gx2_drc_pixel((uint8_t*)g_drc_panel_image_data, 0, y),
               gfx_gx2_drc_pixel(g_drc_panel_base, 0, y), DRC_MAP_SIZE * 4);

    int red_coins = frame.red_coins;
    if (red_coins < 0) red_coins = 0;
    if (red_coins > 8) red_coins = 8;
    const int coin_size = 30;
    const int coin_pitch = 32;
    const int coin_start_x = DRC_MAP_SIZE - 10 - red_coins * coin_pitch;
    for (int i = 0; i < red_coins; ++i)
        gfx_gx2_blit_drc_image((uint8_t*)g_drc_panel_image_data,
            gfx_gx2_load_minimap_texture(g_red_coin_paths[frame.red_coin_frame & 3]),
            coin_start_x + i * coin_pitch, 446, coin_size, coin_size,
            g_drc_screen_rect, 255, true);

    gfx_gx2_cache_music_title(frame.music_valid ? frame.music_id : -1);
    gfx_gx2_draw_drc_image(&g_music_title_texture, 0, 0, DRC_MAP_SIZE, title_strip_height);
    gfx_gx2_drc_flush_rect(0, 0, DRC_MAP_SIZE - 1,
                           title_strip_height - 1);
    gfx_gx2_drc_flush_rect(0, coin_strip_top, DRC_MAP_SIZE - 1,
                           DRC_MAP_SIZE - 1);
}
#endif

static void gfx_gx2_restore_drc_rect(const WiiUMapRect& rect)
{
    if (!g_drc_panel_base || !g_drc_panel_image_data)
        return;
    for (int y = rect.y; y < rect.y + rect.height; ++y)
    {
        uint8_t* dest = gfx_gx2_drc_pixel(
            (uint8_t*)g_drc_panel_image_data, rect.x, y);
        uint8_t* source = gfx_gx2_drc_pixel(g_drc_panel_base, rect.x, y);
        memcpy(dest, source, rect.width * 4);
    }
}

#ifdef EXTERNAL_DATA
#include "wiiu_gamepad_theme.inc.h"
#endif

static void gfx_gx2_update_drc_hud(void)
{
    WiiUGamePadHudFrame frame = {};
    bool visible = wiiu_gamepad_hud_get_frame(&frame);
    /* Map-strip data is redrawn separately and must not invalidate the much
       larger right-hand HUD every time the red coin animation advances. */
    frame.red_coins = 0;
    frame.red_coin_frame = 0;
    frame.music_id = 0;
    frame.music_valid = false;
    // Only tenths are displayed. Raw 30 Hz timer ticks must not invalidate
    // the HUD on every tick (or twice per tick in a 60 FPS build).
    if (g_drc_framed_layout) {
        frame.flags &= ~WIIU_GAMEPAD_HUD_TIMER;
        frame.timer = 0;
    } else if (frame.flags & WIIU_GAMEPAD_HUD_TIMER)
        frame.timer = (frame.timer / 3) * 3;
    if (g_drc_hud_valid && visible == g_drc_hud_visible &&
        (!visible || !memcmp(&frame, &g_drc_hud_frame, sizeof(frame))))
        return;

#ifdef EXTERNAL_DATA
    if (g_drc_hud_valid && visible && g_drc_hud_visible &&
        (frame.flags & WIIU_GAMEPAD_HUD_TIMER))
    {
        WiiUGamePadHudFrame previous = g_drc_hud_frame;
        previous.timer = frame.timer;
        if (!memcmp(&frame, &previous, sizeof(frame)))
        {
            // Restore and redraw only the timer. Repainting the power meter
            // and the whole side panel here costs several ms during races.
            const int width = 224;
            const int x = 667 - width / 2;
            const int y = 440;
            for (int row = y; row < y + 32; ++row)
                memcpy(gfx_gx2_drc_pixel((uint8_t*)g_drc_panel_image_data, x, row),
                       gfx_gx2_drc_pixel(g_drc_panel_base, x, row), width * 4);
            gfx_gx2_draw_drc_timer(frame.timer);
            gfx_gx2_drc_flush_rect(x, y, x + width - 1, y + 31);
            g_drc_hud_frame = frame;
            return;
        }
    }
#endif

    const WiiUMapRect hud_rect = g_drc_framed_layout ? WiiUMapRect{483, 35, 371, 357}
        : WiiUMapRect{483, 0, 371, 480};
    gfx_gx2_restore_drc_rect(hud_rect);
#ifdef EXTERNAL_DATA
    if (visible)
    {
        int row = 0;
        /* Leave breathing room above the compact status rows. */
        const int first_row_y = g_drc_framed_layout ? 54 : 64;
        const int row_pitch = g_drc_framed_layout ? 46 : 58;
        if (frame.flags & WIIU_GAMEPAD_HUD_LIVES)
        {
            int y = first_row_y + row++ * row_pitch;
            gfx_gx2_draw_drc_hud_row(g_hud_mario_path, frame.lives, y);
            gfx_gx2_draw_drc_hud_separator(y + 49);
        }
        if (frame.flags & WIIU_GAMEPAD_HUD_STARS)
        {
            int y = first_row_y + row++ * row_pitch;
            gfx_gx2_draw_drc_hud_row(g_hud_star_path, frame.stars, y);
            gfx_gx2_draw_drc_hud_separator(y + 49);
        }
        if (frame.flags & WIIU_GAMEPAD_HUD_COINS)
        {
            int y = first_row_y + row++ * row_pitch;
            gfx_gx2_draw_drc_hud_row(g_hud_coin_path, frame.coins, y);
            gfx_gx2_draw_drc_hud_separator(y + 49);
        }
        if (frame.flags & WIIU_GAMEPAD_HUD_POWER)
            gfx_gx2_draw_drc_power_meter(frame.wedges, hud_rect);
        if (frame.flags & WIIU_GAMEPAD_HUD_TIMER)
            gfx_gx2_draw_drc_timer(frame.timer);
    }
#endif
    gfx_gx2_drc_flush_rect(hud_rect.x, hud_rect.y, hud_rect.x + hud_rect.width - 1,
                           hud_rect.y + hud_rect.height - 1);
    g_drc_hud_frame = frame;
    g_drc_hud_visible = visible;
    g_drc_hud_valid = true;
}

static void gfx_gx2_restore_drc_marker(void)
{
    if (!g_drc_marker_valid || !g_drc_panel_base || !g_drc_panel_image_data)
        return;
    const int radius = 40;
    int left = g_drc_marker_x - radius;
    int right = g_drc_marker_x + radius;
    int top = g_drc_marker_y - radius;
    int bottom = g_drc_marker_y + radius;
    const WiiUMapRect& clip = gfx_gx2_drc_map_rect();
    if (left < clip.x) left = clip.x;
    if (right >= clip.x + clip.width) right = clip.x + clip.width - 1;
    if (top < clip.y) top = clip.y;
    if (bottom >= clip.y + clip.height) bottom = clip.y + clip.height - 1;
    if (left > right || top > bottom)
        return;
    gfx_gx2_restore_drc_rect({left, top, right - left + 1, bottom - top + 1});
    gfx_gx2_drc_flush_rect(left, top, right, bottom);
}

static void gfx_gx2_draw_drc_marker(float x, float y, float direction)
{
    int center_x = (int)(x + 0.5f);
    int center_y = (int)(y + 0.5f);

#ifdef EXTERNAL_DATA
    /* The original arrow points down. That already corresponds to SM64 yaw 0
       (+Z) in the generated top-down maps, so rotate it clockwise with yaw. */
    const int arrow_distance = 27;
    int arrow_x = center_x + (int)(sinf(direction) * arrow_distance);
    int arrow_y = center_y + (int)(cosf(direction) * arrow_distance);
    gfx_gx2_draw_drc_texture_rotated(g_minimap_arrow_path,
                                      arrow_x, arrow_y, 18, direction);
    gfx_gx2_draw_drc_image(gfx_gx2_load_minimap_texture(g_hud_mario_path),
                           center_x - 18, center_y - 18, 36, 36, gfx_gx2_drc_map_rect());
    const WiiUMapRect& button = WIIU_MAP_ZOOM_BUTTON;
    if (g_drc_framed_layout && center_x + 40 >= button.x && center_x - 40 < button.x + button.width &&
        center_y + 40 >= button.y && center_y - 40 < button.y + button.height) {
        gfx_gx2_restore_drc_rect(button);
        gfx_gx2_drc_flush_rect(button.x, button.y, button.x + button.width - 1,
                               button.y + button.height - 1);
    }
#else
    for (int py = -9; py <= 9; ++py)
    {
        for (int px = -9; px <= 9; ++px)
        {
            int distance_squared = px * px + py * py;
            if (distance_squared <= 81)
            {
                if (distance_squared >= 49)
                    gfx_gx2_drc_put_pixel(center_x + px, center_y + py,
                                          255, 255, 255, 255);
                else
                    gfx_gx2_drc_put_pixel(center_x + px, center_y + py,
                                          220, 35, 42, 255);
            }
        }
    }

    /* SM64 yaw 0 moves along +Z; +Z points down in the generated maps. */
    float end_x = center_x + sinf(direction) * 20.0f;
    float end_y = center_y + cosf(direction) * 20.0f;
    for (int step = 5; step <= 20; ++step)
    {
        float fraction = step / 20.0f;
        int line_x = (int)(center_x + (end_x - center_x) * fraction + 0.5f);
        int line_y = (int)(center_y + (end_y - center_y) * fraction + 0.5f);
        for (int oy = -1; oy <= 1; ++oy)
            for (int ox = -1; ox <= 1; ++ox)
                gfx_gx2_drc_put_pixel(line_x + ox, line_y + oy,
                                      255, 226, 74, 255);
    }
#endif

    g_drc_marker_x = center_x;
    g_drc_marker_y = center_y;
    g_drc_marker_valid = true;
    gfx_gx2_drc_flush_rect(center_x - 40, center_y - 40,
                           center_x + 40, center_y + 40);
}

static bool gfx_gx2_prepare_drc_press_start(void)
{
#ifdef EXTERNAL_DATA
    if (g_drc_panel_base_valid && !g_drc_panel_texture_path)
        return true;
    /* The 3DS title generator uses 16px coloured HUD glyphs with a 12px
       advance. Compose them here so mounted HD replacements work too. */
    static const char text[] = "PRESS START";
    static char glyph_paths[26][80] = {};
    const int scale = 3, glyph_size = 16 * scale, advance = 12 * scale;
    const int width = (sizeof(text) - 2) * advance + glyph_size;
    int x = ((int)DRC_WIDTH - width) / 2;
    const int y = ((int)DRC_HEIGHT - glyph_size) / 2;
    memset(g_drc_panel_image_data, 0, g_drc_panel_color_buffer.surface.imageSize);
    for (uint32_t row = 0; row < DRC_HEIGHT; ++row)
        for (uint32_t column = 0; column < g_drc_panel_color_buffer.surface.pitch; ++column)
            gfx_gx2_drc_pixel((uint8_t*)g_drc_panel_image_data, column, row)[3] = 255;
    for (const char* ch = text; *ch; ++ch, x += advance) {
        if (*ch == ' ') continue;
        char* path = glyph_paths[*ch - 'A'];
        if (!path[0]) snprintf(path, sizeof(glyph_paths[0]),
            "gfx/textures/segment2/segment2.%05X.rgba16.png", 0x1400 + (*ch - 'A') * 0x200);
        gfx_gx2_draw_drc_texture(path, x, y, glyph_size, glyph_size);
    }
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU, g_drc_panel_image_data,
                  g_drc_panel_color_buffer.surface.imageSize);
    /* A null texture tags the cached idle screen. Returning to a map always
       rebuilds the base and overlays, including after foreground reacquire. */
    g_drc_panel_full_flush_pending = false;
    g_drc_panel_texture_path = nullptr;
    g_drc_panel_base_valid = true;
    g_drc_marker_valid = false;
    g_drc_hud_valid = false;
    return true;
#else
    return false;
#endif
}

static bool gfx_gx2_prepare_drc_panel(bool touch_pressed)
{
    if (!g_drc_panel_image_data || !g_drc_panel_base)
        return false;

    for (unsigned y = 0; y < DRC_HEIGHT; ++y) {
        g_drc_dirty_left[y] = DRC_WIDTH;
        g_drc_dirty_right[y] = 0;
    }

    gfx_gx2_refresh_drc_texture_pack();
    struct WiiUMinimapFrame frame = {};
    bool show_map = wiiu_minimap_get_frame(&frame);
    /* The title/file menus get a 3DS-style idle screen. Star selection and
       transitions still mirror TV; a dialogue in a playable area keeps its map. */
    if (!show_map) {
        g_drc_gpu_zoom = false;
        return wiiu_minimap_get_menu_screen() == WIIU_MINIMAP_MENU_PRESS_START
            ? gfx_gx2_prepare_drc_press_start() : false;
    }
    if (touch_pressed) {
        g_drc_map_zoomed = !g_drc_map_zoomed;
        WHBLogPrintf("[gamepad-panel] map zoom=%s", g_drc_map_zoomed ? "2x follow" : "full");
    }
#ifdef EXTERNAL_DATA
    if (g_drc_prefer_framed_layout && (!g_drc_panel_base_valid || !g_drc_panel_texture_path)) {
        const bool framed_available = gfx_gx2_framed_layout_available();
        if (g_drc_framed_layout != framed_available)
            g_drc_panel_base_valid = false;
        g_drc_framed_layout = framed_available;
    } else if (!g_drc_prefer_framed_layout && g_drc_framed_layout) {
        g_drc_framed_layout = false;
        g_drc_panel_base_valid = false;
    }
#endif
    bool gpu_zoom = g_drc_map_zoomed && g_drc_map_pixels && gfx_gx2_gamepad_ready();
    if (gpu_zoom != g_drc_gpu_zoom) {
        g_drc_gpu_zoom = gpu_zoom;
        g_drc_panel_base_valid = false;
        WHBLogPrintf("[gamepad-zoom] renderer=%s", gpu_zoom ? "gpu" : "cpu");
    }
#ifdef EXTERNAL_DATA
    gfx_gx2_preload_minimap_textures();
#endif

    bool texture_changed = !g_drc_panel_texture_path ||
                           strcmp(g_drc_panel_texture_path, frame.texture_path);
    if (!g_drc_panel_base_valid || texture_changed)
        gfx_gx2_compose_drc_panel(&frame);

    WiiUMapView view = gfx_gx2_update_drc_map_view(frame);
    gfx_gx2_restore_drc_marker();
    gfx_gx2_update_drc_hud();
#ifdef EXTERNAL_DATA
    if (g_drc_framed_layout)
        gfx_gx2_draw_drc_theme_status(frame);
    else
        gfx_gx2_draw_drc_map_status();
#endif
    const WiiUMapRect& rect = gfx_gx2_drc_map_rect();
    gfx_gx2_draw_drc_marker(rect.x + (frame.mario_x - view.x) * view.scale * rect.width / DRC_MAP_SIZE,
                            rect.y + (frame.mario_y - view.y) * view.scale * rect.height / DRC_MAP_SIZE,
                            frame.mario_direction);
    if (g_drc_panel_full_flush_pending) {
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU, g_drc_panel_image_data,
                      g_drc_panel_color_buffer.surface.imageSize);
        g_drc_panel_full_flush_pending = false;
    } else if (g_drc_gpu_zoom) {
        /* Publish small UI edits without 480 GX2Invalidate/sync calls or a
         * full-surface CPU writeback. The GPU renderer invalidates its texture
         * cache once after this CPU store barrier. */
        for (unsigned y = 0; y < DRC_HEIGHT; ++y)
            if (g_drc_dirty_left[y] <= g_drc_dirty_right[y])
                DCStoreRangeNoSync(gfx_gx2_drc_pixel((uint8_t*)g_drc_panel_image_data,
                                      g_drc_dirty_left[y], y),
                                  (g_drc_dirty_right[y] - g_drc_dirty_left[y] + 1) * 4);
        OSMemoryBarrier();
    }
    return g_drc_panel_base_valid;
}

#endif

static bool gfx_gx2_window_foreground_acquire_callback(void)
{
    gfx_gx2_set_gpu_available(true);
    //WHBLogPrint("Acquiring foreground");

    // Get the MEM1 heap and Foreground Bucket heap handles
    g_mem1_heap_handle = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM1);
    g_fg_heap_handle = MEMGetBaseHeapHandle(MEM_BASE_HEAP_FG);

    if (!(g_mem1_heap_handle && g_fg_heap_handle))
        return false;

    // Allocate TV scan buffer
    {
        GX2TVRenderMode tv_render_mode;

        // Get current TV scan mode
        GX2TVScanMode tv_scan_mode = GX2GetSystemTVScanMode();
        // Get current TV aspect ratio
        GX2AspectRatio tv_aspect_ratio = GX2GetSystemTVAspectRatio();

        // Determine TV framebuffer dimensions
        switch (tv_scan_mode)
        {
        case GX2_TV_SCAN_MODE_576I:
        case GX2_TV_SCAN_MODE_480I:
        case GX2_TV_SCAN_MODE_480P:
            if (tv_aspect_ratio == GX2_ASPECT_RATIO_4_3)
            {
                g_tv_width = 640;
                g_tv_height = 480;
                tv_render_mode = GX2_TV_RENDER_MODE_STANDARD_480P;
            }
            else // if (tv_aspect_ratio == GX2_ASPECT_RATIO_16_9)
            {
                g_tv_width = 854;
                g_tv_height = 480;
                tv_render_mode = GX2_TV_RENDER_MODE_WIDE_480P;
            }
            break;
        case GX2_TV_SCAN_MODE_720P:
        default:
            g_tv_width = 1280;
            g_tv_height = 720;
            tv_render_mode = GX2_TV_RENDER_MODE_WIDE_720P;
            break;
        case GX2_TV_SCAN_MODE_1080I:
        case GX2_TV_SCAN_MODE_1080P:
            g_tv_width = 1920;
            g_tv_height = 1080;
            tv_render_mode = GX2_TV_RENDER_MODE_WIDE_1080P;
        }

        g_window_width = g_tv_width;
        g_window_height = g_tv_height;
        gfx_gx2_window_refresh_requested_mode();
        gfx_gx2_window_activate_mode(g_requested_mode);

        // Calculate TV scan buffer byte size
        uint32_t tv_scan_buffer_size, unk;
        GX2CalcTVSize(
            tv_render_mode,                       // Render Mode
            GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, // Scan Buffer Surface Format
            GX2_BUFFERING_MODE_DOUBLE,            // Two buffers for double-buffering
            &tv_scan_buffer_size,                 // Output byte size
            &unk                                  // Unknown; seems like we have no use for it
        );

        // Allocate TV scan buffer
        g_tv_scan_buffer = MEMAllocFromFrmHeapEx(
            g_fg_heap_handle,
            tv_scan_buffer_size,
            GX2_SCAN_BUFFER_ALIGNMENT // Required alignment
        );

        if (!g_tv_scan_buffer)
            return false;

        // Flush allocated buffer from CPU cache
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU, g_tv_scan_buffer, tv_scan_buffer_size);

        // Set the current TV scan buffer
        GX2SetTVBuffer(
            g_tv_scan_buffer,                     // Scan Buffer
            tv_scan_buffer_size,                  // Scan Buffer Size
            tv_render_mode,                       // Render Mode
            GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, // Scan Buffer Surface Format
            GX2_BUFFERING_MODE_DOUBLE             // Enable double-buffering
        );

        // Set the current TV scan buffer dimensions
        GX2SetTVScale(g_tv_width, g_tv_height);
    }

    // Allocate DRC (Gamepad) scan buffer
    {
        uint32_t drc_width = 854;
        uint32_t drc_height = 480;

        // Calculate DRC scan buffer byte size
        uint32_t drc_scan_buffer_size, unk;
        GX2CalcDRCSize(
            GX2_DRC_RENDER_MODE_SINGLE,           // Render Mode
            GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, // Scan Buffer Surface Format
            GX2_BUFFERING_MODE_DOUBLE,            // Two buffers for double-buffering
            &drc_scan_buffer_size,                // Output byte size
            &unk                                  // Unknown; seems like we have no use for it
        );

        // Allocate DRC scan buffer
        g_drc_scan_buffer = MEMAllocFromFrmHeapEx(
            g_fg_heap_handle,
            drc_scan_buffer_size,
            GX2_SCAN_BUFFER_ALIGNMENT // Required alignment
        );

        if (!g_drc_scan_buffer)
            return false;

        // Flush allocated buffer from CPU cache
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU, g_drc_scan_buffer, drc_scan_buffer_size);

        // Set the current DRC scan buffer
        GX2SetDRCBuffer(
            g_drc_scan_buffer,                       // Scan Buffer
            drc_scan_buffer_size,                 // Scan Buffer Size
            GX2_DRC_RENDER_MODE_SINGLE,           // Render Mode
            GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, // Scan Buffer Surface Format
            GX2_BUFFERING_MODE_DOUBLE             // Enable double-buffering
        );

        // Set the current DRC scan buffer dimensions
        GX2SetDRCScale(drc_width, drc_height);
    }

    // Keep the replaceable render targets behind a frame-heap state marker so
    // resolution changes can rebuild them safely between two frames.
    if (!MEMRecordStateForFrmHeap(g_mem1_heap_handle, RENDER_BUFFERS_HEAP_TAG) ||
        !gfx_gx2_window_create_render_buffers())
        return false;

    // Enable TV and DRC
    GX2SetTVEnable(true);
    GX2SetDRCEnable(true);

    // If not first time in foreground, restore the GX2 context state
    if (is_running)
    {
        GX2SetContextState(g_context);
        GX2SetColorBuffer(&g_color_buffer, GX2_RENDER_TARGET_0);
        GX2SetDepthBuffer(&g_depth_buffer);
        GX2SetTVScale(g_tv_width, g_tv_height);
        GX2SetDRCScale(854, 480);
    }

    // Initialize GQR2 to GQR5
    asm volatile ("mtspr %0, %1" : : "i" (898), "r" (0x00040004));
    asm volatile ("mtspr %0, %1" : : "i" (899), "r" (0x00050005));
    asm volatile ("mtspr %0, %1" : : "i" (900), "r" (0x00060006));
    asm volatile ("mtspr %0, %1" : : "i" (901), "r" (0x00070007));

    return true;
}

static void gfx_gx2_window_init(UNUSED const char*)
{
    WHBLogCafeInit();
    WHBLogUdpInit();
    WHBInitCrashHandler();

    // Allocate GX2 command buffer
    g_cmd_list = MEMAllocFromDefaultHeapEx(
        0x400000,                    // A very commonly used size in Nintendo games
        GX2_COMMAND_BUFFER_ALIGNMENT // Required alignment
    );

    if (!g_cmd_list)
        gfx_gx2_window_exit_callback();

    // Several parameters to initialize GX2 with
    uint32_t init_attribs[] = {
        GX2_INIT_CMD_BUF_BASE, (uintptr_t)g_cmd_list, // Command Buffer Base Address
        GX2_INIT_CMD_BUF_POOL_SIZE, 0x400000,         // Command Buffer Size
        GX2_INIT_ARGC, 0,                             // main() arguments count
        GX2_INIT_ARGV, (uintptr_t)nullptr,            // main() arguments vector
        GX2_INIT_END                                  // Ending delimiter
    };

    // Initialize GX2
    GX2Init(init_attribs);
    gfx_gx2_set_gpu_available(true);

    // Create TV and DRC scan, color and depth buffers
    if (!gfx_gx2_window_foreground_acquire_callback())
        gfx_gx2_window_exit_callback();

    // Allocate context state instance
    g_context = (GX2ContextState*)MEMAllocFromDefaultHeapEx(
        sizeof(GX2ContextState),    // Size of context
        GX2_CONTEXT_STATE_ALIGNMENT // Required alignment
    );

    if (!g_context)
        gfx_gx2_window_exit_callback();

    // Initialize it to default state
    GX2SetupContextStateEx(g_context, false);

    // Make context of window current
    GX2SetContextState(g_context);
    GX2SetColorBuffer(&g_color_buffer, GX2_RENDER_TARGET_0);
    GX2SetDepthBuffer(&g_depth_buffer);

    // Set swap interval to 2 by default
    // (Set it to 1 when using the 60FPS patch)
#ifdef HIGH_FPS_PC
    GX2SetSwapInterval(1);
#else
    GX2SetSwapInterval(2);
#endif

    // Set the default viewport and scissor
    GX2SetViewport(g_render_offset_x, g_render_offset_y,
                   g_window_width, g_window_height, 0.0f, 1.0f);
    GX2SetScissor(g_render_offset_x, g_render_offset_y,
                  g_window_width, g_window_height);

    GX2SetTVScale(g_tv_width, g_tv_height);
    GX2SetDRCScale(854, 480);

    // Initialize ProcUI
    ProcUIInit(&OSSavesDone_ReadyToRelease);
    g_procui_initialized = true;

    is_running = true;
}

static void gfx_gx2_window_main_loop(void (*run_one_game_iter)(void))
{
    run_one_game_iter();
}

static void gfx_gx2_window_get_dimensions(uint32_t *width, uint32_t *height)
{
    *width = g_window_width;
    *height = g_window_height;
}

static bool gfx_gx2_window_apply_video_mode_change(void)
{
    if (!gfx_gx2_window_mode_changed())
        return true;

    WiiUVideoMode old_mode = g_active_mode;
    bool rebuild_buffers = gfx_gx2_window_mode_needs_new_buffers();

    if (rebuild_buffers &&
        !MEMFreeByStateToFrmHeap(g_mem1_heap_handle, RENDER_BUFFERS_HEAP_TAG))
    {
        WHBLogPrint("[present] hot rebuild failed: could not release render-buffer heap state");
        return false;
    }

    if (rebuild_buffers)
    {
        g_color_buffer_image_data = nullptr;
        g_depth_buffer_image_data = nullptr;
        g_options_background_image_data = nullptr;
#ifdef WIIU_MINIMAP
        g_drc_panel_image_data = nullptr;
        g_drc_panel_base_valid = false;
#endif
    }

    // The cached scene uses the old viewport even when only the aspect ratio
    // changes and the surface can be reused. Capture it again in either case.
    g_options_background_valid = false;
    g_restore_options_background = false;

    gfx_gx2_window_activate_mode(g_requested_mode);

    if (rebuild_buffers)
    {
        if (!MEMRecordStateForFrmHeap(g_mem1_heap_handle, RENDER_BUFFERS_HEAP_TAG) ||
            !gfx_gx2_window_create_render_buffers())
        {
            WHBLogPrint("[present] hot rebuild failed: could not allocate new render buffers");
            return false;
        }
    }

    GX2SetContextState(g_context);
    GX2SetColorBuffer(&g_color_buffer, GX2_RENDER_TARGET_0);
    GX2SetDepthBuffer(&g_depth_buffer);
    GX2SetViewport(g_render_offset_x, g_render_offset_y,
                   g_window_width, g_window_height, 0.0f, 1.0f);
    GX2SetScissor(g_render_offset_x, g_render_offset_y,
                  g_window_width, g_window_height);
    GX2SetTVScale(g_tv_width, g_tv_height);
    GX2SetDRCScale(854, 480);

    WHBLogPrintf("[present] hot change (%s): resolution %u->%u aspect %u->%u surface=%ux%u content=%ux%u offset=%u,%u",
                 rebuild_buffers ? "buffers" : "layout",
                 old_mode.resolution, g_active_mode.resolution,
                 old_mode.aspect, g_active_mode.aspect,
                 g_render_target_width, g_render_target_height,
                 g_window_width, g_window_height,
                 g_render_offset_x, g_render_offset_y);
    return true;
}

static void gfx_gx2_window_handle_events(void)
{
    ProcUIStatus status = ProcUIProcessMessages(true);
    ProcUIStatus previous_status = status;

    if (status == PROCUI_STATUS_RELEASE_FOREGROUND)
    {
        gfx_gx2_window_foreground_release_callback();
        status = ProcUIProcessMessages(true);
    }

    if (status == PROCUI_STATUS_EXITING ||
        (previous_status == PROCUI_STATUS_RELEASE_FOREGROUND &&
         status == PROCUI_STATUS_IN_FOREGROUND &&
         !gfx_gx2_window_foreground_acquire_callback()))
    {
        gfx_gx2_window_exit_callback();
    }
}

static bool gfx_gx2_window_start_frame(void)
{
    if (!is_running)
        return false;

    if (g_restore_options_background)
    {
        GX2CopySurface(&g_options_background_surface, 0, 0,
                       &g_color_buffer.surface, 0, 0);
    }
    else
    {
        GX2ClearColor(
            &g_color_buffer,
            0.0f, 0.0f, 0.0f, 1.0f
        );

        GX2ClearDepthStencilEx(
            &g_depth_buffer,
            g_depth_buffer.depthClear,
            g_depth_buffer.stencilClear,
            (GX2ClearFlags)(GX2_CLEAR_FLAGS_DEPTH |
                            GX2_CLEAR_FLAGS_STENCIL)
        );
    }

    GX2SetContextState(g_context);

    return true;
}

#ifdef WIIU_MINIMAP
static unsigned int gfx_gx2_window_gamepad_mode(void)
{
    if (configWiiUGamePadDisplay >= WIIU_GAMEPAD_DISPLAY_COUNT)
        configWiiUGamePadDisplay = WIIU_GAMEPAD_MIRROR_TV;
    return configWiiUGamePadDisplay;
}

#endif

static void gfx_gx2_window_swap_buffers_begin(void)
{
    // Make sure to flush all commands to GPU before copying the color buffer to the scan buffers
    // (Calling GX2DrawDone instead here *may* cause slow downs)
    GX2Flush();

    // Copy the color buffer to the TV and DRC scan buffers
    GX2CopyColorBufferToScanBuffer(&g_color_buffer, GX2_SCAN_TARGET_TV);
#ifdef WIIU_MINIMAP
    const unsigned mode = gfx_gx2_window_gamepad_mode();
    GX2SetDRCEnable(mode != WIIU_GAMEPAD_OFF);
    const bool touch_pressed = wiiu_gamepad_take_touch_press();
    if (mode == WIIU_GAMEPAD_STATUS_PANEL && gfx_gx2_prepare_drc_panel(touch_pressed)) {
        GX2ColorBuffer* panel = &g_drc_panel_color_buffer;
        if (g_drc_gpu_zoom) {
            if (auto* composed = gfx_gx2_gamepad_compose(panel, g_drc_map_pixels,
                    g_drc_map_generation, g_drc_map_view, gfx_gx2_drc_map_rect()))
                panel = composed;
        }
        GX2CopyColorBufferToScanBuffer(panel, GX2_SCAN_TARGET_DRC);
    } else if (mode != WIIU_GAMEPAD_OFF) {
        GX2CopyColorBufferToScanBuffer(&g_color_buffer, GX2_SCAN_TARGET_DRC);
    }
#else
    GX2CopyColorBufferToScanBuffer(&g_color_buffer, GX2_SCAN_TARGET_DRC);
#endif
    // Flip
    GX2SwapScanBuffers();

    // Reset context state for next frame
    GX2SetContextState(g_context);
    GX2SetColorBuffer(&g_color_buffer, GX2_RENDER_TARGET_0);
    GX2SetDepthBuffer(&g_depth_buffer);
    GX2SetColorBuffer(&g_color_buffer, GX2_RENDER_TARGET_0);
    GX2SetDepthBuffer(&g_depth_buffer);
    GX2SetViewport(g_render_offset_x, g_render_offset_y,
                   g_window_width, g_window_height, 0.0f, 1.0f);
    GX2SetScissor(g_render_offset_x, g_render_offset_y,
                  g_window_width, g_window_height);

    // Flush all commands to GPU before waiting for flip since it will block the CPU
    GX2Flush();

    // Make sure TV and DRC are enabled
    GX2SetTVEnable(true);
#ifndef WIIU_MINIMAP
    GX2SetDRCEnable(true);
#endif
}

// 2022/05/13: OSSecondsToTicks provided by WUT does not allow for fractions of seconds
#define OSSecondsF32ToTicks(val) (uint64_t)((float)(val) * OSTimerClockSpeed)

static void gfx_gx2_window_swap_buffers_end(void)
{
    uint32_t swap_count, flip_count;
    OSTime prev_flip, prev_vsync;

    // Menu changes made while drawing this frame become the requested mode.
    // A resize waits for the current scan copy before replacing its surface.
    gfx_gx2_window_refresh_requested_mode();
    bool finish_flip_for_resize = gfx_gx2_window_mode_changed() &&
                                  gfx_gx2_window_mode_needs_new_buffers();

    uint32_t swap_interval = GX2GetSwapInterval();
    OSTime swap_interval_ticks = swap_interval * OSSecondsF32ToTicks(0.75f / 59.94f);

    if (swap_interval > 0 || finish_flip_for_resize)
    {
        while (true)
        {
            GX2GetSwapStatus(&swap_count, &flip_count, &prev_flip, &prev_vsync);
            if (flip_count >= swap_count)
                break;

            if (finish_flip_for_resize || prev_vsync - prev_flip < swap_interval_ticks)
                GX2WaitForVsync();

            else
            {
                // Swapping is going to happen at the next vsync
                // Do not wait for it (hopefully this does not cause issues)
                break;
            }
        }
    }

    if (!gfx_gx2_window_apply_video_mode_change())
    {
        gfx_gx2_window_exit_callback();
    }
}

static double gfx_gx2_window_get_time(void)
{
    return 0.0;
}

static void gfx_gx2_window_shutdown(void)
{
}

struct GfxWindowManagerAPI gfx_gx2_window = {
    gfx_gx2_window_init,
    gfx_gx2_window_main_loop,
    gfx_gx2_window_get_dimensions,
    gfx_gx2_window_handle_events,
    gfx_gx2_window_start_frame,
    gfx_gx2_window_swap_buffers_begin,
    gfx_gx2_window_swap_buffers_end,
    gfx_gx2_window_get_time,
    gfx_gx2_window_shutdown
};

#endif
