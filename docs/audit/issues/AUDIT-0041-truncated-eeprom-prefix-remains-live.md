# AUDIT-0041: Truncated EEPROM Loads a Partial Prefix Despite Claiming Blank State

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S3 - corrupt save data is partially activated instead of rejected |
| Priority | P1 |
| Area | Save persistence / EEPROM recovery |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Any `ge007_eeprom.bin` shorter than 2048 bytes |

## Resolution

Fixed 2026-07-13. `eeprom_load_from_file` (stubs.c) on a short read now re-`memset`s the buffer to zero — the pre-read memset only zeroed the untouched suffix, so `fread` left a PARTIAL real prefix live that would be written back over the original by `eeprom_save_to_file`. It also distinguishes `ferror` from a truncated file in the warning and renames the original to `ge007_eeprom.bin.corrupt` (best-effort) so a later save can't silently overwrite a recoverable file. Validated on a boot-to-menu with a 10-byte eeprom: warns `read 10 of 2048`, preserves `.corrupt`, and the game writes a fresh valid 2048-byte EEPROM (no stale prefix).

## Summary

When an EEPROM file is truncated, the loader warns that it is treating the
save as blank. In reality, it zeroes the destination before reading and then
leaves every successfully read prefix byte in place. The game receives a hybrid
of corrupt persisted data and zero-filled tail under a misleading recovery log.

## Evidence

[`stubs.c`](../../../src/platform/stubs.c) initializes the 2048-byte EEPROM
buffer with `memset`, then calls `fread` directly into it. If the count is short,
it logs:

```text
treating as blank
```

There is no second `memset`, error return, quarantine, or backup restore after
the short-read branch. `osEepromLongRead` subsequently copies the partially
filled global buffer and returns success.

## Reproduction

Create `ge007_eeprom.bin` containing a recognizable nonzero 16-byte prefix and
no remaining bytes. Initialize EEPROM and read the first blocks through
`osEepromLongRead`. The warning says blank, but the returned prefix matches the
file while later bytes are zero.

## Root Cause

The pre-read zero initialization was mistaken for post-failure rollback. Once
`fread` modifies the buffer, a short read must explicitly clear or replace it.

## Required End State

Apply one documented corruption policy atomically: reject and expose a fully
blank EEPROM, recover a verified backup, or stop with an actionable recovery
prompt. Never expose a partially loaded image while claiming a different state.
Preserve the corrupt file for diagnosis unless the user explicitly replaces it.

## Acceptance Criteria

- A short EEPROM file never yields a mix of file prefix and zero tail.
- The log precisely describes the state presented to the game.
- Full 2048-byte files load unchanged.
- Missing-file first launch still produces a blank EEPROM.
- Read I/O errors are distinguished from an ordinary missing file.
- A corrupt original is not silently overwritten before recovery is possible.

## Verification Plan

Add ROM-free EEPROM fixtures for lengths 0, 1, 16, 2047, 2048, and 2049 plus
an injected read error. Assert the returned image, status/log contract, corrupt
file preservation, and subsequent successful save behavior.

## Related Work

- AUDIT-0033 and AUDIT-0034 cover incorrect save locations rather than corrupt
  save contents.
