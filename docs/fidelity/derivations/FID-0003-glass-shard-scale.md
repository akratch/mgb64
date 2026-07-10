# FID-0003 — glass shard geometry scale derivation

Authority: retail ASM of the shard emitter inlined in `src/game/unk_0A1DA0.c`
(`glabel sub_GAME_7F0A2C44`, the disassembly kept directly below the C reimpl),
plus the retail vs native branches of the field_10E0 build in
`src/game/bondview.c`. Charter authority hierarchy: retail ASM > ares > decomp C.
No stock reference capture or ROM is present in-tree, so the pixel oracle
(`tools/compare_glass_shard_pixel_oracle.py`, a spatial-attribution tool that
diffs two *native* traces — "not a causality proof") cannot establish ground
truth against N64. The ASM is the sole and sufficient authority here.

---

## The six flags (as found)

Block `src/game/unk_0A1DA0.c:1266-1336`, all `#ifdef NATIVE_PORT`, per falling
shard, applied to the per-piece model matrix `mtxf` after the player-position
translation fixup:

| # | flag (env) | scale applied to `mtxf.m[0]` | default | live? |
|---|------------|------------------------------|---------|-------|
| 1 | `GE007_GLASS_SHARD_COMPRESS`      | `matrix_scalar_multiply_3(get_room_data_float1())` | off | no |
| 2 | `GE007_GLASS_SHARD_BASIS_SCALE`   | `matrix_scalar_multiply_2(get_room_data_float1())` | off | no |
| 3 | `GE007_GLASS_SHARD_NO_BASIS_SCALE`| none (identity)                                     | off | no |
| 4 | `GE007_GLASS_SHARD_SQRT_BASIS`    | `matrix_scalar_multiply_2(sqrtf(room_data_float1))` | off | no |
| 5 | `GE007_GLASS_SHARD_INV_VIS_SCALE` | `matrix_scalar_multiply_3(1.0 / bgGetLevelVisibilityScale())` | **on** | **yes** |
| 6 | `GE007_GLASS_SHARD_FIXED_MTX` (`:1160`) | selects fixed-point vs float matrix *emission format* (not a scale) | off (float) | yes |

Flags 1-5 are the competing scale hypotheses; #5 (`INV_VIS_SCALE`) was the
shipped default. #6 (`FIXED_MTX`) is orthogonal — a matrix precision/format
control, not a scale — and is out of scope for the scale question.

## Ground truth (retail ASM)

`sub_GAME_7F0A2C44`, loop `.L7F0A2DB0` (`unk_0A1DA0.c:1475-1536`):

```
dynAllocateMatrix                                 ; -> mtx
matrix_4x4_set_position_and_rotation_around_xyz   ; mtxf@sp+0x90 = pos+rot, NO scale
lwc1 f4,0xC0(sp); lwc1 f6,0x38(v0); sub.s ...     ; mtxf.m[3][0..2] -= player.pos  (sp+0xC0/C4/C8)
matrix_4x4_f32_to_s32                             ; convert mtxf -> mtx, emit
```

Between building the matrix and converting it, the **only** operation is the
player-position subtraction on the translation row (offset 0x30 into the
0x40-byte mtxf → sp+0xC0/0xC4/0xC8). There is **no `matrix_scalar_multiply` of
any kind**. On the ROM the shard model matrix is used **UNSCALED**.

## Why "unscaled" does not mean flag #3 (no-scale) is the shipped answer

The ROM's unscaled shard is faithful **only relative to the projection it draws
through** — `field_10E0` (= proj·view), pushed once before the shard loop
(`unk_0A1DA0.c:1193-1194`, same `get_BONDdata_field_10E0()` in ASM and C).

- **Retail bondview** (`bondview.c:16258-16275`, non-`NATIVE_PORT`): builds
  `field_10E0 = proj·spC4` **first**, *then* applies the visibility scale to
  `spC4` — so `field_10E0` itself is **unscaled**.
- **This port** (`bondview.c:16188-16191`, default `GE007_FIELD_10E0_SCALED=1`):
  applies `matrix_scalar_multiply(visibility, spC4.m[0])` **before** the
  proj·view multiply — so the native `field_10E0` **carries the visibility
  scale**. This is the port's deliberate large-room rendering strategy.

Level visibility (`levelinfotable[].visibility`, `bg.c:2826`): **1.0 for every
level except Dam, Surface, Surface 2 = 0.2** — i.e. exactly the glass-heavy
guard-tower repro. On those levels the native `field_10E0` view basis is ×0.2.

Drawing an *unscaled* shard matrix through a ×0.2 `field_10E0` would render every
shard 5× too small. To reproduce the ROM's **net** transform the shard model
must invert that: `matrix_scalar_multiply_3(1/visibility)` = ×5.0, and
`(1/vis)·vis = 1` = the ROM's unscaled shard. The 5× on the shard modelview and
the 0.2× on the view basis compose to identity.

Only two pairings are retail-faithful:

| `field_10E0` | required shard scale | net vs ROM |
|--------------|----------------------|------------|
| scaled (default) | `× 1/visibility` (flag #5) | identity ✓ |
| unscaled (`GE007_FIELD_10E0_SCALED=0`) | none (flag #3) | identity ✓ (== retail ASM literally) |

Flags #1/#2/#4 scale by `get_room_data_float1()` (or its sqrt), which is the
room-compression scale, **not** `1/visibility` — they never invert the 0.2 and
therefore diverge from the ROM on any visibility≠1 level. Flag #3 alone diverges
5× under the shipped `field_10E0` default. **Flag #5 is uniquely correct given
the shipped default, and is the current default.**

## The collapse (landed)

The scale is not an independent knob — it is *coupled* to the `field_10E0`
decision. Collapsed the five scale flags to that single coupling, keyed on the
already-existing `GE007_FIELD_10E0_SCALED` (the one opt-out, which now correctly
governs both sides in lockstep):

```c
if (field_10E0 scaled)   matrix_scalar_multiply_3(1.0f / visibility, mtxf.m[0]);
else                     /* unscaled: shard as-is == retail ASM */ ;
```

Removed env vars: `GE007_GLASS_SHARD_COMPRESS`, `_BASIS_SCALE`,
`_NO_BASIS_SCALE`, `_SQRT_BASIS`, `_INV_VIS_SCALE`. Retained: `GE007_GLASS_SHARDS`
(shard pass on/off) and `GE007_GLASS_SHARD_FIXED_MTX` (matrix format, orthogonal).
The parallel scale-mode readers in `src/platform/port_trace.c` (diagnostic trace
only) were collapsed to the same coupling so the reported `scale_mode` stays
truthful. Bug fixed in passing: with `GE007_FIELD_10E0_SCALED=0` the old code
still applied `1/vis` to shards (5× too big) — the coupling makes the opt-out
path self-consistent.

## Verification

- **Default behavior bit-identical**: old default resolved to
  `matrix_scalar_multiply_3(1.0f/vis, …)` guarded by `vis>0`; new default is the
  identical call. So both the sim-state hash **and** the rendered display list
  are unchanged for the shipped config.
- **Sim-hash invariance is structural**: the shard scaling writes only to the
  local stack `mtxf`, consumed into the `gdl` display list; `sub_GAME_7F0A2C44`
  returns `Gfx*` and never writes player/prop/BSS sim state. Shards cannot enter
  the sim hash. `sim_state_hash` ctest passes with the change compiled in.
- **Regression lane**: `tools/check_glass_shard_scale_regression.py`
  (ctest `port_glass_shard_scale_guard`) asserts the source invariant — no
  retired flag present, `1/visibility` compensation present, coupled to
  `GE007_FIELD_10E0_SCALED`. Fail-on-revert verified: the pre-fix source (with
  all five retired flags) reddens the guard.
- **Pixel oracle**: cannot run — no ROM / no stock capture in-tree. The ASM net
  transform is the authority and is unambiguous.

## Residual / concerns

- The correctness rests on `field_10E0`'s vis-scaling and the shard's
  `1/vis` compensation composing to *exact* identity. The two use different
  `matrix_scalar_multiply` variants (view: rows 0-2; shard: columns 0-2 incl.
  translation), which compose to identity for the position+basis transform but
  were never confirmed pixel-for-pixel against a stock capture (none exists).
  If a ROM/stock reference is later added, wire the shard pixel oracle as the
  empirical ratchet on top of this source guard.
- Whether the port *should* scale `field_10E0` at all (vs. the stock-style
  unscaled path) is a separate, broader rendering-strategy question owned by
  `bondview.c` — out of scope here. This fix makes the shard scale correct under
  **either** choice.
