# FID-0063 — systemic randomGetNext() call-count phase desync: measurement method + round-1 findings

Status: round 1 (measurement infrastructure + first differential + consumer census).
Decision D5: committed to FULL closure — call-for-call RNG parity, no tolerance exit.

## 1. The measurement insight

`randomGetNext` (`src/random.c:279`, bit-faithful to the retail `GLOBAL_ASM`
`glabel randomGetNext`) is a deterministic step function of `g_randomSeed`.
Two properties make call-count recovery from seeds practical:

1. **The state collapses to 33 bits after one step.** The update is
   `t = rot33(seed_low33); t ^= (seed & 0xFFFFF) << 12; seed' = t ^ ((t>>20) & 0xFFF)`
   — every term keeps bits 0..32 only, so from any 64-bit seed the very first
   step lands in a 33-bit state space (unit-tested:
   `tools/tests/test_rng_callcount_diff.py::test_state_collapses_to_33_bits`).
2. **Both emitters already record the full 64-bit seed once per record.**
   Native: `rng.seed` + the exact `rng.call_count` counter
   (`src/random.c pcRandomGetNextCallCount`, `port_trace.c:8386`).
   ares/stock: `rng.seed` via `readU64(0x80024460)` (the `g_randomSeed`
   address, `sd`-stored in the retail ASM).

Therefore the number of randomGetNext() calls between two consecutive samples
equals the number of PRNG steps from seed(t) to seed(t+1) — computable by a
bounded forward walk, with the whole trace being one chain (total work =
O(total draws)). No new native or ares instrumentation was needed.

Tool: `tools/fidelity/rng_callcount_diff.py`. Unit test (golden vectors
generated from the exact C body): `tools/tests/test_rng_callcount_diff.py`
(auto-discovered by ctest `intro_tools_unittests`).

## 2. Sampling semantics (validated)

| side | emission point | records/tick | seed semantics |
|---|---|---|---|
| native | `portTraceFrame()` at END of each game frame, after the sim tick (`platform_sdl.c:3411`) | exactly 1 | end-of-tick, exact |
| ares/stock | VI-refresh hook (`vi.cpp` patch site), ~3 samples per game tick at speedframes 3, asynchronous to the game frame | ~2–4 | last record per `move.global` ≈ end-of-tick |

The extractor takes the LAST record per tick on each side (the `rng` block is
present on every record, unlike the roster used by the comparator's
`canonical_by_tick`). Empirically the stock intra-tick pattern shows the sim
burst completing before the tick's last VI sample in the common case (the last
sub-sample frequently contributes 0 draws), so per-tick attribution can smear
by at most ±1 tick when a burst straddles the boundary; CUMULATIVE counts
between sampled boundaries are exact.

**Method cross-validation (live):** on every run the extractor re-derives the
native per-tick counts by chain-walking the native seeds and compares them to
the exact `rng.call_count` counter — 218/218 ticks matched on the reference
capture (`validation.native_counter_vs_chainwalk`). The deterministic native
boot chain (`0x12345679 → 0x4C7DBFFB → 0x7DC16821 → …`) and the stock boot
constant (`0xAB8D9F7781280783`, first stock record) also match the golden
vectors.

**Segment gate:** `g_GlobalTimer` resets to 0 on mission (re)start. The
reference stock capture contains TWO gameplay segments — Bond dies to the
engaged guards (health 0.0 by VI frame ~4200) and the game resets at VI frame
4802. A later segment re-visits the same `move.global` values, so last-wins
bucketing across segments silently mixes two runs (the combat comparator
dodges this only because post-reset records carry empty rosters). The
extractor stops at the first global drop after motion onset
(`rel_tick_samples`, unit-tested).

## 3. Natural-route differential (dam_combat_guard6, existing reference capture)

`--align tick`-equivalent anchoring (motion onset, FID-0062 rule). Artifact:
`rng_callcount_diff.py --json-out` on the §12.4 reference capture pair.

- **Pre-combat window (rel ticks 3..57, walking, no shots):** per-span
  (3-tick) counts native ≈ stock ≈ 15/frame; per-span delta oscillates ±10
  with cumulative |delta| ≤ 13. NO gross rate divergence.
- **Fire onset (rel tick 60, first PP7 shot):** BOTH sides burst on the same
  tick — native 282, stock 287 draws (muzzle flash + impact particle init).
- **After guard engagement:** large alternating per-span deltas (±250–460):
  the same bursts land on ticks shifted by 1–2 game frames between sides,
  consistent with the FID-0054/0062 perception phase offset (+76t stock vs
  +108t native). Cumulative delta reaches −6424 by the end of the common
  window — dominated by downstream behavioral divergence (stock engages
  guards 6+7+44 and Bond dies; native run ends after guard 6 only).

**Interpretation limit (the seed-value confound):** the natural route runs
DIFFERENT seeds on the two sides (native deterministic `0x12345679`, stock
`osGetCount()`-seeded), so value-dependent consumers legitimately reschedule
draws (guard idle sleep `rand()%5+14`, wallcount `rand()%120+180`, the
probabilistic `randomGetNext()%distance==0` perception gate). A natural-route
differential can therefore prove RATE parity classes but cannot attribute
call-for-call divergence to a specific consumer.

## 4. Seed-locked route (the call-for-call experiment)

`tools/rom_oracle_routes/dam_combat_guard6_rngseedlock.json`: both sides script
`g_randomSeed = 0x00000001D8F3CC2B` at gameplay frame 40 (pre-motion; onset is
frame 80) — native `GE007_AUTO_RNG_SEED_SCRIPT` (applied at the input poll
that opens frame 40, `stubs.c osContGetReadData`), stock
`MGB64_ARES_RNG_SEED_SCRIPT` (applied at the first VI sample of gameplay frame
40, patch `applyRngSeedEvents`). Native input frame F ↔ stock gameplay frame F
was verified from onset globals (241 = 3·80+1 native; 1387 = 1146+3·80+1
stock).

After the lock, `rng_callcount_diff.py`'s `seeds_match` column is TRUE while
and only while both sides have made the same number of calls since the lock —
the first mismatch tick is the first call-count break, attributable natively
via the per-callsite trace (below).

Results: see §6.

## 5. Native per-callsite census (existing `GE007_TRACE_RNG_CALLS` tooling)

`GE007_TRACE_RNG_CALLS=1 GE007_TRACE_RNG_CALLS_FORCE=1` +
`GE007_TRACE_RNG_CALL_BUDGET` symbolizes every draw (caller + parent via
dladdr; cross-checked against the full `nm` symbol table — no misattribution).
Census on the natural route (native, deterministic; NOTE: run at speedframes 1
— per-frame rates below are for 1-global ticks, consumer SET is what matters):

| draws | consumer (caller<-parent) | class |
|---|---|---|
| 7650 | `explosionSmokeTick` | combat VFX, per smoke particle/frame |
| 1680 | `explosionInitFlyingParticles<-explosionCreate` | per shot impact (~120/shot) |
| 1260 | `explosionUpdateFlyingParticles` | combat VFX |
| 894 | `shuffle_player_ids<-bossMainloop` | 3/frame, EVERY frame (retail-faithful: the bossMainloop player-id shuffle) |
| 770 | `explosionTick` | combat VFX |
| 562 | `portPrepareFirstPersonWeaponModel` | **port-only first-person weapon prep: ~2/frame while a weapon is held, even idle** |
| 515 | `chrCheckTargetInSight<-ai` | guard perception (probabilistic gate) |
| 487 | `chrlvTickStand<-chrlvActionTick` | guard AI |
| 161 | `bgunCalculateBlend` | weapon sway blend (D29 census consumer) |
| 152 | `ai<-chrlvActionTick` | guard AI dispatch |
| 49 | `sub_GAME_7F068508<-handles_firing…` | fire path |
| 36+ | `chrlvIdleAnimationRelated7F023A94`, `expand_09_characters`, `bodiesReset<-lvlStageLoad` (6, boot), … | level-load/AI |

**Prime suspect for a port-only consumer class:**
`portPrepareFirstPersonWeaponModel` (`gun.c:3300`) — a native-only viewmodel
path that draws `RANDOMFRAC()` for muzzle scale (`gun.c:3663`) and muzzle roll
(`gun.c:3669`) UNCONDITIONALLY on every frame the weapon model is prepared,
including idle frames with no muzzle flash (`flash_active == 0`). The retail
first-person path draws its muzzle randoms inside
`handles_firing_or_throwing_weapon_in_hand` behind guards
(`lb 0xc($s0); beqz → skip` + `bondwalkItemCheckBitflags(32/64)` gates,
`7F0606C8..7F0607EC`). Whether the retail path also draws per idle frame (via
a different guard being true) is NOT yet pinned to an instruction — round 2
must diff the exact guard conditions. The pre-combat RATE match (§3) says the
TOTAL idle rates coincide at ~15/frame, so if the port draws 2/frame that
retail does not, retail must draw ~2/frame somewhere the port does not (or the
rates differ by less than the schedule noise) — this is exactly what the
seed-locked capture disambiguates.

## 6. Seed-locked differential (round-1 result)

Both sides verified applying the lock: native `[AUTO_RNG_SEED] frame=40
seed=0x00000001D8F3CC2B` (stderr), stock seed == LOCK observed at global 1263
(= gameplay frame 40) in the trace. Both application points land at the same
sim-tick boundary, rel −123 (native: input poll opening the frame ending at
global 121; stock: first VI sample of gameplay frame 40 at global 1263,
pre-burst).

**Result: the streams never re-synchronize** (`seed_match_ticks: 0`). Walking
both chains forward from LOCK:

1. **The first locked frame consumed a DIFFERENT number of draws for the same
   seed.** Stock's application frame (rel −123, starting exactly at LOCK)
   drew **15**; native's first locked frame (rel −120, also starting exactly
   at LOCK) drew **13**. Same seed values in, +2 draws consumed by retail in
   one idle frame.
2. **The per-frame gap is systemic and idle-weighted.** Cumulative
   native−stock delta from the lock: −15 at rel −120 → −99 at rel 0 (motion
   onset) ≈ **stock draws +2.1/frame more during idle**; the drift flattens to
   ≈ +0.5/frame while walking (−99 → −104 by rel +27) and reverses sign in
   combat (native bursts larger). Because the value streams diverge from the
   first frame, downstream schedules decohere and per-frame counts wander —
   but a persistent nonzero MEAN bias cannot come from schedule noise over an
   identical consumer set; it is a consumer-set or cadence-law difference.
3. **Native side of the locked frame fully attributed** (deterministic rerun
   with `GE007_TRACE_RNG_CALLS`, calls 699–711 = the 13 draws from LOCK):
   `shuffle_player_ids`×3 → `bheadUpdateIdleRoll` (via `bheadUpdate`)×4 →
   `chrCheckTargetInSight`×2 → `ai`×1 → `chrlvTickStand`×1 →
   `portPrepareFirstPersonWeaponModel`×2 (muzzle scale+roll).

**Suspects ruled out this round:**
- *Bond idle-roll cadence*: `standfrac += (1/120 + 0.025·breathing)·delta`
  (`bondhead.c:887`), breathing = 0 at spawn idle on BOTH sides
  (`bondview.c:2478`, builds only with running speed) ⇒ both fire the 4-draw
  roll ≈ every 40 frames. Not a +2/frame class.
- *Port viewmodel prep draws*: `portPrepareFirstPersonWeaponModel`'s 2
  unconditional idle draws (gun.c:3663 muzzle scale, gun.c:3669 bitflag-1
  muzzle roll) are ASM-anchored equivalents of retail
  `handles_firing_or_throwing_weapon_in_hand` `7F060FF0` (scale, guarded only
  by muzzle-node existence at `7F060FE8`) and `7F06105C` (roll, guarded by
  `bondwalkItemCheckBitflags(item,1)`) — same guards, same per-frame cadence.

**Remaining candidate classes for the +2/frame idle gap** (need retail-side
callsite visibility to decide): guard-AI pool/cadence (does retail tick
more/other background guards per frame — `chrlvActionTick`/
`chrCheckTargetInSight` dominate the variance), or another Bond-frame consumer
absent natively. The stock intra-frame VI sub-samples show retail's idle frame
splitting ~[4–5, 9–14] across sub-windows; native's order (shuffle first,
viewmodel last) is compatible — resolution is below what VI sampling can
attribute.

## 7. Instrumentation bug found and fixed: ares `combat.rng_seed` read the HIGH word

`prepare_ares_movement_oracle_build.sh` emitted the combat-block scalar as
`readU32(randomSeedAddress)` — on big-endian N64 that is the HIGH word of the
64-bit `g_randomSeed`, which (property 1 above) is 0 or 1 after the first
step. Native emits the LOW word, so `combat.rng_seed` diverged on EVERY frame
BY CONSTRUCTION — part of the §5/§8 "rng_seed diverges every frame" evidence
was this artifact (the underlying phase desync is real, but its per-frame
comparator signal was meaningless). Fixed to `readU32(randomSeedAddress + 4)`;
takes effect on the next ares oracle rebuild. The movement-record `rng` block
(`readU64`) was always correct and is what the extractor uses.

## 8. Round-2 map

1. **Retail callsite attribution (the decisive tool):** extend the ares patch
   with a `randomGetNext` PC-hook (breakpoint at the function's entry VRAM,
   log `$ra` + call index per gameplay frame). With it, diff the retail
   locked-frame consumption order (15 draws) against native's fully-attributed
   13 (§6.3) and name the +2 idle consumer directly. This closes the
   remaining attribution gap in one capture.
2. **Per-frame seed pinning refinement:** script up to 64 per-frame seed
   events on both sides (`GE007_AUTO_RNG_SEED_SCRIPT="40:S,41:S,…"`,
   `MGB64_ARES_RNG_SEED_SCRIPT` same, offsetting the known 1-frame skew of the
   ares gameplayFrame window) so every frame restarts from the same value —
   prevents the value-stream decoherence and yields per-frame verdicts
   (`seeds_match` per tick) for the whole window instead of aggregate bias.
3. **Consumer-class isolation captures:** seed-locked routes in a guard-free
   idle spot (bias persists ⇒ Bond-frame consumer; vanishes ⇒ guard-AI
   pool/cadence) and with a different weapon/unarmed (viewmodel class).
4. Re-run the FID-0011/0012/0013 re-validation under seed-lock once the idle
   gap is closed; then extend to boot/level-load parity (the pre-lock draws:
   `bodiesReset` ×6, `init_player_data_ptrs_construct_viewports`, guard-body
   picks — the T6/D29 census window).
