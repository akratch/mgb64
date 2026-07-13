# AUDIT-0005: CLI Accepts a Wrong 12 MB ROM and Crashes During Boot

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S2 - deterministic startup crash on rejected input class |
| Priority | P2 |
| Area | Platform / ROM loading / startup |
| Evidence level | Runtime reproduced |
| Confidence | High |
| Origin | Standardized from renderer audit finding 64; runtime reconfirmed 2026-07-12 |
| Affected configurations | Explicit `--rom` on all native platforms; wrong-region or corrupt same-size images |

## Summary

The engine boot path accepts any file that is exactly 12 MB. Bad N64 magic is
only a warning, after which the program applies a hardcoded US resource-offset
table, initializes SDL and audio, and attempts to decompress arbitrary bytes.
An incorrect same-size file therefore fails as a signal crash instead of being
rejected as an unsupported ROM.

The app-shell validator is not shared with engine boot. It also accepts a broad
4-64 MB size range and incorrectly tells users GoldenEye is approximately 16
MB, while the engine correctly requires the actual 12 MB cartridge size.

## Evidence

- [`platformInitRom`](../../../src/platform/rom_io.c#L24) requires only exact
  size at lines 41-57. Unrecognized header bytes print a warning at lines
  100-103, then the function logs success and returns zero.
- [`mgb64_headless_main`](../../../src/platform/main_pc.c#L1262) immediately
  calls `platformPatchFileTable` after that success and initializes SDL at line
  1279. An explicit `--rom` bypasses the auto-detection heuristic by design.
- [`platformPatchFileTable`](../../../src/platform/rom_offsets.c#L12) assigns
  hundreds of absolute offsets from one generated N64 ELF table. The CMake
  native target is compiled as `VERSION_US`.
- [`mgb_validate_rom`](../../../src/app/rom_validate.cpp#L43) is a separate
  app-shell check. Lines 63-68 accept 4-64 MB and report an expected size of
  about 16 MB. Its header repeats the incorrect 16 MB statement.
- The repository's US identity file records SHA-1
  `abe01e4aeb033b6c0836819f549c791b26cfde83`, but boot does not compute or
  compare an identity hash.

A release runtime test passed an all-zero 12,582,912-byte file through
`--rom`. The program logged:

```text
[ROM] Warning: unrecognized ROM header: 00 00 00 00
[ROM] Loaded 12582912 bytes (12.0 MB) ...
[ROM] File table patched with 12582912-byte ROM
```

It then created an OpenGL window and audio device and terminated with signal 11
at frame 0. The backtrace was:

```text
zlib_inflate -> decompressdata -> load_resource -> fileIndexLoadToBank
-> _fileNameLoadToBank -> langInit -> bossInitMainthreadData
```

## Reproduction

1. Create a 12,582,912-byte zero-filled file outside the repository.
2. Run the native executable with `--rom <file> --level dam` and a clean save
   directory.
3. Observe the header warning, successful load/patch messages, platform
   initialization, and signal crash in language resource decompression.

The test file contains no ROM-derived data and can be generated in CI.

## Root Cause

ROM validation is fragmented across auto-detection, the app shell, and the
engine loader. Only the engine loader protects the code that patches and reads
the ROM, and its sole hard failure is file size. The hardcoded resource table
implicitly requires a specific normalized ROM revision, but that requirement
is not expressed or enforced.

## Required End State

Create one portable validator used by the app shell, auto-detection, and the
engine boot path before file-table patching or any SDL/audio/window setup. It
must:

- Require exactly 12,582,912 bytes.
- Recognize z64, v64, and n64 magic and normalize byte order before identity
  checks.
- Verify the normalized internal title and region for actionable diagnostics.
- Require the exact supported US revision identity matching the generated
  offset table. Hash the normalized image; do not hash the container byte order.
- Reject EU/JP or other revisions unless the executable selects a verified
  revision-specific offset table and matching gameplay build.
- Release partially loaded memory and return a stable nonzero exit code on
  failure.
- Report the observed title/region/hash and the supported revision without
  implying a crash or silently continuing.

Update every 16 MB comment and message in `rom_validate.cpp` and
`rom_validate.h` to the exact 12 MB requirement. Validation must remain local;
it must not upload or retain user ROM contents.

## Acceptance Criteria

- The zero-filled 12 MB reproduction exits cleanly before SDL, audio, renderer,
  resource-table patching, or decompression.
- Wrong size, bad magic, wrong title, corrupt supported-region data, and each
  unsupported region/revision produce distinct actionable errors.
- A valid supported US image succeeds in z64, v64, and n64 byte order after
  normalization and reaches the title or direct-boot stage.
- No EU/JP image is patched with US offsets.
- App-shell and CLI validation return the same validity, size, byte-order,
  region, and revision result for the same file.
- Automated tests use synthetic headers for rejection cases and never commit
  copyrighted ROM data.

## Verification Plan

Extend the existing ROM validator tests with exact-size synthetic images and a
hash-provider seam. Add a CLI process test that asserts exit code, stderr, and
absence of SDL initialization for the zero-filled file. Keep valid-image tests
local or inject the expected hash implementation so CI remains ROM-free.

## Related Work

- Prior evidence: finding 64 in
  [`RENDERER_SIM_AUDIT_2026-07-06.md`](../../RENDERER_SIM_AUDIT_2026-07-06.md).
- That earlier report identified same-size wrong-region/corrupt acceptance. This
  standalone record adds a deterministic signal-crash reproduction and the
  inconsistent app-shell size contract.
