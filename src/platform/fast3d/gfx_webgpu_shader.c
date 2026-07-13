/*
 * gfx_webgpu_shader.c — WGSL combiner-shader emitter (see gfx_webgpu_shader.h).
 *
 * Ports gfx_opengl.c's GLSL emitter (gfx_opengl_create_and_load_new_shader) and
 * gfx_metal.mm's MSL port to WGSL. The combiner mux logic (shader_item_to_str /
 * append_formula) is reproduced verbatim in WGSL syntax; the vertex-attribute
 * walk is the same CCFeatures order all three backends use, returned via
 * WgpuShaderInfo so the pipeline's WGPUVertexBufferLayout matches buf_vbo.
 *
 * Task 3 scope: the faithful CORE combiner — 1-/2-cycle formula, tex0/tex1
 * sampling, shade/prim/env inputs, fog, alpha, texture-edge cutout — which
 * covers the vast majority of scene geometry ("recognizable textured geometry").
 * The option flags that only ADD fragment effects (n64 3-point filter, tile
 * mask, shader-side clamp, sun-shadow PCF, per-pixel dfdx sun, the diag_*
 * paths, frame-varying noise) are deferred to Task 4 (parity); their VERTEX
 * ATTRIBUTES are still walked here so the stride stays exact, and their varyings
 * are still passed through — only the fragment EFFECT is omitted for now.
 */
#include "gfx_webgpu_shader.h"

#include "gfx_cc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* WGSL type for an N-float attribute/varying. */
static const char *type_str(int size) {
    switch (size) {
        case 1:  return "f32";
        case 2:  return "vec2<f32>";
        case 3:  return "vec3<f32>";
        default: return "vec4<f32>";
    }
}

/* Combiner item -> WGSL expression. Mirrors gfx_opengl.c shader_item_to_str;
 * varyings are struct fields, so inputs read `in.vInputN`. `inputs_have_alpha`
 * is opt_alpha (whether the vInput* are vec4 vs vec3). */
static const char *wgsl_item(uint32_t item, bool with_alpha, bool only_alpha,
                             bool inputs_have_alpha, bool hint_single) {
    if (!only_alpha) {
        switch (item) {
            case SHADER_0:       return with_alpha ? "vec4<f32>(0.0)" : "vec3<f32>(0.0)";
            case SHADER_INPUT_1: return (with_alpha || !inputs_have_alpha) ? "in.vInput1" : "in.vInput1.rgb";
            case SHADER_INPUT_2: return (with_alpha || !inputs_have_alpha) ? "in.vInput2" : "in.vInput2.rgb";
            case SHADER_INPUT_3: return (with_alpha || !inputs_have_alpha) ? "in.vInput3" : "in.vInput3.rgb";
            case SHADER_INPUT_4: return (with_alpha || !inputs_have_alpha) ? "in.vInput4" : "in.vInput4.rgb";
            case SHADER_INPUT_5: return (with_alpha || !inputs_have_alpha) ? "in.vInput5" : "in.vInput5.rgb";
            case SHADER_INPUT_6: return (with_alpha || !inputs_have_alpha) ? "in.vInput6" : "in.vInput6.rgb";
            case SHADER_INPUT_7: return (with_alpha || !inputs_have_alpha) ? "in.vInput7" : "in.vInput7.rgb";
            case SHADER_TEXEL0:  return with_alpha ? "texVal0" : "texVal0.rgb";
            case SHADER_TEXEL0A: return hint_single ? "texVal0.a"
                                     : (with_alpha ? "vec4<f32>(texVal0.a)" : "vec3<f32>(texVal0.a)");
            case SHADER_TEXEL1:  return with_alpha ? "texVal1" : "texVal1.rgb";
            case SHADER_TEXEL1A: return hint_single ? "texVal1.a"
                                     : (with_alpha ? "vec4<f32>(texVal1.a)" : "vec3<f32>(texVal1.a)");
            case SHADER_1:       return with_alpha ? "vec4<f32>(1.0)" : "vec3<f32>(1.0)";
            case SHADER_COMBINED:return with_alpha ? "texel" : "texel.rgb";
            case SHADER_NOISE:   return with_alpha ? "vec4<f32>(wgpu_noise(in.clip_pos.xy))"
                                                   : "vec3<f32>(wgpu_noise(in.clip_pos.xy))";
        }
    } else {
        switch (item) {
            case SHADER_0:       return "0.0";
            case SHADER_INPUT_1: return "in.vInput1.a";
            case SHADER_INPUT_2: return "in.vInput2.a";
            case SHADER_INPUT_3: return "in.vInput3.a";
            case SHADER_INPUT_4: return "in.vInput4.a";
            case SHADER_INPUT_5: return "in.vInput5.a";
            case SHADER_INPUT_6: return "in.vInput6.a";
            case SHADER_INPUT_7: return "in.vInput7.a";
            case SHADER_TEXEL0:  return "texVal0.a";
            case SHADER_TEXEL0A: return "texVal0.a";
            case SHADER_TEXEL1:  return "texVal1.a";
            case SHADER_TEXEL1A: return "texVal1.a";
            case SHADER_1:       return "1.0";
            case SHADER_COMBINED:return "texel.a";
            case SHADER_NOISE:   return "wgpu_noise(in.clip_pos.xy)";
        }
    }
    return "0.0";
}

/* append_formula port: single / multiply / mix(lerp3) / lerp4, identical
 * structure to gfx_opengl.c append_formula. */
static void wgsl_formula(char *buf, size_t cap, size_t *n, uint8_t c[2][4],
                         bool do_single, bool do_multiply, bool do_mix,
                         bool with_alpha, bool only_alpha, bool opt_alpha) {
    int oa = only_alpha ? 1 : 0;
    if (do_single) {
        *n += (size_t)snprintf(buf + *n, cap - *n, "%s",
                               wgsl_item(c[oa][3], with_alpha, only_alpha, opt_alpha, false));
    } else if (do_multiply) {
        *n += (size_t)snprintf(buf + *n, cap - *n, "%s * %s",
                               wgsl_item(c[oa][0], with_alpha, only_alpha, opt_alpha, false),
                               wgsl_item(c[oa][2], with_alpha, only_alpha, opt_alpha, true));
    } else if (do_mix) {
        *n += (size_t)snprintf(buf + *n, cap - *n, "mix(%s, %s, %s)",
                               wgsl_item(c[oa][1], with_alpha, only_alpha, opt_alpha, false),
                               wgsl_item(c[oa][0], with_alpha, only_alpha, opt_alpha, false),
                               wgsl_item(c[oa][2], with_alpha, only_alpha, opt_alpha, true));
    } else {
        *n += (size_t)snprintf(buf + *n, cap - *n, "(%s - %s) * %s + %s",
                               wgsl_item(c[oa][0], with_alpha, only_alpha, opt_alpha, false),
                               wgsl_item(c[oa][1], with_alpha, only_alpha, opt_alpha, false),
                               wgsl_item(c[oa][2], with_alpha, only_alpha, opt_alpha, true),
                               wgsl_item(c[oa][3], with_alpha, only_alpha, opt_alpha, false));
    }
}

#define MODULE_CAP (48 * 1024)

char *gfx_webgpu_build_wgsl(uint64_t shader_id0, uint32_t shader_id1,
                            struct WgpuShaderInfo *info) {
    struct CCFeatures cc;
    gfx_cc_get_features(shader_id0, shader_id1, &cc);

    memset(info, 0, sizeof(*info));
    info->num_inputs = cc.num_inputs;
    info->used_textures[0] = cc.used_textures[0];
    info->used_textures[1] = cc.used_textures[1];
    info->opt_alpha = cc.opt_alpha;
    info->opt_texture_edge = cc.opt_texture_edge;

    const char *interp_in  = cc.noperspective_inputs     ? " @interpolate(linear)" : "";
    const char *interp_uv  = cc.noperspective_texcoords  ? " @interpolate(linear)" : "";
    const char *interp_fog = cc.noperspective_fog        ? " @interpolate(linear)" : "";

    /* Section buffers: VsIn fields, VsOut varying fields, vs_main passthrough. */
    char vsin[6144];  size_t in_n = 0;
    char vsout[6144]; size_t out_n = 0;
    char vsbody[6144];size_t body_n = 0;
    vsin[0] = vsout[0] = vsbody[0] = '\0';

    int loc_in = 0;   /* VsIn @location counter */
    int loc_out = 0;  /* VsOut varying @location counter */
    int off = 0;      /* running float offset within the vertex */
    int na = 0;       /* attribute count */

/* Record a vertex attribute (for the WGPUVertexBufferLayout) + emit its VsIn
 * field. `sz` floats at the current running offset. */
#define ATTR_IN(name, sz)                                                        \
    do {                                                                         \
        info->attrs[na].location = loc_in;                                       \
        info->attrs[na].size = (sz);                                             \
        info->attrs[na].offset = off;                                            \
        na++;                                                                    \
        in_n += (size_t)snprintf(vsin + in_n, sizeof(vsin) - in_n,               \
                                 "  @location(%d) %s : %s,\n",                    \
                                 loc_in, (name), type_str(sz));                  \
        loc_in++;                                                                \
        off += (sz);                                                             \
    } while (0)

/* Emit a VsOut varying + its vs_main passthrough for an attribute named
 * a<Field> -> v<Field>. */
#define VARY(field, sz, interp)                                                  \
    do {                                                                         \
        out_n += (size_t)snprintf(vsout + out_n, sizeof(vsout) - out_n,          \
                                  "  @location(%d)%s v%s : %s,\n",                \
                                  loc_out, (interp), (field), type_str(sz));     \
        body_n += (size_t)snprintf(vsbody + body_n, sizeof(vsbody) - body_n,     \
                                   "  out.v%s = in.a%s;\n", (field), (field));    \
        loc_out++;                                                               \
    } while (0)

    /* 1. Clip position (attribute only; becomes @builtin(position), not a varying). */
    ATTR_IN("aVtxPos", 4);

    /* 2. Diag coverage-wrap triangle attrs (rare; consumed for stride). */
    if (cc.opt_alpha && cc.diag_rdp_cvg_memory_blend) {
        ATTR_IN("aDiagTri01", 4); VARY("DiagTri01", 4, " @interpolate(linear)");
        ATTR_IN("aDiagTri2", 2);  VARY("DiagTri2", 2, " @interpolate(linear)");
    }
    /* 3. World position (per-pixel lighting / shadow receiver). */
    if (cc.opt_world_pos) {
        ATTR_IN("aWorldPos", 3); VARY("WorldPos", 3, "");
    }
    /* 4. Baked shade (dfdx relight reference). */
    if (cc.opt_dfdx_light) {
        ATTR_IN("aShade", 3); VARY("Shade", 3, "");
    }
    /* 5. Per-texture coords + optional clamp/mask limits. */
    for (int i = 0; i < 2; i++) {
        if (!cc.used_textures[i]) continue;
        char nm[24];
        snprintf(nm, sizeof(nm), "aTexCoord%d", i);
        ATTR_IN(nm, 2);
        char fld[24]; snprintf(fld, sizeof(fld), "TexCoord%d", i);
        VARY(fld, 2, interp_uv);
        for (int axis = 0; axis < 2; axis++) {
            char ac = axis == 0 ? 'S' : 'T';
            if (cc.clamp[i][axis]) {
                snprintf(nm, sizeof(nm), "aTexClamp%c%d", ac, i);
                ATTR_IN(nm, 1);
                snprintf(fld, sizeof(fld), "TexClamp%c%d", ac, i);
                VARY(fld, 1, interp_uv);
            }
            if (cc.tile_mask[i][axis]) {
                snprintf(nm, sizeof(nm), "aTexMask%c%d", ac, i);
                ATTR_IN(nm, 1);
                snprintf(fld, sizeof(fld), "TexMask%c%d", ac, i);
                VARY(fld, 1, interp_uv);
            }
        }
    }
    /* 6. Fog (rgb + factor). */
    if (cc.opt_fog) {
        ATTR_IN("aFog", 4); VARY("Fog", 4, interp_fog);
    }
    /* 7. Combiner inputs (shade/prim/env). vec4 with alpha, else vec3. */
    int input_sz = cc.opt_alpha ? 4 : 3;
    for (int i = 0; i < cc.num_inputs; i++) {
        char nm[24]; snprintf(nm, sizeof(nm), "aInput%d", i + 1);
        ATTR_IN(nm, input_sz);
        char fld[24]; snprintf(fld, sizeof(fld), "Input%d", i + 1);
        VARY(fld, input_sz, interp_in);
    }

    info->num_attrs = na;
    info->num_floats = off;

    /* ---- Assemble the module ------------------------------------------- */
    char *out = (char *)malloc(MODULE_CAP);
    if (out == NULL) {
        return NULL;
    }
    size_t n = 0;
#define P(...) do { n += (size_t)snprintf(out + n, MODULE_CAP - n, __VA_ARGS__); } while (0)

    /* Structs */
    P("struct VsIn {\n%s};\n", vsin);
    P("struct VsOut {\n  @builtin(position) clip_pos : vec4<f32>,\n%s};\n", vsout);

    /* Vertex shader. Depth: WebGPU clip space is 0..1 (z_is_from_0_to_1 == true),
     * matching the Metal path — the CPU T&L already produces that convention, so
     * the position passes through unscaled (no GL 0.3 z-squash). */
    P("@vertex fn vs_main(in : VsIn) -> VsOut {\n");
    P("  var out : VsOut;\n");
    P("%s", vsbody);
    P("  out.clip_pos = in.aVtxPos;\n");
    P("  return out;\n}\n");

    /* Texture/sampler bindings (group 0). */
    if (cc.used_textures[0]) {
        P("@group(0) @binding(0) var uTex0 : texture_2d<f32>;\n");
        P("@group(0) @binding(1) var uSamp0 : sampler;\n");
    }
    if (cc.used_textures[1]) {
        P("@group(0) @binding(2) var uTex1 : texture_2d<f32>;\n");
        P("@group(0) @binding(3) var uSamp1 : sampler;\n");
    }

    /* Position-hash noise stand-in (Task 4 replaces with the frame-varying
     * uniform form). Only emitted when a combiner slot reads SHADER_NOISE. */
    bool needs_noise = false;
    for (int ci = 0; ci < 2 && !needs_noise; ci++)
        for (int cj = 0; cj < 2 && !needs_noise; cj++)
            for (int ck = 0; ck < 4 && !needs_noise; ck++)
                if (cc.c[ci][cj][ck] == SHADER_NOISE) needs_noise = true;
    if (needs_noise) {
        P("fn wgpu_noise(p : vec2<f32>) -> f32 {\n"
          "  return fract(sin(dot(floor(p), vec2<f32>(12.9898, 78.233))) * 43758.5453);\n}\n");
    }

    /* Shader-side tile mask (N64 mask-wrap): emit the helpers when any texture
     * axis uses it OR the N64 3-point filter needs them (its taps are mask-wrapped
     * — mask 0 is identity). Ports gfx_opengl.c n64TileMaskAxis/n64TileMaskUv;
     * GLSL mod() = floor-based remainder, so n64mod() must too (WGSL % truncates). */
    bool needs_filter = cc.n64_filter[0] || cc.n64_filter[1];
    bool uses_mask = cc.tile_mask[0][0] || cc.tile_mask[0][1] ||
                     cc.tile_mask[1][0] || cc.tile_mask[1][1];
    if (uses_mask || needs_filter) {
        P("fn n64mod(x : f32, y : f32) -> f32 { return x - y * floor(x / y); }\n");
        P("fn n64TileMaskAxis(texelCoord : f32, maskPeriod : f32) -> f32 {\n"
          "  let extent = abs(maskPeriod);\n"
          "  if (extent <= 0.5) { return texelCoord; }\n"
          "  var coord = n64mod(texelCoord, select(extent, extent * 2.0, maskPeriod < 0.0));\n"
          "  if (maskPeriod < 0.0 && coord >= extent) { coord = extent * 2.0 - coord; }\n"
          "  return coord;\n}\n");
        P("fn n64TileMaskUv(uv : vec2<f32>, texSize : vec2<f32>, maskS : f32, maskT : f32) -> vec2<f32> {\n"
          "  var tc = uv * texSize;\n"
          "  tc.x = n64TileMaskAxis(tc.x, maskS);\n"
          "  tc.y = n64TileMaskAxis(tc.y, maskT);\n"
          "  return tc / texSize;\n}\n");
    }

    /* N64 3-point ("always 3-point" variant of gfx_opengl.c n64TextureFilter):
     * the RDP's characteristic triangular filter instead of GL bilinear. Uses
     * explicit-LOD sampling so it is valid in any control flow; taps are
     * mask-wrapped (mask 0 = identity). */
    if (needs_filter) {
        P("fn n64Filter3(t : texture_2d<f32>, s : sampler, uv : vec2<f32>, texSize : vec2<f32>, maskS : f32, maskT : f32) -> vec4<f32> {\n"
          "  var offset = fract(uv * texSize - vec2<f32>(0.5));\n"
          "  offset = offset - step(1.0, offset.x + offset.y);\n"
          "  let baseUv = uv - offset / texSize;\n"
          "  let c0 = textureSampleLevel(t, s, n64TileMaskUv(baseUv, texSize, maskS, maskT), 0.0);\n"
          "  let c1 = textureSampleLevel(t, s, n64TileMaskUv(baseUv + vec2<f32>(sign(offset.x), 0.0) / texSize, texSize, maskS, maskT), 0.0);\n"
          "  let c2 = textureSampleLevel(t, s, n64TileMaskUv(baseUv + vec2<f32>(0.0, sign(offset.y)) / texSize, texSize, maskS, maskT), 0.0);\n"
          "  return c0 + abs(offset.x) * (c1 - c0) + abs(offset.y) * (c2 - c0);\n}\n");
    }

    /* Fragment shader */
    P("@fragment fn fs_main(in : VsOut) -> @location(0) vec4<f32> {\n");
    for (int i = 0; i < 2; i++) {
        if (!cc.used_textures[i]) continue;
        const char *tex = i == 0 ? "uTex0" : "uTex1";
        const char *samp = i == 0 ? "uSamp0" : "uSamp1";
        bool clamp_s = cc.clamp[i][0], clamp_t = cc.clamp[i][1];
        bool mask_s = cc.tile_mask[i][0], mask_t = cc.tile_mask[i][1];
        bool filt = cc.n64_filter[i];
        /* texSize when clamp/mask/filter needs it (textureDimensions). */
        if (clamp_s || clamp_t || mask_s || mask_t || filt) {
            P("  let texSize%d = vec2<f32>(textureDimensions(%s));\n", i, tex);
        }
        /* Shader-side UV clamp to the live tile window (gfx_opengl.c :1189-1203). */
        P("  var stc%d = in.vTexCoord%d;\n", i, i);
        if (clamp_s && clamp_t) {
            P("  stc%d = clamp(in.vTexCoord%d, vec2<f32>(0.5) / texSize%d, vec2<f32>(in.vTexClampS%d, in.vTexClampT%d));\n",
              i, i, i, i, i);
        } else if (clamp_s) {
            P("  stc%d.x = clamp(in.vTexCoord%d.x, 0.5 / texSize%d.x, in.vTexClampS%d);\n", i, i, i, i);
        } else if (clamp_t) {
            P("  stc%d.y = clamp(in.vTexCoord%d.y, 0.5 / texSize%d.y, in.vTexClampT%d);\n", i, i, i, i);
        }
        /* Sample: N64 3-point filter if the tile requests it, else mask-wrapped
         * or straight bilinear. */
        const char *ms = mask_s ? (i == 0 ? "in.vTexMaskS0" : "in.vTexMaskS1") : "0.0";
        const char *mt = mask_t ? (i == 0 ? "in.vTexMaskT0" : "in.vTexMaskT1") : "0.0";
        if (filt) {
            P("  let texVal%d : vec4<f32> = n64Filter3(%s, %s, stc%d, texSize%d, %s, %s);\n",
              i, tex, samp, i, i, ms, mt);
        } else if (mask_s || mask_t) {
            P("  let texVal%d : vec4<f32> = textureSample(%s, %s, n64TileMaskUv(stc%d, texSize%d, %s, %s));\n",
              i, tex, samp, i, i, ms, mt);
        } else {
            P("  let texVal%d : vec4<f32> = textureSample(%s, %s, stc%d);\n", i, tex, samp, i);
        }
    }

    P("  var texel : vec4<f32>;\n");
    int num_cycles = cc.opt_2cyc ? 2 : 1;
    for (int cyc = 0; cyc < num_cycles; cyc++) {
        if (!cc.color_alpha_same[cyc] && cc.opt_alpha) {
            P("  texel = vec4<f32>(");
            wgsl_formula(out, MODULE_CAP, &n, cc.c[cyc],
                         cc.do_single[cyc][0], cc.do_multiply[cyc][0], cc.do_mix[cyc][0],
                         false, false, true);
            P(", ");
            wgsl_formula(out, MODULE_CAP, &n, cc.c[cyc],
                         cc.do_single[cyc][1], cc.do_multiply[cyc][1], cc.do_mix[cyc][1],
                         true, true, true);
            P(");\n");
        } else if (cc.opt_alpha) {
            P("  texel = ");
            wgsl_formula(out, MODULE_CAP, &n, cc.c[cyc],
                         cc.do_single[cyc][0], cc.do_multiply[cyc][0], cc.do_mix[cyc][0],
                         true, false, true);
            P(";\n");
        } else {
            /* vec3 combiner: build the color, alpha forced to 1.0. */
            P("  texel = vec4<f32>(");
            wgsl_formula(out, MODULE_CAP, &n, cc.c[cyc],
                         cc.do_single[cyc][0], cc.do_multiply[cyc][0], cc.do_mix[cyc][0],
                         false, false, false);
            P(", 1.0);\n");
        }
        if (cyc == 0 && num_cycles == 2) {
            P("  texel = clamp(texel, vec4<f32>(-1.01), vec4<f32>(1.01));\n");
        }
    }
    P("  texel = clamp(texel, vec4<f32>(0.0), vec4<f32>(1.0));\n");

    /* Fog mix (rgb toward fog, alpha preserved). */
    if (cc.opt_fog) {
        P("  texel = vec4<f32>(mix(texel.rgb, in.vFog.rgb, in.vFog.a), texel.a);\n");
    }
    /* Texture-edge alpha cutout (PD 0.19 threshold). */
    if (cc.opt_texture_edge && cc.opt_alpha) {
        P("  if (texel.a > 0.19) { texel.a = 1.0; } else { discard; }\n");
    }
    if (!cc.opt_alpha) {
        P("  texel.a = 1.0;\n");
    }
    P("  return texel;\n}\n");

#undef P
#undef ATTR_IN
#undef VARY
    return out;
}
