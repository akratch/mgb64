# AUDIT-0028: Weapon Equip and Reload Sounds Diverge from the Retail Tables

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S4 - common weapon actions play incorrect or missing cosmetic cues |
| Priority | P2 |
| Area | Gameplay audio / weapon-state fidelity |
| Evidence level | Source and retail-table proven |
| Confidence | High |
| Origin | Standardized from the prior monolithic audit and reconfirmed in current source |
| Affected configurations | Normal gameplay when equipping or reloading affected items |

## Summary

The C switches selecting weapon equip and reload sound effects do not match the
retail ASM jump tables embedded directly below them. Common cases include a
grenade incorrectly playing the mine cue and a remote mine playing no cue when
the retail table selects the mine sound.

## Evidence

[`gun.c`](../../../src/game/gun.c) labels the state-8 switch as the translation
of `jpt_80054194`. The C code groups `ITEM_GRENADE` with timed and proximity
mines and plays sound 235, but table entry 26 is `weapon_switchstyle_NONE`.
It makes `ITEM_REMOTEMINE` silent, while entry 29 is `weapon_playsfx_mine`.
`ITEM_WATCHLASER` and `ITEM_CAMERA` fall through to gun sound 232 although their
entries are silent. `ITEM_ROCKETLAUNCH`, `ITEM_BOMBDEFUSER`, and
`ITEM_EXPLOSIVEFLOPPY` are silent where the table selects a gun cue.

The state-11 reload switch is labeled as `jpt_80054294` but defaults several
table-silent items to sound 50, including `ITEM_TANKSHELLS`, `ITEM_PLASTIQUE`,
and `ITEM_CAMERA`. The source contains the item enum in
[`bondconstants.h`](../../../src/bondconstants.h), so the zero-based table
indices and mismatches can be checked without a ROM.

## Reproduction

With a valid ROM, enter an inventory or debug setup that provides grenades and
remote mines. Equip each item and trace calls to `sndPlaySfx`:

- Grenade currently emits 235; the retail table selects silence.
- Remote mine currently emits nothing; the retail table selects 235.

Reload or invoke the reload state for Tank Shells, Plastique, or Camera and
observe the current default sound 50 despite a table-silent entry.

## Root Cause

The reimplementation manually grouped item names into broad switch cases
instead of preserving the authoritative per-index jump-table mapping. Several
items that look semantically related have intentionally different retail cues.

## Required End State

Resolve equip and reload cues from mappings that correspond entry-for-entry to
the two retail tables. Retain readable item names, but generate or statically
verify the mapping so default switch fallthrough cannot invent a cue. Preserve
all timing and state transitions; only cue selection should change.

## Acceptance Criteria

- Every item index covered by `jpt_80054194` maps to the same equip cue or
  silence as the table.
- Every item index covered by `jpt_80054294` maps to the same reload cue or
  silence as the table.
- Grenade is silent and Remote Mine plays the mine equip cue.
- Known gun, knife, and laser positive controls retain their retail sounds.
- Out-of-range and post-table item handling is explicit rather than a broad
  audible default.

## Verification Plan

Add a ROM-free table test that enumerates all item IDs and compares resolved
cue IDs with the authoritative expected arrays. Run an audio trace smoke for a
grenade, a remote mine, a conventional firearm, and one affected reload case,
checking cue ID and frame while confirming unchanged simulation hashes.

## Related Work

- [`RENDERER_SIM_AUDIT_2026-07-06.md`](../../RENDERER_SIM_AUDIT_2026-07-06.md)
  first recorded the mismatched switches.

## Resolution

Fixed on `feat/webgpu-backend` by replacing the two hand-grouped equip (state 8)
and reload (state 11) SFX switches in `src/game/gun.c` with a pure, ROM-free
resolver (`src/platform/weapon_action_sfx.{c,h}`) that reproduces the retail jump
tables `jpt_80054194` (equip) and `jpt_80054294` (reload) **entry-for-entry**
(indices 0..61, index == `ITEM_IDS` ordinal). Cue ids are taken from the retail
`sndPlaySfx` bodies in the same file: equip `weapon_switchstyle_NONE`=silent /
`knife`=233 / `gun`=232 / `F2`=242 / `mine`=235; reload none=silent / gun=50.

Two divergence classes are corrected:

- **Per-index mis-grouping.** The old name-grouped switches diverged from retail
  at 12 equip indices and 8 reload indices — e.g. `GRENADE` played the mine cue
  (235) where retail is silent; `REMOTEMINE` was silent where retail plays the
  mine cue (235); `ROCKETLAUNCH`/`BOMBDEFUSER`/`EXPLOSIVEFLOPPY` were silent
  where retail plays the gun cue; `WATCHLASER`/`CAMERA`/`TANKSHELLS`/`PLASTIQUE`/
  `BUG` played the gun cue where retail is silent.
- **Off-by-one bound.** Retail dispatches the table for `weapon_id < 0x3e`
  (`< ITEM_BLACKBOX`, 62), so `ITEM_GOLDENEYEKEY` (61) is table-silent. The old
  guard `weapon_id < ITEM_GOLDENEYEKEY` (61) wrongly routed index 61 to the
  audible `else` branch (232 equip / 50 reload). The resolver restores the
  `< ITEM_BLACKBOX` bound and preserves the retail post-table branch
  (`weapon_id != ITEM_TOKEN` → gun; `ITEM_TOKEN` → silent). A `_Static_assert` at
  the call site pins `ITEM_BLACKBOX`/`ITEM_TOKEN`/`ITEM_GRENADE`/`ITEM_REMOTEMINE`/
  `ITEM_GOLDENEYEKEY` so `ITEM_IDS` drift can't silently desync the tables.

Guarded by the ROM-free unit test `tests/test_weapon_action_sfx.c` (ctest
`weapon_action_sfx`), which re-encodes both tables independently (so a co-edited
array can't mask a divergence) and asserts each named divergence plus the
boundary cases. Verified by a three-way byte match (retail jump tables ⇄ resolver
⇄ test). Audio is disjoint from the hashed simulation state, so the 7 input-tape
baselines stay byte-exact.
