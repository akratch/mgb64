# AUDIT-0040: GLES Screenshots Read the Stale Back Buffer

| Field | Value |
| --- | --- |
| Status | Fixed (GLES-device capture owner-verifiable) |
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

## Resolution

Route (a) from the design — capture-before-swap into a persistent buffer —
was implemented (route (b), reading the scene FBO, was rejected: it would miss
the output VI filter + minimap that are composited into the default framebuffer
*after* the scene FBO). The GLES path now reads a DEFINED composited frame:

1. `gfx_run_dl` (`src/platform/fast3d/gfx_pc.c`), after `gfx_rapi->end_frame()`
   composition + the minimap draw and BEFORE the swap in `gfx_end_frame`, calls
   (GLES build only, `#if defined(MGB64_PORTMASTER_GLES) && defined(NATIVE_PORT)`)
   `gfx_opengl_capture_default_framebuffer()` when a screenshot is pending. The
   pending check is `platformScreenshotCapturePendingForGles()` — a side-effect-free
   SUPERSET of the real capture conditions (F2 request, auto-frame/timer within a
   proximity window, diag display-cast/menu env latched), so it is zero-cost on
   the common no-capture path and can only over-arm (harmless: the freshest stash
   is the one consumed), never under-arm the target frame.
2. `gfx_opengl_capture_default_framebuffer()` (`src/platform/fast3d/gfx_opengl.c`)
   binds the default framebuffer, `glReadBuffer(GL_BACK)`, and `glReadPixels`
   into a persistent RGB buffer. Reading the bound default framebuffer BEFORE the
   swap is defined in GLES3 (unlike a post-swap back-buffer read), so it is
   GLES-safe. Bottom-up RGB, same convention as the desktop front-buffer path.
3. `platformSaveScreenshot()` (`src/platform/platform_sdl.c`), under
   `#ifdef MGB64_PORTMASTER_GLES`, consumes the stash via
   `gfx_opengl_get_captured_frame()` instead of reading the stale back buffer,
   and FAILS CLOSED (leaves `s_lastScreenshotFailed=1`, writes no file) if no
   valid frame is stashed. The desktop-GL `#else` branch keeps its byte-identical
   `glReadBuffer(GL_FRONT)` path; Metal/WebGPU are unchanged.

Inert-by-default: on the default desktop WebGPU build the GLES branches compile
out, the desktop GL front-buffer path is byte-identical (verified: normal capture
health unchanged), and all seven determinism input tapes replay byte-exact
(`tools/fidelity/tape_regression.sh`), so the hook adds no sim/replay divergence.

### Under the deprecation plan (BACKEND_DEPRECATION_PLAN.md Phase G)

The fix is correct under BOTH branches: G-1 deletes `gfx_opengl.c` entirely (the
new functions go with it, moot), and G-2 keeps the GLES path (the fix is
load-bearing there). It does not assume deletion.

## Verification harness (ROM-free synthetic frame counter)

`GE007_SYNTH_FRAME_PATTERN=1` makes `gfx_run_dl` overwrite each composited frame,
after end_frame + minimap, with a solid color encoding the per-presented-frame
counter `g_frame_count_diag` (R=(n>>16)&255, G=(n>>8)&255, B=n&255) — env-gated,
inert by default, GL/GLES only. `tools/synth_frame_screenshot_harness.sh` boots N
consecutive `--screenshot-frame` captures, decodes the counter from each BMP, and
asserts uniform scene region + distinct + strictly-increasing counters + correct
640x480 dimensions. Registered as ctest `port_synth_frame_screenshot` (desktop GL,
ROM-gated, self-skips 125 with no ROM/binary). Desktop GL exercises the identical
render→capture→decode plumbing the owner runs on the GLES device; on desktop GL
it reads the front buffer, on GLES it reads the pre-swap stash under test.

### Owner-device procedure (PortMaster / GLES handheld)

On the handheld with the `MGB64_PORTMASTER_GLES` build (`./ge007`) and a ROM
present, run the SAME harness with `--renderer device` (drops the desktop
`GE007_RENDERER=gl` override so the on-device GLES binary is used) and `--no-build`:

```
GE007_ROM=/path/to/rom.z64 \
tools/synth_frame_screenshot_harness.sh --renderer device --no-build \
    --binary /path/to/ge007 --rom /path/to/rom.z64 \
    --start-frame 40 --count 3 --level 33
```

Expect: `PASS: N consecutive captures, counters [...], uniform + distinct +
monotonic`. A regression to the stale back buffer shows as a non-uniform scene
region or repeated/frozen counters. A quick manual check equivalent:
`GE007_SYNTH_FRAME_PATTERN=1 ./ge007 --rom ROM --level 33 --deterministic
--screenshot-frame 40 --screenshot-label t40 --screenshot-exit` then inspect the
BMP's center color encodes 40-ish and the scene fill is a single solid color.

## Related Work

- AUDIT-0043 requires screenshot write failure to propagate independently of
  which framebuffer source is used. The GLES branch here fails closed (no file
  written, failure flag preserved) when no frame is stashed, consistent with it.
