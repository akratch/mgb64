# AUDIT-0061: Input-Tape Recording Reports Success After Late Output Failure

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - a requested regression fixture can be absent while the recording run is green |
| Priority | P1 |
| Area | Input tape / recording durability |
| Evidence level | Fault injected |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Native direct-level runs using `--record-tape` |

## Summary

The input-tape writer does not open its destination when recording starts. It
buffers the whole session in memory, first opens the file from an `atexit`
handler, and can only print an error if finalization fails. The process still
exits zero, so automation can accept a run that produced no usable tape.

## Evidence

[`input_tape.c`](../../../src/platform/input_tape.c) makes
`inputTapeWriterOpen` allocate a writer and copy the path without opening it.
`inputTapeWriterClose` performs the first `fopen(..., "wb")`. The engine registers
`inputTapeFinishRecording` with `atexit`; that handler logs a close failure but
has no mechanism to change the already-selected process status.

Fault injection used a valid deterministic Dam boot with a recording path under
a nonexistent `/dev/null` child and an automatic screenshot exit. The log said
`recording -> ...`, later said `ERROR: failed to flush recording`, created no
tape, and the process returned 0.

[`tape_regression.sh`](../../../tools/fidelity/tape_regression.sh) trusts the
record process status and immediately prints that the tape was recorded. It
does not require a nonempty, readable tape before returning success.

## Reproduction

Run a deterministic direct-level capture with
`--record-tape /dev/null/impossible.ge7tape` and any automatic exit trigger.
Observe the input-tape flush error, exit status 0, and missing output file.

## Root Cause

Destination validation and all physical I/O are deferred to process teardown,
after normal exit status has already been selected. The regression wrapper
checks only that status rather than validating the promised artifact.

## Required End State

Validate and reserve the destination before recording begins, finalize the tape
through an explicit fallible shutdown step before choosing process status, and
return nonzero whenever the requested artifact cannot be committed. Recording
automation must independently open and validate the resulting tape.

## Acceptance Criteria

- An invalid or unwritable recording destination fails before gameplay starts.
- Append, write, flush, sync, close, and commit failures make the run nonzero.
- A success log is emitted only after a readable tape has been committed.
- `tape_regression.sh --record` rejects a missing, empty, or unreadable tape.
- A regression test injects a finalization failure and asserts nonzero status.

## Verification Plan

Add writer tests for invalid parents and write/close failure, then an engine
smoke that records to a constrained destination. Parse the completed tape with
`inputTapeRead` before allowing the wrapper to report success.

## Related Work

- AUDIT-0062 covers destruction of a pre-existing tape during finalization.
- AUDIT-0043 covers the equivalent false-success contract for screenshots.
