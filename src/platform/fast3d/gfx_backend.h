/*
 * gfx_backend.h — Render-backend selection (GL default; Metal opt-in on macOS).
 */
#ifndef GFX_BACKEND_H
#define GFX_BACKEND_H

#include <stdbool.h>

/* True when the native Metal backend should be used. macOS-only, opt-in via the
 * GE007_RENDERER=metal environment variable. False on every other platform and
 * whenever the flag is unset — so OpenGL stays the default everywhere. The
 * result is cached on first call; it must be read consistently before and after
 * window creation (GL vs Metal window flags are mutually exclusive). */
bool gfx_backend_use_metal(void);

#endif /* GFX_BACKEND_H */
