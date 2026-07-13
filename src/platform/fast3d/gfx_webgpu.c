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
#include "gfx_webgpu.h"          /* public surface helper (shared with AppHost) */
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
#else
/* platform_sdl.c: native window handles for non-macOS surface creation.
 * Returns a windowing-system tag (2=Win32, 3=X11, 4=Wayland; 0=unknown). */
extern int platformWebGpuWindowInfo(void *sdl_window, void **out1, void **out2,
                                    unsigned long long *out_win);
#endif
/* The SDL_Window the engine renders into (platform_sdl.c); NULL before window
 * creation. The app shell passes its own window to the bring-up helper instead. */
extern void *platformGetSdlWindow(void);

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
/* When the app shell owns the device/surface (launcher → game handoff), the
 * engine adopts them and must NOT release them at teardown. False = we created
 * them ourselves (standalone --level boot) and own their lifetime. */
static bool              s_owns_device = false;

/* Host WebGPU handoff (src/platform/host_window.c). Present only when the app
 * shell created a device/queue/surface and registered them before boot. */
extern int   platformHasHostWebGpu(void);
extern void *platformHostWgpuInstance(void);
extern void *platformHostWgpuAdapter(void);
extern void *platformHostWgpuDevice(void);
extern void *platformHostWgpuQueue(void);
extern void *platformHostWgpuSurface(void);
extern int   platformHostWgpuSurfaceFormat(void);

/* Configured swapchain size; 0 forces a (re)configure on the next start_frame. */
static uint32_t s_cfg_w = 0, s_cfg_h = 0;

/* Offscreen scene target: the game renders here (not straight to the surface),
 * so rendering is independent of window visibility (a hidden/occluded window has
 * no drawable) and the frame can be read back (screenshots, Task 5). BGRA8 to
 * match the surface so end_frame presents with a plain texture-to-texture copy.
 * Re-created when the render resolution changes. */
static WGPUTexture           s_scene_tex  = NULL;
static WGPUTextureView       s_scene_view = NULL;
static WGPUTexture           s_depth_tex  = NULL;
static WGPUTextureView       s_depth_view = NULL;
static uint32_t              s_scene_w = 0, s_scene_h = 0;
#define WGPU_DEPTH_FORMAT WGPUTextureFormat_Depth24Plus

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

/* Dynamic depth / viewport / scissor state (Task 4). WebGPU bakes depth into the
 * pipeline, so the depth fields feed the pipeline cache key; viewport/scissor are
 * render-pass encoder state applied per draw. */
static bool     s_depth_test = false, s_depth_update = false, s_depth_compare = false;
static uint16_t s_zmode = 0;
static int s_vp_x = 0, s_vp_y = 0, s_vp_w = 0, s_vp_h = 0;
static int s_sc_x = 0, s_sc_y = 0, s_sc_w = 0, s_sc_h = 0;
static bool s_sc_set = false;

/* ZMODE_DEC decal (gfx_opengl.c / gfx_metal.mm): coplanar decals get a negative
 * polygon offset so they win the depth test against the surface they overlay. */
static bool wgpu_depth_is_decal(void) {
    return s_zmode == 0xc00 && s_depth_test && s_depth_compare;
}

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
 *
 * Parameterized by the native handle so BOTH the engine (standalone) and the
 * app shell (AppHost, which owns the window/layer before the game adopts it)
 * create surfaces the same way. macOS uses `metal_layer` (a CAMetalLayer);
 * every other platform uses `sdl_window` (resolved to HWND/X11/Wayland by
 * platformWebGpuWindowInfo). Declared in gfx_webgpu.h.
 * ---------------------------------------------------------------------- */
WGPUSurface gfx_webgpu_create_surface(WGPUInstance instance, void *metal_layer,
                                      void *sdl_window) {
    if (instance == NULL) {
        return NULL;
    }
    WGPUSurfaceDescriptor sd = {0};
    sd.label = wgpu_sv("mgb64-surface");
#ifdef __APPLE__
    (void)sdl_window;
    if (metal_layer == NULL) {
        fprintf(stderr, "[webgpu] no CAMetalLayer — cannot create surface\n");
        return NULL;
    }
    WGPUSurfaceSourceMetalLayer ml = {0};
    ml.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
    ml.layer = metal_layer;
    sd.nextInChain = (WGPUChainedStruct *)&ml;
    return wgpuInstanceCreateSurface(instance, &sd);
#else
    (void)metal_layer;
    void *h1 = NULL, *h2 = NULL;
    unsigned long long win = 0;
    int sys = platformWebGpuWindowInfo(sdl_window, &h1, &h2, &win);
    if (sys == 2) {   /* Win32 */
        WGPUSurfaceSourceWindowsHWND w = {0};
        w.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
        w.hinstance = h2;
        w.hwnd = h1;
        sd.nextInChain = (WGPUChainedStruct *)&w;
        return wgpuInstanceCreateSurface(instance, &sd);
    } else if (sys == 3) {   /* X11 */
        WGPUSurfaceSourceXlibWindow x = {0};
        x.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
        x.display = h1;
        x.window = (uint64_t)win;
        sd.nextInChain = (WGPUChainedStruct *)&x;
        return wgpuInstanceCreateSurface(instance, &sd);
    } else if (sys == 4) {   /* Wayland */
        WGPUSurfaceSourceWaylandSurface wl = {0};
        wl.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
        wl.display = h1;
        wl.surface = h2;
        sd.nextInChain = (WGPUChainedStruct *)&wl;
        return wgpuInstanceCreateSurface(instance, &sd);
    }
    fprintf(stderr, "[webgpu] unsupported windowing system (tag=%d) — no surface\n", sys);
    return NULL;
#endif
}

/* Pick the swapchain format: prefer BGRA8Unorm (the near-universal surface
 * format and what CAMetalLayer natively presents), else the surface's first
 * advertised format. Parameterized so the shared bring-up helper can use it
 * before the s_surface/s_adapter statics are assigned. */
static WGPUTextureFormat wgpu_choose_format(WGPUSurface surface, WGPUAdapter adapter) {
    WGPUSurfaceCapabilities caps = {0};
    if (wgpuSurfaceGetCapabilities(surface, adapter, &caps) != WGPUStatus_Success ||
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

/* Full WebGPU bring-up for a native window handle: instance -> surface ->
 * adapter -> device -> queue -> surface format, mirroring the validated spike
 * (tests/test_webgpu_spike.c). On success returns true with every out-param set
 * (the caller owns the returned objects); on any failure returns false, the
 * out-params untouched, and the caller treats the backend as inert. Shared by
 * the engine's own wgpu_init AND the app shell (AppHost, via gfx_webgpu.h) so
 * the request sequence + error callback live in exactly one place. */
bool gfx_webgpu_bringup(void *metal_layer, void *sdl_window,
                        WGPUInstance *out_instance, WGPUAdapter *out_adapter,
                        WGPUDevice *out_device, WGPUQueue *out_queue,
                        WGPUSurface *out_surface, int *out_format) {
    WGPUInstance instance = wgpuCreateInstance(NULL);
    if (instance == NULL) {
        fprintf(stderr, "[webgpu] wgpuCreateInstance failed\n");
        return false;
    }

    /* Surface first, so it can be passed as the adapter's compatibleSurface. */
    WGPUSurface surface = gfx_webgpu_create_surface(instance, metal_layer, sdl_window);
    if (surface == NULL) {
        fprintf(stderr, "[webgpu] surface creation failed — backend inert\n");
        return false;
    }

    AdapterReq areq = {0};
    WGPURequestAdapterCallbackInfo acb = {0};
    acb.mode = WGPUCallbackMode_AllowProcessEvents;
    acb.callback = on_adapter;
    acb.userdata1 = &areq;
    WGPURequestAdapterOptions aopts = {0};
    aopts.compatibleSurface = surface;
    aopts.powerPreference = WGPUPowerPreference_HighPerformance;
    wgpuInstanceRequestAdapter(instance, &aopts, acb);
    for (int i = 0; !areq.done && i < 1000; ++i) wgpuInstanceProcessEvents(instance);
    if (!areq.done || areq.status != WGPURequestAdapterStatus_Success || areq.adapter == NULL) {
        fprintf(stderr, "[webgpu] adapter request failed (status=%d)\n", (int)areq.status);
        return false;
    }
    WGPUAdapter adapter = areq.adapter;

    WGPUAdapterInfo info = {0};
    wgpuAdapterGetInfo(adapter, &info);
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
    wgpuAdapterRequestDevice(adapter, &ddesc, dcb);
    for (int i = 0; !dreq.done && i < 1000; ++i) wgpuInstanceProcessEvents(instance);
    if (!dreq.done || dreq.status != WGPURequestDeviceStatus_Success || dreq.device == NULL) {
        fprintf(stderr, "[webgpu] device request failed (status=%d)\n", (int)dreq.status);
        return false;
    }
    WGPUDevice device = dreq.device;

    *out_instance = instance;
    *out_adapter  = adapter;
    *out_device   = device;
    *out_queue    = wgpuDeviceGetQueue(device);
    *out_surface  = surface;
    *out_format   = (int)wgpu_choose_format(surface, adapter);
    return true;
}

/* ------------------------------------------------------------------------
 * Vtable: init / resize / frame lifecycle
 * ---------------------------------------------------------------------- */
static void wgpu_init(void) {
    s_ready = false;

    /* App-shell handoff: the launcher already stood up the WebGPU device and
     * surface for its own UI; adopt them wholesale so the game and launcher
     * share one device/surface (no second present target, no ownership war).
     * We do NOT own these objects — teardown must leave them to the shell. */
    if (platformHasHostWebGpu()) {
        s_instance       = (WGPUInstance)platformHostWgpuInstance();
        s_adapter        = (WGPUAdapter)platformHostWgpuAdapter();
        s_device         = (WGPUDevice)platformHostWgpuDevice();
        s_queue          = (WGPUQueue)platformHostWgpuQueue();
        s_surface        = (WGPUSurface)platformHostWgpuSurface();
        s_surface_format = (WGPUTextureFormat)platformHostWgpuSurfaceFormat();
        s_owns_device    = false;
        if (s_device == NULL || s_queue == NULL || s_surface == NULL) {
            fprintf(stderr, "[webgpu] host handoff incomplete — backend inert\n");
            return;
        }
        s_ready = true;
        fprintf(stderr, "[webgpu] adopted host device/surface (format=%d)\n",
                (int)s_surface_format);
        return;
    }

    s_owns_device = true;
    void *layer = NULL;
#ifdef __APPLE__
    layer = platformGetMetalLayer();
#endif
    int fmt = 0;
    if (!gfx_webgpu_bringup(layer, platformGetSdlWindow(),
                            &s_instance, &s_adapter, &s_device, &s_queue,
                            &s_surface, &fmt)) {
        return;   /* helper logged the specific failure; backend stays inert */
    }
    s_surface_format = (WGPUTextureFormat)fmt;
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
    s_sc_set = false; /* scissor is re-established by gfx_pc each frame */
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
    /* (Re)create the offscreen scene target + depth buffer at the render res. */
    if (s_scene_view == NULL || s_scene_w != rw || s_scene_h != rh) {
        if (s_scene_view != NULL) { wgpuTextureViewRelease(s_scene_view); s_scene_view = NULL; }
        if (s_scene_tex != NULL)  { wgpuTextureRelease(s_scene_tex);      s_scene_tex = NULL; }
        if (s_depth_view != NULL) { wgpuTextureViewRelease(s_depth_view); s_depth_view = NULL; }
        if (s_depth_tex != NULL)  { wgpuTextureRelease(s_depth_tex);      s_depth_tex = NULL; }
        WGPUTextureDescriptor td = {0};
        td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc |
                   WGPUTextureUsage_TextureBinding;
        td.dimension = WGPUTextureDimension_2D;
        td.size.width = rw; td.size.height = rh; td.size.depthOrArrayLayers = 1;
        td.format = s_surface_format;   /* BGRA8 — matches the surface for the present copy */
        td.mipLevelCount = 1; td.sampleCount = 1;
        s_scene_tex = wgpuDeviceCreateTexture(s_device, &td);
        s_scene_view = s_scene_tex ? wgpuTextureCreateView(s_scene_tex, NULL) : NULL;

        WGPUTextureDescriptor dd = {0};
        dd.usage = WGPUTextureUsage_RenderAttachment;
        dd.dimension = WGPUTextureDimension_2D;
        dd.size.width = rw; dd.size.height = rh; dd.size.depthOrArrayLayers = 1;
        dd.format = WGPU_DEPTH_FORMAT;
        dd.mipLevelCount = 1; dd.sampleCount = 1;
        s_depth_tex = wgpuDeviceCreateTexture(s_device, &dd);
        s_depth_view = s_depth_tex ? wgpuTextureCreateView(s_depth_tex, NULL) : NULL;

        s_scene_w = rw; s_scene_h = rh;
    }
    if (s_scene_view == NULL || s_depth_view == NULL) {
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
    WGPURenderPassDepthStencilAttachment depth = {0};
    depth.view = s_depth_view;
    depth.depthLoadOp = WGPULoadOp_Clear;
    depth.depthStoreOp = WGPUStoreOp_Store;
    depth.depthClearValue = 1.0f;   /* 1.0 = far, with WebGPU's 0..1 clip (0 = near) */

    WGPURenderPassDescriptor rp = {0};
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &att;
    rp.depthStencilAttachment = &depth;
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

    /* Minimap / radar overlay: a 2D screen-space pass into the scene target after
     * the geometry (the GL path draws it in gfx_end_frame, Metal in mtl_end_frame;
     * gfx_end_frame skips it for non-GL backends). Reads Input.MinimapEnabled +
     * the frame queue internally; no-op when disabled/empty. */
    {
        extern void minimap_overlay_draw_queued_frames_webgpu(int fb_width, int fb_height);
        minimap_overlay_draw_queued_frames_webgpu((int)s_scene_w, (int)s_scene_h);
    }

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
    /* Pipeline cache keyed by dynamic (blend|depth) state. Sized well above the
     * realistic combo count per combiner (a handful); the round-robin eviction
     * in wgpu_pipeline_for is a never-leak backstop for the theoretical maximum. */
    struct WgpuPipeEntry pipes[32];
    int npipes;
    int pipe_evict;   /* round-robin slot for the (never-hit) overflow case */
};
#define WGPU_PIPE_CACHE 32

#define WGPU_SHADER_MAX 1024
static struct ShaderProgram s_shaders[WGPU_SHADER_MAX];
static int s_shader_count = 0;

/* Currently loaded shader + dynamic blend state (set by load_shader /
 * set_blend_mode, read by draw_triangles). */
static struct ShaderProgram *s_cur_shader = NULL;
static enum GfxBlendMode      s_cur_blend = GFX_BLEND_DISABLED;

/* Single-entry draw bind-group cache (see wgpu_draw_triangles): the cache owns
 * the ref; key is {bgl, view0, samp0, view1, samp1} pointers. */
static WGPUBindGroup s_bg_cache = NULL;
static const void   *s_bg_key[5] = {0};

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

/* Map a GfxBlendMode to a WGPUBlendState, matching gfx_opengl_set_blend_mode's
 * default (diag mode 0) exactly. glBlendFunc applies the same (src,dst) factors
 * to BOTH the color and alpha channels, so both components use identical factors
 * here. Returns false for the opaque modes (no blend state attached):
 *   - DISABLED, and the two RDP-memory modes (GL disables HW blend for those —
 *     their blending is shader-side framebuffer sampling, a diag path not yet
 *     ported; the opaque HW state still matches GL).
 *   - MODULATE -> (DST_COLOR, ZERO)  = src*dst.
 *   - ALPHA / ALPHA_COVERAGE / ALPHA_CVG_WRAP_STENCIL -> (SRC_ALPHA, 1-SRC_ALPHA). */
static bool wgpu_blend_state(enum GfxBlendMode mode, WGPUBlendState *out) {
    memset(out, 0, sizeof(*out));
    if (mode == GFX_BLEND_DISABLED ||
        mode == GFX_BLEND_ALPHA_RDP_MEMORY ||
        mode == GFX_BLEND_ALPHA_RDP_CVG_MEMORY) {
        return false;   /* opaque */
    }
    WGPUBlendFactor src, dst;
    if (mode == GFX_BLEND_MODULATE) {
        src = WGPUBlendFactor_Dst;      /* GL_DST_COLOR */
        dst = WGPUBlendFactor_Zero;     /* GL_ZERO */
    } else {                            /* ALPHA + coverage/stencil variants */
        src = WGPUBlendFactor_SrcAlpha;
        dst = WGPUBlendFactor_OneMinusSrcAlpha;
    }
    out->color.operation = WGPUBlendOperation_Add;
    out->color.srcFactor = src;
    out->color.dstFactor = dst;
    out->alpha.operation = WGPUBlendOperation_Add;
    out->alpha.srcFactor = src;
    out->alpha.dstFactor = dst;
    return true;
}

/* Lazily build + cache the render pipeline for the current (shader, blend, depth)
 * dynamic state — WebGPU bakes all of it into the pipeline (mtl_pso_for shape). */
static WGPURenderPipeline wgpu_pipeline_for(struct ShaderProgram *prg, enum GfxBlendMode blend) {
    if (prg->module == NULL) {
        return NULL;
    }
    bool decal = wgpu_depth_is_decal();
    uint32_t key = (uint32_t)blend
                 | ((uint32_t)(s_depth_test ? 1 : 0)    << 4)
                 | ((uint32_t)(s_depth_update ? 1 : 0)  << 5)
                 | ((uint32_t)(s_depth_compare ? 1 : 0) << 6)
                 | ((uint32_t)(decal ? 1 : 0)           << 7);
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

    /* Depth: LessEqual when the N64 mode tests+compares (0..1 clip, 0 = near),
     * else Always; write when it tests+updates. Decal gets a small negative bias
     * so coplanar overlays win the test (mirrors GL glPolygonOffset(-2,-2)). */
    WGPUDepthStencilState ds = {0};
    ds.format = WGPU_DEPTH_FORMAT;
    ds.depthCompare = (s_depth_test && s_depth_compare) ? WGPUCompareFunction_LessEqual
                                                        : WGPUCompareFunction_Always;
    ds.depthWriteEnabled = (s_depth_test && s_depth_update)
                               ? WGPUOptionalBool_True : WGPUOptionalBool_False;
    if (decal) {
        ds.depthBias = -2;
        ds.depthBiasSlopeScale = -2.0f;
    }
    /* Depth-only format: stencil faces must be their default (Always/Keep) with
     * zero masks, or WebGPU rejects the pipeline. */
    ds.stencilFront.compare = WGPUCompareFunction_Always;
    ds.stencilFront.failOp = WGPUStencilOperation_Keep;
    ds.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
    ds.stencilFront.passOp = WGPUStencilOperation_Keep;
    ds.stencilBack = ds.stencilFront;

    WGPURenderPipelineDescriptor pd = {0};
    pd.layout = prg->playout;
    pd.vertex.module = prg->module;
    pd.vertex.entryPoint = wgpu_sv("vs_main");
    pd.vertex.bufferCount = 1;
    pd.vertex.buffers = &vbl;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.primitive.cullMode = WGPUCullMode_None;   /* N64 backface handled on CPU */
    pd.depthStencil = &ds;
    pd.multisample.count = 1;
    pd.multisample.mask = 0xFFFFFFFFu;
    pd.fragment = &fs;

    WGPURenderPipeline pipe = wgpuDeviceCreateRenderPipeline(s_device, &pd);
    if (pipe != NULL) {
        int slot;
        if (prg->npipes < WGPU_PIPE_CACHE) {
            slot = prg->npipes++;
        } else {
            /* Never expected (a combiner uses a handful of blend|depth combos),
             * but the backstop must not leak: evict a slot round-robin. The
             * render pass retains any pipeline already bound this frame, so
             * releasing the app-side handle here is safe. */
            slot = prg->pipe_evict;
            prg->pipe_evict = (prg->pipe_evict + 1) % WGPU_PIPE_CACHE;
            if (prg->pipes[slot].pipe != NULL) {
                wgpuRenderPipelineRelease(prg->pipes[slot].pipe);
            }
        }
        prg->pipes[slot].key = key;
        prg->pipes[slot].pipe = pipe;
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
    if (s == NULL) {
        return NULL;
    }
    if (s_sampler_n < (int)(sizeof(s_samplers) / sizeof(s_samplers[0]))) {
        s_samplers[s_sampler_n].linear = (int)linear_filter;
        s_samplers[s_sampler_n].cms = cms;
        s_samplers[s_sampler_n].cmt = cmt;
        s_samplers[s_sampler_n].sampler = s;
        s_sampler_n++;
    } else {
        /* Cache full (the N64 uses only a handful of filter/wrap combos, so this
         * is not expected): release rather than leak, and reuse slot 0's sampler
         * for this call so the draw still binds something valid. */
        static bool warned = false;
        if (!warned) { fprintf(stderr, "[webgpu] sampler cache full (%d)\n", s_sampler_n); warned = true; }
        wgpuSamplerRelease(s);
        return s_samplers[0].sampler;
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
    (void)depth_source_prim;
    s_depth_test = depth_test;
    s_depth_update = depth_update;
    s_depth_compare = depth_compare;
    s_zmode = zmode;
}
static void wgpu_set_viewport(int x, int y, int width, int height) {
    s_vp_x = x; s_vp_y = y; s_vp_w = width; s_vp_h = height;
}
static void wgpu_set_scissor(int x, int y, int width, int height) {
    s_sc_x = x; s_sc_y = y; s_sc_w = width; s_sc_h = height; s_sc_set = true;
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

/* Clip a rect (x,y,w,h) to [0,maxw] x [0,maxh], zeroing degenerate extents.
 * Order matters: shift for a negative origin first, bail to empty if the origin
 * is at/past the far edge, THEN clip the extent to the bound, THEN a final
 * non-negative clamp — so a containment adjustment can never re-introduce a
 * negative extent that would cast to a ~4-billion uint32 in Set{Viewport,Scissor}. */
static void wgpu_clamp_rect(int *x, int *y, int *w, int *h, int maxw, int maxh) {
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x >= maxw || *y >= maxh) { *w = 0; *h = 0; return; }
    if (*x + *w > maxw) *w = maxw - *x;
    if (*y + *h > maxh) *h = maxh - *y;
    if (*w < 0) *w = 0;
    if (*h < 0) *h = 0;
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

    /* Bind group: each used texture's live view + sampler, falling back to a 1x1
     * white texture / nearest sampler when unset. Cached single-entry keyed on
     * the (layout, view, sampler) pointers: consecutive same-material draws (the
     * common batched case) reuse it instead of allocating a new bind group every
     * draw. The key is on POINTERS, so a deleted texture (its view becomes NULL ->
     * the white fallback pointer) never spuriously matches a stale entry. */
    WGPUBindGroup bg = NULL;
    if (s_cur_shader->bgl != NULL) {
        WGPUTextureView v0 = s_white_view, v1 = s_white_view;
        WGPUSampler     m0 = s_default_sampler, m1 = s_default_sampler;
        if (s_cur_shader->info.used_textures[0]) {
            struct WgpuTexEntry *e = wgpu_tex_lookup(s_bound_tex[0]);
            if (e != NULL && e->view != NULL) v0 = e->view;
            if (s_bound_sampler[0] != NULL) m0 = s_bound_sampler[0];
        }
        if (s_cur_shader->info.used_textures[1]) {
            struct WgpuTexEntry *e = wgpu_tex_lookup(s_bound_tex[1]);
            if (e != NULL && e->view != NULL) v1 = e->view;
            if (s_bound_sampler[1] != NULL) m1 = s_bound_sampler[1];
        }
        const void *key[5] = { s_cur_shader->bgl, v0, m0, v1, m1 };
        if (s_bg_cache != NULL && memcmp(key, s_bg_key, sizeof(key)) == 0) {
            bg = s_bg_cache;   /* hit: reuse, no alloc/free this draw */
        } else {
            WGPUBindGroupEntry ents[4];
            int ne = 0;
            for (int t = 0; t < 2; t++) {
                if (!s_cur_shader->info.used_textures[t]) continue;
                WGPUBindGroupEntry te = {0}; te.binding = (uint32_t)(t * 2);
                te.textureView = t == 0 ? v0 : v1; ents[ne++] = te;
                WGPUBindGroupEntry se = {0}; se.binding = (uint32_t)(t * 2 + 1);
                se.sampler = t == 0 ? m0 : m1; ents[ne++] = se;
            }
            WGPUBindGroupDescriptor bd = {0};
            bd.layout = s_cur_shader->bgl;
            bd.entryCount = (size_t)ne;
            bd.entries = ents;
            bg = wgpuDeviceCreateBindGroup(s_device, &bd);
            if (bg != NULL) {
                if (s_bg_cache != NULL) wgpuBindGroupRelease(s_bg_cache);
                s_bg_cache = bg;   /* the cache owns the ref now */
                memcpy(s_bg_key, key, sizeof(key));
            }
        }
    }

    /* Viewport + scissor, Y-flipped to WebGPU's top-left origin (GL/gfx_pc emit
     * bottom-left; mirrors mtl_draw_triangles' originY = fb_h - (y + h)). BOTH
     * must be clamped to the scene bounds: WebGPU *validates* setViewport/
     * setScissorRect containment and a single out-of-range rect invalidates the
     * whole command buffer (black frame), whereas GL/Metal silently clip — so the
     * game may legitimately hand us a viewport/scissor extending past the target
     * (widescreen / split-screen). `wgpu_clamp_rect` clips a Y-flipped rect to
     * [0,W]x[0,H], zeroing degenerate extents. */
    {
        int vx = s_vp_x, vy = (int)s_scene_h - (s_vp_y + s_vp_h), vw = s_vp_w, vh = s_vp_h;
        wgpu_clamp_rect(&vx, &vy, &vw, &vh, (int)s_scene_w, (int)s_scene_h);
        if (vw > 0 && vh > 0) {
            wgpuRenderPassEncoderSetViewport(s_pass, (float)vx, (float)vy,
                                             (float)vw, (float)vh, 0.0f, 1.0f);
        }
    }
    {
        int sx = s_sc_set ? s_sc_x : 0;
        int sw = s_sc_set ? s_sc_w : (int)s_scene_w;
        int sh = s_sc_set ? s_sc_h : (int)s_scene_h;
        int sy = s_sc_set ? ((int)s_scene_h - (s_sc_y + s_sc_h)) : 0;
        wgpu_clamp_rect(&sx, &sy, &sw, &sh, (int)s_scene_w, (int)s_scene_h);
        wgpuRenderPassEncoderSetScissorRect(s_pass, (uint32_t)sx, (uint32_t)sy,
                                            (uint32_t)sw, (uint32_t)sh);
    }

    wgpuRenderPassEncoderSetPipeline(s_pass, pipe);
    if (bg != NULL) {
        wgpuRenderPassEncoderSetBindGroup(s_pass, 0, bg, 0, NULL);
    }
    wgpuRenderPassEncoderSetVertexBuffer(s_pass, 0, s_vbuf, voff, bytes);
    wgpuRenderPassEncoderDraw(s_pass, (uint32_t)(3 * buf_vbo_num_tris), 1, 0, 0);
    /* bg is owned by s_bg_cache (retained across draws); the render pass holds
     * its own reference until submit, so we never release it here. */
}

/* ------------------------------------------------------------------------
 * Minimap / radar overlay (T4b) — a 2D screen-space pass into the scene target
 * after the main geometry, mirroring the GL (minimap_overlay.c direct draws) and
 * Metal (gfx_metal_draw_minimap_overlay) paths. Vertices are MinimapOverlayVertex
 * {x,y (pixels), r,g,b,a} (stride 24); the shader maps pixels -> NDC with a
 * screen-size uniform. Called from wgpu_end_frame after the main render pass.
 * ---------------------------------------------------------------------- */
static WGPURenderPipeline s_mm_pipe = NULL;
static WGPUBindGroupLayout s_mm_bgl = NULL;
static WGPUBindGroup s_mm_bg = NULL;
static WGPUBuffer s_mm_ubuf = NULL;

static const char *kMinimapWGSL =
    "struct MMIn { @location(0) pos : vec2<f32>, @location(1) color : vec4<f32> };\n"
    "struct MMOut { @builtin(position) clip : vec4<f32>, @location(0) color : vec4<f32> };\n"
    "struct MMU { screen : vec2<f32>, pad : vec2<f32> };\n"
    "@group(0) @binding(0) var<uniform> mmu : MMU;\n"
    "@vertex fn vs_main(in : MMIn) -> MMOut {\n"
    "  var o : MMOut;\n"
    "  let ndc = vec2<f32>((in.pos.x / mmu.screen.x) * 2.0 - 1.0, 1.0 - (in.pos.y / mmu.screen.y) * 2.0);\n"
    "  o.clip = vec4<f32>(ndc, 0.0, 1.0);\n"
    "  o.color = in.color;\n"
    "  return o;\n}\n"
    "@fragment fn fs_main(in : MMOut) -> @location(0) vec4<f32> { return in.color; }\n";

static bool wgpu_ensure_minimap(void) {
    if (s_mm_pipe != NULL) {
        return true;
    }
    if (!s_ready) {
        return false;
    }
    WGPUShaderSourceWGSL src = {0};
    src.chain.sType = WGPUSType_ShaderSourceWGSL;
    src.code = wgpu_sv(kMinimapWGSL);
    WGPUShaderModuleDescriptor smd = {0};
    smd.nextInChain = (WGPUChainedStruct *)&src;
    WGPUShaderModule mod = wgpuDeviceCreateShaderModule(s_device, &smd);
    if (mod == NULL) {
        return false;
    }

    WGPUBindGroupLayoutEntry ue = {0};
    ue.binding = 0;
    ue.visibility = WGPUShaderStage_Vertex;
    ue.buffer.type = WGPUBufferBindingType_Uniform;
    ue.buffer.minBindingSize = 16;
    WGPUBindGroupLayoutDescriptor bgld = {0};
    bgld.entryCount = 1;
    bgld.entries = &ue;
    s_mm_bgl = wgpuDeviceCreateBindGroupLayout(s_device, &bgld);

    WGPUBufferDescriptor ubd = {0};
    ubd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    ubd.size = 16;
    s_mm_ubuf = wgpuDeviceCreateBuffer(s_device, &ubd);

    WGPUBindGroupEntry bge = {0};
    bge.binding = 0;
    bge.buffer = s_mm_ubuf;
    bge.size = 16;
    WGPUBindGroupDescriptor bgd = {0};
    bgd.layout = s_mm_bgl;
    bgd.entryCount = 1;
    bgd.entries = &bge;
    s_mm_bg = wgpuDeviceCreateBindGroup(s_device, &bgd);

    WGPUPipelineLayoutDescriptor pld = {0};
    pld.bindGroupLayoutCount = 1;
    pld.bindGroupLayouts = &s_mm_bgl;
    WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(s_device, &pld);

    WGPUVertexAttribute attrs[2] = {0};
    attrs[0].format = WGPUVertexFormat_Float32x2; attrs[0].offset = 0;  attrs[0].shaderLocation = 0;
    attrs[1].format = WGPUVertexFormat_Float32x4; attrs[1].offset = 8;  attrs[1].shaderLocation = 1;
    WGPUVertexBufferLayout vbl = {0};
    vbl.stepMode = WGPUVertexStepMode_Vertex;
    vbl.arrayStride = 24;   /* MinimapOverlayVertex: 6 floats */
    vbl.attributeCount = 2;
    vbl.attributes = attrs;

    WGPUBlendState blend = {0};   /* standard alpha (the overlay is translucent) */
    blend.color.operation = WGPUBlendOperation_Add;
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    WGPUColorTargetState color = {0};
    color.format = s_surface_format;
    color.writeMask = WGPUColorWriteMask_All;
    color.blend = &blend;

    WGPUFragmentState fs = {0};
    fs.module = mod; fs.entryPoint = wgpu_sv("fs_main");
    fs.targetCount = 1; fs.targets = &color;
    WGPURenderPipelineDescriptor pd = {0};
    pd.layout = pl;
    pd.vertex.module = mod; pd.vertex.entryPoint = wgpu_sv("vs_main");
    pd.vertex.bufferCount = 1; pd.vertex.buffers = &vbl;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.primitive.cullMode = WGPUCullMode_None;
    pd.multisample.count = 1; pd.multisample.mask = 0xFFFFFFFFu;
    pd.fragment = &fs;   /* no depth-stencil: 2D overlay pass has no depth attachment */
    s_mm_pipe = wgpuDeviceCreateRenderPipeline(s_device, &pd);

    wgpuPipelineLayoutRelease(pl);
    wgpuShaderModuleRelease(mod);
    return s_mm_pipe != NULL;
}

/* Called by minimap_overlay.c's flush. Returns nonzero on success (matching the
 * Metal hook's convention). Draws the queued overlay vertices into the scene
 * target in a fresh load-op render pass, using the still-open frame encoder. */
int gfx_webgpu_draw_minimap_overlay(const void *vertices, size_t vertex_count,
                                    int fb_width, int fb_height,
                                    int scissor_enabled, int scissor_x, int scissor_y,
                                    int scissor_w, int scissor_h) {
    (void)scissor_enabled; (void)scissor_x; (void)scissor_y; (void)scissor_w; (void)scissor_h;
    if (!s_ready || s_encoder == NULL || s_scene_view == NULL || s_vbuf == NULL ||
        vertices == NULL || vertex_count == 0 || fb_width <= 0 || fb_height <= 0) {
        return 0;
    }
    if (!wgpu_ensure_minimap()) {
        return 0;
    }
    uint32_t bytes = (uint32_t)(vertex_count * 24u);
    if ((uint64_t)s_vbuf_off + bytes > WGPU_VBUF_CAP) {
        return 0;
    }
    uint32_t voff = s_vbuf_off;
    wgpuQueueWriteBuffer(s_queue, s_vbuf, voff, vertices, bytes);
    s_vbuf_off += (bytes + 3u) & ~3u;

    float u[4] = { (float)fb_width, (float)fb_height, 0.0f, 0.0f };
    wgpuQueueWriteBuffer(s_queue, s_mm_ubuf, 0, u, sizeof(u));

    WGPURenderPassColorAttachment att = {0};
    att.view = s_scene_view;
    att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    att.loadOp = WGPULoadOp_Load;    /* preserve the rendered scene */
    att.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor rp = {0};
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &att;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(s_encoder, &rp);
    wgpuRenderPassEncoderSetPipeline(pass, s_mm_pipe);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, s_mm_bg, 0, NULL);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, s_vbuf, voff, bytes);
    wgpuRenderPassEncoderDraw(pass, (uint32_t)vertex_count, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    return 1;
}

/* Task 5: read back the last-rendered offscreen scene as GL-convention
 * bottom-left RGB (so platformSaveScreenshot + the parity/oracle tooling work on
 * WebGPU identically to GL/Metal). Copies the whole BGRA8 scene into a mappable
 * buffer, then extracts the requested rect with a vertical flip + BGRA->RGB.
 * Synchronous (submit + poll-map) — only the screenshot/parity path calls it. */
static bool wgpu_read_framebuffer_rgb(int x, int y, int width, int height, uint8_t *rgb_out) {
    if (!s_ready || s_scene_tex == NULL || rgb_out == NULL ||
        width <= 0 || height <= 0 || s_scene_w == 0 || s_scene_h == 0) {
        return false;
    }
    uint32_t bpr = ((s_scene_w * 4u + 255u) / 256u) * 256u;   /* 256-align */
    size_t buf_size = (size_t)bpr * s_scene_h;
    WGPUBufferDescriptor bd = {0};
    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bd.size = buf_size;
    WGPUBuffer buf = wgpuDeviceCreateBuffer(s_device, &bd);
    if (buf == NULL) {
        return false;
    }

    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(s_device, NULL);
    WGPUTexelCopyTextureInfo src = {0};
    src.texture = s_scene_tex; src.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferInfo dst = {0};
    dst.buffer = buf;
    dst.layout.bytesPerRow = bpr;
    dst.layout.rowsPerImage = s_scene_h;
    WGPUExtent3D ext = { s_scene_w, s_scene_h, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &ext);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, NULL);
    wgpuQueueSubmit(s_queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(enc);

    WgpuMapReq mr = {0};
    WGPUBufferMapCallbackInfo ci = {0};
    ci.mode = WGPUCallbackMode_AllowProcessEvents;
    ci.callback = on_map;
    ci.userdata1 = &mr;
    wgpuBufferMapAsync(buf, WGPUMapMode_Read, 0, buf_size, ci);
    for (int i = 0; !mr.done && i < 100000; ++i) wgpuDevicePoll(s_device, true, NULL);
    if (!mr.done || mr.status != WGPUMapAsyncStatus_Success) {
        wgpuBufferRelease(buf);
        return false;
    }
    const uint8_t *px = (const uint8_t *)wgpuBufferGetConstMappedRange(buf, 0, buf_size);
    if (px == NULL) {
        wgpuBufferUnmap(buf);
        wgpuBufferRelease(buf);
        return false;
    }
    /* rgb_out is width*height RGB, bottom-left origin (GL convention). The scene
     * is top-left, so scene row (H-1 - (y+row)) maps to output row `row`. */
    for (int row = 0; row < height; row++) {
        int scene_y = (int)s_scene_h - 1 - (y + row);
        uint8_t *out = rgb_out + (size_t)row * width * 3;
        if (scene_y < 0 || scene_y >= (int)s_scene_h) {
            memset(out, 0, (size_t)width * 3);
            continue;
        }
        const uint8_t *srow = px + (size_t)scene_y * bpr;
        for (int col = 0; col < width; col++) {
            int sx = x + col;
            if (sx < 0 || sx >= (int)s_scene_w) { out[col*3] = out[col*3+1] = out[col*3+2] = 0; continue; }
            const uint8_t *p = srow + (size_t)sx * 4;   /* BGRA8 */
            out[col*3+0] = p[2];
            out[col*3+1] = p[1];
            out[col*3+2] = p[0];
        }
    }
    wgpuBufferUnmap(buf);
    wgpuBufferRelease(buf);
    return true;
}

/* ------------------------------------------------------------------------
 * Optional: draw_modern_mesh (Task 6 — scene decor, G_MODERNMESH).
 *
 * A full-fidelity mesh (float32 pos/nrm/uv + rgba8, u32 indices, RGBA8 texture)
 * drawn into the live scene pass, transformed by the interpreter's MP matrix
 * with the N64 fog curve. Ports gfx_metal.mm's decor shader + cache. Renders
 * scene decoration, which is a no-op on the GL default (AUDIT-0001); needs
 * Video.SceneDecor=1. Per-mesh GPU resources are cached by mesh_id (ids are
 * monotonic/never reused, so a full evict at capacity is safe).
 * ---------------------------------------------------------------------- */
static const char *kModernWGSL =
    "struct DVin { @location(0) pos : vec3<f32>, @location(1) nrm : vec3<f32>, @location(2) uv : vec2<f32>, @location(3) col : vec4<f32> };\n"
    "struct DU { mvp : mat4x4<f32>, fog : vec4<f32>, fogMul : f32, fogOffset : f32, fogOn : f32, pad : f32 };\n"
    "@group(0) @binding(0) var<uniform> u : DU;\n"
    "@group(0) @binding(1) var dtex : texture_2d<f32>;\n"
    "@group(0) @binding(2) var dsmp : sampler;\n"
    "struct DOut { @builtin(position) position : vec4<f32>, @location(0) uv : vec2<f32>, @location(1) col : vec4<f32>, @location(2) fogA : f32 };\n"
    "@vertex fn vs_main(in : DVin) -> DOut {\n"
    "  var clip = u.mvp * vec4<f32>(in.pos, 1.0);\n"
    "  var fogA = 0.0;\n"
    "  if (u.fogOn > 0.5) {\n"
    "    var ww = clip.w;\n"
    "    if (abs(ww) < 0.001) { ww = 0.001; }\n"
    "    let winv = 1.0 / ww;\n"
    "    let coord = select(clip.z * winv, clip.z * 32767.0, winv < 0.0);\n"
    "    fogA = clamp(coord * u.fogMul + u.fogOffset, 0.0, 255.0) / 255.0;\n"
    "  }\n"
    "  var o : DOut;\n"
    "  clip.z = (clip.z + clip.w) * 0.5;\n"   /* GL-clip -> WebGPU 0..1, like Metal */
    "  o.position = clip; o.uv = in.uv; o.col = in.col; o.fogA = fogA;\n"
    "  return o;\n}\n"
    "fn decorShade(o : DOut, t : vec4<f32>) -> vec4<f32> {\n"
    "  var c = t.rgb * o.col.rgb;\n"
    "  c = mix(c, vec3<f32>(0.88, 0.91, 0.96), o.col.a);\n"   /* snow cover in vertex alpha */
    "  c = mix(c, u.fog.rgb, o.fogA);\n"
    "  return vec4<f32>(c, t.a);\n}\n"
    "@fragment fn fs_opaque(in : DOut) -> @location(0) vec4<f32> {\n"
    "  let c = decorShade(in, textureSample(dtex, dsmp, in.uv));\n"
    "  return vec4<f32>(c.rgb, 1.0);\n}\n"
    "@fragment fn fs_cutout(in : DOut) -> @location(0) vec4<f32> {\n"
    "  let t = textureSample(dtex, dsmp, in.uv);\n"
    "  if (t.a < 0.45) { discard; }\n"
    "  let c = decorShade(in, t);\n"
    "  return vec4<f32>(c.rgb, 1.0);\n}\n";

static WGPUShaderModule    s_modern_mod = NULL;
static WGPUBindGroupLayout s_modern_bgl = NULL;
static WGPUPipelineLayout  s_modern_pl = NULL;
static WGPURenderPipeline  s_modern_pipe[2] = {NULL, NULL};   /* [cutout] */
static WGPUSampler         s_modern_sampler = NULL;

struct WgpuModernEntry {
    uint32_t mesh_id;
    WGPUBuffer vbuf, ibuf;
    WGPUTexture tex;
    WGPUTextureView view;
    uint32_t idx_count;
};
static struct WgpuModernEntry s_modern_cache[64];
static int s_modern_count = 0;

static WGPURenderPipeline wgpu_modern_pipe(int cutout) {
    if (s_modern_pipe[cutout] != NULL) {
        return s_modern_pipe[cutout];
    }
    if (s_modern_mod == NULL) {
        WGPUShaderSourceWGSL src = {0};
        src.chain.sType = WGPUSType_ShaderSourceWGSL;
        src.code = wgpu_sv(kModernWGSL);
        WGPUShaderModuleDescriptor smd = {0};
        smd.nextInChain = (WGPUChainedStruct *)&src;
        s_modern_mod = wgpuDeviceCreateShaderModule(s_device, &smd);
        if (s_modern_mod == NULL) return NULL;

        WGPUBindGroupLayoutEntry e[3] = {0};
        e[0].binding = 0; e[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e[0].buffer.type = WGPUBufferBindingType_Uniform; e[0].buffer.minBindingSize = 96;
        e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment;
        e[1].texture.sampleType = WGPUTextureSampleType_Float; e[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[2].binding = 2; e[2].visibility = WGPUShaderStage_Fragment;
        e[2].sampler.type = WGPUSamplerBindingType_Filtering;
        WGPUBindGroupLayoutDescriptor bgld = {0};
        bgld.entryCount = 3; bgld.entries = e;
        s_modern_bgl = wgpuDeviceCreateBindGroupLayout(s_device, &bgld);
        WGPUPipelineLayoutDescriptor pld = {0};
        pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &s_modern_bgl;
        s_modern_pl = wgpuDeviceCreatePipelineLayout(s_device, &pld);
    }

    WGPUVertexAttribute a[4] = {0};
    a[0].format = WGPUVertexFormat_Float32x3; a[0].offset = 0;  a[0].shaderLocation = 0;
    a[1].format = WGPUVertexFormat_Float32x3; a[1].offset = 12; a[1].shaderLocation = 1;
    a[2].format = WGPUVertexFormat_Float32x2; a[2].offset = 24; a[2].shaderLocation = 2;
    a[3].format = WGPUVertexFormat_Unorm8x4;  a[3].offset = 32; a[3].shaderLocation = 3;
    WGPUVertexBufferLayout vbl = {0};
    vbl.stepMode = WGPUVertexStepMode_Vertex;
    vbl.arrayStride = 36;
    vbl.attributeCount = 4;
    vbl.attributes = a;

    WGPUColorTargetState color = {0};
    color.format = s_surface_format;
    color.writeMask = WGPUColorWriteMask_All;   /* opaque (frag outputs a=1) */
    WGPUFragmentState fs = {0};
    fs.module = s_modern_mod;
    fs.entryPoint = wgpu_sv(cutout ? "fs_cutout" : "fs_opaque");
    fs.targetCount = 1; fs.targets = &color;

    WGPUDepthStencilState ds = {0};
    ds.format = WGPU_DEPTH_FORMAT;
    ds.depthCompare = WGPUCompareFunction_LessEqual;
    ds.depthWriteEnabled = WGPUOptionalBool_True;
    ds.stencilFront.compare = WGPUCompareFunction_Always;
    ds.stencilFront.failOp = WGPUStencilOperation_Keep;
    ds.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
    ds.stencilFront.passOp = WGPUStencilOperation_Keep;
    ds.stencilBack = ds.stencilFront;

    WGPURenderPipelineDescriptor pd = {0};
    pd.layout = s_modern_pl;
    pd.vertex.module = s_modern_mod; pd.vertex.entryPoint = wgpu_sv("vs_main");
    pd.vertex.bufferCount = 1; pd.vertex.buffers = &vbl;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.primitive.cullMode = WGPUCullMode_None;   /* cutout cards are two-sided */
    pd.depthStencil = &ds;
    pd.multisample.count = 1; pd.multisample.mask = 0xFFFFFFFFu;
    pd.fragment = &fs;
    s_modern_pipe[cutout] = wgpuDeviceCreateRenderPipeline(s_device, &pd);
    return s_modern_pipe[cutout];
}

static struct WgpuModernEntry *wgpu_modern_resources(struct GfxModernMesh *mesh) {
    for (int i = 0; i < s_modern_count; i++) {
        if (s_modern_cache[i].mesh_id == mesh->mesh_id) return &s_modern_cache[i];
    }
    if (s_modern_count >= (int)(sizeof(s_modern_cache) / sizeof(s_modern_cache[0]))) {
        /* Level churn: ids never repeat, so releasing everything is safe. */
        for (int i = 0; i < s_modern_count; i++) {
            if (s_modern_cache[i].view) wgpuTextureViewRelease(s_modern_cache[i].view);
            if (s_modern_cache[i].tex)  wgpuTextureRelease(s_modern_cache[i].tex);
            if (s_modern_cache[i].vbuf) wgpuBufferRelease(s_modern_cache[i].vbuf);
            if (s_modern_cache[i].ibuf) wgpuBufferRelease(s_modern_cache[i].ibuf);
        }
        s_modern_count = 0;
    }

    uint32_t vbytes = mesh->vtx_count * 36u;
    uint32_t ibytes = mesh->idx_count * 4u;
    WGPUBufferDescriptor vd = {0};
    vd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst; vd.size = vbytes;
    WGPUBuffer vb = wgpuDeviceCreateBuffer(s_device, &vd);
    WGPUBufferDescriptor id = {0};
    id.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst; id.size = ibytes;
    WGPUBuffer ib = wgpuDeviceCreateBuffer(s_device, &id);
    if (vb == NULL || ib == NULL) { if (vb) wgpuBufferRelease(vb); if (ib) wgpuBufferRelease(ib); return NULL; }
    wgpuQueueWriteBuffer(s_queue, vb, 0, mesh->vtx, vbytes);
    wgpuQueueWriteBuffer(s_queue, ib, 0, mesh->idx, ibytes);

    WGPUTextureDescriptor td = {0};
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.dimension = WGPUTextureDimension_2D;
    td.size.width = (uint32_t)mesh->tex_w; td.size.height = (uint32_t)mesh->tex_h; td.size.depthOrArrayLayers = 1;
    td.format = WGPUTextureFormat_RGBA8Unorm;
    td.mipLevelCount = 1; td.sampleCount = 1;   /* single-level (no mip gen) */
    WGPUTexture tex = wgpuDeviceCreateTexture(s_device, &td);
    if (tex == NULL) { wgpuBufferRelease(vb); wgpuBufferRelease(ib); return NULL; }
    WGPUTexelCopyTextureInfo dst = {0};
    dst.texture = tex; dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout lay = {0};
    lay.bytesPerRow = (uint32_t)mesh->tex_w * 4u; lay.rowsPerImage = (uint32_t)mesh->tex_h;
    WGPUExtent3D ext = { (uint32_t)mesh->tex_w, (uint32_t)mesh->tex_h, 1 };
    wgpuQueueWriteTexture(s_queue, &dst, mesh->tex_rgba, (size_t)mesh->tex_w * mesh->tex_h * 4u, &lay, &ext);

    struct WgpuModernEntry *e = &s_modern_cache[s_modern_count++];
    e->mesh_id = mesh->mesh_id;
    e->vbuf = vb; e->ibuf = ib; e->tex = tex;
    e->view = wgpuTextureCreateView(tex, NULL);
    e->idx_count = mesh->idx_count;
    return e;
}

static void wgpu_draw_modern_mesh(struct GfxModernMesh *mesh, const float mvp[4][4],
                                  const float fog_color[3], float fog_mul,
                                  float fog_offset, int fog_enabled) {
    if (!s_ready || !s_frame_open || s_pass == NULL || mesh == NULL ||
        mesh->vtx == NULL || mesh->idx == NULL || mesh->tex_rgba == NULL ||
        mesh->tex_w <= 0 || mesh->tex_h <= 0) {
        return;
    }
    int cutout = mesh->cutout ? 1 : 0;
    WGPURenderPipeline pipe = wgpu_modern_pipe(cutout);
    if (pipe == NULL) return;
    struct WgpuModernEntry *res = wgpu_modern_resources(mesh);
    if (res == NULL) return;

    if (s_modern_sampler == NULL) {
        WGPUSamplerDescriptor sd = {0};
        sd.addressModeU = sd.addressModeV = sd.addressModeW = WGPUAddressMode_Repeat;
        sd.magFilter = sd.minFilter = WGPUFilterMode_Linear;
        sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;   /* single-level */
        sd.maxAnisotropy = 1;
        s_modern_sampler = wgpuDeviceCreateSampler(s_device, &sd);
    }

    /* Uniform (96 bytes): row-major MP memcpy'd into the mat4x4 (columns == MP
     * rows, so u.mvp * v == the CPU row-vector T&L), fog params. */
    float u[24];
    memset(u, 0, sizeof(u));
    memcpy(u, mvp, 16 * sizeof(float));
    u[16] = fog_color[0]; u[17] = fog_color[1]; u[18] = fog_color[2]; u[19] = 0.0f;
    u[20] = fog_mul; u[21] = fog_offset; u[22] = fog_enabled ? 1.0f : 0.0f; u[23] = 0.0f;

    WGPUBufferDescriptor ubd = {0};
    ubd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    ubd.size = sizeof(u);
    WGPUBuffer ubuf = wgpuDeviceCreateBuffer(s_device, &ubd);
    if (ubuf == NULL) return;
    wgpuQueueWriteBuffer(s_queue, ubuf, 0, u, sizeof(u));

    WGPUBindGroupEntry be[3] = {0};
    be[0].binding = 0; be[0].buffer = ubuf; be[0].size = sizeof(u);
    be[1].binding = 1; be[1].textureView = res->view;
    be[2].binding = 2; be[2].sampler = s_modern_sampler;
    WGPUBindGroupDescriptor bgd = {0};
    bgd.layout = s_modern_bgl; bgd.entryCount = 3; bgd.entries = be;
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(s_device, &bgd);

    wgpuRenderPassEncoderSetPipeline(s_pass, pipe);
    wgpuRenderPassEncoderSetBindGroup(s_pass, 0, bg, 0, NULL);
    wgpuRenderPassEncoderSetVertexBuffer(s_pass, 0, res->vbuf, 0, mesh->vtx_count * 36u);
    wgpuRenderPassEncoderSetIndexBuffer(s_pass, res->ibuf, WGPUIndexFormat_Uint32, 0, mesh->idx_count * 4u);
    wgpuRenderPassEncoderDrawIndexed(s_pass, res->idx_count, 1, 0, 0, 0);

    wgpuBindGroupRelease(bg);
    wgpuBufferRelease(ubuf);   /* the encoder retains referenced resources until submit */
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
    wgpu_draw_modern_mesh,   /* Task 6: scene decor (G_MODERNMESH) */
};
