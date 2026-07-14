# AUDIT-0022: Launcher Mode Overrides Survive Return-to-Launcher Re-exec

| Field | Value |
| --- | --- |
| Status | Fixed (core; one re-exec smoke owner-verifiable) |
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

## Resolution

Fixed on `feat/webgpu-backend` by making `applyModeEnv` (src/app/ui_modes.cpp)
**authoritative** over the launcher-owned `GE007_*` keys instead of append-only,
so a re-enabled hatch or a deleted advanced override no longer survives the
`execvp` Return-to-Launcher re-exec (which inherits the environment).

- New `portableUnsetenv` companion to `portableSetenv` (unsetenv / `_putenv_s(k,"")`).
- Pure, SDL-/ImGui-free core `src/app/env_ownership.{h,cpp}` (`EnvOwnership`):
  - `hatchOps`: emits an explicit op every apply — OFF ⇒ set `"0"`, ON ⇒ **unset**
    so the engine default (ON) re-applies and any inherited value is cleared.
    `GE007_AUTO_AIM` is never set to a non-`"0"` value (in the engine that key is
    a scripted-input frame pattern, so `"1"` would forge input — unset is the only
    sim-safe "on").
  - `parseAdvanced`: mirrors the launcher's historical inline KEY=VALUE parse
    exactly (split on `\n`, trim, skip blank/`#`, require `=` and a `GE007_` prefix,
    last-writer-wins).
  - `reconcile`: against a persisted ownership record (AppConfig key
    `advanced_env_owned`), a key dropped from the advanced box is unset — or
    restored to the external value it displaced when first claimed — instead of
    left stale; the record is re-persisted each apply so the reconciliation
    survives the re-exec.
- The record is single-line (newlines escaped; `\x1e`/`\x1f` delimiters), so it
  round-trips through AppConfig's escaped save()/load() safely.

Guarded by the ROM-free/SDL-free ctest `env_ownership`
(tests/test_env_ownership.cpp): hatch ON⇒unset / OFF⇒`"0"`, advanced parse parity
(dup last-wins, indent trim, non-prefix/`=`-less drop), external-value
capture+restore, two-apply idempotency (record stable, original preserved across
a value change), and the encode/decode round-trip (absent-marker, value, control
bytes). The app links and the full-engine build is green; this is launcher/UI
state, disjoint from the hashed simulation. The one remaining acceptance item —
an end-to-end POSIX launcher re-exec smoke driving the graphical launcher — needs
an interactive UI session and is left for owner verification.
