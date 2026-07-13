# AUDIT-0028: Weapon Equip and Reload Sounds Diverge from the Retail Tables

| Field | Value |
| --- | --- |
| Status | Open |
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
