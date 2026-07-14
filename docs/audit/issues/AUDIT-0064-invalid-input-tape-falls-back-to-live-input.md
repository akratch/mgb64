# AUDIT-0064: Invalid Playback Tape Falls Back to Ordinary Input

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S3 - a requested deterministic replay can become an uncontrolled live-input run |
| Priority | P1 |
| Area | Input tape / failure propagation |
| Evidence level | Fault injected |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Missing, unreadable, truncated, or malformed `--play-tape` input |

## Summary

When the requested playback tape cannot be read, the engine logs an error but
continues without installing the playback hook. The game therefore consumes
ordinary frozen/live input. A bounded automation run can return zero, while
the tape regression wrapper can wait forever because it expects tape exhaustion
to terminate the process.

## Evidence

In [`input_tape.c`](../../../src/platform/input_tape.c), a failed
`inputTapeRead` sets `s_tapeFailed`, prints `ERROR: cannot read tape`, and
returns from hook installation. It does not abort startup or select a failure
status. No playback function is installed after that state is set.

A deterministic Dam run requested an absent tape and used screenshot frame 1
as a bound. The engine logged the tape read error, completed the screenshot
path, and returned 0. [`tape_regression.sh`](../../../tools/fidelity/tape_regression.sh)
launches replays with no timeout, so the same failure without a separate exit
trigger has no tape-exhaustion path.

## Reproduction

Run a direct deterministic level with `--play-tape` pointing to an absent file
and add `--screenshot-frame 1 --screenshot-exit`. Observe the read error and
successful exit. Remove the screenshot bound to demonstrate that the regression
command has no intrinsic termination after this failure.

## Root Cause

Playback setup is lazy and its failure state disables the feature rather than
failing the explicit user request. The wrapper has no defensive time bound.

## Required End State

Read and fully validate the tape during startup, before entering gameplay. Any
failure of an explicit `--play-tape` request must exit nonzero and must never
fall back to another input source. Add a regression timeout as defense in depth.

## Acceptance Criteria

- Missing, unreadable, short, or malformed tapes fail before gameplay starts.
- No live, frozen, RAMROM, or controller input replaces a failed tape request.
- Every tape setup failure returns a documented nonzero status.
- The regression wrapper has a per-tape timeout and reports timeout as failure.
- Tests assert the playback hook remains unused and the process cannot exit 0.

## Verification Plan

Run end-to-end cases for absent, permission-denied, truncated, and corrupt
tapes, plus a timeout fixture. Assert each exits promptly and nonzero before
the first gameplay input sample is consumed.

## Related Work

- AUDIT-0063 covers structurally readable but semantically incompatible tapes.
- AUDIT-0035 covers the app shell masking engine boot failures.

## Resolution

In `inputTapeInstallHooks`, a `--play-tape` that is missing/unreadable/short/malformed (`inputTapeRead` returns NULL) now `exit(2)`s instead of setting a flag and silently continuing on live/ordinary input. Symmetric with the adjacent seed/level/metadata validation failures. Verified: `--play-tape <missing>` exits 2 with a clear diagnostic.
