# AUDIT-0063: Input-Tape Playback Accepts Incompatible Session Metadata

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - a replay can run under session semantics different from its recorded header |
| Priority | P2 |
| Area | Input tape / replay validity |
| Evidence level | Runtime reproduced |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Native `--play-tape` deterministic runs |

## Summary

The tape format records difficulty, tick rate, flags, player stride, and
controller count, but playback does not validate most of those fields against
the requested session or supported format. A tape labeled for a different
difficulty can replay to completion and exit zero under Agent.

## Evidence

[`input_tape.h`](../../../src/platform/input_tape.h) defines `difficulty`,
`tick_hz`, `flags`, `num_players`, and `controller_count` as fixed header
semantics. [`input_tape.c`](../../../src/platform/input_tape.c) validates magic,
version, player-stride range, and a tick-count cap while reading. Engine glue
then checks deterministic seed and level only before passing an unchecked
`controller_count` cast to `joySetPlaybackFunc`.

A copy of `dam_combat_guard6.ge7tape` was changed only at header byte offset 16
so its difficulty became 2. It was launched as Dam Agent with the normal tape
regression determinism envelope. The log identified `diff=2`, consumed all 302
ticks, printed `playback complete`, and returned 0.

## Reproduction

Copy a valid Dam Agent tape, write little-endian value 2 to its difficulty
field, and replay it with `--level dam --difficulty agent --deterministic` plus
the standard stable-count environment. Playback accepts the mismatch.

## Root Cause

Header fields were serialized as provenance but never turned into a complete
compatibility contract at the reader/engine boundary.

## Required End State

Before installing the playback hook, validate every semantic header field:
requested level and difficulty, supported tick rate, known flags, nonzero and
bounded player/controller counts, and their cross-field consistency. Refuse
unknown or incompatible semantics with a diagnostic and nonzero status.

## Acceptance Criteria

- Difficulty mismatch exits nonzero before the game loop consumes tape input.
- Tick rates other than `GE7TAPE_TICK_HZ` are rejected.
- Unknown flag bits are rejected until their semantics are implemented.
- Controller count is range-checked and consistent with the playback model.
- Empty or internally inconsistent tapes cannot produce a green replay.
- Unit tests mutate every header field and assert the documented outcome.

## Verification Plan

Build a table-driven mutation suite around a valid fixture and exercise both
the pure reader and engine preflight. Keep one end-to-end mismatch smoke to
prove no playback hook is installed on rejection.

## Related Work

- AUDIT-0064 covers unreadable tapes falling back to ordinary input.
- AUDIT-0065 covers allocation before validating the file extent.
