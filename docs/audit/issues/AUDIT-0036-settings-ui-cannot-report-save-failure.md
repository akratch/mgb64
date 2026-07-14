# AUDIT-0036: Settings UI Cannot Report Failed or Suppressed Persistence

| Field | Value |
| --- | --- |
| Status | Deferred |
| Severity | S4 - settings presented as saved can be lost at the next launch |
| Priority | P2 |
| Area | Settings UI / configuration persistence |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Unwritable save locations and read-only faithful sessions |

## Summary

The engine configuration writer detects open, close, and atomic-replace
failures, but none of that status reaches the settings UI. Apply always reports
an applied result, Save Settings ignores its integer return, and closing the
overlay auto-applies without feedback. Read-only faithful sessions are also
reported as success even though persistence is intentionally suppressed.

## Evidence

[`config_pc.c`](../../../src/platform/config_pc.c) returns zero on write or
replace failure and logs the error. It returns one when saving is suppressed by
a faithful session, making an intentional no-write indistinguishable from a
successful write at the current API boundary.

[`config_schema.c`](../../../src/platform/config_schema.c) exposes
`mgb_config_save` as an integer, but `configStagingApply` returns void and drops
`configSave()` entirely. [`ui_settings.cpp`](../../../src/app/ui_settings.cpp)
sets `SETTINGS_APPLIED` after every Apply and ignores the result of the separate
Save Settings button. [`ui_overlay.cpp`](../../../src/app/ui_overlay.cpp)
silently applies staged changes when closing the overlay.

## Reproduction

Point the engine save directory at a location where a temporary file cannot be
created or atomically renamed, then change a setting and press Apply. The log
contains `[CONFIG] Failed to save`, but the UI closes the transaction as
applied. Restart and observe the old value. A faithful launch provides the
suppressed case without filesystem failure.

## Root Cause

The configuration core uses a boolean that conflates saved and intentionally
suppressed outcomes, while the staged and UI layers were designed as
fire-and-forget calls.

## Required End State

Propagate a structured result such as Saved, AppliedButReadOnly, and Failed
through direct and staged saves. Keep live in-memory application separate from
durable persistence. Show a concise actionable state and retain retry or cancel
controls when persistence fails.

## Acceptance Criteria

- A write, close, or replace failure is visible in the settings surface.
- Apply distinguishes live application from durable save success.
- Faithful read-only sessions state that changes apply only to the current run.
- Closing the overlay cannot silently discard a persistence failure.
- Retry succeeds after the directory becomes writable without losing edits.
- CLI `--config-set` retains its current nonzero failure behavior.

## Verification Plan

Add fault-injected config writer tests for open, short-write/close, and replace
failure, then exercise direct Save, staged Apply, and overlay-close paths. Test
the faithful suppressed state separately and verify UI result text and retry
behavior.

## Related Work

- AUDIT-0026 covers value truncation before this persistence boundary.

## Deferral (verify-before-fixing triage 2026-07-14) <!-- triage-2026-07-14 -->

Lower-priority UI-feedback polish (S4). The engine save path already reports success/failure correctly (configSave returns 0 on open/close/replace failure) and honors configSetSaveSuppressed for read-only --faithful/--remaster sessions; the gap is purely surfacing saved-vs-suppressed-vs-failed in the ImGui settings panel, which requires interactive UI validation not available headlessly. Deferred behind the higher-severity correctness/robustness backlog.
