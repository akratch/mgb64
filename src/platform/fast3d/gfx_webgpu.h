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

#include <stdbool.h>

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

/* Full WebGPU bring-up for a native window handle: instance -> surface ->
 * adapter -> device -> queue -> surface format. `metal_layer` is used on macOS
 * (a CAMetalLayer*), `sdl_window` (a SDL_Window*) everywhere else — pass both;
 * the unused one is ignored. On success returns true with every out-param set
 * and the caller owning the objects; on failure returns false (out-params
 * untouched). Shared by the engine and the app shell so the adapter/device
 * request sequence lives once. `out_format` is a WGPUTextureFormat as int. */
bool gfx_webgpu_bringup(void *metal_layer, void *sdl_window,
                        WGPUInstance *out_instance, WGPUAdapter *out_adapter,
                        WGPUDevice *out_device, WGPUQueue *out_queue,
                        WGPUSurface *out_surface, int *out_format);

#ifdef __cplusplus
}
#endif

#endif /* GFX_WEBGPU_H */
