# AUDIT-0035: App Shell Converts Engine Boot Failures into Exit Status Zero

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - launch and validation failures are reported to callers as success |
| Priority | P1 |
| Area | App shell / process status propagation |
| Evidence level | Fault injected |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Interactive Play, app autoplay, and app-shell PortMaster boot paths |

## Summary

`mgb64_engine_boot` returns the engine's status, but the app's `play` lambda
discards it. Every caller then returns zero or exits the launcher loop normally.
Automation can therefore certify a boot that failed before loading a ROM, and
desktop launchers hide actionable engine initialization failures from the
parent process.

## Evidence

[`main_app.cpp`](../../../src/app/main_app.cpp) defines `play` without a return
value, calls `mgb64_engine_boot(&cfg)`, and ignores the result. The autoplay and
PortMaster branches call `play`, shut down, and `return 0`. Interactive Play
does the same through the final normal return.

[`main_pc.c`](../../../src/platform/main_pc.c) returns meaningful nonzero values
for invalid arguments, missing or invalid ROMs, initialization failures, and
runtime setup errors. The app-shell bridge returns that value correctly; only
the caller drops it.

Fault injection from an empty HOME and working directory produced:

```text
[GE007-PC] No GoldenEye ROM found.
```

The outer app process nevertheless exited with status 0.

## Reproduction

```sh
tmp=$(mktemp -d)
mkdir -p "$tmp/home" "$tmp/save"
cd "$tmp"
HOME="$tmp/home" MGB64_APP_AUTOPLAY=1 MGB64_APP_SAVEDIR="$tmp/save" \
  /path/to/ge007
echo "$?"
```

The current result is 0 after the engine's missing-ROM error.

## Root Cause

The app bridge was treated as a blocking UI action instead of a fallible
operation with a process-level contract. The autoplay smoke checks output
markers but has no lower-level assertion that the embedded engine status
survives the shell.

## Required End State

Return the exact engine exit status from autoplay and noninteractive handheld
boot paths. For interactive Play, retain the launcher and present the engine
failure with a diagnostic-log action, or exit nonzero if the application cannot
recover. A successful user quit remains zero.

## Acceptance Criteria

- Missing-ROM autoplay exits with the engine's nonzero status.
- Invalid boot arguments and renderer/audio initialization failures propagate.
- Successful bounded autoplay exits zero.
- Interactive boot failure is visible and cannot look like a clean game exit.
- Normal user quit after successful play remains zero.
- CI asserts both process status and its expected success marker.

## Verification Plan

Add an app-shell integration matrix for missing ROM, deliberately invalid ROM,
successful screenshot exit, and injected engine initialization failure. Assert
the outer process status equals `mgb64_engine_boot` and that interactive failure
returns to a usable launcher with the same diagnostic.

## Related Work

- AUDIT-0005 covers the separate invalid-ROM crash after weak CLI validation.
- AUDIT-0012 covers a sanitizer wrapper that independently ignores process
  failure.
