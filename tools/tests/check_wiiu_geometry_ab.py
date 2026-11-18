#!/usr/bin/env python3
"""Check the actual A/B controller's clocks, four blocks and sample exclusion."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TEST = r'''
#include <assert.h>
#include <stdio.h>
#include "pc/gfx/wiiu_geometry_ab.h"

int main(void) {
    struct WiiUGeometryAb s = {0};
    unsigned starts=0, switches=0, completions=0;
    unsigned measured[4]={0};
    // Continuous pair clock: the comparison must finish at 3 + 4*20 seconds.
    for(uint64_t now=0;now<=84000000;now+=20000) {
        enum WiiUAbEvent event=wiiu_geometry_ab_step(&s,now,true,false);
        if(event==WIIU_AB_START) { ++starts; assert(now==3000000); }
        if(event==WIIU_AB_NEXT) { ++switches; assert(now==3000000+s.block*20000000ULL); }
        if(event==WIIU_AB_DONE) { ++completions; assert(now==83000000); }
        if(s.running) {
            assert(wiiu_geometry_ab_alternate(&s)==((s.block&1)!=0));
            bool sample=wiiu_geometry_ab_measure(&s,now);
            assert(sample==(now-s.block_us>=1000000));
            if(sample) ++measured[s.block];
        } else {
            assert(wiiu_geometry_ab_alternate(&s));
            assert(!wiiu_geometry_ab_measure(&s,now));
        }
    }
    assert(starts==1 && switches==3 && completions==1);
    for(unsigned i=0;i<4;i++) assert(measured[i]==950);
    // A completed run stays stopped until the scene changes.
    for(uint64_t now=84020000;now<85000000;now+=20000)
        assert(wiiu_geometry_ab_step(&s,now,true,false)==WIIU_AB_NONE);
    assert(wiiu_geometry_ab_step(&s,85000000,true,true)==WIIU_AB_RESET);
    assert(!s.running && !s.complete && s.block==0);
    // Settle again, then interrupt a running test by moving or opening a menu.
    for(uint64_t now=85020000;now<=88000000;now+=20000)
        wiiu_geometry_ab_step(&s,now,true,false);
    assert(s.running);
    assert(wiiu_geometry_ab_step(&s,88020000,false,false)==WIIU_AB_RESET);
    assert(!s.initialized && !s.running);
    for(uint64_t now=88040000;now<=91040000;now+=20000)
        wiiu_geometry_ab_step(&s,now,true,false);
    assert(s.running);
    // HOME/long stalls and backwards clocks invalidate, rather than pollute,
    // a measured block or underflow the deadline arithmetic.
    assert(wiiu_geometry_ab_step(&s,93000000,true,false)==WIIU_AB_RESET);
    for(uint64_t now=93020000;now<=96000000;now+=20000)
        wiiu_geometry_ab_step(&s,now,true,false);
    assert(s.running);
    assert(wiiu_geometry_ab_step(&s,95000000,true,false)==WIIU_AB_RESET);
    assert(!s.running && !s.complete);
    puts("PASS: four timed blocks, warmup exclusion, completion, scene/menu/HOME/clock resets");
}
'''

IDLE_STUBS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
typedef int s32;
typedef float f32;
#define TRUE 1
#define FALSE 0
#define ACT_IN_QUICKSAND 1
#define ACT_COUGHING 2
#define ACT_PANTING 3
#define ACT_SHIVERING 4
#define ACT_START_SLEEPING 5
#define INPUT_IN_POISON_GAS 1
#define TERRAIN_MASK 7
#define TERRAIN_SNOW 2
#define SURFACE_FLAG_DYNAMIC 1
#define MARIO_ANIM_STAND_AGAINST_WALL 0
#define MARIO_ANIM_IDLE_HEAD_LEFT 1
#define MARIO_ANIM_IDLE_HEAD_RIGHT 2
#define MARIO_ANIM_IDLE_HEAD_CENTER 3
struct Area { unsigned terrainType; };
struct Floor { unsigned flags; };
struct MarioState {
    float quicksandDepth,pos[3];
    unsigned input,health,actionArg,actionState,actionTimer;
    struct Area *area;
    struct Floor *floor;
};
static bool benchmark,cancel;
static int last_action,animations;
static bool gfx_profile_ab_keep_awake(void) { return benchmark; }
static s32 set_mario_action(struct MarioState *m,int action,int arg) {
    (void)m;(void)arg;last_action=action;return 1;
}
static bool check_common_idle_cancels(struct MarioState *m) { (void)m;return cancel; }
static void set_mario_animation(struct MarioState *m,int anim) { (void)m;animations|=1<<anim; }
static bool is_anim_at_end(struct MarioState *m) { (void)m;return true; }
static float find_floor_height_relative_polar(struct MarioState *m,int a,float b) { (void)m;(void)a;(void)b;return 0; }
static void stationary_ground_step(struct MarioState *m) { (void)m; }
'''

IDLE_TEST = r'''
int main(void) {
    struct Area area={0};struct Floor floor={0};
    struct MarioState m={.health=0x880,.area=&area,.floor=&floor};
    benchmark=true;
    for(unsigned i=0;i<10000;i++) {
        if(act_idle(&m)) break;
    }
#ifdef WIIU_LOAD_TIMING_PROFILE
    assert(last_action==0); // longer than the complete 80-second test
    assert((animations&14)==14); // head turns continue
    cancel=true;assert(act_idle(&m)==TRUE);cancel=false;
    m.health=0x200;assert(act_idle(&m)==TRUE && last_action==ACT_PANTING);
    m.health=0x880;last_action=0;benchmark=false;
    for(unsigned i=0;i<10000 && !last_action;i++) act_idle(&m);
#endif
    assert(last_action==ACT_START_SLEEPING); // normal idle still sleeps
    puts("PASS: benchmark stays awake, idle animations/cancels and normal sleep preserved");
}
'''


def main():
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        path = temp/"ab.c"
        path.write_text(TEST)
        exe = temp/"ab"
        subprocess.run(["cc", "-std=c99", "-O2", "-fsanitize=address,undefined", "-no-pie",
                        "-I"+str(ROOT/"src"), str(path), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
        source = (ROOT/"src/game/mario_actions_stationary.c").read_text()
        start = source.index("s32 act_idle(")
        end = source.index("\nvoid play_anim_sound", start)
        path.write_text(IDLE_STUBS + source[start:end] + IDLE_TEST)
        for flags in [[], ["-DEXTERNAL_DATA", "-DTARGET_WII_U", "-DWIIU_LOAD_TIMING_PROFILE"]]:
            subprocess.run(["cc", "-std=c99", "-O2", "-fsanitize=address,undefined", "-no-pie",
                            *flags, str(path), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
