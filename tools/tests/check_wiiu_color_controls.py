#!/usr/bin/env python3
"""Exercise Minus gesture handling and the real manual triangle-capacity control functions."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
def function(source,name):
    m=re.search(r'^(?:static )?(?:void|const char \*) ?'+name+r'\([^;\n]*\)\s*\{',source,re.M)
    assert m,name
    brace=source.index('{',m.start());end=brace+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[m.start():end]

STUBS=r'''
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pc/gfx/wiiu_color_input.h"
#include "pc/gfx/wiiu_geometry_ab.h"
static bool geometry_profile_awake,geometry_ab_measure;
static unsigned geometry_buffer_choice,geometry_buffer_steps;
static unsigned buf_triangle_limit=256;
static void *gCurrentArea=(void *)1,*gMarioState=(void *)1;
static bool packed,indexed;
static unsigned resets,logs;
static float multiplier=2.0f;
static float pc_get_render_frame_multiplier(void) { return multiplier; }
static void gfx_profile_ab_clear_window(void) { ++resets; }
static void WHBLogPrintf(const char *fmt,...) { (void)fmt;++logs; }
static void gfx_gx2_set_packed_colors(bool enable) { packed=enable; }
static bool gfx_gx2_indexed(void) { return indexed; }
static void gfx_gx2_set_indexed(bool enable) { indexed=enable; }
'''
TEST=r'''
int main(void) {
 struct WiiUColorButton key={0};
 assert(!wiiu_color_button_step(&key,0,true));
 assert(!wiiu_color_button_step(&key,WIIU_COLOR_MINUS,true));
 for(int i=0;i<300;i++) assert(!wiiu_color_button_step(&key,WIIU_COLOR_MINUS,true));
 assert(wiiu_color_button_step(&key,0,true));
 assert(!wiiu_color_button_step(&key,0,true));
 for(unsigned direction=WIIU_COLOR_LEFT;direction<=WIIU_COLOR_RIGHT;direction*=2) {
  assert(!wiiu_color_button_step(&key,WIIU_COLOR_MINUS,true));
  assert(!wiiu_color_button_step(&key,WIIU_COLOR_MINUS|direction,true));
  assert(!wiiu_color_button_step(&key,WIIU_COLOR_MINUS,true));
  assert(!wiiu_color_button_step(&key,0,true));
 }
 // Simultaneous chord and disconnect never masquerade as a standalone release.
 assert(!wiiu_color_button_step(&key,WIIU_COLOR_MINUS|WIIU_COLOR_LEFT,true));
 assert(!wiiu_color_button_step(&key,0,true));
 assert(!wiiu_color_button_step(&key,WIIU_COLOR_MINUS,true));
 assert(!wiiu_color_button_step(&key,0,false));
 assert(!wiiu_color_button_step(&key,0,true));
 gfx_profile_ab_update();
 assert(packed&&indexed&&geometry_ab_measure&&geometry_profile_awake&&resets==1&&logs==1);
 assert(!strcmp(gfx_profile_geometry_indicator(),"B 256"));
 for(int i=0;i<10000;i++) gfx_profile_ab_update();
 assert(buf_triangle_limit==256&&logs==1&&resets==1);
 gfx_profile_toggle_geometry();
 assert(buf_triangle_limit==256); // queued input never changes a pending batch
 gfx_profile_ab_update();
 assert(buf_triangle_limit==512&&logs==2&&resets==2&&!strcmp(gfx_profile_geometry_indicator(),"B 512"));
 gCurrentArea=NULL;gfx_profile_ab_update();
 assert(buf_triangle_limit==512&&!geometry_profile_awake);
 gCurrentArea=(void *)1;gfx_profile_ab_update();
 assert(buf_triangle_limit==512&&geometry_profile_awake);
 gfx_profile_toggle_geometry();gfx_profile_ab_update();
 assert(buf_triangle_limit==1024&&logs==3&&!strcmp(gfx_profile_geometry_indicator(),"B 1024"));
 gfx_profile_toggle_geometry();gfx_profile_ab_update();
 assert(buf_triangle_limit==256&&logs==4);
 multiplier=1;gfx_profile_ab_update();
 assert(buf_triangle_limit==256&&logs==5&&resets==5); // separate FPS measurements
 multiplier=2;gfx_profile_ab_update();
 assert(logs==6);
 gfx_profile_toggle_geometry();gfx_profile_toggle_geometry();gfx_profile_ab_update();
 assert(buf_triangle_limit==1024&&logs==7);
 for(int i=0;i<3;i++) gfx_profile_toggle_geometry();
 gfx_profile_ab_update();assert(buf_triangle_limit==1024&&logs==7);
 assert(packed&&indexed); // neither approved optimization changes during this trial
 puts("PASS: 256/512/1024 cycle, FPS reset, queued changes, held key, chords and disconnects");
}
'''
OVERLAY=r'''
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#define TARGET_WII_U 1
#define EXTERNAL_DATA 1
#define WIIU_LOAD_TIMING_PROFILE 1
#define EXT_OPTIONS_MENU 1
#define GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(x) (x)
typedef uint64_t OSTime;typedef int16_t s16;typedef uint32_t u32;
static OSTime sFPSLastTime,sMenuFPSLastTime;
static bool configShowFPS,optmenu_open,frame_ready;
static unsigned labels,flushes;
static char selected='A',drawn;
static const char *gfx_profile_geometry_indicator(void) {return selected=='A'?"B 256":"B 1024";}
static void print_text(int x,int y,const char *s) {assert(x==22&&y==164);++labels;assert(s[0]=='B'&&s[1]==' ');drawn=s[2]=='2'?'A':'B';}
static bool draw_fps_counter(OSTime *t,s16 *value) {(void)t;if(value)*value=60;return frame_ready;}
static void render_text_labels(void) {++flushes;}
static void WHBLogPrintf(const char *s,...) {(void)s;}
'''
OVERLAY_TEST=r'''
int main(void) {
 display_fps_counter();assert(labels==1&&drawn=='A');
 optmenu_open=true;selected='B';display_fps_counter();assert(labels==1);
 display_menu_fps_counter();assert(labels==2&&drawn=='B'&&flushes==1);
 configShowFPS=true;display_menu_fps_counter();assert(labels==3&&flushes==2);
 frame_ready=true;selected='A';display_menu_fps_counter();assert(labels==4&&drawn=='A'&&flushes==3);
 puts("PASS: buffer capacity visible with FPS off and redrawn above the cached options background");
}
'''
def main():
    source=(ROOT/'src/pc/gfx/gfx_pc.c').read_text()
    names=['gfx_profile_toggle_geometry','gfx_profile_geometry_indicator','gfx_profile_ab_update']
    with tempfile.TemporaryDirectory() as tmp:
        p=Path(tmp);c=p/'controls.c';exe=p/'controls'
        c.write_text(STUBS+'\n'.join(function(source,n) for n in names)+TEST)
        subprocess.run(['cc','-std=c99','-O2','-fsanitize=address,undefined','-no-pie','-I'+str(ROOT/'src'),str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
        overlay=(ROOT/'src/game/game_init.c').read_text()
        c.write_text(OVERLAY+function(overlay,'display_fps_counter')+function(overlay,'display_menu_fps_counter')+OVERLAY_TEST)
        subprocess.run(['cc','-std=c99','-O2','-fsanitize=address,undefined','-no-pie',str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
