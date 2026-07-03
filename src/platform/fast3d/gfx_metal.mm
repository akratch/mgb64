/*
 * gfx_metal.mm — Native Metal rendering backend for macOS (opt-in; GL default).
 *
 * Implements the fast3d GfxRenderingAPI vtable (from Emill/n64-fast3d-engine,
 * MIT — see gfx_rendering_api.h) using Apple Metal directly, so screen-space
 * effects (SSAO / depth reconstruction) run natively instead of through Apple's
 * deprecated OpenGL-over-Metal translator, which op-hangs on that math.
 *
 * Structural reference: the libultraship (Kenix3, MIT) Metal backend. This file
 * is authored fresh against this port's 22-fn vtable; only the idioms are shared.
 * The combiner->MSL translator retargets THIS port's own GLSL string-builder
 * (gfx_opengl.c:722-1449) to emit MSL, with LUS p_shader_item_to_str as a 1:1
 * correctness oracle.
 *
 * Implemented (Phases 1-3): device/queue/CAMetalLayer bring-up + clear/present;
 * combiner -> MSL translation + per-combiner MTLLibrary/MTLRenderPipelineState;
 * offscreen scene targets (color + Depth32Float); texture upload; blend/depth/
 * sampler baked into cached PSOs; deferred-state draw flush over a ring vertex
 * arena; the RDP-memory / coverage-memory XLU framebuffer snapshot-and-resume;
 * and synchronous CPU readback (screenshots/probes).
 * The Phase 4/5 SSAO + output-VI-filter hook builds on the offscreen scene
 * color/depth targets and the snapshot machinery (see mtl_ensure_targets /
 * mtl_open_scene_encoder below).
 * ARC-managed (CMake sets -fobjc-arc for this TU).
 */

/* This project ships include/math.h — an N64 native-port shim (guard
 * _MATH_EXT_H_) that sits on the -I path ahead of the SDK and therefore SHADOWS
 * the system <math.h>. It lacks float_t/double_t, so the legacy Carbon <fp.h>
 * pulled in transitively by the Metal/QuartzCore umbrellas fails to compile
 * ("unknown type name 'double_t'"). fp.h is deprecated ("Use math.h instead")
 * and nothing in the Metal path needs it, so we suppress it via its own include
 * guard — LOCAL to this TU, so the shared shim (and the byte-identical C TUs)
 * are untouched. */
#define __FP__  /* skip <CoreServices/.../CarbonCore/fp.h> */

#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dispatch/dispatch.h>

/* gfx_cc.c / gfx_rendering_api are C TUs — force C linkage so the mangled
 * references from this ObjC++ TU resolve to the unmangled C symbols. */
extern "C" {
#include "gfx_cc.h"
#include "gfx_rendering_api.h"
#include "gfx_screen_config.h"
}

/* Growable text buffer. STL (<string>/<vector>) is unusable in this TU because
 * the project's include/math.h shim shadows the system <math.h>, which breaks
 * libc++'s <cmath> (pulled transitively by <string>). So the shader-source
 * builder uses C buffers, exactly like the GL backend (gfx_opengl.c). */
struct TB {
    char *p;
    size_t len;
    size_t cap;
};
static void tb_init(TB *b, size_t cap) {
    b->p = (char *)malloc(cap);
    b->len = 0;
    /* cap stays 0 on OOM so tb_str/tb_fmt no-op (their `len+n+1 > cap` guard
     * rejects everything) rather than dereferencing a NULL buffer; the empty
     * source then routes into the controlled MSL-compile abort. */
    b->cap = b->p ? cap : 0;
    if (b->p) b->p[0] = '\0';
}
static void tb_free(TB *b) { free(b->p); b->p = NULL; }
static void tb_str(TB *b, const char *s) {
    size_t n = strlen(s);
    if (b->len + n + 1 > b->cap) return; /* generous caps; never expected to hit */
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
}
static void tb_fmt(TB *b, const char *fmt, ...) {
    va_list a;
    va_start(a, fmt);
    int n = vsnprintf(b->p + b->len, b->cap - b->len, fmt, a);
    va_end(a);
    if (n > 0 && (size_t)n < b->cap - b->len) b->len += (size_t)n;
}

extern "C" void *platformGetMetalLayer(void);  /* platform_sdl.c */

/* Set once by whichever backend inits; the CPU clipper + shader gen read it.
 * Metal init forces it true (native depth-clamp), so the z*=0.3 hack is dropped
 * and the CPU vertex/clip pipeline is byte-identical to GL. */
extern "C" {
    extern bool g_depth_clamp_enabled;
    /* Minimal re-declarations (avoid pulling gfx_pc.h -> ultra64.h into this TU).
     * Layout must match src/platform/gfx_pc.h and src/vi.h. */
    struct GfxDimensions { uint32_t width, height; float aspect_ratio; };
    extern struct GfxDimensions gfx_current_dimensions;
    short viGetX(void);
    short viGetY(void);
    /* Output-VI-filter (Phase 4) config — defined in platform_sdl.c / gfx_pc.c.
     * Types match the extern declarations in gfx_opengl.c:38-74. */
    extern int   g_pcRemasterFX, g_pcBloom, g_pcFxaa, g_pcTonemap, g_pcGradePresets, g_pcOutputDither;
    extern float g_pcVideoGamma, g_pcVideoSaturation, g_pcVideoContrast, g_pcVideoBrightness;
    extern float g_pcVignette, g_pcBloomThreshold, g_pcBloomIntensity, g_pcSharpen;
    extern float g_pcGradeLevelSat, g_pcGradeLevelCon, g_pcGradeLevelTintR, g_pcGradeLevelTintG, g_pcGradeLevelTintB;
    /* SSAO (Phase 5). proj_a/b are the scene-projection coefficients the frontend
     * (gfx_sp_matrix) raises each scene frame; proj_b == 0 gates SSAO off (menu). */
    extern int   g_pcSsao;
    extern float g_pcSsaoRadius, g_pcSsaoIntensity;
    extern float g_pc_ssao_proj_a, g_pc_ssao_proj_b;
    extern int   g_pc_view_inv_valid;  /* W1.E2.T1 view-inverse capture latch (reset per frame) */
}

/* N64 tile wrap flags (PR/gbi.h) — declared locally to avoid the N64 header. */
#define GE007_G_TX_MIRROR 0x1
#define GE007_G_TX_CLAMP  0x2

/* Device/queue/layer live for the process; per-frame drawable & command buffer
 * are strong statics so ARC keeps them alive across the start_frame/end_frame
 * boundary (they are created in start_frame and presented in end_frame). */
static id<MTLDevice>         s_device   = nil;
static id<MTLCommandQueue>   s_queue    = nil;
static CAMetalLayer         *s_layer    = nil;
static double s_clear_r = 0.0, s_clear_g = 0.0, s_clear_b = 0.0;
static id<CAMetalDrawable>   s_drawable = nil;
static id<MTLCommandBuffer>  s_cmdbuf   = nil;
static bool s_logged_first_frame = false;

/* ==========================================================================
 * Phase 2 — combiner -> MSL shader translation
 * ========================================================================== */

/* One vertex attribute in the interleaved buf_vbo layout. size/offset in floats.
 * The order these are pushed mirrors gfx_pc.c:18390-18540 == the GL generator's
 * glGetAttribLocation push order (gfx_opengl.c:1349-1397), so the Metal vertex
 * descriptor and the frontend's VBO packing cannot drift. */
struct MtlAttr {
    int index;   /* [[attribute(index)]] */
    int size;    /* 1, 2, 3, or 4 floats */
    int offset;  /* float offset within the vertex */
};

struct MetalShader {
    uint64_t id0;
    uint32_t id1;
    struct CCFeatures cc;
    __strong id<MTLLibrary> library;
    __strong id<MTLFunction> vtxFn;
    __strong id<MTLFunction> fragFn;
    __strong MTLVertexDescriptor *vtxDesc;   /* built lazily in Phase 2.2 */
    __strong NSMutableDictionary<NSNumber *, id<MTLRenderPipelineState>> *psoCache; /* 2.2 */
    int numFloats;
    int numInputs;
    bool usedTextures[2];
    bool diagRdpMemory;
    bool diagRdpCvgMemory;
    MtlAttr attrs[32];
    int numAttrs;
};

/* Heap-allocated, never moved — ARC manages the __strong members. The pointer
 * array grows on demand (realloc) so lookups always hit and no compiled shader
 * is ever dropped/recompiled (unlike a fixed cap). Lives for the process. */
static MetalShader **s_shaders = nullptr;
static int s_shader_count = 0;
static int s_shader_cap = 0;
static MetalShader *s_cur_shader = nullptr;

/* Diagnostic MSL dump (mirrors GL's [SHADER_n] dump at gfx_opengl.c:1290). */
static int s_shader_dump_checked = -1;
static bool mtl_dump_shaders(void) {
    if (s_shader_dump_checked < 0) {
        s_shader_dump_checked = getenv("GE007_METAL_DUMP_SHADERS") ? 1 : 0;
    }
    return s_shader_dump_checked > 0;
}

/* MSL vocabulary port of shader_item_to_str (gfx_opengl.c:731-790). Same case
 * structure; GLSL -> MSL: vec4->float4, texture varyings referenced as locals
 * (aliased at fragment-body top), gl_FragCoord.xy -> fragCoord (GL-oriented,
 * flipped from Metal top-left), window_height -> u.winH, frame_count ->
 * u.frameCount. .rgb/.a swizzles are valid MSL. */
static const char *msl_item(uint32_t item, bool with_alpha, bool only_alpha,
                            bool inputs_have_alpha, bool hint_single_element) {
    if (!only_alpha) {
        switch (item) {
            case SHADER_0:       return with_alpha ? "float4(0.0)" : "float3(0.0)";
            case SHADER_INPUT_1: return with_alpha || !inputs_have_alpha ? "vInput1" : "vInput1.rgb";
            case SHADER_INPUT_2: return with_alpha || !inputs_have_alpha ? "vInput2" : "vInput2.rgb";
            case SHADER_INPUT_3: return with_alpha || !inputs_have_alpha ? "vInput3" : "vInput3.rgb";
            case SHADER_INPUT_4: return with_alpha || !inputs_have_alpha ? "vInput4" : "vInput4.rgb";
            case SHADER_INPUT_5: return with_alpha || !inputs_have_alpha ? "vInput5" : "vInput5.rgb";
            case SHADER_INPUT_6: return with_alpha || !inputs_have_alpha ? "vInput6" : "vInput6.rgb";
            case SHADER_INPUT_7: return with_alpha || !inputs_have_alpha ? "vInput7" : "vInput7.rgb";
            case SHADER_TEXEL0:  return with_alpha ? "texVal0" : "texVal0.rgb";
            case SHADER_TEXEL0A: return hint_single_element ? "texVal0.a" :
                (with_alpha ? "float4(texVal0.a)" : "float3(texVal0.a)");
            case SHADER_TEXEL1:  return with_alpha ? "texVal1" : "texVal1.rgb";
            case SHADER_TEXEL1A: return hint_single_element ? "texVal1.a" :
                (with_alpha ? "float4(texVal1.a)" : "float3(texVal1.a)");
            case SHADER_1:       return with_alpha ? "float4(1.0)" : "float3(1.0)";
            case SHADER_COMBINED: return with_alpha ? "texel" : "texel.rgb";
            case SHADER_NOISE:   return with_alpha ?
                "float4(random(float3(floor(fragCoord * (240.0 / float(u.winH))), float(u.frameCount))))" :
                "float3(random(float3(floor(fragCoord * (240.0 / float(u.winH))), float(u.frameCount))))";
        }
    } else {
        switch (item) {
            case SHADER_0:       return "0.0";
            case SHADER_INPUT_1: return "vInput1.a";
            case SHADER_INPUT_2: return "vInput2.a";
            case SHADER_INPUT_3: return "vInput3.a";
            case SHADER_INPUT_4: return "vInput4.a";
            case SHADER_INPUT_5: return "vInput5.a";
            case SHADER_INPUT_6: return "vInput6.a";
            case SHADER_INPUT_7: return "vInput7.a";
            case SHADER_TEXEL0:  return "texVal0.a";
            case SHADER_TEXEL0A: return "texVal0.a";
            case SHADER_TEXEL1:  return "texVal1.a";
            case SHADER_TEXEL1A: return "texVal1.a";
            case SHADER_1:       return "1.0";
            case SHADER_COMBINED: return "texel.a";
            case SHADER_NOISE:   return "random(float3(floor(fragCoord * (240.0 / float(u.winH))), float(u.frameCount)))";
        }
    }
    return "0.0";
}

/* MSL port of append_formula (gfx_opengl.c:792-817) — identical arithmetic. */
static void msl_formula(TB *s, uint8_t c[2][4], bool do_single,
                        bool do_multiply, bool do_mix, bool with_alpha,
                        bool only_alpha, bool opt_alpha) {
    if (do_single) {
        tb_str(s, msl_item(c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false));
    } else if (do_multiply) {
        tb_str(s, msl_item(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        tb_str(s, " * ");
        tb_str(s, msl_item(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
    } else if (do_mix) {
        tb_str(s, "mix(");
        tb_str(s, msl_item(c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false));
        tb_str(s, ", ");
        tb_str(s, msl_item(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        tb_str(s, ", ");
        tb_str(s, msl_item(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
        tb_str(s, ")");
    } else {
        tb_str(s, "(");
        tb_str(s, msl_item(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        tb_str(s, " - ");
        tb_str(s, msl_item(c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false));
        tb_str(s, ") * ");
        tb_str(s, msl_item(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
        tb_str(s, " + ");
        tb_str(s, msl_item(c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false));
    }
}

/* W1.E2.T3 validation latch: GE007_WORLD_POS_DIAG makes fragmentMain visualize the
 * interpolated world position (fract(vWorldPos*0.01)) — mirrors the GL diag so the
 * pattern can be compared GL vs Metal. Run-constant. */
static int mtl_world_pos_diag_enabled(void) {
    static int v = -1;
    if (v < 0) v = (getenv("GE007_WORLD_POS_DIAG") != NULL);
    return v;
}

/* Build the full MSL translation unit (vertexMain + fragmentMain) for a
 * combiner into `out`. Mirrors gfx_opengl_create_and_load_new_shader:823-1284
 * block for block. Also fills ms->attrs / numFloats. */
static void mtl_generate_msl(MetalShader *ms, TB *s) {
    struct CCFeatures &cc = ms->cc;

    const char *input_interp  = cc.noperspective_inputs ? "[[center_no_perspective]] " : "";
    const char *texc_interp   = cc.noperspective_texcoords ? "[[center_no_perspective]] " : "";
    const char *fog_interp    = cc.noperspective_fog ? "[[center_no_perspective]] " : "";
    bool uses_tile_mask =
        cc.tile_mask[0][0] || cc.tile_mask[0][1] ||
        cc.tile_mask[1][0] || cc.tile_mask[1][1];

    bool needs_noise = cc.opt_noise;
    for (int ci = 0; ci < 2 && !needs_noise; ci++)
        for (int cj = 0; cj < 2 && !needs_noise; cj++)
            for (int ck = 0; ck < 4 && !needs_noise; ck++)
                if (cc.c[ci][cj][ck] == SHADER_NOISE) needs_noise = true;

    bool uses_fragcoord = needs_noise ||
        (cc.opt_alpha && (cc.diag_rdp_memory_blend || cc.diag_rdp_cvg_memory_blend)) ||
        (cc.opt_alpha && cc.opt_noise) ||
        (cc.opt_alpha && cc.diag_xlu_coverage_wrap_thin);

    /* ---- header ---- */
    tb_str(s, "#include <metal_stdlib>\n");
    tb_str(s, "using namespace metal;\n");

    /* Uniform block — layout must match the C Uniforms struct uploaded in 3.1.
     * Ordered largest-alignment-first for a clean 16-byte stride. */
    tb_str(s, "struct Uniforms {\n");
    tb_str(s, "    float4 diagViewport;\n");   /* offset 0  */
    tb_str(s, "    float2 n64FilterScale;\n"); /* offset 16 */
    tb_str(s, "    float2 diagFbOrigin;\n");   /* offset 24 */
    tb_str(s, "    int frameCount;\n");        /* offset 32 */
    tb_str(s, "    int winH;\n");              /* offset 36 (viewport height, noise scale) */
    tb_str(s, "    int fbH;\n");               /* offset 40 (attachment height, fragcoord flip) */
    tb_str(s, "    int _pad;\n");              /* offset 44 -> size 48 */
    tb_str(s, "};\n");

    /* ---- helper functions (file scope, dependency order) ---- */
    tb_str(s, "static float glslMod(float x, float y) { return x - y * floor(x / y); }\n");

    if (uses_tile_mask || cc.n64_filter[0] || cc.n64_filter[1]) {
        tb_str(s, "static float n64TileMaskAxis(float texelCoord, float maskPeriod) {\n");
        tb_str(s, "    float extent = abs(maskPeriod);\n");
        tb_str(s, "    if (extent <= 0.5) return texelCoord;\n");
        tb_str(s, "    float coord = glslMod(texelCoord, maskPeriod < 0.0 ? extent * 2.0 : extent);\n");
        tb_str(s, "    if (maskPeriod < 0.0 && coord >= extent) coord = extent * 2.0 - coord;\n");
        tb_str(s, "    return coord;\n");
        tb_str(s, "}\n");
        tb_str(s, "static float2 n64TileMaskUv(float2 uv, float2 texSize, float maskS, float maskT) {\n");
        tb_str(s, "    float2 texelCoord = uv * texSize;\n");
        tb_str(s, "    texelCoord.x = n64TileMaskAxis(texelCoord.x, maskS);\n");
        tb_str(s, "    texelCoord.y = n64TileMaskAxis(texelCoord.y, maskT);\n");
        tb_str(s, "    return texelCoord / texSize;\n");
        tb_str(s, "}\n");
    }

    if (cc.n64_filter[0] || cc.n64_filter[1]) {
        bool always_3point = cc.n64_filter_always_3point;
        /* nearest_threshold: GL derives this from diag env knobs
         * (gfx_diag_n64_filter_nearest_threshold). Default is 1.0 for all live
         * paths; the env overrides are diag-only and out of Metal scope. */
        float nearest_threshold = 1.0f;
        tb_str(s, "static float4 n64TextureFilter(texture2d<float> tex, sampler smp, float2 uv, float maskS, float maskT, float2 filterScale) {\n");
        tb_str(s, "    float2 texSize = float2(tex.get_width(), tex.get_height());\n");
        tb_str(s, "    float2 texelCoord = uv * texSize;\n");
        if (!always_3point) {
            tb_str(s, "    float2 dx = dfdx(texelCoord) * filterScale.x;\n");
            tb_str(s, "    float2 dy = dfdy(texelCoord) * filterScale.y;\n");
            tb_str(s, "    float2 footprint = max(abs(dx), abs(dy));\n");
            tb_fmt(s, "    if (max(footprint.x, footprint.y) < %.9f) {\n", nearest_threshold);
            tb_str(s, "        return tex.sample(smp, n64TileMaskUv((floor(texelCoord) + float2(0.5)) / texSize, texSize, maskS, maskT), level(0.0));\n");
            tb_str(s, "    }\n");
        }
        tb_str(s, "    float2 offset = fract(uv * texSize - float2(0.5));\n");
        tb_str(s, "    offset -= step(1.0, offset.x + offset.y);\n");
        tb_str(s, "    float2 baseUv = uv - offset / texSize;\n");
        tb_str(s, "    float4 c0 = tex.sample(smp, n64TileMaskUv(baseUv, texSize, maskS, maskT), level(0.0));\n");
        tb_str(s, "    float4 c1 = tex.sample(smp, n64TileMaskUv(baseUv + float2(sign(offset.x), 0.0) / texSize, texSize, maskS, maskT), level(0.0));\n");
        tb_str(s, "    float4 c2 = tex.sample(smp, n64TileMaskUv(baseUv + float2(0.0, sign(offset.y)) / texSize, texSize, maskS, maskT), level(0.0));\n");
        tb_str(s, "    return c0 + abs(offset.x) * (c1 - c0) + abs(offset.y) * (c2 - c0);\n");
        tb_str(s, "}\n");
    }

    if (needs_noise) {
        tb_str(s, "static float random(float3 value) {\n");
        tb_str(s, "    float r = dot(sin(value), float3(12.9898, 78.233, 37.719));\n");
        tb_str(s, "    return fract(sin(r) * 143758.5453);\n");
        tb_str(s, "}\n");
    }

    if (cc.opt_alpha && cc.diag_rdp_cvg_memory_blend) {
        tb_str(s, "static float diagEdge(float2 a, float2 b, float2 p) {\n");
        tb_str(s, "    return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);\n");
        tb_str(s, "}\n");
        tb_str(s, "static float diagInsideTri(float2 p, float2 a, float2 b, float2 c) {\n");
        tb_str(s, "    float e0 = diagEdge(a, b, p);\n");
        tb_str(s, "    float e1 = diagEdge(b, c, p);\n");
        tb_str(s, "    float e2 = diagEdge(c, a, p);\n");
        tb_str(s, "    bool hasNeg = (e0 < 0.0) || (e1 < 0.0) || (e2 < 0.0);\n");
        tb_str(s, "    bool hasPos = (e0 > 0.0) || (e1 > 0.0) || (e2 > 0.0);\n");
        tb_str(s, "    return (hasNeg && hasPos) ? 0.0 : 1.0;\n");
        tb_str(s, "}\n");
        tb_str(s, "static float diagCoverageSample(float2 fragCoord, float4 diagViewport, float2 pixelOffset, float2 a, float2 b, float2 c) {\n");
        tb_str(s, "    float2 p = ((fragCoord + pixelOffset - diagViewport.xy) / diagViewport.zw) * 2.0 - 1.0;\n");
        tb_str(s, "    return diagInsideTri(p, a, b, c);\n");
        tb_str(s, "}\n");
    }

    /* ---- VertexIn / VertexOut structs + attribute walk ---- */
    ms->numAttrs = 0;
    int attr_idx = 0;
    int foff = 0;
    TB vin, vout, vbody;
    tb_init(&vin, 8192);
    tb_init(&vout, 8192);
    tb_init(&vbody, 8192);
    tb_str(&vin, "struct VertexIn {\n");
    tb_str(&vout, "struct VertexOut {\n    float4 position [[position]];\n");
    tb_str(&vbody, "vertex VertexOut vertexMain(VertexIn in [[stage_in]]) {\n    VertexOut out;\n");

    auto add_attr = [&](const char *type, const char *name, int size) {
        tb_fmt(&vin, "    %s %s [[attribute(%d)]];\n", type, name, attr_idx);
        if (ms->numAttrs < 32) {
            ms->attrs[ms->numAttrs].index = attr_idx;
            ms->attrs[ms->numAttrs].size = size;
            ms->attrs[ms->numAttrs].offset = foff;
            ms->numAttrs++;
        }
        attr_idx++;
        foff += size;
    };

    add_attr("float4", "aVtxPos", 4);
    if (cc.opt_alpha && cc.diag_rdp_cvg_memory_blend) {
        add_attr("float4", "aDiagTri01", 4);
        add_attr("float2", "aDiagTri2", 2);
        tb_str(&vout, "    float4 vDiagTri01 [[center_no_perspective]];\n");
        tb_str(&vout, "    float2 vDiagTri2 [[center_no_perspective]];\n");
        tb_str(&vbody, "    out.vDiagTri01 = in.aDiagTri01;\n");
        tb_str(&vbody, "    out.vDiagTri2 = in.aDiagTri2;\n");
    }
    /* W1.E2.T3: world-space position attribute at the same ordinal as the VBO pack
     * (after pos/diag, before texcoords). Default perspective interpolation (no
     * [[center_no_perspective]]) — world position must interpolate perspective-correct. */
    if (cc.opt_world_pos) {
        add_attr("float3", "aWorldPos", 3);
        tb_str(&vout, "    float3 vWorldPos;\n");
        tb_str(&vbody, "    out.vWorldPos = in.aWorldPos;\n");
    }
    for (int i = 0; i < 2; i++) {
        if (!cc.used_textures[i]) continue;
        char nm[24];
        snprintf(nm, sizeof nm, "aTexCoord%d", i); add_attr("float2", nm, 2);
        tb_fmt(&vout, "    %sfloat2 vTexCoord%d;\n", texc_interp, i);
        tb_fmt(&vbody, "    out.vTexCoord%d = in.aTexCoord%d;\n", i, i);
        for (int axis = 0; axis < 2; axis++) {
            char ax = axis == 0 ? 'S' : 'T';
            if (cc.clamp[i][axis]) {
                snprintf(nm, sizeof nm, "aTexClamp%c%d", ax, i); add_attr("float", nm, 1);
                tb_fmt(&vout, "    %sfloat vTexClamp%c%d;\n", texc_interp, ax, i);
                tb_fmt(&vbody, "    out.vTexClamp%c%d = in.aTexClamp%c%d;\n", ax, i, ax, i);
            }
            if (cc.tile_mask[i][axis]) {
                snprintf(nm, sizeof nm, "aTexMask%c%d", ax, i); add_attr("float", nm, 1);
                tb_fmt(&vout, "    %sfloat vTexMask%c%d;\n", texc_interp, ax, i);
                tb_fmt(&vbody, "    out.vTexMask%c%d = in.aTexMask%c%d;\n", ax, i, ax, i);
            }
        }
    }
    if (cc.opt_fog) {
        add_attr("float4", "aFog", 4);
        tb_fmt(&vout, "    %sfloat4 vFog;\n", fog_interp);
        tb_str(&vbody, "    out.vFog = in.aFog;\n");
    }
    for (int i = 0; i < cc.num_inputs; i++) {
        char nm[24];
        const char *ty = cc.opt_alpha ? "float4" : "float3";
        snprintf(nm, sizeof nm, "aInput%d", i + 1); add_attr(ty, nm, cc.opt_alpha ? 4 : 3);
        tb_fmt(&vout, "    %s%s vInput%d;\n", input_interp, ty, i + 1);
        tb_fmt(&vbody, "    out.vInput%d = in.aInput%d;\n", i + 1, i + 1);
    }
    tb_str(&vin, "};\n");
    tb_str(&vout, "};\n");
    /* g_depth_clamp_enabled is forced true on Metal, so the GL z*=0.3 spreading
     * hack is dropped: pass clip position through unchanged. */
    tb_str(&vbody, "    out.position = in.aVtxPos;\n    return out;\n}\n");

    ms->numFloats = foff;

    tb_str(s, vin.p);
    tb_str(s, vout.p);
    tb_str(s, vbody.p);
    tb_free(&vin);
    tb_free(&vout);
    tb_free(&vbody);

    /* ---- fragmentMain ---- */
    tb_str(s, "fragment float4 fragmentMain(VertexOut in [[stage_in]],\n");
    tb_str(s, "                            constant Uniforms& u [[buffer(1)]]");
    if (cc.used_textures[0])
        tb_str(s, ",\n                            texture2d<float> uTex0 [[texture(0)]], sampler smp0 [[sampler(0)]]");
    if (cc.used_textures[1])
        tb_str(s, ",\n                            texture2d<float> uTex1 [[texture(1)]], sampler smp1 [[sampler(1)]]");
    if (cc.opt_alpha && (cc.diag_rdp_memory_blend || cc.diag_rdp_cvg_memory_blend))
        tb_str(s, ",\n                            texture2d<float> uDiagFramebuffer [[texture(2)]], sampler smpDiag [[sampler(2)]]");
    tb_str(s, ") {\n");

    /* Alias varyings into locals so the ported body reads exactly like the GLSL
     * (only vocabulary differs, not name-scoping). */
    for (int i = 0; i < cc.num_inputs; i++)
        tb_fmt(s, "    %s vInput%d = in.vInput%d;\n", cc.opt_alpha ? "float4" : "float3", i + 1, i + 1);
    for (int i = 0; i < 2; i++) {
        if (!cc.used_textures[i]) continue;
        tb_fmt(s, "    float2 vTexCoord%d = in.vTexCoord%d;\n", i, i);
        for (int axis = 0; axis < 2; axis++) {
            char ax = axis == 0 ? 'S' : 'T';
            if (cc.clamp[i][axis])     tb_fmt(s, "    float vTexClamp%c%d = in.vTexClamp%c%d;\n", ax, i, ax, i);
            if (cc.tile_mask[i][axis]) tb_fmt(s, "    float vTexMask%c%d = in.vTexMask%c%d;\n", ax, i, ax, i);
        }
    }
    if (cc.opt_fog) tb_str(s, "    float4 vFog = in.vFog;\n");
    if (cc.opt_alpha && cc.diag_rdp_cvg_memory_blend) {
        tb_str(s, "    float4 vDiagTri01 = in.vDiagTri01;\n");
        tb_str(s, "    float2 vDiagTri2 = in.vDiagTri2;\n");
    }
    if (uses_fragcoord)
        tb_str(s, "    float2 fragCoord = float2(in.position.x, float(u.fbH) - in.position.y);\n");

    /* Shader-side UV clamping (gfx_opengl.c:1079-1107). .s/.t -> .x/.y. */
    for (int i = 0; i < 2; i++) {
        if (!cc.used_textures[i]) continue;
        tb_fmt(s, "    float2 sampleTexCoord%d = vTexCoord%d;\n", i, i);
        if (cc.clamp[i][0] || cc.clamp[i][1] || cc.tile_mask[i][0] || cc.tile_mask[i][1])
            tb_fmt(s, "    float2 texSize%d = float2(uTex%d.get_width(), uTex%d.get_height());\n", i, i, i);
        if (cc.clamp[i][0] && cc.clamp[i][1]) {
            tb_fmt(s, "    sampleTexCoord%d = clamp(vTexCoord%d, 0.5 / texSize%d, float2(vTexClampS%d, vTexClampT%d));\n", i, i, i, i, i);
        } else if (cc.clamp[i][0]) {
            tb_fmt(s, "    sampleTexCoord%d.x = clamp(vTexCoord%d.x, 0.5 / texSize%d.x, vTexClampS%d);\n", i, i, i, i);
        } else if (cc.clamp[i][1]) {
            tb_fmt(s, "    sampleTexCoord%d.y = clamp(vTexCoord%d.y, 0.5 / texSize%d.y, vTexClampT%d);\n", i, i, i, i);
        }
    }

    for (int i = 0; i < 2; i++) {
        if (!cc.used_textures[i]) continue;
        const char *mask_s = cc.tile_mask[i][0] ? (i == 0 ? "vTexMaskS0" : "vTexMaskS1") : "0.0";
        const char *mask_t = cc.tile_mask[i][1] ? (i == 0 ? "vTexMaskT0" : "vTexMaskT1") : "0.0";
        const char *tx = i == 0 ? "uTex0" : "uTex1";
        const char *sm = i == 0 ? "smp0" : "smp1";
        if (cc.n64_filter[i]) {
            tb_fmt(s, "    float4 texVal%d = n64TextureFilter(%s, %s, sampleTexCoord%d, %s, %s, u.n64FilterScale);\n",
                   i, tx, sm, i, mask_s, mask_t);
        } else if (cc.tile_mask[i][0] || cc.tile_mask[i][1]) {
            tb_fmt(s, "    float4 texVal%d = %s.sample(%s, n64TileMaskUv(sampleTexCoord%d, texSize%d, %s, %s));\n",
                   i, tx, sm, i, i, mask_s, mask_t);
        } else {
            tb_fmt(s, "    float4 texVal%d = %s.sample(%s, sampleTexCoord%d);\n", i, tx, sm, i);
        }
    }

    if (cc.opt_alpha && cc.diag_alpha_from_tex_intensity) {
        /* GL mixes toward tex intensity with a diag knob (default 1.0). */
        float mix = 1.0f;
        if (cc.used_textures[0]) tb_fmt(s, "    texVal0.a = mix(texVal0.a, texVal0.r, %.9g);\n", mix);
        if (cc.used_textures[1]) tb_fmt(s, "    texVal1.a = mix(texVal1.a, texVal1.r, %.9g);\n", mix);
    }

    tb_str(s, cc.opt_alpha ? "    float4 texel;\n" : "    float3 texel;\n");
    int num_cycles = cc.opt_2cyc ? 2 : 1;
    for (int cyc = 0; cyc < num_cycles; cyc++) {
        tb_str(s, "    texel = ");
        if (!cc.color_alpha_same[cyc] && cc.opt_alpha) {
            tb_str(s, "float4(");
            msl_formula(s, cc.c[cyc], cc.do_single[cyc][0], cc.do_multiply[cyc][0], cc.do_mix[cyc][0], false, false, true);
            tb_str(s, ", ");
            msl_formula(s, cc.c[cyc], cc.do_single[cyc][1], cc.do_multiply[cyc][1], cc.do_mix[cyc][1], true, true, true);
            tb_str(s, ")");
        } else {
            msl_formula(s, cc.c[cyc], cc.do_single[cyc][0], cc.do_multiply[cyc][0], cc.do_mix[cyc][0],
                        cc.opt_alpha, false, cc.opt_alpha);
        }
        tb_str(s, ";\n");
        if (cyc == 0 && num_cycles == 2)
            tb_str(s, "    texel = clamp(texel, -1.01, 1.01);\n");
    }
    tb_str(s, "    texel = clamp(texel, 0.0, 1.0);\n");

    if (cc.opt_fog) {
        if (cc.opt_alpha)
            tb_str(s, "    texel = float4(mix(texel.rgb, vFog.rgb, vFog.a), texel.a);\n");
        else
            tb_str(s, "    texel = mix(texel, vFog.rgb, vFog.a);\n");
    }

    if (cc.opt_texture_edge && cc.opt_alpha)
        tb_str(s, "    if (texel.a > 0.19) { texel.a = 1.0; } else { discard_fragment(); return float4(0.0); }\n");

    if (cc.opt_alpha && cc.opt_noise)
        tb_str(s, "    texel.a *= floor(random(float3(floor(fragCoord * (240.0 / float(u.winH))), float(u.frameCount))) + 0.5);\n");

    /* diag_color_scale / diag_alpha_scale: GL reads a settex knob (default ~1.0).
     * Live scale is 1.0 unless the diag env is set (out of Metal scope). */
    if (cc.diag_color_scale) {
        if (cc.opt_alpha) tb_str(s, "    texel = float4(clamp(texel.rgb * 1.02, 0.0, 1.0), texel.a);\n");
        else              tb_str(s, "    texel = clamp(texel * 1.02, 0.0, 1.0);\n");
    }
    if (cc.opt_alpha && cc.diag_alpha_scale)
        tb_str(s, "    texel.a = clamp(texel.a * 1.0, 0.0, 1.0);\n");
    if (cc.opt_alpha && cc.room_water_alpha_suppress)
        tb_str(s, "    texel.a = 0.0;\n");

    if (cc.opt_alpha && cc.diag_xlu_coverage_wrap_thin) {
        float rate = 0.25f;
        tb_fmt(s, "    float coverageWrapHash = fract(sin(dot(floor(fragCoord), float2(12.9898, 78.233))) * 43758.5453);\n"
                  "    if (coverageWrapHash >= %.9f) { discard_fragment(); return float4(0.0); }\n", rate);
    }

    if (cc.opt_alpha && cc.diag_rdp_cvg_memory_blend) {
        /* memoryUv samples the framebuffer SNAPSHOT (a direct top-left copy of
         * the scene color), so it uses Metal-native in.position (top-left) —
         * NOT the GL-oriented flipped fragCoord that coverage/noise use.
         * u.diagFbOrigin is the top-left viewport origin (Phase 3.3). */
        tb_str(s, "    float2 memoryUv = (in.position.xy - u.diagFbOrigin) / float2(uDiagFramebuffer.get_width(), uDiagFramebuffer.get_height());\n");
        tb_str(s, "    float4 memoryColor = uDiagFramebuffer.sample(smpDiag, clamp(memoryUv, float2(0.0), float2(1.0)));\n");
        tb_str(s, "    float2 diagTri0 = vDiagTri01.xy;\n");
        tb_str(s, "    float2 diagTri1 = vDiagTri01.zw;\n");
        tb_str(s, "    float2 diagTri2 = vDiagTri2;\n");
        tb_str(s, "    float coverageCount = 0.0;\n");
        const char *offs[8][2] = {
            {"-0.500", "-0.375"}, {" 0.000", "-0.375"}, {"-0.250", "-0.125"}, {" 0.250", "-0.125"},
            {"-0.500", " 0.125"}, {" 0.000", " 0.125"}, {"-0.250", " 0.375"}, {" 0.250", " 0.375"}};
        for (int k = 0; k < 8; k++)
            tb_fmt(s, "    coverageCount += diagCoverageSample(fragCoord, u.diagViewport, float2(%s, %s), diagTri0, diagTri1, diagTri2);\n", offs[k][0], offs[k][1]);
        tb_str(s, "    if (coverageCount < 0.5) { discard_fragment(); return float4(0.0); }\n");
        tb_str(s, "    float memoryCoverage = floor(floor(clamp(memoryColor.a, 0.0, 1.0) * 255.0 + 0.5) / 32.0);\n");
        tb_str(s, "    float coverageTotal = coverageCount + memoryCoverage;\n");
        tb_str(s, "    float coverageWrap = step(8.0, coverageTotal);\n");
        tb_str(s, "    float newCoverage = glslMod(coverageTotal, 8.0);\n");
        tb_str(s, "    float newCoverageAlpha = (newCoverage * 32.0) / 255.0;\n");
        tb_str(s, "    float pixelAlphaByte = floor(clamp(texel.a, 0.0, 1.0) * 255.0 + 0.5);\n");
        tb_str(s, "    float a0 = floor(pixelAlphaByte / 8.0);\n");
        tb_str(s, "    float a1 = floor((255.0 - pixelAlphaByte) / 8.0);\n");
        tb_str(s, "    float3 pixelByte = floor(clamp(texel.rgb, 0.0, 1.0) * 255.0 + 0.5);\n");
        tb_str(s, "    float3 memoryByte = floor(clamp(memoryColor.rgb, 0.0, 1.0) * 255.0 + 0.5);\n");
        tb_str(s, "    float3 blendedByte = floor((pixelByte * a0 + memoryByte * (a1 + 1.0)) / 32.0);\n");
        tb_str(s, "    float3 outByte = mix(memoryByte, blendedByte, coverageWrap);\n");
        tb_str(s, "    texel = float4(clamp(outByte / 255.0, 0.0, 1.0), newCoverageAlpha);\n");
    } else if (cc.opt_alpha && cc.diag_rdp_memory_blend) {
        /* memoryUv samples the framebuffer SNAPSHOT (a direct top-left copy of
         * the scene color), so it uses Metal-native in.position (top-left) —
         * NOT the GL-oriented flipped fragCoord that coverage/noise use.
         * u.diagFbOrigin is the top-left viewport origin (Phase 3.3). */
        tb_str(s, "    float2 memoryUv = (in.position.xy - u.diagFbOrigin) / float2(uDiagFramebuffer.get_width(), uDiagFramebuffer.get_height());\n");
        tb_str(s, "    float4 memoryColor = uDiagFramebuffer.sample(smpDiag, clamp(memoryUv, float2(0.0), float2(1.0)));\n");
        tb_str(s, "    float pixelAlphaByte = floor(clamp(texel.a, 0.0, 1.0) * 255.0 + 0.5);\n");
        tb_str(s, "    float a0 = floor(pixelAlphaByte / 8.0);\n");
        tb_str(s, "    float a1 = floor((255.0 - pixelAlphaByte) / 8.0);\n");
        tb_str(s, "    float3 pixelByte = floor(clamp(texel.rgb, 0.0, 1.0) * 255.0 + 0.5);\n");
        tb_str(s, "    float3 memoryByte = floor(clamp(memoryColor.rgb, 0.0, 1.0) * 255.0 + 0.5);\n");
        tb_str(s, "    float3 blendedByte = floor((pixelByte * a0 + memoryByte * (a1 + 1.0)) / 32.0);\n");
        tb_str(s, "    texel = float4(clamp(blendedByte / 255.0, 0.0, 1.0), 1.0);\n");
    }

    if (cc.opt_world_pos && mtl_world_pos_diag_enabled())
        tb_str(s, "    return float4(fract(in.vWorldPos * 0.01), 1.0);\n}\n");
    else if (cc.opt_alpha) tb_str(s, "    return texel;\n}\n");
    else                   tb_str(s, "    return float4(texel, 1.0);\n}\n");
}

static struct ShaderProgram *mtl_create_and_load_new_shader(uint64_t id0, uint32_t id1) {
    MetalShader *ms = new MetalShader();
    ms->id0 = id0;
    ms->id1 = id1;
    gfx_cc_get_features(id0, id1, &ms->cc);
    ms->numInputs = ms->cc.num_inputs;
    ms->usedTextures[0] = ms->cc.used_textures[0];
    ms->usedTextures[1] = ms->cc.used_textures[1];
    ms->diagRdpMemory = ms->cc.opt_alpha && ms->cc.diag_rdp_memory_blend;
    ms->diagRdpCvgMemory = ms->cc.opt_alpha && ms->cc.diag_rdp_cvg_memory_blend;

    TB src;
    tb_init(&src, 65536);
    mtl_generate_msl(ms, &src);

    if (mtl_dump_shaders()) {
        fprintf(stderr, "[metal] MSL for id0=0x%016llX id1=0x%X:\n%s\n--- END ---\n",
                (unsigned long long)id0, id1, src.p);
    }

    NSError *err = nil;
    NSString *nssrc = [NSString stringWithUTF8String:src.p];
    id<MTLLibrary> lib = [s_device newLibraryWithSource:nssrc options:nil error:&err];
    if (lib == nil) {
        fprintf(stderr, "[metal] FATAL: MSL compile failed for id0=0x%016llX id1=0x%X:\n%s\nSource:\n%s\n",
                (unsigned long long)id0, id1,
                err ? err.localizedDescription.UTF8String : "(no error)", src.p);
        abort();
    }
    tb_free(&src);
    ms->library = lib;
    ms->vtxFn = [lib newFunctionWithName:@"vertexMain"];
    ms->fragFn = [lib newFunctionWithName:@"fragmentMain"];
    if (ms->vtxFn == nil || ms->fragFn == nil) {
        fprintf(stderr, "[metal] FATAL: missing vertexMain/fragmentMain for id0=0x%016llX\n",
                (unsigned long long)id0);
        abort();
    }
    ms->psoCache = [NSMutableDictionary dictionary];

    if (s_shader_count >= s_shader_cap) {
        int newcap = s_shader_cap ? s_shader_cap * 2 : 256;
        MetalShader **grown = (MetalShader **)realloc(s_shaders, (size_t)newcap * sizeof(MetalShader *));
        if (grown != nullptr) {
            s_shaders = grown;
            s_shader_cap = newcap;
        }
    }
    if (s_shader_count < s_shader_cap) {
        s_shaders[s_shader_count++] = ms;
    } else {
        /* realloc failed (OOM) — the shader still works for this call; it just
         * won't be cached (may recompile). Prefer that over a crash. */
        fprintf(stderr, "[metal] WARNING: shader pool grow failed — not caching\n");
    }
    s_cur_shader = ms;
    return (struct ShaderProgram *)ms;
}

static struct ShaderProgram *mtl_lookup_shader(uint64_t id0, uint32_t id1) {
    for (int i = 0; i < s_shader_count; i++) {
        MetalShader *ms = s_shaders[i];
        if (ms->id0 == id0 && ms->id1 == id1) return (struct ShaderProgram *)ms;
    }
    return nullptr;
}

static void mtl_shader_get_info(struct ShaderProgram *prg, uint8_t *num_inputs, bool used_textures[2]) {
    MetalShader *ms = (MetalShader *)prg;
    if (ms == nullptr) {
        if (num_inputs) *num_inputs = 0;
        if (used_textures) { used_textures[0] = used_textures[1] = false; }
        return;
    }
    if (num_inputs) *num_inputs = (uint8_t)ms->numInputs;
    if (used_textures) { used_textures[0] = ms->usedTextures[0]; used_textures[1] = ms->usedTextures[1]; }
}

static void mtl_load_shader(struct ShaderProgram *new_prg) {
    s_cur_shader = (MetalShader *)new_prg;
}

static void mtl_unload_shader(struct ShaderProgram *old_prg) {
    if (s_cur_shader == (MetalShader *)old_prg) s_cur_shader = nullptr;
}

/* ==========================================================================
 * Phase 3 — render targets, deferred state, caches, draw flush
 * ========================================================================== */

/* Uniform block — byte layout MUST match `struct Uniforms` in the generated MSL
 * (mtl_generate_msl). 16-byte aligned, 48 bytes total. */
struct MtlUniforms {
    float diagViewport[4];   /* 0  */
    float n64FilterScale[2]; /* 16 */
    float diagFbOrigin[2];   /* 24 */
    int   frameCount;        /* 32 */
    int   winH;              /* 36 */
    int   fbH;               /* 40 */
    int   _pad;              /* 44 -> 48 */
};

/* Offscreen scene targets (color is sampleable + blittable for readback and the
 * RDP snapshot; the drawable is framebufferOnly and cannot serve those). */
static id<MTLTexture> s_scene_color = nil;
static id<MTLTexture> s_scene_depth = nil;
static id<MTLTexture> s_snapshot_tex = nil;  /* XLU/RDP framebuffer snapshot (3.3) */
static id<MTLTexture> s_final_color = nil;   /* Phase 4 output-filter result (present + readback source) */
static id<MTLTexture> s_filter_low = nil;    /* Phase 4 output-filter 8-bit intermediate (GL's pre-pass low_tex) */
static id<MTLSamplerState> s_snapshot_sampler = nil;
static id<MTLSamplerState> s_depth_sampler = nil;  /* nearest/clamp for SSAO depth reads (Phase 5) */
static int s_fb_w = 0, s_fb_h = 0;
static id<MTLRenderCommandEncoder> s_enc = nil;
static id<MTLTexture> s_white_tex = nil;   /* 1x1 white for unbound tiles */
/* The texture actually presented this frame — what readback/screenshots must
 * read (post-filter when the filter ran, else the raw scene). GL reads its
 * post-filter default framebuffer; this keeps Metal screenshots consistent. */
static id<MTLTexture> s_readback_src = nil;

/* Deferred render state (set by the vtable setters, resolved in draw_triangles). */
static enum GfxBlendMode s_blend = GFX_BLEND_DISABLED;
static bool s_preserve_cov_alpha = false;
static bool s_depth_test = false, s_depth_update = false, s_depth_compare = false;
static uint16_t s_zmode = 0;
static uint32_t s_tile_tex[2] = {0, 0};
static bool s_tile_linear[2] = {false, false};
static uint32_t s_tile_cms[2] = {0, 0}, s_tile_cmt[2] = {0, 0};
static uint32_t s_last_selected_id = 0;
static int s_vp_x = 0, s_vp_y = 0, s_vp_w = 0, s_vp_h = 0;
static int s_sc_x = 0, s_sc_y = 0, s_sc_w = 0, s_sc_h = 0;
static bool s_sc_set = false;
static uint32_t s_frame_count_metal = 0;

/* Triple-buffered vertex arena — replaces a per-draw newBufferWithBytes (which
 * was thousands of driver allocations/frame on foliage/glass scenes). Each real
 * frame bump-allocates from one of MTL_VBUF_SLOTS StorageModeShared buffers; a
 * dispatch_semaphore throttles the CPU to <= MTL_VBUF_SLOTS frames ahead so a
 * slot is only reused after its GPU work completes. Draws that overflow a slot
 * fall back to a one-off buffer (rare; grows the high-water for next (re)alloc). */
#define MTL_VBUF_SLOTS 3
static id<MTLBuffer> s_vbuf[MTL_VBUF_SLOTS];
static size_t s_vbuf_cap[MTL_VBUF_SLOTS];
static int s_vbuf_slot = 0;
static size_t s_vbuf_cursor = 0;
static size_t s_vbuf_want = (2u << 20);  /* high-water target for slot (re)alloc; 2 MB seed */
static int s_ring_index = 0;             /* advances per REAL frame (cmdbuf created), not per skip */
static dispatch_semaphore_t s_frame_sem = nil;

/* Texture registry (id -> MTLTexture) + pipeline/depth/sampler caches. */
static NSMutableDictionary<NSNumber *, id<MTLTexture>> *s_textures = nil;
static NSMutableDictionary<NSNumber *, id<MTLDepthStencilState>> *s_depth_cache = nil;
static NSMutableDictionary<NSNumber *, id<MTLSamplerState>> *s_sampler_cache = nil;

static id<MTLTexture> mtl_lookup_tex(uint32_t id) {
    if (id == 0 || s_textures == nil) return nil;
    return s_textures[@(id)];
}

/* Video.VSync for the Metal backend. platformApplyVSync's SDL_GL_SetSwapInterval
 * is a GL-only no-op here; the CAMetalLayer equivalent is displaySyncEnabled
 * (default YES). Stored in a static because the platform applies vsync before
 * mtl_init has acquired the layer; mtl_init re-applies the stored value. */
static bool s_vsync_enabled = true;

extern "C" void gfx_metal_set_vsync(int enabled) {
    s_vsync_enabled = (enabled != 0);
    if (s_layer != nil) {
        s_layer.displaySyncEnabled = s_vsync_enabled ? YES : NO;
    }
}

/* Port of gfx_opengl_axis_filter_scale (gfx_opengl.c:594-615). */
static float mtl_axis_filter_scale(uint32_t drawable_size, int logical_size, int fallback) {
    if (logical_size <= 0) logical_size = fallback;
    if (drawable_size == 0 || logical_size <= 0) return 1.0f;
    float scale = (float)drawable_size / (float)logical_size;
    if (scale < 1.0f) return 1.0f;
    if (scale > 64.0f) return 64.0f;
    return scale;
}

static MTLSamplerAddressMode mtl_wrap(uint32_t v) {
    if (v & GE007_G_TX_CLAMP) return MTLSamplerAddressModeClampToEdge;
    return (v & GE007_G_TX_MIRROR) ? MTLSamplerAddressModeMirrorRepeat
                                   : MTLSamplerAddressModeRepeat;
}

extern "C" int gfx_metal_max_offscreen_dim(void);  /* defined below */

static void mtl_ensure_targets(int w, int h) {
    if (w <= 0 || h <= 0) return;
    int cap = gfx_metal_max_offscreen_dim();
    if (w > cap) w = cap;
    if (h > cap) h = cap;
    if (s_scene_color != nil && s_fb_w == w && s_fb_h == h) return;
    /* Scene color: render target + blit source (present/readback/snapshot) AND
     * the Phase-4 output-filter source (sampled/read in the filter fragment), so
     * it needs ShaderRead. */
    MTLTextureDescriptor *cd =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:w height:h mipmapped:NO];
    cd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    cd.storageMode = MTLStorageModePrivate;
    id<MTLTexture> color = [s_device newTextureWithDescriptor:cd];
    /* Depth: render target + ShaderRead so Phase 5 SSAO can sample it (the
     * render pass stores it — storeAction=Store in mtl_open_scene_encoder). This
     * native Depth32Float sample is the whole point: it op-hangs GL-over-Metal. */
    MTLTextureDescriptor *dd =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                           width:w height:h mipmapped:NO];
    dd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    dd.storageMode = MTLStorageModePrivate;
    id<MTLTexture> depth = [s_device newTextureWithDescriptor:dd];
    /* XLU/RDP snapshot target: a copy of the scene color the fragment samples
     * mid-frame (Phase 3.3). Same format so the blit-copy is exact. */
    MTLTextureDescriptor *sd =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:w height:h mipmapped:NO];
    sd.usage = MTLTextureUsageShaderRead;
    sd.storageMode = MTLStorageModePrivate;
    id<MTLTexture> snap = [s_device newTextureWithDescriptor:sd];
    /* Phase-4 output-filter result: render target + blit source (present) +
     * readback source. */
    MTLTextureDescriptor *fd =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:w height:h mipmapped:NO];
    fd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    fd.storageMode = MTLStorageModePrivate;
    id<MTLTexture> finalc = [s_device newTextureWithDescriptor:fd];
    id<MTLTexture> low = [s_device newTextureWithDescriptor:fd];  /* same desc: RT + ShaderRead */

    if (color == nil || depth == nil || snap == nil || finalc == nil || low == nil) {
        /* Partial allocation failure: bind nothing broken. Leave s_fb_w/h
         * unchanged so the line-above early-out doesn't latch a bad size and
         * start_frame's `s_scene_color == nil` guard skips the frame; next frame
         * retries. */
        fprintf(stderr, "[metal] WARNING: scene target allocation failed (%dx%d)\n", w, h);
        s_scene_color = nil;
        s_scene_depth = nil;
        s_snapshot_tex = nil;
        s_final_color = nil;
        s_filter_low = nil;
        s_readback_src = nil;
        return;
    }
    s_scene_color = color;
    s_scene_depth = depth;
    s_snapshot_tex = snap;
    s_final_color = finalc;
    s_filter_low = low;
    s_readback_src = color;  /* until a frame sets it */
    s_fb_w = w;
    s_fb_h = h;
    /* Match the drawable to the render resolution so the end_frame blit is
     * size-exact; the compositor scales the drawable to the window (this is the
     * SSAA downsample, mirroring GL's resolve_scene_target blit). */
    if (s_layer != nil) s_layer.drawableSize = CGSizeMake(w, h);
}

static void mtl_ensure_white(void) {
    if (s_white_tex != nil) return;
    MTLTextureDescriptor *d =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:1 height:1 mipmapped:NO];
    d.usage = MTLTextureUsageShaderRead;
    d.storageMode = MTLStorageModeShared;
    s_white_tex = [s_device newTextureWithDescriptor:d];
    if (s_white_tex == nil) return;  /* draw falls back to whatever is bound; no crash */
    uint8_t white[4] = {255, 255, 255, 255};
    [s_white_tex replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:white bytesPerRow:4];
}

static void mtl_build_vertex_descriptor(MetalShader *ms) {
    if (ms->vtxDesc != nil) return;
    MTLVertexDescriptor *vd = [MTLVertexDescriptor vertexDescriptor];
    for (int i = 0; i < ms->numAttrs; i++) {
        MtlAttr a = ms->attrs[i];
        MTLVertexFormat fmt = a.size == 1 ? MTLVertexFormatFloat
                            : a.size == 2 ? MTLVertexFormatFloat2
                            : a.size == 3 ? MTLVertexFormatFloat3
                                          : MTLVertexFormatFloat4;
        vd.attributes[a.index].format = fmt;
        vd.attributes[a.index].offset = (NSUInteger)a.offset * sizeof(float);
        vd.attributes[a.index].bufferIndex = 0;
    }
    vd.layouts[0].stride = (NSUInteger)ms->numFloats * sizeof(float);
    vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    vd.layouts[0].stepRate = 1;
    ms->vtxDesc = vd;
}

/* Metal bakes blend/format/samples/writeMask into the PSO, so the GL immediate
 * setters collapse to this lazily-cached lookup keyed on the dynamic state. */
static id<MTLRenderPipelineState> mtl_pso_for(MetalShader *ms, enum GfxBlendMode blend,
                                              int samples, bool write_alpha) {
    uint64_t key = (uint64_t)(blend & 0xF) |
                   ((uint64_t)(samples & 0xFF) << 4) |
                   ((uint64_t)(write_alpha ? 1 : 0) << 12);
    /* Fast path: consecutive draws usually share shader+state — skip the
     * NSNumber boxing + dictionary lookup (and its autoreleased temporary). */
    static MetalShader *last_ms = nullptr;
    static uint64_t last_key = ~0ull;
    static id<MTLRenderPipelineState> last_pso = nil;
    if (ms == last_ms && key == last_key && last_pso != nil) return last_pso;
    NSNumber *k = @(key);
    id<MTLRenderPipelineState> pso = ms->psoCache[k];
    if (pso != nil) { last_ms = ms; last_key = key; last_pso = pso; return pso; }

    MTLRenderPipelineDescriptor *d = [[MTLRenderPipelineDescriptor alloc] init];
    d.vertexFunction = ms->vtxFn;
    d.fragmentFunction = ms->fragFn;
    d.vertexDescriptor = ms->vtxDesc;
    d.rasterSampleCount = samples;
    d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

    MTLRenderPipelineColorAttachmentDescriptor *ca = d.colorAttachments[0];
    ca.pixelFormat = MTLPixelFormatBGRA8Unorm;
    MTLColorWriteMask wm = MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue;
    if (write_alpha) wm |= MTLColorWriteMaskAlpha;
    ca.writeMask = wm;

    /* Blend factors mirror gfx_opengl_set_blend_mode:1619-1647 (default diag). */
    bool enable = (blend == GFX_BLEND_ALPHA || blend == GFX_BLEND_MODULATE ||
                   blend == GFX_BLEND_ALPHA_COVERAGE ||
                   blend == GFX_BLEND_ALPHA_CVG_WRAP_STENCIL);
    ca.blendingEnabled = enable;
    if (enable) {
        if (blend == GFX_BLEND_MODULATE) {
            ca.sourceRGBBlendFactor = MTLBlendFactorDestinationColor;
            ca.destinationRGBBlendFactor = MTLBlendFactorZero;
            ca.sourceAlphaBlendFactor = MTLBlendFactorDestinationAlpha;
            ca.destinationAlphaBlendFactor = MTLBlendFactorZero;
        } else {
            ca.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
            ca.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            ca.sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
            ca.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        }
        ca.rgbBlendOperation = MTLBlendOperationAdd;
        ca.alphaBlendOperation = MTLBlendOperationAdd;
    }

    NSError *err = nil;
    pso = [s_device newRenderPipelineStateWithDescriptor:d error:&err];
    if (pso == nil) {
        fprintf(stderr, "[metal] FATAL: PSO build failed: %s\n",
                err ? err.localizedDescription.UTF8String : "(no error)");
        abort();
    }
    ms->psoCache[k] = pso;
    last_ms = ms; last_key = key; last_pso = pso;
    return pso;
}

static id<MTLDepthStencilState> mtl_depth_state_for(bool test, bool update, bool compare, uint16_t zmode) {
    uint64_t key = (test ? 1 : 0) | (update ? 2 : 0) | (compare ? 4 : 0) | ((uint64_t)zmode << 3);
    static uint64_t last_key = ~0ull;
    static id<MTLDepthStencilState> last_ds = nil;
    if (key == last_key && last_ds != nil) return last_ds;
    NSNumber *k = @(key);
    id<MTLDepthStencilState> ds = s_depth_cache[k];
    if (ds != nil) { last_key = key; last_ds = ds; return ds; }
    MTLDepthStencilDescriptor *dd = [[MTLDepthStencilDescriptor alloc] init];
    /* GL: test+compare -> LEQUAL (all live zmodes); test+!compare -> ALWAYS
     * (write-only); !test -> disabled (ALWAYS, no write). */
    dd.depthCompareFunction = (test && compare) ? MTLCompareFunctionLessEqual
                                                : MTLCompareFunctionAlways;
    dd.depthWriteEnabled = (test && update);
    ds = [s_device newDepthStencilStateWithDescriptor:dd];
    s_depth_cache[k] = ds;
    last_key = key; last_ds = ds;
    return ds;
}

static id<MTLSamplerState> mtl_sampler_for(bool linear, uint32_t cms, uint32_t cmt) {
    uint64_t key = (linear ? 1 : 0) | ((uint64_t)(cms & 0xFFFF) << 1) | ((uint64_t)(cmt & 0xFFFF) << 17);
    static uint64_t last_key = ~0ull;
    static id<MTLSamplerState> last_s = nil;
    if (key == last_key && last_s != nil) return last_s;
    NSNumber *k = @(key);
    id<MTLSamplerState> s = s_sampler_cache[k];
    if (s != nil) { last_key = key; last_s = s; return s; }
    MTLSamplerDescriptor *sd = [[MTLSamplerDescriptor alloc] init];
    MTLSamplerMinMagFilter f = linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    sd.minFilter = f;
    sd.magFilter = f;
    sd.mipFilter = MTLSamplerMipFilterNotMipmapped;
    sd.sAddressMode = mtl_wrap(cms);
    sd.tAddressMode = mtl_wrap(cmt);
    sd.maxAnisotropy = linear ? 16 : 1;
    s = [s_device newSamplerStateWithDescriptor:sd];
    s_sampler_cache[k] = s;
    last_key = key; last_s = s;
    return s;
}

/* Open the scene render encoder. clear=true clears color+depth (frame start);
 * clear=false preserves prior contents (Load) — used to resume after an XLU/RDP
 * snapshot forces an encoder break. */
static void mtl_open_scene_encoder(bool clear) {
    MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    rpd.colorAttachments[0].texture = s_scene_color;
    rpd.colorAttachments[0].loadAction = clear ? MTLLoadActionClear : MTLLoadActionLoad;
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
    if (clear) rpd.colorAttachments[0].clearColor = MTLClearColorMake(s_clear_r, s_clear_g, s_clear_b, 1.0);
    rpd.depthAttachment.texture = s_scene_depth;
    rpd.depthAttachment.loadAction = clear ? MTLLoadActionClear : MTLLoadActionLoad;
    rpd.depthAttachment.storeAction = MTLStoreActionStore;
    if (clear) rpd.depthAttachment.clearDepth = 1.0;
    s_enc = [s_cmdbuf renderCommandEncoderWithDescriptor:rpd];
    [s_enc setDepthClipMode:MTLDepthClipModeClamp];
}

/* ---- init / frame lifecycle ---------------------------------------------- */

static void mtl_init(void) {
    s_device = MTLCreateSystemDefaultDevice();
    if (s_device == nil) {
        /* No usable Metal device. The backend is already selected upstream and
         * there is no in-process GL fallback, so fail here with a clear cause
         * rather than aborting later inside newLibraryWithSource with a
         * misleading "MSL compile failed" message. */
        fprintf(stderr, "[metal] FATAL: MTLCreateSystemDefaultDevice returned nil — no usable Metal device\n");
        abort();
    }
    s_queue = [s_device newCommandQueue];
    s_frame_sem = dispatch_semaphore_create(MTL_VBUF_SLOTS);
    s_layer = (__bridge CAMetalLayer *)platformGetMetalLayer();
    if (s_layer != nil) {
        s_layer.device = s_device;
        s_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        /* NO: the scene is composited offscreen and blitted into the drawable at
         * end_frame; a framebufferOnly drawable can't be a blit destination. */
        s_layer.framebufferOnly = NO;
        /* Re-apply the vsync mode the platform recorded before the layer existed. */
        s_layer.displaySyncEnabled = s_vsync_enabled ? YES : NO;
    } else {
        fprintf(stderr, "[metal] WARNING: no CAMetalLayer from platform — present will be skipped\n");
    }
    s_textures = [NSMutableDictionary dictionary];
    s_depth_cache = [NSMutableDictionary dictionary];
    s_sampler_cache = [NSMutableDictionary dictionary];
    {
        /* Clamp+linear sampler for the XLU/RDP framebuffer snapshot (index 2). */
        MTLSamplerDescriptor *ss = [[MTLSamplerDescriptor alloc] init];
        ss.minFilter = MTLSamplerMinMagFilterLinear;
        ss.magFilter = MTLSamplerMinMagFilterLinear;
        ss.mipFilter = MTLSamplerMipFilterNotMipmapped;
        ss.sAddressMode = MTLSamplerAddressModeClampToEdge;
        ss.tAddressMode = MTLSamplerAddressModeClampToEdge;
        s_snapshot_sampler = [s_device newSamplerStateWithDescriptor:ss];
        /* SSAO depth sampler: NEAREST (no interpolation across depth
         * discontinuities) + clampToEdge; non-comparison (returns the depth). */
        MTLSamplerDescriptor *ds = [[MTLSamplerDescriptor alloc] init];
        ds.minFilter = MTLSamplerMinMagFilterNearest;
        ds.magFilter = MTLSamplerMinMagFilterNearest;
        ds.mipFilter = MTLSamplerMipFilterNotMipmapped;
        ds.sAddressMode = MTLSamplerAddressModeClampToEdge;
        ds.tAddressMode = MTLSamplerAddressModeClampToEdge;
        s_depth_sampler = [s_device newSamplerStateWithDescriptor:ds];
    }
    /* Invariance-critical (parent plan §2.1): the CPU clipper reads
     * g_depth_clamp_enabled; Metal uses native depth-clamp, so force it true. */
    g_depth_clamp_enabled = true;
    fprintf(stderr, "[metal] native Metal backend init: device='%s' layer=%p\n",
            s_device.name.UTF8String, (__bridge void *)s_layer);
}

static void mtl_start_frame(void) {
  @autoreleasepool {  /* drains per-frame autoreleased temporaries — without it,
                       * the C game loop never drains and RSS/drawable pool grow. */
    if (s_layer == nil || s_queue == nil) return;
    s_frame_count_metal++;
    /* Reset the SSAO scene-projection coefficient each frame (mirrors
     * gfx_opengl_start_frame): gfx_sp_matrix only ever RAISES proj_b, so without
     * this the `proj_b != 0` SSAO gate would latch on forever and menu/HUD frames
     * would feed stale coefficients. Render-only global — gameplay-invariant. */
    g_pc_ssao_proj_b = 0.0f;
    g_pc_view_inv_valid = 0;   /* W1.E2.T1: recapture the view-inverse each frame */
    /* Render at the frontend's resolution (gfx_current_dimensions), which the
     * viewports/T&L are computed against — NOT the raw layer size, which can be
     * a 2x render-scale/SSAA smaller. */
    int rw = (int)gfx_current_dimensions.width;
    int rh = (int)gfx_current_dimensions.height;
    if (rw <= 0 || rh <= 0) {
        CGSize ds = s_layer.drawableSize;
        rw = (int)ds.width;
        rh = (int)ds.height;
    }
    mtl_ensure_targets(rw, rh);
    mtl_ensure_white();
    if (s_scene_color == nil) return;

    /* Throttle the CPU to <= MTL_VBUF_SLOTS frames ahead so the ring slot we are
     * about to write is not still being read by the GPU. Placed AFTER the early
     * returns and immediately before the command buffer is created, so this wait
     * is exactly paired with the signal in end_frame (which runs iff s_cmdbuf). */
    if (s_frame_sem != nil) dispatch_semaphore_wait(s_frame_sem, DISPATCH_TIME_FOREVER);
    s_vbuf_slot = s_ring_index % MTL_VBUF_SLOTS;
    s_ring_index++;
    s_vbuf_cursor = 0;
    if (s_vbuf[s_vbuf_slot] == nil || s_vbuf_cap[s_vbuf_slot] < s_vbuf_want) {
        /* Safe to (re)allocate: the semaphore guarantees this slot's prior GPU
         * work completed. Any still-in-flight reference to the old buffer is
         * retained by its command buffer until the GPU is done. */
        s_vbuf[s_vbuf_slot] = [s_device newBufferWithLength:s_vbuf_want options:MTLResourceStorageModeShared];
        s_vbuf_cap[s_vbuf_slot] = (s_vbuf[s_vbuf_slot] != nil) ? s_vbuf_want : 0;
    }

    s_cmdbuf = [s_queue commandBuffer];
    mtl_open_scene_encoder(true);

    if (!s_logged_first_frame) {
        fprintf(stderr, "[metal] first frame: scene %dx%d, geometry encoder open\n", s_fb_w, s_fb_h);
        s_logged_first_frame = true;
    }
  }
}

/* ==========================================================================
 * Phase 4 — output-VI-filter post-FX (FXAA / bloom / grade / tonemap / gamma /
 * vignette / CAS sharpen / Bayer dither / RGB555). Fullscreen-triangle chain run
 * in end_frame, sampling s_scene_color. Ports gfx_opengl.c:2833-3091 (minus SSAO,
 * which lands in Phase 5) + the per-pass uniform values (gfx_opengl.c:3163-3244).
 * ========================================================================== */

/* C mirror of the MSL `struct FilterUniforms` — 16-byte aligned, 144 bytes.
 * Field offsets match the MSL struct (float4@0, float2@16/24, 17 floats@32,
 * 9 ints@100); trailing pad rounds MSL's align-16 size to 144. */
struct MtlFilterUniforms {
    float colorTint[4];     /* 0  (float4; .xyz used) */
    float srcSize[2];       /* 16 */
    float dstSize[2];       /* 24 */
    float colorScale, colorBias, gamma, saturation, contrast, brightness, vignette,
          bloomThreshold, bloomIntensity, sharpen, levelSat, levelCon,
          ssaoRadius, ssaoIntensity, ssaoAspect, ssaoProjA, ssaoProjB;  /* 32..99 */
    int applyPost, dither, bloom, ssao, filterMode, fxaa, tonemap, rgb555, fbH;  /* 100..135 */
    int _pad[2];            /* 136..143 -> 144 */
};

static id<MTLLibrary>            s_filter_lib = nil;
static id<MTLRenderPipelineState> s_filter_pso = nil;
static id<MTLSamplerState>       s_filter_smp = nil;

static float mtl_clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* Diag knobs (default identity) — cached like the other env checks. */
static float mtl_diag_color_scale(void) {
    static float v = -1.0f;
    if (v < 0.0f) { const char *e = getenv("GE007_DIAG_OUTPUT_COLOR_SCALE"); v = (e && e[0]) ? (float)atof(e) : 1.0f; }
    return v;
}
static float mtl_diag_color_bias(void) {
    static int checked = 0; static float v = 0.0f;
    if (!checked) { const char *e = getenv("GE007_DIAG_OUTPUT_COLOR_BIAS"); if (e && e[0]) v = (float)atof(e); checked = 1; }
    return v;
}
static int mtl_diag_rgb555_mode(void) {
    static int m = -1;
    if (m < 0) {
        const char *e = getenv("GE007_DIAG_OUTPUT_RGB555");
        m = 0;
        if (e && e[0]) {
            if (!strcmp(e, "1") || !strcmp(e, "on") || !strcmp(e, "true")) m = 1;
            else if (!strcmp(e, "dither")) m = 2;
        }
    }
    return m;
}

/* Gate mirrors gfx_opengl_output_color_adjust_active (gfx_opengl.c:2779-2808)
 * plus the separate bloom term from apply_output_vi_filter's gate (:3289-3295).
 * VI-downscale + SSAO terms are out of Phase-4 scope (single-pass, SSAO off). */
static bool mtl_output_filter_active(void) {
    float gamma = mtl_clampf(g_pcVideoGamma, 0.5f, 2.5f);
    /* gamma + diag color/rgb555 are honored even with RemasterFX off (display). */
    if (mtl_diag_color_scale() != 1.0f || mtl_diag_color_bias() != 0.0f ||
        mtl_diag_rgb555_mode() != 0 || gamma < 0.999f || gamma > 1.001f) return true;
    if (!g_pcRemasterFX) return false;
    return mtl_clampf(g_pcVideoSaturation, 0.0f, 2.0f) != 1.0f ||
           mtl_clampf(g_pcVideoContrast, 0.5f, 2.0f) != 1.0f ||
           mtl_clampf(g_pcVideoBrightness, -0.5f, 0.5f) != 0.0f ||
           g_pcOutputDither != 0 ||
           mtl_clampf(g_pcVignette, 0.0f, 1.0f) > 0.0001f ||
           g_pcFxaa != 0 ||
           mtl_clampf(g_pcSharpen, 0.0f, 1.0f) > 0.0f ||   /* GL's use_sharpen gate is >0 (:3293) */
           g_pcTonemap != 0 ||
           g_pcBloom != 0 ||
           /* SSAO alone must trigger the filter chain (mirrors GL's use_ssao,
            * gfx_opengl.c:3292 — output_ssao_active is RemasterFX && Ssao, and we
            * are already past the !RemasterFX early-out; proj_b!=0 stands in for
            * GL's g_scene_depth_valid on the single-sample Metal path). */
           (g_pcSsao != 0 && g_pc_ssao_proj_b != 0.0f) ||
           (g_pcGradePresets && (g_pcGradeLevelSat != 1.0f || g_pcGradeLevelCon != 1.0f ||
                                 g_pcGradeLevelTintR != 1.0f || g_pcGradeLevelTintG != 1.0f ||
                                 g_pcGradeLevelTintB != 1.0f));
}

/* Build + compile the fullscreen filter library/PSO/sampler once. Returns false
 * (caller falls back to the straight blit) on any failure. */
static bool mtl_ensure_filter_program(void) {
    if (s_filter_pso != nil) return true;
    if (s_device == nil) return false;
    TB s; tb_init(&s, 32768);
    tb_str(&s, "#include <metal_stdlib>\nusing namespace metal;\n");
    tb_str(&s, "struct FilterUniforms {\n");
    tb_str(&s, "  float4 colorTint; float2 srcSize; float2 dstSize;\n");
    tb_str(&s, "  float colorScale, colorBias, gamma, saturation, contrast, brightness, vignette, bloomThreshold, bloomIntensity, sharpen, levelSat, levelCon, ssaoRadius, ssaoIntensity, ssaoAspect, ssaoProjA, ssaoProjB;\n");
    tb_str(&s, "  int applyPost, dither, bloom, ssao, filterMode, fxaa, tonemap, rgb555, fbH;\n");
    tb_str(&s, "};\n");
    tb_str(&s, "constant float kBayer4[16] = { 0.0/16.0, 8.0/16.0, 2.0/16.0, 10.0/16.0, 12.0/16.0, 4.0/16.0, 14.0/16.0, 6.0/16.0, 3.0/16.0, 11.0/16.0, 1.0/16.0, 9.0/16.0, 15.0/16.0, 7.0/16.0, 13.0/16.0, 5.0/16.0 };\n");
    tb_str(&s, "struct FVO { float4 position [[position]]; float2 vTexCoord; };\n");
    /* Fullscreen triangle; V-flip vTexCoord so v=0 is the TOP of the top-left
     * scene texture (bloom/vignette sample via vTexCoord). */
    tb_str(&s, "vertex FVO filterVS(uint vid [[vertex_id]]) {\n");
    tb_str(&s, "  const float2 kPos[3] = { float2(-1.0,-1.0), float2(3.0,-1.0), float2(-1.0,3.0) };\n");
    tb_str(&s, "  FVO o; o.position = float4(kPos[vid], 0.0, 1.0);\n");
    tb_str(&s, "  o.vTexCoord = float2(kPos[vid].x * 0.5 + 0.5, 0.5 - kPos[vid].y * 0.5); return o;\n}\n");
    /* Color helpers (thread tex+uniforms explicitly — MSL free funcs see no globals).
     * texelFetch -> uTex.read(uint2) with GL's clamp preserved. */
    tb_str(&s, "static float4 sampleNearest(texture2d<float> uTex, constant FilterUniforms& u, float2 d) {\n");
    tb_str(&s, "  int2 p = int2(floor(d * u.srcSize / u.dstSize)); p = clamp(p, int2(0), int2(u.srcSize) - int2(1)); return uTex.read(uint2(p));\n}\n");
    tb_str(&s, "static float2 fitSizeForAspect(float2 b, float a) { float ba = b.x / b.y; if (ba > a) return float2(b.y * a, b.y); return float2(b.x, b.x / a); }\n");
    tb_str(&s, "static float4 sampleFitSrcToDst(texture2d<float> uTex, constant FilterUniforms& u, float2 d) {\n");
    tb_str(&s, "  float sa = u.srcSize.x / u.srcSize.y; float2 fs = fitSizeForAspect(u.dstSize, sa); float2 off = floor((u.dstSize - fs) * 0.5);\n");
    tb_str(&s, "  if (d.x < off.x || d.y < off.y || d.x >= off.x + fs.x || d.y >= off.y + fs.y) return float4(0.0,0.0,0.0,1.0);\n");
    tb_str(&s, "  int2 p = int2(floor((d - off) * u.srcSize / fs)); p = clamp(p, int2(0), int2(u.srcSize) - int2(1)); return uTex.read(uint2(p));\n}\n");
    tb_str(&s, "static float4 sampleFitLogical(texture2d<float> uTex, constant FilterUniforms& u, float2 d) {\n");
    tb_str(&s, "  float da = u.dstSize.x / u.dstSize.y; float2 fs = fitSizeForAspect(u.srcSize, da); float2 off = floor((u.srcSize - fs) * 0.5);\n");
    tb_str(&s, "  int2 p = int2(floor(off + d * fs / u.dstSize)); p = clamp(p, int2(0), int2(u.srcSize) - int2(1)); return uTex.read(uint2(p));\n}\n");
    tb_str(&s, "static float4 sampleCpuBilinear(texture2d<float> uTex, constant FilterUniforms& u, float2 d) {\n");
    tb_str(&s, "  float2 sc = d * u.srcSize / u.dstSize - float2(0.5); int2 p0 = int2(floor(sc)); float2 f = sc - float2(p0);\n");
    tb_str(&s, "  if (p0.x < 0) { p0.x = 0; f.x = 0.0; } else if (p0.x >= int(u.srcSize.x) - 1) { p0.x = int(u.srcSize.x) - 1; f.x = 0.0; }\n");
    tb_str(&s, "  if (p0.y < 0) { p0.y = 0; f.y = 0.0; } else if (p0.y >= int(u.srcSize.y) - 1) { p0.y = int(u.srcSize.y) - 1; f.y = 0.0; }\n");
    tb_str(&s, "  int2 p1 = min(p0 + int2(1), int2(u.srcSize) - int2(1));\n");
    tb_str(&s, "  float4 c00 = uTex.read(uint2(p0)); float4 c10 = uTex.read(uint2(int2(p1.x, p0.y)));\n");
    tb_str(&s, "  float4 c01 = uTex.read(uint2(int2(p0.x, p1.y))); float4 c11 = uTex.read(uint2(p1));\n");
    tb_str(&s, "  return mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);\n}\n");
    tb_str(&s, "static float4 sampleDst(texture2d<float> uTex, constant FilterUniforms& u, float2 d) {\n");
    tb_str(&s, "  if (u.filterMode == 1) return sampleNearest(uTex, u, d);\n");
    tb_str(&s, "  else if (u.filterMode == 2) return sampleFitSrcToDst(uTex, u, d);\n");
    tb_str(&s, "  else if (u.filterMode == 3) return sampleFitLogical(uTex, u, d);\n");
    tb_str(&s, "  return sampleCpuBilinear(uTex, u, d);\n}\n");
    tb_str(&s, "static float fxLuma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }\n");
    tb_str(&s, "static float3 fxaa(texture2d<float> uTex, constant FilterUniforms& u, float2 fc, float3 rgbM) {\n");
    tb_str(&s, "  float lM = fxLuma(rgbM);\n");
    tb_str(&s, "  float lN = fxLuma(sampleDst(uTex,u,fc+float2(0.0,-1.0)).rgb); float lS = fxLuma(sampleDst(uTex,u,fc+float2(0.0,1.0)).rgb);\n");
    tb_str(&s, "  float lW = fxLuma(sampleDst(uTex,u,fc+float2(-1.0,0.0)).rgb); float lE = fxLuma(sampleDst(uTex,u,fc+float2(1.0,0.0)).rgb);\n");
    tb_str(&s, "  float lNW = fxLuma(sampleDst(uTex,u,fc+float2(-1.0,-1.0)).rgb); float lNE = fxLuma(sampleDst(uTex,u,fc+float2(1.0,-1.0)).rgb);\n");
    tb_str(&s, "  float lSW = fxLuma(sampleDst(uTex,u,fc+float2(-1.0,1.0)).rgb); float lSE = fxLuma(sampleDst(uTex,u,fc+float2(1.0,1.0)).rgb);\n");
    tb_str(&s, "  float lMin = min(lM, min(min(lN,lS), min(lW,lE))); float lMax = max(lM, max(max(lN,lS), max(lW,lE)));\n");
    tb_str(&s, "  float range = lMax - lMin; if (range < max(0.0625, lMax * 0.125)) return rgbM;\n");
    tb_str(&s, "  float2 dir; dir.x = -((lNW + lNE) - (lSW + lSE)); dir.y = ((lNW + lSW) - (lNE + lSE));\n");
    tb_str(&s, "  float dirReduce = max((lNW+lNE+lSW+lSE) * 0.03125, 0.0078125); float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);\n");
    tb_str(&s, "  dir = clamp(dir * rcpDirMin, float2(-8.0), float2(8.0));\n");
    tb_str(&s, "  float3 rgbA = 0.5 * (sampleDst(uTex,u,fc + dir * (1.0/3.0 - 0.5)).rgb + sampleDst(uTex,u,fc + dir * (2.0/3.0 - 0.5)).rgb);\n");
    tb_str(&s, "  float3 rgbB = rgbA * 0.5 + 0.25 * (sampleDst(uTex,u,fc + dir * -0.5).rgb + sampleDst(uTex,u,fc + dir * 0.5).rgb);\n");
    tb_str(&s, "  float lB = fxLuma(rgbB); if (lB < lMin || lB > lMax) return rgbA; return rgbB;\n}\n");
    tb_str(&s, "static float3 casSharpen(texture2d<float> uTex, constant FilterUniforms& u, float2 fc, float3 rgbC) {\n");
    tb_str(&s, "  float3 n = sampleDst(uTex,u,fc+float2(0.0,-1.0)).rgb; float3 so = sampleDst(uTex,u,fc+float2(0.0,1.0)).rgb;\n");
    tb_str(&s, "  float3 w = sampleDst(uTex,u,fc+float2(-1.0,0.0)).rgb; float3 e = sampleDst(uTex,u,fc+float2(1.0,0.0)).rgb;\n");
    tb_str(&s, "  float3 mn = min(rgbC, min(min(n,so), min(w,e))); float3 mx = max(rgbC, max(max(n,so), max(w,e)));\n");
    tb_str(&s, "  float3 amp = clamp(min(mn, 1.0 - mx) / max(mx, 0.0001), 0.0, 1.0); amp = sqrt(amp);\n");
    tb_str(&s, "  float peak = -0.125 - 0.075 * u.sharpen; float3 wgt = amp * peak;\n");
    tb_str(&s, "  float3 sum = rgbC + (n + so + w + e) * wgt; float3 rcpW = 1.0 / (1.0 + 4.0 * wgt);\n");
    tb_str(&s, "  float3 outc = clamp(sum * rcpW, mn, mx); return mix(rgbC, outc, clamp(u.sharpen, 0.0, 1.0));\n}\n");
    /* SSAO (Phase 5) — ports gfx_opengl.c:2984-3018. Reads native Depth32Float
     * (the op that hangs GL-over-Metal). depth2d.sample returns a float (no .r).
     * ssaoLinZ reuses GL's 2d-1 window->NDC mapping verbatim: the frontend's
     * z=(z+w)/2 remap makes Metal window depth == GL window depth. */
    tb_str(&s, "constant float2 kSsaoDir[8] = { float2(1.0,0.0), float2(0.7071,0.7071), float2(0.0,1.0), float2(-0.7071,0.7071), float2(-1.0,0.0), float2(-0.7071,-0.7071), float2(0.0,-1.0), float2(0.7071,-0.7071) };\n");
    tb_str(&s, "static float ssaoLinZ(float d, constant FilterUniforms& u) { return u.ssaoProjB / (u.ssaoProjA + 2.0 * d - 1.0); }\n");
    tb_str(&s, "static float ssaoAO(depth2d<float> uDepthTex, sampler depSmp, constant FilterUniforms& u, float2 uv) {\n");
    tb_str(&s, "  float cd = uDepthTex.sample(depSmp, uv); if (cd >= 0.99999) return 0.0;\n");
    tb_str(&s, "  float cz = ssaoLinZ(cd, u); float occ = 0.0;\n");
    tb_str(&s, "  for (int i = 0; i < 8; ++i) {\n");
    tb_str(&s, "    float2 dir = kSsaoDir[i]; dir.x /= max(u.ssaoAspect, 0.001);\n");
    tb_str(&s, "    for (int sp = 1; sp <= 2; ++sp) {\n");
    tb_str(&s, "      float2 o = dir * u.ssaoRadius * float(sp);\n");
    tb_str(&s, "      float nz = ssaoLinZ(uDepthTex.sample(depSmp, uv + o), u); float diff = cz - nz;\n");
    tb_str(&s, "      if (diff > cz * 0.015 && diff < cz * 0.12) occ += 1.0 / float(sp);\n    }\n  }\n");
    tb_str(&s, "  return occ / 12.0;\n}\n");
    /* Main — orientation: sampleDst/fxaa/casSharpen use UNFLIPPED in.position
     * (scene + drawable are both top-left; self-consistent like GL's bottom-up);
     * bloom/vignette use the V-flipped vTexCoord; dither/rgb555 reconstruct GL's
     * bottom-left pixel index (fbH - position.y) for pixel-exact pattern parity. */
    tb_str(&s, "fragment float4 filterFragment(FVO in [[stage_in]], constant FilterUniforms& u [[buffer(1)]], texture2d<float> uTex [[texture(0)]], sampler colSmp [[sampler(0)]], depth2d<float> uDepthTex [[texture(1)]], sampler depSmp [[sampler(1)]]) {\n");
    tb_str(&s, "  float4 color = sampleDst(uTex, u, in.position.xy);\n");
    /* SSAO folded in right after sampling, before FXAA/bloom/grade (matches GL
     * main() :3020-3024). Depth sampled at the V-flipped vTexCoord (same visual
     * pixel as color); the 8 symmetric directions make it flip-invariant. */
    tb_str(&s, "  if (u.ssao == 1) { float ao = 1.0 - u.ssaoIntensity * ssaoAO(uDepthTex, depSmp, u, in.vTexCoord); color.rgb *= clamp(ao, 0.0, 1.0); }\n");
    tb_str(&s, "  if (u.applyPost == 1 && u.fxaa == 1) color.rgb = fxaa(uTex, u, in.position.xy, color.rgb);\n");
    tb_str(&s, "  if (u.applyPost == 1 && u.bloom == 1) {\n");
    tb_str(&s, "    float2 texel = 1.0 / u.srcSize; float3 bloom = float3(0.0); float wsum = 0.0; const int R = 3;\n");
    tb_str(&s, "    for (int y = -R; y <= R; ++y) for (int x = -R; x <= R; ++x) {\n");
    tb_str(&s, "      float2 o = float2(float(x), float(y)) * texel * 2.0; float3 sp = uTex.sample(colSmp, in.vTexCoord + o).rgb;\n");
    tb_str(&s, "      float l = dot(sp, float3(0.299, 0.587, 0.114)); float b = max(l - u.bloomThreshold, 0.0) / max(1.0 - u.bloomThreshold, 0.001);\n");
    tb_str(&s, "      float wv = exp(-float(x*x + y*y) / 6.0); bloom += sp * b * wv; wsum += wv;\n    }\n");
    tb_str(&s, "    bloom /= max(wsum, 0.001); color.rgb = clamp(color.rgb + bloom * u.bloomIntensity, 0.0, 1.0);\n  }\n");
    tb_str(&s, "  float3 rgb = clamp(color.rgb * u.colorScale + float3(u.colorBias / 255.0), 0.0, 1.0);\n");
    tb_str(&s, "  if (u.applyPost == 1) {\n");
    tb_str(&s, "    rgb += float3(u.brightness); float con = u.contrast * u.levelCon; rgb = (rgb - 0.5) * con + 0.5;\n");
    tb_str(&s, "    float luma = dot(rgb, float3(0.299, 0.587, 0.114)); float sat = u.saturation * u.levelSat; rgb = mix(float3(luma), rgb, sat);\n");
    tb_str(&s, "    rgb *= u.colorTint.rgb;\n");
    tb_str(&s, "    if (u.tonemap == 1) { float3 t = rgb / (rgb * 0.45 + 0.62); t = pow(t, float3(0.90)); rgb = mix(rgb, t, 0.5); }\n");
    tb_str(&s, "    rgb = clamp(rgb, 0.0, 1.0);\n  }\n");
    tb_str(&s, "  rgb = pow(rgb, float3(1.0 / max(u.gamma, 0.001)));\n");
    tb_str(&s, "  if (u.applyPost == 1 && u.vignette > 0.0) { float2 vc = in.vTexCoord - float2(0.5); float dd = dot(vc, vc) * 2.0; float vig = 1.0 - u.vignette * smoothstep(0.3, 1.0, dd); rgb *= vig; }\n");
    tb_str(&s, "  if (u.applyPost == 1 && u.sharpen > 0.0) rgb = casSharpen(uTex, u, in.position.xy, rgb);\n");
    tb_str(&s, "  if (u.applyPost == 1 && u.dither == 1) {\n");
    tb_str(&s, "    int dx = int(floor(in.position.x)) & 3; int dy = int(floor(float(u.fbH) - in.position.y)) & 3;\n");
    tb_str(&s, "    float t = kBayer4[dy * 4 + dx] - 0.5; rgb += float3(t / 255.0); rgb = clamp(rgb, 0.0, 1.0);\n  }\n");
    tb_str(&s, "  if (u.rgb555 != 0) {\n");
    tb_str(&s, "    float threshold = 0.5;\n");
    tb_str(&s, "    if (u.rgb555 == 2) { int dx = int(floor(in.position.x)) & 3; int dy = int(floor(float(u.fbH) - in.position.y)) & 3; threshold += kBayer4[dy * 4 + dx] - 0.5; }\n");
    tb_str(&s, "    rgb = floor(clamp(rgb, 0.0, 1.0) * 31.0 + threshold) / 31.0; rgb = clamp(rgb, 0.0, 1.0);\n  }\n");
    tb_str(&s, "  return float4(rgb, color.a);\n}\n");

    NSError *err = nil;
    NSString *nssrc = [NSString stringWithUTF8String:s.p];
    /* Prefer SAFE math so the rgb555/dither quantize boundaries stay bit-stable
     * vs GL (fast math may fuse/reassociate the floor(x*31+t) quantize). The
     * fastMathEnabled property is deprecated (macOS 15) — use mathMode where the
     * SDK supports it; older SDKs/runtimes keep the default (a sub-LSB nicety). */
    MTLCompileOptions *opts = [[MTLCompileOptions alloc] init];
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
    if (@available(macOS 15.0, *)) {
        opts.mathMode = MTLMathModeSafe;
    }
#endif
    s_filter_lib = [s_device newLibraryWithSource:nssrc options:opts error:&err];
    tb_free(&s);
    if (s_filter_lib == nil) {
        fprintf(stderr, "[metal] output-filter MSL compile failed: %s\n",
                err ? err.localizedDescription.UTF8String : "(no error)");
        return false;
    }
    id<MTLFunction> vfn = [s_filter_lib newFunctionWithName:@"filterVS"];
    id<MTLFunction> ffn = [s_filter_lib newFunctionWithName:@"filterFragment"];
    if (vfn == nil || ffn == nil) return false;

    MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.vertexFunction = vfn;
    pd.fragmentFunction = ffn;
    pd.vertexDescriptor = nil;                 /* fullscreen triangle via [[vertex_id]] */
    pd.rasterSampleCount = 1;
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pd.colorAttachments[0].blendingEnabled = NO;
    s_filter_pso = [s_device newRenderPipelineStateWithDescriptor:pd error:&err];
    if (s_filter_pso == nil) {
        fprintf(stderr, "[metal] output-filter PSO build failed: %s\n",
                err ? err.localizedDescription.UTF8String : "(no error)");
        return false;
    }
    MTLSamplerDescriptor *smp = [[MTLSamplerDescriptor alloc] init];
    smp.minFilter = MTLSamplerMinMagFilterLinear;
    smp.magFilter = MTLSamplerMinMagFilterLinear;
    smp.mipFilter = MTLSamplerMipFilterNotMipmapped;
    smp.sAddressMode = MTLSamplerAddressModeClampToEdge;
    smp.tAddressMode = MTLSamplerAddressModeClampToEdge;
    s_filter_smp = [s_device newSamplerStateWithDescriptor:smp];
    return s_filter_pso != nil && s_filter_smp != nil;
}

/* Fill the filter uniforms for one pass, mirroring the per-pass values in
 * gfx_opengl_draw_output_filter_texture:3176-3241. `apply_post` is GL's pass
 * argument (0 = pre-pass, 1 = final pass). colorScale/colorBias/gamma are set
 * regardless of apply_post (GL applies them in BOTH passes — see the 2-pass note
 * on mtl_run_output_filter); everything else is apply_post-gated exactly as GL.
 * SSAO fields are zeroed (Phase 5). src == dst == scene (mode 0). */
static void mtl_fill_filter_uniforms(struct MtlFilterUniforms *u, int apply_post) {
    memset(u, 0, sizeof *u);
    int gp = g_pcGradePresets ? 1 : 0;
    u->colorTint[0] = gp ? g_pcGradeLevelTintR : 1.0f;
    u->colorTint[1] = gp ? g_pcGradeLevelTintG : 1.0f;
    u->colorTint[2] = gp ? g_pcGradeLevelTintB : 1.0f;
    u->colorTint[3] = 1.0f;
    u->srcSize[0] = (float)s_fb_w; u->srcSize[1] = (float)s_fb_h;
    u->dstSize[0] = (float)s_fb_w; u->dstSize[1] = (float)s_fb_h;
    u->colorScale = mtl_diag_color_scale();
    u->colorBias = mtl_diag_color_bias();
    u->gamma = mtl_clampf(g_pcVideoGamma, 0.5f, 2.5f);
    u->saturation = mtl_clampf(g_pcVideoSaturation, 0.0f, 2.0f);
    u->contrast = mtl_clampf(g_pcVideoContrast, 0.5f, 2.0f);
    u->brightness = mtl_clampf(g_pcVideoBrightness, -0.5f, 0.5f);
    u->vignette = mtl_clampf(g_pcVignette, 0.0f, 1.0f);
    u->bloomThreshold = g_pcBloomThreshold;
    u->bloomIntensity = g_pcBloomIntensity;
    u->sharpen = apply_post ? mtl_clampf(g_pcSharpen, 0.0f, 1.0f) : 0.0f;
    u->levelSat = gp ? g_pcGradeLevelSat : 1.0f;
    u->levelCon = gp ? g_pcGradeLevelCon : 1.0f;
    u->applyPost = (apply_post && g_pcRemasterFX) ? 1 : 0;
    u->dither = g_pcOutputDither ? 1 : 0;   /* shader gates on applyPost */
    u->bloom = g_pcBloom ? 1 : 0;           /* shader gates on applyPost */
    /* SSAO (Phase 5): final pass only, gated like gfx_opengl.c:3201 (apply_post &&
     * Ssao && proj_b != 0; RemasterFX implied by apply_post). No MSAA clause —
     * Metal is single-sample here, so the GL "SSAO off under MSAA" limit is gone. */
    u->ssao = (apply_post && g_pcRemasterFX && g_pcSsao != 0 && g_pc_ssao_proj_b != 0.0f) ? 1 : 0;
    u->ssaoRadius = g_pcSsaoRadius * 0.02f;  /* radius key -> UV offset scale (load-bearing) */
    u->ssaoIntensity = g_pcSsaoIntensity;
    u->ssaoAspect = s_fb_h > 0 ? (float)s_fb_w / (float)s_fb_h : 1.0f;
    u->ssaoProjA = g_pc_ssao_proj_a;
    u->ssaoProjB = g_pc_ssao_proj_b;
    /* Mode 0 (bilinear) is the only mode used here; src == dst makes it an
     * identity fetch, and its centered kernel commutes with the top-left/
     * bottom-left flip so the unflipped in.position is byte-exact. Modes 1/2/3
     * (nearest / aspect-fit, for the deferred VI-downscale 2-pass) do NOT commute
     * with the flip — when that feature lands, feed them a bottom-left dst coord
     * (dstSize.y - position.y) and read the mirrored src row, or the letterbox
     * bar lands on the wrong edge and content shifts a row vs GL. */
    u->filterMode = 0;
    u->fxaa = (apply_post && g_pcFxaa) ? 1 : 0;
    u->tonemap = (apply_post && g_pcTonemap) ? 1 : 0;
    u->rgb555 = apply_post ? mtl_diag_rgb555_mode() : 0;
    u->fbH = s_fb_h;
}

/* Run the output filter as GL's TWO passes (gfx_opengl_apply_output_vi_filter is
 * a resolve pre-pass + a final pass even without VI downscale): pass 1 applies
 * colorScale/bias/gamma into an 8-bit intermediate (s_filter_low, apply_post=0);
 * pass 2 reads that and applies the full post-FX into s_final_color
 * (apply_post=1). Because colorScale/bias/gamma live OUTSIDE GL's apply_post
 * guard, GL applies each TWICE with an 8-bit round between — so Metal must too
 * or it diverges at non-default gamma / color-scale/bias. At the default gamma=1
 * (scale 1, bias 0) pass 1 is an identity 8-bit copy, so this stays byte-exact
 * with the prior single-pass. s_final_color is then blitted to the drawable AND
 * used as the readback source. Returns false if unavailable (caller blits raw). */
static bool mtl_run_output_filter(id<MTLCommandBuffer> cmdbuf) {
    if (!mtl_ensure_filter_program() || s_scene_color == nil ||
        s_filter_low == nil || s_final_color == nil) return false;
    struct MtlFilterUniforms u;
    id<MTLTexture> passes_src[2] = { s_scene_color, s_filter_low };
    id<MTLTexture> passes_dst[2] = { s_filter_low, s_final_color };
    for (int pass = 0; pass < 2; pass++) {
        MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
        rpd.colorAttachments[0].texture = passes_dst[pass];
        rpd.colorAttachments[0].loadAction = MTLLoadActionDontCare;  /* triangle covers all */
        rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> enc = [cmdbuf renderCommandEncoderWithDescriptor:rpd];
        [enc setRenderPipelineState:s_filter_pso];
        mtl_fill_filter_uniforms(&u, pass /* 0 = pre-pass, 1 = final */);
        [enc setFragmentBytes:&u length:sizeof u atIndex:1];
        [enc setFragmentTexture:passes_src[pass] atIndex:0];
        [enc setFragmentSamplerState:s_filter_smp atIndex:0];
        /* Scene depth for SSAO (final pass samples it when u.ssao==1; bound in
         * both passes so the declared depth2d arg is always satisfied). */
        [enc setFragmentTexture:s_scene_depth atIndex:1];
        [enc setFragmentSamplerState:s_depth_sampler atIndex:1];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [enc endEncoding];
    }
    return true;
}

static void mtl_end_frame(void) {
  @autoreleasepool {
    if (s_enc != nil) {
        [s_enc endEncoding];
        s_enc = nil;
    }
    if (s_cmdbuf == nil) return;
    /* Signal the frame throttle when THIS command buffer completes, and surface
     * GPU faults/device loss (otherwise silently swallowed). Balanced with the
     * start_frame wait (both gated on s_cmdbuf existing). If the readback path
     * split the frame, this is the final command buffer, and in-order completion
     * means all of the frame's buffers are done when it fires. */
    if (s_frame_sem != nil) {
        [s_cmdbuf addCompletedHandler:^(id<MTLCommandBuffer> cb) {
            if (cb.status == MTLCommandBufferStatusError) {
                fprintf(stderr, "[metal] command buffer error: %s\n",
                        cb.error ? cb.error.localizedDescription.UTF8String : "(unknown)");
            }
            dispatch_semaphore_signal(s_frame_sem);
        }];
    }
    /* CAMetalLayer can resize drawableSize async during a window resize or a drag
     * across displays of differing backing scale. Re-assert the render size so
     * the equal-size check below doesn't drop the present and freeze the screen. */
    if ((int)s_layer.drawableSize.width != s_fb_w || (int)s_layer.drawableSize.height != s_fb_h) {
        s_layer.drawableSize = CGSizeMake(s_fb_w, s_fb_h);
    }
    /* Output-VI-filter chain (Phase 4) when any post-FX is active; otherwise the
     * raw scene — which keeps the faithful/default path byte-identical (and
     * covers the case where the filter program failed to build). The presented
     * texture is also the readback source so screenshots match the display. */
    id<MTLTexture> present_src = s_scene_color;
    if (mtl_output_filter_active() && mtl_run_output_filter(s_cmdbuf)) {
        present_src = s_final_color;
    }
    s_readback_src = present_src;
    s_drawable = [s_layer nextDrawable];
    if (s_drawable != nil && present_src != nil &&
        (int)s_drawable.texture.width == s_fb_w && (int)s_drawable.texture.height == s_fb_h) {
        id<MTLBlitCommandEncoder> b = [s_cmdbuf blitCommandEncoder];
        [b copyFromTexture:present_src sourceSlice:0 sourceLevel:0
              sourceOrigin:MTLOriginMake(0, 0, 0)
                sourceSize:MTLSizeMake(s_fb_w, s_fb_h, 1)
                 toTexture:s_drawable.texture destinationSlice:0 destinationLevel:0
         destinationOrigin:MTLOriginMake(0, 0, 0)];
        [b endEncoding];
        [s_cmdbuf presentDrawable:s_drawable];
    }
    [s_cmdbuf commit];
    s_drawable = nil;
    s_cmdbuf = nil;
  }
}

static void mtl_finish_render(void) {
    /* Present is committed in end_frame; readback issues its own waited cmdbuf. */
}

static void mtl_on_resize(void) {
    /* Targets are re-created lazily in start_frame when drawableSize changes. */
}

/* ---- Non-vtable couplings (called directly from gfx_pc.c, backend-aware) --- */

extern "C" void gfx_metal_set_clear_color(float r, float g, float b) {
    s_clear_r = (double)r;
    s_clear_g = (double)g;
    s_clear_b = (double)b;
}

extern "C" int gfx_metal_max_offscreen_dim(void) {
    return 16384;  /* Apple GPU max 2D texture dimension; matches Apple GL's GL_MAX_TEXTURE_SIZE */
}

/* ---- Vtable: state setters (record only) + textures + draw ---------------- */

static bool mtl_z_is_from_0_to_1(void) { return true; }

static uint32_t mtl_new_texture(void) {
    static uint32_t next_id = 1;  /* nonzero, monotonic — 0 reads as "unset" */
    if (next_id == 0) next_id = 1;  /* skip the reserved 0 on the (2^32) wrap */
    return next_id++;
}

static void mtl_delete_texture(uint32_t texture_id) {
    if (texture_id != 0 && s_textures != nil) {
        [s_textures removeObjectForKey:@(texture_id)];
    }
}

static void mtl_select_texture(int tile, uint32_t texture_id) {
    if (tile >= 0 && tile < 2) s_tile_tex[tile] = texture_id;
    s_last_selected_id = texture_id;  /* upload target = most-recently selected id */
}

static bool mtl_upload_texture(const uint8_t *rgba32_buf, int width, int height) {
  @autoreleasepool {
    /* Reject >4096 to match gfx_opengl_upload_texture's guard: an oversized HD
     * asset must fall through to the frontend's native-texel fallback on BOTH
     * backends (gfx_pc.c:21067), or GL and Metal diverge. */
    if (rgba32_buf == NULL || width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        return false;
    }
    MTLTextureDescriptor *d =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:width height:height mipmapped:NO];
    d.usage = MTLTextureUsageShaderRead;
    d.storageMode = MTLStorageModeShared;
    id<MTLTexture> tex = [s_device newTextureWithDescriptor:d];
    if (tex == nil) return false;
    [tex replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0
             withBytes:rgba32_buf bytesPerRow:(NSUInteger)width * 4];
    if (s_last_selected_id != 0 && s_textures != nil) {
        s_textures[@(s_last_selected_id)] = tex;
    }
    return true;
  }
}

static void mtl_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt) {
    if (tile < 0 || tile >= 2) return;
    s_tile_linear[tile] = linear_filter;
    s_tile_cms[tile] = cms;
    s_tile_cmt[tile] = cmt;
}

static void mtl_set_depth_mode(bool depth_test, bool depth_update, bool depth_compare,
                               bool depth_source_prim, uint16_t zmode) {
    (void)depth_source_prim;
    s_depth_test = depth_test;
    s_depth_update = depth_update;
    s_depth_compare = depth_compare;
    s_zmode = zmode;
}

static void mtl_set_viewport(int x, int y, int width, int height) {
    s_vp_x = x;
    s_vp_y = y;
    s_vp_w = width;
    s_vp_h = height;
    static int dbg = -1;  /* cache like the other env checks (was a per-call getenv) */
    if (dbg < 0) dbg = getenv("GE007_METAL_DEBUG_VP") ? 1 : 0;
    if (dbg) {
        static int n = 0;
        if (n++ < 16)
            fprintf(stderr, "[metal-vp] set_viewport(%d,%d,%d,%d) fb=%dx%d flipY=%d\n",
                    x, y, width, height, s_fb_w, s_fb_h, s_fb_h - (y + height));
    }
}

static void mtl_set_scissor(int x, int y, int width, int height) {
    s_sc_x = x;
    s_sc_y = y;
    s_sc_w = width;
    s_sc_h = height;
    s_sc_set = true;
}

/* Mirror gfx_opengl.c:310-322 — default ON unless disabled by env. */
static bool mtl_room_cvg_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *d = getenv("GE007_DISABLE_ROOM_XLU_CVG_MEMORY");
        const char *e = getenv("GE007_ROOM_XLU_CVG_MEMORY");
        cached = 1;
        if ((d != NULL && d[0] != '\0' && d[0] != '0') || (e != NULL && e[0] == '0')) cached = 0;
    }
    return cached != 0;
}
/* Mirror gfx_opengl.c:300-308 — diag override (default off). */
static bool mtl_diag_cvg_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *e = getenv("GE007_DIAG_XLU_RDP_CVG_MEMORY_BLEND_CC");
        cached = (e != NULL && e[0] != '\0' && strcmp(e, "0") != 0) ? 1 : 0;
    }
    return cached != 0;
}

static void mtl_set_blend_mode(enum GfxBlendMode mode) {
    s_blend = mode;
    /* Coverage-alpha preservation (gfx_opengl.c:1692-1703): when the
     * coverage-memory feature is active, ordinary translucent draws must NOT
     * overwrite the 3-bit coverage the RDP-CVG-memory shader stores in the
     * scene-target alpha channel. Metal always renders to the sampleable
     * offscreen (== GL's scene target, which is bound-by-default because
     * room_xlu_cvg_memory defaults on, gfx_opengl.c:2277), so gate on the
     * feature + the same four blend modes GL masks. Resolved into the PSO
     * colorWriteMask at draw time. */
    s_preserve_cov_alpha =
        (mtl_room_cvg_enabled() || mtl_diag_cvg_enabled()) &&
        (mode == GFX_BLEND_ALPHA || mode == GFX_BLEND_MODULATE ||
         mode == GFX_BLEND_ALPHA_COVERAGE || mode == GFX_BLEND_ALPHA_CVG_WRAP_STENCIL);
}

/* Union of the batch's triangle screen rects in GL (bottom-left) viewport space,
 * ported from gfx_opengl_compute_batch_snapshot_rect. Only valid for the
 * coverage-memory path, whose VBO carries the flat diag-tri NDC at floats 4..9
 * of each triangle's first vertex (stride >= 10). Returns false if unusable. */
static bool mtl_rdp_batch_rect_cvg(const float *buf_vbo, size_t buf_vbo_len,
                                   size_t num_tris, int out[4]) {
    if (buf_vbo == NULL || num_tris == 0) return false;
    size_t vcount = num_tris * 3;
    if (vcount == 0 || buf_vbo_len % vcount != 0) return false;
    size_t stride = buf_vbo_len / vcount;
    if (stride < 10) return false;
    const int margin = 3;
    float min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    bool any = false;
    for (size_t tri = 0; tri < num_tris; tri++) {
        const float *base = &buf_vbo[tri * 3 * stride];
        for (int i = 0; i < 3; i++) {
            float ndc_x = base[4 + i * 2 + 0];
            float ndc_y = base[4 + i * 2 + 1];
            if (ndc_x != ndc_x || ndc_y != ndc_y ||
                ndc_x <= -100000.0f || ndc_x >= 100000.0f ||
                ndc_y <= -100000.0f || ndc_y >= 100000.0f) return false;
            float px = (float)s_vp_x + (ndc_x * 0.5f + 0.5f) * (float)s_vp_w;
            float py = (float)s_vp_y + (ndc_y * 0.5f + 0.5f) * (float)s_vp_h;
            if (!any) { min_x = max_x = px; min_y = max_y = py; any = true; }
            else {
                if (px < min_x) min_x = px; if (px > max_x) max_x = px;
                if (py < min_y) min_y = py; if (py > max_y) max_y = py;
            }
        }
    }
    if (!any) return false;
    /* floor(min)/ceil(max) via casts (avoid pulling <math.h> into this TU). */
    int ix0 = (int)min_x; if (min_x < (float)ix0) ix0--;
    int iy0 = (int)min_y; if (min_y < (float)iy0) iy0--;
    int ix1 = (int)max_x; if (max_x > (float)ix1) ix1++;
    int iy1 = (int)max_y; if (max_y > (float)iy1) iy1++;
    out[0] = ix0 - margin;
    out[1] = iy0 - margin;
    out[2] = ix1 + margin - out[0];
    out[3] = iy1 + margin - out[1];
    return out[2] > 0 && out[3] > 0;
}

static void mtl_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
  @autoreleasepool {
    if (s_enc == nil || s_cur_shader == nil || buf_vbo_num_tris == 0 || buf_vbo == NULL) return;
    MetalShader *ms = s_cur_shader;

    /* Scissor: GL(bottom-left) -> Metal(top-left) Y-flip + clamp to attachment.
     * Compute first so a degenerate/off-screen rect (inactive split-screen pane,
     * fully-clipped HUD element) early-outs — GL keeps GL_SCISSOR_TEST on and
     * discards every fragment, so skipping the whole draw is equivalent, avoids
     * leaking the previous draw's scissor on the persistent encoder, and skips
     * the wasted snapshot + vertex copy. */
    int sx = s_sc_set ? s_sc_x : 0;
    int sy = s_sc_set ? s_sc_y : 0;
    int sw = s_sc_set ? s_sc_w : s_fb_w;
    int sh = s_sc_set ? s_sc_h : s_fb_h;
    int myTop = s_fb_h - (sy + sh);
    if (myTop < 0) { sh += myTop; myTop = 0; }
    if (sx < 0) { sw += sx; sx = 0; }
    if (sx + sw > s_fb_w) sw = s_fb_w - sx;
    if (myTop + sh > s_fb_h) sh = s_fb_h - myTop;
    if (sw <= 0 || sh <= 0) return;

    /* RDP-memory / coverage-memory blend: the fragment samples the framebuffer
     * "memory" color it blends against. Metal can't sample the attachment being
     * written, so end the encoder, blit-copy the region the batch samples into
     * the SAME origin of the sampled snapshot, and resume with loadAction=Load
     * (§4 risk #1 — no LUS template). Copy only the batch rect (coverage) or the
     * viewport (plain memory blend) instead of the whole framebuffer. */
    bool rdp_mem = (ms->diagRdpMemory || ms->diagRdpCvgMemory) &&
                   (s_blend == GFX_BLEND_ALPHA_RDP_MEMORY ||
                    s_blend == GFX_BLEND_ALPHA_RDP_CVG_MEMORY);
    if (rdp_mem && s_snapshot_tex != nil) {
        int rect[4];
        int rx, ry, rw, rh;
        if (ms->diagRdpCvgMemory &&
            mtl_rdp_batch_rect_cvg(buf_vbo, buf_vbo_len, buf_vbo_num_tris, rect)) {
            rx = rect[0]; ry = rect[1]; rw = rect[2]; rh = rect[3];
        } else {
            rx = s_vp_x; ry = s_vp_y; rw = s_vp_w; rh = s_vp_h;
        }
        /* clamp to viewport (GL space) */
        int vx0 = s_vp_x, vy0 = s_vp_y, vx1 = s_vp_x + s_vp_w, vy1 = s_vp_y + s_vp_h;
        int cx0 = rx < vx0 ? vx0 : rx, cy0 = ry < vy0 ? vy0 : ry;
        int cx1 = (rx + rw) > vx1 ? vx1 : (rx + rw), cy1 = (ry + rh) > vy1 ? vy1 : (ry + rh);
        rx = cx0; ry = cy0; rw = cx1 - cx0; rh = cy1 - cy0;
        int top = s_fb_h - (ry + rh);   /* Y-flip */
        if (rx < 0) { rw += rx; rx = 0; }
        if (top < 0) { rh += top; top = 0; }
        if (rx + rw > s_fb_w) rw = s_fb_w - rx;
        if (top + rh > s_fb_h) rh = s_fb_h - top;
        [s_enc endEncoding];
        if (rw > 0 && rh > 0) {
            id<MTLBlitCommandEncoder> b = [s_cmdbuf blitCommandEncoder];
            [b copyFromTexture:s_scene_color sourceSlice:0 sourceLevel:0
                  sourceOrigin:MTLOriginMake(rx, top, 0) sourceSize:MTLSizeMake(rw, rh, 1)
                     toTexture:s_snapshot_tex destinationSlice:0 destinationLevel:0
             destinationOrigin:MTLOriginMake(rx, top, 0)];
            [b endEncoding];
        }
        mtl_open_scene_encoder(false);
    }

    mtl_build_vertex_descriptor(ms);

    id<MTLRenderPipelineState> pso = mtl_pso_for(ms, s_blend, 1, !s_preserve_cov_alpha);
    [s_enc setRenderPipelineState:pso];
    [s_enc setDepthStencilState:mtl_depth_state_for(s_depth_test, s_depth_update, s_depth_compare, s_zmode)];
    /* ZMODE_DEC decal polygon offset (gfx_opengl.c:1582-1595, factor/units -2). */
    if (s_zmode == 0xc00 && s_depth_test && s_depth_compare) {
        [s_enc setDepthBias:-7.5e-6f slopeScale:-2.0f clamp:0.0f];
    } else {
        [s_enc setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];
    }

    /* Viewport: Y-flip + non-negative clamp (0 is legal and reproduces GL's
     * collapse; a negative extent would abort). Unconditional so a zero-extent
     * viewport doesn't leak the previous draw's viewport. */
    MTLViewport vp;
    vp.originX = s_vp_x;
    vp.originY = s_fb_h - (s_vp_y + s_vp_h);
    vp.width = s_vp_w > 0 ? (double)s_vp_w : 0.0;
    vp.height = s_vp_h > 0 ? (double)s_vp_h : 0.0;
    vp.znear = 0.0;
    vp.zfar = 1.0;
    [s_enc setViewport:vp];

    MTLScissorRect sc = {(NSUInteger)sx, (NSUInteger)myTop, (NSUInteger)sw, (NSUInteger)sh};
    [s_enc setScissorRect:sc];

    /* Vertex data: bump-allocate from the frame's ring slot (StorageModeShared);
     * fall back to a one-off buffer only if the batch overflows the slot. */
    size_t need = buf_vbo_len * sizeof(float);
    id<MTLBuffer> vb;
    NSUInteger voff;
    size_t aligned = (s_vbuf_cursor + 255u) & ~(size_t)255u;
    if (s_vbuf[s_vbuf_slot] != nil && aligned + need <= s_vbuf_cap[s_vbuf_slot]) {
        vb = s_vbuf[s_vbuf_slot];
        voff = (NSUInteger)aligned;
        memcpy((uint8_t *)vb.contents + aligned, buf_vbo, need);
        s_vbuf_cursor = aligned + need;
        if (s_vbuf_cursor > s_vbuf_want) s_vbuf_want = s_vbuf_cursor;  /* grow target for next (re)alloc */
    } else {
        if (aligned + need > s_vbuf_want) s_vbuf_want = aligned + need;
        vb = [s_device newBufferWithBytes:buf_vbo length:need options:MTLResourceStorageModeShared];
        voff = 0;
    }
    if (vb == nil) return;  /* transient OOM: drop this batch rather than GPU-fault */
    [s_enc setVertexBuffer:vb offset:voff atIndex:0];

    MtlUniforms u;
    memset(&u, 0, sizeof u);
    u.n64FilterScale[0] = mtl_axis_filter_scale(gfx_current_dimensions.width, viGetX(), DESIRED_SCREEN_WIDTH);
    u.n64FilterScale[1] = mtl_axis_filter_scale(gfx_current_dimensions.height, viGetY(), DESIRED_SCREEN_HEIGHT);
    u.frameCount = (int)s_frame_count_metal;
    u.winH = s_vp_h > 0 ? s_vp_h : s_fb_h;   /* GL current_height = viewport height (noise scale) */
    u.fbH = s_fb_h;                          /* attachment height (fragcoord flip) */
    /* RDP diag uniforms: full-fb snapshot -> origin 0 (memoryUv = in.position/fb);
     * diagViewport is the GL(bottom-left) viewport for the coverage NDC
     * reconstruction, which pairs with the GL-oriented flipped fragCoord. */
    u.diagFbOrigin[0] = 0.0f;
    u.diagFbOrigin[1] = 0.0f;
    u.diagViewport[0] = (float)s_vp_x;
    u.diagViewport[1] = (float)s_vp_y;
    u.diagViewport[2] = (float)s_vp_w;
    u.diagViewport[3] = (float)s_vp_h;
    [s_enc setFragmentBytes:&u length:sizeof u atIndex:1];

    for (int t = 0; t < 2; t++) {
        if (!ms->usedTextures[t]) continue;
        id<MTLTexture> tex = mtl_lookup_tex(s_tile_tex[t]);
        if (tex == nil) tex = s_white_tex;
        [s_enc setFragmentTexture:tex atIndex:t];
        [s_enc setFragmentSamplerState:mtl_sampler_for(s_tile_linear[t], s_tile_cms[t], s_tile_cmt[t]) atIndex:t];
    }
    if (rdp_mem) {
        [s_enc setFragmentTexture:s_snapshot_tex atIndex:2];
        [s_enc setFragmentSamplerState:s_snapshot_sampler atIndex:2];
    }

    [s_enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3 * buf_vbo_num_tris];
  }
}

/* ---- readback (3.4): blit offscreen -> shared buffer, wait, RGB out -------
 * Returns bytes in GL's convention (bottom-left origin, row 0 = bottom) so the
 * existing screenshot/probe consumers (calibrated to glReadPixels) are
 * unchanged. Source region is Y-flipped into the top-left scene texture. */
static bool mtl_read_framebuffer_rgb(int x, int y, int width, int height, uint8_t *rgb_out) {
  @autoreleasepool {
    if (rgb_out == NULL || width <= 0 || height <= 0 || s_scene_color == nil || s_device == nil) {
        return false;
    }
    /* Source selection matches GL: a MID-frame read (GE007_SCREENSHOT / diag
     * pixel probes, encoder still open) sees the current scene BEFORE the
     * output filter (GL's probes read the scene FBO mid-render), so read
     * s_scene_color; a BETWEEN-frame read (main screenshot, encoder closed)
     * sees the last PRESENTED image (GL reads its post-filter GL_FRONT), so read
     * s_readback_src (== s_final_color when the filter ran, else s_scene_color). */
    bool mid_frame = (s_enc != nil && s_cmdbuf != nil);
    id<MTLTexture> src_tex = mid_frame ? s_scene_color
                                       : (s_readback_src != nil ? s_readback_src : s_scene_color);
    if (mid_frame) {
        /* Flush the in-flight draws so the readback sees THIS frame's content,
         * then resume the frame (Load-preserved). Metal serializes command
         * buffers by commit order, so without this the read would capture the
         * previous committed frame. */
        [s_enc endEncoding];
        s_enc = nil;
        [s_cmdbuf commit];
        [s_cmdbuf waitUntilCompleted];
        s_cmdbuf = [s_queue commandBuffer];
        mtl_open_scene_encoder(false);
    }
    if (src_tex == nil) return false;
    /* Clamp the requested rect to the attachment. */
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > s_fb_w) width = s_fb_w - x;
    if (y + height > s_fb_h) height = s_fb_h - y;
    if (width <= 0 || height <= 0) return false;

    int src_top = s_fb_h - (y + height);   /* GL bottom-left y -> Metal top-left origin */
    if (src_top < 0) src_top = 0;

    NSUInteger bpr = (NSUInteger)width * 4;
    id<MTLBuffer> buf = [s_device newBufferWithLength:bpr * (NSUInteger)height
                                             options:MTLResourceStorageModeShared];
    if (buf == nil) return false;

    id<MTLCommandBuffer> cb = [s_queue commandBuffer];
    id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
    [b copyFromTexture:src_tex sourceSlice:0 sourceLevel:0
          sourceOrigin:MTLOriginMake(x, src_top, 0)
            sourceSize:MTLSizeMake(width, height, 1)
              toBuffer:buf destinationOffset:0
     destinationBytesPerRow:bpr destinationBytesPerImage:bpr * (NSUInteger)height];
    [b endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    if (cb.status != MTLCommandBufferStatusCompleted) {
        /* A failed blit leaves buf.contents undefined; report failure so the
         * probe/screenshot consumers match GL's `return err == GL_NO_ERROR`. */
        fprintf(stderr, "[metal] readback command buffer failed: %s\n",
                cb.error ? cb.error.localizedDescription.UTF8String : "(unknown)");
        return false;
    }

    const uint8_t *src = (const uint8_t *)buf.contents;  /* BGRA, top-to-bottom */
    for (int row = 0; row < height; row++) {
        /* rgb_out row r is GL bottom-up; Metal buffer row 0 = top of region. */
        const uint8_t *srow = src + (size_t)(height - 1 - row) * bpr;
        uint8_t *drow = rgb_out + (size_t)row * (size_t)width * 3;
        for (int col = 0; col < width; col++) {
            drow[col * 3 + 0] = srow[col * 4 + 2];  /* R (BGRA -> RGB) */
            drow[col * 3 + 1] = srow[col * 4 + 1];  /* G */
            drow[col * 3 + 2] = srow[col * 4 + 0];  /* B */
        }
    }
    return true;
  }
}

/* Positional init MUST match the field order in gfx_rendering_api.h. C linkage
 * so gfx_pc.c's `extern struct GfxRenderingAPI gfx_metal_api;` resolves. */
extern "C" struct GfxRenderingAPI gfx_metal_api = {
    mtl_z_is_from_0_to_1,
    mtl_unload_shader,
    mtl_load_shader,
    mtl_create_and_load_new_shader,
    mtl_lookup_shader,
    mtl_shader_get_info,
    mtl_new_texture,
    mtl_delete_texture,
    mtl_select_texture,
    mtl_upload_texture,
    mtl_set_sampler_parameters,
    mtl_set_depth_mode,
    mtl_set_viewport,
    mtl_set_scissor,
    mtl_set_blend_mode,
    mtl_draw_triangles,
    mtl_read_framebuffer_rgb,
    mtl_init,
    mtl_on_resize,
    mtl_start_frame,
    mtl_end_frame,
    mtl_finish_render,
};
