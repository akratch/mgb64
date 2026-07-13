/*
 * gfx_webgpu.h — public seam of the WebGPU (wgpu-native) render backend.
 *
 * Exposes the pieces of gfx_webgpu.c that the app shell (AppHost) needs to
 * create a WGPUSurface from the window it owns, so both the standalone engine
 * and the launcher build surfaces through one code path. Only meaningful when
 * MGB64_WEBGPU_BACKEND.
 */
#ifndef GFX_WEBGPU_H
#define GFX_WEBGPU_H

#include <webgpu/webgpu.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create a WGPUSurface for a native window. macOS uses `metal_layer` (a
 * CAMetalLayer*, e.g. from platformGetMetalLayer or SDL_Metal_GetLayer) and
 * ignores `sdl_window`; every other platform ignores `metal_layer` and resolves
 * `sdl_window` (a SDL_Window*) to its HWND/X11/Wayland handle. Returns NULL on
 * failure (null instance, missing handle, unsupported windowing system). */
WGPUSurface gfx_webgpu_create_surface(WGPUInstance instance, void *metal_layer,
                                      void *sdl_window);

#ifdef __cplusplus
}
#endif

#endif /* GFX_WEBGPU_H */
