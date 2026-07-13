# AUDIT-0073: Native Mission-Music Initializer Is Bypassed by a Stale Guard

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - the retail mission-music state machine is never initialized on the native port, degrading dynamic background/X-track behavior |
| Priority | P2 |
| Area | Audio / mission-music state machine (native port fidelity) |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Native port, solo missions that use dynamic background and X-track music |

## Summary

The mission-music state-machine initializer returns early on the native build,
under a comment that the music subsystem is not initialized. The subsystem is in
fact active (stage tracks decompress and play through the native sequence
players), so the guard is stale: it seeds mission state 0 and skips the retail
stop-and-start transitions that begin and later restore the mission score.

## Evidence

In [`mp_music.c`](../../../src/game/mp_music.c) the initializer
`sub_GAME_7F0C11FC` opens with an `#ifdef NATIVE_PORT` block that sets
`mission_state = MISSION_STATE_0`, records `stageMusicID`, and returns. The
comment justifies this as skipping music because the "music subsystem not
initialized". Behind the guard, the retail body calls `musicTrack1Stop`,
`musicTrack2Stop`, `musicTrack3Stop`, and then `set_missionstate(MISSION_STATE_1)`
or `set_missionstate(MISSION_STATE_4)` depending on
`musicGetBgTrackForStage(stageMusicID)`. Those `set_missionstate` calls are the
transitions that actually start the mission score; the native early return never
reaches them, so the machine is left in state 0.

That the subsystem is initialized is corroborated by AUDIT-0071 and AUDIT-0072,
whose runtime evidence shows `musicTrack1Play` decompressing and parsing tracks
on the native build. `set_missionstate` (same file) only performs its
start/restore work on specific transitions out of the current state, so a machine
pinned at state 0 does not receive the intended background/X-track transitions.

A native music-trace run over a scripted watch open/close sequence observed the
mission track not being restored after the watch closed. That runtime observation
is consistent with the source, but a natural (non-scripted) gameplay
confirmation of the watch-restore silence has not yet been recorded.

## Reproduction

Read `sub_GAME_7F0C11FC` in `mp_music.c`: the `#ifdef NATIVE_PORT` early return
skips the stop calls and the `set_missionstate` start transitions. For the
runtime signal, boot a stage-music level and capture the music trace across a
watch open then close; note whether the mission track resumes after the watch
closes.

## Root Cause

A stub guard that was correct while the native audio backend was inert was never
removed after the sequence engine became functional. The initializer therefore
skips the state-machine setup the rest of the music code assumes has run.

## Required End State

On the native port the mission-music initializer must perform the same state
setup as retail: stop any active tracks and transition into the correct starting
mission state so dynamic background/X-track behavior and watch-restore work. If a
specific sub-behavior is still unsupported, gate only that part explicitly rather
than short-circuiting the whole initializer under a stale premise.

## Acceptance Criteria

- `sub_GAME_7F0C11FC` drives the mission state into its retail starting state on
  the native port instead of returning at state 0.
- The stale "subsystem not initialized" comment is removed or corrected.
- Closing the watch restores the mission track on a stage that has one.
- Dynamic background/X-track transitions occur where retail produces them.

## Verification Plan

Trace mission state and track activity across level start, watch open, watch
close, and objective-driven transitions on both the native port and the retail
reference; assert the native transitions match. Confirm no regression in stages
without dynamic music.

## Related Work

- AUDIT-0071 covers the ignored decompression status in the music loader.
- AUDIT-0072 covers the sequence-table over-read in the same loader.
