# FID-0051 / FID-0052 — weapon firing-dispatch ASM derivation

Authority: retail ASM jump tables inlined in `src/game/gun.c`
(`glabel jpt_80053F24`, `glabel jpt_80054084`, `glabel jpt_weapon_bullet_type`).
ITEM ordinals from `src/bondconstants.h` (`enum ITEM_IDS`).

Charter authority hierarchy: retail ASM > ares > decomp C. The `#else` reference
bodies are not consulted as authority.

---

## FID-0052 — Automatic Shotgun (AUTOSHOT) mis-grouped in `jpt_weapon_bullet_type`  — LANDED

**Function:** `handles_firing_or_throwing_weapon_in_hand` (US), live C `src/game/gun.c:6127`.
**Dispatch:** `jpt_weapon_bullet_type[item-4]`, bounds `sltiu $at,$t6,0x14` (idx < 20),
guarded by `hp->weapon_firing_status != 0 && item ∈ [ITEM_WPPK(4), ITEM_WATCHLASER(23)]`.

**Handler bodies (ASM, gun.c:8705-8716):**
- `weapon_bullet_type_pistol`: `jal sub_GAME_7F061BF4` then `lw 0x30($s0); addiu +1; sw` — i.e. `sub_GAME_7F061BF4(hand); field_8A0++`, then common tail.
- `weapon_bullet_type_none`: `jal sub_GAME_7F061BF4`, then falls through to the tail (no increment).
- `weapon_bullet_type_shotgun_mine`: tail only — **neither** call nor increment.

**Jump table (`jpt_weapon_bullet_type`, gun.c:7076; idx = item-4):**

| idx | item | ordinal | handler |
|----:|------|--------:|---------|
| 0–10 | WPPK..FNP90 | 4–14 | pistol |
| 11 | SHOTGUN | 15 | **shotgun_mine** |
| 12 | AUTOSHOT | 16 | **shotgun_mine** |
| 13–17 | SNIPERRIFLE..GOLDWPPK | 17–21 | pistol |
| 18 | LASER | 22 | none |
| 19 | WATCHLASER | 23 | none |

**Divergence:** the live C `switch(item)` (gun.c:6961) put `ITEM_AUTOSHOT` in the
pistol case (`sub_GAME_7F061BF4(hand); hp->field_8A0++`). Retail routes AUTOSHOT to
`weapon_bullet_type_shotgun_mine` — no pre-tail action — exactly like the pump
`ITEM_SHOTGUN` (idx 11). The port therefore applied pistol recoil
(`sub_GAME_7F061BF4`) and the per-shot counter (`field_8A0++`) to the Automatic
Shotgun that retail does not.

**Reachability:** AUTOSHOT (Automatic Shotgun) is a standard equippable/fireable
weapon (solo + MP). Its STATE-1/STATE-2 routing (`jpt_80053F24[15]`=guns → state 2,
`jpt_80054084[14]`=pistol) latches `weapon_firing_status` on every shot, so this
function runs with `item==ITEM_AUTOSHOT` in the guarded range each time it fires.
The mis-routing is player-visible (extra recoil + shot-count on the auto-shotgun).

**Fix:** `weaponBulletTypeClassify()` (`src/platform/weapon_bullet_type.c`) encodes
the retail table; AUTOSHOT → `SHOTGUN_MINE` at the faithful default. Negative
control `GE007_NO_AUTOSHOT_BULLETTYPE_FIX` restores the buggy pistol grouping
(byte-identical to the pre-fix port). Regression lane:
`tests/test_weapon_bullet_type.c` (CTest `weapon_bullet_type`).

---

## FID-0051 — `jpt_80054084` STATE-2 groupings: ROCKETLAUNCH/REMOTEMINE/PLASTIQUE  — WAIVED (entangled with STATE-1)

**Function:** `handle_weapon_id_values_possibly_1st_person_animation` (US).
Two switches on `when_detonating_mines_is_0` (gunstate):
STATE-1 `switch(weapon_id-1)` = `jpt_80053F24[weapon_id-1]` (Weapon_function_*),
STATE-2 `switch(weapon_id-2)` = `jpt_80054084[weapon_id-2]` (Weapon_shooting_*).

**Retail routing for the three suspects (both tables):**

| weapon | id | STATE-1 (`jpt_80053F24`) | STATE-2 (`jpt_80054084`) |
|--------|---:|--------------------------|--------------------------|
| ROCKETLAUNCH | 25 | `Weapon_function_guns` (→ state 2) | `Weapon_shooting_pistol` |
| REMOTEMINE | 29 | `Weapon_function_throwable_item` (→ throw) | `Weapon_shooting_throwable` |
| PLASTIQUE | 34 | `Weapon_function_throwable_item` (→ throw) | `Weapon_shooting_throwable` |

FID-0051 flagged only the STATE-2 column. But the **STATE-1** column in the live C
also diverges, and the two divergences currently compensate to keep each weapon
functional. A STATE-2-only fix is therefore either dead code or a regression:

- **ROCKETLAUNCH — STATE-2 fix is dead code.** The live C STATE-1 (gun.c:18152)
  gives ROCKETLAUNCH a bespoke charge case that sets `when_detonating_mines_is_0 = 26`,
  **never 2**. State 26/27 (gun.c:19307-19341) latch `weapon_firing_status` and fire
  via the tail `gunFireTankShell(hand)` (gun.c:7009-7010). ROCKETLAUNCH never reaches
  the STATE-2 switch with state==2, so adding it to the STATE-2 pistol group would
  never execute. (The real divergence is the bespoke STATE-1 charge vs retail
  `guns`→state 2 — a separate, larger finding.)

- **REMOTEMINE / PLASTIQUE — STATE-2-only fix regresses the weapon.** The live C
  STATE-1 puts both in the `guns` group (gun.c:18108/18112) → state 2, and STATE-2 =
  pistol latches `weapon_firing_status`. The throw itself happens in
  `handles_firing_or_throwing_weapon_in_hand` tail, gated purely on
  `weapon_firing_status != 0`: `generate_player_thrown_object(hand)` for
  REMOTEMINE/PLASTIQUE (gun.c:7013-7017). Removing them from the STATE-2 pistol case
  (→ default "throwable: do nothing") would stop `weapon_firing_status` from ever
  latching, so the mine/plastique could **never be thrown** — a regression of a
  user-confirmed working feature (and of the FID-0016 remote-mine detonator path).

**Faithful fix requires a coupled STATE-1 + STATE-2 rework:** route REMOTEMINE and
PLASTIQUE through `Weapon_function_throwable_item` → the state-28 mine-throw arc
(as PROXIMITYMINE already is) *and* the STATE-2 throwable group; and replace
ROCKETLAUNCH's bespoke STATE-1 charge with `guns`→state 2 + STATE-2 pistol. This
changes throw/fire animations and cadence and cannot be validated headlessly
against stock without an oracle capture of retail REMOTEMINE/PLASTIQUE throw +
ROCKETLAUNCH fire. Per charter rules 3–4 (evidence monopoly / oracle-verify both
sides) and rule 10 (escalate rather than guess), the narrow STATE-2 fix is not
landable as scoped.

**Waiver retest:** re-open when a coupled `jpt_80053F24`+`jpt_80054084` dispatch
rework is scoped with ares oracle captures of stock REMOTEMINE/PLASTIQUE throw and
ROCKETLAUNCH fire behavior.
