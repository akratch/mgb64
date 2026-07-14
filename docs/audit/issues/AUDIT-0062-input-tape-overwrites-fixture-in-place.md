# AUDIT-0062: Failed Input-Tape Finalization Destroys the Previous Fixture

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S3 - a failed rerecord can destroy a known-good regression fixture |
| Priority | P2 |
| Area | Input tape / atomic persistence |
| Evidence level | Fault injected |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Rerecording to an existing `.ge7tape` path |

## Summary

Tape finalization opens the destination itself with `wb`. That truncates an
existing fixture before the replacement header and records are known to be
durable. A short write or close failure leaves the old known-good tape replaced
by a partial new file.

## Evidence

[`input_tape.c`](../../../src/platform/input_tape.c) opens the final path
directly in `inputTapeWriterClose`, writes the header and every record to that
stream, and returns an error after a failed `fwrite` or `fclose`. It has no
sibling temporary file, sync step, validation pass, or atomic rename.

For fault injection, a 6,088-byte committed tape was copied to the requested
recording path. The game was run with a 512-byte file-size limit and the signal
ignored so stdio returned an error. The process logged a flush failure and
returned 0; the previous fixture had been truncated to 512 bytes.

## Reproduction

Copy a valid tape to a temporary fixture path, impose a file-size limit smaller
than a new recording, and rerecord to that same path. Compare the fixture before
and after the expected finalization failure.

## Root Cause

The writer treats the target path as a work file instead of committing a fully
written replacement transactionally.

## Required End State

Write a complete tape to a unique sibling temporary file, flush and sync it,
close it successfully, validate its header and record extent, and atomically
replace the destination. On any failure, remove only the temporary file and
leave the prior destination byte-identical.

## Acceptance Criteria

- Failed rerecording preserves the prior tape byte for byte.
- Successful replacement is atomic within the destination filesystem.
- Temporary files are cleaned after both success and failure.
- The final tape is read back and structurally validated before commit success.
- Unit tests cover write, flush, close, validation, and rename failures.

## Verification Plan

Seed a destination with a valid fixture, inject each persistence failure, and
assert its hash is unchanged. Also terminate a writer mid-finalization and
confirm the destination is either the complete old tape or complete new tape.

## Related Work

- AUDIT-0061 covers late failure and the incorrect process status.
- AUDIT-0049 covers non-atomic keyboard and gamepad binding saves.

## Resolution

`inputTapeWriterClose` no longer truncates the live destination in place. The writer holds a `<path>.tmp` handle (opened at `inputTapeWriterOpen`), writes header+records there, checks `ferror`/`fflush`/`fclose`, and only `rename`s it over the live file on full success; any failure `remove`s the temp and leaves the existing tape untouched. Mirrors the atomic temp+rename writer in `input_bindings.c` (AUDIT-0049) / `config_pc.c`. Covered by `tests/test_input_tape.c` (no leftover `.tmp`, byte-exact roundtrip).
