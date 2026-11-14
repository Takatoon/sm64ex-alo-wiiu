"""Compare the real GamePad blitter with the preceding pixel arithmetic."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(source, name):
    start = source.index("static void " + name + "(")
    brace = source.index("{", start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    source = (ROOT / "src/pc/gfx/gfx_gx2_window.cpp").read_text()
    code = r'''
    #include <cassert>
    #include <cstdint>
    #include <cstring>
    #include <vector>
    #include <algorithm>
    #include <cstdio>
    static const uint32_t DRC_WIDTH = 854, DRC_HEIGHT = 480;
    struct WiiUMapRect { int x,y,width,height; };
    struct WiiUMinimapTextureCache { const char* path; uint8_t* pixels; int width,height; };
    static uint8_t* gfx_gx2_drc_pixel(uint8_t* buffer, int x, int y) {
        return buffer + ((size_t)y * 864 + x) * 4;
    }
    static uint32_t state = 1234567;
    static uint32_t random32() {
        state = state * 1664525u + 1013904223u;
        return state;
    }
    '''
    code += function(source, "gfx_gx2_drc_blend") + "\n"
    code += function(source, "gfx_gx2_blit_drc_image") + "\n"
    code += r'''
    static void reference(uint8_t* target, const WiiUMinimapTextureCache* texture,
                          int x, int y, int width, int height, const WiiUMapRect& clip,
                          unsigned opacity, bool red_coin) {
        if (!texture || width <= 0 || height <= 0) return;
        for (int dy = 0; dy < height; ++dy) {
            int py = y + dy;
            if (py < clip.y || py >= clip.y + clip.height || py < 0 || py >= int(DRC_HEIGHT)) continue;
            int sy = dy * texture->height / height;
            for (int dx = 0; dx < width; ++dx) {
                int px = x + dx;
                if (px < clip.x || px >= clip.x + clip.width || px < 0 || px >= int(DRC_WIDTH)) continue;
                int sx = dx * texture->width / width;
                auto* src = texture->pixels + (size_t(sy)*texture->width + sx)*4;
                if (src[3] == 0) continue;
                auto* dst = gfx_gx2_drc_pixel(target, px, py);
                unsigned a = src[3] * opacity / 255;
                dst[0] = (src[0]*a + dst[0]*(255-a))/255;
                dst[1] = ((red_coin ? 0 : src[1])*a + dst[1]*(255-a))/255;
                dst[2] = ((red_coin ? 0 : src[2])*a + dst[2]*(255-a))/255;
                dst[3] = a + dst[3]*(255-a)/255;
            }
        }
    }
    int main() {
        const size_t size = size_t(864)*DRC_HEIGHT*4;
        std::vector<uint8_t> expected(size), actual(size);
        for (int test = 0; test < 256; ++test) {
            int sw = (test & 1) ? 960 : 480;
            int sh = (test & 2) ? 960 : 64;
            std::vector<uint8_t> pixels(size_t(sw)*sh*4);
            for (size_t i=0; i<pixels.size(); ++i) pixels[i]=random32()>>24;
            for (size_t i=3; i<pixels.size(); i+=16) pixels[i]=255;
            for (size_t i=7; i<pixels.size(); i+=16) pixels[i]=0;
            WiiUMinimapTextureCache texture{nullptr,pixels.data(),sw,sh};
            for (auto& byte:actual) byte=random32()>>24;
            expected=actual;
            int x=int(random32()%1000)-200, y=int(random32()%600)-100;
            int w=int(random32()%1100)-10, h=int(random32()%550)-10;
            WiiUMapRect clip = (test%3==0) ? WiiUMapRect{0,0,854,480}
                : (test%3==1) ? WiiUMapRect{30,50,392,360} : WiiUMapRect{-20,-20,1000,600};
            unsigned opacity=(test%4==0) ? 0 : (test%4==1) ? 255 : test;
            bool red_coin = test&4;
            auto* input = test==255 ? nullptr : &texture;
            reference(expected.data(),input,x,y,w,h,clip,opacity,red_coin);
            gfx_gx2_blit_drc_image(actual.data(),input,x,y,w,h,clip,opacity,red_coin);
            assert(actual==expected); // Includes untouched rows/pitch padding.
        }
        puts("PASS: 256 native/HD blits, clipping, transparent/opaque/partial alpha, red coins, pitch padding");
    }
    '''
    with tempfile.TemporaryDirectory(prefix="wiiu-drc-blit-") as directory:
        cpp = Path(directory) / "blit.cpp"
        cpp.write_text(code)
        binary = str(Path(directory) / "blit-test")
        subprocess.run(["g++", "-std=c++17", "-O2", "-fsanitize=address,undefined",
                        "-no-pie", str(cpp), "-o", binary], check=True)
        subprocess.run([binary], check=True, timeout=45)


if __name__ == "__main__":
    main()
