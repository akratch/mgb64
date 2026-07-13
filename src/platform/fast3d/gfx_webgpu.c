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
#include "gfx_webgpu_shader.h"   /* WGSL combiner emitter (Task 3) */

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

/* Offscreen scene target: the game renders here (not straight to the surface),
 * so rendering is independent of window visibility (a hidden/occluded window has
 * no drawable) and the frame can be read back (screenshots, Task 5). BGRA8 to
 * match the surface so end_frame presents with a plain texture-to-texture copy.
 * Re-created when the render resolution changes. */
static WGPUTexture           s_scene_tex  = NULL;
static WGPUTextureView       s_scene_view = NULL;
static uint32_t              s_scene_w = 0, s_scene_h = 0;

/* Per-frame objects, valid only between start_frame and end_frame. */
static WGPUCommandEncoder    s_encoder    = NULL;
static WGPURenderPassEncoder s_pass       = NULL;
static bool                  s_frame_open = false;

/* Clear color, pushed by gfx_pc.c before start_frame (see gfx_webgpu_set_clear_color). */
static double s_clear_r = 0.0, s_clear_g = 0.0, s_clear_b = 0.0;

/* Draw resources (Task 3): a 1x1 white fallback texture + a nearest sampler for
 * used-but-unuploaded texture slots, and one large vertex buffer bump-allocated
 * per frame (reset in start_frame; consumed by draw_triangles). */
static WGPUTexture     s_white_tex = NULL;
static WGPUTextureView s_white_view = NULL;
static WGPUSampler     s_default_sampler = NULL;
#define WGPU_VBUF_CAP (16u * 1024u * 1024u)
static WGPUBuffer s_vbuf = NULL;
static uint32_t   s_vbuf_off = 0;

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

/* Log device errors instead of letting wgpu-native's default uncaptured-error
 * handler panic and abort the process — a malformed shader or a bad draw must
 * degrade to a logged, skipped frame, never crash the game. */
static void on_device_error(WGPUDevice const *device, WGPUErrorType type,
                            WGPUStringView msg, void *u1, void *u2) {
    (void)device; (void)u1; (void)u2;
    fprintf(stderr, "[webgpu] device error (type=%d): %.*s\n",
            (int)type, (int)msg.length, msg.data ? msg.data : "");
    fflush(stderr);
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
    /* The scene is rendered offscreen and copied here at present, so the surface
     * only needs to be a copy destination (plus RenderAttachment, which surfaces
     * require). If a platform disallows CopyDst, wgpuSurfaceConfigure raises a
     * device error that on_device_error logs (never aborts) and present is
     * skipped — offscreen rendering + readback still work. */
    cfg.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopyDst;
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
    ddesc.uncapturedErrorCallbackInfo.callback = on_device_error;
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
    s_vbuf_off = 0;   /* reset the per-frame vertex bump allocator */
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
    /* (Re)create the offscreen scene target at the render resolution. */
    if (s_scene_view == NULL || s_scene_w != rw || s_scene_h != rh) {
        if (s_scene_view != NULL) { wgpuTextureViewRelease(s_scene_view); s_scene_view = NULL; }
        if (s_scene_tex != NULL)  { wgpuTextureRelease(s_scene_tex);      s_scene_tex = NULL; }
        WGPUTextureDescriptor td = {0};
        td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc |
                   WGPUTextureUsage_TextureBinding;
        td.dimension = WGPUTextureDimension_2D;
        td.size.width = rw; td.size.height = rh; td.size.depthOrArrayLayers = 1;
        td.format = s_surface_format;   /* BGRA8 — matches the surface for the present copy */
        td.mipLevelCount = 1; td.sampleCount = 1;
        s_scene_tex = wgpuDeviceCreateTexture(s_device, &td);
        s_scene_view = s_scene_tex ? wgpuTextureCreateView(s_scene_tex, NULL) : NULL;
        s_scene_w = rw; s_scene_h = rh;
    }
    if (s_scene_view == NULL) {
        return;
    }

    s_encoder = wgpuDeviceCreateCommandEncoder(s_device, NULL);

    WGPURenderPassColorAttachment att = {0};
    att.view = s_scene_view;
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
    s_frame_open = true;
}

typedef struct { int done; WGPUMapAsyncStatus status; } WgpuMapReq;
static void on_map(WGPUMapAsyncStatus s, WGPUStringView m, void *u1, void *u2) {
    (void)m; (void)u2; WgpuMapReq *r = (WgpuMapReq *)u1; r->status = s; r->done = 1;
}

/* GE007_WEBGPU_DUMP_FRAME=<n> writes presented frame n to a PPM (debug/validation
 * seed for the Task 5 readback). Returns the target frame, or -1 if unset. */
static int wgpu_dump_target_frame(void) {
    static int cached = -2;
    if (cached == -2) {
        const char *e = getenv("GE007_WEBGPU_DUMP_FRAME");
        cached = e ? atoi(e) : -1;
    }
    return cached;
}

/* Map `buf` (bytesPerRow=bpr, BGRA8) and write a w*h RGB PPM to `path`. */
static void wgpu_write_ppm(WGPUBuffer buf, uint32_t bpr, uint32_t w, uint32_t h, const char *path) {
    size_t size = (size_t)bpr * h;
    WgpuMapReq mr = {0};
    WGPUBufferMapCallbackInfo ci = {0};
    ci.mode = WGPUCallbackMode_AllowProcessEvents;
    ci.callback = on_map;
    ci.userdata1 = &mr;
    wgpuBufferMapAsync(buf, WGPUMapMode_Read, 0, size, ci);
    for (int i = 0; !mr.done && i < 100000; ++i) wgpuDevicePoll(s_device, true, NULL);
    if (!mr.done || mr.status != WGPUMapAsyncStatus_Success) {
        fprintf(stderr, "[webgpu] frame dump map failed (status=%d)\n", (int)mr.status);
        return;
    }
    const uint8_t *px = (const uint8_t *)wgpuBufferGetConstMappedRange(buf, 0, size);
    FILE *f = px ? fopen(path, "wb") : NULL;
    if (f != NULL) {
        fprintf(f, "P6\n%u %u\n255\n", w, h);
        for (uint32_t y = 0; y < h; y++) {
            const uint8_t *row = px + (size_t)y * bpr;
            for (uint32_t x = 0; x < w; x++) {
                const uint8_t *p = row + (size_t)x * 4;   /* BGRA8 */
                uint8_t rgb[3] = { p[2], p[1], p[0] };
                fwrite(rgb, 1, 3, f);
            }
        }
        fclose(f);
        fprintf(stderr, "[webgpu] wrote frame dump %s (%ux%u)\n", path, w, h);
    }
    wgpuBufferUnmap(buf);
}

static void wgpu_end_frame(void) {
    if (!s_frame_open) {
        /* start_frame bailed (not ready / no texture) — nothing to submit. */
        return;
    }
    wgpuRenderPassEncoderEnd(s_pass);
    wgpuRenderPassEncoderRelease(s_pass);
    s_pass = NULL;

    /* Optional debug frame dump: copy the offscreen scene into a mappable buffer
     * (works even when the window is hidden, unlike a surface readback). */
    static int frame_no = -1;
    frame_no++;
    WGPUBuffer dump_buf = NULL;
    uint32_t dump_bpr = 0;
    if (frame_no == wgpu_dump_target_frame() && s_scene_w > 0 && s_scene_h > 0) {
        dump_bpr = ((s_scene_w * 4u + 255u) / 256u) * 256u;   /* 256-align for CopyTextureToBuffer */
        WGPUBufferDescriptor bd = {0};
        bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        bd.size = (uint64_t)dump_bpr * s_scene_h;
        dump_buf = wgpuDeviceCreateBuffer(s_device, &bd);
        if (dump_buf != NULL) {
            WGPUTexelCopyTextureInfo src = {0};
            src.texture = s_scene_tex; src.aspect = WGPUTextureAspect_All;
            WGPUTexelCopyBufferInfo dst = {0};
            dst.buffer = dump_buf;
            dst.layout.bytesPerRow = dump_bpr;
            dst.layout.rowsPerImage = s_scene_h;
            WGPUExtent3D ext = { s_scene_w, s_scene_h, 1 };
            wgpuCommandEncoderCopyTextureToBuffer(s_encoder, &src, &dst, &ext);
        }
    }

    /* Present: copy the scene into the window's surface texture (same format +
     * size) and present it. A hidden/occluded window has no drawable — that's
     * fine, the offscreen frame still rendered (and dumped/read back). */
    WGPUSurfaceTexture st = {0};
    wgpuSurfaceGetCurrentTexture(s_surface, &st);
    bool present_ok = st.texture != NULL &&
        (st.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
         st.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal);
    if (present_ok) {
        WGPUTexelCopyTextureInfo cs = {0};
        cs.texture = s_scene_tex; cs.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyTextureInfo cd = {0};
        cd.texture = st.texture; cd.aspect = WGPUTextureAspect_All;
        WGPUExtent3D ext = { s_scene_w, s_scene_h, 1 };
        wgpuCommandEncoderCopyTextureToTexture(s_encoder, &cs, &cd, &ext);
    }

    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(s_encoder, NULL);
    wgpuQueueSubmit(s_queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(s_encoder);
    s_encoder = NULL;

    if (dump_buf != NULL) {
        char path[128];
        snprintf(path, sizeof(path), "/tmp/webgpu_frame_%d.ppm", frame_no);
        wgpu_write_ppm(dump_buf, dump_bpr, s_scene_w, s_scene_h, path);
        wgpuBufferRelease(dump_buf);
    }

    if (present_ok) {
        wgpuSurfacePresent(s_surface);
    }
    if (st.texture != NULL) {
        wgpuTextureRelease(st.texture);
    }
    s_frame_open = false;
}

static void wgpu_finish_render(void) {
    /* Readback/GPU-drain sync lands with read_framebuffer_rgb in Task 5. */
}

/* ------------------------------------------------------------------------
 * Vtable: shaders + render pipelines (Task 3)
 *
 * gfx_pc.c owns the shader-pointer lifecycle: it calls lookup_shader, and on a
 * miss create_and_load_new_shader, then load_shader + shader_get_info + draw.
 * Each ShaderProgram holds the compiled WGSL module, the derived vertex layout,
 * the bind-group + pipeline layouts, and a small cache of WGPURenderPipelines
 * keyed by dynamic state (WebGPU bakes blend/depth/format into the pipeline, so
 * the GL immediate setters collapse into this lazy lookup — same shape as the
 * Metal backend's mtl_pso_for).
 * ---------------------------------------------------------------------- */
struct WgpuPipeEntry { uint32_t key; WGPURenderPipeline pipe; };

struct ShaderProgram {
    uint64_t shader_id0;
    uint32_t shader_id1;
    struct WgpuShaderInfo info;
    WGPUShaderModule     module;
    WGPUVertexAttribute  vattrs[24];
    WGPUBindGroupLayout  bgl;       /* NULL when the combiner samples no textures */
    WGPUPipelineLayout   playout;
    struct WgpuPipeEntry pipes[16];
    int npipes;
};

#define WGPU_SHADER_MAX 1024
static struct ShaderProgram s_shaders[WGPU_SHADER_MAX];
static int s_shader_count = 0;

/* Currently loaded shader + dynamic blend state (set by load_shader /
 * set_blend_mode, read by draw_triangles). */
static struct ShaderProgram *s_cur_shader = NULL;
static enum GfxBlendMode      s_cur_blend = GFX_BLEND_DISABLED;

static struct ShaderProgram *wgpu_lookup_shader(uint64_t shader_id0, uint32_t shader_id1) {
    for (int i = 0; i < s_shader_count; ++i) {
        if (s_shaders[i].shader_id0 == shader_id0 && s_shaders[i].shader_id1 == shader_id1) {
            return &s_shaders[i];
        }
    }
    return NULL;
}

/* Build the bind-group layout for a combiner: (tex,sampler) pairs for each used
 * texture at bindings (0,1) and (2,3). NULL when no textures are sampled. */
static WGPUBindGroupLayout wgpu_make_bgl(const struct WgpuShaderInfo *info) {
    WGPUBindGroupLayoutEntry ents[4];
    int ne = 0;
    for (int t = 0; t < 2; t++) {
        if (!info->used_textures[t]) continue;
        WGPUBindGroupLayoutEntry te = {0};
        te.binding = (uint32_t)(t * 2);
        te.visibility = WGPUShaderStage_Fragment;
        te.texture.sampleType = WGPUTextureSampleType_Float;
        te.texture.viewDimension = WGPUTextureViewDimension_2D;
        ents[ne++] = te;
        WGPUBindGroupLayoutEntry se = {0};
        se.binding = (uint32_t)(t * 2 + 1);
        se.visibility = WGPUShaderStage_Fragment;
        se.sampler.type = WGPUSamplerBindingType_Filtering;
        ents[ne++] = se;
    }
    if (ne == 0) {
        return NULL;
    }
    WGPUBindGroupLayoutDescriptor d = {0};
    d.entryCount = (size_t)ne;
    d.entries = ents;
    return wgpuDeviceCreateBindGroupLayout(s_device, &d);
}

static struct ShaderProgram *wgpu_create_and_load_new_shader(uint64_t shader_id0, uint32_t shader_id1) {
    struct ShaderProgram *prg = wgpu_lookup_shader(shader_id0, shader_id1);
    if (prg != NULL) {
        s_cur_shader = prg;
        return prg;
    }
    if (s_shader_count >= WGPU_SHADER_MAX) {
        static bool warned = false;
        if (!warned) { fprintf(stderr, "[webgpu] shader table full (%d)\n", WGPU_SHADER_MAX); warned = true; }
        return &s_shaders[0];   /* never NULL; gfx_pc.c dereferences the result */
    }
    prg = &s_shaders[s_shader_count];
    memset(prg, 0, sizeof(*prg));
    prg->shader_id0 = shader_id0;
    prg->shader_id1 = shader_id1;

    char *wgsl = gfx_webgpu_build_wgsl(shader_id0, shader_id1, &prg->info);
    if (wgsl == NULL || !s_ready) {
        free(wgsl);
        s_shader_count++;
        s_cur_shader = prg;
        return prg;   /* inert program; draw guards on prg->module */
    }

    WGPUShaderSourceWGSL src = {0};
    src.chain.sType = WGPUSType_ShaderSourceWGSL;
    src.code = wgpu_sv(wgsl);
    WGPUShaderModuleDescriptor smd = {0};
    smd.nextInChain = (WGPUChainedStruct *)&src;
    prg->module = wgpuDeviceCreateShaderModule(s_device, &smd);
    free(wgsl);

    /* Vertex attributes for the pipeline's WGPUVertexBufferLayout. */
    for (int i = 0; i < prg->info.num_attrs; i++) {
        int sz = prg->info.attrs[i].size;
        prg->vattrs[i].format = sz == 1 ? WGPUVertexFormat_Float32
                              : sz == 2 ? WGPUVertexFormat_Float32x2
                              : sz == 3 ? WGPUVertexFormat_Float32x3
                                        : WGPUVertexFormat_Float32x4;
        prg->vattrs[i].offset = (uint64_t)prg->info.attrs[i].offset * sizeof(float);
        prg->vattrs[i].shaderLocation = (uint32_t)prg->info.attrs[i].location;
    }

    prg->bgl = wgpu_make_bgl(&prg->info);
    WGPUPipelineLayoutDescriptor pld = {0};
    if (prg->bgl != NULL) {
        pld.bindGroupLayoutCount = 1;
        pld.bindGroupLayouts = &prg->bgl;
    }
    prg->playout = wgpuDeviceCreatePipelineLayout(s_device, &pld);

    s_shader_count++;
    s_cur_shader = prg;
    return prg;
}

static void wgpu_load_shader(struct ShaderProgram *new_prg) { s_cur_shader = new_prg; }
static void wgpu_unload_shader(struct ShaderProgram *old_prg) {
    (void)old_prg;
    s_cur_shader = NULL;
}

static void wgpu_shader_get_info(struct ShaderProgram *prg, uint8_t *num_inputs, bool used_textures[2]) {
    if (prg != NULL) {
        if (num_inputs != NULL) *num_inputs = (uint8_t)prg->info.num_inputs;
        if (used_textures != NULL) {
            used_textures[0] = prg->info.used_textures[0];
            used_textures[1] = prg->info.used_textures[1];
        }
    } else {
        if (num_inputs != NULL) *num_inputs = 0;
        if (used_textures != NULL) { used_textures[0] = false; used_textures[1] = false; }
    }
}

/* Map a GfxBlendMode to a WGPUBlendState. Task 3 covers opaque / standard alpha
 * / multiplicative; the coverage/stencil/RDP-memory variants approximate to
 * alpha blending here and get their exact treatment in Task 4. Returns false for
 * opaque (no blend state attached). */
static bool wgpu_blend_state(enum GfxBlendMode mode, WGPUBlendState *out) {
    memset(out, 0, sizeof(*out));
    switch (mode) {
        case GFX_BLEND_DISABLED:
            return false;
        case GFX_BLEND_MODULATE:
            out->color.operation = WGPUBlendOperation_Add;
            out->color.srcFactor = WGPUBlendFactor_Dst;
            out->color.dstFactor = WGPUBlendFactor_Zero;
            out->alpha.operation = WGPUBlendOperation_Add;
            out->alpha.srcFactor = WGPUBlendFactor_Dst;
            out->alpha.dstFactor = WGPUBlendFactor_Zero;
            return true;
        default: /* ALPHA + coverage/stencil/memory variants -> standard alpha */
            out->color.operation = WGPUBlendOperation_Add;
            out->color.srcFactor = WGPUBlendFactor_SrcAlpha;
            out->color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
            out->alpha.operation = WGPUBlendOperation_Add;
            out->alpha.srcFactor = WGPUBlendFactor_One;
            out->alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
            return true;
    }
}

/* Lazily build + cache the render pipeline for (shader, blend). */
static WGPURenderPipeline wgpu_pipeline_for(struct ShaderProgram *prg, enum GfxBlendMode blend) {
    if (prg->module == NULL) {
        return NULL;
    }
    uint32_t key = (uint32_t)blend;
    for (int i = 0; i < prg->npipes; i++) {
        if (prg->pipes[i].key == key) return prg->pipes[i].pipe;
    }

    WGPUVertexBufferLayout vbl = {0};
    vbl.stepMode = WGPUVertexStepMode_Vertex;
    vbl.arrayStride = (uint64_t)prg->info.num_floats * sizeof(float);
    vbl.attributeCount = (size_t)prg->info.num_attrs;
    vbl.attributes = prg->vattrs;

    WGPUBlendState blendState;
    bool has_blend = wgpu_blend_state(blend, &blendState);
    WGPUColorTargetState color = {0};
    color.format = s_surface_format;
    color.writeMask = WGPUColorWriteMask_All;
    color.blend = has_blend ? &blendState : NULL;

    WGPUFragmentState fs = {0};
    fs.module = prg->module;
    fs.entryPoint = wgpu_sv("fs_main");
    fs.targetCount = 1;
    fs.targets = &color;

    WGPURenderPipelineDescriptor pd = {0};
    pd.layout = prg->playout;
    pd.vertex.module = prg->module;
    pd.vertex.entryPoint = wgpu_sv("vs_main");
    pd.vertex.bufferCount = 1;
    pd.vertex.buffers = &vbl;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.primitive.cullMode = WGPUCullMode_None;   /* N64 backface handled on CPU */
    pd.multisample.count = 1;
    pd.multisample.mask = 0xFFFFFFFFu;
    pd.fragment = &fs;
    /* No depth-stencil in Task 3 (submission-order draw); Task 4 adds it. */

    WGPURenderPipeline pipe = wgpuDeviceCreateRenderPipeline(s_device, &pd);
    if (pipe != NULL && prg->npipes < (int)(sizeof(prg->pipes) / sizeof(prg->pipes[0]))) {
        prg->pipes[prg->npipes].key = key;
        prg->pipes[prg->npipes].pipe = pipe;
        prg->npipes++;
    }
    return pipe;
}

/* ------------------------------------------------------------------------
 * Vtable: textures + samplers (Task 2)
 *
 * The N64 interpreter's model (mirroring gfx_opengl.c): new_texture() hands out
 * an opaque id; select_texture(tile, id) makes that id current on a tile;
 * upload_texture(rgba,w,h) uploads into the id current on the most-recently
 * selected tile; set_sampler_parameters(tile,...) sets the filter/wrap for that
 * tile. WebGPU has no global bind state, so textures + samplers are looked up
 * by id and staged per-tile here; the draw path (Task 3) binds tile 0/1's view
 * + sampler into a bind group at submit time.
 *
 * Ids are recycled through a free-list over a growable array (like glGenTextures
 * reuse), so live GPU resources stay bounded by the interpreter's texture cache
 * (~1024) rather than growing with monotonic ids over a long session.
 * ---------------------------------------------------------------------- */
#define WGPU_TX_MIRROR 0x1u   /* G_TX_MIRROR (PR/gbi.h) — mirrored here to avoid
                                 pulling the N64 GBI headers into this TU */
#define WGPU_TX_CLAMP  0x2u   /* G_TX_CLAMP */

struct WgpuTexEntry {
    WGPUTexture     tex;
    WGPUTextureView view;
    int             w, h;
    bool            used;
};
static struct WgpuTexEntry *s_tex = NULL;   /* indexed by (id - 1) */
static uint32_t s_tex_cap = 0;
static uint32_t s_tex_hi = 0;               /* highest id ever allocated */
static uint32_t *s_tex_free = NULL;         /* stack of freed ids for reuse */
static uint32_t s_tex_free_n = 0, s_tex_free_cap = 0;

/* Per-tile binding staged by select_texture / set_sampler_parameters and read by
 * the Task 3 draw path. */
static uint32_t    s_bound_tex[2] = {0, 0};
static WGPUSampler s_bound_sampler[2] = {NULL, NULL};
static int         s_active_tile = 0;       /* tile of the last select_texture */

/* Sampler cache keyed by (linear, cms, cmt). Small: the N64 uses a handful of
 * distinct (filter, wrap) combinations. */
struct WgpuSamplerEntry { int linear; uint32_t cms, cmt; WGPUSampler sampler; };
static struct WgpuSamplerEntry s_samplers[64];
static int s_sampler_n = 0;

static WGPUAddressMode wgpu_cm_to_address(uint32_t cm) {
    if (cm & WGPU_TX_CLAMP) {
        return WGPUAddressMode_ClampToEdge;
    }
    return (cm & WGPU_TX_MIRROR) ? WGPUAddressMode_MirrorRepeat : WGPUAddressMode_Repeat;
}

static struct WgpuTexEntry *wgpu_tex_lookup(uint32_t id) {
    if (id == 0 || id > s_tex_hi) {
        return NULL;
    }
    struct WgpuTexEntry *e = &s_tex[id - 1];
    return e->used ? e : NULL;
}

static uint32_t wgpu_new_texture(void) {
    uint32_t id;
    if (s_tex_free_n > 0) {
        id = s_tex_free[--s_tex_free_n];
    } else {
        id = ++s_tex_hi;
        if (id > s_tex_cap) {
            uint32_t ncap = s_tex_cap ? s_tex_cap * 2 : 256;
            struct WgpuTexEntry *n = (struct WgpuTexEntry *)realloc(s_tex, ncap * sizeof(*s_tex));
            if (n == NULL) {
                --s_tex_hi;
                return 0;   /* allocation failure — interpreter treats 0 as none */
            }
            memset(n + s_tex_cap, 0, (ncap - s_tex_cap) * sizeof(*s_tex));
            s_tex = n;
            s_tex_cap = ncap;
        }
    }
    struct WgpuTexEntry *e = &s_tex[id - 1];
    e->tex = NULL;
    e->view = NULL;
    e->w = e->h = 0;
    e->used = true;
    return id;
}

static void wgpu_delete_texture(uint32_t texture_id) {
    struct WgpuTexEntry *e = wgpu_tex_lookup(texture_id);
    if (e == NULL) {
        return;
    }
    if (e->view != NULL) { wgpuTextureViewRelease(e->view); e->view = NULL; }
    if (e->tex != NULL)  { wgpuTextureRelease(e->tex);      e->tex = NULL; }
    e->used = false;
    if (s_tex_free_n >= s_tex_free_cap) {
        uint32_t ncap = s_tex_free_cap ? s_tex_free_cap * 2 : 256;
        uint32_t *n = (uint32_t *)realloc(s_tex_free, ncap * sizeof(*n));
        if (n == NULL) {
            return;   /* drop the id (never reused); GPU resource already freed */
        }
        s_tex_free = n;
        s_tex_free_cap = ncap;
    }
    s_tex_free[s_tex_free_n++] = texture_id;
}

static void wgpu_select_texture(int tile, uint32_t texture_id) {
    if (tile < 0 || tile > 1) {
        return;
    }
    s_active_tile = tile;
    s_bound_tex[tile] = texture_id;
}

static bool wgpu_upload_texture(const uint8_t *rgba32_buf, int width, int height) {
    if (!s_ready || rgba32_buf == NULL || width <= 0 || height <= 0 ||
        width > 8192 || height > 8192) {
        return false;
    }
    struct WgpuTexEntry *e = wgpu_tex_lookup(s_bound_tex[s_active_tile]);
    if (e == NULL) {
        return false;
    }
    /* Re-upload into an existing id: drop the old GPU resources first. */
    if (e->view != NULL) { wgpuTextureViewRelease(e->view); e->view = NULL; }
    if (e->tex != NULL)  { wgpuTextureRelease(e->tex);      e->tex = NULL; }

    WGPUTextureDescriptor td = {0};
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.dimension = WGPUTextureDimension_2D;
    td.size.width = (uint32_t)width;
    td.size.height = (uint32_t)height;
    td.size.depthOrArrayLayers = 1;
    td.format = WGPUTextureFormat_RGBA8Unorm;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    e->tex = wgpuDeviceCreateTexture(s_device, &td);
    if (e->tex == NULL) {
        return false;
    }
    WGPUTexelCopyTextureInfo dst = {0};
    dst.texture = e->tex;
    dst.mipLevel = 0;
    dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout layout = {0};
    layout.offset = 0;
    layout.bytesPerRow = (uint32_t)width * 4u;
    layout.rowsPerImage = (uint32_t)height;
    WGPUExtent3D ext = { (uint32_t)width, (uint32_t)height, 1 };
    wgpuQueueWriteTexture(s_queue, &dst, rgba32_buf, (size_t)width * (size_t)height * 4u, &layout, &ext);

    e->view = wgpuTextureCreateView(e->tex, NULL);
    e->w = width;
    e->h = height;
    return e->view != NULL;
}

static WGPUSampler wgpu_get_sampler(bool linear_filter, uint32_t cms, uint32_t cmt) {
    for (int i = 0; i < s_sampler_n; ++i) {
        if (s_samplers[i].linear == (int)linear_filter &&
            s_samplers[i].cms == cms && s_samplers[i].cmt == cmt) {
            return s_samplers[i].sampler;
        }
    }
    WGPUSamplerDescriptor sd = {0};
    sd.addressModeU = wgpu_cm_to_address(cms);
    sd.addressModeV = wgpu_cm_to_address(cmt);
    sd.addressModeW = WGPUAddressMode_ClampToEdge;
    sd.magFilter = linear_filter ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
    sd.minFilter = linear_filter ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
    sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;   /* single-level like the GL path */
    sd.lodMinClamp = 0.0f;
    sd.lodMaxClamp = 0.0f;
    sd.maxAnisotropy = 1;
    WGPUSampler s = wgpuDeviceCreateSampler(s_device, &sd);
    if (s != NULL && s_sampler_n < (int)(sizeof(s_samplers) / sizeof(s_samplers[0]))) {
        s_samplers[s_sampler_n].linear = (int)linear_filter;
        s_samplers[s_sampler_n].cms = cms;
        s_samplers[s_sampler_n].cmt = cmt;
        s_samplers[s_sampler_n].sampler = s;
        s_sampler_n++;
    }
    return s;
}

static void wgpu_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt) {
    if (tile < 0 || tile > 1 || !s_ready) {
        return;
    }
    s_bound_sampler[tile] = wgpu_get_sampler(linear_filter, cms, cmt);
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
static void wgpu_set_blend_mode(enum GfxBlendMode mode) { s_cur_blend = mode; }

/* Lazily create the draw fallbacks (1x1 white texture, nearest sampler) + the
 * per-frame bump vertex buffer (declared with the frame state above). Each draw
 * appends its buf_vbo at a fresh offset so all draws in the frame's single
 * render pass read distinct data (queue writes are ordered before submit). */
static void wgpu_ensure_draw_resources(void) {
    if (s_white_view == NULL) {
        WGPUTextureDescriptor td = {0};
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.dimension = WGPUTextureDimension_2D;
        td.size.width = 1; td.size.height = 1; td.size.depthOrArrayLayers = 1;
        td.format = WGPUTextureFormat_RGBA8Unorm;
        td.mipLevelCount = 1; td.sampleCount = 1;
        s_white_tex = wgpuDeviceCreateTexture(s_device, &td);
        if (s_white_tex != NULL) {
            uint8_t white[4] = {255, 255, 255, 255};
            WGPUTexelCopyTextureInfo dst = {0};
            dst.texture = s_white_tex; dst.aspect = WGPUTextureAspect_All;
            WGPUTexelCopyBufferLayout lay = {0};
            lay.bytesPerRow = 4; lay.rowsPerImage = 1;
            WGPUExtent3D ext = {1, 1, 1};
            wgpuQueueWriteTexture(s_queue, &dst, white, 4, &lay, &ext);
            s_white_view = wgpuTextureCreateView(s_white_tex, NULL);
        }
    }
    if (s_default_sampler == NULL) {
        WGPUSamplerDescriptor sd = {0};
        sd.addressModeU = sd.addressModeV = sd.addressModeW = WGPUAddressMode_Repeat;
        sd.magFilter = sd.minFilter = WGPUFilterMode_Nearest;
        sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        sd.maxAnisotropy = 1;
        s_default_sampler = wgpuDeviceCreateSampler(s_device, &sd);
    }
    if (s_vbuf == NULL) {
        WGPUBufferDescriptor bd = {0};
        bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        bd.size = WGPU_VBUF_CAP;
        s_vbuf = wgpuDeviceCreateBuffer(s_device, &bd);
    }
}

static void wgpu_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    if (!s_frame_open || s_cur_shader == NULL || s_cur_shader->module == NULL ||
        buf_vbo == NULL || buf_vbo_len == 0 || buf_vbo_num_tris == 0) {
        return;
    }
    wgpu_ensure_draw_resources();
    if (s_vbuf == NULL) {
        return;
    }
    WGPURenderPipeline pipe = wgpu_pipeline_for(s_cur_shader, s_cur_blend);
    if (pipe == NULL) {
        return;
    }

    /* Bump-allocate this batch's vertex data. */
    uint32_t bytes = (uint32_t)(buf_vbo_len * sizeof(float));
    if ((uint64_t)s_vbuf_off + bytes > WGPU_VBUF_CAP) {
        static bool warned = false;
        if (!warned) { fprintf(stderr, "[webgpu] per-frame vertex buffer full — dropping draws\n"); warned = true; }
        return;
    }
    uint32_t voff = s_vbuf_off;
    wgpuQueueWriteBuffer(s_queue, s_vbuf, voff, buf_vbo, bytes);
    s_vbuf_off += (bytes + 3u) & ~3u;   /* keep 4-byte alignment for the next offset */

    /* Per-draw bind group: each used texture's live view + sampler, falling back
     * to a 1x1 white texture / nearest sampler when unset. */
    WGPUBindGroup bg = NULL;
    if (s_cur_shader->bgl != NULL) {
        WGPUBindGroupEntry ents[4];
        int ne = 0;
        for (int t = 0; t < 2; t++) {
            if (!s_cur_shader->info.used_textures[t]) continue;
            struct WgpuTexEntry *e = wgpu_tex_lookup(s_bound_tex[t]);
            WGPUTextureView view = (e != NULL && e->view != NULL) ? e->view : s_white_view;
            WGPUSampler samp = s_bound_sampler[t] != NULL ? s_bound_sampler[t] : s_default_sampler;
            WGPUBindGroupEntry te = {0}; te.binding = (uint32_t)(t * 2);     te.textureView = view; ents[ne++] = te;
            WGPUBindGroupEntry se = {0}; se.binding = (uint32_t)(t * 2 + 1); se.sampler = samp;      ents[ne++] = se;
        }
        WGPUBindGroupDescriptor bd = {0};
        bd.layout = s_cur_shader->bgl;
        bd.entryCount = (size_t)ne;
        bd.entries = ents;
        bg = wgpuDeviceCreateBindGroup(s_device, &bd);
    }

    wgpuRenderPassEncoderSetPipeline(s_pass, pipe);
    if (bg != NULL) {
        wgpuRenderPassEncoderSetBindGroup(s_pass, 0, bg, 0, NULL);
    }
    wgpuRenderPassEncoderSetVertexBuffer(s_pass, 0, s_vbuf, voff, bytes);
    wgpuRenderPassEncoderDraw(s_pass, (uint32_t)(3 * buf_vbo_num_tris), 1, 0, 0);

    if (bg != NULL) {
        wgpuBindGroupRelease(bg);   /* the encoder retains it until submit */
    }
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
