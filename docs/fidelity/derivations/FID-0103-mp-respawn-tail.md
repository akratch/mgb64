# FID-0103 — object-respawn tail dropped from chrprop respawn tick

**Status:** landed (runtime `verified` gated on an MP-tick harness — see below).

**Class:** port-defect · **Surface:** sim · **Priority:** P3

## Summary

`handle_mp_respawn_and_some_things` (retail US ASM `glabel 7F03C648`) ticks the
object/prop list and, when a destructible or regenerating OBJ/WEAPON/DOOR prop
crosses its regen threshold, respawns it. Retail runs a **respawn tail** at labels
`.L7F03C8AC`–`.L7F03C8E4` that the `NATIVE_PORT` C body dropped entirely, so
regenerating objects respawn **silently** and the PROPDEF_ARMOUR amount reset is
lost. Player-audible.

## Retail behaviour (instruction-level)

The tail is reached from **both** respawn sub-branches when the object regenerates
(`timetoregen > 0`, `< 0x3C`, was-above-threshold `$v1 == 0`, `maxdamage == 0`
path via `b .L7F03C8AC`, or the `maxdamage != 0` else branch which falls through
`.L7F03C894` into `.L7F03C8AC`):

```
.L7F03C8AC:
/* 7F03C8AC 92180003 */  lbu   $t8, 3($s0)        # obj type byte (obj+3)
.L7F03C8B0:
/* 7F03C8B0 24010015 */  li    $at, 21            # PROPDEF_ARMOUR
/* 7F03C8B4 3C048006 */  lui   $a0, %hi(g_musicSfxBufferPtr)
/* 7F03C8B8 17010003 */  bne   $t8, $at, .L7F03C8C8   # type != 21 -> skip copy
/* 7F03C8BC 24050052 */   li    $a1, 82            # sound index 82 (delay slot)
/* 7F03C8C0 C6060080 */  lwc1  $f6, 0x80($s0)      # f6 = obj[0x80] = initialamount
/* 7F03C8C4 E6060084 */  swc1  $f6, 0x84($s0)      # obj[0x84] = f6  -> amount = initialamount
.L7F03C8C8:
/* 7F03C8C8 16600006 */  bnez  $s3, .L7F03C8E4     # s3 (reparent marker) != 0 -> skip sound
/* 7F03C8CC 00003025 */   move  $a2, $zero          # a2 = 0 (delay slot)
/* 7F03C8D0 0C002382 */  jal   sndPlaySfx           # sndPlaySfx(g_musicSfxBufferPtr, 82, 0)
/* 7F03C8D4 8C843720 */   lw    $a0, %lo(g_musicSfxBufferPtr)($a0)
/* 7F03C8D8 00402025 */  move  $a0, $v0             # a0 = returned ALSoundState*
/* 7F03C8DC 0FC14E84 */  jal   chrobjSndCreatePostEventDefault
/* 7F03C8E0 26250008 */   addiu $a1, $s1, 8         # a1 = &prop->pos (prop+8)
.L7F03C8E4:
```

So the tail is exactly:

1. **type-21 float copy.** `obj+3` is the `ObjectRecord`'s inherited
   `PropDefHeaderRecord.type` byte. Object type `21` == `PROPDEF_ARMOUR`
   (`BodyArmourRecord`), whose N64 layout is `initialamount` at `0x80` and
   `amount` at `0x84`. The copy is `armour->amount = armour->initialamount`
   (reset current body armour to its authored initial amount on respawn).

2. **respawn sound.** `s3` is set to `1` only inside the `flags & 0x8000`
   reparent path (`.L7F03C838 li $s3, 1`). The port skips that path entirely, so
   `s3` is **always 0** here → the sound always fires:
   `chrobjSndCreatePostEventDefault(sndPlaySfx(g_musicSfxBufferPtr, 82, 0), &prop->pos)`.
   The identical `sndPlaySfx`→`chrobjSndCreatePostEventDefault` idiom is already
   live at `chrlv.c:6717` (sound `0x101`), so only this call site was missing.

## Port defect (before)

`src/game/chrprop.c` `handle_mp_respawn_and_some_things`: both respawn branches
end without the tail — no armour reset, no sound. `sizeof(ObjectRecord)` grows
`0x80`→`0x90` on the 64-bit port, so the retail raw `obj[0x80]`/`obj[0x84]` reads
would also be wrong; the fix uses the named `BodyArmourRecord` fields.

## Fix

Pure decision helper `src/platform/mp_respawn_tail.c` (`mpRespawnTailPlan`)
returns `{copy_armour_amount, play_respawn_sfx}`; the chrprop.c call site performs
the two side effects. Default **ON**; `GE007_NO_MP_RESPAWN_TAIL_FIX` restores the
silent legacy tail byte-identically (the plan returns `{0,0}` → no writes, no
sound). ROM-free regression lane: ctest `mp_respawn_tail`
(`tests/test_mp_respawn_tail.c`) pins the retail vs legacy decision and the
`faithful != legacy` flip; a revert reddens 6 checks (demonstrated).

## Verification honesty

The 1P combat oracle does not reach the MP object-respawn tick (precedent
FID-0093/0095), and ROM-derived assets are not present in this environment, so
full runtime `verified` promotion is gated on an MP-tick harness. Landed with:
the ASM derivation (this file), the passing fail-on-revert unit test, and sim
byte-identity under the opt-out — `test_sim_state_hash` and `test_struct_layout`
both PASS (the fix perturbs no struct layout and only fires on an MP object-regen
threshold crossing, which the 1P deterministic tapes do not hit).

## Anchors

- ASM: `src/game/chrprop.c:5699-5715` (US `7F03C8AC`–`7F03C8E4`).
- Idiom precedent: `src/game/chrlv.c:6717`.
- Type/field: `PROPDEF_ARMOUR` (`bondconstants.h`), `BodyArmourRecord`
  `initialamount`/`amount` (`bondtypes.h`).
- Fix + test: `src/platform/mp_respawn_tail.{c,h}`, `tests/test_mp_respawn_tail.c`.
