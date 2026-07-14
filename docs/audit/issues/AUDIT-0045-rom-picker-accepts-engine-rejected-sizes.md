# AUDIT-0045: ROM Picker Marks Engine-Rejected ROM Sizes Ready to Play

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S3 - the launcher authorizes inputs the engine cannot boot safely |
| Priority | P1 |
| Area | Launcher / ROM validation contract |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | App-shell ROM selection for 4 MB through 64 MB files with a GoldenEye title |

## Summary

The launcher accepts any recognized N64 image from 4 MB through 64 MB whose
header title contains `GOLDENEYE`, then labels it Ready to play. The engine
requires exactly 12 MB. A padded, truncated-within-range, or synthetic image can
enable Play in the launcher only to be rejected or reach the weaker engine
validation covered by AUDIT-0005.

## Evidence

[`rom_validate.cpp`](../../../src/app/rom_validate.cpp) states incorrectly that
GoldenEye is a 16 MB cartridge and accepts the broad standard N64 range. After
checking magic and a title substring, it sets `info.valid=1` and emits Ready to
play. It does not validate the exact size, supported region identity, ROM hash,
or file table.

[`rom_io.c`](../../../src/platform/rom_io.c) correctly documents that all
supported GoldenEye regions are exactly `0xC00000` bytes and rejects any other
size before allocation. [`ui_rom.cpp`](../../../src/app/ui_rom.cpp) enables Play
solely from the app validator's `valid` field.

The existing validator test uses a valid 12 MB positive fixture and a 2 KB
negative fixture, but does not cover 4 MB, 16 MB, or 64 MB GoldenEye-titled
images that the current range accepts.

## Reproduction

Create a 16 MB file with valid z64 magic, `GOLDENEYE` at header offset 0x20, and
a supported country byte. Select it in the launcher. The card reports Ready to
play and enables Play. Engine boot then rejects it because it is not exactly
12 MB.

## Root Cause

The app and engine implement separate validators with different assumptions.
The app checks generic N64 plausibility and branding, while the engine checks a
single structural property but neither shares one authoritative supported-ROM
contract.

## Required End State

Use one validation result across the picker and engine. At minimum, the launcher
must reject every input the engine rejects. The complete end state authenticates
supported U/E/J dumps by normalized byte order and approved identity/hash before
either enabling Play or indexing fixed ROM structures.

## Acceptance Criteria

- Only exact supported GoldenEye image sizes can enable Play.
- A 16 MB GoldenEye-titled synthetic file is rejected in the launcher.
- z64, v64, and n64 byte orders normalize to the same supported identity.
- Supported regions are identified explicitly rather than by title substring.
- Launcher and CLI return the same decision and reason for every fixture.
- Valid user ROM bytes are never modified on disk.

## Verification Plan

Create a shared ROM-validation fixture matrix for exact supported identities,
wrong game at 12 MB, correct title at 4/16/64 MB, bad magic, truncated header,
and each byte order. Run the same table through launcher and engine APIs and
assert identical decisions before a short boot of each accepted region.

## Related Work

- AUDIT-0005 requires the engine to reject a wrong 12 MB ROM before fixed-table
  dereferences.
- AUDIT-0035 requires the app to propagate an engine rejection if one still
  reaches boot.

## Resolution <!-- triage-2026-07-14 -->

Verified already fixed in commit `6c1a23d` (ledger Status was stale). rom_validate.cpp:67 now gates on the exact 12 MB size the engine accepts, matching the loader; oversized/undersized files are no longer marked Ready.
