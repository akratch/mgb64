# AUDIT-0003: Screenshot Series Calls OpenGL Under Metal and Crashes

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S2 - deterministic backend crash |
| Priority | P2 |
| Area | Rendering / capture / Metal |
| Evidence level | Runtime reproduced |
| Confidence | High |
| Origin | Newly confirmed on 2026-07-12 |
| Affected configurations | macOS native Metal with `GE007_SCREENSHOT_SERIES_DIR` enabled |

## Summary

The screenshot-series diagnostic is implemented in shared renderer code but
calls `glReadPixels` directly. Native Metal has no OpenGL context, so the first
scheduled capture deterministically raises `SIGSEGV`. The capture is also
scheduled before backend end-of-frame composition and before the OpenGL
minimap overlay, so even a non-crashing read does not represent the final frame.

## Evidence

- [`gfx_diag_screenshot_series_capture_if_due`](../../../src/platform/fast3d/gfx_pc.c#L23936)
  allocates the image and unconditionally calls `glReadPixels` at line 24024.
- The function is invoked at
  [`gfx_run_dl`](../../../src/platform/fast3d/gfx_pc.c#L24232), before
  `gfx_rapi->end_frame()` at line 24273 and before the OpenGL minimap draw at
  line 24281.
- The adjacent one-shot diagnostic already recognizes the backend distinction
  and routes Metal through `read_framebuffer_rgb` at lines 24249-24256.
- Metal's [`mtl_end_frame`](../../../src/platform/fast3d/gfx_metal.mm#L3098)
  performs SSAO/SMAA/output filtering, draws the minimap, assigns the final
  readback source, and presents between lines 3134 and 3174.
- Metal exposes a backend readback implementation in
  [`mtl_read_framebuffer_rgb`](../../../src/platform/fast3d/gfx_metal.mm#L3634).

A release runtime reproduction used native Metal, direct-booted Dam, and set
the series to start immediately, capture every frame, and stop after five. The
log reached:

```text
[metal] first frame: scene 2880x1620, geometry encoder open
[SCREENSHOT-SERIES] enabled ... after=0 every=1 limit=5 room="*"
[CRASH] Signal 11 (unrecoverable)
[CRASH-DL] frame=1 ...
```

The process exited with a signal before writing any PPM file. The backtrace
placed the fault in `gfx_run_dl`, matching the direct GL call.

## Reproduction

On macOS, use a clean directory and run a direct-boot stage with:

```text
GE007_RENDERER=metal
GE007_SCREENSHOT_SERIES_DIR=<writable-directory>
GE007_SCREENSHOT_SERIES_AFTER_FRAME=0
GE007_SCREENSHOT_SERIES_EVERY=1
GE007_SCREENSHOT_SERIES_LIMIT=5
```

The process crashes on frame 1 and creates no series images. The same stage
without `GE007_SCREENSHOT_SERIES_DIR` continues normally.

## Root Cause

Screenshot series predates or bypasses the rendering API's framebuffer
readback abstraction. A second architectural error couples capture timing to
display-list completion rather than final frame composition. The result is a
backend violation on Metal and incomplete frame semantics on both backends.

## Required End State

Remove all direct OpenGL operations from the shared screenshot-series path.
Schedule the series capture after backend end-of-frame post-processing and the
queued minimap overlay, while the completed image is still available for
readback. Use `gfx_backend_read_framebuffer_rgb` (or an equivalent required
rendering-API operation) for every backend.

Check and report readback, path creation, header write, row write, flush, and
close failures. Increment `written` only after a complete file is durable.
Define the series image contract as bottom-left backend RGB converted once to a
top-left PPM, with dimensions matching the current drawable. The move must not
change frame scheduling, simulation state, or capture cadence.

## Acceptance Criteria

- The Metal reproduction exits normally and writes exactly five valid P6 PPMs.
- Each file has the expected dimensions and payload length and is nonblank.
- Captures have the correct vertical orientation.
- Captures include active output post-FX and the minimap on both OpenGL and
  Metal.
- OpenGL's frame numbers, room filter, limit behavior, filenames, and image
  orientation remain correct.
- A failed backend readback writes no partial success file and emits an
  actionable error.
- No OpenGL symbol is called by the shared series function.
- Capture does not introduce an unbounded per-frame allocation or GPU stall
  outside frames that are actually due.

## Verification Plan

Add a backend-mock unit test for cadence and failure accounting, then run a
five-frame capture on OpenGL and Metal. Parse every PPM header and length, check
pixel variance, and compare the final composed markers (post-FX and minimap)
against the normal platform screenshot path.

## Related Work

- Fidelity finding `FID-0021` discusses broader Metal mid-frame readback stalls
  and degraded targets. This issue is narrower: it is a direct OpenGL call and
  deterministic crash in screenshot series.
- The regular `--screenshot-frame` path in `platform_sdl.c` reads the last
  presented image and is not the crashing path described here.
