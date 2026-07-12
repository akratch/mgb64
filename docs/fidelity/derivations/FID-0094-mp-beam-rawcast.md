# FID-0094 — MP other-player beam/aim tick: raw N64-offset cluster

**Status:** root-caused. Native field mapping derived and the raw-offset
divergence machine-proven (compile-time locks in `tests/test_struct_layout.c`);
the code fix is authored below but **NOT applied** because its only site,
`playerTickBeams` in `src/game/bondview.c`, is owned by the raw-cast /
determinism agent and off-limits to this pass — see "Code fix (ready to apply)".

**Class:** port-defect · **Surface:** sim · **Priority:** P3

## Summary

The multiplayer / split-screen beam-and-aim tick for a *non-current* player
(`player = g_playerPointers[playerIndex]`) in `bondview.c` (~lines 24935–25439)
is **native-active** (no `#ifdef NATIVE_PORT` guard) yet addresses the `struct
player` — and indexes its `hands[]` array — entirely through **raw N64 byte
offsets and the N64 936-byte hand stride**. On the 64-bit port layout, pointer
fields inside `struct player` and `struct hand` expand 4→8B, so every one of
these offsets (and the stride) is wrong, exactly like FID-0085/0087/0088. Because
the reads and writes are internally consistent *with each other* (all raw), the
cluster is self-referential — but it diverges from any code that touches the same
logical fields via named members, and the `i*936` hand index walks off the real
`hands[]` layout.

This path is only reached for a non-current player's beam tick (split-screen /
MP). The current deterministic 1P oracle does not exercise it, so this is filed
with the derivation below rather than fixed in the raw-cast sweep pass — each
site needs a per-field native mapping, which is more than a one-line same-idiom
swap.

## Sites and raw offsets

| Site (bondview.c) | Expression | Raw N64 offset | Meaning |
|---|---|---|---|
| ~24935 | `aimback = *(f32*)((u8*)player + 0x2A00)` | player+0x2A00 | aim-back |
| ~24936 | `aimsideback = *(f32*)((u8*)player + 0x2A04)` | player+0x2A04 | aim-side-back |
| ~25388 | `chrSetFiring(chr, 0, *(s8*)((u8*)player + 0x875))` | player+0x875 | hand-0 firing flag |
| ~25390 | `chrSetFiring(chr, 1, *(s8*)((u8*)player + 0xC1D))` | player+0xC1D | hand-1 firing flag |
| ~25405 | `sub_GAME_7F02D630(chr, i, (coord3d*)((u8*)player + handOffset12 + 0x2A10))` | player+i*12+0x2A10 | per-hand beam position |
| ~25408/25413 | `*(s32*)((u8*)player + handOffset4 + 0x2A28)` | player+i*4+0x2A28 | per-hand last-active frame |
| ~25429–25433 | `*(f32*)((u8*)player + srcOff + 0x0B50/0x0B54/0x0B58)` | player+i*936+0xB50.. | hand-position source |

`handOffset4`/`handOffset12` step 4/12 per hand (`i` = 0,1); `srcOff` is computed
by the strength-reduced chain at 25419–25424 = **`i*936`**, the N64 `sizeof(struct
hand)`. On native `sizeof(struct hand) == 968` (locked in `test_struct_layout.c`,
FID-0085), so the second hand's source is mis-indexed.

## Why native-active raw offsets are wrong here

Same mechanism as FID-0085/0087/0088:

- `struct player` contains real pointers before these offsets (e.g.
  `StandTile *room_pointer` at N64 0x34, and the `hands[]`/weapon-buffer pointer
  growth), so `player + 0x2A00` etc. do not name the intended fields on the
  64-bit build.
- `hands[]` indexing with the N64 936 stride diverges from the native 968 stride
  from the second element on.

The FID-0085 offset probe established the post-hands uniform shift is +0x150 for
fields just past `hands[]`; the 0x2A00+ region sits much further into the struct
where additional pointer growth accumulates, so a per-field native mapping (not a
single global delta) is required.

## VERSION_US ASM anchor (authoritative)

`playerTickBeams` VERSION_US body (`src/game/bondview.c`, `glabel playerTickBeams`
at line ~26391) confirms every raw offset:

| ASM | offset |
|---|---|
| `7F08B688 C5042A00 lwc1 $f4,0x2a00($t0)` | player+0x2A00 aimback |
| `7F08B690 C5062A04 lwc1 $f6,0x2a04($t0)` | player+0x2A04 aimsideback |
| `7F08BE40 81860875 lb  $a2,0x875($t4)`   | player+0x875 hand-0 firing |
| `7F08BE54 81C60C1D lb  $a2,0xc1d($t6)`   | player+0xC1D hand-1 firing |
| `7F08BE7C 24C62A10 addiu $a2,$a2,0x2a10` | player+i*12+0x2A10 beam pos |
| `7F08BEAC ADF82A28 sw  $t8,0x2a28($t7)`  | player+i*4+0x2A28 last-active frame |
| `7F08BEF0 C5C80B50 lwc1 $f8,0xb50($t6)`  | player+i*936+0xB50 hand-pos source |

## Native field mapping (the fix targets)

- **Firing flags.** `player+0x875` / `player+0xC1D` are
  `hands[0].weapon_firing_status` / `hands[1].weapon_firing_status`. Proof the
  same logical field is accessed by *name* elsewhere: `gun.c:1949`
  `return g_CurrentPlayer->hands[hand].weapon_firing_status;`. The two raws are
  exactly one N64 hand stride apart: `0xC1D - 0x875 == 0x3A8 == 936`.
- **Hand-position source.** `player+i*936+0xB50` is `hands[i].field_B50` (three
  consecutive f32 at 0xB50/0xB54/0xB58). The beam loop's `srcOff` strength-reduces
  (`(i<<3)-i` → `<<2` → `+i` → `<<2` → `+i` → `<<3`) to `i*936`, i.e. the **N64**
  `sizeof(struct hand)`. Native `sizeof(struct hand) == 968` (FID-0085 lock), so
  hand 1's source mis-indexes.
- **Post-hands aim/beam fields** (`0x2A00`, `0x2A04`, `0x2A10..0x2A24`,
  `0x2A28/0x2A2C`) sit past `hands[]`, so on the 64-bit layout they are shifted by
  the single uniform post-hands delta already proven for FID-0088/0091/0092
  (`offsetof(player, tileColor) - 0xFDC`, etc. in `test_struct_layout.c`). Every
  raw `player+0x2A00…` access therefore lands on the **wrong** native field — and,
  worse, the struct's names in this region are speculative/overlapping (e.g.
  `ptr_text_first_mp_award` is a *pointer* at N64 0x2A10, so the raw beam-position
  write corrupts a pointer's bytes on native). This sub-region needs the struct
  owner to introduce correctly-typed `beam_pos[2]` / `last_active_frame[2]` members
  (or an `offsetof`-correct overlay) as part of the fix.

## Divergence proof (machine-checkable, landed here)

`tests/test_struct_layout.c` now carries FID-0094 compile-time locks that fire if
the native layout ever coincides with a raw N64 offset (which would make the
divergence illusory): `offsetof(player,hands)+offsetof(hand,weapon_firing_status)
!= 0x875`, `+sizeof(hand) != 0xC1D`, `offsetof(player,hands)+offsetof(hand,
field_B50) != 0xB50`, and `sizeof(hand) != 936`. All hold — the raw offsets the
live `playerTickBeams` uses cannot be correct on the 64-bit build.

## Code fix (ready to apply — blocked on the `bondview.c` owner)

`playerTickBeams` is in `src/game/bondview.c`, which is off-limits to this pass
(owned by the raw-cast / Streets-determinism agent). When that file is free, apply
behind a default-ON `GE007_NO_MP_BEAM_RAWCAST_FIX` opt-out:

- `*(f32*)((u8*)player + 0x2A00)` → the player's aim-back field (post-hands member).
- `*(f32*)((u8*)player + 0x2A04)` → the player's aim-side-back field.
- `chrSetFiring(chr, 0, *(s8*)((u8*)player + 0x875))`
  → `chrSetFiring(chr, 0, player->hands[0].weapon_firing_status)`.
- `chrSetFiring(chr, 1, *(s8*)((u8*)player + 0xC1D))`
  → `chrSetFiring(chr, 1, player->hands[1].weapon_firing_status)`.
- beam pos `player + i*12 + 0x2A10` and last-active `player + i*4 + 0x2A28`
  → correctly-typed post-hands `beam_pos[i]` / `last_active_frame[i]` members.
- hand-pos source `player + i*936 + 0xB50` → `&player->hands[i].field_B50`.

Gate it with a 2-player split-screen beam capture (the 1P oracle does not exercise
this path), plus the compile-time locks above.

## Anchors

- Raw-cast defect class: FID-0085 (matrix), FID-0087 (position), FID-0088
  (casing tint), FID-0092 (last_z_trigger_timer), FID-0093 (gunammooff).
- Native stride/offset locks: `tests/test_struct_layout.c` (FID-0085 hand
  stride 968; FID-0087/0088/0091/0092 field locks).
