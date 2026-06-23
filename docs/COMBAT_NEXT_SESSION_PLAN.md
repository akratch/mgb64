# Combat — Next-Session Execution Plan (high-value follow-ups)

Execution-ready scope for the highest-value combat follow-ups, sequenced for the
next session. Builds on the landed work (H1/H1b/H2/H3/H5/H6, off-by-one/F1/F2,
knife crash, TEX recovery, log gating, env-gate footgun fix) — see
[COMBAT_DEFERRED_PLAN.md](COMBAT_DEFERRED_PLAN.md) and
[COMBAT_GLIDEPATH.md](COMBAT_GLIDEPATH.md). Each item has file:line anchors,
ordered steps, a test, acceptance criteria, risks, effort, and a minimal vertical
slice. The holistic code review verdict for the landed work was **SOLID**; these
are the validation/infra gaps it identified, not correctness fixes.

## Executive summary & sequencing

The **combat-field ROM-oracle extension (Item 1)** is the keystone: it converts
H5/H4/F2/H7 from "ASM-faithful + in-port trace + screenshot" into **frame-exact
stock-vs-native parity**. It also produces the `selected-stan-tile`/`floor-Y`
trace field that **Item 2 (seam/F2 validation)** needs. **Item 3 (H7 MP frustum)**
and **Item 4 (test hooks)** are independent and can proceed in parallel without the
oracle.

**Recommended order:**
1. **Quick warm-ups** (S, ~30 min) — env-gate helper unify + the zero-risk cleanup bundle (§5). Clears review debt, no behavior risk.
2. **Item 4 — test hooks** (M) — closes the two biggest correctness test gaps (hidden-guard contract, knife regression); independent; high confidence-per-effort.
3. **Item 1 — combat oracle, vertical slice** (start of XL) — per-chr `pos.y`/`actiontype` on the ares side + one guard route + `compare_combat_trace.py`. Proves the pipeline before the full build-out.
4. **Item 2 — seam/F2 route** (L) — once Item 1 adds the `selected-stan-tile` trace field; decides accept-or-revert F2 default-ON.
5. **Item 3 — H7 MP per-player frustum** (M) — the functional fix is oracle-independent; frame-exact MP validation reuses Item 1.

**Dependency graph:** Item 1 (oracle) → provides trace field + comparator → Item 2 (seam/F2) and the frame-exact halves of Item 3/H5. Items 3-functional and 4 have no dependency. Warm-ups gate nothing.

**Resolve up front (blocks Item 1):** the stock-ROM addresses of `g_ChrSlots`
(ChrRecord* array base) and `g_NumChrSlots` — there is **no symbol map in-repo**;
get them from the matching-target ELF (`nm`) or the symbol table the ares hook
already uses (extend [ROM_COMPARISON.md](ROM_COMPARISON.md)'s table). Without
these the ares side can't read per-chr state.

---

## Item 1 — Combat-field ROM-oracle extension  (XL, keystone)

**Objective:** Extend the instrumented ares hook + native trace + comparator +
route schema so a scripted combat scenario can be compared frame-exact between the
stock ROM (in ares) and the native port — unblocking H5 (prox-mine detonation
frame), H4 (patrol floor-Y / anim cadence), H7 (visibility), F2 (floor-tile).

**Existing infra to reuse:**
- ares hook C++ (embedded heredoc) in `tools/prepare_ares_movement_oracle_build.sh`:
  `readF32/readU16/readU32` RDRAM helpers; `traceFrame()` ~L691; the JSONL
  `fprintf` block ~L954 which **already does per-chr reads** (`bond_chrnum`,
  `bond_action`, `bond_sleep`) for the intro — mirror that over guard slots.
  Address env parse pattern at ~L424 (`parseAddressEnv("MGB64_ARES_...")`).
- Native combat trace already emitted by `src/platform/port_trace.c`
  (`combat{...}`, `scan.nearest`, `target_x/y`, per-guard via `GE007_TRACE_CHRNUM`)
  — most fields exist; only `selected-stan-tile`/`floor-Y` is new.
- Comparator template `tools/compare_movement_trace.py` (align by `move.global`,
  profile + tolerances) → clone to `tools/compare_combat_trace.py`.
- Route schema `tools/rom_oracle_route.py` (`compare_kind`, `native_env`); wrapper
  `tools/movement_oracle_capture.sh`. Symbol table in `docs/ROM_COMPARISON.md`.
- ChrRecord layout `src/bondtypes.h` (chrnum@0x0, actiontype@0x7, sleep@0x8;
  prop->pos via PropRecord); `g_ChrSlots`/`g_NumChrSlots` at `src/game/chr.c:136-137`.

**Steps:**
1. Resolve `g_ChrSlots` + `g_NumChrSlots` stock addresses; add `MGB64_ARES_CHR_SLOTS`
   / `MGB64_ARES_NUM_CHR_SLOTS` to the ares hook's `parseAddressEnv` block and to
   `docs/ROM_COMPARISON.md`'s symbol table.
2. In the ares hook `traceFrame()`: read `g_ChrSlots` pointer, loop `g_NumChrSlots`
   (bounded), and per chr read `chrnum` (0x0), `actiontype` (0x7), `hidden`, `damage`,
   and `prop->pos.y` (chase the prop pointer like the existing bond read). Emit a
   `"chrs":[{chrnum,action,pos_y,hidden,damage},...]` array in the JSONL.
3. In `src/platform/port_trace.c`: confirm the same per-chr fields are emitted under
   a combat trace mode (extend `GE007_TRACE_CHRNUM` to optionally dump all guards),
   and **add a new `"floor":{"tile":<id>,"y":<f32>}`** field sourced from
   `prop->stan` + `stanGetPositionYValue` (needed by Item 2).
4. Clone `compare_movement_trace.py` → `compare_combat_trace.py`: align by
   `move.global`, add a `combat` profile comparing per-chr `pos_y`/`action` and the
   `floor` field with tolerances (pos 0.05, action exact).
5. Wire `compare_kind:"combat"` in `rom_oracle_route.py` + `movement_oracle_capture.sh`.
6. Author one combat route (`tools/rom_oracle_routes/dam_guard_floor.json`) that
   warps Bond near a guard and lets it patrol/approach (`native_env` GE007_AUTO_*).
7. (Expansion) add prox-mine timer table + explosion-event reads (for H5) and the
   anim-frame field (for H4) once the slice is proven.

**Test/validation:** run `movement_oracle_capture.sh --route dam_guard_floor
--ares-bin ... --rom ...`; the combat comparator reports per-chr `pos_y`/`action`
deltas stock-vs-native.

**Acceptance:** the slice (step 1-6) yields a green per-chr `pos_y`/`action` match
on the guard route, proving the pipeline end-to-end.

**Risks:** stock chr-array layout/offsets differ subtly from native struct
(validate offsets against the matching ELF); per-frame timing alignment for chrs
(reuse the proven `move.global` alignment); ares-side pointer chasing (guard NULLs).

**Effort:** XL overall; the vertical slice is M.

**Vertical slice:** ares reads `g_ChrSlots[i].{chrnum,actiontype}` + `prop->pos.y`
→ one guard route → `compare_combat_trace.py` reports a per-chr match. Proves the
whole combat-oracle approach before adding prox-mine/explosion/floor breadth.

**Open questions:** stock `g_ChrSlots`/`g_NumChrSlots` addresses; does the matching
ChrRecord/PropRecord layout match native field offsets (it should — native is the
decomp — but verify pos/stan offsets).

---

## Item 2 — Seam/stacked-floor route + F2 frame-exact validation  (L)

**Objective:** Validate F2's on-edge seam disambiguation frame-exact, then **accept
or revert the F2 default-ON decision** (it shipped default-ON without seam-positive
validation; all existing routes are flat-ground).

**Existing infra:** `src/game/stan.c` `sub_GAME_7F0AF20C` (F2, default-ON via
`GE007_STAN_ONEDGE`); route schema `tools/rom_oracle_routes/`; the `selected-stan-tile`/
`floor-Y` trace field from **Item 1 step 3** (hard dependency).

**Steps:**
1. Find a stacked/overlapping-floor location (two stan tiles overlapping in XZ at
   different Y) — a catwalk/bridge/ramp. Candidates: Dam elevated walkways, Facility/
   Archives catwalks. Confirm via a `GE007_STAN_ONEDGE` ON-vs-OFF floor-Y diff
   (the on_edge branch must actually fire there).
2. Author `tools/rom_oracle_routes/<level>_seam.json` walking Bond across the seam
   deterministically (`native_env` GE007_AUTO_* movement/warp).
3. Capture native (ON) vs stock; also native ON-vs-OFF to show the branch changes
   tile selection.
4. **Decision gate:** ON matches stock where OFF diverges → keep default-ON
   (validated). ON diverges from stock → revert F2 to default-OFF (opt-in) + fix.

**Test/validation:** `movement_oracle_capture.sh --route <seam> --ares-bin ...`
comparing the `floor` field (Item 1 step 3).

**Acceptance:** a definitive accept-or-revert verdict on F2 default-ON, backed by a
seam route where the on_edge branch demonstrably fires.

**Risks:** finding a location that reliably triggers the sticky on_edge worst case;
the sticky-across-rooms flag could surface a flat-ground regression elsewhere (run
the existing flat routes ON to confirm no drift).

**Effort:** L. **Dependency:** Item 1 (the floor trace field + combat comparator).

**Vertical slice:** the ON-vs-OFF native floor-Y diff at a hand-picked seam (no
ares needed) — proves the branch fires and shows the magnitude before investing in
the stock comparison.

---

## Item 3 — H7 split-screen per-player frustum union (M1b)  (M)

**Objective:** Make H7 frustum visibility correct in 2P split-screen so it can
eventually default-ON for MP. Today `sub_GAME_7F054D6C` tests only the **current**
player's camera; in 2P it culls chrs visible to the other player (probe: 75.9%
one-directional disagree).

**Existing infra:** H7 gate `src/game/chr.c:~5072` (chrTickBeams); the frustum test
`src/game/chrobjhandler.c:47205` (`sub_GAME_7F054D6C` → `camIsPosInScreen`);
`g_CurrentPlayer`/`g_playerPointers`; `tools/mp_smoke.sh`; the `GE007_TRACE_VISIBILITY`
probe (chr.c ~5097).

**Steps:**
1. Determine how the active-player set + per-player cameras are addressable from
   `chrTickBeams` (which runs once per chr, not per viewport). Find the player loop /
   `g_playerPointers[0..N]` and whether `camIsPosInScreen` can be evaluated against a
   specified player's camera (vs the implicit current one).
2. In the H7 path: when player count > 1, test visibility against **each** active
   player and keep the chr if visible to **any**; else (visible to none) use the
   room-rendered bypass for MP (conservative).
3. Extend `GE007_TRACE_VISIBILITY` to log per-player results in MP; run via
   `mp_smoke.sh`.

**Test/validation:** 2P `GE007_TRACE_VISIBILITY` + per-viewport screenshot
cross-check; the MP cull set ⊆ {chrs off-screen for ALL players}.

**Acceptance:** no chr visible in either viewport is culled when H7 is ON in 2P;
then decide whether H7 can default-ON.

**Risks:** the per-chr (not per-viewport) tick structure may make per-player frustum
evaluation awkward (might need camera state passed in or a per-player visibility
precompute); split-screen camera/viewport plane state (`c_scalex`/`c_halfwidth`).

**Effort:** M. **Dependency:** functional fix is oracle-independent; frame-exact MP
validation reuses Item 1.

**Vertical slice:** the per-player union in `chrTickBeams` + the 2P trace showing
the cull set shrinks to genuinely-off-screen-for-all chrs (screenshot-validated),
before any default-ON flip.

---

## Item 4 — Hidden-guard + knife-impact test hooks + regressions  (M)

**Objective:** Close the two biggest correctness test gaps — the H1/H1b/H2/H5
hidden-guard contract and the knife-vs-guard crash path — neither has a runtime
repro today.

**Existing infra:** `GE007_AUTO_SET_CHR_AI` hook in `src/platform/stubs.c` (init
parse ~L1624, per-frame apply via `chrFindByLiteralId` ~L1675) — the template for a
new hook; `chrSetWeaponFlag4`/`CHRFLAG_HIDDEN`/`CHRFLAG_00040000`/`CHRHIDDEN_BACKGROUND_AI`
(`src/bondconstants.h`); object_interaction throwknife branch
`src/game/chrobjhandler.c:9529-9548`; smoke patterns `tools/damage_hud_smoke.sh`,
`tools/spawn_health_check.sh`; on-screen list `g_OnScreenPropList` (`chrprop.c`).

**Steps:**
1. **Hidden-guard hook:** add `GE007_AUTO_SET_CHRFLAG_{FRAME,CHRNUM,FLAG}` to
   `stubs.c` mirroring `GE007_AUTO_SET_CHR_AI` (init + apply: `chrFindByLiteralId(chrnum)`
   then `chr->chrflags |= flag`). Lets a test set `CHRFLAG_HIDDEN` (0x400) at a frame.
2. **Hidden-guard smoke:** a focused trace run that sets `CHRFLAG_HIDDEN` on an
   in-range guard and asserts the composed contract: the guard is NOT on
   `g_OnScreenPropList` / not rendered (H1b), `firecount==0` + no Bond damage (H1),
   AI-frozen when `!CHRFLAG_00040000` (H2), and `hidden & BACKGROUND_AI` set per H5.
3. **Knife regression:** add a deterministic way to land a thrown knife on an
   on-screen guard (a `GE007_AUTO` force-embed hook, OR a warp+equip(item 3)+throw
   recipe with autoaim), then a smoke asserting no SIGSEGV + a hit registered on the
   `chrobjhandler.c:9529` branch.

**Test/validation:** the two new smokes run headless deterministically (mirror
`damage_hud_smoke.sh`); both green = the contracts are codified.

**Acceptance:** a standing regression that would have caught the knife crash, and
an assertion of the hidden-guard contract (H1b+H1+H2+H5 together).

**Risks:** reliably landing a thrown knife on a guard headless (the throw didn't
impact in this session's attempts — the force-embed hook is the robust path);
`g_OnScreenPropList` observability from the harness.

**Effort:** M. **Dependency:** none — fully independent, do early.

**Vertical slice:** the `GE007_AUTO_SET_CHRFLAG` hook + the hidden-guard
firecount==0/no-Bond-damage assertion (the H1 phantom-damage regression) — the
single highest-value contract, before the fuller composed assertion + knife hook.

---

## 5. Quick warm-ups  (S — DONE 2026-06-23)

All three landed and validated (default-gate combat trace **byte-identical** to
baseline; `GE007_CHRBEAMS_FRUSTUM=0` == default proving the footgun fix; `=1`
diverges proving correct routing; ASan + spawn-health + damage-HUD + playability +
soak smokes green). The WU2 dead-code/extern/comment subset rebuilt to a
**byte-identical binary** (provable no-op).

- ✅ **Unify the env-gate truthiness** — added `ge_env_bool(name, default_on)` to
  `ge_debug.h` (unset/empty→default, `"0"`→off, else→on) and routed
  `GE007_CHRBEAMS_FRUSTUM` (chr.c) and `GE007_STAN_ONEDGE` (stan.c) through it.
  Trace flags keep their inline cached form (platform/diagnostic layer, presence-based
  convention — intentionally not unified to avoid a game-header dependency in fast3d).
- ✅ **Cleanup bundle** — removed the dead nested `#ifdef NATIVE_PORT` guards +
  dead `#else` frustum branch in `chrTickBeams`, the redundant
  `walkTilesBetweenPoints_NoCallback` extern (already in `stan.h`), hoisted the
  duplicated F2 floor-select tail into `stanConsiderFloorTile`, and corrected the
  stale `sub_GAME_7F03E27C` comment (it's room-intersection and *implemented* on
  NATIVE_PORT; `prop->stan` is resolved via `sub_GAME_7F0AF20C`).
- ✅ **Gate `[TEX-FMT-RECOVER]`** behind `GE007_TRACE_TEX` (the only un-gated
  combat diagnostic). The `fmt/siz` recovery itself stays unconditional (it's
  behavior, not diagnostics).
  - **Deliberately NOT done:** compile-gating `CHR_PHY`/`VIS_PROBE` behind
    `GE007_COMBAT_DIAG`. They are already runtime-gated (single cached branch) and
    **Item 3 (H7 MP frustum) reuses `GE007_TRACE_VISIBILITY` at runtime** — compiling
    them out would regress that workflow. The ~55-site macro consolidation was also
    skipped (churn/typo risk for marginal benefit; the cached-getenv + bounded-counter
    pattern is sufficient).

## 6. Lower-priority (later)

- **Broaden the TEX fix:** propagate `fmt/siz` out of `gfx_resolve_static_game_texture`
  at resolve time for all static textures (unconditional correctness) + audit the
  other static-tex import paths (`gfx_pc.c:8071/8112/8229`); address the static-CI/TLUT
  palette caveat.
- **H4 decision:** full chrTickBeams ASM-match (make the position advance
  hidden/freeze-conditional, retire the "hidden guards still advance position"
  deviation) vs formally accept the divergence.
- **H5 real-content coverage:** confirm any solo stage places guard-triggerable prox
  mines; if not, document H5 as MP-relevant + add an MP detonation smoke.
- **Cradle (L18) spawn-health exit-2:** pre-existing, unrelated to combat; investigate
  separately.
