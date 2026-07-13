# AUDIT-0026: Staged String Settings Silently Truncate Paths at 63 Bytes

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - valid selected resource paths are silently corrupted on Apply |
| Priority | P2 |
| Area | Configuration staging / string settings |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | In-game staged edits to string settings, including texture and decor paths |

## Summary

The registered texture-pack and scene-decor path buffers accept up to 1023
bytes, but the in-game staging store retains only 63 bytes of any value. A
native folder selection longer than that is silently truncated and the damaged
path is committed and saved by Apply.

## Evidence

[`config_schema.c`](../../../src/platform/config_schema.c) defines each
`StagedEntry.val` as 64 bytes. `stagedPut` copies with bounded `snprintf` and
does not report truncation. `mgb_config_set_string` routes through that function
whenever a staging session is active, and `configStagingApply` commits the
truncated value before saving.

[`platform_sdl.c`](../../../src/platform/platform_sdl.c) registers both
`Video.TexturePack` and `Video.SceneDecorDir` against 1024-byte backing arrays.
[`ui_settings.cpp`](../../../src/app/ui_settings.cpp) passes the complete path
returned by the native folder picker to `mgb_config_set_string`, but its local
display and picker-start buffer is independently limited to 512 bytes.

The existing staging unit test covers numeric and enum transactions but has no
string-length case.

## Reproduction

1. Start an in-game settings staging session.
2. Select a texture-pack or scene-decor directory whose UTF-8 path exceeds 63
   bytes.
3. Press Apply and inspect the live value or saved INI.
4. Observe that the value ends after byte 63 and no error was shown.

The defect is also directly reproducible in a ROM-free unit test by staging a
64-byte value and reading it before and after Apply.

## Root Cause

The staging implementation sized every serialized value for short numeric and
enum text instead of honoring each registered string setting's capacity. The
write API has no success or truncation result.

## Required End State

Store staged values without loss up to the registered setting capacity, using
capacity-aware storage or owned dynamic strings. Reject over-capacity input
explicitly and leave the prior value intact. The UI must display and reopen the
same complete path it will commit.

## Acceptance Criteria

- Values of 63, 64, 511, and 1023 bytes round-trip exactly when within the
  registered capacity.
- An over-capacity value is rejected visibly and is never partially saved.
- Apply commits the full value and Discard restores the prior full value.
- UTF-8 paths are not cut in the middle of a code unit sequence.
- Folder-picker initial paths and displayed paths are not independently
  truncated by a smaller UI buffer.
- String boundary cases are covered by the ROM-free staging test.

## Verification Plan

Extend the staging unit test with ASCII and multibyte path cases at every
boundary, checking staged reads, Apply, Discard, saved configuration, and
failure atomicity. Run a native folder-picker smoke with a path longer than 512
bytes where the host filesystem supports it.

## Related Work

- None.
