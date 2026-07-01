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
 * Phase 1 (bring-up): device/queue/CAMetalLayer setup, and a per-frame clear +
 * present. Geometry, textures, shaders, and readback are stubbed (Phases 2-4).
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
#include "gfx_rendering_api.h"

extern "C" void *platformGetMetalLayer(void);  /* platform_sdl.c */

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

/* ---- Bring-up: init / clear / present ------------------------------------- */

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
        s_layer.framebufferOnly = YES;  /* present-only for now; readback lands in Phase 3/4 */
    } else {
        fprintf(stderr, "[metal] WARNING: no CAMetalLayer from platform — present will be skipped\n");
    }
    fprintf(stderr, "[metal] native Metal backend init: device='%s' layer=%p\n",
            s_device.name.UTF8String, (__bridge void *)s_layer);
}

static void mtl_start_frame(void) {
    if (s_layer == nil || s_queue == nil) return;
    @autoreleasepool {
        s_drawable = [s_layer nextDrawable];
        if (s_drawable == nil) return;
        MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
        rpd.colorAttachments[0].texture = s_drawable.texture;
        rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
        rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
        rpd.colorAttachments[0].clearColor = MTLClearColorMake(s_clear_r, s_clear_g, s_clear_b, 1.0);
        s_cmdbuf = [s_queue commandBuffer];
        /* Phase 1: open+close the encoder so loadAction=Clear runs. Geometry
         * (draw_triangles) will record into this encoder in Phase 3. */
        id<MTLRenderCommandEncoder> enc = [s_cmdbuf renderCommandEncoderWithDescriptor:rpd];
        [enc endEncoding];
        if (!s_logged_first_frame) {
            fprintf(stderr, "[metal] first frame: drawable %.0fx%.0f, clear pass encoded\n",
                    (double)s_drawable.texture.width, (double)s_drawable.texture.height);
            s_logged_first_frame = true;
        }
    }
}

static void mtl_end_frame(void) {
    @autoreleasepool {
        if (s_cmdbuf != nil && s_drawable != nil) {
            [s_cmdbuf presentDrawable:s_drawable];
            [s_cmdbuf commit];
        }
        s_drawable = nil;
        s_cmdbuf = nil;
    }
}

static void mtl_finish_render(void) {
    /* Phase 1: nothing to flush beyond the committed present. A blocking
     * waitUntilCompleted lands with the readback path (Phase 3). */
}

static void mtl_on_resize(void) {
    /* SDL's Metal view resizes the CAMetalLayer with the window; nextDrawable
     * tracks it. Explicit drawableSize management lands with the offscreen
     * targets (Phase 4). */
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

/* ---- Phase 2/3 stubs -------------------------------------------------------
 * Bring-up must not crash the DL interpreter, which treats ShaderProgram* and
 * texture ids opaquely (gfx_pc.c never dereferences them). So we hand back
 * benign non-null/valid tokens instead of null/0: the interpreter runs its full
 * per-frame work, but draw_triangles is a no-op, so nothing is rasterized and
 * each frame is just the clear. Real programs/textures land in Phases 2-3. */
static char s_dummy_shader_token;  /* opaque non-null sentinel */

static bool mtl_z_is_from_0_to_1(void) {
    return true; /* Metal NDC z is [0,1]; gfx_pc.c already remaps clip z for this */
}
static void mtl_unload_shader(struct ShaderProgram *old_prg) { (void)old_prg; }
static void mtl_load_shader(struct ShaderProgram *new_prg) { (void)new_prg; }
static struct ShaderProgram *mtl_create_and_load_new_shader(uint64_t id0, uint32_t id1) {
    (void)id0; (void)id1; return (struct ShaderProgram *)&s_dummy_shader_token;
}
static struct ShaderProgram *mtl_lookup_shader(uint64_t id0, uint32_t id1) {
    (void)id0; (void)id1; return (struct ShaderProgram *)&s_dummy_shader_token;
}
static void mtl_shader_get_info(struct ShaderProgram *prg, uint8_t *num_inputs, bool used_textures[2]) {
    (void)prg;
    if (num_inputs) *num_inputs = 0;
    if (used_textures) { used_textures[0] = false; used_textures[1] = false; }
}
static uint32_t mtl_new_texture(void) {
    static uint32_t next_id = 1;  /* nonzero, monotonic — never 0 (0 reads as "unset") */
    return next_id++;
}
static void mtl_delete_texture(uint32_t texture_id) { (void)texture_id; }
static void mtl_select_texture(int tile, uint32_t texture_id) { (void)tile; (void)texture_id; }
static bool mtl_upload_texture(const uint8_t *rgba32_buf, int width, int height) {
    (void)rgba32_buf; (void)width; (void)height; return true; /* pretend success; no GPU upload yet */
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
