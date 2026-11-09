#ifdef WIIU_MINIMAP

#include <math.h>
#include <stddef.h>

#include "behavior_data.h"
#include "course_table.h"
#include "game/area.h"
#include "game/game_init.h"
#include "game/ingame_menu.h"
#include "game/level_update.h"
#include "game/memory.h"
#include "game/object_helpers.h"
#include "game/object_list_processor.h"
#include "game/save_file.h"
#include "level_table.h"
#include "object_constants.h"
#include "object_fields.h"

#include "wiiu_minimap.h"

/*
 * State selection and marker projection are adapted from Mark Street's
 * 3DS minimap prototype (mkst/sm64-port, commit 5f0d317).  The Wii U version
 * keeps the platform-neutral design but adds room/floor selection and
 * per-map bounds for larger 480x480 renders.
 */

#define WIIU_MINIMAP_SIZE 480.0f

enum WiiUMinimapLayer {
    WIIU_MINIMAP_LAYER_OUTSIDE,
    WIIU_MINIMAP_LAYER_BASEMENT,
    WIIU_MINIMAP_LAYER_GROUND,
    WIIU_MINIMAP_LAYER_UPPER,
    WIIU_MINIMAP_LAYER_ATTIC,
};

struct WiiUMinimapInfo {
    const char *texture_path;
    uint32_t background_color;
    float min_x;
    float max_x;
    float min_z;
    float max_z;
};

struct WiiUMinimapEntry {
    s16 level;
    s16 area;
    struct WiiUMinimapInfo map;
};

#include "wiiu_minimap_generated.inc.h"

static const char *const sMinimapPreloadPaths[] = {
    "gfx/textures/minimap/bbh_outside.png",
    "gfx/textures/minimap/bbh_basement.png",
    "gfx/textures/minimap/bbh_ground.png",
    "gfx/textures/minimap/bbh_upper.png",
    "gfx/textures/minimap/bbh_attic.png",
    "gfx/textures/minimap/hmc_deep.png",
    "gfx/textures/minimap/hmc_lower.png",
    "gfx/textures/minimap/hmc_middle.png",
    "gfx/textures/minimap/hmc_upper.png",
    "gfx/textures/minimap/ddd_2_submarine.png",
    "gfx/textures/minimap/jrb_1_sunken.png",
    "gfx/textures/minimap/jrb_1_surfaced.png",
    "gfx/textures/minimap/jrb_2_drained.png",
    "gfx/textures/minimap/bowser_3_full.png",
    "gfx/textures/minimap/wdw_1_0256.png",
    "gfx/textures/minimap/wdw_1_1024.png",
    "gfx/textures/minimap/wdw_1_1792.png",
    "gfx/textures/minimap/wdw_1_2560.png",
    "gfx/textures/minimap/wdw_1_2816.png",
    "gfx/textures/minimap/wdw_2.png",
    "gfx/textures/minimap/wdw_2_low.png",
};

static s16 sCurrentLevel = -1;
static s16 sCurrentArea = -1;
static s16 sCurrentPlayMode = -1;
static s16 sCurrentRoom = -1;
static s16 sCurrentLayer = -1;
static bool sCurrentMoatDrained = false;
static const struct WiiUMinimapInfo *sCurrentMap = NULL;
static const char *const sGamePadCourseNames[COURSE_MAX + 1] = {
#define _(name) name
#define COURSE_ACTS(course, name, ...) [course] = name,
#define SECRET_STAR(course, name) [course] = name,
#define CASTLE_SECRET_STARS(name)
#define EXTRA_TEXT(index, name)
#include "text/us/courses.h"
#undef EXTRA_TEXT
#undef CASTLE_SECRET_STARS
#undef SECRET_STAR
#undef COURSE_ACTS
#undef _
};

static const char *minimap_course_name(void) {
    switch (gCurrLevelNum) {
        case LEVEL_CASTLE: return "PEACH'S CASTLE";
        case LEVEL_CASTLE_GROUNDS: return "CASTLE GROUNDS";
        case LEVEL_CASTLE_COURTYARD: return "CASTLE COURTYARD";
        default: break;
    }
    if (gCurrCourseNum >= COURSE_MIN && gCurrCourseNum < COURSE_CAKE_END) {
        const char *name = sGamePadCourseNames[gCurrCourseNum];
        if (name) {
            while (*name == ' ' || (*name >= '0' && *name <= '9'))
                ++name;
            return name;
        }
    }
    return "";
}

static enum WiiUMinimapMenuScreen sMenuScreen = WIIU_MINIMAP_MENU_PRESS_START;
static bool sDialogMapActive = false;

void wiiu_minimap_set_menu_screen(enum WiiUMinimapMenuScreen screen) {
    sMenuScreen = screen;
    sDialogMapActive = false;
}

enum WiiUMinimapMenuScreen wiiu_minimap_get_menu_screen(void) {
    return sMenuScreen;
}

static float clampf(float value, float low, float high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static enum WiiUMinimapLayer bbh_layer_for_mario(void) {
    const float height = gMarioState->pos[1];

    if (gMarioCurrentRoom == BBH_OUTSIDE_ROOM)
        return WIIU_MINIMAP_LAYER_OUTSIDE;
    if (height < -750.0f)
        return WIIU_MINIMAP_LAYER_BASEMENT;
    if (height < 817.0f)
        return WIIU_MINIMAP_LAYER_GROUND;
    if (height < 1637.0f)
        return WIIU_MINIMAP_LAYER_UPPER;
    return WIIU_MINIMAP_LAYER_ATTIC;
}

static bool bowser_3_full_arena_active(void) {
    const BehaviorScript *behavior = segmented_to_virtual(bhvFallingBowserPlatform);
    struct ObjectNode *list = &gObjectLists[get_object_list_from_behavior(behavior)];
    struct Object *object = (struct Object *) list->next;
    s32 stable_platforms = 0;

    while (object != (struct Object *) list) {
        if (object->behavior == behavior &&
            object->activeFlags != ACTIVE_FLAG_DEACTIVATED &&
            object->oAction != BOWSER_BITS_PLAT_ACT_FALL) {
            stable_platforms++;
        }
        object = (struct Object *) object->header.next;
    }

    /* Ignore isolated platforms knocked loose by Bowser's earlier jumps. */
    return stable_platforms > 5;
}

static const struct WiiUMinimapInfo *generated_map_for_level(s16 level, s16 area) {
    size_t i;
    for (i = 0; i < sizeof(sGeneratedMaps) / sizeof(sGeneratedMaps[0]); ++i) {
        if (sGeneratedMaps[i].level == level && sGeneratedMaps[i].area == area)
            return &sGeneratedMaps[i].map;
    }
    return NULL;
}

static const struct WiiUMinimapInfo *wdw_map_for_water_level(void) {
    const s32 height = gEnvironmentLevels[0];

    if (gCurrAreaIndex == 2) {
        /* City's two water diamonds are at -2508 and -127. Use the closest
           state while water moves; the animated surface wave is ignored. */
        const s32 low = -2508;
        const s32 high = -127;
        return height <= low + (high - low) / 2
            ? &sWdwCityLowMap : generated_map_for_level(LEVEL_WDW, 2);
    }

    /* Choose the render closest to the continuously changing in-game water level. */
    if (height >= 2688)
        return &sWdwWater2816Map;
    if (height >= 2176)
        return &sWdwWater2560Map;
    if (height >= 1408)
        return &sWdwWater1792Map;
    if (height >= 640)
        return &sWdwWater1024Map;
    if (height >= 144)
        return &sWdwWater0256Map;
    return generated_map_for_level(LEVEL_WDW, 1);
}

static bool minimap_has_playable_area(void) {
    /* marioObj can outlive the unloaded level. Menus and act select load an
       area too, but only load_mario_area marks it as containing the player. */
    return gCurrDemoInput == NULL && gCurrentArea != NULL && (gCurrentArea->flags & 0x01) &&
           gMarioState != NULL && gMarioObject != NULL &&
           gMarioState->marioObj == gMarioObject;
}

static bool minimap_play_mode_visible(void) {
    bool dialogue = get_dialog_id() >= 0;
    if (sCurrPlayMode == PLAY_MODE_NORMAL || sCurrPlayMode == PLAY_MODE_PAUSED ||
        sCurrPlayMode == PLAY_MODE_FRAME_ADVANCE) {
        sDialogMapActive = dialogue;
        return true;
    }
    if (sCurrPlayMode == PLAY_MODE_CHANGE_AREA) {
        /* Closing the text can precede NORMAL by one presentation. Keep the
           same map until that scripted dialogue transition has finished. */
        sDialogMapActive |= dialogue;
        return sDialogMapActive;
    }
    sDialogMapActive = false;
    return false;
}

bool wiiu_minimap_get_frame(struct WiiUMinimapFrame *frame) {
    enum WiiUMinimapLayer layer = WIIU_MINIMAP_LAYER_OUTSIDE;
    const struct WiiUMinimapInfo *map;
    bool changed;
    bool moat_drained = false;

    if (frame == NULL)
        return false;
    if (!minimap_has_playable_area()) {
        sDialogMapActive = false;
        if (gCurrDemoInput != NULL)
            sMenuScreen = WIIU_MINIMAP_MENU_PRESS_START;
        return false;
    }
    /* Entering gameplay ends the idle screen. Real menus explicitly select
       their own content; dialogues keep this same playable area's map. */
    sMenuScreen = WIIU_MINIMAP_MENU_MIRROR_TV;
    if (!minimap_play_mode_visible())
        return false;

    if (gCurrLevelNum == LEVEL_BBH) {
        layer = bbh_layer_for_mario();
        map = &sBbhMaps[layer];
    } else if (gCurrLevelNum == LEVEL_HMC && gCurrAreaIndex == 1) {
        const float height = gMarioState->pos[1];
        map = &sHmcMaps[height < -2000.0f ? 0 :
                        height < 750.0f ? 1 :
                        height < 2250.0f ? 2 : 3];
    } else if (gCurrLevelNum == LEVEL_WDW &&
               (gCurrAreaIndex == 1 || gCurrAreaIndex == 2)) {
        map = wdw_map_for_water_level();
    } else if (gCurrLevelNum == LEVEL_DDD && gCurrAreaIndex == 2 &&
               !(save_file_get_flags() &
                 (SAVE_FLAG_HAVE_KEY_2 | SAVE_FLAG_UNLOCKED_UPSTAIRS_DOOR))) {
        map = &sDddSubmarineMap;
    } else if (gCurrLevelNum == LEVEL_JRB && gCurrAreaIndex == 1) {
        map = gCurrActNum == 1 ? &sJrbSunkenMap : &sJrbSurfacedMap;
    } else if (gCurrLevelNum == LEVEL_JRB && gCurrAreaIndex == 2 &&
               gMarioState->waterLevel < 500.0f) {
        map = &sJrbShipDrainedMap;
    } else if (gCurrLevelNum == LEVEL_CASTLE_GROUNDS &&
               (save_file_get_flags() & SAVE_FLAG_MOAT_DRAINED)) {
        moat_drained = true;
        map = &sCastleGroundsDrainedMap;
    } else if (gCurrLevelNum == LEVEL_BOWSER_3 &&
               bowser_3_full_arena_active()) {
        map = &sBowser3FullMap;
    } else {
        map = generated_map_for_level(gCurrLevelNum, gCurrAreaIndex);
        if (map == NULL)
            return false;
    }
    changed = sCurrentLevel != gCurrLevelNum ||
              sCurrentArea != gCurrAreaIndex ||
              sCurrentPlayMode != sCurrPlayMode ||
              sCurrentMap != map ||
              sCurrentMoatDrained != moat_drained ||
              (gCurrLevelNum == LEVEL_BBH &&
               (sCurrentRoom != gMarioCurrentRoom || sCurrentLayer != layer));

    sCurrentLevel = gCurrLevelNum;
    sCurrentArea = gCurrAreaIndex;
    sCurrentPlayMode = sCurrPlayMode;
    sCurrentRoom = gMarioCurrentRoom;
    sCurrentLayer = layer;
    sCurrentMoatDrained = moat_drained;
    sCurrentMap = map;

    frame->texture_path = map->texture_path;
    frame->background_color = map->background_color;
    frame->mario_x = clampf((gMarioState->pos[0] - map->min_x) * WIIU_MINIMAP_SIZE /
                            (map->max_x - map->min_x), 0.0f, WIIU_MINIMAP_SIZE - 1.0f);
    /* The Blender camera used for these assets maps increasing Z downward. */
    frame->mario_y = clampf((gMarioState->pos[2] - map->min_z) * WIIU_MINIMAP_SIZE /
                            (map->max_z - map->min_z), 0.0f, WIIU_MINIMAP_SIZE - 1.0f);
    frame->mario_direction = gMarioState->faceAngle[1] * (6.28318530717958647692f / 65536.0f);
    frame->texture_changed = changed;
    frame->course_name = minimap_course_name();
    frame->course_number = gCurrCourseNum >= COURSE_MIN && gCurrCourseNum <= COURSE_STAGES_MAX
        ? gCurrCourseNum : 0;
    return true;
}

size_t wiiu_minimap_get_preload_paths(const char *const **paths) {
    if (paths != NULL)
        *paths = sMinimapPreloadPaths;
    return sizeof(sMinimapPreloadPaths) / sizeof(sMinimapPreloadPaths[0]);
}

void wiiu_minimap_reset(void) {
    sDialogMapActive = false;
    sCurrentLevel = -1;
    sCurrentArea = -1;
    sCurrentPlayMode = -1;
    sCurrentRoom = -1;
    sCurrentLayer = -1;
    sCurrentMoatDrained = false;
    sCurrentMap = NULL;
}

#endif
