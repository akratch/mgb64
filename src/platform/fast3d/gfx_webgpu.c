/*
 * gfx_webgpu.c — Cross-platform WebGPU (wgpu-native) render backend.
 *
 * Implements the Fast3D `GfxRenderingAPI` vtable (gfx_rendering_api.h) — the
 * same ~23-function seam gfx_opengl.c and gfx_metal.mm fill — against the
 * standard webgpu.h C API. Selected at runtime by GE007_RENDERER=webgpu;
 * compiled and linked only when MGB64_WEBGPU_BACKEND (see cmake/webgpu.cmake).
 * Off by default so the OpenGL backend stays byte-identical until the
 * deliberate flip (Task 8 of docs/superpowers/plans/2026-07-13-webgpu-backend.md).
 *
 * TASK 1 SCOPE (this file at this stage): bring-up + a cleared frame in the
 * real MGB64 window.
 *   - init()        creates instance -> surface -> adapter -> device -> queue
 *                   (the spike's proven sequence) plus a WGPUSurface from the
 *                   platform window (a CAMetalLayer on macOS).
 *   - start_frame() lazily (re)configures the surface to the frontend
 *                   resolution, acquires the swapchain texture, and opens a
 *                   render pass that CLEARS to the frame's charcoal color.
 *   - end_frame()   ends the pass, submits, and presents.
 *   - on_resize()   forces a reconfigure on the next frame.
 * The shader / texture / state / draw entry points are deliberately safe
 * no-op stubs so gfx_pc.c's full DL-interpreter frame loop runs to a cleared
 * frame WITHOUT crashing. They are implemented in Tasks 2-6:
 *   T2 textures+samplers, T3 WGSL combiner emitter + draw_triangles,
 *   T4 depth/viewport/scissor/blend -> parity, T5 read_framebuffer_rgb,
 *   T6 draw_modern_mesh.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>   /* wgpuDevicePoll + native extensions */

#include "gfx_rendering_api.h"

/* gfx_current_dimensions is the frontend render resolution the viewports and
 * T&L are computed against (gfx_pc.c). Declared here exactly as gfx_metal.mm
 * does — the shared header is C++-guarded, so each backend re-declares the POD
 * view it needs. Read every frame to size the swapchain. */
struct GfxDimensions { uint32_t width, height; float aspect_ratio; };
extern struct GfxDimensions gfx_current_dimensions;

/* Platform window handle. On macOS the SDL window is a Metal-layer window
 * (platform_sdl.c creates the view when gfx_backend_use_webgpu() is true) and
 * platformGetMetalLayer() hands back its CAMetalLayer, which wgpu-native wraps
 * as a WGPUSurface. HWND/X11/Wayland handles arrive in Task 7. */
#ifdef __APPLE__
extern void *platformGetMetalLayer(void);
#endif

/* ------------------------------------------------------------------------
 * Backend state
 * ---------------------------------------------------------------------- */
static WGPUInstance      s_instance = NULL;
static WGPUAdapter       s_adapter  = NULL;
static WGPUDevice        s_device   = NULL;
static WGPUQueue         s_queue    = NULL;
static WGPUSurface       s_surface  = NULL;
static WGPUTextureFormat s_surface_format = WGPUTextureFormat_Undefined;
static bool              s_ready    = false;   /* device + surface both live */

/* Configured swapchain size; 0 forces a (re)configure on the next start_frame. */
static uint32_t s_cfg_w = 0, s_cfg_h = 0;

/* Per-frame objects, valid only between start_frame and end_frame. */
static WGPUTexture           s_frame_tex  = NULL;
static WGPUTextureView       s_frame_view = NULL;
static WGPUCommandEncoder    s_encoder    = NULL;
static WGPURenderPassEncoder s_pass       = NULL;
static bool                  s_frame_open = false;

/* Clear color, pushed by gfx_pc.c before start_frame (see gfx_webgpu_set_clear_color). */
static double s_clear_r = 0.0, s_clear_g = 0.0, s_clear_b = 0.0;

/* ------------------------------------------------------------------------
 * Async request helpers (wgpu-native fires these during processEvents), mirroring
 * the validated spike (tests/test_webgpu_spike.c).
 * ---------------------------------------------------------------------- */
typedef struct { WGPUAdapter adapter; int done; WGPURequestAdapterStatus status; } AdapterReq;
static void on_adapter(WGPURequestAdapterStatus s, WGPUAdapter a, WGPUStringView m, void *u1, void *u2) {
    (void)m; (void)u2; AdapterReq *r = (AdapterReq *)u1; r->status = s; r->adapter = a; r->done = 1;
}
typedef struct { WGPUDevice device; int done; WGPURequestDeviceStatus status; } DeviceReq;
static void on_device(WGPURequestDeviceStatus s, WGPUDevice d, WGPUStringView m, void *u1, void *u2) {
    (void)m; (void)u2; DeviceReq *r = (DeviceReq *)u1; r->status = s; r->device = d; r->done = 1;
}

static WGPUStringView wgpu_sv(const char *s) {
    WGPUStringView v; v.data = s; v.length = s ? strlen(s) : 0; return v;
}

/* ------------------------------------------------------------------------
 * Surface creation (platform-specific window -> WGPUSurface)
 * ---------------------------------------------------------------------- */
static WGPUSurface wgpu_create_surface(void) {
#ifdef __APPLE__
    void *layer = platformGetMetalLayer();
    if (layer == NULL) {
        fprintf(stderr, "[webgpu] no CAMetalLayer from platform — cannot create surface\n");
        return NULL;
    }
    WGPUSurfaceSourceMetalLayer ml = {0};
    ml.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
    ml.layer = layer;
    WGPUSurfaceDescriptor sd = {0};
    sd.nextInChain = (WGPUChainedStruct *)&ml;
    sd.label = wgpu_sv("mgb64-surface");
    return wgpuInstanceCreateSurface(s_instance, &sd);
#else
    /* HWND (WGPUSurfaceSourceWindowsHWND) / X11 / Wayland land in Task 7. */
    fprintf(stderr, "[webgpu] surface creation on this platform is Task 7 (not yet wired)\n");
    return NULL;
#endif
}

/* Pick the swapchain format: prefer BGRA8Unorm (the near-universal surface
 * format and what CAMetalLayer natively presents), else the surface's first
 * advertised format. */
static WGPUTextureFormat wgpu_choose_format(void) {
    WGPUSurfaceCapabilities caps = {0};
    if (wgpuSurfaceGetCapabilities(s_surface, s_adapter, &caps) != WGPUStatus_Success ||
        caps.formatCount == 0) {
        wgpuSurfaceCapabilitiesFreeMembers(caps);
        return WGPUTextureFormat_BGRA8Unorm;   /* safe default */
    }
    WGPUTextureFormat chosen = caps.formats[0];
    for (size_t i = 0; i < caps.formatCount; ++i) {
        if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm) {
            chosen = WGPUTextureFormat_BGRA8Unorm;
            break;
        }
    }
    wgpuSurfaceCapabilitiesFreeMembers(caps);
    return chosen;
}

static void wgpu_configure_surface(uint32_t w, uint32_t h) {
    if (s_surface == NULL || s_device == NULL || w == 0 || h == 0) {
        return;
    }
    WGPUSurfaceConfiguration cfg = {0};
    cfg.device = s_device;
    cfg.format = s_surface_format;
    cfg.usage = WGPUTextureUsage_RenderAttachment;
    cfg.width = w;
    cfg.height = h;
    cfg.alphaMode = WGPUCompositeAlphaMode_Auto;
    cfg.presentMode = WGPUPresentMode_Fifo;   /* vsync; matches the default GL/Metal swap */
    wgpuSurfaceConfigure(s_surface, &cfg);
    s_cfg_w = w;
    s_cfg_h = h;
}

/* ------------------------------------------------------------------------
 * Vtable: init / resize / frame lifecycle
 * ---------------------------------------------------------------------- */
static void wgpu_init(void) {
    s_ready = false;

    s_instance = wgpuCreateInstance(NULL);
    if (s_instance == NULL) {
        fprintf(stderr, "[webgpu] wgpuCreateInstance failed\n");
        return;
    }

    /* Surface first, so it can be passed as the adapter's compatibleSurface. */
    s_surface = wgpu_create_surface();
    if (s_surface == NULL) {
        fprintf(stderr, "[webgpu] surface creation failed — backend inert\n");
        return;
    }

    AdapterReq areq = {0};
    WGPURequestAdapterCallbackInfo acb = {0};
    acb.mode = WGPUCallbackMode_AllowProcessEvents;
    acb.callback = on_adapter;
    acb.userdata1 = &areq;
    WGPURequestAdapterOptions aopts = {0};
    aopts.compatibleSurface = s_surface;
    aopts.powerPreference = WGPUPowerPreference_HighPerformance;
    wgpuInstanceRequestAdapter(s_instance, &aopts, acb);
    for (int i = 0; !areq.done && i < 1000; ++i) wgpuInstanceProcessEvents(s_instance);
    if (!areq.done || areq.status != WGPURequestAdapterStatus_Success || areq.adapter == NULL) {
        fprintf(stderr, "[webgpu] adapter request failed (status=%d)\n", (int)areq.status);
        return;
    }
    s_adapter = areq.adapter;

    WGPUAdapterInfo info = {0};
    wgpuAdapterGetInfo(s_adapter, &info);
    fprintf(stderr, "[webgpu] adapter backend=%d device=%.*s\n",
            (int)info.backendType, (int)info.device.length,
            info.device.data ? info.device.data : "");
    wgpuAdapterInfoFreeMembers(info);

    DeviceReq dreq = {0};
    WGPURequestDeviceCallbackInfo dcb = {0};
    dcb.mode = WGPUCallbackMode_AllowProcessEvents;
    dcb.callback = on_device;
    dcb.userdata1 = &dreq;
    WGPUDeviceDescriptor ddesc = {0};
    ddesc.label = wgpu_sv("mgb64-device");
    wgpuAdapterRequestDevice(s_adapter, &ddesc, dcb);
    for (int i = 0; !dreq.done && i < 1000; ++i) wgpuInstanceProcessEvents(s_instance);
    if (!dreq.done || dreq.status != WGPURequestDeviceStatus_Success || dreq.device == NULL) {
        fprintf(stderr, "[webgpu] device request failed (status=%d)\n", (int)dreq.status);
        return;
    }
    s_device = dreq.device;
    s_queue = wgpuDeviceGetQueue(s_device);

    s_surface_format = wgpu_choose_format();
    s_ready = true;
    fprintf(stderr, "[webgpu] backend initialized (surface format=%d)\n", (int)s_surface_format);
}

static void wgpu_on_resize(void) {
    /* Force a reconfigure on the next start_frame; the actual size is read from
     * gfx_current_dimensions there (mirrors the Metal backend's lazy target
     * re-creation). */
    s_cfg_w = 0;
    s_cfg_h = 0;
}

static void wgpu_start_frame(void) {
    s_frame_open = false;
    if (!s_ready) {
        return;
    }

    /* Render at the frontend's resolution (gfx_current_dimensions) — the same
     * resolution the viewports and T&L are computed against, exactly like the
     * Metal backend. */
    uint32_t rw = gfx_current_dimensions.width;
    uint32_t rh = gfx_current_dimensions.height;
    if (rw == 0 || rh == 0) {
        return;   /* dimensions not established yet — skip this frame cleanly */
    }
    if (rw != s_cfg_w || rh != s_cfg_h) {
        wgpu_configure_surface(rw, rh);
    }

    WGPUSurfaceTexture st = {0};
    wgpuSurfaceGetCurrentTexture(s_surface, &st);
    if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        /* Outdated/Lost/Timeout: drop the texture, reconfigure, skip the frame. */
        if (st.texture != NULL) wgpuTextureRelease(st.texture);
        wgpu_configure_surface(rw, rh);
        return;
    }
    s_frame_tex = st.texture;
    s_frame_view = wgpuTextureCreateView(s_frame_tex, NULL);
    if (s_frame_view == NULL) {
        wgpuTextureRelease(s_frame_tex);
        s_frame_tex = NULL;
        return;
    }

    s_encoder = wgpuDeviceCreateCommandEncoder(s_device, NULL);

    WGPURenderPassColorAttachment att = {0};
    att.view = s_frame_view;
    att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;   /* required for a 2D color target */
    att.loadOp = WGPULoadOp_Clear;
    att.storeOp = WGPUStoreOp_Store;
    att.clearValue.r = s_clear_r;
    att.clearValue.g = s_clear_g;
    att.clearValue.b = s_clear_b;
    att.clearValue.a = 1.0;
    WGPURenderPassDescriptor rp = {0};
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &att;
    s_pass = wgpuCommandEncoderBeginRenderPass(s_encoder, &rp);
    s_frame_open = true;   /* Task 1: the pass only clears; draws arrive in Task 3 */
}

static void wgpu_end_frame(void) {
    if (!s_frame_open) {
        /* start_frame bailed (not ready / no texture) — nothing to submit. */
        return;
    }
    wgpuRenderPassEncoderEnd(s_pass);
    wgpuRenderPassEncoderRelease(s_pass);
    s_pass = NULL;

    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(s_encoder, NULL);
    wgpuQueueSubmit(s_queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(s_encoder);
    s_encoder = NULL;

    wgpuSurfacePresent(s_surface);

    wgpuTextureViewRelease(s_frame_view);
    s_frame_view = NULL;
    wgpuTextureRelease(s_frame_tex);
    s_frame_tex = NULL;
    s_frame_open = false;
}

static void wgpu_finish_render(void) {
    /* Readback/GPU-drain sync lands with read_framebuffer_rgb in Task 5. */
}

/* ------------------------------------------------------------------------
 * Vtable: shaders (safe stubs — real WGSL emitter is Task 3)
 *
 * gfx_pc.c owns the shader-pointer lifecycle: it calls lookup_shader, and on a
 * miss create_and_load_new_shader, then load_shader + shader_get_info. The
 * pointers must be stable and non-NULL. A tiny by-value cache keyed on the
 * combiner id satisfies that without emitting any real pipeline yet; draws are
 * no-ops in Task 1, so num_inputs/used_textures may report empty.
 * ---------------------------------------------------------------------- */
struct ShaderProgram {
    uint64_t shader_id0;
    uint32_t shader_id1;
    uint8_t  num_inputs;
    bool     used_textures[2];
};

#define WGPU_SHADER_CACHE_MAX 512
static struct ShaderProgram s_shader_cache[WGPU_SHADER_CACHE_MAX];
static int s_shader_cache_count = 0;

static struct ShaderProgram *wgpu_lookup_shader(uint64_t shader_id0, uint32_t shader_id1) {
    for (int i = 0; i < s_shader_cache_count; ++i) {
        if (s_shader_cache[i].shader_id0 == shader_id0 &&
            s_shader_cache[i].shader_id1 == shader_id1) {
            return &s_shader_cache[i];
        }
    }
    return NULL;
}

static struct ShaderProgram *wgpu_create_and_load_new_shader(uint64_t shader_id0, uint32_t shader_id1) {
    struct ShaderProgram *prg = wgpu_lookup_shader(shader_id0, shader_id1);
    if (prg != NULL) {
        return prg;
    }
    int idx = s_shader_cache_count;
    if (idx >= WGPU_SHADER_CACHE_MAX) {
        static bool warned = false;
        if (!warned) {
            fprintf(stderr, "[webgpu] shader cache full (%d) — reusing slot 0\n", WGPU_SHADER_CACHE_MAX);
            warned = true;
        }
        idx = 0;   /* never return NULL; gfx_pc.c dereferences the result */
    } else {
        s_shader_cache_count++;
    }
    prg = &s_shader_cache[idx];
    prg->shader_id0 = shader_id0;
    prg->shader_id1 = shader_id1;
    prg->num_inputs = 0;
    prg->used_textures[0] = false;
    prg->used_textures[1] = false;
    return prg;
}

static void wgpu_load_shader(struct ShaderProgram *new_prg) { (void)new_prg; }
static void wgpu_unload_shader(struct ShaderProgram *old_prg) { (void)old_prg; }

static void wgpu_shader_get_info(struct ShaderProgram *prg, uint8_t *num_inputs, bool used_textures[2]) {
    if (prg != NULL) {
        if (num_inputs != NULL) *num_inputs = prg->num_inputs;
        if (used_textures != NULL) {
            used_textures[0] = prg->used_textures[0];
            used_textures[1] = prg->used_textures[1];
        }
    } else {
        if (num_inputs != NULL) *num_inputs = 0;
        if (used_textures != NULL) { used_textures[0] = false; used_textures[1] = false; }
    }
}

/* ------------------------------------------------------------------------
 * Vtable: textures (stubs — real WGPUTexture/WGPUSampler path is Task 2)
 * ---------------------------------------------------------------------- */
static uint32_t s_next_texture_id = 1;   /* 0 reserved as "none" */
static uint32_t wgpu_new_texture(void) { return s_next_texture_id++; }
static void wgpu_delete_texture(uint32_t texture_id) { (void)texture_id; }
static void wgpu_select_texture(int tile, uint32_t texture_id) { (void)tile; (void)texture_id; }
static bool wgpu_upload_texture(const uint8_t *rgba32_buf, int width, int height) {
    (void)rgba32_buf; (void)width; (void)height; return true;
}
static void wgpu_set_sampler_parameters(int sampler, bool linear_filter, uint32_t cms, uint32_t cmt) {
    (void)sampler; (void)linear_filter; (void)cms; (void)cmt;
}

/* ------------------------------------------------------------------------
 * Vtable: pipeline state (stubs — baked into pipelines in Tasks 3/4)
 * ---------------------------------------------------------------------- */
static void wgpu_set_depth_mode(bool depth_test, bool depth_update, bool depth_compare,
                                bool depth_source_prim, uint16_t zmode) {
    (void)depth_test; (void)depth_update; (void)depth_compare; (void)depth_source_prim; (void)zmode;
}
static void wgpu_set_viewport(int x, int y, int width, int height) {
    (void)x; (void)y; (void)width; (void)height;
}
static void wgpu_set_scissor(int x, int y, int width, int height) {
    (void)x; (void)y; (void)width; (void)height;
}
static void wgpu_set_blend_mode(enum GfxBlendMode mode) { (void)mode; }

static void wgpu_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    (void)buf_vbo; (void)buf_vbo_len; (void)buf_vbo_num_tris;
    /* Geometry submission is Task 3 (WGSL combiner emitter + transient VB). */
}

static bool wgpu_read_framebuffer_rgb(int x, int y, int width, int height, uint8_t *rgb_out) {
    (void)x; (void)y; (void)width; (void)height; (void)rgb_out;
    return false;   /* texture->buffer readback is Task 5 */
}

/* WebGPU clip space is 0..1 (like Metal/D3D, unlike GL's -1..1). Reported to the
 * CPU clipper (gfx_pc.c GFX_CLIP_Z_SCALE); paired with g_depth_clamp_enabled set
 * in gfx_init() for the WebGPU path. */
static bool wgpu_z_is_from_0_to_1(void) { return true; }

/* ------------------------------------------------------------------------
 * Non-vtable couplings (called directly from gfx_pc.c, backend-aware)
 * ---------------------------------------------------------------------- */
void gfx_webgpu_set_clear_color(float r, float g, float b) {
    s_clear_r = (double)r;
    s_clear_g = (double)g;
    s_clear_b = (double)b;
}

int gfx_webgpu_max_offscreen_dim(void) {
    return 8192;   /* WebGPU guaranteed minimum maxTextureDimension2D; refined in Task 4 */
}

/* ------------------------------------------------------------------------
 * The vtable — same field order as gfx_opengl_api / gfx_metal_api.
 * ---------------------------------------------------------------------- */
struct GfxRenderingAPI gfx_webgpu_api = {
    wgpu_z_is_from_0_to_1,
    wgpu_unload_shader,
    wgpu_load_shader,
    wgpu_create_and_load_new_shader,
    wgpu_lookup_shader,
    wgpu_shader_get_info,
    wgpu_new_texture,
    wgpu_delete_texture,
    wgpu_select_texture,
    wgpu_upload_texture,
    wgpu_set_sampler_parameters,
    wgpu_set_depth_mode,
    wgpu_set_viewport,
    wgpu_set_scissor,
    wgpu_set_blend_mode,
    wgpu_draw_triangles,
    wgpu_read_framebuffer_rgb,
    wgpu_init,
    wgpu_on_resize,
    wgpu_start_frame,
    wgpu_end_frame,
    wgpu_finish_render,
    NULL,   /* draw_modern_mesh — optional, implemented in Task 6 */
};
