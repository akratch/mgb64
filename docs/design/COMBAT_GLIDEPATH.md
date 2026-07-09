# Combat System Glidepath to "Done"

Status owner: combat lifecycle (enemy engagement, activation/detection, guard
shooting & accuracy, Bond auto-aim, Bond fire pipeline, hit/damage application,
projectiles/explosions, determinism).

This document is the end-to-end plan to take the native port's combat system from
"substantially accurate, exercised end-to-end" to **provably gameplay-accurate vs.
the original ROM**, backed by the orchestration and instrumentation passes that
have already been executed. It is written so any contributor can reproduce the
evidence and continue the remaining phases.

It is the combat-specific counterpart to [ROM_COMPARISON.md](../ROM_COMPARISON.md)
(the oracle mechanism) and [INSTRUMENTATION.md](../INSTRUMENTATION.md) (the trace
contract and env knobs). Read those first.

> **Definition of "done" for combat:** every lifecycle stage runs the genuine N64
> logic, and that claim is backed by (a) a deterministic native trace, (b) a
> stock-ROM ares oracle trace of the same scenario, and (c) a comparator that
> passes within tolerance — across a representative scenario matrix, with the two
> known reimplementations (`chrTickBeams`, the `stan.c` floor/LOS primitives)
> either proven equivalent by differential test or matched/decompiled. Until then
> combat is "accurate in steady-state play, conditional on the items in Phase 3."

---

## 1. Current state (evidence-backed)

Two prior studies established the architecture and the source-level risk surface:

- **Adversarial source review** (76-agent pass): the full lifecycle compiles the
  genuine decompiled C in the native build (`NONMATCHING + NATIVE_PORT +
  PORT_FIXME_STUBS + VERSION_US + REFRESH_NTSC`); `GLOBAL_ASM()` is empty
  (`include/ultra64.h:30`), so native runs the C siblings. No active combat stubs.
  Residual risk = two from-scratch reimplementations + a stall-only timer clamp.
- **Instruction-grounded ASM-equivalence study** (see §3): pinned down exactly
  where those reimplementations diverge from the original MIPS.

The orchestration/instrumentation passes below were then **executed** (read-only,
no code edits) on Dam (`--level 33`) using the existing harness. All artifacts
are local under `/tmp/mgb64_combat/` and `/tmp/mgb64_movement_oracle_*`.

### 1.1 What the native port already provides

- **A complete combat trace contract** (`--trace-state`, emitted by
  `src/platform/port_trace.c`): `combat{aim_mode,gunsightmode,autoaim_opt,
  enabled[],autoaim[],crosshair[],gunsight[],autoaim_time[],target_same}`,
  `health{bond,armor,actual_h,actual_a,damage_show,health_show,...}`,
  `shots{total,regs[7]}`, `casings`, `scan.nearest{type,chrnum,alive,action,
  alert,firecount,damage,maxdamage,dist,vis{line_clear,same_stan,could_see_bond,
  line_clear_world,line_clear_solid}}`, `target_x`/`target_y` (full autoaim-target
  identity + AI list/cmd + seen/heard age + LOS), and per-guard state via
  `GE007_TRACE_CHRNUM=N`.
- **A complete deterministic combat-scripting harness** (`GE007_AUTO_*`):
  `WARP_CHR`, `SET_CHR_AI`, `FIRE`, `AIM`, `AIM_DIR`, `AIMOVE_CHR`, `AIREACT_CHR`,
  `ATTACKROLL_CHR`, `DAMAGE_BOND`, `DAMAGE_CHR`, `DAMAGE_TAG`, `FACE_CHR`,
  `CHR_TO_PAD` (+ `_SCRIPT` multi-event variants).
- **A working ROM-vs-native oracle** (instrumented ares reading stock RDRAM).

### 1.2 Passes executed and results

| Pass | Command (abridged) | Result |
|---|---|---|
| Trace pipeline + guard census | `--level 33 --trace-state` | ✅ 36 guards; full combat schema emits |
| **Determinism** | probe A vs identical rerun A2 | ✅ **byte-for-byte identical** traces |
| **Bond auto-aim** | `WARP_CHR(d=250)` + `FIRE=45:330`, `TRACE_CHRNUM=0` | ✅ target acquired f22; nudge `autoaim=[0.278,0.436]` pulling to target |
| **Bond fire → hit → guard damage** | same probe | ✅ `shots.total` 0→12; traced guard `damage` 0→4.0; action transitions |
| **Guard AI engagement** | `WARP_CHR` + `SET_CHR_AI(list=13)` | ✅ detect → GOTO (action 15/8) → BFS pathing (`GOPOS_TICK`) |
| **Guard fire → Bond damage** | `SET_CHR_AI(list=13)`, 700 frames | ✅ `firecount` 0→53; **Bond `health` 1.0→0.938** (live `shotbondsum`) |
| **Bond damage application** | `tools/damage_hud_smoke.sh --level 33` | ✅ PASS (health/armor rings, `damage_show`, render-health) |
| Native ROM-oracle route harness | `tools/movement_oracle_capture.sh ... --native-only` | ✅ PASS |
| **Stock ROM vs native (ares)** | `movement_oracle_capture.sh ... --ares-bin ...` | ✅ **MATCH, 60 frames, `max_abs move.speed=0.00000`** |

**Conclusions from the executed passes**

1. Every lifecycle stage is live and behaves correctly in the common case:
   detection → AI engagement → guard fire → Bond damage, and Bond auto-aim →
   fire → hit → guard damage. The probabilistic `shotbondsum` model
   (`chrlv.c:7785-7858`) demonstrably damages Bond.
2. The combat path is **deterministic** — the prerequisite for any oracle
   comparison. Identical scripted inputs produce identical traces.
3. The **ROM-vs-native oracle pipeline works in this environment** and matches on
   movement with zero divergence. Extending it to combat is incremental.
4. Self-corrected false alarm: a SIGSEGV from forcing AI list `6`
   (`GAILIST_ATTACK_BOND`) is documented CALL-list misuse (*"if no return list
   set, will crash"*), **not** a port bug. Use top-level lists (e.g. `13`
   `PERSISTENTLY_CHASE_AND_ATTACK_BOND`).
5. Trace-contract note for the oracle: `scan.could_see_bond` is **Bond's** autoaim
   scan (his forward view), not the firing guard's LOS — encode this so a combat
   comparator does not treat it as the guard's sight test.

### 1.3 Per-subsystem status going into the glidepath

| Subsystem | Source verdict | Runtime evidence | Oracle-proven? |
|---|---|---|---|
| Activation / detection | faithful | engagement triggers, AI escalates | not yet (Phase 1/2) |
| AI engagement (bytecode) | faithful | list dispatch, GOTO/pathing observed | not yet |
| Guard shooting + accuracy + damage | faithful | firecount→Bond health drop | not yet |
| Bond auto-aim | faithful | acquisition + nudge observed | not yet |
| Bond fire pipeline | faithful | shots/casings/guard-damage | not yet |
| Hit / damage application | faithful | damage_hud PASS; guard & Bond damage | not yet |
| Projectiles / explosions | faithful (source) | **not yet exercised** | not yet |
| `chrTickBeams` (per-chr tick) | **divergent by design** | common case correct | §3 — proof/fix needed |
| `stan.c` floor/LOS primitives | **divergent** (2 of 4) | LOS works in play | §3 — proof/fix needed |
| Determinism / timing | faithful + clamp | byte-identical reruns | clamp is policy decision |

---

## 2. The gap to 100%

Three concrete gaps, in priority order:

1. **No combat ROM-oracle.** The oracle reads only movement/intro RDRAM fields.
   Combat fields exist natively but are not compared against the ROM. (Phase 1)
2. **Two reimplementations are unproven/divergent** against the original MIPS,
   with specific combat-relevant divergences now identified. (Phase 3, proved by
   Phase 2)
3. **Coverage is narrow.** Evidence is Dam-only, hitscan-only; projectiles/
   explosions, every weapon class, all stages, and MP combat are unexercised.
   (Phase 4)

---

## 3. ASM-equivalence findings (the precise divergence surface)

From the instruction-grounded C-vs-`GLOBAL_ASM` study. Severity is combat impact.

### 3.1 `chrTickBeams` (`chr.c:4917-5150` native subset; ASM original `5155-6002`) — **DIVERGENT BY DESIGN**

Combat-critical calls are **present and faithful for the common case** (non-hidden,
visible guard): `chrUpdateAimProperties` (C:4976 ≡ ASM 0x1400),
`chrDropPendingHeldItems` (C:4984, gated on `CHRHIDDEN_DROP_HELD_ITEMS`),
`chrlvTriggerFireWeapon` (C:4993). But the minimal reimpl diverges in specific
states:

| # | Divergence | Severity | Status |
|---|---|---|---|
| H1 | **Hidden guard still fires/drops.** ASM: a `CHRFLAG_HIDDEN` (0x400) guard returns with **no fire, no drop** (`.L7F021AAC`). Native ran `chrlvTriggerFireWeapon`/`chrDropPendingHeldItems` unconditionally → **phantom damage & phantom pickups from hidden guards**. | **HIGH** | ✅ **FIXED** chr.c — gated behind `!(chrflags & CHRFLAG_HIDDEN)` |
| H2 | **Early hidden-action gate missing.** ASM skips `chrlvActionTick`/anim when `(HIDDEN && !0x40000)` (US `7F020F64-78`); native always ticked. | **HIGH** | ✅ **FIXED** chr.c — AI-tick block wrapped in the same gate |
| H3 | **Freeze-derived timer dropped.** ASM passes a freeze-aware timer (`g_ClockTimer` when `D_8002C90C==0`, else `0`/`1`) to the position/anim fns; native always passes raw `g_ClockTimer` (C:4961). | LOW (was HIGH) | **DEFERRED** — `D_8002C90C`/`D_8002C910` are debug toggles (`chrToggleD_8002C90C`, default 0). No divergence in normal play; pure debug-freeze parity. |
| H4 | **Actiontype position-dispatch collapsed.** ASM dispatches among `chrPositionRelated7F020E40`, `…7F020D94` (act 14/15 floor path), `modelTickAnimQuarterSpeed` by `actiontype`, **with the frustum test `sub_GAME_7F054D6C` interleaved into every arm**; native uses one path. | **HIGH** | **DEFERRED** — entangled with the uncalibrated PC frustum (H7). Reproducing it faithfully needs PC frustum calibration; doing it blind would regress the validated common-case position path. This is the "durable rewrite" tier. |
| H5 | `CHRHIDDEN_BACKGROUND_AI` (0x200) set/clear divergence. **Corrected from the prior study:** my ASM read of `.L7F0213C8` shows the original SETS 0x200 in **all** cases **except** `actiontype==ACT_STAND(1) && model->0x54==0 && prop->type!=6` (the prior summary had this inverted). | MEDIUM | **DEFERRED** — live downstream reader at `chrobjhandler.c:44375` + clearer at `chrlv.c:11079`; adding the set changes that reader's branch and needs validated downstream analysis. |
| H6 | `objFreePermanently` for held weapons with `obj->flags&4` (`.L7F02106C`/`.L7F021094`) dropped → object leak. | MEDIUM | **DEFERRED** — access is through the `PropRecord` union (`weapons_held[N]->{obj}->flags`); the exact native union field for the obj-flags read is unverified, and freeing the wrong object is worse than a rare leak. |
| H7 | Visibility gate coarsened to room-rendered vs frustum (**intentional**, C:5003-5004) → strictly larger set of guards ticked/rendered as visible. | MEDIUM | **DEFERRED (intentional)** — rendering-domain; needs `camIsPosInScreen` PC frustum calibration. Combat firing happens before the gate, so this is perf/render, not combat-correctness. |
| — | not-visible settle path (flag/colour) dropped; hat render; cheat distance-scale | LOW/cosmetic | DEFERRED (cosmetic) |

> **Off-by-one (`stan.c` tile-walk)** and **F1 (degenerate floor-tile skip)** are ✅ **FIXED** (see §3.2/§3.3 statuses below).

### 3.2 `sub_GAME_7F0AF20C` (closest-floor-tile, `stan.c:687-751`) — **DIVERGENT** (new finding)

| # | Divergence | Severity | Observable |
|---|---|---|---|
| F1 | **Degenerate-tile skip missing.** ASM calls `sub_GAME_7F0AF760` and skips zero-area/collinear tiles; native never did → could select a degenerate tile. | **HIGH** near seams | ✅ **FIXED** stan.c — `sub_GAME_7F0AF760(tile)==0` gate added |
| F2 | **On-edge disambiguation missing.** ASM, when query is within ±2u of an edge, walks tile-midpoint→point and rejects the tile on failure; native accepts on its (different, simpler) bounds test alone → wrong neighbor at seams. | **HIGH** near seams | **DEFERRED** — requires replacing the native bounds test with the canonical 3-edge `getShortest2dDispToInfTripleEdge` ± `walkTilesBetweenPoints_NoCallback` walk-back (canonical decomp is in-file at `stan.c:755-864`). A standalone rewrite of critical floor detection; do under its own validated change. |

Affects guard (and potentially Bond) floor placement near tile seams / stacked
floors (catwalks). Clean single-tile floors match. F1 (the simpler, strictly-correct
half) is landed; F2 (the bounds-test rewrite) is the larger follow-up.

### 3.3 `sub_GAME_7F0B0914` (tile-walk LOS / bullet-block, `stan.c:3193-3337`) — **NEAR-EQUIVALENT**

One defect: **iteration-cap off-by-one** (C:3294-3297 vs ASM:3499-3507). Native
aborted a tile-walk at 500 continuing steps; ASM permits 501. ✅ **FIXED** stan.c —
the cap test now reads the pre-increment counter (continue while `< 0x1F5`, fail at
`== 0x1F5`), and the increment moved to the continue path (loop termination
preserved). Everything else (cross-product crossing test, 3-tile backtrack history,
callback order, boundary early-return, `level_scale`) already matched.

### 3.4 `sub_GAME_7F0B07BC` (segment-intersect, `stan.c:2979-3027`) — **CONFIRMED EXACT MATCH**

Bit-for-bit equivalent to ASM (same six diff vectors, two
`getRotationalDirectionBetween` products, clamps). Use as a regression guard, not
a suspect.

---

## 4. The glidepath (phases with exit criteria)

Phases 0–2 are **read-only / instrumentation** (no game-code edits). Phases 3+
involve code edits and are specified here but intentionally **not executed** under
the current "no code edits" constraint.

### Phase 0 — Foundations — ✅ DONE

- Native combat trace contract exists and emits the full lifecycle.
- Deterministic combat scripting harness exists and is byte-reproducible.
- ROM-vs-native oracle pipeline is operational and matches on movement.

**Exit criteria (met):** a scripted engagement produces identical traces across
runs; a movement route matches the stock ROM within tolerance.

### Phase 1 — Combat ROM-oracle extension — read-only, **next up**

Mirror, on the ares side, the combat fields the native port already emits, then
add combat routes and a combat comparator. This reuses the entire movement-oracle
mechanism; only the read set, routes, and comparator profile are new.

**1a. Extend the ares RDRAM hook** with combat fields. The source of truth for
*what* to read is `port_trace.c` (native already reads each field). Resolve
addresses the same way movement does (symbol base + struct offset). Concrete set:

- From `g_CurrentPlayer` (base in [ROM_COMPARISON.md] symbol table, `0x8008a0b0`):
  `bondhealth 0x00dc`, `bondarmour 0x00e0`, `insightaimmode 0x0124`,
  `autoaimy 0x012c`, `autoaim_target_y 0x0130`, `autoaimx 0x013c`,
  `autoaim_target_x 0x0140`, `autoxaimtime60 0x0144`, plus `crosshair_x_pos/
  crosshair_y_pos` and `gunsightmode` (offsets from `bondview.h` player struct);
  shots counters.
- Per guard from `g_ChrSlots` (`ChrRecord *`, dynamically allocated — read the
  pointer, then index by `sizeof(ChrRecord)`) bounded by `g_NumChrSlots`:
  `accuracyrating`, `shotbondsum`, `damage`, `maxdamage`, `firecount`,
  `actiontype`, `alertness`, `chrflags`, `chrhidden`, `prop->pos`, `ground`.
  Field offsets: resolve from the `ChrRecord` definition / `port_trace.c` readers.
- New symbol-layout entries to add alongside the existing table:
  `MGB64_ARES_CHR_SLOTS` (`g_ChrSlots` pointer), `MGB64_ARES_NUM_CHR_SLOTS`
  (`g_NumChrSlots`), and the freeze globals `D_8002C90C`/`D_8002C910` (needed for
  H3 tests).

**1b. Add combat route specs** under `tools/rom_oracle_routes/` using the existing
`mgb64.rom_oracle.route.v1` schema, with combat events expressed through
`native_env` (the `GE007_AUTO_*` knobs) and matching stock boot/menu automation.
Seed routes (all deterministic, all on stages with a known early guard):

| Route | Native setup | Proves |
|---|---|---|
| `dam_autoaim_acquire` | `WARP_CHR(d=250,a=0)` + `FIRE=45:330`, `TRACE_CHRNUM=0` | auto-aim acquisition + nudge + Bond fire + guard damage |
| `dam_guard_fires` | `WARP_CHR(d=200)` + `SET_CHR_AI(list=13)`, 700 frames | guard fire cadence + `shotbondsum`→Bond health |
| `dam_bond_damage` | `DAMAGE_BOND` window (as `damage_hud_smoke`) | armor-first/health-overflow split |
| `dam_los_short` | short guard sighting | tile-walk LOS common case (3.3 test 4) |

**1c. Add a combat comparator** (`tools/compare_combat_trace.py`) aligning by
`move.global` (proven deterministic), with a `combat` profile and tolerances:
`autoaim[]`/`crosshair[]` 0.005; `health`/`armor` 0.001; `shotbondsum` 0.005;
`firecount`/`shots.total`/`action`/`alert` exact; `accuracyrating` exact. Wire it
into `movement_oracle_capture.sh` behind `compare_kind: "combat"`. Emit
`compare_<route>.json` like the movement lane.

**Exit criteria:** at least the four seed routes pass ROM-vs-native within
tolerance, with stock traces reporting `p:1` and the control audit clean. A
deliberately mismatched value (e.g. forcing `g_AiAccuracyModifier`) fails the lane.

### Phase 2 — Differential equivalence proofs — read-only

Run the targeted differential tests from §3 through the Phase-1 combat oracle.
Ordered cheapest-first (matches the study's proof plan):

1. **Retire `sub_GAME_7F0B07BC`** — confirmed exact match; add `dam_los_short` as
   a standing regression guard.
2. **Prove `chrTickBeams` common case bit-identical** — ordinary non-hidden,
   visible, in-frustum guard firing at Bond: `firecount`, Bond `health`,
   `pos` match the oracle every frame.
3. **`stan.c` tile-walk off-by-one** — construct the 499/500/501/502-crossing
   corridor (large map, near-collinear long ray); confirm divergence appears only
   at 501 and is otherwise identical. (Severity LOW → likely accept-and-document.)
4. **`chrTickBeams` HIGH states** — the four targeted routes:
   - H1 hidden-guard-fires: `CHRFLAG_HIDDEN` set, in firing range → expect oracle
     `firecount`=0/no Bond damage; native fires. (If confirmed, real fix needed.)
   - H3 freeze: drive a pause/freeze (`D_8002C90C|D_8002C910==0`) with a patrol
     guard → oracle holds pos/anim; native advances.
   - H4 patrol floor-Y: `ACT_PATROL`/`ACT_GOPOS` across distinct tiles → compare
     `ground`/`pos.y`.
   - H2 hidden-AI-tick: hidden-without-0x40000 guard → compare `action`/`alert`.
5. **`sub_GAME_7F0AF20C` floor-tile** — degenerate-tile and seam/stacked-floor
   probes → compare selected tile id + floor-Y near seams.
6. **`chrTickBeams` MEDIUM** — `hidden_bits & 0x200` for ACT_STAND; object-count
   leak (needs an object-count trace field, small native addition deferred to
   Phase 3 tooling).

**Exit criteria:** each divergence is either (a) proven unreachable/within
tolerance in real play and accepted with a documented rationale, or (b) confirmed
reproducible and promoted to a Phase-3 fix with a failing regression route.

### Phase 3 — Close the confirmed divergences — **requires code edits (specified, not executed)**

Only the items Phase 2 confirms reproducible and gameplay-relevant. Likely set,
ranked:

1. **`chrTickBeams` H1 (hidden guard fires/drops)** — gate `chrlvTriggerFireWeapon`
   / `chrDropPendingHeldItems` behind the same `CHRFLAG_HIDDEN` early-return the
   ASM uses. Highest correctness value (phantom damage). Small, local.
2. **`chrTickBeams` H2/H3/H4** — restore the hidden-action gate, the freeze-derived
   timer arg, and the actiontype-driven position dispatch. Medium effort; ideally
   reached by matching the original `chrTickBeams` (the durable fix — see step 5).
3. **`sub_GAME_7F0AF20C` F1/F2** — port the degenerate-tile skip
   (`sub_GAME_7F0AF760`) and the on-edge midpoint walk-back. Medium; improves
   floor-Y fidelity near seams.
4. **`stan.c` tile-walk off-by-one** — one-line: pre-increment compare to match
   `old < 0x1F5`. Trivial; do it for exactness even though impact is negligible.
5. **Durable option:** decompile/ASM-match the full original `chrTickBeams` and
   gate native behind it, removing the minimal-subset risk class entirely. This is
   the "good first issue / N64-ROM-build" axis and the only way to retire H1–H7 at
   once.

**Exit criteria:** each fix flips its Phase-2 regression route from divergent to
MATCH, and the common-case routes stay green.

### Phase 4 — Coverage expansion — read-only (uses Phases 1–2)

- **Weapon matrix:** repeat `dam_autoaim_acquire` / `dam_guard_fires` per weapon
  class — pistol, auto (KF7/AR33), shotgun/auto-shotgun (the `accuracy*=2`/`*3`
  branches in `chrlv.c:7832-7846`), magnum, sniper (insight aim), throwing knives,
  dual-wield.
- **Projectiles/explosions** (currently unexercised): grenade throw/fuse/bounce,
  grenade launcher/rockets, proximity/remote/timed mines, splash radius/falloff/
  occlusion, chain reactions. Add `combat`-profile fields for active
  projectiles/explosions if the native trace doesn't already expose them.
- **Stage matrix:** run the seed routes across the 20 solo stages (different stan
  geometry stresses §3.2/§3.3 differently). Reuse `playability_smoke.sh --all`
  composition.
- **Multiplayer combat:** extend `mp_smoke.sh` with a scripted 2P fight; assert
  symmetric damage application and per-player auto-aim.
- **Soak:** long deterministic combat run (`soak_stability.sh`-style) with guards
  attacking, asserting zero crashes / render-health clean and bounded object
  counts (catches H6-class leaks).

**Exit criteria:** the weapon × stage matrix passes the combat oracle within
tolerance; projectiles/explosions have at least one passing oracle route each; MP
combat smoke green; combat soak crash-free.

### Phase 5 — Sign-off

- A single `tools/combat_oracle_suite.sh` runs the full route matrix + differential
  guards and emits a `summary.json` (mirrors the movement/intro lanes).
- The §1.3 table flips every row to **oracle-proven**, or carries an explicit,
  documented accepted-divergence rationale (e.g. the timer clamp, the off-by-one).
- This document's risk register (§6) is empty or all-accepted.

---

## 5. Open decisions (need a human call)

1. **`g_ClockTimer ≤ 4` clamp** (`lvl.c:2030-2045`): keep the bounded stall-only
   mitigation, or implement sub-stepping/catch-up that preserves N64 integration?
   RAMROM replay already bypasses it. Recommend: keep + document as accepted
   divergence; revisit only if a real >15fps-equivalent stall is observed in play.
2. **`chrTickBeams` strategy:** patch the confirmed HIGH divergences individually
   (fast, leaves the subset), or invest in a full ASM-match (slow, retires the
   whole risk class). Recommend: patch H1 now (correctness), schedule the full
   match as the durable fix.
3. **Off-by-one and cosmetic gaps:** fix for exactness, or accept-and-document?
   Recommend: fix the one-line off-by-one; accept cosmetic (hat/LOD/colour-settle)
   with a note.

---

## 6. Risk register (ranked)

| Risk | Severity | Status |
|---|---|---|
| `chrTickBeams` hidden-guard phantom fire/drop (H1) | HIGH | ✅ **FIXED + validated** (chr.c) |
| `chrTickBeams` hidden-AI tick (H2) | HIGH | ✅ **FIXED + validated** (chr.c) |
| `sub_GAME_7F0AF20C` degenerate floor tile (F1) | MED-HIGH | ✅ **FIXED + validated** (stan.c) |
| `stan.c` tile-walk off-by-one | LOW | ✅ **FIXED + validated** (stan.c) |
| `chrTickBeams` patrol/anim floor-Y drift (H4) | HIGH | DEFERRED — frustum-entangled; durable-rewrite tier |
| `sub_GAME_7F0AF20C` seam disambiguation (F2) | MED-HIGH | DEFERRED — needs canonical bounds-test rewrite |
| `chrTickBeams` BACKGROUND_AI bit (H5) | MEDIUM | DEFERRED — live downstream reader; analysis inverted in prior study, corrected here |
| `chrTickBeams` object-leak (H6) | MEDIUM | DEFERRED — uncertain union field mapping |
| No combat ROM-oracle yet | MEDIUM | being built (Phase 1) |
| Projectiles/explosions unexercised | MEDIUM | not started (Phase 4) |
| `chrTickBeams` freeze-timer (H3) | LOW | DEFERRED — debug-toggle only, no normal-play effect |
| `chrTickBeams` visibility coarsening (H7) | LOW/render | DEFERRED — intentional; needs PC frustum calibration |
| `g_ClockTimer` clamp | LOW | proven, bounded — §5 decision |

### Fixes landed (this pass)

Four ASM-faithful fixes in `src/game/chr.c` + `src/game/stan.c` (the ADS work in
`bondview.c`/`ads_*`/`gun.c` is a separate concurrent effort, untouched here):

- **H1** — drop + fire gated behind `!(chr->chrflags & CHRFLAG_HIDDEN)` (matches
  ASM `.L7F021AAC`). Eliminates phantom damage/pickups from hidden guards.
- **H2** — AI-decision tick gated for `(HIDDEN && !CHRFLAG_00040000)` (matches
  ASM `7F020F64-78`).
- **stan off-by-one** — tile-walk iteration cap now tests the pre-increment
  counter (501 steps), increment moved to the continue path.
- **F1** — `sub_GAME_7F0AF20C` skips degenerate tiles via `sub_GAME_7F0AF760==0`.

**Validation (all green):** clean build; 2-agent adversarial review vs the MIPS
original returned *correct, zero concerns*; post-fix combat probe A **byte-for-byte
identical** to pre-fix (common path untouched); guard-fire probe B3 identical
(Bond health 1.0→0.9375, firecount 53); spawn-health (Dam/Facility/Surface1),
playability (Dam/Surface1), damage-HUD all pass; 3-stage × 2999-frame soak
render-health clean, 0 crashes; ares ROM-oracle still `MATCH` with
`max_abs move.speed=0.00000` (no movement-parity regression from the floor-tile
change). A dedicated hidden-guard runtime repro needs a new `GE007_AUTO` hook to
set `CHRFLAG_HIDDEN` (no such hook exists yet); H1 is otherwise proven by exact
ASM-match + byte-identical common path.

---

## 7. Appendix — reproducible recipes

Shared env: `SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_NO_VSYNC=1
GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 GE007_DETERMINISTIC_STABLE_COUNT=1`.
Binary `build/ge007`, ROM `baserom.u.z64`. All outputs are ROM-derived; keep local.

```sh
# Auto-aim acquire + Bond fire kills guard (lifecycle: aim→fire→hit→damage)
env $SHARED GE007_AUTO_WARP_CHR_FRAME=20 GE007_AUTO_WARP_CHRNUM=0 \
  GE007_AUTO_WARP_CHR_DISTANCE=250 GE007_AUTO_WARP_CHR_ANGLE=0 \
  GE007_AUTO_FIRE=45:330 GE007_TRACE_CHRNUM=0 \
  build/ge007 --rom baserom.u.z64 --level 33 --deterministic \
  --trace-state A.jsonl --screenshot-frame 300 --screenshot-label A --screenshot-exit

# Guard persistently attacks stationary Bond (lifecycle: detect→engage→fire→Bond damage)
env $SHARED GE007_AUTO_WARP_CHR_FRAME=20 GE007_AUTO_WARP_CHRNUM=0 \
  GE007_AUTO_WARP_CHR_DISTANCE=200 GE007_AUTO_WARP_CHR_ANGLE=0 \
  GE007_AUTO_SET_CHR_AI_FRAME=30 GE007_AUTO_SET_CHR_AI_CHRNUM=0 GE007_AUTO_SET_CHR_AI_LIST=13 \
  GE007_TRACE_CHRNUM=0 \
  build/ge007 --rom baserom.u.z64 --level 33 --deterministic \
  --trace-state B.jsonl --screenshot-frame 700 --screenshot-label B --screenshot-exit

# Bond damage application (proven lane)
tools/damage_hud_smoke.sh --level 33 --no-build

# ROM-vs-native oracle (movement template the combat oracle extends)
tools/movement_oracle_capture.sh --route dam_forward_stop --rom baserom.u.z64 --no-build \
  --ares-bin build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares
```

**Top-level AI lists** for `GE007_AUTO_SET_CHR_AI_LIST` (do NOT use CALL/subroutine
lists like `6`=`ATTACK_BOND`, `8`=`RUN_TO_BOND`, which crash without a return list):
`2`=`STANDARD_GUARD`, `13`=`PERSISTENTLY_CHASE_AND_ATTACK_BOND`.

**Guard accuracy/damage reference** (`chrlv.c:7785-7858`): per-tick
`shotbondsum += 0.16*g_GlobalTimerDelta`, scaled by distance (>300u), `accuracyrating`
(`-100..+…`), difficulty (`get_007_accuracy_mod`/`damage_mod`),
`g_AiAccuracyModifier`/`DamageModifier`, weapon auto-rate (×2) and shotgun (×2/×3);
Bond takes `0.125*destructionAmount*…` when `shotbondsum ≥ 1.0`.
