#ifndef WIIU_MINIMAP_H
#define WIIU_MINIMAP_H

#ifdef WIIU_MINIMAP

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct WiiUMinimapFrame {
    const char *texture_path;
    uint32_t background_color;
    float mario_x;
    float mario_y;
    float mario_direction;
    bool texture_changed;
    const char *course_name;
    uint8_t course_number;
};

enum WiiUMinimapMenuScreen {
    WIIU_MINIMAP_MENU_PRESS_START,
    WIIU_MINIMAP_MENU_MIRROR_TV,
};

void wiiu_minimap_set_menu_screen(enum WiiUMinimapMenuScreen screen);
enum WiiUMinimapMenuScreen wiiu_minimap_get_menu_screen(void);

bool wiiu_minimap_get_frame(struct WiiUMinimapFrame *frame);
size_t wiiu_minimap_get_preload_paths(const char *const **paths);
void wiiu_minimap_reset(void);

#ifdef __cplusplus
}
#endif

#endif

#endif
