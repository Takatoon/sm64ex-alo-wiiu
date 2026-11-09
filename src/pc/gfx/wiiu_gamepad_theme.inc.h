#include "wiiu_gamepad_nunito.inc.h"

static const char* const g_drc_theme_font = "gfx/textures/minimap/theme/nunito-bold.png";
static const char* const g_drc_red_coin_texture = "gfx/actors/coin/coin_front.ia16.png";

static bool gfx_gx2_framed_layout_available(void)
{
    const auto* background = gfx_gx2_load_minimap_texture(g_drc_theme_background);
    const auto* font = gfx_gx2_load_minimap_texture(g_drc_theme_font);
    return background && background->width == DRC_WIDTH && background->height == DRC_HEIGHT &&
           font && font->width == NUNITO_COLUMNS * NUNITO_CELL_WIDTH &&
           font->height == 6 * NUNITO_CELL_HEIGHT &&
           gfx_gx2_load_minimap_texture(g_drc_theme_frame);
}

static unsigned gfx_gx2_nunito_glyph(unsigned char c)
{
    return (c >= 32 && c <= 126 ? c : '?') - 32;
}

static int gfx_gx2_nunito_width(const char* text)
{
    int width = 0;
    for (; *text; ++text)
        width += g_nunito_advance[gfx_gx2_nunito_glyph(*text)];
    return width;
}

static void gfx_gx2_draw_nunito(const char* text, float x, int baseline, float scale,
                                unsigned color, const WiiUMapRect& clip)
{
    const auto* atlas = gfx_gx2_load_minimap_texture(g_drc_theme_font);
    const int width = (int)ceilf(NUNITO_CELL_WIDTH * scale);
    const int height = (int)ceilf(NUNITO_CELL_HEIGHT * scale);
    const int top = baseline - (int)roundf(29 * scale);
    auto* target = (uint8_t*)g_drc_panel_image_data;
    for (; *text; ++text) {
        const unsigned glyph = gfx_gx2_nunito_glyph(*text);
        const int src_x = (glyph % NUNITO_COLUMNS) * NUNITO_CELL_WIDTH;
        const int src_y = (glyph / NUNITO_COLUMNS) * NUNITO_CELL_HEIGHT;
        const int left = (int)roundf(x - 2 * scale);
        for (int y = 0; y < height; ++y) {
            const uint8_t* source = atlas->pixels +
                ((src_y + y * NUNITO_CELL_HEIGHT / height) * atlas->width + src_x) * 4;
            for (int column = 0; column < width; ++column) {
                const unsigned alpha = source[(column * NUNITO_CELL_WIDTH / width) * 4 + 3];
                if (alpha && gfx_gx2_drc_contains(clip, left + column, top + y))
                    gfx_gx2_drc_blend(gfx_gx2_drc_pixel(target, left + column, top + y),
                                      color >> 16, color >> 8, color, alpha);
            }
        }
        x += g_nunito_advance[glyph] * scale;
    }
}

static void gfx_gx2_draw_drc_theme_labels(const WiiUMinimapFrame& map, int music)
{
    static const char* last_course;
    static unsigned last_course_number;
    static int last_music;
    if (!g_drc_theme_labels_valid || last_course != map.course_name ||
        last_course_number != map.course_number) {
        const WiiUMapRect header = {0, 0, DRC_WIDTH, 35};
        gfx_gx2_restore_drc_rect(header);
        int title_x = 26;
        if (map.course_number) {
            const char number[] = {char('0' + map.course_number / 10),
                                   char('0' + map.course_number % 10), '\0'};
            gfx_gx2_draw_drc_texture("gfx/textures/minimap/theme/placa-curso.png", 24, 4, 48, 26);
            gfx_gx2_draw_nunito(number, 48 - gfx_gx2_nunito_width(number) * 0.4f, 25,
                                0.8f, 0xFFFFFF, header);
            title_x = 82;
        }
        if (map.course_name && *map.course_name) {
            const int width = gfx_gx2_nunito_width(map.course_name);
            const float scale = fminf(1.0f, (DRC_WIDTH - title_x - 24.0f) / width);
            gfx_gx2_draw_nunito(map.course_name, title_x + 2, 29, scale, 0x7B8291, header);
            gfx_gx2_draw_nunito(map.course_name, title_x, 27, scale, 0x122D53, header);
        }
        gfx_gx2_drc_flush_rect(0, 0, DRC_WIDTH - 1, 34);
        last_course = map.course_name;
        last_course_number = map.course_number;
    }
    if (!g_drc_theme_labels_valid || last_music != music) {
        const WiiUMapRect footer = {0, 454, DRC_WIDTH, 26};
        gfx_gx2_restore_drc_rect(footer);
        for (const auto& title : g_music_titles) {
            if (title.sequence != music)
                continue;
            gfx_gx2_draw_drc_texture("gfx/textures/minimap/theme/nota-musical.png", 29, 460, 17, 16);
            gfx_gx2_draw_nunito(title.text, 53, 474, 0.57f, 0x7B8291, footer);
            gfx_gx2_draw_nunito(title.text, 52, 473, 0.57f, 0x122D53, footer);
            break;
        }
        gfx_gx2_drc_flush_rect(0, footer.y, DRC_WIDTH - 1, DRC_HEIGHT - 1);
        last_music = music;
    }
    g_drc_theme_labels_valid = true;
}

static void gfx_gx2_draw_drc_theme_status(const WiiUMinimapFrame& map)
{
    WiiUGamePadHudFrame frame = {};
    wiiu_gamepad_hud_get_frame(&frame);
    gfx_gx2_draw_drc_theme_labels(map, frame.music_valid ? frame.music_id : -1);

    const int collected = frame.red_coins < 0 ? 0 : frame.red_coins > 8 ? 8 : frame.red_coins;
    const bool timer_active = frame.flags & WIIU_GAMEPAD_HUD_TIMER;
    const int counter = timer_active ? 9 + frame.timer / 3 : collected;
    if (counter == g_drc_theme_counter)
        return;
    const WiiUMapRect strip = {510, 393, 314, 34};
    gfx_gx2_restore_drc_rect(strip);
    if (timer_active) {
        gfx_gx2_draw_drc_timer(frame.timer);
    } else {
        const auto* coin = gfx_gx2_load_minimap_texture(g_drc_red_coin_texture);
        for (int i = 0; i < 8; ++i)
            gfx_gx2_blit_drc_image((uint8_t*)g_drc_panel_image_data, coin, 512 + i * 39, 393,
                                   32, 32, strip, i < collected ? 255 : 80, true);
    }
    gfx_gx2_drc_flush_rect(strip.x, strip.y, strip.x + strip.width - 1, strip.y + strip.height - 1);
    g_drc_theme_counter = counter;
}
