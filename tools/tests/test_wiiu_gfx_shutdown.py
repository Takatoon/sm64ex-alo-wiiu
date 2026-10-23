"""Execute the actual shutdown/event handlers with mocked GX2/ProcUI ownership.

Only platform dependencies are replaced. Extracting these small functions avoids
emulating every graphics type used by the full window translation unit.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(text, name):
    start = re.search(r'^(?:extern "C" |static )?(?:void|bool) ' + name + r'\([^\n]*\)\s*\{', text, re.M)
    assert start, name
    brace = text.index('{', start.start())
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start.start():end]


class Shutdown(unittest.TestCase):
    def test_real_handlers_foreground_background_and_failed_resume(self):
        backend = (ROOT / 'src/pc/gfx/gfx_gx2.cpp').read_text()
        window = (ROOT / 'src/pc/gfx/gfx_gx2_window.cpp').read_text()
        bodies = '\n'.join(function(backend, n) for n in (
            'gfx_gx2_set_gpu_available', 'gfx_gx2_gpu_available', 'gfx_gx2_reclaim_barrier'))
        handlers = '\n'.join(function(window, n) for n in (
            'gfx_gx2_window_foreground_release_callback', 'gfx_gx2_window_exit_callback',
            'gfx_gx2_window_handle_events')).replace('_Exit(-1);', 'test_exit(-1);')
        code = r'''
#include <cassert>
#include <cstdlib>
#include <vector>
#define WG_BEGIN(x) ((void)0)
#define WG_TIME(x,y) ((void)0)
#define WHBLogPrintf(...) ((void)0)
#define WHBLogPrint(...) ((void)0)
#define wiiu_gpu_profile_cancel() ((void)0)
static bool gpu_available, is_running, g_procui_initialized, gx2_alive;
static bool gpu_retired, resources_freed, fail_resume;
static bool g_options_background_valid, g_restore_options_background;
static void *g_fg_heap_handle, *g_mem1_heap_handle, *g_tv_scan_buffer, *g_drc_scan_buffer;
static void *g_color_buffer_image_data, *g_depth_buffer_image_data;
static void *g_options_background_image_data;
static void *g_cmd_list, *g_context;
static unsigned draws, releases, shutdowns, gpu_shutdowns;
static const int MEM_FRM_HEAP_FREE_ALL=0;
enum ProcUIStatus { PROCUI_STATUS_IN_FOREGROUND, PROCUI_STATUS_IN_BACKGROUND,
    PROCUI_STATUS_RELEASE_FOREGROUND, PROCUI_STATUS_EXITING };
static std::vector<ProcUIStatus> messages;
static size_t message_index;
static void GX2DrawDone() {
    assert(gx2_alive && gpu_available);
    gpu_retired=true; ++draws;
}
static void GX2Shutdown() {
    assert(gx2_alive && gpu_available && gpu_retired && resources_freed);
    gx2_alive=false; ++gpu_shutdowns;
}
static void ProcUIDrawDoneRelease() {
    assert(g_procui_initialized && gpu_retired && !gpu_available);
    assert(!g_fg_heap_handle && !g_mem1_heap_handle);
    ++releases;
}
static void ProcUIShutdown() {
    assert(g_procui_initialized && resources_freed && !gpu_available);
    assert(!g_cmd_list && !g_context && !g_fg_heap_handle && !g_mem1_heap_handle);
    gx2_alive=false; ++shutdowns;
}
static ProcUIStatus ProcUIProcessMessages(bool) {
    assert(g_procui_initialized && message_index < messages.size());
    return messages[message_index++];
}
static void MEMFreeToFrmHeap(void*, int) { assert(gpu_retired); }
static void MEMFreeToDefaultHeap(void*) { assert(gpu_retired); }
static void game_deinit() { assert(!shutdowns); }
static void KPADShutdown() {}
static void AXQuit() {}
static void WHBLogCafeDeinit() {}
static void WHBLogUdpDeinit() {}
static void OSBlockThreadsOnExit() {}
static void test_exit(int) { throw 1; }
''' + bodies + r'''
static void gfx_wiiu_shutdown_texture_cache() {
    // The actual barrier is the important platform dependency here.
    gfx_gx2_reclaim_barrier();
}
static void gfx_gx2_free() {
    gfx_gx2_reclaim_barrier();
    assert(gpu_retired);
    resources_freed=true;
}
static bool gfx_gx2_window_foreground_acquire_callback() {
    gfx_gx2_set_gpu_available(true);
    g_fg_heap_handle=g_mem1_heap_handle=(void*)1;
    gpu_retired=false;
    return !fail_resume;
}
''' + handlers + r'''
static void init() {
    gpu_available=is_running=g_procui_initialized=gx2_alive=true;
    gpu_retired=resources_freed=fail_resume=false;
    draws=releases=shutdowns=gpu_shutdowns=0;
    g_fg_heap_handle=g_mem1_heap_handle=g_cmd_list=g_context=(void*)1;
    messages.clear(); message_index=0;
}
int main() {
    // Direct exit: retire before GX2/ProcUI shutdown, with all heaps intact.
    init(); messages={PROCUI_STATUS_EXITING};
    try { gfx_gx2_window_handle_events(); assert(false); } catch(int) {}
    assert(draws==2 && shutdowns==1 && gpu_shutdowns==1 && releases==0);

    // Regression: HOME releases foreground, then system asks the title to exit.
    // GX2DrawDone must not be called again once resources have been released.
    init(); messages={PROCUI_STATUS_RELEASE_FOREGROUND, PROCUI_STATUS_EXITING};
    try { gfx_gx2_window_handle_events(); assert(false); } catch(int) {}
    assert(draws==1 && releases==1 && shutdowns==1 && gpu_shutdowns==0);

    // Normal HOME roundtrip restores permission to render and later exit.
    init(); messages={PROCUI_STATUS_RELEASE_FOREGROUND, PROCUI_STATUS_IN_FOREGROUND};
    gfx_gx2_window_handle_events();
    assert(draws==1 && releases==1 && gpu_available && !shutdowns);
    messages.push_back(PROCUI_STATUS_EXITING);
    try { gfx_gx2_window_handle_events(); assert(false); } catch(int) {}
    assert(draws==3 && shutdowns==1 && gpu_shutdowns==1);

    // Resume allocation failure still cleans up before shutting down ProcUI.
    init(); fail_resume=true;
    messages={PROCUI_STATUS_RELEASE_FOREGROUND, PROCUI_STATUS_IN_FOREGROUND};
    try { gfx_gx2_window_handle_events(); assert(false); } catch(int) {}
    assert(draws==3 && shutdowns==1 && gpu_shutdowns==1);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'shutdown.cpp'
            source.write_text(code)
            exe = str(Path(tmp) / 'shutdown')
            subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra',
                            str(source), '-o', exe], check=True)
            subprocess.run([exe], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
