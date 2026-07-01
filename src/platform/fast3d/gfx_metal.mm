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
 * Phase 1 (bring-up): device/queue/CAMetalLayer + per-frame clear + present.
 * Phase 2 (this file): combiner -> MSL shader translation + MTLLibrary compile.
 * Geometry/textures/blend/readback land in Phase 3.
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
    b->cap = cap;
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
    bool usedNoise;
    bool usedN64Filter;
    bool diagRdpMemory;
    bool diagRdpCvgMemory;
    MtlAttr attrs[32];
    int numAttrs;
};

/* Heap-allocated, never moved — ARC manages the __strong members. Lives for the
 * process (like GL's shader_program_pool[256]). */
#define MTL_MAX_SHADERS 1024
static MetalShader *s_shaders[MTL_MAX_SHADERS];
static int s_shader_count = 0;
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
        tb_str(s, "    float2 memoryUv = (fragCoord - u.diagFbOrigin) / float2(uDiagFramebuffer.get_width(), uDiagFramebuffer.get_height());\n");
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
        tb_str(s, "    float2 memoryUv = (fragCoord - u.diagFbOrigin) / float2(uDiagFramebuffer.get_width(), uDiagFramebuffer.get_height());\n");
        tb_str(s, "    float4 memoryColor = uDiagFramebuffer.sample(smpDiag, clamp(memoryUv, float2(0.0), float2(1.0)));\n");
        tb_str(s, "    float pixelAlphaByte = floor(clamp(texel.a, 0.0, 1.0) * 255.0 + 0.5);\n");
        tb_str(s, "    float a0 = floor(pixelAlphaByte / 8.0);\n");
        tb_str(s, "    float a1 = floor((255.0 - pixelAlphaByte) / 8.0);\n");
        tb_str(s, "    float3 pixelByte = floor(clamp(texel.rgb, 0.0, 1.0) * 255.0 + 0.5);\n");
        tb_str(s, "    float3 memoryByte = floor(clamp(memoryColor.rgb, 0.0, 1.0) * 255.0 + 0.5);\n");
        tb_str(s, "    float3 blendedByte = floor((pixelByte * a0 + memoryByte * (a1 + 1.0)) / 32.0);\n");
        tb_str(s, "    texel = float4(clamp(blendedByte / 255.0, 0.0, 1.0), 1.0);\n");
    }

    if (cc.opt_alpha) tb_str(s, "    return texel;\n}\n");
    else              tb_str(s, "    return float4(texel, 1.0);\n}\n");
}

static struct ShaderProgram *mtl_create_and_load_new_shader(uint64_t id0, uint32_t id1) {
    MetalShader *ms = new MetalShader();
    ms->id0 = id0;
    ms->id1 = id1;
    gfx_cc_get_features(id0, id1, &ms->cc);
    ms->numInputs = ms->cc.num_inputs;
    ms->usedTextures[0] = ms->cc.used_textures[0];
    ms->usedTextures[1] = ms->cc.used_textures[1];
    ms->usedN64Filter = ms->cc.n64_filter[0] || ms->cc.n64_filter[1];
    ms->diagRdpMemory = ms->cc.opt_alpha && ms->cc.diag_rdp_memory_blend;
    ms->diagRdpCvgMemory = ms->cc.opt_alpha && ms->cc.diag_rdp_cvg_memory_blend;
    ms->usedNoise = ms->cc.opt_noise;
    for (int ci = 0; ci < 2 && !ms->usedNoise; ci++)
        for (int cj = 0; cj < 2 && !ms->usedNoise; cj++)
            for (int ck = 0; ck < 4 && !ms->usedNoise; ck++)
                if (ms->cc.c[ci][cj][ck] == SHADER_NOISE) ms->usedNoise = true;

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

    if (s_shader_count < MTL_MAX_SHADERS) {
        s_shaders[s_shader_count++] = ms;
    } else {
        fprintf(stderr, "[metal] WARNING: shader pool full (%d) — leaking new shader\n", MTL_MAX_SHADERS);
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
static int s_fb_w = 0, s_fb_h = 0;
static id<MTLRenderCommandEncoder> s_enc = nil;
static id<MTLTexture> s_white_tex = nil;   /* 1x1 white for unbound tiles */

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

/* Texture registry (id -> MTLTexture) + pipeline/depth/sampler caches. */
static NSMutableDictionary<NSNumber *, id<MTLTexture>> *s_textures = nil;
static NSMutableDictionary<NSNumber *, id<MTLDepthStencilState>> *s_depth_cache = nil;
static NSMutableDictionary<NSNumber *, id<MTLSamplerState>> *s_sampler_cache = nil;

static id<MTLTexture> mtl_lookup_tex(uint32_t id) {
    if (id == 0 || s_textures == nil) return nil;
    return s_textures[@(id)];
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

static void mtl_ensure_targets(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (s_scene_color != nil && s_fb_w == w && s_fb_h == h) return;
    MTLTextureDescriptor *cd =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:w height:h mipmapped:NO];
    cd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    cd.storageMode = MTLStorageModePrivate;
    s_scene_color = [s_device newTextureWithDescriptor:cd];
    MTLTextureDescriptor *dd =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                           width:w height:h mipmapped:NO];
    dd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    dd.storageMode = MTLStorageModePrivate;
    s_scene_depth = [s_device newTextureWithDescriptor:dd];
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
    s_white_tex = [s_device newTextureWithDescriptor:d];
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
    NSNumber *k = @(key);
    id<MTLRenderPipelineState> pso = ms->psoCache[k];
    if (pso != nil) return pso;

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
    return pso;
}

static id<MTLDepthStencilState> mtl_depth_state_for(bool test, bool update, bool compare, uint16_t zmode) {
    uint64_t key = (test ? 1 : 0) | (update ? 2 : 0) | (compare ? 4 : 0) | ((uint64_t)zmode << 3);
    NSNumber *k = @(key);
    id<MTLDepthStencilState> ds = s_depth_cache[k];
    if (ds != nil) return ds;
    MTLDepthStencilDescriptor *dd = [[MTLDepthStencilDescriptor alloc] init];
    /* GL: test+compare -> LEQUAL (all live zmodes); test+!compare -> ALWAYS
     * (write-only); !test -> disabled (ALWAYS, no write). */
    dd.depthCompareFunction = (test && compare) ? MTLCompareFunctionLessEqual
                                                : MTLCompareFunctionAlways;
    dd.depthWriteEnabled = (test && update);
    ds = [s_device newDepthStencilStateWithDescriptor:dd];
    s_depth_cache[k] = ds;
    return ds;
}

static id<MTLSamplerState> mtl_sampler_for(bool linear, uint32_t cms, uint32_t cmt) {
    uint64_t key = (linear ? 1 : 0) | ((uint64_t)(cms & 0xFFFF) << 1) | ((uint64_t)(cmt & 0xFFFF) << 17);
    NSNumber *k = @(key);
    id<MTLSamplerState> s = s_sampler_cache[k];
    if (s != nil) return s;
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
    return s;
}

/* ---- init / frame lifecycle ---------------------------------------------- */

static void mtl_init(void) {
    s_device = MTLCreateSystemDefaultDevice();
    if (s_device == nil) {
        fprintf(stderr, "[metal] FATAL: MTLCreateSystemDefaultDevice returned nil\n");
        return;
    }
    s_queue = [s_device newCommandQueue];
    s_layer = (__bridge CAMetalLayer *)platformGetMetalLayer();
    if (s_layer != nil) {
        s_layer.device = s_device;
        s_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        /* NO: the scene is composited offscreen and blitted into the drawable at
         * end_frame; a framebufferOnly drawable can't be a blit destination. */
        s_layer.framebufferOnly = NO;
    } else {
        fprintf(stderr, "[metal] WARNING: no CAMetalLayer from platform — present will be skipped\n");
    }
    s_textures = [NSMutableDictionary dictionary];
    s_depth_cache = [NSMutableDictionary dictionary];
    s_sampler_cache = [NSMutableDictionary dictionary];
    /* Invariance-critical (parent plan §2.1): the CPU clipper reads
     * g_depth_clamp_enabled; Metal uses native depth-clamp, so force it true. */
    g_depth_clamp_enabled = true;
    fprintf(stderr, "[metal] native Metal backend init: device='%s' layer=%p\n",
            s_device.name.UTF8String, (__bridge void *)s_layer);
}

static void mtl_start_frame(void) {
    if (s_layer == nil || s_queue == nil) return;
    s_frame_count_metal++;
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

    s_cmdbuf = [s_queue commandBuffer];
    MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    rpd.colorAttachments[0].texture = s_scene_color;
    rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpd.colorAttachments[0].clearColor = MTLClearColorMake(s_clear_r, s_clear_g, s_clear_b, 1.0);
    rpd.depthAttachment.texture = s_scene_depth;
    rpd.depthAttachment.loadAction = MTLLoadActionClear;
    rpd.depthAttachment.storeAction = MTLStoreActionStore;
    rpd.depthAttachment.clearDepth = 1.0;
    s_enc = [s_cmdbuf renderCommandEncoderWithDescriptor:rpd];
    /* Native depth-clamp (equivalent to GL_DEPTH_CLAMP; matches g_depth_clamp). */
    [s_enc setDepthClipMode:MTLDepthClipModeClamp];

    if (!s_logged_first_frame) {
        fprintf(stderr, "[metal] first frame: scene %dx%d, geometry encoder open\n", s_fb_w, s_fb_h);
        s_logged_first_frame = true;
    }
}

static void mtl_end_frame(void) {
    if (s_enc != nil) {
        [s_enc endEncoding];
        s_enc = nil;
    }
    if (s_cmdbuf == nil) return;
    s_drawable = [s_layer nextDrawable];
    if (s_drawable != nil && s_scene_color != nil &&
        (int)s_drawable.texture.width == s_fb_w && (int)s_drawable.texture.height == s_fb_h) {
        id<MTLBlitCommandEncoder> b = [s_cmdbuf blitCommandEncoder];
        [b copyFromTexture:s_scene_color sourceSlice:0 sourceLevel:0
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
    if (rgba32_buf == NULL || width <= 0 || height <= 0 || width > 16384 || height > 16384) {
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
    if (getenv("GE007_METAL_DEBUG_VP")) {
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

static void mtl_set_blend_mode(enum GfxBlendMode mode) {
    s_blend = mode;
    /* Coverage-alpha preservation (gfx_opengl.c:1692-1703) is gated on the
     * default-off room/diag coverage-memory features, so alpha stays writable
     * on the faithful base path. Wired fully with the RDP path (3.3). */
    s_preserve_cov_alpha = false;
}

static void mtl_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    if (s_enc == nil || s_cur_shader == nil || buf_vbo_num_tris == 0 || buf_vbo == NULL) return;
    MetalShader *ms = s_cur_shader;
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

    /* Viewport / scissor with GL(bottom-left) -> Metal(top-left) Y-flip. */
    MTLViewport vp;
    vp.originX = s_vp_x;
    vp.originY = s_fb_h - (s_vp_y + s_vp_h);
    vp.width = s_vp_w;
    vp.height = s_vp_h;
    vp.znear = 0.0;
    vp.zfar = 1.0;
    if (vp.width > 0 && vp.height > 0) [s_enc setViewport:vp];

    int sx = s_sc_set ? s_sc_x : 0;
    int sy = s_sc_set ? s_sc_y : 0;
    int sw = s_sc_set ? s_sc_w : s_fb_w;
    int sh = s_sc_set ? s_sc_h : s_fb_h;
    int myTop = s_fb_h - (sy + sh);         /* flip */
    if (myTop < 0) { sh += myTop; myTop = 0; }
    if (sx < 0) { sw += sx; sx = 0; }
    if (sx + sw > s_fb_w) sw = s_fb_w - sx;
    if (myTop + sh > s_fb_h) sh = s_fb_h - myTop;
    if (sw > 0 && sh > 0) {
        MTLScissorRect sc = {(NSUInteger)sx, (NSUInteger)myTop, (NSUInteger)sw, (NSUInteger)sh};
        [s_enc setScissorRect:sc];
    }

    /* Per-draw vertex buffer (correctness-first; a ring pool is a later perf
     * pass — parity, not fps, is the Phase-3 goal). */
    id<MTLBuffer> vb = [s_device newBufferWithBytes:buf_vbo
                                             length:buf_vbo_len * sizeof(float)
                                            options:MTLResourceStorageModeShared];
    [s_enc setVertexBuffer:vb offset:0 atIndex:0];

    MtlUniforms u;
    memset(&u, 0, sizeof u);
    u.n64FilterScale[0] = mtl_axis_filter_scale(gfx_current_dimensions.width, viGetX(), DESIRED_SCREEN_WIDTH);
    u.n64FilterScale[1] = mtl_axis_filter_scale(gfx_current_dimensions.height, viGetY(), DESIRED_SCREEN_HEIGHT);
    u.frameCount = (int)s_frame_count_metal;
    u.winH = s_vp_h > 0 ? s_vp_h : s_fb_h;   /* GL current_height = viewport height (noise scale) */
    u.fbH = s_fb_h;                          /* attachment height (fragcoord flip) */
    [s_enc setFragmentBytes:&u length:sizeof u atIndex:1];

    for (int t = 0; t < 2; t++) {
        if (!ms->usedTextures[t]) continue;
        id<MTLTexture> tex = mtl_lookup_tex(s_tile_tex[t]);
        if (tex == nil) tex = s_white_tex;
        [s_enc setFragmentTexture:tex atIndex:t];
        [s_enc setFragmentSamplerState:mtl_sampler_for(s_tile_linear[t], s_tile_cms[t], s_tile_cmt[t]) atIndex:t];
    }

    [s_enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3 * buf_vbo_num_tris];
}

/* ---- readback (3.4): blit offscreen -> shared buffer, wait, RGB out -------
 * Returns bytes in GL's convention (bottom-left origin, row 0 = bottom) so the
 * existing screenshot/probe consumers (calibrated to glReadPixels) are
 * unchanged. Source region is Y-flipped into the top-left scene texture. */
static bool mtl_read_framebuffer_rgb(int x, int y, int width, int height, uint8_t *rgb_out) {
    if (rgb_out == NULL || width <= 0 || height <= 0 || s_scene_color == nil || s_device == nil) {
        return false;
    }
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
    [b copyFromTexture:s_scene_color sourceSlice:0 sourceLevel:0
          sourceOrigin:MTLOriginMake(x, src_top, 0)
            sourceSize:MTLSizeMake(width, height, 1)
              toBuffer:buf destinationOffset:0
     destinationBytesPerRow:bpr destinationBytesPerImage:bpr * (NSUInteger)height];
    [b endEncoding];
    [cb commit];
    [cb waitUntilCompleted];

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
