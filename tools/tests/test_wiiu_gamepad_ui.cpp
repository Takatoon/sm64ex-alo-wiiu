#include <cassert>
#include <cstring>
#include <vector>
#include "src/pc/wiiu_gamepad_layout.h"
#include "src/pc/wiiu_gamepad_hud.h"
#include "src/pc/gfx/wiiu_gamepad_font.h"
#include "src/pc/gfx/wiiu_dds_rgba.h"

static void put32(std::vector<uint8_t>& bytes, int offset, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes[offset + i] = value >> (i * 8);
}

static std::vector<uint8_t> dds(bool bc3, int width = 4, int height = 4) {
    std::vector<uint8_t> bytes(128 + (bc3 ? 16 : 8));
    memcpy(bytes.data(), "DDS ", 4);
    put32(bytes, 4, 124); put32(bytes, 12, height); put32(bytes, 16, width);
    put32(bytes, 76, 32); put32(bytes, 80, 4);
    memcpy(bytes.data() + 84, bc3 ? "DXT5" : "DXT1", 4);
    return bytes;
}

int main() {
    // Touch is edge-triggered; no reactivation when a press has been consumed.
    wiiu_gamepad_set_touch(true);
    assert(wiiu_gamepad_take_touch_press());
    for (int i = 0; i < 100; ++i) {
        wiiu_gamepad_set_touch(true);
        assert(!wiiu_gamepad_take_touch_press());
    }
    wiiu_gamepad_set_touch(false); wiiu_gamepad_set_touch(true);
    assert(wiiu_gamepad_take_touch_press());
    wiiu_gamepad_set_touch(false);

    // Full/zoomed views and map edges: crop and projected marker stay in range.
    for (int y = 0; y < 480; ++y) for (int x = 0; x < 480; ++x) {
        auto view = wiiu_map_view(true, x, y);
        assert(view.x >= 0 && view.x <= 240 && view.y >= 0 && view.y <= 240);
        assert((x - view.x) * view.scale >= 0 && (x - view.x) * view.scale < 480);
        assert((y - view.y) * view.scale >= 0 && (y - view.y) * view.scale < 480);
    }
    auto center = wiiu_map_view(true, 240, 240);
    assert(center.x == 120 && center.y == 120 && center.scale == 2);
    auto full = wiiu_map_view(false, 479, 479);
    assert(full.x == 0 && full.y == 0 && full.scale == 1);

    // Visible bounds and spacing are resolution-independent, including an HD
    // glyph with asymmetric transparent padding and a differently colored tip.
    for (int hd : {1, 4}) {
        int width = 16 * hd, height = 8 * hd;
        std::vector<uint8_t> glyph(width * height * 4);
        for (int y = hd; y < 6 * hd; ++y) for (int x = 2 * hd; x < 14 * hd; ++x) {
            uint8_t* p = glyph.data() + (y * width + x) * 4;
            p[0] = 100; p[1] = 150; p[2] = 200; p[3] = 255;
        }
        auto bounds = wiiu_gamepad_glyph_bounds(glyph.data(), width, height);
        assert(bounds.width == 5);
        std::vector<uint8_t> title(480 * 56 * 4);
        int draw_width = bounds.width * 3, start = wiiu_map_title_x(draw_width);
        wiiu_gamepad_draw_glyph(title.data(), 480, 56, glyph.data(), width, height,
                               bounds, start, 4, draw_width, 48, false);
        int right = -1;
        for (int y = 0; y < 56; ++y) for (int x = 0; x < 480; ++x)
            if (title[(y * 480 + x) * 4 + 3]) right = x > right ? x : right;
        assert(right == 467); // twelve pixels between ink and the map edge
    }
    for (float width : {30.0f, 150.0f, 500.0f}) {
        int scaled = (int)ceilf(width * wiiu_map_title_scale(width, 16));
        assert(scaled <= 456 && wiiu_map_title_x(scaled) >= 12);
        assert(wiiu_map_title_x(scaled) + scaled == 468);
    }

    // BC1 transparency/color interpolation, BC3 alpha modes, clipped edge
    // blocks, RGBA8 and malformed payloads. No GPU or endian assumptions.
    auto bytes = dds(false);
    bytes[128] = 0; bytes[129] = 0xf8; // red, endpoint 0 > black
    int w, h;
    auto pixels = wiiu_dds_decode_rgba(bytes.data(), bytes.size(), &w, &h);
    assert(pixels && w == 4 && h == 4 && pixels[0] == 255 && pixels[3] == 255);
    free(pixels);
    bytes[128] = bytes[129] = 0;
    bytes[130] = 255; bytes[131] = 255;
    put32(bytes, 132, 0xffffffff);
    pixels = wiiu_dds_decode_rgba(bytes.data(), bytes.size(), &w, &h);
    assert(pixels && pixels[3] == 0); free(pixels);
    bytes = dds(true, 3, 2);
    bytes[128] = 200; bytes[129] = 60;
    bytes[130] = 2; // first pixel selects 6/7 * a0 + 1/7 * a1
    bytes[136] = 0; bytes[137] = 0xf8;
    pixels = wiiu_dds_decode_rgba(bytes.data(), bytes.size(), &w, &h);
    assert(pixels && w == 3 && h == 2 && pixels[0] == 255 && pixels[3] == 180);
    free(pixels);
    bytes[128] = 0; bytes[129] = 100; bytes[130] = 7;
    pixels = wiiu_dds_decode_rgba(bytes.data(), bytes.size(), &w, &h);
    assert(pixels && pixels[3] == 255); free(pixels);
    bytes.resize(128 + 4 * 4 * 4);
    put32(bytes, 12, 4); put32(bytes, 16, 4);
    put32(bytes, 8, 8); put32(bytes, 20, 16); put32(bytes, 80, 0x41);
    put32(bytes, 84, 0); put32(bytes, 88, 32);
    put32(bytes, 92, 255); put32(bytes, 96, 0xff00);
    put32(bytes, 100, 0xff0000); put32(bytes, 104, 0xff000000);
    pixels = wiiu_dds_decode_rgba(bytes.data(), bytes.size(), &w, &h);
    assert(pixels && !memcmp(pixels, bytes.data() + 128, 64)); free(pixels);
    assert(!wiiu_dds_decode_rgba(bytes.data(), bytes.size() - 1, &w, &h));
    bytes[0] = 'X';
    assert(!wiiu_dds_decode_rgba(bytes.data(), bytes.size(), &w, &h));
}
