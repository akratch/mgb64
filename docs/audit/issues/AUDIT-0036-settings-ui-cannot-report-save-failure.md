# AUDIT-0036: Settings UI Cannot Report Failed or Suppressed Persistence

| Field | Value |
| --- | --- |
| Status | Fixed (status line + Retry surfaced; fault-injected write + faithful-suppressed acceptance owner-verifiable) |
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

## Resolution

Fixed on `feat/webgpu-backend`. The tri-state save outcome
(`MgbConfigSaveResult`: OK / SUPPRESSED / FAILED; core landed 3cf15b8, with
`configStagingApply` already returning it) is now consumed and surfaced by the
settings UI instead of discarded.

- New pure slice `src/platform/save_status.{h,c}`: `saveStatusMessage` maps the
  tri-state to the exact status line ("Saved to ge007.ini" / "Applied to this run
  only — faithful/remaster session doesn't persist" / "Couldn't save — check
  permissions"), and `saveStatusIsFailure` flags ONLY FAILED (an intentional
  faithful-mode SUPPRESSED no-write is not an error). SDL/ImGui/engine-free.
- `src/app/ui_settings.cpp`: Apply consumes `configStagingApply()`'s result and
  Save Settings consumes `mgb_config_save_result()`; both render a concise status
  line under the buttons. FAILED is tinted and gets a **Retry** that re-invokes
  the save WITHOUT discarding staged edits (Apply already committed them to the
  live globals, so a plain re-save persists the current values). The status is
  cleared on the next staged edit / Cancel so it never reads stale.
- `src/app/ui_overlay.cpp`: the overlay-close auto-apply no longer silently
  swallows a failure — on FAILED it reports the result to the panel
  (`Settings_reportSaveResult`), keeps the overlay open on the Settings panel, and
  re-opens a staging session so the edits stay editable/retryable; OK and
  SUPPRESSED resume normally.

Pure logic guarded by the ROM/SDL/ImGui-free ctest `save_status`
(`tests/test_save_status.c`): pins all three message strings byte-for-byte and the
FAILED-only failure predicate (added TDD, RED→GREEN). `config_save_result` and
`config_staging` regression tests still green; full engine + app build links.

The interactive surfaces are left for **owner UI validation** (they need a
writable/unwritable save dir and a faithful session, not available headlessly):

- Owner-verifiable tail: (1) point the save dir at a read-only location, change a
  setting, press Apply/Save, and confirm the panel shows "Couldn't save — check
  permissions" with a Retry; make the dir writable and press Retry → "Saved to
  ge007.ini", edits intact. (2) With the overlay open on Settings over an
  unwritable dir, press Resume/close and confirm the overlay STAYS open on the
  error rather than silently resuming. (3) Launch `--faithful`, change a setting,
  Apply, and confirm the panel states it applies to this run only. (4) After
  showing a status, change any setting and confirm the status line clears.
