# AUDIT-0065: Tape Reader Allocates From an Unverified Header Count

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - a tiny malformed tape can request roughly 320 MiB before rejection |
| Priority | P2 |
| Area | Input tape / malformed-file robustness |
| Evidence level | Source and mechanism proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Reading a crafted or corrupted `.ge7tape`, especially memory-limited handhelds |

## Summary

The reader trusts the header's capped tick count for its large allocations
before checking whether the file contains those records. A 48-byte header can
claim the current maximum and trigger allocation attempts totaling about 320
MiB before the first record read proves the file is short.

## Evidence

[`input_tape.c`](../../../src/platform/input_tape.c) permits up to 16,777,216
ticks. It then allocates a 4-byte tick array and a 4-byte pad record for each of
up to four players before reading records. At the maximum stride this requests
67,108,864 bytes for ticks plus 268,435,456 bytes for pads. The code never
stats the file or verifies an exact overflow-safe expected length first.

After reading the declared records, it also returns success without checking
for trailing bytes, so file extent is not part of the format validation in
either direction.

## Reproduction

Create only a valid 48-byte header with `num_players=4` and
`tick_count=16777216`, then call `inputTapeRead` under a memory limiter or an
allocation tracer. It attempts the two large allocations before rejecting the
missing first record.

## Root Cause

The tick-count cap is treated as sufficient trust validation, and record extent
is discovered only through incremental `fread` after allocation.

## Required End State

Determine the regular file size first, compute the exact expected record extent
with overflow-safe arithmetic, and reject short or trailing data before any
count-derived allocation. Apply a practical byte/duration limit suitable for
supported targets or stream records with bounded memory.

## Acceptance Criteria

- A header-only maximum-count file is rejected without a large allocation.
- Expected-size arithmetic is checked for every multiplication and addition.
- Short files and unexpected trailing data are rejected explicitly.
- The accepted tape-size limit is documented and tested on handheld targets.
- Allocation-failure tests return a clean parse error without process failure.

## Verification Plan

Add synthetic headers around every size boundary, run under an allocator that
records requested bytes, and assert malformed small inputs remain within a
small constant memory bound. Fuzz the reader with allocation limits enabled.

## Related Work

- AUDIT-0063 covers validation of semantic header fields.
- AUDIT-0064 covers propagation of reader failure into engine status.
