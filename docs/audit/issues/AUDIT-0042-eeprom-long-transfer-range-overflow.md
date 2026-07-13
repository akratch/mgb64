# AUDIT-0042: EEPROM Long Transfers Use Overflow-Prone Range Arithmetic

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S3 - invalid transfer lengths can trigger undefined or out-of-bounds access |
| Priority | P2 |
| Area | N64 platform stubs / EEPROM API |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Invalid direct calls to `osEepromLongRead` or `osEepromLongWrite` |

## Resolution

Verified already fixed (commit `b76b8dc8`, 2026-07-13, with an explicit `[AUDIT-0042]` comment). `osEepromLongRead`/`osEepromLongWrite` (stubs.c) bound via subtraction — `if (nbytes > EEPROM_FILE_SIZE - offset) nbytes = ...` after an `offset >= EEPROM_FILE_SIZE` guard — eliminating the signed-overflow UB; null buffer / non-positive length copy nothing. Status flipped by a verification sweep.

## Summary

The EEPROM long-read and long-write stubs calculate `offset + nbytes` in signed
32-bit arithmetic before clamping. A very large positive `nbytes` can overflow
that addition, bypass the clamp, and reach `memcpy` with a huge length. Negative
lengths are silently accepted as successful no-ops.

## Evidence

[`stubs.c`](../../../src/platform/stubs.c) computes an offset from the `u8`
block address and then uses:

```c
if (offset + nbytes > EEPROM_FILE_SIZE)
    nbytes = EEPROM_FILE_SIZE - offset;
if (nbytes > 0 && buffer)
    memcpy(..., nbytes);
```

The addition is signed and can overflow, which is undefined behavior in C.
If it wraps below the file-size threshold, the positive original length is
converted to `size_t` for `memcpy`. Null buffers and negative lengths also
return zero rather than an invalid-argument result. Authored game calls are
expected to use valid block ranges; a natural gameplay trigger was not found.

## Reproduction

Call each long-transfer API in an ASan/UBSan ROM-free test with address 255 and
`nbytes=INT_MAX`. UBSan reports signed overflow or ASan reports an oversized
copy, depending on optimization. Repeat with a negative length and null buffer;
the current functions return success.

## Root Cause

The stub implemented permissive end clamping without first validating signed
inputs or using a nonwrapping range predicate.

## Required End State

Validate buffer, length, alignment requirements, and address range before any
addition or copy. Use subtraction or a wider unsigned domain for the remaining
capacity. Return a defined EEPROM/libultra-compatible error on invalid input
and leave memory and the persisted image unchanged.

## Acceptance Criteria

- `INT_MAX`, negative, null-buffer, and one-past-end requests are rejected
  without undefined behavior.
- Exact-end and ordinary multi-block transfers succeed.
- Invalid writes do not modify the in-memory image or save file.
- Read destinations remain unchanged after rejection.
- UBSan and ASan boundary tests are clean.
- Valid retail call behavior and status codes remain unchanged.

## Verification Plan

Add a table-driven ROM-free test across addresses 0, 254, and 255 with zero,
negative, exact, one-over, and `INT_MAX` lengths for read and write. Snapshot
both buffers and the file before each rejected case and run under ASan/UBSan.

## Related Work

- AUDIT-0027 covers the same range-validation class in PI ROM DMA.
- AUDIT-0041 covers truncated EEPROM file loading.
