# Combat — Deferred-Work Execution Plan

Step-by-step, testable, milestone-by-milestone plan for the combat-fidelity items
deferred from the first fix pass (H1/H2/off-by-one/F1 already landed — see
[COMBAT_GLIDEPATH.md](COMBAT_GLIDEPATH.md) §"Fixes landed"). Every claim here was
re-validated against the **actual native body + the `#else` MIPS ground truth +
every reader/clearer/helper** (11-agent deep dive + adversarial verification +
independent spot-checks). Prior-analysis errors are called out explicitly.

> Build context: native defines `NONMATCHING + NATIVE_PORT + PORT_FIXME_STUBS +
> VERSION_US + REFRESH_NTSC + LEFTOVERDEBUG`. `GLOBAL_ASM()` is empty; the `#else`
> MIPS is the original N64 function. `chrTickBeams` native subset = `chr.c:4917-5160`;
> ASM original = `chr.c:5155-6002`.

## 1. Validated severities (corrected)

**None of the deferred items is a critical/playability blocker.** All native
dependencies exist and work.

| Item | Was | **Corrected** | One-line validated finding |
|---|---|---|---|
| **H4+H7** — actiontype dispatch + visibility frustum | HIGH / "needs frustum build" | **MEDIUM** | The frustum test `sub_GAME_7F054D6C` is **fully native** (chrobjhandler.c:47205) and already used by the object handler (10895). The `chrTickBeams` bypass comment is stale-conservative; task is *validate + restore*, not build. |
| **H5** — `CHRHIDDEN_BACKGROUND_AI` set | MEDIUM / "risky downstream" | **MEDIUM** | Confirmed *dead behavior*: flag never set in port → `check_guard_detonate_proxmine` (chrobjhandler.c:44364) never fires → **guards never trigger proximity mines**. Bounded one-site fix. |
| **F2** — floor-tile seam disambiguation | MED-HIGH / "needs rewrite" | **MEDIUM** | Narrower than thought — only the `disp<2.0` on-edge walk-back is missing; bbox gate + `disp<-2` reject + F1 + advance + highest-floor already faithful. But the on-edge flag is **sticky per call** → global blast radius. |
| **H6** — held-weapon `objFreePermanently` | MEDIUM / "uncertain union" | **LOW** | Union resolved: it's `obj->runtime_bitflags & RUNTIMEBITFLAG_REMOVE`, not raw `0x64`. Bounded leak, other reclamation paths exist, never a crash. |
| **H3** — freeze-derived anim timer | LOW / "debug-only" | **COSMETIC** | Confirmed no-op in normal play: `D_8002C90C/910` mutate only inside `switch(getDebugMode()) case 8` (DEB_SELANIM); default mode never touches them. |

### Key corrections to prior analysis (validated)

- **`sub_GAME_7F03E27C` is NOT stubbed** on this build (real impl at `chrprop.c:7091`
  active under NONMATCHING+PORT_FIXME_STUBS+NATIVE_PORT) — the `chr.c:4972` comment is
  stale.
- **H5 must read `model->anim2` (bondtypes.h:1557), NOT raw `+0x54`** — the `Model`
  struct widens (`unk34`/`unk38` → `uintptr_t`) on NATIVE_PORT, so a literal offset
  reads garbage. The ASM's `model->0x54` is the `anim2` field.
- **F2's `bVar2` (on-edge) flag is sticky/monotone, not per-tile** in the canonical
  decomp — the single highest-risk faithfulness item (global-per-call blast radius).
- **H3's per-guard `anim_frame` trace field already exists** (port_trace.c:4678/4760) —
  the prior "missing field" gap is refuted.
- **H6's `chrSetWeaponFlag4` is at `chrobjhandler.c:44878`** (not 4878), and `objFree`
  NULLs `weapons_held[hand]` only conditionally (`prop->parent != NULL`), so the fix
  must re-read the slot each iteration.

## 2. Cross-cutting prerequisites (shared blockers)

These are net-new harness/infra; several unblock multiple items at once.

1. **Combat/floor-field ROM-oracle extension** *(the single biggest blocker, XL)* —
   `rom_oracle_route.py` + the ares RDRAM reader extract only movement/intro fields
   today. Reading stock **prox-mine timer + explosion events, per-chr `pos.y` /
   `actiontype` / anim-frame, and floor-tile selection** is the shared prerequisite for
   the *final ground-truth* milestone of **H5, H4+H7, and F2** (Phase 5). Until it
   lands, frame-exact stock parity for those is untestable; they rest on
   ASM-faithfulness + in-port traces.
2. **New seam/stacked-floor oracle route** — all four existing routes are Dam
   flat-ground/intro and likely never fire F2's on-edge branch. A route walking the
   player across stacked tiles (e.g. a Dam wall walkway) is required to *exercise and
   validate* F2.
3. **`--trace-state` selected-stan-tile / floor-Y field** — floor-Y is only indirect via
   `pos[1]` today; tile identity isn't traced, so two stacked tiles with near-equal Y
   can't be distinguished. Needed to make F2 directly observable.
4. **Runtime env gates** `GE007_CHRBEAMS_FRUSTUM`, `GE007_CHRBEAMS_DISPATCH`,
   `GE007_STAN_ONEDGE` (default OFF) — keep both old and new paths compiled so every
   behavioral change is instantly revertable and gates-OFF stays byte-identical to
   baseline (mirrors how ADS shipped behind `Input.AdsEnabled`).
5. **`GE007_TRACE_VISIBILITY` dual-result probe** — behavior-neutral diagnostic in
   `chr.c:5013-5027` computing *both* the room-rendered bypass and `sub_GAME_7F054D6C`,
   plus `chraiGetPropRoomIds`, to measure PC frustum-plane accuracy in 1P and 2P. **This
   is the load-bearing gate for the entire H4+H7 effort.**
6. **`GE007_AUTO_SET_HELD_WEAPON_REMOVE`** — new `stubs.c` hook wrapping
   `chrSetWeaponFlag4` to force the H6 leak deterministically (observation needs no new
   hook — `traceHeldPropSnapshot` already emits runtime/present per hand).
7. **H5 set-site debug trace** *(effectively mandatory)* — a `ge_dbg_enabled()` fprintf
   at the flag-set site to disambiguate "flag set but no mine in range" from "flag never
   set"; a null `EXPLOSION_TRACE` is otherwise ambiguous.
8. **`GE007_AUTO_DEBUG_MODE=8` (DEB_SELANIM)** — optional, H3-M4 only; deferrable since
   H3 lands as a no-op proof.

## 3. Dependencies between items

- **Shared edit site:** H3, H5, H6, H4, H7 all edit the *same* `chrTickBeams` body at
  adjacent merge points. Recommended single-author order *within the file*:
  **H3 (timer @4969) → H5 (set @4984) → H6 (free @5001) → H4 (dispatch @4969 region) →
  H7 (visibility @5013-5027)**. Do **not** gate H5/H6 behind the H1/H2 hidden checks —
  the ASM evaluates them on the common tick path; the H5/H6 inserts sit *before* the
  landed H1 gate (chr.c:5001).
- **H7 unblocks H4:** the Phase-1 frustum-accuracy probe is the gate. If frustum planes
  mis-cull on-screen guards, H7 *and* the H4 ACT_STAND visibility-first arm stay
  deferred with a concrete root cause.
- **Shared oracle prereq:** the combat-field oracle extension is the same blocker for
  H5-M4, H4-M3, and F2-M4 — build once, unblock three.
- **F2 ⇐ walk-core:** F2 depends on `sub_GAME_7F0B0914` (a native reimpl) being correct
  at seams; F2-M1 must validate it, and that needs the same new seam route as F2-M4.

## 4. Phased plan

| Phase | Items | Gate (exit criterion) |
|---|---|---|
| **0 — Read-only baselines** | all | Baselines captured as artifacts; each item's omission/dead-behavior objectively reproduced; F2/H4 flat-ground divergence numbers fixed as the no-regression bar. No code changed. |
| **1 — Harness prereqs (behavior-neutral)** | all | New hooks/gates/probes compile; **gates-OFF byte-identical to Phase 0**. H7 dual-result probe runs 1P+2P → frustum-vs-bypass disagreement table. **Decision gate:** frustum accurate → proceed to Phase 3; else freeze H4/H7 with documented root cause. |
| **2 — Low-risk ASM-faithful fixes** | H5, H6, H3 | Builds green; H5 detonation fires iff guard in 250u & Bond out of range; H6 ASan-clean, no spurious frees, H1 intact; H3 zero-diff on default oracle route. All smokes pass. |
| **3 — Gated dispatch + visibility** *(only if Phase 1 PASSED)* | H4, H7 | Gates-OFF == Phase 0; gates-ON runs full levels 1P+2P, no crash/ASAN; patrol guards keep valid floor-Y; no on-screen guard wrongly culled (screenshot cross-check); off-screen guards correctly stop tile updates. |
| **4 — F2 seam disambiguation** *(gated, route-blocked)* | F2 | Builds; ASan-clean; flag-OFF byte-identical; flag-ON ≤ baseline divergence on all four flat routes; smokes pass. **Default-ON flip gated on Phase 5 seam route.** |
| **5 — Combat-field ROM-oracle (ground-truth parity)** | H5, H4, F2 | Per item: port matches stock within tolerance OR documented divergence. The only path to frame-exact parity for the three final milestones. |

## 5. Per-item milestones (steps · test · exit)

### H5 — guard-triggered proximity mines (MEDIUM)
Insert, before `chrUpdateAimProperties(chr)` (chr.c:4984), the ASM-faithful set
(`.L7F0213F8`): `if (!(chr->actiontype == ACT_STAND && model->anim2 == NULL &&
prop->type != PROP_TYPE_VIEWER)) chr->hidden |= CHRHIDDEN_BACKGROUND_AI;`
(use `model->anim2`, **not** raw `+0x54`).

- **M0** (S, read-only) — baseline: arm a prox mine (`GE007_AUTO_ADD_ITEM`/`EQUIP_ITEM`
  + `GE007_AUTO_FIRE`), warp a guard in range (`GE007_AUTO_WARP_CHR_*`), Bond out of
  range. *Test:* grep `[EXPLOSION_TRACE]` — **no** guard-triggered detonation; flag
  observed unset. *Exit:* dead behavior reproduced.
- **M1** (S) — land the set + set-site trace. *Test:* build green; `GE007_TRACE_CHRNUM`
  shows `0x200` set on a moving/anim guard, unset on a standing `anim2==NULL` guard.
  *Exit:* flag set per the condition.
- **M2** (M) — prove detonation. *Test:* `[EXPLOSION_TRACE]` fires iff guard ≤250u &
  Bond out of range; set-site trace disambiguates flag-set vs no-mine-in-range.
  *Exit:* reliable guard-triggered detonation, none when out of range.
- **M3** (M) — regression guard. *Test:* all smokes exit 0; MP scenario shows no
  spurious detonations; flag confined to the one reader, cleared each AI tick.
- **M4** (XL, blocked) — stock parity (Phase 5 oracle): detonation frame/pos within
  tolerance.
- *Testability:* M0–M3 runnable today; M4 needs the combat oracle. Real-content caveat:
  prox mines are primarily MP/player-thrown — solo coverage may be near-zero.

### H4 + H7 — actiontype dispatch + visibility (MEDIUM)
H7 (visibility) first, then H4 (dispatch), both behind env gates, old paths kept
compiled.

- **M0** (S, read-only) — baseline: `GE007_DUMP_STAGE_CHRS`; confirm an `ACT_PATROL(14)`
  guard with varying floor-Y + stable room registration.
- **M1** (M) — **measure frustum accuracy** (the gate): `GE007_TRACE_VISIBILITY`
  dual-result log (room-rendered vs `sub_GAME_7F054D6C`) cross-checked against
  screenshots. *Exit:* PASS (`frustum=0` only when genuinely off-screen → proceed) or
  FAIL (document the plane/scale root cause → freeze H4/H7).
- **M2** (L) — restore real visibility (replace bypass with
  `sub_GAME_7F054D6C(prop,&prop->pos,getinstsize(model),1)` behind
  `GE007_CHRBEAMS_FRUSTUM`; re-validate the zDepth/fog workaround doesn't double-apply)
  + the actiontype dispatch behind `GE007_CHRBEAMS_DISPATCH`. *Test:* build +
  playability with gates OFF (== M0) then ON; `asan_smoke.sh` for the new
  `+0xBC/+0xF8/+0x78/+0x9c` accesses; validate 1P **and** 2P split-screen. *Exit:*
  gates-OFF == M0 exactly; gates-ON full level, no crash/ASAN; patrol floor-Y valid.
- **M3** (XL, blocked) — stock parity (Phase 5): patrol floor-Y tolerance + actiontype
  transitions + `modelTickAnimQuarterSpeed` cadence.

### F2 — floor-tile on-edge seam disambiguation (MEDIUM)
Only the `disp<2.0` sticky on-edge + `getTileMidPoint`+`walkTilesBetweenPoints_NoCallback`
reject is missing; implement behind `GE007_STAN_ONEDGE`.

- **M0** (S, read-only) — `movement_oracle_capture.sh --native-only` on all four routes;
  record `pos/col` divergence as the no-regression bar.
- **M1** (M) — validate the walk core `sub_GAME_7F0B0914` at seams (interior same-tile
  testable now; cross-seam deferred to M4). *Exit:* same-tile interior / other-tile
  across seams — else F2 is blocked on a walk-core bug.
- **M2** (M) — implement the per-tile 3-edge loop (declare `on_edge` **once**, faithful
  to the sticky `bVar2`) + the midpoint walk-back reject, **native arg orders**,
  scaled-edge / unscaled-walk discipline. *Test:* build no-warnings; `asan_smoke.sh`
  clean. *Exit:* flag-OFF byte-identical to M0.
- **M3** (M) — no flat-ground regression: flag-ON ≤ M0 divergence on every route +
  playability + mp smokes. *(Proves only no-regression — Dam flat routes likely never
  set on_edge.)*
- **M4** (L, blocked) — new seam route + selected-tile trace field + stock compare:
  flag-ON floor-Y matches stock where flag-OFF diverges. **Default-ON flip gated here.**

### H6 — held-weapon free (LOW)
Re-introduce the two-hand `objFreePermanently` blocks (ASM `.L7F02106C`/`.L7F021094`)
before the H1 gate (chr.c:5001), re-reading `weapons_held[hand]` each iteration; free
when `obj->runtime_bitflags & RUNTIMEBITFLAG_REMOVE`.

- **M0** (S, read-only) — grep proves `objFreePermanently` absent from chr.c:4917-5150;
  document the 1:1 ASM→C field map.
- **M1** (M) — add `GE007_AUTO_SET_HELD_WEAPON_REMOVE` (wrap `chrSetWeaponFlag4`).
  *Test:* pre-fix, a held weapon with `runtime&REMOVE` persists `present=1` across a
  full tick.
- **M2** (S) — implement the free. *Test:* post-fix, the flagged weapon is gone
  (`present=0` **or** obj id changed — not strict `present→0`) within one tick.
- **M3** (M) — regression: `asan_smoke.sh` with the hook = zero reports; unflagged
  weapons untouched; H1 drop/fire intact.

### H3 — freeze-derived anim timer (COSMETIC)
At chr.c:4969: `animTick = g_ClockTimer; if (D_8002C90C != 0) animTick = D_8002C910 ? 1
: 0;` then pass `animTick`. ASM-faithful; no normal-play effect.

- **M1** (S, read-only) — confirm only `lvl.c` case-8 (DEB_SELANIM) mutates the globals;
  default `g_DebugMode` never does.
- **M2** (S) — implement. *Test:* build (incl. LEFTOVERDEBUG); playability +
  route_contract smokes pass, no diff vs baseline.
- **M3** (M) — **prove default-play no-op:** `movement_oracle_capture.sh --native-only`
  before/after = **zero diff**.
- **M4** (L, needs `GE007_AUTO_DEBUG_MODE`) — verify the freeze/single-step actually
  fires in DEB_SELANIM (anim_frame delta 0 frozen / 1 single-step). Deferrable
  (cosmetic).

## 6. Residual untestable (honest limits)

1. **Frame-exact stock parity** for H5 detonation timing, H4 patrol floor-Y / ACT_ANIM
   sub-frame cadence, and F2 floor-tile selection — all require the Phase-5 combat-field
   oracle. Until then: ASM-faithfulness + in-port traces only.
2. **Real-content coverage for H5** — prox mines are primarily MP/player-thrown; no solo
   level is confirmed to place guard-relevant prox mines, so solo coverage may be near
   zero regardless of correctness.
3. **Render/cull fidelity gap (H7)** — off-screen / `CHRFLAG_HIDDEN` guards dropped by
   the port's list culling may never reach `chrTickBeams`, an inherent fidelity gap
   independent of these fixes.
4. **Per-player split-screen frustum (H4+H7)** — not per-guard-traceable by `mp_smoke.sh`
   today; M1 relies on judgement-based screenshot cross-check.
5. **F2 global blast radius** — the sticky on-edge flag can alter floor-Y far from any
   seam depending on tile iteration order; flat-ground smokes may not hit the worst case.
6. **F2 cross-seam walk-core** — unverifiable until the new seam route exists.
7. **H3 DEB_SELANIM behavior** — verified by code-inspection only without
   `GE007_AUTO_DEBUG_MODE`; acceptable given cosmetic severity.
8. **H6 child-prop teardown** — previously unexercised in the port; ASan substitutes for
   the absent memory-correctness oracle and can't prove every path.

## 7. Recommended starting point

**Phase 0 + Phase 1 (read-only baselines + behavior-neutral harness)** unblock
everything and change no behavior. Then **H5** (genuinely dead feature, bounded,
testable today) and the **H7 frustum probe** (the load-bearing decision gate for the
highest-value H4+H7 work) are the highest-leverage first landings. F2 and the combat
oracle are the heaviest and should be sequenced last.
