# AUDIT-0003: Screenshot Series Calls OpenGL Under Metal and Crashes

| Field | Value |
| --- | --- |
| Status | Fixed (crash + capture-composition call-site move + backend-mock cadence/write ctest all landed; sole remaining tail is owner-only Metal 5-frame series verification) |
| Severity | S2 - deterministic backend crash |
| Priority | P2 |
| Area | Rendering / capture / Metal |
| Evidence level | Runtime reproduced |
| Confidence | High |
| Origin | Newly confirmed on 2026-07-12 |
| Affected configurations | macOS native Metal with `GE007_SCREENSHOT_SERIES_DIR` enabled |

## Resolution

**Crash fixed** in commit `6c1a23d` (2026-07-13, "do-now batch — 10 mechanical
audit fixes"). The screenshot-series capture no longer calls `glReadPixels`
directly: it now routes through `gfx_backend_read_framebuffer_rgb()`
(defined `src/platform/fast3d/gfx_pc.c:24029`; GL -> `glReadPixels`, Metal ->
blit, WebGPU -> scene-texture copy), called from the series-capture path at
`gfx_pc.c:24127` inside a block carrying an explicit `[AUDIT-0003]` comment
(`gfx_pc.c:24124-24132`). A failed readback frees the buffer and returns
without writing a partial file (:24127-24132). Native Metal no longer crashes
on a scheduled series capture.

**Call-site move — DONE** (HARNESS_STRATEGY.md Phase C / C1). The series capture
call was moved out of the mid-`gfx_run_dl` position (formerly before
`gfx_rapi->end_frame()` and the OpenGL minimap draw) to immediately **after** the
`#ifdef NATIVE_PORT` minimap block, just before
`gfx_trace_glass_shard_coverage_frame_end()`, guarded by an `[AUDIT-0003]`
comment. Every backend now reads back the fully composited frame (output post-FX
+ minimap), not a pre-composition one. Cadence keys on
`g_frame_count_diag`/`g_BgCurrentRoom`, both stable across `end_frame()` within
one `gfx_run_dl` call, so the move does not change capture cadence (verified: a
before/after WebGPU capture at the same frame differs only by the added
composition). Accepted trade-off noted in the comment: the readback now runs
after the recovery-gate clear, but the path already fail-closes on a readback
error and writes no partial file.

**Backend-mock cadence/failure test — DONE.** The cadence/accounting logic and
the P6 write were extracted from the static `gfx_diag_screenshot_series_capture_if_due()`
in `gfx_pc.c` into a dedicated ROM-free/GPU-free TU
(`src/platform/fast3d/screenshot_series.{h,c}`) that takes a readback function
pointer; the `gfx_pc.c` function is now a thin wrapper passing the real
`gfx_backend_read_framebuffer_rgb` adapter. New ctest `screenshot_series`
(`tests/test_screenshot_series.c`) mocks the readback and asserts exact
`AFTER_FRAME`/`EVERY`/`LIMIT` cadence, the room filter, the written-only-on-
durable-file contract (valid P6 header, expected byte size, vertical flip applied),
that a failed readback writes no file and does not block later frames, and that
readback is not invoked once the limit is reached.

**Still outstanding — owner-only:** a native-Metal 5-frame series run producing
exactly five valid, nonblank, correctly oriented P6 PPMs (no Metal runtime on
this host). This is the sole remaining verification tail. The GL runtime
composition proof could not be captured headlessly on this macOS host either:
GL-over-Metal stalls at frame 0 (the documented SSAO GL-over-Metal hang, a
pre-existing fallback-backend issue unrelated to this change). WebGPU is the
default backend and its composition proof passed.

The Summary/Evidence/Reproduction/Root-Cause sections below describe the
pre-fix state (kept for record); Required End State / Acceptance Criteria /
Verification Plan describe what is now the remaining scope.

## Summary

The screenshot-series diagnostic was implemented in shared renderer code but
called `glReadPixels` directly. Native Metal has no OpenGL context, so every
scheduled capture deterministically raised `SIGSEGV`; **that crash is fixed**
(see Resolution). The capture is still scheduled before backend end-of-frame
composition and before the OpenGL minimap overlay, so even the now-safe read
does not yet represent the final composited frame — the call-site move is the
remaining scope of this issue.

## Evidence (pre-fix, historical — line numbers below predate `6c1a23d`)

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

## Reproduction (pre-fix, historical)

The crash below no longer reproduces after `6c1a23d`; kept for record. On
macOS, use a clean directory and run a direct-boot stage with:

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

**Fixed part:** screenshot series bypassed the rendering API's framebuffer
readback abstraction and called `glReadPixels` directly, which is a backend
violation on Metal (no GL context) — this is what crashed. Fixed by routing
through `gfx_backend_read_framebuffer_rgb()` (see Resolution).

**Remaining part:** a second, independent architectural gap — capture timing
is coupled to display-list completion (`gfx_pc.c:24346`) rather than to final
frame composition (`end_frame()` at :24386, minimap at :24392-24393) — was
never part of the crash and is not fixed by the readback-routing change. It
produces syntactically valid but incomplete (pre-post-FX, pre-minimap)
captures on every backend, not just Metal.

## Required End State

The direct-OpenGL crash is resolved (`gfx_backend_read_framebuffer_rgb` is now
used for every backend; see Resolution). What remains to reach the original
required end state:

- Move the `gfx_diag_screenshot_series_capture_if_due()` call from
  `gfx_pc.c:24346` to immediately after the `#ifdef NATIVE_PORT` minimap block
  ends (:24394), before `gfx_trace_glass_shard_coverage_frame_end()` (:24395),
  so the completed (post-FX + minimap) image is what gets read back on every
  backend. This must not change frame scheduling, simulation state, or capture
  cadence — accounting keys on `g_frame_count_diag`/`g_BgCurrentRoom`, which
  are stable across `end_frame()`.
- Readback/path/write failure accounting (readback, path creation, header
  write, row write, flush, close) and the write-only-after-durable-file
  contract already exist in the current code (:24127-24132 and onward) but are
  untested — see Verification Plan.

## Acceptance Criteria

Already satisfied (fixed in `6c1a23d`):
- No OpenGL symbol is called by the shared series function on non-GL backends
  (routed through `gfx_backend_read_framebuffer_rgb`).
- A failed backend readback writes no partial success file and emits an
  actionable error (:24127-24132).
- OpenGL's frame numbers, room filter, limit behavior, filenames, and image
  orientation remain correct (unchanged code path for GL).

Still outstanding:
- The Metal reproduction exits normally and writes exactly five valid,
  nonblank P6 PPMs with correct vertical orientation (owner-only: no Metal
  runtime on this host).
- Captures include active output post-FX and the minimap on both OpenGL and
  Metal (requires the call-site move above).
- Capture does not introduce an unbounded per-frame allocation or GPU stall
  outside frames that are actually due (should hold post-move; re-verify after
  the change).

## Verification Plan

1. **Solo (this host):** move the call site as described above; run WebGPU
   (default) and GL (`GE007_RENDERER=gl`) series captures and confirm each PPM
   includes post-FX + minimap with correct top-left orientation and valid P6
   headers.
2. **Solo (this host):** add a backend-mock cadence/failure ctest — extract the
   cadence gate and write/accounting logic from the static function into a
   small non-static ROM-free/GPU-free TU (`src/platform/fast3d/screenshot_series.c`)
   taking a readback function pointer plus frame/room inputs; a new
   `tests/test_screenshot_series.c` mocks readback (success + failure) and
   asserts correct `AFTER_FRAME`/`EVERY`/`LIMIT`/room cadence, that the
   written-count increments only on a durable full file, and that a failed
   readback writes no file.
3. **Owner-only:** a Metal five-frame series capture — parse every PPM header
   and length, check pixel variance, and compare the final composed markers
   (post-FX and minimap) against the normal platform screenshot path.

## Related Work

- Fidelity finding `FID-0021` discusses broader Metal mid-frame readback stalls
  and degraded targets. This issue is narrower: it is a direct OpenGL call and
  deterministic crash in screenshot series.
- The regular `--screenshot-frame` path in `platform_sdl.c` reads the last
  presented image and is not the crashing path described here.
