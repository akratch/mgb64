# FID-0069 — laser impact-FX suppression gates on the wrong item

**Function:** `sub_GAME_7F04EA68` (object-hit shot handler), VRAM `0x7F04EA68`.
**Live C:** `src/game/chrobjhandler.c:39076` (`sub_GAME_7F04EA68`), gate at `:39160`.
**Authoritative ASM:** `src/game/chrobjhandler.c:39336-39554` (GLOBAL_ASM body).
**Class:** port-defect → fix, default-ON behind `GE007_NO_WATCHLASER_IMPACT_FIX`.

## Divergence

The handler, after applying damage/audio, spawns the material bullet-impact
effect (`explosionCreateBulletImpact`) unless the shot came from a specific
weapon. Retail suppresses the whole impact block for the **Watch Laser**.

Retail ASM (`chrobjhandler.c:39445-39448`):

```
/* 7F04EBF8 */  li    $at, 23              # 23 == ITEM_WATCHLASER
/* 7F04EBFC */  lw    $a0, 0x18($t5)       # $t5 = shotdata (saved sw 0x70($sp) @7F04EB2C),
                                            # 0x18(shotdata) = shotdata->weapon
/* 7F04EC00 */  beq   $a0, $at, .L7F04ED84 # weapon == 23 -> skip the entire impact block
/* 7F04EC04 */   nop
```

`.L7F04ED84` is the join point *after* both `explosionCreateBulletImpact`
call sites (`7F04EC80`, `7F04ED74`). So retail enters the impact block iff
`weapon != 23` (`ITEM_WATCHLASER`).

Port C (`chrobjhandler.c:39160`):

```c
if (weapon != ITEM_TASER) {   /* ITEM_TASER == 31 — WRONG constant */
    ... explosionCreateBulletImpact ...
}
```

## Item enum anchors (`src/bondconstants.h:3344-3375`)

`ITEM_UNARMED = 0` (no explicit initializers), counting forward:
`ITEM_LASER = 22`, **`ITEM_WATCHLASER = 23`**, … `ITEM_TRIGGER = 30`,
**`ITEM_TASER = 31`**. `ITEM_TRIGGER = 30` is the sanity anchor validated by
the `GE007_AUTO_EQUIP_ITEM=30` detonator repro (memory: detonator-fix).

## Player-visible effect

- **Watch Laser (23) equipped, firing at a prop/door/crate:** retail spawns
  *no* impact effect; the port takes `23 != 31` (true) → enters the block →
  spawns a material bullet-impact effect **and draws an extra
  `randomGetNext()`** (ASM `7F04EC40` / `7F04ECC4`, both skipped when
  `weapon == 23`). The extra PRNG draw shifts the deterministic stream
  (cf. FID-0063), so every subsequent RNG-consuming event on that frame
  diverges.
- **Taser (31) equipped:** retail takes `31 != 23` (true) → spawns the impact
  effect; the port takes `31 != 31` (false) → the Taser's impact FX is wrongly
  suppressed.

## Corroboration

The sibling background-hit path uses the correct constant for the identical
"is this the watch laser" test: `src/game/chrprop.c:2835` and `:3043`
(`weapon_id == ITEM_WATCHLASER`). Only the object-hit handler transcribed the
wrong enum member.

The rest of `sub_GAME_7F04EA68` is verified equivalent to the ASM (ancestor
walk `7F04EA88-7F04EA9C`; impact-pos math incl. the `26.0f` push
`7F04EAB8`+; hit-count chain `li $a1,6` `7F04EBAC`; material-table
`D_8004E86C` stride-8 + `hitSound & 0xf`; door/cctv skeleton grouping;
`F_80030B24`/`F_80030B18` autogun/cctv damage scaling; detonate call; door
rattle `unkbd>=3`; bounce flag `sll9/sll10`). The single defect is the gate
constant.

## Fix

Factored the gate into a pure ROM-free predicate
`chrobjImpactFxSuppressed(weapon, legacy)`
(`src/platform/chrobj_impact_suppress.c`):

- faithful (`legacy == 0`): `weapon == ITEM_WATCHLASER` (23)
- legacy (`legacy != 0`): `weapon == ITEM_TASER` (31) — reproduces the pre-fix
  port byte-identically for the `GE007_NO_WATCHLASER_IMPACT_FIX` negative
  control.

Regression lane: `tests/test_chrobj_impact_suppress.c` (ctest
`chrobj_impact_suppress`) — asserts the faithful predicate suppresses 23 (not
31), the legacy predicate suppresses 31 (not 23), and faithful ≠ legacy at
both 23 and 31 (fails on revert of the constant swap).

## Status

Dormant on the combat/traverse golden tapes (the Watch Laser is not fired at
props on those routes), so `landed` is the terminal state reachable without
the ROM oracle; tape/oracle re-verification is blocked on the runtime lock.
