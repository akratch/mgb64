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
 *
 * Phase 1: stub scaffold — every entry point is present (so the backend seam in
 * gfx_pc.c and the linker resolve) but does no rendering yet. Bring-up follows.
 */
/* This project ships include/math.h — an N64 native-port shim (guard
 * _MATH_EXT_H_) that sits on the -I path ahead of the SDK and therefore SHADOWS
 * the system <math.h>. It declares only a few float funcs and lacks
 * float_t/double_t, so the legacy Carbon <fp.h> pulled in transitively by the
 * Metal/QuartzCore umbrella headers fails to compile ("unknown type name
 * 'double_t'"). fp.h is deprecated (its own header says "Use math.h instead")
 * and nothing in the Metal path needs it, so we suppress it via its own include
 * guard. This is LOCAL to this TU — the shared shim is untouched, so the C
 * game/render TUs stay byte-identical. */
#define __FP__  /* skip <CoreServices/.../CarbonCore/fp.h> */

#import <Metal/Metal.h>

#include <stdio.h>
#include "gfx_rendering_api.h"

/* ---- Phase 1 stubs: no-op / default returns ------------------------------- */

static bool mtl_z_is_from_0_to_1(void) {
    return true; /* Metal NDC z is [0,1]; gfx_pc.c already remaps clip z for this */
}
static void mtl_unload_shader(struct ShaderProgram *old_prg) { (void)old_prg; }
static void mtl_load_shader(struct ShaderProgram *new_prg) { (void)new_prg; }
static struct ShaderProgram *mtl_create_and_load_new_shader(uint64_t id0, uint32_t id1) {
    (void)id0; (void)id1; return nullptr;
}
static struct ShaderProgram *mtl_lookup_shader(uint64_t id0, uint32_t id1) {
    (void)id0; (void)id1; return nullptr;
}
static void mtl_shader_get_info(struct ShaderProgram *prg, uint8_t *num_inputs, bool used_textures[2]) {
    (void)prg;
    if (num_inputs) *num_inputs = 0;
    if (used_textures) { used_textures[0] = false; used_textures[1] = false; }
}
static uint32_t mtl_new_texture(void) { return 0; }
static void mtl_delete_texture(uint32_t texture_id) { (void)texture_id; }
static void mtl_select_texture(int tile, uint32_t texture_id) { (void)tile; (void)texture_id; }
static bool mtl_upload_texture(const uint8_t *rgba32_buf, int width, int height) {
    (void)rgba32_buf; (void)width; (void)height; return false;
}
static void mtl_set_sampler_parameters(int sampler, bool linear_filter, uint32_t cms, uint32_t cmt) {
    (void)sampler; (void)linear_filter; (void)cms; (void)cmt;
}
static void mtl_set_depth_mode(bool depth_test, bool depth_update, bool depth_compare,
                               bool depth_source_prim, uint16_t zmode) {
    (void)depth_test; (void)depth_update; (void)depth_compare; (void)depth_source_prim; (void)zmode;
}
static void mtl_set_viewport(int x, int y, int width, int height) {
    (void)x; (void)y; (void)width; (void)height;
}
static void mtl_set_scissor(int x, int y, int width, int height) {
    (void)x; (void)y; (void)width; (void)height;
}
static void mtl_set_blend_mode(enum GfxBlendMode mode) { (void)mode; }
static void mtl_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    (void)buf_vbo; (void)buf_vbo_len; (void)buf_vbo_num_tris;
}
static bool mtl_read_framebuffer_rgb(int x, int y, int width, int height, uint8_t *rgb_out) {
    (void)x; (void)y; (void)width; (void)height; (void)rgb_out; return false;
}
static void mtl_init(void) {
    fprintf(stderr, "[metal] native Metal backend selected (Phase 1: bring-up stub)\n");
}
static void mtl_on_resize(void) {}
static void mtl_start_frame(void) {}
static void mtl_end_frame(void) {}
static void mtl_finish_render(void) {}

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
