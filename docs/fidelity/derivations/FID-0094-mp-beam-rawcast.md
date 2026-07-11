# FID-0094 — MP other-player beam/aim tick: raw N64-offset cluster

**Status:** filed (not fixed this pass — the complex member of the raw-cast
defect class closed out alongside FID-0085/0087/0088/0091/0092).

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

## Fix shape (deferred)

Each raw site must become a named-field or `offsetof`-correct access, and the
`i*936` hand index must become `&player->hands[i].<field>`. Because this only
affects the MP/split-screen non-current-player beam+aim path, it should land with
a split-screen beam regression capture (2-player) as its gate rather than the 1P
oracle. Until then the cluster is preserved as-is (internally consistent) behind
this filing.

## Anchors

- Raw-cast defect class: FID-0085 (matrix), FID-0087 (position), FID-0088
  (casing tint), FID-0092 (last_z_trigger_timer), FID-0093 (gunammooff).
- Native stride/offset locks: `tests/test_struct_layout.c` (FID-0085 hand
  stride 968; FID-0087/0088/0091/0092 field locks).
