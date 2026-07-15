// gfx_webgpu_compat.h — single seam between wgpu-native (desktop) and
// emdawnwebgpu (browser). Everything dialect-specific in gfx_webgpu.c goes
// through these three primitives so the 4k-line backend stays shared.
//
// Browser rule: WebGPU is async-only; a spin that never yields to the JS
// event loop deadlocks. Under Asyncify, emscripten_sleep() unwinds the wasm
// stack and lets mapAsync/requestAdapter callbacks fire — so the SAME
// "loop until done" call-sites work on both dialects with a different pump.
//
// Seam discipline (docs/superpowers/plans/2026-07-15-web-port.md, Task W7):
// ALL dialect divergence lives HERE. gfx_webgpu.c carries no inline
// `#ifdef __EMSCRIPTEN__` outside this include and the wgpuCompatCreateSurface
// bodies.
#ifndef GFX_WEBGPU_COMPAT_H
#define GFX_WEBGPU_COMPAT_H

#ifdef __EMSCRIPTEN__
  #include <webgpu/webgpu.h>          /* emdawnwebgpu: modern unified header */
  #include <emscripten/emscripten.h>
  #include <emscripten/html5.h>
  /* Browser: yield to the event loop so the JS-side promise resolves, THEN
   * drain the future queue so AllowProcessEvents callbacks actually dispatch.
   * emscripten_sleep() alone resolves the promise but never fires the C
   * callback (it only runs during ProcessEvents), so both steps are required. */
  #define WGPU_COMPAT_PUMP(instance, device)                    \
      do {                                                      \
          emscripten_sleep(1);                                  \
          if (instance) wgpuInstanceProcessEvents((instance));  \
      } while (0)
  /* Browser: the canvas is presented automatically when the frame's JS task
   * yields (via requestAnimationFrame); emscripten's WebGPU binding ABORTS on
   * an explicit wgpuSurfacePresent, so presenting is a no-op here. */
  #define WGPU_COMPAT_PRESENT(surface) ((void)(surface))
#else
  #include <webgpu/webgpu.h>
  #include <webgpu/wgpu.h>            /* wgpu-native extensions             */
  /* Native: drive wgpu-native's event machinery.                           */
  #define WGPU_COMPAT_PUMP(instance, device)                    \
      do {                                                      \
          if (device)        wgpuDevicePoll((device), true, NULL); \
          else if (instance) wgpuInstanceProcessEvents((instance)); \
      } while (0)
  /* Native: wgpu-native presents the surface explicitly. */
  #define WGPU_COMPAT_PRESENT(surface) wgpuSurfacePresent((surface))
#endif

/* Loop until `cond` is nonzero or max_iters pumps elapse. Identical shape to
 * the existing spins; only the pump differs per dialect.                    */
#define WGPU_COMPAT_WAIT(cond, instance, device, max_iters)                  \
    do {                                                                     \
        for (int _wc_i = 0; !(cond) && _wc_i < (max_iters); ++_wc_i) {       \
            WGPU_COMPAT_PUMP((instance), (device));                          \
        }                                                                    \
    } while (0)

struct SDL_Window;
/* Create a surface for the dialect: native window descriptors on desktop,
 * the #mgb64-canvas selector in the browser. Implemented in gfx_webgpu.c —
 * the desktop `#else` body holds the Metal-layer / Win32 / X11 / Wayland
 * descriptor code (wgpu-native types kept off the browser compile); the
 * browser body builds the canvas descriptor.
 *
 * NOTE: carries `metal_layer` in addition to the plan's `window` because the
 * native app shell (app_host.cpp → gfx_webgpu_bringup) owns a window/layer
 * distinct from platformGetMetalLayer(); dropping it would break the shell's
 * surface. Browser body ignores both handles (the page owns one canvas).     */
WGPUSurface wgpuCompatCreateSurface(WGPUInstance instance, void *metal_layer,
                                    struct SDL_Window *window);

#endif /* GFX_WEBGPU_COMPAT_H */
