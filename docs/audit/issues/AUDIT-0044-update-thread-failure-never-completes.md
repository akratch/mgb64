# AUDIT-0044: Update Thread Creation Failure Never Completes the UI State

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S4 - a recoverable launcher service failure leaves permanent busy state |
| Priority | P3 |
| Area | App shell / update check |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Sessions where `SDL_CreateThread` fails |

## Summary

If the background update thread cannot be created, the update subsystem logs
the SDL error but never marks itself done. The About panel consequently displays
`Checking for updates...` for the rest of the launcher session even though no
worker exists and no retry is possible.

## Evidence

[`update_check.cpp`](../../../src/app/update_check.cpp) sets `s_started=1`
before `SDL_CreateThread`. The success branch detaches the worker, whose final
action sets `s_done=1`. The failure branch only prints
`SDL_CreateThread failed`; it leaves `s_done=0` and `s_started=1`.

[`ui_launcher.cpp`](../../../src/app/ui_launcher.cpp) renders Checking whenever
`UpdateCheck_isDone()` is false. Because `s_started` prevents another start,
the state cannot recover during that process.

## Reproduction

Inject `SDL_CreateThread` failure in an app-shell test and call
`UpdateCheck_start`. Verify that the error is logged, then query
`UpdateCheck_isDone` and draw the About panel. It remains false/Checking
indefinitely.

## Root Cause

Only the worker owns the normal completion transition, and the synchronous
thread-creation failure path omitted the equivalent terminal state.

## Required End State

Treat thread-creation failure as a completed unsuccessful check. Publish the
terminal state before returning so the UI can show the existing unavailable
message. Optionally allow an explicit bounded retry, but never retain a busy
state without live work.

## Acceptance Criteria

- Thread-creation failure sets the update check to done and unsuccessful.
- The About panel shows an unavailable/error state, not Checking.
- The failure is still logged with the SDL reason.
- A successful worker retains existing banner and up-to-date behavior.
- Automation and disabled-check terminal states remain unchanged.
- Any retry policy cannot create concurrent workers.

## Verification Plan

Add a thread-factory seam and test success, network failure, disabled,
automation, and create-failure state transitions. Assert `started`, `done`,
`didCheck`, banner output, and displayed status for each path.

## Related Work

- None.

## Resolution <!-- triage-2026-07-14 -->

Verified already fixed in commit `6c1a23d` (ledger Status was stale). The update-thread create-failure branch now publishes the terminal UI state (SDL_AtomicSet(s_done,1)) so the UI leaves 'Checking…' (update_check.cpp:246).
