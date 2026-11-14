"""Run on the Wii U development image; checks sources without building a WUHB."""
from pathlib import Path
import shlex
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def check_map_visibility(directory):
    source = (ROOT / "src/pc/wiiu_minimap.c").read_text()
    modes = (ROOT / "src/game/level_update.h").read_text()
    mode_enum = modes[modes.index("enum PlayModes {"):]
    mode_enum = mode_enum[:mode_enum.index("};") + 2]
    helpers = source[source.index("static bool minimap_has_playable_area(void)"):
                     source.index("bool wiiu_minimap_get_frame(")]
    menu_state = source[source.index("static enum WiiUMinimapMenuScreen sMenuScreen"):
                        source.index("static float clampf(")]
    fixture = r'''
    #define TARGET_WII_U 1
    #define WIIU_MINIMAP 1
    #include <cassert>
    #include <cstddef>
    #include "src/pc/wiiu_minimap.h"
    struct Area { unsigned flags; } area;
    struct Object {} mario_object, stale_object;
    struct MarioState { Object* marioObj; } mario;
    static Area* gCurrentArea;
    static MarioState* gMarioState;
    static Object* gMarioObject;
    static void* gCurrDemoInput;
    static int sCurrPlayMode, dialog_id = -1;
    static int get_dialog_id() { return dialog_id; }
    '''
    tests = r'''
    int main() {
        assert(wiiu_minimap_get_menu_screen() == WIIU_MINIMAP_MENU_PRESS_START);
        wiiu_minimap_set_menu_screen(WIIU_MINIMAP_MENU_MIRROR_TV);
        assert(wiiu_minimap_get_menu_screen() == WIIU_MINIMAP_MENU_MIRROR_TV);
        assert(!minimap_has_playable_area());
        gCurrentArea = &area; area.flags = 1;
        gMarioState = &mario; gMarioObject = &mario_object; mario.marioObj = gMarioObject;
        assert(minimap_has_playable_area());
        gCurrDemoInput = &stale_object; // attract-mode demo is not a started game
        assert(!minimap_has_playable_area());
        gCurrDemoInput = nullptr;
        sCurrPlayMode = PLAY_MODE_NORMAL;
        assert(minimap_play_mode_visible());
        dialog_id = 17; // NPC/sign dialogue
        assert(minimap_play_mode_visible());
        sCurrPlayMode = PLAY_MODE_CHANGE_AREA; // scripted level-entry text
        assert(minimap_play_mode_visible());
        dialog_id = -1; // closing text precedes NORMAL by one presentation
        assert(minimap_play_mode_visible());
        sCurrPlayMode = PLAY_MODE_NORMAL;
        assert(minimap_play_mode_visible());
        sCurrPlayMode = PLAY_MODE_CHANGE_AREA; // subsequent unrelated transition
        assert(!minimap_play_mode_visible());
        dialog_id = 17; sCurrPlayMode = PLAY_MODE_CHANGE_LEVEL;
        assert(!minimap_play_mode_visible());
        for (int mode : {PLAY_MODE_NORMAL, PLAY_MODE_PAUSED, PLAY_MODE_FRAME_ADVANCE}) {
            sCurrPlayMode = mode;
            assert(minimap_play_mode_visible());
        }
        area.flags = 0; // act select can retain stale Mario state
        assert(!minimap_has_playable_area());
        area.flags = 1; mario.marioObj = &stale_object;
        assert(!minimap_has_playable_area());
        mario.marioObj = gMarioObject; gMarioObject = nullptr;
        assert(!minimap_has_playable_area());
        wiiu_minimap_set_menu_screen(WIIU_MINIMAP_MENU_PRESS_START);
        assert(wiiu_minimap_get_menu_screen() == WIIU_MINIMAP_MENU_PRESS_START);
    }
    '''
    cpp = Path(directory) / "visibility.cpp"
    cpp.write_text("#include <initializer_list>\n" + fixture + mode_enum + menu_state + helpers + tests)
    binary = str(Path(directory) / "visibility-test")
    subprocess.run(["g++", "-std=c++17", "-fsanitize=undefined", "-I.", str(cpp), "-o", binary],
                   cwd=ROOT, check=True)
    subprocess.run([binary], cwd=ROOT, check=True, timeout=20)
    print("PASS: map retained during dialogue, scripted intro text, stale Mario/menu state", flush=True)


def check_floor_selection(directory):
    source = (ROOT / "src/pc/wiiu_minimap.c").read_text()
    actions = (ROOT / "include/sm64.h").read_text()
    flags = "\n".join(re.search(r"^#define " + name + r"\s+.*$", actions, re.M).group()
                      for name in ("ACT_FLAG_AIR", "ACT_FLAG_SWIMMING_OR_FLYING",
                                   "ACT_FLAG_ON_POLE", "ACT_FLAG_HANGING"))
    layers = source[source.index("enum WiiUMinimapLayer {"):]
    layers = layers[:layers.index("};") + 2]
    helpers = source[source.index("static float minimap_floor_height(void)"):
                     source.index("static bool bowser_3_full_arena_active(void)")]
    fixture = r'''
    #include <cassert>
    #include <cstdint>
    #include <cstddef>
    #include <initializer_list>
    typedef uint32_t u32;
    struct WiiUMinimapInfo { int id; };
    static WiiUMinimapInfo sHmcMaps[] = {{0}, {1}, {2}, {3}};
    struct MarioState { float pos[3], floorHeight; void* floor; u32 action; } mario;
    static MarioState* gMarioState = &mario;
    static int gMarioCurrentRoom;
    enum { BBH_OUTSIDE_ROOM = 99 };
    '''
    tests = r'''
    int main() {
        int collision;
        mario.floor = &collision;
        // Same supporting floor, despite crossing a boundary twice per jump.
        for (int boundary : {-2000, 750, 2250}) {
            mario.floorHeight = boundary - 80;
            mario.action = 0;
            mario.pos[1] = mario.floorHeight;
            auto* lower = hmc_map_for_mario();
            for (int rise : {0, 40, 80, 160, 300, 160, 80, 40, 0}) {
                mario.action = rise ? ACT_FLAG_AIR : 0;
                mario.pos[1] = mario.floorHeight + rise;
                assert(hmc_map_for_mario() == lower);
            }
            // Landing upstairs or riding a lift must change the map.
            mario.floorHeight = boundary + 80;
            mario.pos[1] = mario.floorHeight;
            assert(hmc_map_for_mario()->id == lower->id + 1);
        }
        // Water, flight, poles, hanging and absent collision use actual height.
        mario.floorHeight = 0;
        mario.pos[1] = 2500;
        for (u32 action : {u32(ACT_FLAG_SWIMMING_OR_FLYING), u32(ACT_FLAG_ON_POLE),
                           u32(ACT_FLAG_HANGING)}) {
            mario.action = action;
            assert(hmc_map_for_mario()->id == 3);
        }
        mario.action = ACT_FLAG_AIR; mario.floor = nullptr;
        assert(hmc_map_for_mario()->id == 3);
        mario.floor = &collision; mario.floorHeight = -2100;
        assert(hmc_map_for_mario()->id == 0); // No stale floor from another level.

        // BBH shares the fix but retains its room-based exterior selection.
        gMarioCurrentRoom = 1;
        mario.floorHeight = 700; mario.pos[1] = 1000;
        assert(bbh_layer_for_mario() == WIIU_MINIMAP_LAYER_GROUND);
        mario.floorHeight = 900;
        assert(bbh_layer_for_mario() == WIIU_MINIMAP_LAYER_UPPER);
        gMarioCurrentRoom = BBH_OUTSIDE_ROOM;
        assert(bbh_layer_for_mario() == WIIU_MINIMAP_LAYER_OUTSIDE);
    }
    '''
    cpp = Path(directory) / "floor-selection.cpp"
    cpp.write_text(fixture + flags + "\n" + layers + helpers + tests)
    binary = str(Path(directory) / "floor-selection-test")
    subprocess.run(["g++", "-std=c++17", "-fsanitize=undefined", str(cpp), "-o", binary],
                   cwd=ROOT, check=True)
    subprocess.run([binary], cwd=ROOT, check=True, timeout=20)
    print("PASS: HMC/BBH jumps retain supporting floor; landings, lifts and suspended movement", flush=True)


def check_wdw_water_levels(directory):
    source = (ROOT / "src/pc/wiiu_minimap.c").read_text()
    selector = source[source.index("static const struct WiiUMinimapInfo *wdw_map_for_water_level(void)"):
                      source.index("static bool minimap_has_playable_area(void)")]
    fixture = r'''
    #include <cassert>
    #include <initializer_list>
    typedef int s32;
    enum { LEVEL_WDW };
    struct WiiUMinimapInfo { int id; };
    static WiiUMinimapInfo base_main{0},base_city{1},sWdwCityLowMap{2},
        sWdwWater0256Map{3},sWdwWater1024Map{4},sWdwWater1792Map{5},
        sWdwWater2560Map{6},sWdwWater2816Map{7};
    static int gCurrAreaIndex,gEnvironmentLevels[1];
    static const WiiUMinimapInfo* generated_map_for_level(int,int area) {
        return area==1 ? &base_main : &base_city;
    }
    '''
    tests = r'''
    int main() {
        gCurrAreaIndex=1;
        struct Case { int height; const WiiUMinimapInfo* map; } cases[] = {
            {31,&base_main},{40,&base_main},{256,&sWdwWater0256Map},
            {1024,&sWdwWater1024Map},{1792,&sWdwWater1792Map},
            {2560,&sWdwWater2560Map},{2816,&sWdwWater2816Map},
        };
        for (const auto& item:cases) {
            gEnvironmentLevels[0]=item.height;
            assert(wdw_map_for_water_level()==item.map);
        }
        gCurrAreaIndex=2;
        for (int height:{-3000,-2508,-2500,-1318}) {
            gEnvironmentLevels[0]=height;
            assert(wdw_map_for_water_level()==&sWdwCityLowMap);
        }
        for (int height:{-1317,-127,31,2816}) {
            gEnvironmentLevels[0]=height;
            assert(wdw_map_for_water_level()==&base_city);
        }
        // Returning to the main area must use its own initial-water state.
        gCurrAreaIndex=1;gEnvironmentLevels[0]=31;
        assert(wdw_map_for_water_level()==&base_main);
    }
    '''
    cpp = Path(directory) / "water-levels.cpp"
    cpp.write_text(fixture + selector + tests)
    binary = str(Path(directory) / "water-levels-test")
    subprocess.run(["g++", "-std=c++17", "-fsanitize=undefined", str(cpp), "-o", binary],
                   cwd=ROOT, check=True)
    subprocess.run([binary], cwd=ROOT, check=True, timeout=20)
    print("PASS: WDW initial/switch heights, main/city areas and low/high transition", flush=True)


def main():
    with tempfile.TemporaryDirectory(prefix="wiiu-gamepad-ui-") as directory:
        binary = str(Path(directory) / "ui-test")
        subprocess.run([
            "g++", "-std=c++17", "-O1", "-g", "-fsanitize=undefined",
            "-fno-omit-frame-pointer", "-I.", "tools/tests/test_wiiu_gamepad_ui.cpp",
            "src/pc/wiiu_gamepad_hud.c", "-o", binary,
        ], cwd=ROOT, check=True)
        subprocess.run([binary], cwd=ROOT, check=True, timeout=20,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        check_map_visibility(directory)
        check_floor_selection(directory)
        check_wdw_water_levels(directory)
    print("PASS: touch edges, zoom bounds, native/HD font metrics, DDS decoding", flush=True)

    targets = [
        "build/us_wiiu/src/pc/gfx/gfx_gx2_window.o",
        "build/us_wiiu/src/pc/gfx/gfx_gx2_gamepad.o",
        "build/us_wiiu/src/pc/controller/controller_wiiu.o",
        "build/us_wiiu/src/pc/wiiu_gamepad_hud.o",
        "build/us_wiiu/src/pc/wiiu_minimap.o",
        "build/us_wiiu/src/menu/title_screen.o",
        "build/us_wiiu/src/menu/file_select.o",
        "build/us_wiiu/src/menu/star_select.o",
    ]
    force_sources = []
    for target in targets:
        source = target.removeprefix("build/us_wiiu/").removesuffix(".o")
        extension = ".cpp" if source.endswith(("gfx_gx2_window", "gfx_gx2_gamepad")) else ".c"
        force_sources.extend(["-W", source + extension])
    dry = subprocess.run([
        "make", "-n", *force_sources, "TARGET_WII_U=1", "VERSION=us", "EXTERNAL_DATA=1",
        "HIGH_FPS_PC=1", "WIIU_MINIMAP=1", *targets,
    ], cwd=ROOT, check=True, capture_output=True, text=True)
    commands = []
    for line in dry.stdout.splitlines():
        if not line.startswith(("powerpc-eabi-gcc ", "powerpc-eabi-g++ ")):
            continue
        tokens = iter(shlex.split(line))
        command = []
        for token in tokens:
            if token in ("-o", "-MF"):
                next(tokens)
            elif token not in ("-c", "-MMD"):
                command.append(token)
        commands.append(command + ["-fsyntax-only"])
    if len(commands) != len(targets):
        raise RuntimeError(f"Expected {len(targets)} compiler commands, got {len(commands)}")
    for command in commands:
        subprocess.run(command, cwd=ROOT, check=True)
    window = next(cmd for cmd in commands if cmd[0].endswith("g++"))
    for excluded in ({"-DHIGH_FPS_PC"}, {"-DWIIU_MINIMAP=1", "-DEXTERNAL_DATA", "-DHIGH_FPS_PC"}):
        subprocess.run([arg for arg in window if arg not in excluded], cwd=ROOT, check=True)
    print("PASS: Wii U compiler, minimap/embedded and 30/60 FPS configurations")


if __name__ == "__main__":
    main()
