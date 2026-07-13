# AUDIT-0040: GLES Screenshots Read the Stale Back Buffer

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - handheld captures can contain stale or undefined frame data |
| Priority | P2 |
| Area | PortMaster GLES renderer / screenshots |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | `MGB64_PORTMASTER_GLES` manual and automated screenshots |

## Summary

Desktop OpenGL screenshots explicitly read the front buffer because capture
runs before the next swap and the back buffer is stale at that point. GLES
cannot use that front-buffer path, so the code knowingly leaves the default
back buffer selected and reads the stale or undefined pixels anyway.

## Evidence

[`platform_sdl.c`](../../../src/platform/platform_sdl.c) explains that capture
runs at the top of frame sync before the current swap, after the previous
`SDL_GL_SwapWindow`. Desktop OpenGL selects `GL_FRONT`, reads, then restores
`GL_BACK` because the post-swap back buffer is undefined.

Both `glReadBuffer` calls are excluded under `MGB64_PORTMASTER_GLES`. The
remaining `glReadPixels` therefore reads the default framebuffer state. The
source comment explicitly records: `GL_FRONT is unavailable in GLES;
screenshot reads back buffer (stale)`.

A real ARM/GLES capture was not available during this audit. The timing and
selected-buffer mismatch are source-proven; exact device artifact shape can
range from the previous render to garbage according to the swap implementation.

## Reproduction

On a PortMaster/GLES device, request labeled screenshots on several visibly
different consecutive frames, including a menu transition. Compare each BMP
with a camera photo or an in-frame trace marker for the requested frame. The
capture can lag, repeat, or contain undefined data because it reads the
post-swap back buffer.

## Root Cause

The screenshot hook is scheduled around desktop front-buffer availability.
The GLES port disabled an unavailable enum without moving capture to a point
where the rendered back buffer is still defined.

## Required End State

Capture a defined image before the swap that consumes it, or render/copy the
final frame into an offscreen texture readable on every backend. Preserve the
documented screenshot-frame and gameplay-timer semantics so the BMP corresponds
to the same simulation state as desktop and Metal captures.

## Acceptance Criteria

- GLES screenshots contain the requested completed frame, not a prior frame.
- Consecutive distinct test frames produce the expected distinct captures.
- Manual and automated capture use the same defined source.
- OpenGL, GLES, and Metal agree on vertical orientation and RGB/BMP conversion.
- Capture does not add a visible intermediate swap or one-frame presentation
  delay.
- PortMaster has a real runtime screenshot check, not compile coverage alone.

## Verification Plan

Add a synthetic frame-counter color pattern and capture consecutive frames on
desktop GL and a GLES device, asserting the encoded counter and pixels. Repeat
with normal, native-size, menu, and gameplay-timer screenshots and compare
orientation and dimensions.

## Related Work

- AUDIT-0043 requires screenshot write failure to propagate independently of
  which framebuffer source is used.
