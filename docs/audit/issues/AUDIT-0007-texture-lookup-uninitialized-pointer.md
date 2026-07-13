# AUDIT-0007: Texture Lookup Decode Advances an Uninitialized Source Pointer

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - undefined behavior on a normal asset-load path |
| Priority | P2 |
| Area | Assets / texture decompression |
| Evidence level | Shipped data reachable; analyzer and source proven |
| Confidence | High |
| Origin | Newly confirmed on 2026-07-12 |
| Affected configurations | Native texture loading for Huffman+lookup and RLE+lookup images |

## Summary

`texInflateLookupFromBuffer` supports either 8-bit or 16-bit palette indices.
It initializes only the source pointer for the selected index width, but at the
end of every decoded row it increments both pointers. Pointer arithmetic on the
uninitialized pointer is undefined behavior even though the next row reads only
the selected pointer.

This helper is part of normal supported-ROM loading. A temporary runtime census
on Dam observed both compression methods that call it across five shipped pixel
formats, so the defect is not limited to malformed texture metadata.

## Evidence

[`texInflateLookupFromBuffer`](../../../src/game/image.c#L2883) declares
`src8` and `src16` without initializers at lines 2889-2890. It then initializes
exactly one:

```c
if (numcolours <= 256) {
    src8 = (u8 *)src;
} else {
    src16 = (u16 *)src;
}
```

Every format branch advances both pointers at the end of each row. Examples are
lines 2932-2933 for RGBA32, 2954-2955 for RGB24, and 3047-3048 for I4/IA4. One
of those two compound assignments therefore evaluates an indeterminate pointer
on every nonempty image.

The live callers are in
[`texInflateNonZlib`](../../../src/game/image.c#L1716):

- `TEXCOMPMETHOD_HUFFMANLOOKUP` calls the helper at line 1833.
- `TEXCOMPMETHOD_RLELOOKUP` calls the helper at line 1839.

Clang reports uninitialized array subscripts and compound assignments throughout
the helper. An environment-gated, read-only header census was temporarily added,
run, and removed. A deterministic Dam load observed:

| Compression | Method | Shipped formats observed |
| --- | ---: | --- |
| Huffman+lookup | 6 | I8, I4 |
| RLE+lookup | 7 | RGBA16, IA8, IA4 |

Observed examples included I8 `64x32`, I4 `64x64`, IA8 `56x56`, IA4 `64x64`,
and RGBA16 `14x14`. The run completed, which shows current Clang happens not to
turn the undefined operation into a visible failure; it does not make the C
behavior defined.

## Reproduction

1. Analyze `src/game/image.c` under the native build defines with Clang's core
   uninitialized-value checks.
2. Follow either the `numcolours <= 256` or `numcolours > 256` branch into any
   nonempty format loop.
3. At the end of the first row, observe that both `src8` and `src16` are
   incremented although only one was initialized.

A runtime route can direct-boot Dam and count compression headers at
`texInflateNonZlib`; methods 6 and 7 prove the helper is reached by shipped
assets without retaining or committing ROM data.

## Root Cause

The function kept two typed cursors to mirror the source index width but placed
cursor advancement outside the same conditional that selects the cursor. The
inactive pointer was probably harmless on the original compiler/ABI, but its
evaluation is undefined in native C and modern optimizers are not required to
preserve that accidental behavior.

## Required End State

Advance only the active source cursor. The smallest repair is to place `src8 +=
width` and `src16 += width` in the matching `numcolours` branch in each format,
or use one byte cursor and derive a typed row pointer without ever creating an
uninitialized object.

Preserve index interpretation, row stride, palette lookup, output padding,
endianness, and the returned byte count. Do not add a per-pixel branch if a
per-image or per-row selection gives the same result. Separately validate that
16-bit input rows remain suitably aligned before using a `u16 *`.

## Acceptance Criteria

- No uninitialized pointer is evaluated for either index width.
- Clang static analysis reports no uninitialized subscript or compound
  assignment in `texInflateLookupFromBuffer`.
- ROM-free fixtures cover every output format with `numcolours` values 1, 256,
  257, and the supported maximum.
- Multi-row fixtures prove only the selected cursor advances and row starts are
  correct.
- Odd and even widths are covered for I4/IA4 without a source over-read.
- Decoded byte hashes and returned byte counts for all shipped textures are
  unchanged from a current-compiler baseline.
- A local supported-ROM all-stage load completes with zero texture-decode
  sanitizer findings.

## Verification Plan

Add table-driven unit vectors with repository-owned compressed index/palette
data, then run the existing texture load path under ASan/UBSan and a Clang
MemorySanitizer build where available. For ROM-backed verification, hash only
the decoded texture outputs and aggregate method/format counts; do not commit
the pixels or compressed source bytes.

## Related Work

This is separate from texture-ID bounds and allocation-size hardening already
implemented in the native loader. Those guards validate which asset and output
size are used; they do not define the inactive local pointer in this helper.
