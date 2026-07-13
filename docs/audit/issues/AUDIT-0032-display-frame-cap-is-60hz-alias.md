# AUDIT-0032: Display Frame Cap Is a 60 Hz Alias Without Render Interpolation

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S4 - a selectable display-rate mode cannot deliver display-rate rendering |
| Priority | P3 |
| Area | Frame pacing / high-refresh presentation |
| Evidence level | Source and design-plan proven |
| Confidence | High |
| Origin | Newly standardized improvement opportunity from current source |
| Affected configurations | `Video.FrameCap=display`, especially displays above 60 Hz |

## Summary

The settings surface offers a `display` frame-cap mode, but the implementation
always returns a 60 Hz period for it. This is the necessary correctness fallback
for the fixed 60 Hz simulation after a prior high-refresh speedup defect, but it
means a 90, 120, or 144 Hz display receives no additional presentation frames
and the option is currently indistinguishable from `60`.

## Evidence

[`platform_sdl.c`](../../../src/platform/platform_sdl.c) registers 30, 60, and
display options and describes them as frame pacing caps. In
`platformFrameCapPeriodMs`, the display branch returns `1000.0 / 60.0`. Its
comment states that display behaves like 60 until render interpolation lands,
because running the existing loop at high refresh advances the integer-tick
simulation too quickly.

[`UNCAPPED_FPS_PLAN.md`](../../design/UNCAPPED_FPS_PLAN.md) documents the
required architecture: preserve a fixed authoritative 60 Hz simulation and add
render-only frames plus interpolation, deterministic purity gates, late look,
and snap handling. This audit did not treat the current 60 Hz safety cap as a
regression; the gap is the unfinished advertised mode.

## Reproduction

Run on a display above 60 Hz with VSync enabled. Compare `Video.FrameCap=60`
and `display` using the FPS overlay or frame-present trace. Both modes target
16.667 ms and present approximately 60 frames per second.

## Root Cause

Simulation and presentation still share one frame loop. The display option had
to be clamped after the N64-style tick rounding made high-refresh presents
advance gameplay at 1.25x to 2x speed. The interpolation design has not yet
been implemented.

## Required End State

Keep simulation, gameplay input consumption, audio, RAMROM, and deterministic
state at fixed 60 Hz while producing render-only presentation frames at the
actual display rate. Interpolate camera and world transforms with defined snap
rules and no writes to hashed simulation state. Until that is complete, label
the option as a 60 Hz compatibility alias or hide it to avoid overstating it.

## Acceptance Criteria

- Display mode presents above 60 fps on a verified high-refresh panel.
- Game speed, timers, AI, physics, audio cadence, and input semantics match the
  60 Hz reference.
- Deterministic and RAMROM sessions bypass interpolation and keep identical
  hashes.
- Camera cuts, teleports, pause, menus, stage loads, and split-screen do not
  smear or extrapolate.
- 30 and 60 modes retain the current pacing behavior and output.
- The settings description accurately reflects the mode available in the
  current build.

## Verification Plan

Execute the purity, timing, camera, split-screen, and screenshot matrix already
specified in the uncapped-FPS design. Add measured 60/90/120/144 Hz present
tests, simulation-hash comparisons against capped 60, audio-cadence assertions,
and camera-cut/teleport visual captures on both render backends.

## Related Work

- [`UNCAPPED_FPS_PLAN.md`](../../design/UNCAPPED_FPS_PLAN.md) contains the
  implementation milestones and invariants for this work.
