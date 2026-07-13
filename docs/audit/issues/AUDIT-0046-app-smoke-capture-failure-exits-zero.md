# AUDIT-0046: App Smoke Capture Failure Still Exits Successfully

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S4 - launcher visual validation succeeds without its requested image |
| Priority | P2 |
| Area | App shell / UI smoke capture |
| Evidence level | Fault injected |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | `MGB64_APP_SMOKE_SHOT` when the BMP cannot be written |

## Summary

The app-shell smoke lane treats its optional requested BMP as best effort. Its
writer returns void, ignores all write and close results after open, and the
smoke path always prints its rendered-frame success marker and returns zero.
A design-review capture can therefore pass while absent or truncated.

## Evidence

[`app_host.cpp`](../../../src/app/app_host.cpp) returns early on a failed BMP
open, but `writeBackbufferBmp` has no result. Header and row `fwrite` calls and
`fclose` are unchecked, followed unconditionally by a `wrote` log after a
successful open.

[`main_app.cpp`](../../../src/app/main_app.cpp) calls `host.endFrame(shot)`, then
always prints `smoke: rendered N frames` and returns zero.

Fault injection requested a shot in a read-only directory. The process logged
`could not open .../shot.bmp`, printed the smoke success marker, exited zero,
and produced no image.

## Reproduction

```sh
MGB64_APP_SMOKE_FRAMES=1 \
MGB64_APP_SMOKE_SHOT=/read-only/path/shot.bmp ge007
echo "$?"
```

The current result is zero and no BMP exists.

## Root Cause

The smoke marker validates UI frame execution only, while the optional capture
was added through a manual-style void writer without defining whether requesting
an artifact makes it mandatory.

## Required End State

When `MGB64_APP_SMOKE_SHOT` is set, make successful finalization of a valid BMP
part of the smoke contract. Propagate allocation, readback, open, write, and
close results through `endFrame` to `main`, remove partial output, and exit
nonzero on failure.

## Acceptance Criteria

- Requested-shot open, write, and close failures make the app smoke nonzero.
- No success marker claims the requested artifact was produced after failure.
- A successful capture produces a valid, nonempty BMP and exits zero.
- A smoke with no shot requested retains its current frame-only success rule.
- Partial files are removed.
- Release/design tooling checks status and BMP validity when it requests a shot.

## Verification Plan

Add app smoke tests for no shot, successful shot, unwritable path, injected
short write, and close failure. Check exit code, marker wording, file presence,
header, dimensions, and cleanup.

## Related Work

- AUDIT-0043 covers the separate engine screenshot-exit writer and status path.
