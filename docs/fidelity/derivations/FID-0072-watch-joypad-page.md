# FID-0072 — watch controller page: button-depress vector, hand slot, phantom lifecycle

**Function:** `sub_GAME_7F06359C` (watch controller-settings page 3D renderer),
VRAM `0x7F06359C`.
**Live C:** `src/game/gun.c:16398`.
**Authoritative ASM:** `src/game/gun.c:16812-17840` (GLOBAL_ASM body); helper
`sub_GAME_7F06351C` at `gun.c:16354`.
**Class:** port-defect → fix, default-ON behind `GE007_NO_WATCH_JOYPAD_FIX`.

Three independent divergences (all initial-transcription defects, blame
`8de295b`, no rationale comment).

## D1 — button-depress −10 offset added to the wrong translation vector

The page composes each button's matrix through `sub_GAME_7F06351C(pos, R_y, R_x,
I, rot, persp, out)` (`gun.c:16354`), which applies **T(pos) before** the Y/X
rotations and **T(rot) after** them:

```c
matrix_4x4_set_identity_and_position(arg0 /*pos*/, arg6);  /* T(pos) innermost */
matrix_4x4_multiply_in_place(arg1 /*R_y*/,  arg6);
matrix_4x4_multiply_in_place(arg2 /*R_x*/,  arg6);
matrix_4x4_multiply_in_place(arg3 /*I*/,    arg6);
matrix_4x4_set_identity_and_position(arg4 /*rot*/, &sp20);
matrix_4x4_multiply_in_place(&sp20,         arg6);         /* T(rot) applied after */
matrix_4x4_multiply_in_place(arg5 /*persp*/,arg6);
```

For the 7 face-button cases (`i` = 1,4,5,6,7,8,9), retail adds `f20 = -10.0`
(`gun.c:17063`, `li $at,0xC1200000`) to the **y of `pos` (arg0)** — the vector
copied from the static table `D_80035D44` — *before* the call. Case `i==4`
(anchor):

```
/* 7F063B30 */  D_80035D44+0x48 -> sp+0x190          # pos = table copy (all-zero)
/* 7F063B58 */  vertices fp+0x70.. -> sp+0x19c       # rot = vertices
/* 7F063B74 */  jal joyGetButtons  (mask 8)
/* 7F063B84 */  lwc1 $f16, 0x194($sp)                # pos.y  (sp+0x190 + 4)
/* 7F063B88 */  add.s $f18, $f16, $f20               # pos.y += -10
/* 7F063B8C */  swc1  $f18, 0x194($sp)
/* 7F063BA0 */  addiu $a0, $sp, 0x190                # arg0 = pos
/* 7F063B98 */  sw    $t8, 0x10($sp)  ($t8=sp+0x19c) # arg4 = rot
```

Verified for **all 7** cases — each stores the offset at its `pos.y` slot
(table-copy base + 4), never the `rot` slot:

| i | table  | pos base | offset store | mask |
|---|--------|----------|--------------|------|
| 4 | +0x48  | 0x190    | 0x194        | 8      |
| 5 | +0x54  | 0x178    | 0x17c        | 4      |
| 6 | +0x60  | 0x160    | 0x164        | 2      |
| 7 | +0x6C  | 0x148    | 0x14c        | 1      |
| 9 | +0x78  | 0x130    | 0x134        | 16384  |
| 8 | +0x84  | 0x118    | 0x11c        | 32768  |
| 1 | +0x90  | 0x090    | 0x094        | 4096   |

The pre-fix port added `neg_ten` to **`rotN.y` (arg4, the vertices-derived
vector)** instead (`gun.c:16627,16641,16655,16669,16683,16697,16751`). With the
`-pi/3` X-rotation on these cases, retail's offset contributes ≈(0,−5,−8.66) in
the post-rotation frame, vs the port's (0,−10,0): the pressed button travels a
visibly different direction/depth. `D_80035D44` is all zeros
(`gun.c:972-975`, only `[0]=1,[1]=3` belong to the preceding renderdata
template), so the −10 IS the entire `pos` contribution in both engines — the
difference is purely *which* vector carries it. (The port also reads a different
all-zero table index for some `posN`, e.g. C `pos1 = D_80035D44[42]` vs ASM
`[36]`; harmless since every entry is 0.)

## D2 — wrong hand slot (GUNLEFT instead of GUNRIGHT)

Retail drives hand **0 (GUNRIGHT)** throughout:

```
/* 7F06361C */  move $a0, $zero          # hand 0
/* 7F063620 */  li   $a1, 85             # 85 == ITEM_JOYPAD
/* 7F063624 */  jal  sub_GAME_7F05DA8C
/* 7F06362C */  jal  Gun_hand_without_item ; move $a0,$zero
/* 7F06363C */  jal  get_itemtype_in_hand  ; move $a0,$zero
/* 7F063658 */  itemheader = g_CurrentPlayer + 0x810  = &copy_of_body_obj_header[0]
```

`get_ptr_itemheader_in_hand(hand) = &copy_of_body_obj_header[hand]`
(`gun.c:1480`), so hand 0 == the retail-inlined `+0x810`. The pre-fix port
passed **GUNLEFT (1)** to every hand query (`gun.c:16458,16459,16461,16464,
16468,16480`), mutating the wrong hand's `weaponnum_watchmenu` / pending-model
state and fetching `copy_of_body_obj_header[1]`. `GUNRIGHT=0, GUNLEFT=1`
(`bondconstants.h:1345`). `ITEM_JOYPAD == 85` (`bondconstants.h`, index from
`ITEM_UNARMED=0`).

## D3 — phantom per-frame save/restore

Identical to FID-0073: retail's body has a single `jal sub_GAME_7F05DA8C`
(`7F063624`) and every exit is the bare epilogue — no restore, no
`sub_GAME_7F05DAE4`. The pre-fix port captured `hands[GUNLEFT].weaponnum_watchmenu`
(`gun.c:16458`) and restored/cleared it on every exit (`gun.c:16804-16807`).

## Fix

Two pure ROM-free helpers (`src/platform/watch_joypad_page.c`) plus reuse of the
FID-0073 exit-lifecycle helper:

- `watchJoypadPageHand(legacy)` → faithful `GUNRIGHT (0)`, legacy `GUNLEFT (1)`.
- `watchJoypadButtonDepress(&posN.y, &rotN.y, offset, legacy)` → faithful
  `*pos_y += offset` (D1 correct), legacy `*rot_y += offset` (pre-fix).
- `watchMenuHandExitAction(prev, legacy)` (shared, FID-0073) → faithful `NONE`,
  legacy `RESTORE/CLEAR` (D3).

`GE007_NO_WATCH_JOYPAD_FIX` (default-OFF ⇒ fix ON) reproduces all three pre-fix
behaviors byte-identically.

Regression lane: `tests/test_watch_joypad_page.c` (ctest `watch_joypad_page`) —
asserts the faithful hand is GUNRIGHT (legacy GUNLEFT), the faithful depress
moves `pos_y` while legacy moves `rot_y` (and never the other), and faithful ≠
legacy for hand + depress. Composes with `watchmenu_hand_lifecycle` for D3.

The rest of the function is verified equivalent to the ASM (renderdata template;
stick-rotation constants `D_80053ED4/ED8`; PropType `green>=255` split; the 12
case masks incl. the else-if chaining; subdraw + f32→s32 loops; `arg3==0`
gate + `Switches[13]` hide).

## Status

Dormant on the combat/traverse golden tapes (the watch controller page is not
opened on those routes), so `landed` is the terminal reachable without the ROM
oracle; tape re-verification is blocked on the runtime lock.
