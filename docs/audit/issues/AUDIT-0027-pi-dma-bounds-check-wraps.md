# AUDIT-0027: PI DMA Bounds Checks Wrap and Permit Out-of-Bounds ROM Reads

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S3 - integer overflow can turn an invalid DMA into an out-of-bounds read |
| Priority | P2 |
| Area | N64 platform stubs / ROM DMA |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Standardized from the prior monolithic audit and reconfirmed in current source |
| Affected configurations | Direct callers of `osPiStartDma` or `osPiRawStartDma` with invalid ranges |

## Resolution

Verified already fixed (commit `b76b8dc8`, 2026-07-13). `osPiStartDma`/`osPiRawStartDma` (stubs.c) use the non-wrapping predicate `size <= g_romSize && devAddr <= g_romSize - size` (size checked first, so no underflow), rejecting the `devAddr+size` overflow case before the memcpy. Status flipped by a verification sweep; the field was stale.

## Summary

Both direct PI DMA stubs validate a ROM range with `devAddr + size <=
g_romSize`. Because both operands are `u32`, the addition can wrap to a small
value and pass, after which `memcpy` reads from a pointer far beyond the loaded
ROM.

## Evidence

[`stubs.c`](../../../src/platform/stubs.c) uses the same condition in
`osPiStartDma` and `osPiRawStartDma`:

```c
devAddr + size <= g_romSize
```

The accepted branch reads `g_romData + devAddr` for `size` bytes. With a 12 MB
ROM, `devAddr=0xfffff000` and `size=0x2000` wrap the sum to `0x1000`, which is
less than `g_romSize`, while the resulting source pointer is invalid. Both
functions then return success; the queued variant also posts completion.

This is a latent robustness path: normal authored DMA offsets are expected to
be valid, and a natural retail-data trigger was not reproduced.

## Reproduction

Create a ROM-free unit fixture with a small allocated `g_romData`, then call
each function with:

```text
devAddr = 0xfffff000
size    = 0x00002000
```

Under ASan, the current accepted `memcpy` reports an invalid read or the process
faults, depending on the allocation layout.

## Root Cause

The range validation performs an overflow-prone end-address addition instead
of comparing the start and length with subtraction in a wider or nonwrapping
domain.

## Required End State

Reject a transfer unless the ROM and destination pointers are valid, the size
fits the ROM, and `devAddr <= g_romSize - size`. Invalid requests must perform
no copy and return a defined nonzero error. Completion-message behavior for a
rejected asynchronous request must be explicit and must not leave callers
hung.

## Acceptance Criteria

- Wrapped ranges are rejected before pointer arithmetic or `memcpy`.
- Exact-end and zero-length boundary behavior is defined and tested.
- A one-byte overrun is rejected without modifying the destination.
- Null ROM and destination pointers are rejected.
- Both PI entry points share the same range policy and error result.
- Valid audio-streaming and direct ROM transfers remain unchanged.

## Verification Plan

Add ASan-enabled ROM-free boundary tests for zero, exact end, one-past end,
wrapped end, null pointers, and an ordinary valid transfer through both APIs.
Confirm completion queue behavior separately for accepted and rejected
requests, then rerun an audio-streaming gameplay smoke.

## Related Work

- [`RENDERER_SIM_AUDIT_2026-07-06.md`](../../RENDERER_SIM_AUDIT_2026-07-06.md)
  originally recorded the two affected entry points as separate findings.
