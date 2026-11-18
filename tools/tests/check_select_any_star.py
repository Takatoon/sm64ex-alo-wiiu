"""Exercise real mission selection for all save masks, with and without cheats.

Run with Python and a host C compiler (cc), including inside the build container.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, name):
    match = re.search(r"^(?:void|s32) " + name + r"\([^\n]*\)\s*\{", source, re.M)
    assert match, name
    end = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


FIXTURE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef int8_t s8; typedef uint8_t u8; typedef int16_t s16; typedef int32_t s32;
#define UNUSED
#define FALSE 0
#define NO_SEGMENTED_MEMORY 1
#define AVOID_UB 1
#define COURSE_NUM_TO_INDEX(x) ((x)-1)
#define MODEL_STAR 1
#define MODEL_TRANSPARENT_STAR 2
#define STAR_SELECTOR_SELECTED 1
#define STAR_SELECTOR_NOT_SELECTED 0
#define MENU_SCROLL_HORIZONTAL 0
#define A_BUTTON 1
#define START_BUTTON 2
#define B_BUTTON 4
#define Z_BUTTON_DEF(x) (x)
#define SOUND_MENU_STAR_SOUND_LETS_A_GO 0
struct Object { float oStarSelectorSize; int oStarSelectorType; };
static struct Object objects[8], *sStarSelectorModels[8], *gCurrentObject;
static int object_count, models[8], coin_star, scroll_max;
static u8 saved_stars;
static s8 sLoadedActNum, sVisibleStars, sSelectedActIndex, sSelectableStarIndex;
static u8 sObtainedStars, sInitSelectedActNum, sSelectAnyStar;
static s32 sActSelectorMenuTimer;
static int gCurrSaveFileNum=1, gCurrCourseNum=1, gDialogCourseActNum;
static int bhvActSelectorStarType, gGlobalSoundSource;
static struct { bool EnableCheats, SelectAnyStar; } Cheats;
static struct { unsigned buttonPressed; } controller, *gPlayer1Controller=&controller;
static u8 save_file_get_star_flags(int file,int course) { return saved_stars; }
static u8 save_file_get_course_star_count(int file,int course) {
    return __builtin_popcount(saved_stars);
}
static struct Object *spawn_object_abs_with_rot(struct Object *parent, int unused,
        int model, int behavior, int x, int y, int z, int rx, int ry, int rz) {
    assert(object_count<8); models[object_count]=model;
    return &objects[object_count++];
}
static void render_100_coin_star(u8 stars) { coin_star=!!(stars&64); }
static void handle_menu_scrolling(int direction,s8 *value,int low,int high) {
    scroll_max=high; assert(*value>=low && *value<=high);
}
static void play_sound(int sound,int position) {}
static void area_update_objects(void) {}
'''

TEST = r'''
int main(void) {
    for (int mask=0;mask<128;mask++) {
        for (int enabled=0;enabled<2;enabled++) {
            for (int selected=0;selected<2;selected++) {
                saved_stars=mask; Cheats.EnableCheats=enabled;
                Cheats.SelectAnyStar=selected; object_count=0;
                lvl_init_act_selector_values_and_stars(0,0);
                bool active=false;
#ifdef CHEATS_ACTIONS
                active=enabled && selected;
#endif
                assert(sSelectAnyStar==active);
                assert(sObtainedStars==__builtin_popcount(mask&63));
                bhv_act_selector_init();
                assert(coin_star==!!(mask&64));
                assert(object_count==sVisibleStars);
                assert(sVisibleStars>=1 && sVisibleStars<=6);
                if (active) assert(sVisibleStars==6);
                for (int i=0;i<sVisibleStars;i++) {
                    assert(models[i]==((mask&(1<<i))?MODEL_STAR:MODEL_TRANSPARENT_STAR));
                }
                // Changing the menu option after entry must not alter this visit.
                Cheats.EnableCheats=false; Cheats.SelectAnyStar=false;
                int choices=active?6:(sObtainedStars==6?6:sObtainedStars+1);
                for (int slot=0;slot<choices;slot++) {
                    sSelectableStarIndex=slot;
                    bhv_act_selector_loop();
                    assert(scroll_max==choices-1);
                    int index=sSelectedActIndex;
                    assert(index>=0 && index<sVisibleStars);
                    if (active) assert(index==slot);
                    else assert((mask&(1<<index)) || index==sInitSelectedActNum-1);
                    sActSelectorMenuTimer=11; sLoadedActNum=0;
                    controller.buttonPressed=A_BUTTON;
                    int act=lvl_update_obj_and_load_act_button_actions(0,0);
                    int expected=index+1;
                    if (!active && expected>sInitSelectedActNum) expected=sInitSelectedActNum;
                    assert(act==expected && gDialogCourseActNum==index+1);
                    assert(saved_stars==mask);
                }
            }
        }
    }
    puts("PASS: 128 save masks, cheat toggles, six missions, coin star, snapshot and save preservation");
}
'''


def main():
    source = (ROOT / "src/menu/star_select.c").read_text()
    code = FIXTURE + "\n".join(function(source, name) for name in (
        "bhv_act_selector_init", "bhv_act_selector_loop",
        "lvl_init_act_selector_values_and_stars",
        "lvl_update_obj_and_load_act_button_actions")) + TEST
    with tempfile.TemporaryDirectory(prefix="select-any-star-") as directory:
        path = Path(directory) / "selection.c"
        path.write_text(code)
        for cheats in (False, True):
            binary = Path(directory) / ("cheats" if cheats else "normal")
            flags = ["-DCHEATS_ACTIONS=1"] if cheats else []
            subprocess.run(["cc", "-std=c99", "-O2", "-fsanitize=address,undefined",
                            "-no-pie", *flags, str(path), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=20)


if __name__ == "__main__":
    main()
