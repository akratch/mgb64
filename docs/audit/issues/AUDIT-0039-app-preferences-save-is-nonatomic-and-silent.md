# AUDIT-0039: App Preferences Save Is Non-Atomic and Silently Fallible

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S4 - launcher preferences can be partially lost without feedback |
| Priority | P2 |
| Area | App shell / launcher preference persistence |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Launcher preference writes interrupted or denied by the filesystem |

## Summary

Launcher preferences are written by truncating `mgb64_app.ini` in place.
`AppConfig::save` returns void, silently returns when opening fails, and never
checks write or close state. A crash, full disk, permission failure, or short
write can destroy a previously valid preference file while every caller assumes
success.

## Evidence

[`app_config.cpp`](../../../src/app/app_config.cpp) constructs
`std::ofstream f(prefsFilePath())`, which truncates the destination, then writes
the header and map directly. It only tests the initial stream open. There is no
temporary file, flush/close validation, atomic replace, backup, or status
return. If `SDL_GetPrefPath` fails, the empty base also silently falls back to a
relative `mgb64_app.ini` in the working directory.

Callers in [`ui_rom.cpp`](../../../src/app/ui_rom.cpp),
[`ui_launch.cpp`](../../../src/app/ui_launch.cpp), and
[`ui_modes.cpp`](../../../src/app/ui_modes.cpp) save immediately after edits but
cannot display failure because the API is void. Existing tests exercise value
escaping only; they do not execute filesystem persistence.

## Reproduction

Start with a valid app preference file, then fault inject a write failure after
the destination has been opened and truncated. Restart the launcher. The prior
preferences are missing or partial, and no save failure was reported. An
unwritable preference directory demonstrates the silent initial-open path.

## Root Cause

The small launcher store implemented format correctness but not the atomic
writer and error contract already present in the engine's `ge007.ini` writer.

## Required End State

Serialize to a sibling temporary file, verify every write and close, and
atomically replace the live preferences only after success. Return a structured
result to callers and surface failures without discarding the in-memory edits.
Treat preference-directory resolution failure as an error, not an implicit CWD
policy.

## Acceptance Criteria

- A failed open, write, close, or replace leaves the previous file intact.
- Every save caller can observe failure and offer retry or a diagnostic path.
- Successful replacement is atomic on Windows, macOS, and Linux.
- A failed `SDL_GetPrefPath` cannot write preferences to an unexpected CWD.
- Escaping and multiline advanced values retain their current round trip.
- Filesystem fault cases are covered beyond the existing codec-only test.

## Verification Plan

Refactor path and file operations behind a test seam, then inject failures at
open, partial write, flush/close, and replace. Verify old-file preservation,
temporary cleanup, exact successful content, caller status, and platform replace
semantics.

## Related Work

- AUDIT-0036 covers feedback for the separate engine `ge007.ini` writer.
