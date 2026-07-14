# AUDIT-0067: Guard Debug Dump Falsely Confirms a Truncated File

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S4 - a diagnostic collected during failure analysis can be incomplete despite a success message |
| Priority | P2 |
| Area | Diagnostics / output integrity |
| Evidence level | Fault injected |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Guard dumps under disk-full, quota, disconnect, or late I/O failure |

## Summary

After a dump file opens, every `fprintf` result and the final `fclose` result are
ignored. The engine always prints `Wrote`, shows a saved overlay, and changes
the window title even when only a prefix reached disk.

## Evidence

[`debug_dump.c`](../../../src/platform/debug_dump.c) checks only `fopen`. It
performs the complete dump through unchecked `fprintf` calls, ignores the return
from `fclose`, and unconditionally announces success afterward.

Fault injection ran an automatic dump with a 512-byte file-size limit while
ignoring the file-size signal so stdio reported errors. The process returned 0
and logged `Wrote /tmp/ge007_dump_0000.txt at frame 1`; the resulting dump was
exactly 512 bytes and lacked its normal body and end marker.

## Reproduction

Set `GE007_AUTO_DEBUG_DUMP_FRAME=1`, impose a file-size limit smaller than a
normal dump, ignore the limit signal, and run a bounded Dam boot. Compare the
success log and overlay with the truncated file and absent `=== END DUMP ===`.

## Root Cause

The diagnostic UI models successful open as successful capture and never
propagates stream errors accumulated during buffered output or close.

## Required End State

Track every write, flush, and close result. Announce success only after the
complete dump has been durably closed; otherwise remove the partial file and
surface a failure state that includes the path and operating-system error.

## Acceptance Criteria

- Short write, flush, and close failures never produce a saved-success overlay.
- Partial dumps are removed or explicitly renamed and labeled incomplete.
- A successful dump ends with the expected marker and passes a size sanity check.
- Automation can observe dump failure through a status or result API.
- Fault-injection tests cover buffered errors that appear only during close.

## Verification Plan

Wrap the output stream behind an injectable writer and fail at multiple offsets,
including final close. Retain an end-to-end constrained-file smoke and assert
the UI, log, file cleanup, and automation result agree.

## Related Work

- AUDIT-0066 covers the Windows-incompatible default path.
- AUDIT-0069 covers the same false-completion pattern in PCM capture.

## Resolution

`debugDumpExecute` now checks `ferror(fp)` (accumulated buffered-write failures) and the `fclose` return before reporting success. On failure it deletes the partial file (`remove`), logs the errno to stderr, sets a "DUMP FAILED (write/close)" overlay + window title, and returns WITHOUT the "DUMP SAVED" confirmation. A truncated/failed dump is no longer confirmed as complete.
