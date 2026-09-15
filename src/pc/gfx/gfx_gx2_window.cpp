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
static MEMHeapHandle    g_mem1_heap_handle        = nullptr;
static MEMHeapHandle    g_fg_heap_handle          = nullptr;

static void gfx_gx2_window_foreground_release_callback(void)
{
    //WHBLogPrint("To release foreground");

    if (is_running)
        ProcUIDrawDoneRelease();

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
}

extern "C" void OSBlockThreadsOnExit(void);
extern "C" void _Exit(int);
extern "C" void KPADShutdown();

static void gfx_gx2_window_exit_callback(void)
{
    //WHBLogPrint("Exit callback");
    is_running = false;

    game_deinit();

    gfx_gx2_free_vbo();
    gfx_gx2_free();

    KPADShutdown();
    AXQuit();

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

    gfx_gx2_window_foreground_release_callback();

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
    return true;
}

static bool gfx_gx2_window_foreground_acquire_callback(void)
{
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
    }

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
        ProcUIShutdown();
        gfx_gx2_window_exit_callback();
    }
}

static bool gfx_gx2_window_start_frame(void)
{
    if (!is_running)
        return false;

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

    GX2SetContextState(g_context);

    return true;
}

static void gfx_gx2_window_swap_buffers_begin(void)
{
    // Make sure to flush all commands to GPU before copying the color buffer to the scan buffers
    // (Calling GX2DrawDone instead here *may* cause slow downs)
    GX2Flush();

    // Copy the color buffer to the TV and DRC scan buffers
    GX2CopyColorBufferToScanBuffer(&g_color_buffer, GX2_SCAN_TARGET_TV);
    GX2CopyColorBufferToScanBuffer(&g_color_buffer, GX2_SCAN_TARGET_DRC);
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
    GX2SetDRCEnable(true);
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
        ProcUIShutdown();
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
