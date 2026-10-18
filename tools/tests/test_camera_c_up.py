"""Compile the actual C-Up entry function against a small native harness."""
import pathlib
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class CameraCUpTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('gcc'), 'Native gcc is required (available in the build container)')
    def test_ttm_restriction_does_not_lock_mario(self):
        source = (ROOT / 'src/game/camera.c').read_text()
        # Exercise the production function, not a copied implementation.
        function = re.search(r'^s32 set_mode_c_up\(struct Camera \*c\) \{.*?^\}',
                             source, re.M | re.S).group()
        harness = r'''
#include <assert.h>
typedef int s32;
struct Camera { int unused; };
enum { AREA_TTM_OUTSIDE = 577, CAM_MOVE_C_UP_MODE = 1, CAM_SOUND_C_UP_PLAYED = 2 };
int gCurrLevelArea, gCameraMovementFlags, sCameraSoundFlags;
int stored, buzzed;
void store_lakitu_cam_info_for_c_up(struct Camera *c) { (void)c; stored++; }
void play_sound_button_change_blocked(void) { buzzed++; }
FUNCTION
int main(void) {
    struct Camera camera = {0};
    gCurrLevelArea = AREA_TTM_OUTSIDE;
    gCameraMovementFlags = 0x40;
    sCameraSoundFlags = CAM_SOUND_C_UP_PLAYED;
#if CAMERA_VANILLA_DEFINES
    assert(set_mode_c_up(&camera) == 0);
    assert(gCameraMovementFlags == 0x40);
    assert(sCameraSoundFlags == CAM_SOUND_C_UP_PLAYED);
    assert(stored == 0 && buzzed == 1);
#else
    assert(set_mode_c_up(&camera) == 1);
    assert(gCameraMovementFlags == (0x40 | CAM_MOVE_C_UP_MODE));
#endif
    stored = buzzed = 0;
    gCameraMovementFlags = 0x40;
    sCameraSoundFlags = CAM_SOUND_C_UP_PLAYED;
    gCurrLevelArea = 1;
    assert(set_mode_c_up(&camera) == 1);
    assert(gCameraMovementFlags == (0x40 | CAM_MOVE_C_UP_MODE));
    assert(sCameraSoundFlags == 0);
    assert(stored == 1 && buzzed == 0);
    assert(set_mode_c_up(&camera) == 0);
    assert(stored == 1);
    return 0;
}
'''.replace('FUNCTION', function)
        (ROOT / 'build').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            folder = pathlib.Path(temporary)
            c_file = folder / 'camera.c'
            c_file.write_text(harness)
            for vanilla in (0, 1):
                with self.subTest(vanilla_checks=vanilla):
                    executable = folder / ('camera-test.exe' if os.name == 'nt' else 'camera-test')
                    subprocess.run(['gcc', '-std=c99', '-Wall', '-Werror',
                                    f'-DCAMERA_VANILLA_DEFINES={vanilla}', str(c_file),
                                    '-o', str(executable)], check=True, capture_output=True)
                    subprocess.run([str(executable)], check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
