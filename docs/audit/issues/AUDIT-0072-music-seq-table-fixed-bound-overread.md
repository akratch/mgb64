# AUDIT-0072: Music Sequence Table Is Read Past Its Allocation With a Fixed Bound

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S3 - a music sequence-table header with fewer entries than the fixed track count causes an out-of-bounds read and crash |
| Priority | P2 |
| Area | Audio / music sequence-table parsing |
| Evidence level | Fault injected |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Native port, any ROM whose music sequence-table count is below `NUM_MUSIC_TRACKS` |

## Resolution

Verified already fixed (commit `e51d12ef`, 2026-07-13, `[AUDIT-0072]` comment). The length-load loops (`music.c:985-995` and the `#else` `:1057-1066`) are bounded by `min(seqCount, NUM_MUSIC_TRACKS)` with remaining slots zero-filled, and the per-track play paths guard `CurrentTrackNum >= seqCount`. A short/zero header no longer over-reads. Status flipped by a verification sweep; the field was stale.

## Summary

The music loader reads a sequence count from the table header and allocates the
sequence array for exactly that many entries, then immediately iterates a fixed
`NUM_MUSIC_TRACKS` (63) times reading each `seqArray[ui]`. When the header count
is below 63 the load loop reads past the allocation, which faults.

## Evidence

In [`music.c`](../../../src/music.c) the loader reads `seqCount` as a big-endian
`u16` from the table header, sizes the destination as
`sizeof(RareALSeqBankFile) + sizeof(RareALSeqData) * (seqCount > 0 ? seqCount - 1
: 0)`, stores `seqCount`, and fills `seqArray` in a `ui < seqCount` loop. The very
next loop is bounded by `NUM_MUSIC_TRACKS` and reads
`g_musicDataTable->seqArray[ui].uncompressed_len` and `.len` for every `ui`. A
second table path in the same file repeats the pattern: it sizes the segment by
`g_musicDataTable->seqCount` but then loads track lengths with a
`ui < NUM_MUSIC_TRACKS` loop. [`music.h`](../../../src/music.h) defines
`NUM_MUSIC_TRACKS` as 63, so any header count below 63 makes the length-load loop
read beyond the allocated array.

An ASan build was run on a copy of a legally provided ROM with the sequence-table
count header set to zero. The sanitizer reported an out-of-bounds read and crash
at frame 5 during music-table load. No ROM bytes are reproduced here.

## Reproduction

On a writable copy of a legally provided ROM, set the music sequence-table count
field (the big-endian `u16` at the start of the sequence-table segment) below 63,
for example to zero. Boot under an ASan build into a level that loads stage music
(`--level dam --difficulty agent --deterministic --screenshot-frame 5
--screenshot-exit`, dummy audio driver). Observe the sanitizer read fault.

## Root Cause

Allocation is sized by the data-driven `seqCount`, but consumption is bounded by
the compile-time `NUM_MUSIC_TRACKS`. The two bounds are never reconciled, and no
validation requires the header count to cover the fixed track table.

## Required End State

The load loops must not read beyond the allocated entry count. Either reject a
table whose count is outside the expected range at load time, or bound every
`seqArray` read by the smaller of `seqCount` and `NUM_MUSIC_TRACKS` and
zero-fill the remaining track-length slots.

## Acceptance Criteria

- A header count below `NUM_MUSIC_TRACKS` never reads past the allocation.
- Both table-load paths in `music.c` share the reconciled bound.
- An out-of-range or zero count is rejected or clamped, not dereferenced.
- A regression loads a short-count table under ASan and asserts no fault.

## Verification Plan

Drive the music-table loader with count values of 0, 1, 62, 63, and an
above-range value under ASan; assert no out-of-bounds access and a defined
outcome for each. Confirm a normal-count ROM still loads all tracks unchanged.

## Related Work

- AUDIT-0071 covers the ignored decompression status in the same loader.
- AUDIT-0027 covers out-of-bounds ROM reads from wrapped bounds arithmetic.
- AUDIT-0065 covers a tape reader that allocates from an unverified header count.
