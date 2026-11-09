#if defined(WAPI_GX2) && defined(WIIU_MINIMAP)
#include <malloc.h>
#include <string.h>
#include <gx2/context.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/state.h>
#include <gx2/texture.h>
#include <whb/log.h>
#include "gfx_gx2_gamepad.h"
#include "gx2_shader_gen.h"

static GX2ContextState *sContext;
static ShaderGroup sShader;
static bool sAttempted, sReady, sShaderValid;
static GX2ColorBuffer sOutput;
static GX2Texture sMap;
static GX2Sampler sSampler;
static float *sVertices;
static unsigned sSamplerLocation;
static uint32_t sGeneration;

void gfx_gx2_gamepad_free(void) {
    free(sOutput.surface.image);
    free(sMap.surface.image);
    free(sContext);
    free(sVertices);
    if (sShaderValid) gx2FreeShaderGroup(&sShader);
    sOutput = {}; sMap = {}; sShader = {};
    sContext = nullptr; sVertices = nullptr;
    sReady = sShaderValid = sAttempted = false;
    sGeneration = 0;
}

bool gfx_gx2_gamepad_ready(void) {
    if (sAttempted) return sReady;
    sAttempted = true;
    CCFeatures features;
    const uint32_t shader = SHADER_OPT_ALPHA | (SHADER_TEXEL0 << 9) | (SHADER_TEXEL0 << 21);
    gfx_cc_get_features(shader, &features);
    if (gx2GenerateShaderGroup(&sShader, &features) != 0) goto failed;
    sShaderValid = true;
    if (sShader.numAttributes != 2) goto failed;
    for (unsigned i = 0; i < sShader.pixelShader.samplerVarCount; ++i)
        if (!strcmp(sShader.pixelShader.samplerVars[i].name, "uTex0"))
            sSamplerLocation = sShader.pixelShader.samplerVars[i].location;

    sContext = (GX2ContextState*)memalign(GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState));
    sVertices = (float*)memalign(0x40, 12 * 8 * sizeof(float));
    if (!sContext || !sVertices) goto failed;
    GX2SetupContextStateEx(sContext, FALSE);
    GX2InitSampler(&sSampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_POINT);

    sOutput.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    sOutput.surface.width = 854;
    sOutput.surface.height = 480;
    sOutput.surface.depth = sOutput.surface.mipLevels = 1;
    sOutput.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    sOutput.surface.aa = GX2_AA_MODE1X;
    sOutput.surface.use = GX2_SURFACE_USE_TEXTURE_COLOR_BUFFER_TV;
    sOutput.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
    sOutput.viewNumSlices = 1;
    GX2CalcSurfaceSizeAndAlignment(&sOutput.surface);
    sOutput.surface.image = memalign(sOutput.surface.alignment, sOutput.surface.imageSize);
    if (!sOutput.surface.image) goto failed;
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU, sOutput.surface.image, sOutput.surface.imageSize);
    GX2InitColorBufferRegs(&sOutput);

    sMap.surface = sOutput.surface;
    sMap.surface.width = sMap.surface.height = 480;
    sMap.surface.use = GX2_SURFACE_USE_TEXTURE;
    sMap.surface.image = nullptr;
    sMap.surface.pitch = sMap.surface.imageSize = sMap.surface.alignment = 0;
    GX2CalcSurfaceSizeAndAlignment(&sMap.surface);
    sMap.surface.image = memalign(sMap.surface.alignment, sMap.surface.imageSize);
    if (!sMap.surface.image) goto failed;
    sMap.viewNumMips = sMap.viewNumSlices = 1;
    sMap.compMap = 0x00010203;
    GX2InitTextureRegs(&sMap);
    sReady = true;
    WHBLogPrintf("[gamepad-zoom] GPU ready: map=%u output=%u bytes",
                 sMap.surface.imageSize, sOutput.surface.imageSize);
    return true;

failed:
    gfx_gx2_gamepad_free();
    sAttempted = true;
    WHBLogPrint("[gamepad-zoom] GPU unavailable; retaining CPU fallback");
    return false;
}

static void set_quad(float *vertices, const WiiUMapRect& rect, const WiiUMapView& view, bool map) {
    const float left = rect.x * 2.0f / 854.0f - 1.0f;
    const float right = (rect.x + rect.width) * 2.0f / 854.0f - 1.0f;
    const float top = 1.0f - rect.y * 2.0f / 480.0f;
    const float bottom = 1.0f - (rect.y + rect.height) * 2.0f / 480.0f;
    const float u = map ? view.x / 480.0f : 0;
    const float v = map ? view.y / 480.0f : 0;
    const float extent = map ? 1.0f / view.scale : 1.0f;
    const float quad[6][8] = {
        {left,top,0,1,u,v,0,0}, {right,top,0,1,u+extent,v,0,0},
        {left,bottom,0,1,u,v+extent,0,0}, {left,bottom,0,1,u,v+extent,0,0},
        {right,top,0,1,u+extent,v,0,0}, {right,bottom,0,1,u+extent,v+extent,0,0},
    };
    memcpy(vertices, quad, sizeof(quad));
}

GX2ColorBuffer *gfx_gx2_gamepad_compose(const GX2ColorBuffer *overlay,
    const uint8_t *map, uint32_t generation, const WiiUMapView& view, const WiiUMapRect& map_rect) {
    if (!sReady || !overlay || !map) return nullptr;
    if (generation != sGeneration) {
        for (unsigned y = 0; y < 480; ++y)
            memcpy((uint8_t*)sMap.surface.image + y * sMap.surface.pitch * 4,
                   map + y * 480 * 4, 480 * 4);
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, sMap.surface.image, sMap.surface.imageSize);
        sGeneration = generation;
        WHBLogPrintf("[gamepad-zoom] map uploaded generation=%u", generation);
    }
    GX2Texture ui = {};
    ui.surface = overlay->surface;
    ui.viewNumMips = ui.viewNumSlices = 1;
    ui.compMap = 0x00010203;
    GX2InitTextureRegs(&ui);
    /* The compositor already published only its dirty rows. This invalidates
     * GPU texture caches without writing back the complete CPU surface. */
    GX2Invalidate(GX2_INVALIDATE_MODE_TEXTURE, ui.surface.image, ui.surface.imageSize);
    set_quad(sVertices, map_rect, view, true);
    set_quad(sVertices + 6 * 8, {0, 0, 854, 480}, view, false);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, sVertices, 12 * 8 * sizeof(float));

    /* A separate context keeps shaders, blending, viewport and depth state
     * out of the game's state cache. The window restores its context below. */
    GX2SetContextState(sContext);
    GX2SetColorBuffer(&sOutput, GX2_RENDER_TARGET_0);
    GX2SetViewport(0, 0, 854, 480, 0, 1);
    GX2SetScissor(0, 0, 854, 480);
    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
    GX2SetFetchShader(&sShader.fetchShader);
    GX2SetVertexShader(&sShader.vertexShader);
    GX2SetPixelShader(&sShader.pixelShader);
    GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
    GX2SetAlphaTest(FALSE, GX2_COMPARE_FUNC_ALWAYS, 0);
    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
    const GX2ChannelMask none = static_cast<GX2ChannelMask>(0);
    GX2SetTargetChannelMasks(static_cast<GX2ChannelMask>(0xf), none, none, none,
                             none, none, none, none);
    GX2SetPixelSampler(&sSampler, sSamplerLocation);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0, FALSE, TRUE);
    GX2SetPixelTexture(&sMap, sSamplerLocation);
    GX2SetAttribBuffer(0, 6 * 8 * sizeof(float), 8 * sizeof(float), sVertices);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 6, 0, 1);

    /* UI pixels are premultiplied by the CPU compositor. */
    GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_INV_SRC_ALPHA,
                      GX2_BLEND_COMBINE_MODE_ADD, FALSE, GX2_BLEND_MODE_ONE,
                      GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 1, FALSE, TRUE);
    GX2SetPixelTexture(&ui, sSamplerLocation);
    GX2SetAttribBuffer(0, 6 * 8 * sizeof(float), 8 * sizeof(float), sVertices + 6 * 8);
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 6, 0, 1);
    GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER, sOutput.surface.image, sOutput.surface.imageSize);
    GX2Flush();
    return &sOutput;
}
#endif
