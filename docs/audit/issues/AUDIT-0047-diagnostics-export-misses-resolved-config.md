# AUDIT-0047: Diagnostics Export Ignores the Resolved Engine Config Path

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S4 - support bundle can silently omit the configuration needed to diagnose it |
| Priority | P2 |
| Area | App shell / diagnostics export |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Engine config stored outside the launch working directory |

## Summary

The diagnostics panel claims to export `ge007.ini`, but it only looks for that
filename in the current working directory. The engine resolves configuration
through its smart save-directory policy, so packaged macOS/Linux launches and
explicit save directories commonly store the real file elsewhere. Export
silently skips it and still opens the bundle as if successful.

## Evidence

[`ui_diag.cpp`](../../../src/app/ui_diag.cpp) copies engine configuration with:

```cpp
copyIfExists("ge007.ini", "ge007.ini");
```

It does not call `savedirPath`, query a config path API, or inspect the active
engine configuration source. Its `copyIfExists` lambda and directory creation
also ignore `std::error_code`, and `exportDiagnostics` always returns the output
path.

[`savedir.c`](../../../src/platform/savedir.c) may place files in an explicit
`--savedir`, existing portable directory, Windows AppData, or `$HOME/.ge007`
depending on the runtime. The UI text nevertheless promises that export writes
`mgb64.log + ge007.ini + sysinfo`.

## Reproduction

Launch with the engine config under `$HOME/.ge007` or an explicit directory and
no `ge007.ini` in the current working directory. Click Export Diagnostics and
inspect `mgb64-diagnostics`. The real config is absent, no failure is shown,
and the folder still opens.

## Root Cause

The app diagnostics layer guessed a relative engine filename instead of using
the save-directory owner as the source of truth. Export was implemented as a
best-effort copier without an artifact manifest or result contract.

## Required End State

Expose the active engine config path through a stable API and copy exactly that
file. Return per-artifact results, show incomplete export visibly, and include a
small manifest distinguishing absent, copied, redacted, and failed items.

## Acceptance Criteria

- Export copies the config actually loaded by the current process.
- CWD, per-user, portable, and explicit-save-directory modes are covered.
- A missing or failed config copy makes the export visibly incomplete.
- The bundle manifest records each expected artifact and source category
  without exposing unnecessary private paths.
- The UI claim matches the produced files.
- Export directory creation and sysinfo write failures are also reported.

## Verification Plan

Add integration fixtures for every savedir policy with decoy CWD configs and
distinct active values. Export each bundle and assert it contains the active
file, never the decoy, plus correct manifest outcomes for missing and denied
files.

## Related Work

- AUDIT-0033 and AUDIT-0034 cover incorrect save-directory selection before
  export.
- AUDIT-0048 covers path redaction inside the same bundle.
