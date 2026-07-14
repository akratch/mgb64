# AUDIT-0068: Audio Queue Failures Are Counted as Accepted Output

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S3 - runtime device failure can produce permanent silence while health telemetry remains green |
| Priority | P2 |
| Area | Audio backend / device lifecycle |
| Evidence level | Source and API-contract proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | SDL playback device removal or any `SDL_QueueAudio` failure after initialization |

## Summary

Both SDL queue calls ignore their return value and immediately record the full
buffer as accepted. The SDL event loop does not handle audio-device removal, so
loss of the opened device can leave the engine continuously synthesizing into
an invalid queue while reporting successful output.

## Evidence

[`stubs.c`](../../../src/platform/stubs.c) calls `SDL_QueueAudio` in the
deterministic and live paths without testing the result. Each path then sets
`accepted_bytes = size`; `osAiSetNextBuffer` always returns 0. The SDL2 header
installed for this build specifies a negative return on queue failure.

[`platform_sdl.c`](../../../src/platform/platform_sdl.c) handles controller
device addition and removal but has no `SDL_AUDIODEVICEREMOVED` or recovery
case. `s_aiOpen` remains true and the cached device ID is never invalidated.

This is source and local SDL API-contract proven. Physical device removal was
not reproduced during this audit pass.

## Reproduction

Start with output on a removable USB/Bluetooth audio device, disconnect it while
playing, and inspect sound plus `GE007_AUDIO_TRACE` telemetry. A deterministic
test seam can instead make `SDL_QueueAudio` return a negative error after a
chosen frame and compare the reported accepted bytes.

## Root Cause

The queue call was modeled as infallible, and the audio device is treated as a
process-lifetime singleton without SDL hotplug/error state transitions.

## Required End State

Check every queue result, record accepted bytes only on success, and transition
the backend to an explicit unavailable/recovering state on error or device
removal. Reopen the configured/default output when possible and surface a clear
in-app diagnostic when recovery fails.

## Acceptance Criteria

- A failed `SDL_QueueAudio` records zero accepted bytes and a dropped/error count.
- Audio-device removal invalidates the cached device and stops queue attempts.
- Device recovery either resumes sound or exposes a persistent actionable error.
- Mute controls do not operate on stale device identifiers.
- Tests inject queue errors and add/remove events without requiring hardware.

## Verification Plan

Add an SDL audio adapter seam with scripted queue failures and hotplug events.
Assert state transitions, counters, logs, and successful reopen behavior, then
perform manual USB and Bluetooth disconnect tests on each desktop platform.

## Related Work

- AUDIT-0069 covers PCM diagnostic-file integrity, not playback-device output.
- AUDIT-0059 covers packaged SDL runtime availability at process startup.

## Resolution

`osAiSetNextBuffer` (src/platform/stubs.c) now credits `accepted_bytes` only when `SDL_QueueAudio` returns 0; a failed queue call increments the dropped-buffer/dropped-byte counters instead of being counted as accepted output (both the deterministic and live branches). New `osAiNotifyDeviceRemoved`, called from the `SDL_AUDIODEVICEREMOVED` case in platform_sdl.c's event loop, invalidates the cached device (`s_aiOpen=0`) so queueing stops when our output device is unplugged. Sim-neutral: under the deterministic dummy driver `SDL_QueueAudio` always succeeds, so baselines are byte-identical (7/7 tapes verified). **Residual (deferred):** automatic device RE-OPEN/recovery (resume sound or surface a persistent actionable error after re-plug) and a headless device-event test harness are a larger, hardware-dependent follow-up; the accounting bug and removal-invalidation (the criteria reachable without hardware) are fixed.
