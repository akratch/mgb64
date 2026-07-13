# AUDIT-0022: Launcher Mode Overrides Survive Return-to-Launcher Re-exec

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - removed launcher choices remain active in later game sessions |
| Priority | P2 |
| Area | Launcher / process environment ownership |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | POSIX builds using the in-game Return to Launcher action |

## Summary

Launcher mode controls only add environment variables. They never remove a
launcher-owned variable when a toggle is re-enabled or an advanced override is
deleted. Return to Launcher uses `execvp`, so the replacement launcher inherits
those stale variables and silently reapplies behavior the UI no longer shows.

## Evidence

[`ui_modes.cpp`](../../../src/app/ui_modes.cpp) sets
`GE007_SHOOT_OUT_LIGHTS=0` and `GE007_AUTO_AIM=0` only when their toggles are
off. The on branches do not set or unset either variable. The same function
sets every current `GE007_*` line from the advanced text box but retains no set
of keys to remove when a line disappears.

[`ui_overlay.cpp`](../../../src/app/ui_overlay.cpp) implements Return to
Launcher with `execvp(g_argv0, argv)`. The process image is replaced, but its
environment is inherited. App configuration reloads the updated visible UI
state while the inherited engine override remains present.

## Reproduction

On a POSIX build:

1. Turn Shoot out lights off, launch the game, and choose Return to Launcher.
2. Turn Shoot out lights on and launch again.
3. Observe that the inherited `GE007_SHOOT_OUT_LIGHTS=0` still disables it.
4. Repeat by adding an advanced `GE007_*` line, returning, deleting the line,
   and launching again. The deleted override remains in the environment.

The same toggle sequence applies to auto-aim.

## Root Cause

The launcher mutates the process environment as an append-only global store.
It does not distinguish inherited external values from launcher-owned values,
and re-exec preserves the mutated store across launcher sessions.

## Required End State

Treat the current launcher state as authoritative for variables the launcher
owns. Track their original inherited values and the keys added by advanced
mode text, then restore or unset obsolete launcher-owned values before each
application. Preserve unrelated external environment variables and intentional
external values that the launcher never claimed.

## Acceptance Criteria

- Off, Return to Launcher, on produces the engine's on/default behavior.
- An advanced key removed after re-exec is absent from the next game process.
- An inherited external value is restored when the launcher stops overriding
  it instead of being unconditionally deleted.
- Repeated Play and Return to Launcher cycles are idempotent.
- Windows' quit-only behavior is unchanged.

## Verification Plan

Add a ROM-free environment ownership test around `applyModeEnv` covering
toggle off/on, advanced add/delete, original external-value restoration, and
two consecutive applications. Run one POSIX launcher re-exec smoke to confirm
that the child process receives exactly the second UI state.

## Related Work

- None.
