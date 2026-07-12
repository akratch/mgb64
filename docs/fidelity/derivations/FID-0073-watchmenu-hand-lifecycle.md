# FID-0073 — phantom per-frame save/restore of the watch-menu hand item

**Function:** `set_enviro_fog_for_items_in_solo_watch_menu` (watch inventory-item
renderer), VRAM `0x7F063004` region.
**Live C:** `src/game/gun.c:15793`.
**Authoritative ASM:** `src/game/gun.c:15994-16332` (GLOBAL_ASM body).
**Class:** port-defect → fix, default-ON behind `GE007_NO_WATCHMENU_FOG_ITEM_FIX`.

## Divergence

Retail sets the right-hand watch-menu item **once** and never restores it. The
only hand-state call in the entire ASM body is a single
`jal sub_GAME_7F05DA8C` with `a0 = 0` (GUNRIGHT) and the remapped weaponnum:

```
/* 7F06305C */  lw    $at, ($t7)          # (tail of the renderdata template copy)
/* 7F063060 */  move  $a0, $zero          # hand 0 = GUNRIGHT
/* 7F063068 */  li    $at, 30
/* 7F06306C */  beq   $a1, $at, .L7F06307C # weaponnum == 30 (ITEM_TRIGGER)  -> 60
/* 7F063070 */  li    $at, 23
/* 7F063074 */  bne   $a1, $at, .L7F063080 # weaponnum == 23 (ITEM_WATCHLASER)-> 60
/* 7F06307C */  li    $a1, 60
/* 7F063080 */  jal   sub_GAME_7F05DA8C    # set hands[0].weaponnum_watchmenu
```

Crucially, before this call there is **no read** of the previous
`hands[GUNRIGHT].weaponnum_watchmenu`, and **every** exit path falls to the bare
epilogue (`.L7F0634B8` / `.L7F0634BC` → `lw $ra; jr $ra`, `7F0634B8-7F0634D4`).
There is **no** `jal sub_GAME_7F05DA8C` restore and **no** `jal
sub_GAME_7F05DAE4` anywhere in the body. Retail therefore leaves the highlighted
item pinned in the right hand while the menu is up (teardown happens elsewhere,
`bondview.c:10258/10347`).

The pre-fix port captured the previous item and restored it on every exit:

```c
prev_watchmenu_item = g_CurrentPlayer->hands[GUNRIGHT].weaponnum_watchmenu; /* 15821 */
sub_GAME_7F05DA8C(GUNRIGHT, weaponnum);
...
done:
    if ((s32)prev_watchmenu_item >= 0)
        sub_GAME_7F05DA8C(GUNRIGHT, prev_watchmenu_item);   /* phantom restore */
    else
        sub_GAME_7F05DAE4(GUNRIGHT);                        /* phantom clear   */
```

`sub_GAME_7F05DAE4` (gun.c:1815) resets `weaponnum_watchmenu = -1` and calls
`place_item_in_hand_swap_and_make_visible`, which overwrites the pending
hand-model request and toggles `hand_invisible`. Tree-wide, `jal
sub_GAME_7F05DAE4` appears in **zero** GLOBAL_ASM blocks — it is not part of any
retail body.

## Player-visible effect

The port reverts `hands[GUNRIGHT].weaponnum_watchmenu` within the same draw call,
so consumers outside this function — the bondview gun raise/lower model-swap
state machine (`bondview.c:10321-10372`, `:3144`) — see the *equipped* weapon
instead of the highlighted *menu* item, and the pending hand-model request
oscillates every frame while the menu is up.

## Provenance / class

Initial-transcription defect (blame `8de295b`), no fix rationale comment. The
retail teardown that the pinned item relies on already exists in the port
(`bondview.c`), so removing the phantom restore matches retail without leaving
the state un-torn-down. Classified port-defect (state trajectory diverges from
the ASM on valid data, undocumented). Sibling `sub_GAME_7F06359C` = FID-0072
has the same phantom pattern plus a wrong-hand (GUNLEFT) twist.

The rest of the function is verified equivalent to the ASM (renderdata template
`D_80035D00`; matrix alloc `numMatrices<<6`; revolver skeleton switch slots
4/5→mtxlist[3]/[4]; shell loop 5 iters +0x48/+0x5C; lights item set
{19,18,2,3,20,21}; PropType 4/5 alpha split; f32→s32 loop).

## Fix

Factored the exit lifecycle into a pure ROM-free decision
`watchMenuHandExitAction(prev_item, legacy)`
(`src/platform/watchmenu_hand_lifecycle.c`):

- faithful (`legacy == 0`): `WATCHMENU_HAND_EXIT_NONE` — leave the item pinned
  (retail: no restore call).
- legacy (`legacy != 0`): `prev_item >= 0 ? WATCHMENU_HAND_EXIT_RESTORE :
  WATCHMENU_HAND_EXIT_CLEAR` — reproduce the pre-fix port save/restore
  byte-identically for `GE007_NO_WATCHMENU_FOG_ITEM_FIX`.

The same helper is reused by FID-0072 (`sub_GAME_7F06359C`).

Regression lane: `tests/test_watchmenu_hand_lifecycle.c` (ctest
`watchmenu_hand_lifecycle`) — asserts faithful is `NONE` for every prev item
(-1, 0, 23, 60, …), legacy branches RESTORE/CLEAR on the sign of prev, and
faithful ≠ legacy (fails if the phantom restore is reintroduced).

## Status

Dormant on the combat/traverse golden tapes (the watch inventory page is not
opened on those routes), so `landed` is the terminal state reachable without the
ROM oracle; tape re-verification is blocked on the runtime lock.
