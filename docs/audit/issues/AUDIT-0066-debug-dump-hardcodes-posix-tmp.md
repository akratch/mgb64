# AUDIT-0066: Guard Debug Dumps Hard-Code the POSIX `/tmp` Directory

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S4 - the advertised diagnostic is unavailable on standard Windows installs |
| Priority | P2 |
| Area | Diagnostics / cross-platform paths |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Windows native builds and hosts without a writable `/tmp` |

## Summary

The backtick guard-state dump always writes to `/tmp/ge007_dump_NNNN.txt`.
Standard Windows installations do not provide that POSIX directory, so the
same native feature that works on macOS and Linux fails at `fopen` on Windows.

## Evidence

[`debug_dump.c`](../../../src/platform/debug_dump.c) documents `/tmp` as the
destination and constructs that absolute path unconditionally. The input event
handler exposes the backtick trigger on every SDL platform, with no Windows
path branch and no use of the existing save-directory or SDL preference-path
abstractions.

This is source-proven; a natural Windows run was not available during this
audit pass.

## Reproduction

Run the Windows build on a normal machine without a manually created `/tmp`,
enter gameplay, and press backtick. The overlay reports `DUMP FAILED (fopen)`
and no diagnostic file is created.

## Root Cause

A developer-local POSIX path was embedded directly in a cross-platform runtime
tool instead of resolving an application-owned diagnostic directory.

## Required End State

Resolve dumps through a cross-platform, user-writable location such as the
active save directory or an SDL preference directory. Expose the resolved path
in the success message and allow an explicit diagnostic-directory override for
automation.

## Acceptance Criteria

- Backtick creates a dump on clean Windows, macOS, Linux, and PortMaster setups.
- The default location is user-writable and owned by the application.
- An optional override rejects invalid directories with a useful diagnostic.
- File names cannot collide silently across concurrent processes.
- Documentation and the overlay report the actual resolved path.

## Verification Plan

Add platform-path unit coverage and automated dump smokes on Windows and Linux.
Run from a read-only current directory to prove the selected default remains
writable and does not depend on POSIX filesystem layout.

## Related Work

- AUDIT-0067 covers false success after the dump file has opened.
- AUDIT-0047 covers incorrect path selection in diagnostics export.
