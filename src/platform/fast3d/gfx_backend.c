/*
 * gfx_backend.c — Render-backend selection (see gfx_backend.h).
 *
 * Compiled on all platforms; returns false anywhere but macOS so OpenGL stays
 * the default. Metal is chosen only when GE007_RENDERER=metal is set.
 */
#include "gfx_backend.h"

#include <stdlib.h>
#include <string.h>

bool gfx_backend_use_webgpu(void) {
#ifdef MGB64_WEBGPU_BACKEND
    /* WebGPU is the DEFAULT backend when compiled in: used unless GE007_RENDERER
     * explicitly selects the GL or Metal fallback. `gl`/`opengl` -> OpenGL (the
     * one-release fallback per the flip runbook); `metal` -> native Metal (still
     * used by --remaster for its post-FX/SSAO on macOS). Anything else, or unset,
     * is WebGPU. Cached on first call; read consistently before/after window
     * creation. */
    static int cached = -1;
    if (cached < 0) {
        const char *r = getenv("GE007_RENDERER");
        if (r != NULL && (strcmp(r, "gl") == 0 || strcmp(r, "opengl") == 0 ||
                          strcmp(r, "GL") == 0 || strcmp(r, "OpenGL") == 0 ||
                          strcmp(r, "metal") == 0 || strcmp(r, "Metal") == 0)) {
            cached = 0;
        } else {
            cached = 1;   /* default (unset or "webgpu") */
        }
    }
    return cached != 0;
#else
    return false;
#endif
}

bool gfx_backend_use_opengl(void) {
    /* GL is the default/fallback: active whenever neither Metal nor WebGPU is. */
    return !gfx_backend_use_metal() && !gfx_backend_use_webgpu();
}

bool gfx_backend_use_metal(void) {
#ifdef __APPLE__
    /* A WebGPU session on macOS drives Metal through wgpu-native, so the window
     * must NOT also be a native-Metal-backend window: WebGPU owns the layer. */
    if (gfx_backend_use_webgpu()) {
        return false;
    }
    static int cached = -1;
    if (cached < 0) {
        const char *r = getenv("GE007_RENDERER");
        cached = (r != NULL && (strcmp(r, "metal") == 0 || strcmp(r, "Metal") == 0)) ? 1 : 0;
    }
    return cached != 0;
#else
    return false;
#endif
}
