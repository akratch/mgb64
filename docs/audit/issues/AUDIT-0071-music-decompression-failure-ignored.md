# AUDIT-0071: Music Track Decompression Failure Is Ignored Before Sequence Parsing

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - a corrupt or truncated compressed music track produces a hard crash instead of a handled skip |
| Priority | P2 |
| Area | Audio / music track decompression |
| Evidence level | Fault injected |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Native port, any level whose stage music decompresses a damaged track |

## Summary

The music loader calls `decompressdata` for its return-valued status but discards
it, then copies the decompression output into the sequence buffer and hands it to
`alCSeqNew` regardless of whether inflation succeeded. A track whose compressed
Deflate stream is damaged is parsed as if it were a valid sequence, which faults.

## Evidence

[`decompress.h`](../../../src/game/decompress.h) declares
`u32 decompressdata(u8 *src, u8 *dst, struct huft *hlist)`, i.e. the helper
returns a status word. Every music call site discards it. In
[`music.c`](../../../src/music.c) the track-1 path calls `decompressdata` as a
bare statement, then `memcpy`s the output into `g_musicXTrack1SeqData` and calls
`alCSeqNew` on it with no success check. The same unchecked pattern repeats at
all six `decompressdata` call sites in that file (the track-1, track-2, and
track-3 primary and short paths).

An ASan build was run on a copy of a legally provided ROM with a single byte of
one music track's Deflate stream flipped, so decompression fails while the rest
of the image is intact. The sanitizer recorded a BUS fault on frame 0 inside the
sequence-parse path reached from `musicTrack1Play` via `alCSeqNew`, confirming
that failed inflation output flows straight into the parser. No ROM bytes are
reproduced here; the corruption is described by parameter only.

## Reproduction

On a writable copy of a legally provided ROM, flip one byte inside the compressed
Deflate payload of a stage music track so inflation fails but the file table and
checksum-relevant identity used by the boot path are otherwise reached. Boot that
copy under an ASan build into a level that plays stage music
(`--level dam --difficulty agent --deterministic --screenshot-frame 120
--screenshot-exit`, dummy audio driver). Observe the sanitizer fault at frame 0.

## Root Cause

The decompression result is treated as guaranteed rather than fallible. There is
no error branch between `decompressdata` and the `alCSeqNew` parse, and no
validation that the produced sequence has a sane length or structure before it is
consumed.

## Required End State

A failed or short decompression must not reach `alCSeqNew`. The loader should
check the `decompressdata` status, skip or silence the affected track on failure,
and keep the rest of audio and gameplay running. All six music decompress sites
share the corrected contract.

## Acceptance Criteria

- A nonzero `decompressdata` status skips the track and never calls `alCSeqNew`.
- A damaged track degrades to silence for that slot without a process fault.
- The guard is applied at every music decompress call site, not only track 1.
- A regression exercises a deliberately damaged track and asserts no crash.

## Verification Plan

Add a fault-injection harness that drives the music loader with a damaged
compressed track under ASan and asserts a clean skip and zero exit. Re-run the
existing audio smoke paths to confirm undamaged tracks still play unchanged.

## Related Work

- AUDIT-0072 covers the sequence-table count read that overruns its allocation.
- AUDIT-0027 covers out-of-bounds ROM reads from wrapped bounds arithmetic.
- AUDIT-0005 covers a wrong-size ROM accepted by the CLI and crashing at boot.
