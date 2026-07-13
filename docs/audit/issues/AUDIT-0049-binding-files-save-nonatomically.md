# AUDIT-0049: Keyboard and Gamepad Binding Files Save Non-Atomically

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S4 - rebinding state can be lost or partially persisted without UI feedback |
| Priority | P2 |
| Area | Input bindings / persistence |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Keyboard or gamepad binding saves interrupted or denied by the filesystem |

## Resolution

Fixed 2026-07-13. `inputBindingSave`/`gamepadBindingSave` (input_bindings.c) previously `fopen(path,"w")` truncated the live file, wrote via unchecked `fprintf`, and `fclose`d unchecked — a crash or full disk mid-write left the bindings truncated/lost. Both now route through a shared `bindings_atomic_write` helper: write to `<file>.tmp`, verify the stream (`ferror`/`fflush`), check `fclose`, then `rename` over the live file — the temp+rename pattern already proven in `configSave`/`eeprom_save_to_file`. On any failure the live file is left intact and the temp removed. Builds clean; the binding-file format and load path are unchanged.

## Summary

Both binding writers truncate their live INI files in place, ignore every write
and close result, and return void. A crash, full disk, short write, or close
failure can replace valid bindings with a partial file while the Controls panel
commits the capture as successful.

## Evidence

[`input_bindings.c`](../../../src/platform/input_bindings.c) opens
`ge007_bindings.ini` and `ge007_gp_bindings.ini` with mode `w`, writes a header
and entries using unchecked `fprintf`, calls unchecked `fclose`, and returns no
status. It logs only an initial `fopen` failure.

[`ui_bindings.cpp`](../../../src/app/ui_bindings.cpp) saves immediately after
every capture and defaults reset, then ends capture with no persistence result
or retry state. There are no binding persistence tests in the current CTest
surface.

## Reproduction

Start with valid custom bindings and inject a write failure after either live
file has been opened and truncated. Restart the app. The loader reads the
surviving prefix and defaults the missing entries, while the prior UI session
gave no failure indication.

## Root Cause

The binding stores implemented path routing and initial-open diagnostics but
did not adopt the atomic replacement or fallible UI contracts used by more
robust persistence paths.

## Required End State

Serialize each binding set to a sibling temporary file, validate all writes and
close, and atomically replace the live file only after complete success. Return
a result to Controls, keep the in-memory edit available, and support retry
without ending capture as durably committed.

## Acceptance Criteria

- Open, write, close, and replace failures leave the prior binding file intact.
- Controls visibly reports non-persistence and can retry.
- Keyboard and gamepad files use the same atomic writer policy.
- Successful saves round-trip every action exactly.
- Temporary files are removed after failure.
- Legacy keyboard-file migration remains supported and becomes durable only
  after a verified atomic save.

## Verification Plan

Add ROM-free persistence tests with injected open, partial-write, close, and
replace failures for both stores. Assert old-file preservation, in-memory state,
UI result, retry, temp cleanup, and exact successful reload.

## Related Work

- AUDIT-0039 covers the equivalent failure class in launcher app preferences.
- AUDIT-0036 covers engine settings persistence feedback.
