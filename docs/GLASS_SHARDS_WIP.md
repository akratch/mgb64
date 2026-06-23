# Glass shards (C1) — WIP state

Falling-glass shards (`sub_GAME_7F0A2C44` in `src/game/unk_0A1DA0.c`) were un-stubbed
(commits `f5b8f18`, `d8debf5`). The **shatter crash is fixed**; the **render is still
WIP** — shards are too large and look randomly scattered. `GE007_GLASS_SHARDS=0`
disables them.

## Deterministic repro (no aiming)

Shooting glass headlessly is driven by scripted tag-damage, which calls
`maybe_detonate_object` → the shatter path:

```bash
# Dam glass prop is tag 19 (found by scanning GE007_AUTO_DAMAGE_TAG 0..40).
GE007_GLASS_SHARDS=1 GE007_TRACE_GLASS=1 \
GE007_AUTO_DAMAGE_TAG_FRAME=80 GE007_AUTO_DAMAGE_TAG=19 GE007_AUTO_DAMAGE_TAG_AMOUNT=80.0 \
./build/ge007 --rom baserom.u.z64 --level dam --deterministic \
  --screenshot-frame 110 --screenshot-exit
```

To see it close up, warp Bond to a pad next to the glass first (pad 100 ≈ 118 units
from the tag-19 glass; dump pads with `GE007_DUMP_STAGE_PADS=/tmp/pads.txt`):

```bash
... GE007_AUTO_WARP_FRAME=50 GE007_AUTO_WARP_PAD=100 ...
```

The tag-19 window is in a dark basement — too dark to judge quality. **A lit window
(interactive) is the real visual check.**

## Fixed

- **Crash:** `dynAllocateMatrix` returns `Mtx*` but had **no prototype** in
  `unk_0A1DA0.c` → implicit-`int` truncated its 64-bit pointer (`0x728705820` →
  `0x28705820`) → `matrix_4x4_f32_to_s32` wrote 64 bytes to a garbage address → SEGV.
  Fixed with the prototype. (Same class earlier: `currentPlayerGetMatrix10C8`.)
- **Blur (partial):** the shard render did not compress positions by the room scale.
  Dam's `get_room_data_float1()` ≈ **0.234** (room rendered ~4.3× compressed); the
  explosion and bullet-impact effects multiply by it, the shards didn't → ~4.3× too
  big. Now `matrix_scalar_multiply_3(get_room_data_float1(), &mtxf.m[0][0])`.
  NOTE: `bgGetLevelVisibilityScale()` returns **0.0** for Dam — wrong scale, do not use.
- Hardening: `dynAllocateMatrix` overflow → scratch `Mtx`; VTX buffer 256→512KB;
  shard loop breaks before the GFX buffer overruns; NULL guards.

## Remaining work (shards still too large + random)

1. **Still too large** after the 0.234 scale. The shard uses a **player-relative**
   origin (`(piece - player) * scale`) while the explosion uses **room-relative**
   (`world * scale - room_origin`). The size/position scheme may need to match the
   explosion's, and/or the per-shard size (`sub_GAME_7F0A2160` `size` from
   `sqrt(range1*range2/halfBuf)`) / the `piece+0x38` vertex extents need review.
2. **Random orientation/scatter.** Check the per-piece rotation
   (`piece->field_0x10/0x14/0x18`, read by `matrix_4x4_set_position_and_rotation_around_xyz`)
   and the `update_broken_windows` dispersion against the N64 original — the rotation
   fields may be stale/uninitialized at spawn (`sub_GAME_7F0A2160` does not visibly set
   them).

## Debug instrumentation (gated, strip once the visual lands)

- `GE007_TRACE_GLASS=1` → `[GLASS-SHATTER]` (centre, piece count, bondPos) and per-frame
  `[GLASS-SHARD]` (active count, `visScale`, `roomScale`, sample piece pos/rotation/vertex
  extents).
- `GE007_GLASS_SHARD_NO_SCALE=1` → A/B disable the room-scale compression.
