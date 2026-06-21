/**
 * gfx_opengl.c — OpenGL 3.3 rendering backend with GLSL shader generation.
 *
 * Adapted from Emill/n64-fast3d-engine (MIT license).
 * Changes: uses glad instead of GLEW/SDL2_opengles2, removed ENABLE_OPENGL guard.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <platform_stdio.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <glad/glad.h>
#endif
#include <SDL.h>

#include "gfx_cc.h"
#include "gfx_rendering_api.h"
#include "../gfx_pc.h"
#include "front.h"
#include "othermodemicrocode.h"
#include "vi.h"

/* Verbose diagnostic flag from gfx_pc.c */
extern int g_diag_verbose;
extern float g_pcVideoGamma;
extern float g_pcRenderScale;
extern int g_pcMsaaSamples;
extern int g_pcRetroFilterMode;

#define PC_RETRO_FILTER_AUTO 0
#define PC_RETRO_FILTER_OFF  1
#define PC_RETRO_FILTER_ON   2

/* GL_DEPTH_CLAMP support — defined in gfx_pc.c, set once here in
 * gfx_opengl_init() before any rendering, read by CPU clipper and
 * shader generation. Never changes after init. */
extern bool g_depth_clamp_enabled;

#ifndef GL_DEPTH_CLAMP
#define GL_DEPTH_CLAMP 0x864F
#endif

/* Anisotropic filtering extension (part of GL 4.6 core, extension on earlier) */
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

struct ShaderProgram {
    uint64_t shader_id0;
    uint32_t shader_id1;
    GLuint opengl_program_id;
    uint8_t num_inputs;
    bool used_textures[2];
    uint8_t num_floats;
    GLint attrib_locations[16];
    uint8_t attrib_sizes[16];
    uint8_t num_attribs;
    bool used_noise;
    GLint frame_count_location;
    GLint window_height_location;
};

static struct ShaderProgram shader_program_pool[256];
static uint16_t shader_program_pool_size;
static GLuint opengl_vbo;
static GLuint opengl_vao;

static uint32_t frame_count;
static uint32_t current_height;
static int g_diag_noperspective_inputs = -1; /* GE007_DIAG_NOPERSPECTIVE_INPUTS=1 */
static int g_diag_noperspective_texcoords = -1; /* GE007_DIAG_NOPERSPECTIVE_TEXCOORDS=1 */
static int g_diag_quantize_combiner = -1; /* GE007_DIAG_QUANTIZE_COMBINER=1 */
static int g_diag_settex_cc_color_scale_checked; /* GE007_DIAG_SETTEX_CC_COLOR_SCALE_VALUE=N */
static float g_diag_settex_cc_color_scale_value = 1.02f;
static int g_diag_n64_filter_always_3point = -1; /* GE007_DIAG_N64_FILTER_ALWAYS_3POINT=1 */
static int g_diag_n64_filter_nearest_threshold_checked; /* GE007_DIAG_N64_FILTER_NEAREST_THRESHOLD=N */
static int g_diag_n64_filter_nearest_threshold_enabled;
static float g_diag_n64_filter_nearest_threshold = 1.0f;
static int g_diag_n64_filter_clamped_non_texedge_nearest_threshold_checked; /* GE007_DIAG_N64_FILTER_CLAMPED_NON_TEXEDGE_NEAREST_THRESHOLD=N */
static int g_diag_n64_filter_clamped_non_texedge_nearest_threshold_enabled;
static float g_diag_n64_filter_clamped_non_texedge_nearest_threshold = 1.0f;
static int g_diag_n64_filter_non_texedge_nearest_threshold_checked; /* GE007_DIAG_N64_FILTER_NON_TEXEDGE_NEAREST_THRESHOLD=N */
static int g_diag_n64_filter_non_texedge_nearest_threshold_enabled;
static float g_diag_n64_filter_non_texedge_nearest_threshold = 1.0f;

static bool gfx_diag_noperspective_inputs_enabled(void) {
    if (g_diag_noperspective_inputs < 0) {
        g_diag_noperspective_inputs =
            (getenv("GE007_DIAG_NOPERSPECTIVE_INPUTS") != NULL) ? 1 : 0;
        if (g_diag_noperspective_inputs) {
            fprintf(stderr,
                    "[fast3d] DIAG NOPERSPECTIVE SHADER INPUTS ENABLED "
                    "(GE007_DIAG_NOPERSPECTIVE_INPUTS)\n");
            fflush(stderr);
        }
    }
    return g_diag_noperspective_inputs > 0;
}

static bool gfx_diag_n64_filter_always_3point_enabled(void) {
    if (g_diag_n64_filter_always_3point < 0) {
        g_diag_n64_filter_always_3point =
            (getenv("GE007_DIAG_N64_FILTER_ALWAYS_3POINT") != NULL) ? 1 : 0;
        if (g_diag_n64_filter_always_3point) {
            fprintf(stderr,
                    "[fast3d] DIAG N64 FILTER ALWAYS 3POINT ENABLED "
                    "(GE007_DIAG_N64_FILTER_ALWAYS_3POINT)\n");
            fflush(stderr);
        }
    }
    return g_diag_n64_filter_always_3point > 0;
}

static float gfx_parse_diag_float_threshold(const char *env, float fallback) {
    float value = fallback;
    if (env != NULL && env[0] != '\0') {
        char *end = NULL;
        value = strtof(env, &end);
        if (end == env || value != value) {
            value = fallback;
        }
    }
    if (value < 0.0f) value = 0.0f;
    if (value > 4.0f) value = 4.0f;
    return value;
}

static float gfx_diag_n64_filter_nearest_threshold(bool texture_edge,
                                                   bool clamped,
                                                   float default_threshold)
{
    if (!g_diag_n64_filter_nearest_threshold_checked) {
        const char *env = getenv("GE007_DIAG_N64_FILTER_NEAREST_THRESHOLD");
        if (env != NULL && env[0] != '\0') {
            g_diag_n64_filter_nearest_threshold =
                gfx_parse_diag_float_threshold(env, default_threshold);
            g_diag_n64_filter_nearest_threshold_enabled = 1;
            fprintf(stderr,
                    "[fast3d] DIAG N64 FILTER NEAREST THRESHOLD value=%.6f "
                    "(GE007_DIAG_N64_FILTER_NEAREST_THRESHOLD)\n",
                    g_diag_n64_filter_nearest_threshold);
            fflush(stderr);
        }
        g_diag_n64_filter_nearest_threshold_checked = 1;
    }
    if (g_diag_n64_filter_nearest_threshold_enabled) {
        return g_diag_n64_filter_nearest_threshold;
    }

    if (!g_diag_n64_filter_clamped_non_texedge_nearest_threshold_checked) {
        const char *env = getenv("GE007_DIAG_N64_FILTER_CLAMPED_NON_TEXEDGE_NEAREST_THRESHOLD");
        if (env != NULL && env[0] != '\0') {
            g_diag_n64_filter_clamped_non_texedge_nearest_threshold =
                gfx_parse_diag_float_threshold(env,
                                               default_threshold);
            g_diag_n64_filter_clamped_non_texedge_nearest_threshold_enabled = 1;
            fprintf(stderr,
                    "[fast3d] DIAG N64 FILTER CLAMPED NON-TEXEDGE NEAREST THRESHOLD value=%.6f "
                    "(GE007_DIAG_N64_FILTER_CLAMPED_NON_TEXEDGE_NEAREST_THRESHOLD)\n",
                    g_diag_n64_filter_clamped_non_texedge_nearest_threshold);
            fflush(stderr);
        }
        g_diag_n64_filter_clamped_non_texedge_nearest_threshold_checked = 1;
    }
    if (clamped && !texture_edge &&
        g_diag_n64_filter_clamped_non_texedge_nearest_threshold_enabled) {
        return g_diag_n64_filter_clamped_non_texedge_nearest_threshold;
    }

    if (!g_diag_n64_filter_non_texedge_nearest_threshold_checked) {
        const char *env = getenv("GE007_DIAG_N64_FILTER_NON_TEXEDGE_NEAREST_THRESHOLD");
        if (env != NULL && env[0] != '\0') {
            g_diag_n64_filter_non_texedge_nearest_threshold =
                gfx_parse_diag_float_threshold(env, default_threshold);
            g_diag_n64_filter_non_texedge_nearest_threshold_enabled = 1;
            fprintf(stderr,
                    "[fast3d] DIAG N64 FILTER NON-TEXEDGE NEAREST THRESHOLD value=%.6f "
                    "(GE007_DIAG_N64_FILTER_NON_TEXEDGE_NEAREST_THRESHOLD)\n",
                    g_diag_n64_filter_non_texedge_nearest_threshold);
            fflush(stderr);
        }
        g_diag_n64_filter_non_texedge_nearest_threshold_checked = 1;
    }
    if (!texture_edge && g_diag_n64_filter_non_texedge_nearest_threshold_enabled) {
        return g_diag_n64_filter_non_texedge_nearest_threshold;
    }

    return default_threshold;
}

static bool gfx_diag_noperspective_texcoords_enabled(void) {
    if (g_diag_noperspective_texcoords < 0) {
        g_diag_noperspective_texcoords =
            (getenv("GE007_DIAG_NOPERSPECTIVE_TEXCOORDS") != NULL) ? 1 : 0;
        if (g_diag_noperspective_texcoords) {
            fprintf(stderr,
                    "[fast3d] DIAG NOPERSPECTIVE TEXCOORDS ENABLED "
                    "(GE007_DIAG_NOPERSPECTIVE_TEXCOORDS)\n");
            fflush(stderr);
        }
    }
    return g_diag_noperspective_texcoords > 0;
}

static bool gfx_diag_quantize_combiner_enabled(void) {
    if (g_diag_quantize_combiner < 0) {
        g_diag_quantize_combiner =
            (getenv("GE007_DIAG_QUANTIZE_COMBINER") != NULL) ? 1 : 0;
        if (g_diag_quantize_combiner) {
            fprintf(stderr,
                    "[fast3d] DIAG QUANTIZE COMBINER ENABLED "
                    "(GE007_DIAG_QUANTIZE_COMBINER)\n");
            fflush(stderr);
        }
    }
    return g_diag_quantize_combiner > 0;
}

static float gfx_diag_settex_cc_color_scale_value(void) {
    if (!g_diag_settex_cc_color_scale_checked) {
        const char *env = getenv("GE007_DIAG_SETTEX_CC_COLOR_SCALE_VALUE");
        if (env != NULL && env[0] != '\0') {
            char *end = NULL;
            float value = strtof(env, &end);
            if (end != env && value == value) {
                if (value < 0.0f) value = 0.0f;
                if (value > 4.0f) value = 4.0f;
                g_diag_settex_cc_color_scale_value = value;
            }
        }
        fprintf(stderr,
                "[fast3d] DIAG SETTEX CC COLOR SCALE value=%.6f "
                "(GE007_DIAG_SETTEX_CC_COLOR_SCALE_VALUE)\n",
                g_diag_settex_cc_color_scale_value);
        fflush(stderr);
        g_diag_settex_cc_color_scale_checked = 1;
    }
    return g_diag_settex_cc_color_scale_value;
}

static bool gfx_opengl_z_is_from_0_to_1(void) {
    return false;
}

static void gfx_opengl_vertex_array_set_attribs(struct ShaderProgram *prg) {
    size_t num_floats = prg->num_floats;
    size_t pos = 0;

    for (int i = 0; i < prg->num_attribs; i++) {
        glEnableVertexAttribArray(prg->attrib_locations[i]);
        glVertexAttribPointer(prg->attrib_locations[i], prg->attrib_sizes[i], GL_FLOAT, GL_FALSE, num_floats * sizeof(float), (void *)(pos * sizeof(float)));
        pos += prg->attrib_sizes[i];
    }
}

static void gfx_opengl_set_uniforms(struct ShaderProgram *prg) {
    if (prg->used_noise) {
        glUniform1i(prg->frame_count_location, frame_count);
        glUniform1i(prg->window_height_location, current_height);
    }
}

static void gfx_opengl_unload_shader(struct ShaderProgram *old_prg) {
    if (old_prg != NULL) {
        for (int i = 0; i < old_prg->num_attribs; i++) {
            glDisableVertexAttribArray(old_prg->attrib_locations[i]);
        }
    }
}

static void gfx_opengl_load_shader(struct ShaderProgram *new_prg) {
    glUseProgram(new_prg->opengl_program_id);
    gfx_opengl_vertex_array_set_attribs(new_prg);
    gfx_opengl_set_uniforms(new_prg);
}

static void append_str(char *buf, size_t *len, const char *str) {
    while (*str != '\0') buf[(*len)++] = *str++;
}

static void append_line(char *buf, size_t *len, const char *str) {
    while (*str != '\0') buf[(*len)++] = *str++;
    buf[(*len)++] = '\n';
}

static const char *shader_item_to_str(uint32_t item, bool with_alpha, bool only_alpha, bool inputs_have_alpha, bool hint_single_element) {
    if (!only_alpha) {
        switch (item) {
            case SHADER_0:
                return with_alpha ? "vec4(0.0, 0.0, 0.0, 0.0)" : "vec3(0.0, 0.0, 0.0)";
            case SHADER_INPUT_1:
                return with_alpha || !inputs_have_alpha ? "vInput1" : "vInput1.rgb";
            case SHADER_INPUT_2:
                return with_alpha || !inputs_have_alpha ? "vInput2" : "vInput2.rgb";
            case SHADER_INPUT_3:
                return with_alpha || !inputs_have_alpha ? "vInput3" : "vInput3.rgb";
            case SHADER_INPUT_4:
                return with_alpha || !inputs_have_alpha ? "vInput4" : "vInput4.rgb";
            case SHADER_INPUT_5:
                return with_alpha || !inputs_have_alpha ? "vInput5" : "vInput5.rgb";
            case SHADER_INPUT_6:
                return with_alpha || !inputs_have_alpha ? "vInput6" : "vInput6.rgb";
            case SHADER_INPUT_7:
                return with_alpha || !inputs_have_alpha ? "vInput7" : "vInput7.rgb";
            case SHADER_TEXEL0:
                return with_alpha ? "texVal0" : "texVal0.rgb";
            case SHADER_TEXEL0A:
                return hint_single_element ? "texVal0.a" :
                    (with_alpha ? "vec4(texVal0.a, texVal0.a, texVal0.a, texVal0.a)" : "vec3(texVal0.a, texVal0.a, texVal0.a)");
            case SHADER_TEXEL1:
                return with_alpha ? "texVal1" : "texVal1.rgb";
            case SHADER_TEXEL1A:
                return hint_single_element ? "texVal1.a" :
                    (with_alpha ? "vec4(texVal1.a, texVal1.a, texVal1.a, texVal1.a)" : "vec3(texVal1.a, texVal1.a, texVal1.a)");
            case SHADER_1:
                return with_alpha ? "vec4(1.0, 1.0, 1.0, 1.0)" : "vec3(1.0, 1.0, 1.0)";
            case SHADER_COMBINED:
                return with_alpha ? "texel" : "texel.rgb";
            case SHADER_NOISE:
                return with_alpha ? "vec4(random(vec3(floor(gl_FragCoord.xy * (240.0 / float(window_height))), float(frame_count))))" :
                    "vec3(random(vec3(floor(gl_FragCoord.xy * (240.0 / float(window_height))), float(frame_count))))";
        }
    } else {
        switch (item) {
            case SHADER_0:
                return "0.0";
            case SHADER_INPUT_1: return "vInput1.a";
            case SHADER_INPUT_2: return "vInput2.a";
            case SHADER_INPUT_3: return "vInput3.a";
            case SHADER_INPUT_4: return "vInput4.a";
            case SHADER_INPUT_5: return "vInput5.a";
            case SHADER_INPUT_6: return "vInput6.a";
            case SHADER_INPUT_7: return "vInput7.a";
            case SHADER_TEXEL0: return "texVal0.a";
            case SHADER_TEXEL0A: return "texVal0.a";
            case SHADER_TEXEL1: return "texVal1.a";
            case SHADER_TEXEL1A: return "texVal1.a";
            case SHADER_1: return "1.0";
            case SHADER_COMBINED: return "texel.a";
            case SHADER_NOISE:
                return "random(vec3(floor(gl_FragCoord.xy * (240.0 / float(window_height))), float(frame_count)))";
        }
    }
    return "0.0";
}

static void append_formula(char *buf, size_t *len, uint8_t c[2][4], bool do_single, bool do_multiply, bool do_mix, bool with_alpha, bool only_alpha, bool opt_alpha) {
    if (do_single) {
        append_str(buf, len, shader_item_to_str(c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false));
    } else if (do_multiply) {
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, " * ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
    } else if (do_mix) {
        append_str(buf, len, "mix(");
        append_str(buf, len, shader_item_to_str(c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
        append_str(buf, len, ")");
    } else {
        append_str(buf, len, "(");
        append_str(buf, len, shader_item_to_str(c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, " - ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false));
        append_str(buf, len, ") * ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true));
        append_str(buf, len, " + ");
        append_str(buf, len, shader_item_to_str(c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false));
    }
}

static struct ShaderProgram *gfx_opengl_create_and_load_new_shader(uint64_t shader_id0, uint32_t shader_id1) {
    struct CCFeatures cc_features;
    gfx_cc_get_features(shader_id0, shader_id1, &cc_features);

    char vs_buf[8192];
    char fs_buf[12288];
    size_t vs_len = 0;
    size_t fs_len = 0;
    size_t num_floats = 4;
    const char *input_interp =
        (gfx_diag_noperspective_inputs_enabled() || cc_features.noperspective_inputs) ?
        "noperspective " : "";
    const char *texcoord_interp =
        (gfx_diag_noperspective_texcoords_enabled() || cc_features.noperspective_texcoords) ?
        "noperspective " : "";
    const char *fog_interp = cc_features.noperspective_fog ? "noperspective " : "";
    bool quantize_combiner = gfx_diag_quantize_combiner_enabled();

    /* Use GLSL 150 for macOS Core Profile compatibility, 330 elsewhere */
#ifdef __APPLE__
    append_line(vs_buf, &vs_len, "#version 150");
#else
    append_line(vs_buf, &vs_len, "#version 330 core");
#endif
    append_line(vs_buf, &vs_len, "in vec4 aVtxPos;");
    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            vs_len += ge007_sprintf(vs_buf + vs_len, "in vec2 aTexCoord%d;\n", i);
            vs_len += ge007_sprintf(vs_buf + vs_len, "%sout vec2 vTexCoord%d;\n",
                                     texcoord_interp, i);
            num_floats += 2;
            for (int axis = 0; axis < 2; axis++) {
                if (cc_features.clamp[i][axis]) {
                    const char axis_name = axis == 0 ? 'S' : 'T';
                    vs_len += ge007_sprintf(vs_buf + vs_len,
                                             "in float aTexClamp%c%d;\n",
                                             axis_name, i);
                    vs_len += ge007_sprintf(vs_buf + vs_len,
                                             "%sout float vTexClamp%c%d;\n",
                                             texcoord_interp, axis_name, i);
                    num_floats += 1;
                }
            }
        }
    }
    if (cc_features.opt_fog) {
        append_line(vs_buf, &vs_len, "in vec4 aFog;");
        vs_len += ge007_sprintf(vs_buf + vs_len, "%sout vec4 vFog;\n",
                                 fog_interp);
        num_floats += 4;
    }
    for (int i = 0; i < cc_features.num_inputs; i++) {
        vs_len += ge007_sprintf(vs_buf + vs_len, "in vec%d aInput%d;\n", cc_features.opt_alpha ? 4 : 3, i + 1);
        vs_len += ge007_sprintf(vs_buf + vs_len, "%sout vec%d vInput%d;\n",
                                 input_interp, cc_features.opt_alpha ? 4 : 3, i + 1);
        num_floats += cc_features.opt_alpha ? 4 : 3;
    }
    append_line(vs_buf, &vs_len, "void main() {");
    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            vs_len += ge007_sprintf(vs_buf + vs_len, "vTexCoord%d = aTexCoord%d;\n", i, i);
            for (int axis = 0; axis < 2; axis++) {
                if (cc_features.clamp[i][axis]) {
                    const char axis_name = axis == 0 ? 'S' : 'T';
                    vs_len += ge007_sprintf(vs_buf + vs_len,
                                             "vTexClamp%c%d = aTexClamp%c%d;\n",
                                             axis_name, i, axis_name, i);
                }
            }
        }
    }
    if (cc_features.opt_fog) {
        append_line(vs_buf, &vs_len, "vFog = aFog;");
    }
    for (int i = 0; i < cc_features.num_inputs; i++) {
        vs_len += ge007_sprintf(vs_buf + vs_len, "vInput%d = aInput%d;\n", i + 1, i + 1);
    }
    append_line(vs_buf, &vs_len, "gl_Position = aVtxPos;");
    if (!g_depth_clamp_enabled) {
        /* Depth range fix: N64 perspective maps most geometry to z/w ≈
         * 0.998-1.005, clustering the entire scene into <1% of the depth
         * buffer. Scaling clip-space z by 0.3 spreads the useful range while
         * the CPU clipper keeps triangles inside the effective frustum.
         * Skipped when GL_DEPTH_CLAMP is available (PD's preferred approach). */
        append_line(vs_buf, &vs_len, "gl_Position.z *= 0.3;");
    }
    append_line(vs_buf, &vs_len, "}");

    /* Fragment shader */
#ifdef __APPLE__
    append_line(fs_buf, &fs_len, "#version 150");
    append_line(fs_buf, &fs_len, "out vec4 fragColor;");
#else
    append_line(fs_buf, &fs_len, "#version 330 core");
    append_line(fs_buf, &fs_len, "out vec4 fragColor;");
#endif
    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            fs_len += ge007_sprintf(fs_buf + fs_len, "%sin vec2 vTexCoord%d;\n",
                                     texcoord_interp, i);
            for (int axis = 0; axis < 2; axis++) {
                if (cc_features.clamp[i][axis]) {
                    const char axis_name = axis == 0 ? 'S' : 'T';
                    fs_len += ge007_sprintf(fs_buf + fs_len,
                                             "%sin float vTexClamp%c%d;\n",
                                             texcoord_interp, axis_name, i);
                }
            }
        }
    }
    if (cc_features.opt_fog) {
        fs_len += ge007_sprintf(fs_buf + fs_len, "%sin vec4 vFog;\n",
                                 fog_interp);
    }
    for (int i = 0; i < cc_features.num_inputs; i++) {
        fs_len += ge007_sprintf(fs_buf + fs_len, "%sin vec%d vInput%d;\n",
                                 input_interp, cc_features.opt_alpha ? 4 : 3, i + 1);
    }
    if (cc_features.used_textures[0]) {
        append_line(fs_buf, &fs_len, "uniform sampler2D uTex0;");
    }
    if (cc_features.used_textures[1]) {
        append_line(fs_buf, &fs_len, "uniform sampler2D uTex1;");
    }

    if (cc_features.n64_filter[0] || cc_features.n64_filter[1]) {
        bool always_3point = cc_features.n64_filter_always_3point ||
            gfx_diag_n64_filter_always_3point_enabled();
        bool clamped =
            cc_features.clamp[0][0] || cc_features.clamp[0][1] ||
            cc_features.clamp[1][0] || cc_features.clamp[1][1];
        float default_nearest_threshold =
            cc_features.n64_filter_nearest_threshold_005 ? 0.05f : 1.0f;
        float nearest_threshold =
            gfx_diag_n64_filter_nearest_threshold(cc_features.opt_texture_edge,
                                                  clamped,
                                                  default_nearest_threshold);
        append_line(fs_buf, &fs_len, "vec4 n64TextureFilter(sampler2D tex, vec2 uv) {");
        append_line(fs_buf, &fs_len, "    vec2 texSize = vec2(textureSize(tex, 0));");
        append_line(fs_buf, &fs_len, "    vec2 texelCoord = uv * texSize;");
        if (!always_3point) {
            append_line(fs_buf, &fs_len, "    vec2 dx = dFdx(texelCoord);");
            append_line(fs_buf, &fs_len, "    vec2 dy = dFdy(texelCoord);");
            append_line(fs_buf, &fs_len, "    vec2 footprint = max(abs(dx), abs(dy));");
            fs_len += ge007_sprintf(fs_buf + fs_len,
                                     "    if (max(footprint.x, footprint.y) < %.9f) {\n",
                                     nearest_threshold);
            append_line(fs_buf, &fs_len, "        return textureLod(tex, (floor(texelCoord) + vec2(0.5)) / texSize, 0.0);");
            append_line(fs_buf, &fs_len, "    }");
        }
        append_line(fs_buf, &fs_len, "    vec2 offset = fract(uv * texSize - vec2(0.5));");
        append_line(fs_buf, &fs_len, "    offset -= step(1.0, offset.x + offset.y);");
        append_line(fs_buf, &fs_len, "    vec2 baseUv = uv - offset / texSize;");
        append_line(fs_buf, &fs_len, "    vec4 c0 = textureLod(tex, baseUv, 0.0);");
        append_line(fs_buf, &fs_len, "    vec4 c1 = textureLod(tex, baseUv + vec2(sign(offset.x), 0.0) / texSize, 0.0);");
        append_line(fs_buf, &fs_len, "    vec4 c2 = textureLod(tex, baseUv + vec2(0.0, sign(offset.y)) / texSize, 0.0);");
        append_line(fs_buf, &fs_len, "    return c0 + abs(offset.x) * (c1 - c0) + abs(offset.y) * (c2 - c0);");
        append_line(fs_buf, &fs_len, "}");
    }

    /* Declare noise function if ANY combiner input uses SHADER_NOISE,
     * or if opt_noise is set for alpha dithering. */
    bool needs_noise = cc_features.opt_noise;
    if (!needs_noise) {
        for (int ci = 0; ci < 2 && !needs_noise; ci++)
            for (int cj = 0; cj < 2 && !needs_noise; cj++)
                for (int ck = 0; ck < 4 && !needs_noise; ck++)
                    if (cc_features.c[ci][cj][ck] == SHADER_NOISE) needs_noise = true;
    }
    if (needs_noise) {
        append_line(fs_buf, &fs_len, "uniform int frame_count;");
        append_line(fs_buf, &fs_len, "uniform int window_height;");
        append_line(fs_buf, &fs_len, "float random(in vec3 value) {");
        append_line(fs_buf, &fs_len, "    float random = dot(sin(value), vec3(12.9898, 78.233, 37.719));");
        append_line(fs_buf, &fs_len, "    return fract(sin(random) * 143758.5453);");
        append_line(fs_buf, &fs_len, "}");
    }

    append_line(fs_buf, &fs_len, "void main() {");

    /* Shader-side UV clamping (PD pattern): clamp tex coords to the live
     * N64 tile's logical window, not blindly to the GL texture's 0..1 range. */
    for (int i = 0; i < 2; i++) {
        if (!cc_features.used_textures[i]) continue;
        fs_len += ge007_sprintf(fs_buf + fs_len,
                                 "vec2 sampleTexCoord%d = vTexCoord%d;\n",
                                 i, i);
        if (cc_features.clamp[i][0] || cc_features.clamp[i][1]) {
            fs_len += ge007_sprintf(fs_buf + fs_len,
                                     "vec2 texSize%d = vec2(textureSize(uTex%d, 0));\n",
                                     i, i);
            if (cc_features.clamp[i][0] && cc_features.clamp[i][1]) {
                fs_len += ge007_sprintf(fs_buf + fs_len,
                                         "sampleTexCoord%d = clamp(vTexCoord%d, 0.5 / texSize%d, vec2(vTexClampS%d, vTexClampT%d));\n",
                                         i, i, i, i, i);
            } else if (cc_features.clamp[i][0]) {
                fs_len += ge007_sprintf(fs_buf + fs_len,
                                         "sampleTexCoord%d.s = clamp(vTexCoord%d.s, 0.5 / texSize%d.s, vTexClampS%d);\n",
                                         i, i, i, i);
            } else {
                fs_len += ge007_sprintf(fs_buf + fs_len,
                                         "sampleTexCoord%d.t = clamp(vTexCoord%d.t, 0.5 / texSize%d.t, vTexClampT%d);\n",
                                         i, i, i, i);
            }
        }
    }

    if (cc_features.used_textures[0]) {
        const char *sample_fn = cc_features.n64_filter[0] ? "n64TextureFilter" : "texture";
        fs_len += ge007_sprintf(fs_buf + fs_len, "vec4 texVal0 = %s(uTex0, sampleTexCoord0);\n", sample_fn);
    }
    if (cc_features.used_textures[1]) {
        const char *sample_fn = cc_features.n64_filter[1] ? "n64TextureFilter" : "texture";
        fs_len += ge007_sprintf(fs_buf + fs_len, "vec4 texVal1 = %s(uTex1, sampleTexCoord1);\n", sample_fn);
    }

    /* 2-cycle combiner: emit formula for each cycle.
     * SHADER_COMBINED in cycle 1 references the 'texel' variable written by cycle 0. */
    append_line(fs_buf, &fs_len, cc_features.opt_alpha ? "vec4 texel;" : "vec3 texel;");

    int num_cycles = cc_features.opt_2cyc ? 2 : 1;
    for (int cyc = 0; cyc < num_cycles; cyc++) {
        append_str(fs_buf, &fs_len, "texel = ");
        if (!cc_features.color_alpha_same[cyc] && cc_features.opt_alpha) {
            append_str(fs_buf, &fs_len, "vec4(");
            append_formula(fs_buf, &fs_len, cc_features.c[cyc],
                           cc_features.do_single[cyc][0], cc_features.do_multiply[cyc][0],
                           cc_features.do_mix[cyc][0], false, false, true);
            append_str(fs_buf, &fs_len, ", ");
            append_formula(fs_buf, &fs_len, cc_features.c[cyc],
                           cc_features.do_single[cyc][1], cc_features.do_multiply[cyc][1],
                           cc_features.do_mix[cyc][1], true, true, true);
            append_str(fs_buf, &fs_len, ")");
        } else {
            append_formula(fs_buf, &fs_len, cc_features.c[cyc],
                           cc_features.do_single[cyc][0], cc_features.do_multiply[cyc][0],
                           cc_features.do_mix[cyc][0], cc_features.opt_alpha, false,
                           cc_features.opt_alpha);
        }
        append_line(fs_buf, &fs_len, ";");

        /* Color wrapping between cycles (PD pattern) */
        if (cyc == 0 && num_cycles == 2) {
            append_line(fs_buf, &fs_len, "texel = clamp(texel, -1.01, 1.01);");
        }
        if (quantize_combiner) {
            append_line(fs_buf, &fs_len,
                        "texel = floor(clamp(texel, 0.0, 1.0) * 255.0 + 0.5) / 255.0;");
        }
    }
    /* Final clamp after all cycles */
    append_line(fs_buf, &fs_len, "texel = clamp(texel, 0.0, 1.0);");

    if (cc_features.opt_fog) {
        if (cc_features.opt_alpha) {
            append_line(fs_buf, &fs_len, "texel = vec4(mix(texel.rgb, vFog.rgb, vFog.a), texel.a);");
        } else {
            append_line(fs_buf, &fs_len, "texel = mix(texel, vFog.rgb, vFog.a);");
        }
    }

    if (cc_features.opt_texture_edge && cc_features.opt_alpha) {
        /* PD uses 0.19 threshold (more permissive than our old 0.3) */
        append_line(fs_buf, &fs_len, "if (texel.a > 0.19) texel.a = 1.0; else discard;");
    }

    if (cc_features.opt_alpha && cc_features.opt_noise) {
        append_line(fs_buf, &fs_len, "texel.a *= floor(random(vec3(floor(gl_FragCoord.xy * (240.0 / float(window_height))), float(frame_count))) + 0.5);");
    }

    if (cc_features.diag_color_scale) {
        float scale = gfx_diag_settex_cc_color_scale_value();
        if (cc_features.opt_alpha) {
            fs_len += ge007_sprintf(fs_buf + fs_len,
                                    "texel = vec4(clamp(texel.rgb * %.9f, 0.0, 1.0), texel.a);\n",
                                    scale);
        } else {
            fs_len += ge007_sprintf(fs_buf + fs_len,
                                    "texel = clamp(texel * %.9f, 0.0, 1.0);\n",
                                    scale);
        }
    }

    if (cc_features.opt_alpha) {
        append_line(fs_buf, &fs_len, "fragColor = texel;");
    } else {
        append_line(fs_buf, &fs_len, "fragColor = vec4(texel, 1.0);");
    }
    append_line(fs_buf, &fs_len, "}");

    vs_buf[vs_len] = '\0';
    fs_buf[fs_len] = '\0';

    /* Dump first few generated shaders */
    {
        static int shader_dump = 0;
        if (g_diag_verbose && shader_dump < 3) {
            printf("[SHADER_%d] id0=0x%016llX id1=0x%X tex=%d,%d fog=%d alpha=%d noise=%d 2cyc=%d inputs=%d\n",
                   shader_dump, (unsigned long long)shader_id0, shader_id1,
                   cc_features.used_textures[0], cc_features.used_textures[1],
                   cc_features.opt_fog, cc_features.opt_alpha, cc_features.opt_noise,
                   cc_features.opt_2cyc, cc_features.num_inputs);
            printf("--- FS ---\n%s\n--- END ---\n", fs_buf);
            fflush(stdout);
            shader_dump++;
        }
    }

    const GLchar *sources[2] = { vs_buf, fs_buf };
    const GLint lengths[2] = { (GLint)vs_len, (GLint)fs_len };
    GLint success;

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &sources[0], &lengths[0]);
    glCompileShader(vertex_shader);
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char error_log[1024];
        GLint max_length = 0;
        glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &max_length);
        if (max_length > (GLint)sizeof(error_log)) max_length = sizeof(error_log) - 1;
        glGetShaderInfoLog(vertex_shader, max_length, &max_length, error_log);
        fprintf(stderr, "[fast3d] Vertex shader compilation failed:\n%s\nSource:\n%s\n", error_log, vs_buf);
        abort();
    }

    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &sources[1], &lengths[1]);
    glCompileShader(fragment_shader);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char error_log[1024];
        GLint max_length = 0;
        glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &max_length);
        if (max_length > (GLint)sizeof(error_log)) max_length = sizeof(error_log) - 1;
        glGetShaderInfoLog(fragment_shader, max_length, &max_length, error_log);
        fprintf(stderr, "[fast3d] Fragment shader compilation failed:\n%s\nSource:\n%s\n", error_log, fs_buf);
        abort();
    }

    GLuint shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    size_t cnt = 0;
    if (shader_program_pool_size >= 256) {
        shader_program_pool_size = 0; /* Pool full — wrap around (simple eviction) */
    }
    struct ShaderProgram *prg = &shader_program_pool[shader_program_pool_size++];
    prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aVtxPos");
    prg->attrib_sizes[cnt] = 4;
    ++cnt;

    for (int i = 0; i < 2; i++) {
        if (cc_features.used_textures[i]) {
            char name[16];
            ge007_sprintf(name, "aTexCoord%d", i);
            prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, name);
            prg->attrib_sizes[cnt] = 2;
            ++cnt;
            for (int axis = 0; axis < 2; axis++) {
                if (cc_features.clamp[i][axis]) {
                    ge007_sprintf(name, "aTexClamp%c%d", axis == 0 ? 'S' : 'T', i);
                    prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, name);
                    prg->attrib_sizes[cnt] = 1;
                    ++cnt;
                }
            }
        }
    }

    if (cc_features.opt_fog) {
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, "aFog");
        prg->attrib_sizes[cnt] = 4;
        ++cnt;
    }

    for (int i = 0; i < cc_features.num_inputs; i++) {
        char name[16];
        ge007_sprintf(name, "aInput%d", i + 1);
        prg->attrib_locations[cnt] = glGetAttribLocation(shader_program, name);
        prg->attrib_sizes[cnt] = cc_features.opt_alpha ? 4 : 3;
        ++cnt;
    }

    prg->shader_id0 = shader_id0;
    prg->shader_id1 = shader_id1;
    prg->opengl_program_id = shader_program;
    prg->num_inputs = cc_features.num_inputs;
    prg->used_textures[0] = cc_features.used_textures[0];
    prg->used_textures[1] = cc_features.used_textures[1];
    prg->num_floats = num_floats;
    prg->num_attribs = cnt;

    gfx_opengl_load_shader(prg);

    if (cc_features.used_textures[0]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTex0");
        glUniform1i(sampler_location, 0);
    }
    if (cc_features.used_textures[1]) {
        GLint sampler_location = glGetUniformLocation(shader_program, "uTex1");
        glUniform1i(sampler_location, 1);
    }

    if (needs_noise) {
        prg->frame_count_location = glGetUniformLocation(shader_program, "frame_count");
        prg->window_height_location = glGetUniformLocation(shader_program, "window_height");
        prg->used_noise = true;
    } else {
        prg->used_noise = false;
    }

    return prg;
}

static struct ShaderProgram *gfx_opengl_lookup_shader(uint64_t shader_id0, uint32_t shader_id1) {
    for (size_t i = 0; i < shader_program_pool_size; i++) {
        if (shader_program_pool[i].shader_id0 == shader_id0 && shader_program_pool[i].shader_id1 == shader_id1) {
            return &shader_program_pool[i];
        }
    }
    return NULL;
}

static void gfx_opengl_shader_get_info(struct ShaderProgram *prg, uint8_t *num_inputs, bool used_textures[2]) {
    *num_inputs = prg->num_inputs;
    used_textures[0] = prg->used_textures[0];
    used_textures[1] = prg->used_textures[1];
}

static GLuint gfx_opengl_new_texture(void) {
    GLuint ret;
    glGenTextures(1, &ret);
    return ret;
}

static void gfx_opengl_delete_texture(GLuint texture_id) {
    if (texture_id != 0) {
        glDeleteTextures(1, &texture_id);
    }
}

static void gfx_opengl_select_texture(int tile, GLuint texture_id) {
    glActiveTexture(GL_TEXTURE0 + tile);
    glBindTexture(GL_TEXTURE_2D, texture_id);
}

static bool gfx_opengl_upload_texture(const uint8_t *rgba32_buf, int width, int height) {
    if (rgba32_buf == NULL || width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        return false;
    }
    static int unpack_set = 0;
    if (!unpack_set) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        unpack_set = 1;
    }
    while (glGetError() != GL_NO_ERROR) {
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba32_buf);
    /* Treat every uploaded texture as a single-level image. Metal-backed GL is
     * particularly sensitive to incomplete mip state on frontend NPOT uploads
     * such as the 440x1 eye-intro strips, and will silently substitute a zero
     * texture when the object is considered unloadable. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "[GL-TEX-UPLOAD-ERR] width=%d height=%d err=0x%x\n", width, height, err);
        texDebugDumpRecentFireEvents(stderr);
        return false;
    }
    GLint actual_width = 0;
    GLint actual_height = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &actual_width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &actual_height);
    err = glGetError();
    if (err != GL_NO_ERROR || actual_width != width || actual_height != height) {
        fprintf(stderr,
                "[GL-TEX-UPLOAD-BAD] width=%d height=%d actual=%d,%d err=0x%x\n",
                width, height, actual_width, actual_height, err);
        texDebugDumpRecentFireEvents(stderr);
        return false;
    }
    return true;
}

static uint32_t gfx_cm_to_opengl(uint32_t val) {
    if (val & G_TX_CLAMP) {
        return GL_CLAMP_TO_EDGE;
    }
    return (val & G_TX_MIRROR) ? GL_MIRRORED_REPEAT : GL_REPEAT;
}

static void gfx_opengl_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt) {
    glActiveTexture(GL_TEXTURE0 + tile);
    /* Use non-mipmap filters.  On macOS Metal, NPOT textures (32x48, etc.)
     * can fail glGenerateMipmap silently, leaving the texture incomplete.
     * The driver then substitutes a "zero texture" → garbage output.
     * GL_LINEAR/GL_NEAREST without mipmaps avoids this entirely. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    linear_filter ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear_filter ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gfx_cm_to_opengl(cms));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gfx_cm_to_opengl(cmt));
    /* Enable anisotropic filtering if available — critical for textures
     * that tile many times at oblique angles (N64 room/terrain surfaces) */
    static float max_aniso = -1;
    if (max_aniso < 0) {
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_aniso);
        if (max_aniso <= 0) max_aniso = 1;
    }
    if (max_aniso > 1) {
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                        linear_filter ? (max_aniso > 8 ? 8.0f : max_aniso) : 1.0f);
    }
}

/* Unified depth mode — matches PD's set_depth_mode pattern.
 * Separates depth test enable from depth comparison function,
 * handles all four N64 zmodes, and respects prim depth source. */
static void gfx_opengl_set_depth_mode(bool depth_test, bool depth_update,
                                       bool depth_compare, bool depth_source_prim,
                                       uint16_t zmode) {
    if (depth_test) {
        glEnable(GL_DEPTH_TEST);
        glDepthMask(depth_update ? GL_TRUE : GL_FALSE);

        if (depth_compare) {
            switch (zmode) {
                case 0x400: /* ZMODE_INTER */
                    glDepthFunc(GL_LEQUAL);
                    glDisable(GL_POLYGON_OFFSET_FILL);
                    glPolygonOffset(0, 0);
                    break;
                case 0:     /* ZMODE_OPA */
                case 0x800: /* ZMODE_XLU */
                    /* N64 room geometry frequently lands on equal-depth seams
                     * after CPU clipping. Using GL_LESS here leaves thin gaps
                     * where later coplanar/clipped fragments fail the compare,
                     * showing the "blue shard" leaks seen in interior levels. */
                    glDepthFunc(GL_LEQUAL);
                    glDisable(GL_POLYGON_OFFSET_FILL);
                    glPolygonOffset(0, 0);
                    break;
                case 0xc00: /* ZMODE_DEC */
                    glDepthFunc(GL_LEQUAL);
                    glEnable(GL_POLYGON_OFFSET_FILL);
                    glPolygonOffset(-2, -2);
                    break;
            }
        } else {
            /* No depth compare: write depth but always pass.
             * Critical for surfaces that prime the depth buffer
             * without testing against it. */
            glDepthFunc(GL_ALWAYS);
            glDisable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(0, 0);
        }
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

static void gfx_opengl_set_viewport(int x, int y, int width, int height) {
    glViewport(x, y, width, height);
    current_height = height;
}

static void gfx_opengl_set_scissor(int x, int y, int width, int height) {
    glScissor(x, y, width, height);
}

static void gfx_opengl_set_blend_mode(enum GfxBlendMode mode) {
    if (mode == GFX_BLEND_DISABLED) {
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
        if (mode == GFX_BLEND_MODULATE) {
            glBlendFunc(GL_DST_COLOR, GL_ZERO);
        } else {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
    }
}

static void gfx_opengl_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * buf_vbo_len, buf_vbo, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 3 * buf_vbo_num_tris);
}

static int g_diag_output_vi_filter_checked;
static int g_diag_output_vi_filter_enabled;
static int g_diag_output_vi_filter_disabled;
static int g_diag_output_vi_filter_w;
static int g_diag_output_vi_filter_h;
static int g_auto_menu_vi_filter_checked;
static int g_auto_menu_vi_filter_disabled;
static int g_auto_menu_vi_filter_logged;
static int g_auto_gameplay_vi_filter_checked;
static int g_auto_gameplay_vi_filter_enabled;
static int g_auto_gameplay_vi_filter_disabled;
static int g_auto_gameplay_vi_filter_logged;
static int g_diag_output_vi_logical_checked;
static int g_diag_output_vi_logical_enabled;
static int g_diag_output_vi_logical_w;
static int g_diag_output_vi_logical_h;
static GLuint g_output_filter_copy_tex;
static GLuint g_output_filter_low_tex;
static GLuint g_output_filter_logical_tex;
static GLuint g_output_filter_fbo;
static GLuint g_output_filter_program;
static GLuint g_output_filter_vao;
static int g_output_filter_copy_w;
static int g_output_filter_copy_h;
static int g_output_filter_low_w;
static int g_output_filter_low_h;
static int g_output_filter_logical_w;
static int g_output_filter_logical_h;
static int g_diag_output_filter_color_checked;
static float g_diag_output_filter_color_scale = 1.0f;
static float g_diag_output_filter_color_bias;
static GLuint g_scene_fbo;
static GLuint g_scene_color_tex;
static GLuint g_scene_depth_rb;
static GLuint g_scene_msaa_fbo;
static GLuint g_scene_msaa_color_rb;
static GLuint g_scene_msaa_depth_rb;
static int g_scene_w;
static int g_scene_h;
static int g_scene_msaa_w;
static int g_scene_msaa_h;
static int g_scene_msaa_samples;
static bool g_scene_target_bound;
static bool g_scene_target_multisampled;

static float gfx_opengl_effective_render_scale(void) {
    if (g_pcRenderScale < 1.0f) {
        return 1.0f;
    }
    if (g_pcRenderScale > 2.0f) {
        return 2.0f;
    }
    return g_pcRenderScale;
}

static int gfx_opengl_effective_msaa_samples(void) {
    static int max_samples = -1;
    static int last_requested = -1;
    static int last_effective = -1;
    static int warned_clamp;
    int requested = g_pcMsaaSamples;
    int effective = 0;

    if (requested < 2) {
        return 0;
    }

    if (max_samples < 0) {
        GLint gl_max_samples = 0;

        glGetIntegerv(GL_MAX_SAMPLES, &gl_max_samples);
        max_samples = gl_max_samples > 0 ? (int)gl_max_samples : 0;
    }

    if (requested >= 8 && max_samples >= 8) {
        effective = 8;
    } else if (requested >= 4 && max_samples >= 4) {
        effective = 4;
    } else if (requested >= 2 && max_samples >= 2) {
        effective = 2;
    }

    if ((requested != last_requested || effective != last_effective) &&
        requested > 0 && effective != requested && !warned_clamp) {
        fprintf(stderr,
                "[fast3d] Video.MSAA=%d clamped to %d (GL_MAX_SAMPLES=%d)\n",
                requested, effective, max_samples);
        fflush(stderr);
        warned_clamp = 1;
    }
    last_requested = requested;
    last_effective = effective;

    return effective;
}

static bool gfx_opengl_scene_target_enabled(void) {
    float render_scale = gfx_opengl_effective_render_scale();
    return render_scale > 1.001f ||
           gfx_opengl_effective_msaa_samples() > 0;
}

static bool gfx_opengl_ensure_scene_target(int width, int height) {
    GLenum status;
    int samples = gfx_opengl_effective_msaa_samples();

    if (width <= 0 || height <= 0) {
        return false;
    }
    if (g_scene_fbo == 0) {
        glGenFramebuffers(1, &g_scene_fbo);
    }
    if (g_scene_color_tex == 0) {
        glGenTextures(1, &g_scene_color_tex);
    }
    if (g_scene_depth_rb == 0) {
        glGenRenderbuffers(1, &g_scene_depth_rb);
    }

    glBindTexture(GL_TEXTURE_2D, g_scene_color_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    if (g_scene_w != width || g_scene_h != height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindRenderbuffer(GL_RENDERBUFFER, g_scene_depth_rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        g_scene_w = width;
        g_scene_h = height;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, g_scene_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           g_scene_color_tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                              g_scene_depth_rb);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        static int warned;

        if (!warned) {
            fprintf(stderr,
                    "[fast3d] Scene render target incomplete: 0x%04X\n",
                    (unsigned int)status);
            fflush(stderr);
            warned = 1;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    if (samples > 0) {
        if (g_scene_msaa_fbo == 0) {
            glGenFramebuffers(1, &g_scene_msaa_fbo);
        }
        if (g_scene_msaa_color_rb == 0) {
            glGenRenderbuffers(1, &g_scene_msaa_color_rb);
        }
        if (g_scene_msaa_depth_rb == 0) {
            glGenRenderbuffers(1, &g_scene_msaa_depth_rb);
        }

        if (g_scene_msaa_w != width ||
            g_scene_msaa_h != height ||
            g_scene_msaa_samples != samples) {
            glBindRenderbuffer(GL_RENDERBUFFER, g_scene_msaa_color_rb);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8,
                                             width, height);
            glBindRenderbuffer(GL_RENDERBUFFER, g_scene_msaa_depth_rb);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples,
                                             GL_DEPTH_COMPONENT24,
                                             width, height);
            g_scene_msaa_w = width;
            g_scene_msaa_h = height;
            g_scene_msaa_samples = samples;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, g_scene_msaa_fbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER, g_scene_msaa_color_rb);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, g_scene_msaa_depth_rb);
        status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            static int warned_msaa;

            if (!warned_msaa) {
                fprintf(stderr,
                        "[fast3d] MSAA scene render target incomplete: 0x%04X "
                        "(samples=%d)\n",
                        (unsigned int)status, samples);
                fflush(stderr);
                warned_msaa = 1;
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
    }

    return true;
}

static bool gfx_opengl_diag_output_vi_filter(void) {
    if (!g_diag_output_vi_filter_checked) {
        const char *env = getenv("GE007_DIAG_OUTPUT_VI_FILTER");
        int w = 0;
        int h = 0;

        g_diag_output_vi_filter_checked = 1;
        if (env != NULL && env[0] != '\0' && strcmp(env, "0") != 0) {
            if (sscanf(env, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
                g_diag_output_vi_filter_w = w;
                g_diag_output_vi_filter_h = h;
                g_diag_output_vi_filter_enabled = 1;
                fprintf(stderr,
                        "[fast3d] DIAG OUTPUT VI FILTER ENABLED %dx%d "
                        "(GE007_DIAG_OUTPUT_VI_FILTER)\n",
                        w, h);
                fflush(stderr);
            } else {
                fprintf(stderr,
                        "[fast3d] Ignoring invalid GE007_DIAG_OUTPUT_VI_FILTER=%s "
                        "(expected WxH)\n",
                        env);
                fflush(stderr);
            }
        } else if (env != NULL && strcmp(env, "0") == 0) {
            g_diag_output_vi_filter_disabled = 1;
        }
    }

    return g_diag_output_vi_filter_enabled != 0;
}

static bool gfx_opengl_env_flag_enabled(const char *name) {
    const char *env = getenv(name);
    return env != NULL && env[0] != '\0' && strcmp(env, "0") != 0;
}

static bool gfx_opengl_current_menu_uses_auto_vi_filter(void) {
    switch (current_menu) {
        case MENU_FILE_SELECT:
        case MENU_MODE_SELECT:
        case MENU_MISSION_SELECT:
        case MENU_DIFFICULTY:
        case MENU_007_OPTIONS:
        case MENU_BRIEFING:
        case MENU_MISSION_FAILED:
        case MENU_MISSION_COMPLETE:
        case MENU_MP_OPTIONS:
        case MENU_MP_CHAR_SELECT:
        case MENU_MP_HANDICAP:
        case MENU_MP_CONTROL_STYLE:
        case MENU_MP_STAGE_SELECT:
        case MENU_MP_SCENARIO_SELECT:
        case MENU_MP_TEAMS:
        case MENU_CHEAT:
        case MENU_NO_CONTROLLERS:
            return true;
        default:
            return false;
    }
}

static bool gfx_opengl_auto_menu_vi_filter(int *filter_w, int *filter_h) {
    if (!g_auto_menu_vi_filter_checked) {
        g_auto_menu_vi_filter_disabled =
            gfx_opengl_env_flag_enabled("GE007_DISABLE_AUTO_MENU_VI_FILTER") ? 1 : 0;
        if (g_auto_menu_vi_filter_disabled) {
            fprintf(stderr,
                    "[fast3d] Auto menu output VI filter disabled "
                    "(GE007_DISABLE_AUTO_MENU_VI_FILTER)\n");
            fflush(stderr);
        }
        g_auto_menu_vi_filter_checked = 1;
    }

    if (g_pcRetroFilterMode == PC_RETRO_FILTER_OFF ||
        g_auto_menu_vi_filter_disabled ||
        !gfx_opengl_current_menu_uses_auto_vi_filter() ||
        viGetX() != 440 ||
        viGetY() != 330) {
        return false;
    }

    if (!g_auto_menu_vi_filter_logged) {
        fprintf(stderr,
                "[fast3d] Auto paper-menu output VI filter enabled 440x330 "
                "(logical VI 440x330; set GE007_DISABLE_AUTO_MENU_VI_FILTER=1 to disable)\n");
        fflush(stderr);
        g_auto_menu_vi_filter_logged = 1;
    }

    if (filter_w != NULL) {
        *filter_w = 440;
    }
    if (filter_h != NULL) {
        *filter_h = 330;
    }
    return true;
}

static bool gfx_opengl_current_frame_uses_auto_gameplay_vi_filter(void) {
    if (current_menu == MENU_RUN_STAGE || current_menu == MENU_DISPLAY_CAST) {
        return true;
    }

    return current_menu == MENU_INVALID &&
           (gamemode == GAMEMODE_SOLO || gamemode == GAMEMODE_MULTI) &&
           viGetY() <= 240;
}

static bool gfx_opengl_auto_gameplay_vi_filter(int framebuffer_w,
                                               int framebuffer_h,
                                               int *filter_w,
                                               int *filter_h) {
    const int target_h = 240;
    int target_w;
    bool setting_enabled = g_pcRetroFilterMode == PC_RETRO_FILTER_ON;
    bool setting_disabled = g_pcRetroFilterMode == PC_RETRO_FILTER_OFF;

    if (!g_auto_gameplay_vi_filter_checked) {
        /* The N64 VI filter bilinearly downsamples to 240p then upsamples, which
         * softens the entire image. Default it OFF for a sharp picture; users who
         * want the more hardware-accurate (softer) look can opt in. The explicit
         * DISABLE flag is kept so existing configs/scripts keep working. */
        g_auto_gameplay_vi_filter_enabled =
            gfx_opengl_env_flag_enabled("GE007_ENABLE_AUTO_GAMEPLAY_VI_FILTER") ? 1 : 0;
        g_auto_gameplay_vi_filter_disabled =
            gfx_opengl_env_flag_enabled("GE007_DISABLE_AUTO_GAMEPLAY_VI_FILTER") ? 1 : 0;
        if ((setting_enabled || g_auto_gameplay_vi_filter_enabled) &&
            !setting_disabled &&
            !g_auto_gameplay_vi_filter_disabled) {
            fprintf(stderr,
                    "[fast3d] Auto gameplay/display-cast output VI filter enabled "
                    "(Video.RetroFilter or GE007_ENABLE_AUTO_GAMEPLAY_VI_FILTER)\n");
            fflush(stderr);
        }
        g_auto_gameplay_vi_filter_checked = 1;
    }

    if (setting_disabled ||
        (!setting_enabled && !g_auto_gameplay_vi_filter_enabled) ||
        g_auto_gameplay_vi_filter_disabled ||
        framebuffer_w <= 0 ||
        framebuffer_h < target_h) {
        return false;
    }

    if (!gfx_opengl_current_frame_uses_auto_gameplay_vi_filter()) {
        return false;
    }

    target_w = (framebuffer_w * target_h) / framebuffer_h;
    if (target_w < 1) {
        target_w = 1;
    }
    if ((target_w & 1) != 0 && target_w > 1) {
        target_w--;
    }

    if (!g_auto_gameplay_vi_filter_logged) {
        fprintf(stderr,
                "[fast3d] Auto gameplay/display-cast output VI filter target %dx%d "
                "(framebuffer %dx%d; set GE007_DISABLE_AUTO_GAMEPLAY_VI_FILTER=1 to disable)\n",
                target_w, target_h, framebuffer_w, framebuffer_h);
        fflush(stderr);
        g_auto_gameplay_vi_filter_logged = 1;
    }

    if (filter_w != NULL) {
        *filter_w = target_w;
    }
    if (filter_h != NULL) {
        *filter_h = target_h;
    }
    return true;
}

static bool gfx_opengl_output_vi_filter_target(int framebuffer_w,
                                               int framebuffer_h,
                                               int *filter_w,
                                               int *filter_h) {
    if (gfx_opengl_diag_output_vi_filter()) {
        if (filter_w != NULL) {
            *filter_w = g_diag_output_vi_filter_w;
        }
        if (filter_h != NULL) {
            *filter_h = g_diag_output_vi_filter_h;
        }
        return true;
    }

    if (g_diag_output_vi_filter_disabled) {
        return false;
    }

    if (gfx_opengl_auto_menu_vi_filter(filter_w, filter_h)) {
        return true;
    }

    return gfx_opengl_auto_gameplay_vi_filter(framebuffer_w, framebuffer_h,
                                              filter_w, filter_h);
}

static bool gfx_opengl_diag_output_vi_logical_size(int *width, int *height) {
    if (!g_diag_output_vi_logical_checked) {
        const char *env = getenv("GE007_DIAG_OUTPUT_VI_LOGICAL_SIZE");
        int w = 0;
        int h = 0;

        g_diag_output_vi_logical_checked = 1;
        if (env != NULL && env[0] != '\0' && strcmp(env, "0") != 0) {
            if (sscanf(env, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
                g_diag_output_vi_logical_w = w;
                g_diag_output_vi_logical_h = h;
                g_diag_output_vi_logical_enabled = 1;
                fprintf(stderr,
                        "[fast3d] DIAG OUTPUT VI LOGICAL SIZE ENABLED %dx%d "
                        "(GE007_DIAG_OUTPUT_VI_LOGICAL_SIZE)\n",
                        w, h);
                fflush(stderr);
            } else {
                fprintf(stderr,
                        "[fast3d] Ignoring invalid GE007_DIAG_OUTPUT_VI_LOGICAL_SIZE=%s "
                        "(expected WxH)\n",
                        env);
                fflush(stderr);
            }
        }
    }

    if (g_diag_output_vi_logical_enabled) {
        if (width != NULL) {
            *width = g_diag_output_vi_logical_w;
        }
        if (height != NULL) {
            *height = g_diag_output_vi_logical_h;
        }
        return true;
    }

    return false;
}

static void gfx_opengl_check_output_filter_color_diag(void) {
    if (!g_diag_output_filter_color_checked) {
        const char *scale_env = getenv("GE007_DIAG_OUTPUT_COLOR_SCALE");
        const char *bias_env = getenv("GE007_DIAG_OUTPUT_COLOR_BIAS");

        g_diag_output_filter_color_checked = 1;
        if (scale_env != NULL && scale_env[0] != '\0') {
            g_diag_output_filter_color_scale = (float)atof(scale_env);
        }
        if (bias_env != NULL && bias_env[0] != '\0') {
            g_diag_output_filter_color_bias = (float)atof(bias_env);
        }
        if (g_diag_output_filter_color_scale != 1.0f ||
            g_diag_output_filter_color_bias != 0.0f) {
            fprintf(stderr,
                    "[fast3d] DIAG OUTPUT COLOR scale=%.6f bias=%.6f "
                    "(GE007_DIAG_OUTPUT_COLOR_SCALE/BIAS)\n",
                    g_diag_output_filter_color_scale,
                    g_diag_output_filter_color_bias);
            fflush(stderr);
        }
    }
}

static float gfx_opengl_output_gamma(void) {
    if (g_pcVideoGamma < 0.5f) {
        return 0.5f;
    }
    if (g_pcVideoGamma > 2.5f) {
        return 2.5f;
    }
    return g_pcVideoGamma;
}

static bool gfx_opengl_output_color_adjust_active(void) {
    float gamma = gfx_opengl_output_gamma();

    return g_diag_output_filter_color_scale != 1.0f ||
           g_diag_output_filter_color_bias != 0.0f ||
           gamma < 0.999f ||
           gamma > 1.001f;
}

static GLuint gfx_opengl_compile_filter_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    GLint success = GL_FALSE;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success != GL_TRUE) {
        char error_log[1024];
        GLsizei length = 0;

        glGetShaderInfoLog(shader, sizeof(error_log), &length, error_log);
        fprintf(stderr,
                "[fast3d] Output VI filter shader compile failed: %.*s\n",
                (int)length, error_log);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static bool gfx_opengl_ensure_output_filter_program(void) {
    static const char *vs_source =
        "#version 330 core\n"
        "out vec2 vTexCoord;\n"
        "const vec2 kPos[3] = vec2[3](\n"
        "    vec2(-1.0, -1.0),\n"
        "    vec2( 3.0, -1.0),\n"
        "    vec2(-1.0,  3.0));\n"
        "void main() {\n"
        "    vec2 pos = kPos[gl_VertexID];\n"
        "    vTexCoord = pos * 0.5 + 0.5;\n"
        "    gl_Position = vec4(pos, 0.0, 1.0);\n"
        "}\n";
    static const char *fs_source =
        "#version 330 core\n"
        "uniform sampler2D uTex;\n"
        "uniform vec2 uSrcSize;\n"
        "uniform vec2 uDstSize;\n"
        "uniform float uColorScale;\n"
        "uniform float uColorBias;\n"
        "uniform float uGamma;\n"
        "uniform int uFilterMode;\n"
        "in vec2 vTexCoord;\n"
        "out vec4 outColor;\n"
        "vec4 sampleNearest(vec2 dstCoord) {\n"
        "    ivec2 p = ivec2(floor(dstCoord * uSrcSize / uDstSize));\n"
        "    p = clamp(p, ivec2(0), ivec2(uSrcSize) - ivec2(1));\n"
        "    return texelFetch(uTex, p, 0);\n"
        "}\n"
        "vec2 fitSizeForAspect(vec2 boundsSize, float aspect) {\n"
        "    float boundsAspect = boundsSize.x / boundsSize.y;\n"
        "    if (boundsAspect > aspect) {\n"
        "        return vec2(boundsSize.y * aspect, boundsSize.y);\n"
        "    }\n"
        "    return vec2(boundsSize.x, boundsSize.x / aspect);\n"
        "}\n"
        "vec4 sampleFitSrcToDstNearest(vec2 dstCoord) {\n"
        "    float srcAspect = uSrcSize.x / uSrcSize.y;\n"
        "    vec2 fitSize = fitSizeForAspect(uDstSize, srcAspect);\n"
        "    vec2 offset = floor((uDstSize - fitSize) * 0.5);\n"
        "    if (dstCoord.x < offset.x || dstCoord.y < offset.y ||\n"
        "        dstCoord.x >= offset.x + fitSize.x || dstCoord.y >= offset.y + fitSize.y) {\n"
        "        return vec4(0.0, 0.0, 0.0, 1.0);\n"
        "    }\n"
        "    ivec2 p = ivec2(floor((dstCoord - offset) * uSrcSize / fitSize));\n"
        "    p = clamp(p, ivec2(0), ivec2(uSrcSize) - ivec2(1));\n"
        "    return texelFetch(uTex, p, 0);\n"
        "}\n"
        "vec4 sampleFitLogicalToDstNearest(vec2 dstCoord) {\n"
        "    float dstAspect = uDstSize.x / uDstSize.y;\n"
        "    vec2 fitSize = fitSizeForAspect(uSrcSize, dstAspect);\n"
        "    vec2 offset = floor((uSrcSize - fitSize) * 0.5);\n"
        "    ivec2 p = ivec2(floor(offset + dstCoord * fitSize / uDstSize));\n"
        "    p = clamp(p, ivec2(0), ivec2(uSrcSize) - ivec2(1));\n"
        "    return texelFetch(uTex, p, 0);\n"
        "}\n"
        "vec4 sampleCpuBilinear(vec2 dstCoord) {\n"
        "    vec2 srcCoord = dstCoord * uSrcSize / uDstSize - vec2(0.5);\n"
        "    ivec2 p0 = ivec2(floor(srcCoord));\n"
        "    vec2 f = srcCoord - vec2(p0);\n"
        "    if (p0.x < 0) { p0.x = 0; f.x = 0.0; }\n"
        "    else if (p0.x >= int(uSrcSize.x) - 1) { p0.x = int(uSrcSize.x) - 1; f.x = 0.0; }\n"
        "    if (p0.y < 0) { p0.y = 0; f.y = 0.0; }\n"
        "    else if (p0.y >= int(uSrcSize.y) - 1) { p0.y = int(uSrcSize.y) - 1; f.y = 0.0; }\n"
        "    ivec2 p1 = min(p0 + ivec2(1), ivec2(uSrcSize) - ivec2(1));\n"
        "    vec4 c00 = texelFetch(uTex, p0, 0);\n"
        "    vec4 c10 = texelFetch(uTex, ivec2(p1.x, p0.y), 0);\n"
        "    vec4 c01 = texelFetch(uTex, ivec2(p0.x, p1.y), 0);\n"
        "    vec4 c11 = texelFetch(uTex, p1, 0);\n"
        "    return mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);\n"
        "}\n"
        "void main() {\n"
        "    vec4 color;\n"
        "    if (uFilterMode == 1) color = sampleNearest(gl_FragCoord.xy);\n"
        "    else if (uFilterMode == 2) color = sampleFitSrcToDstNearest(gl_FragCoord.xy);\n"
        "    else if (uFilterMode == 3) color = sampleFitLogicalToDstNearest(gl_FragCoord.xy);\n"
        "    else color = sampleCpuBilinear(gl_FragCoord.xy);\n"
        "    vec3 rgb = clamp(color.rgb * uColorScale + vec3(uColorBias / 255.0), 0.0, 1.0);\n"
        "    rgb = pow(rgb, vec3(1.0 / max(uGamma, 0.001)));\n"
        "    outColor = vec4(rgb, color.a);\n"
        "}\n";

    if (g_output_filter_program == 0) {
        GLuint vs = gfx_opengl_compile_filter_shader(GL_VERTEX_SHADER, vs_source);
        GLuint fs = gfx_opengl_compile_filter_shader(GL_FRAGMENT_SHADER, fs_source);
        GLint success = GL_FALSE;

        if (vs == 0 || fs == 0) {
            glDeleteShader(vs);
            glDeleteShader(fs);
            return false;
        }

        g_output_filter_program = glCreateProgram();
        glAttachShader(g_output_filter_program, vs);
        glAttachShader(g_output_filter_program, fs);
        glLinkProgram(g_output_filter_program);
        glDeleteShader(vs);
        glDeleteShader(fs);

        glGetProgramiv(g_output_filter_program, GL_LINK_STATUS, &success);
        if (success != GL_TRUE) {
            char error_log[1024];
            GLsizei length = 0;

            glGetProgramInfoLog(g_output_filter_program, sizeof(error_log), &length, error_log);
            fprintf(stderr,
                    "[fast3d] Output VI filter program link failed: %.*s\n",
                    (int)length, error_log);
            glDeleteProgram(g_output_filter_program);
            g_output_filter_program = 0;
            return false;
        }

    }

    if (g_output_filter_vao == 0) {
        glGenVertexArrays(1, &g_output_filter_vao);
    }

    if (g_output_filter_fbo == 0) {
        glGenFramebuffers(1, &g_output_filter_fbo);
    }

    return g_output_filter_program != 0 && g_output_filter_vao != 0 && g_output_filter_fbo != 0;
}

static void gfx_opengl_ensure_filter_texture(GLuint *tex_id,
                                             int *tex_w,
                                             int *tex_h,
                                             int width,
                                             int height) {
    if (*tex_id == 0) {
        glGenTextures(1, tex_id);
    }

    glBindTexture(GL_TEXTURE_2D, *tex_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    if (*tex_w != width || *tex_h != height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        *tex_w = width;
        *tex_h = height;
    }
}

static void gfx_opengl_draw_output_filter_texture(GLuint texture_id,
                                                  int src_w, int src_h,
                                                  int dst_w, int dst_h,
                                                  int filter_mode) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glUseProgram(g_output_filter_program);
    glUniform1i(glGetUniformLocation(g_output_filter_program, "uTex"), 0);
    glUniform2f(glGetUniformLocation(g_output_filter_program, "uSrcSize"),
                (float)src_w, (float)src_h);
    glUniform2f(glGetUniformLocation(g_output_filter_program, "uDstSize"),
                (float)dst_w, (float)dst_h);
    glUniform1f(glGetUniformLocation(g_output_filter_program, "uColorScale"),
                g_diag_output_filter_color_scale);
    glUniform1f(glGetUniformLocation(g_output_filter_program, "uColorBias"),
                g_diag_output_filter_color_bias);
    glUniform1f(glGetUniformLocation(g_output_filter_program, "uGamma"),
                gfx_opengl_output_gamma());
    glUniform1i(glGetUniformLocation(g_output_filter_program, "uFilterMode"),
                filter_mode);
    glBindVertexArray(g_output_filter_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void gfx_opengl_apply_output_vi_filter(void) {
    GLint viewport[4];
    GLint saved_program = 0;
    GLint saved_vao = 0;
    GLint saved_active_texture = 0;
    GLint saved_texture0 = 0;
    GLint saved_draw_fbo = 0;
    GLint saved_read_fbo = 0;
    GLint saved_array_buffer = 0;
    GLboolean saved_scissor = GL_FALSE;
    GLboolean saved_depth_test = GL_FALSE;
    GLboolean saved_blend = GL_FALSE;
    GLboolean saved_dither = GL_FALSE;
    GLboolean saved_depth_mask = GL_TRUE;
    int width;
    int height;
    int filter_w = 0;
    int filter_h = 0;
    int logical_w = 0;
    int logical_h = 0;
    bool use_logical_size = false;
    GLuint filter_source_tex;
    int filter_source_w;
    int filter_source_h;
    bool use_vi_filter;
    bool use_color_adjust;
    extern SDL_Window *g_sdlWindow;
    int drawable_w = 0;
    int drawable_h = 0;

    glGetIntegerv(GL_VIEWPORT, viewport);
    if (g_sdlWindow != NULL) {
        SDL_GL_GetDrawableSize(g_sdlWindow, &drawable_w, &drawable_h);
    }
    width = drawable_w > 0 ? drawable_w : viewport[2];
    height = drawable_h > 0 ? drawable_h : viewport[3];
    gfx_opengl_check_output_filter_color_diag();
    use_vi_filter = gfx_opengl_output_vi_filter_target(width, height, &filter_w, &filter_h);
    use_color_adjust = gfx_opengl_output_color_adjust_active();
    if (!use_vi_filter && !use_color_adjust) {
        return;
    }
    if (!use_vi_filter) {
        filter_w = width;
        filter_h = height;
    }

    if (width <= 0 || height <= 0 || filter_w <= 0 || filter_h <= 0) {
        return;
    }

    if (filter_w > width || filter_h > height) {
        static int warned_too_large;

        if (!warned_too_large) {
            fprintf(stderr,
                    "[fast3d] Ignoring GE007_DIAG_OUTPUT_VI_FILTER=%dx%d for "
                    "%dx%d framebuffer\n",
                    filter_w, filter_h, width, height);
            fflush(stderr);
            warned_too_large = 1;
        }
        return;
    }

    use_logical_size = gfx_opengl_diag_output_vi_logical_size(&logical_w, &logical_h);
    if (use_logical_size &&
        (logical_w <= 0 || logical_h <= 0 ||
         logical_w > width || logical_h > height ||
         filter_w > logical_w || filter_h > logical_h)) {
        static int warned_logical_invalid;

        if (!warned_logical_invalid) {
            fprintf(stderr,
                    "[fast3d] Ignoring GE007_DIAG_OUTPUT_VI_LOGICAL_SIZE=%dx%d "
                    "for %dx%d framebuffer and %dx%d filter\n",
                    logical_w, logical_h, width, height, filter_w, filter_h);
            fflush(stderr);
            warned_logical_invalid = 1;
        }
        use_logical_size = false;
    }

    glGetIntegerv(GL_CURRENT_PROGRAM, &saved_program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &saved_vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &saved_active_texture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved_texture0);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &saved_draw_fbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &saved_read_fbo);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &saved_array_buffer);
    saved_scissor = glIsEnabled(GL_SCISSOR_TEST);
    saved_depth_test = glIsEnabled(GL_DEPTH_TEST);
    saved_blend = glIsEnabled(GL_BLEND);
    saved_dither = glIsEnabled(GL_DITHER);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &saved_depth_mask);

    if (!gfx_opengl_ensure_output_filter_program()) {
        glActiveTexture((GLenum)saved_active_texture);
        return;
    }

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DITHER);
    glDepthMask(GL_FALSE);

    gfx_opengl_ensure_filter_texture(&g_output_filter_copy_tex,
                                     &g_output_filter_copy_w,
                                     &g_output_filter_copy_h,
                                     width, height);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);

    filter_source_tex = g_output_filter_copy_tex;
    filter_source_w = width;
    filter_source_h = height;

    if (use_logical_size) {
        gfx_opengl_ensure_filter_texture(&g_output_filter_logical_tex,
                                         &g_output_filter_logical_w,
                                         &g_output_filter_logical_h,
                                         logical_w, logical_h);
        glBindFramebuffer(GL_FRAMEBUFFER, g_output_filter_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               g_output_filter_logical_tex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            glViewport(0, 0, logical_w, logical_h);
            gfx_opengl_draw_output_filter_texture(g_output_filter_copy_tex,
                                                  width, height,
                                                  logical_w, logical_h,
                                                  2);
            filter_source_tex = g_output_filter_logical_tex;
            filter_source_w = logical_w;
            filter_source_h = logical_h;
        }
    }

    gfx_opengl_ensure_filter_texture(&g_output_filter_low_tex,
                                     &g_output_filter_low_w,
                                     &g_output_filter_low_h,
                                     filter_w, filter_h);
    glBindFramebuffer(GL_FRAMEBUFFER, g_output_filter_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           g_output_filter_low_tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        glViewport(0, 0, filter_w, filter_h);
        gfx_opengl_draw_output_filter_texture(filter_source_tex,
                                              filter_source_w, filter_source_h,
                                              filter_w, filter_h,
                                              0);

        if (use_logical_size) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   g_output_filter_logical_tex, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
                glViewport(0, 0, logical_w, logical_h);
                gfx_opengl_draw_output_filter_texture(g_output_filter_low_tex,
                                                      filter_w, filter_h,
                                                      logical_w, logical_h,
                                                      0);

                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0, 0, width, height);
                gfx_opengl_draw_output_filter_texture(g_output_filter_logical_tex,
                                                      logical_w, logical_h,
                                                      width, height,
                                                      3);
            }
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, width, height);
            gfx_opengl_draw_output_filter_texture(g_output_filter_low_tex,
                                                  filter_w, filter_h,
                                                  width, height,
                                                  0);
        }
    }

    if (saved_blend) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    if (saved_depth_test) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    if (saved_scissor) {
        glEnable(GL_SCISSOR_TEST);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
    if (saved_dither) {
        glEnable(GL_DITHER);
    } else {
        glDisable(GL_DITHER);
    }
    glDepthMask(saved_depth_mask);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)saved_read_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)saved_draw_fbo);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)saved_array_buffer);
    glBindVertexArray((GLuint)saved_vao);
    glUseProgram((GLuint)saved_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)saved_texture0);
    glActiveTexture((GLenum)saved_active_texture);
}

static void gfx_opengl_init(void) {
    /* glad is already loaded by platform_sdl.c before this is called */

    glGenVertexArrays(1, &opengl_vao);
    glBindVertexArray(opengl_vao);

    glGenBuffers(1, &opengl_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);

    glDepthFunc(GL_LESS);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* GL_DEPTH_CLAMP: prevents near/far clipping, clamping depth values
     * instead of discarding fragments. Core in OpenGL 3.2+.
     * When enabled, we skip the z*=0.3 depth scaling hack in the vertex
     * shader and CPU clipper, matching the PD port's preferred approach. */
    {
        GLint major = 0, minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);

        bool can_depth_clamp = (major > 3 || (major == 3 && minor >= 2));

        /* Allow override: GE007_NO_DEPTH_CLAMP=1 forces fallback z*=0.3 */
        if (getenv("GE007_NO_DEPTH_CLAMP")) {
            can_depth_clamp = false;
        }

        if (can_depth_clamp) {
            glEnable(GL_DEPTH_CLAMP);
            GLenum err = glGetError();
            if (err == GL_NO_ERROR) {
                g_depth_clamp_enabled = true;
                printf("[fast3d] GL_DEPTH_CLAMP enabled (GL %d.%d)\n", major, minor);
            } else {
                g_depth_clamp_enabled = false;
                printf("[fast3d] GL_DEPTH_CLAMP failed (err=0x%04X), using z*=0.3 fallback\n", err);
            }
        } else {
            g_depth_clamp_enabled = false;
            printf("[fast3d] GL_DEPTH_CLAMP not available (GL %d.%d), using z*=0.3 fallback\n", major, minor);
        }
    }

    printf("[fast3d] OpenGL %s, GLSL %s\n",
           glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
}

static void gfx_opengl_on_resize(void) {
    g_scene_w = 0;
    g_scene_h = 0;
    g_scene_msaa_w = 0;
    g_scene_msaa_h = 0;
    g_scene_msaa_samples = 0;
}

static float g_clear_r = 0, g_clear_g = 0, g_clear_b = 0;

void gfx_opengl_set_clear_color(float r, float g, float b) {
    g_clear_r = r; g_clear_g = g; g_clear_b = b;
}

static int wireframe_checked = 0, wireframe_on = 0;

static void gfx_opengl_start_frame(void) {
    frame_count++;
    g_scene_target_bound = false;
    g_scene_target_multisampled = false;

    if (gfx_opengl_scene_target_enabled() &&
        gfx_opengl_ensure_scene_target((int)gfx_current_dimensions.width,
                                       (int)gfx_current_dimensions.height)) {
        g_scene_target_bound = true;
        g_scene_target_multisampled = gfx_opengl_effective_msaa_samples() > 0;
        glBindFramebuffer(GL_FRAMEBUFFER,
                          g_scene_target_multisampled ? g_scene_msaa_fbo : g_scene_fbo);
        glViewport(0, 0, g_scene_w, g_scene_h);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    if (!wireframe_checked) {
        wireframe_on = (getenv("GE007_WIREFRAME") != NULL);
        wireframe_checked = 1;
    }
    if (wireframe_on) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }

    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glClearColor(g_clear_r, g_clear_g, g_clear_b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
}

static void gfx_opengl_resolve_scene_target(void) {
    extern SDL_Window *g_sdlWindow;
    int drawable_w = 0;
    int drawable_h = 0;

    if (!g_scene_target_bound || g_scene_fbo == 0) {
        return;
    }

    if (g_sdlWindow != NULL) {
        SDL_GL_GetDrawableSize(g_sdlWindow, &drawable_w, &drawable_h);
    }
    if (drawable_w <= 0 || drawable_h <= 0) {
        drawable_w = g_scene_w;
        drawable_h = g_scene_h;
    }

    glDisable(GL_SCISSOR_TEST);
    if (g_scene_target_multisampled) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_scene_msaa_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_scene_fbo);
        glBlitFramebuffer(0, 0, g_scene_w, g_scene_h,
                          0, 0, g_scene_w, g_scene_h,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_scene_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, g_scene_w, g_scene_h,
                      0, 0, drawable_w, drawable_h,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, drawable_w, drawable_h);
    g_scene_target_bound = false;
}

static void gfx_opengl_end_frame(void) {
    gfx_opengl_resolve_scene_target();
    gfx_opengl_apply_output_vi_filter();
}

static void gfx_opengl_finish_render(void) {
}

struct GfxRenderingAPI gfx_opengl_api = {
    gfx_opengl_z_is_from_0_to_1,
    gfx_opengl_unload_shader,
    gfx_opengl_load_shader,
    gfx_opengl_create_and_load_new_shader,
    gfx_opengl_lookup_shader,
    gfx_opengl_shader_get_info,
    gfx_opengl_new_texture,
    gfx_opengl_delete_texture,
    gfx_opengl_select_texture,
    gfx_opengl_upload_texture,
    gfx_opengl_set_sampler_parameters,
    gfx_opengl_set_depth_mode,
    gfx_opengl_set_viewport,
    gfx_opengl_set_scissor,
    gfx_opengl_set_blend_mode,
    gfx_opengl_draw_triangles,
    gfx_opengl_init,
    gfx_opengl_on_resize,
    gfx_opengl_start_frame,
    gfx_opengl_end_frame,
    gfx_opengl_finish_render
};
