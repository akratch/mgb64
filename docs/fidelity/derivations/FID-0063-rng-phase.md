# FID-0063 — systemic randomGetNext() call-count phase desync: measurement method + round-1/round-2 findings

Status: **root-caused (round 2, §9)** — the retail idle excess is fully attributed
per-callsite: consumer sets are 1:1; the entire residual is the geometry-conditional
guard-perception draw, driven by cross-side guard patrol-state divergence, not an
RNG-layer defect.
Decision D5: committed to FULL closure — call-for-call RNG parity, no tolerance exit.
Round-2 consequence: D5 closure is GATED ON guard-state parity (the FID-0054/0055
position/anim lanes); there is no missing or extra consumer to fix in the port.

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
   one idle frame. ⚠ **CORRECTED in §9.3**: the per-call PC-trace ground truth
   puts the retail first locked frame at **12** draws (native 13 confirmed);
   the "15" was the RDRAM-writeback/VI-window smear (§9.1). The idle-window
   MEAN bias (item 2) survives at +1.2/frame and is fully attributed in §9.2.
2. **The per-frame gap is systemic and idle-weighted.** Cumulative
   native−stock delta from the lock: −15 at rel −120 → −99 at rel 0 (motion
   onset) ≈ **stock draws +2.1/frame more during idle**; the drift flattens to
   ≈ +0.5/frame while walking (−99 → −104 by rel +27) and reverses sign in
   combat (native bursts larger). Because the value streams diverge from the
   first frame, downstream schedules decohere and per-frame counts wander —
   but a persistent nonzero MEAN bias cannot come from schedule noise over an
   identical consumer set; it is a consumer-set or cadence-law difference. ⚠
   **CORRECTED in §9.3/§9.2**: the +2.1/frame figure above is the same
   round-1 RDRAM-writeback-smeared measurement as item 1's stale "15"; the
   PC-trace ground truth puts the idle-window mean bias at +1.22/frame
   (§9.2), entirely concentrated in the `chrCheckTargetInSight` perception
   class (+1.48/f), with every other consumer call-for-call matched.
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
   locked-frame consumption order (15 draws) ⚠ **stale round-1 figure,
   corrected to 12 in §9.3/§9.1 (RDRAM-writeback smear, not the true per-call
   count)** against native's fully-attributed 13 (§6.3) and name the +2 idle
   consumer directly. This closes the remaining attribution gap in one
   capture. **DONE — see §9.**
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

## 9. Round 2 — retail caller-PC attribution (root cause)

### 9.1 Instrumentation: ares randomGetNext caller-PC hook

`MGB64_ARES_RNG_PC_TRACE=PATH` (default off) arms a guest-PC compare in the
ares CPU dispatcher (`ares/n64/cpu/cpu.cpp`, patched by
`tools/prepare_ares_movement_oracle_build.sh`) at the retail `randomGetNext`
entry (VRAM `0x7000A450`, the `GLOBAL_ASM` in `src/random.c`; override
`MGB64_ARES_RNG_PC_HOOK`). Correctness: `J/JR/JAL/JALR` are hard
block-window terminals in the ares recompiler (no cross-block chaining), so
EVERY call enters through the dispatcher with `ipu.pc` at the entry; the
compare sits after the interrupt/NMI/devirtualize early-outs so a faulting
dispatch is not double-counted. One JSONL line per call: call index, guest
`$ra` (caller PC = `ra − 8`), `g_randomSeed` at entry, `g_GlobalTimer`, VI
frame.

**Log-completeness validation:** every distinct sampled seed in the post-lock
stream lies on the LOCK chain in monotone order, and the per-frame counts are
internally consistent with per-callsite structure. **Instrumentation caveat
discovered:** the RDRAM-side `seed`/`global` fields LAG the guest CPU's
dcache (writeback visibility) by a few draws and catch up in bursts — this is
also why the §2 VI-window sampler smears by ±a few draws at tick boundaries
(it reads the same RDRAM). Per-call chain POSITION is the call index from the
LOCK entry; the seed column is auxiliary.

Reference run (dam_combat_guard6_rngseedlock, rebuilt instrumented ares +
Release native, 2026-07-11): 39,904 retail calls logged; LOCK consumed at
retail call 6173 (global 1263, VI frame 2408); native LOCK consumed at
call_count 699 (frame ending global 121). Both application points at the
same sim-tick boundary, confirming §4.

### 9.2 Complete callsite census — consumer sets are 1:1

Caller identification: exact `jal randomGetNext` word (`0x0C002914`) in the
retained `GLOBAL_ASM` reference bodies (`src/game/gun.c` etc.), the
`Address 0x7F......` function headers (chrlv.c, gun.c, fog.c, …), and
address-encoding `sub_GAME_7F......` names. 40-frame locked idle window
(frame boundaries = the per-frame `shuffle_player_ids` triple):

| retail callsite (`ra−8`) | retail function | native consumer | stock rate | native rate |
|---|---|---|---|---|
| `0x7F09B470` | `shuffle_player_ids` loop (player_2.c:685, precedes `sub_GAME_7F09B4D8`) | `shuffle_player_ids<-bossMainloop` | 3.00/f | 3.00/f |
| `0x7F060FF0` | `handles_firing_or_throwing_weapon_in_hand` muzzle scale (gun.c:8198 asm) | `portPrepareFirstPersonWeaponModel` gun.c:3663 | 1.00/f | 1.00/f |
| `0x7F06105C` | same, muzzle roll (gun.c:8226 asm) | gun.c:3669 | 1.00/f | 1.00/f |
| `0x7F08DBDC/0x7F08DC64/0x7F08DCEC/0x7F08DD34` | `bheadUpdateIdleRoll` 4-draw group (bondhead.c:514) | `bheadUpdate` ×4 | 4 draws ×1 firing in window | same (×1 firing) |
| `0x7F02B05C` | `chrlvTickStand` sleep draw (chrlv.c:6365, `+0x2C4`) | `chrlvTickStand<-chrlvActionTick` | 4.35/f | 4.38/f |
| `0x7F036188` | `ai()` `AI_SetNewRandom` (chrai.c:1595/2414) | `ai<-chrlvActionTick` | 1.32/f | 1.45/f |
| `0x7F02B608` | `chrlvTickAnim` (chrlv.c:6630, `+0x120`) | `chrlvTickAnim` | 0.17/f | 0.10/f |
| `0x7F05E3CC` | weapon-sway syncchange class (gun.c:2069 region) | `bgunCalculateBlend`/`gunSetBondWeaponSway` | 0.05/f | 0.22/f |
| `0x7F029FE0` | **`chrCheckTargetInSight` probabilistic gate** (chrlv.c:5749-5755, `+0x270` from US `0x7F029D70`) | `chrCheckTargetInSight<-ai` | **3.08/f** | **1.60/f** |

No retail callsite lacks a native counterpart and vice versa (scoped to this
40-frame locked idle window; the full trace has 124 distinct retail
callsites, only these 9 classes fire inside the window). Totals over the
window: stock 14.07/f vs native 12.85/f (+1.22 stock); adjacent windows:
pre-lock 15.90 vs ~14.33, later (walking) 15.80 vs 15.61 (+0.19/f — the
"+1.2–1.5/frame" figure below is a locked-idle-window statement, not a
universal one). The mean bias is real but is **entirely concentrated in the
perception-gate class** (+1.48/f); every UNCONDITIONAL consumer is
call-for-call matched (the sway class contributes −0.17/f the OTHER way,
Bond-sway state phase; `ai`/`chrlvTickAnim`/`chrlvTickStand` carry small
deltas of their own since they are also per-guard conditional, just not
literally call-for-call identical).

### 9.3 Divergence statement (the “+2” named)

**Retail consumer:** `chrCheckTargetInSight`'s probabilistic perception draw,
retail callsite `0x7F029FE0` (`AI_IFISeeBond` → chrai.c:1929 →
chrlv.c:5749-5755). **The native port has the identical consumer at the
identical program point** (`src/game/chrlv.c:5749`, decomp-matching body) —
nothing is missing, extra, or reordered. The draw is CONDITIONAL on guard–Bond
geometry (vision cone ±110°, `visionrange`/200u floor, `
fogGetScaledFarFogIntensitySquared()` gate): it fires at a different RATE
because the PATROLLING guard population's state differs between the two runs.
At aligned rel ticks in the locked window (41 aligned tick pairs across the
full 36-guard roster), **25** Dam guards match **byte-identically** (`pos`
delta 0.0). Two more are close but NOT byte-identical: chr 28 (≤1.71u) and
chr 6 (a constant 3.95u — the known FID-0054 offset). The remaining **9**
diverge >100u, chrnum **{2, 3, 39, 40, 41, 42, 43, 44, 45}**, spanning
**127–2003 units** apart at aligned ticks (chr 43 nearest at 127–333u, chr 45
farthest at 1831–2003u), with different phase (e.g. chr 45 walks 295u
natively while parked on stock; chr 39/40 walk 633–685u on stock vs 150u
natively). ⚠ **chr 3 (~620u apart) is a distinct sub-symptom, not patrol
phase:** it is STATIONARY on BOTH sides (walks 2.6u stock / 0.0u native) — a
divergent SPAWN/STATION position, not a divergent patrol trajectory, so the
"patrol phase" mechanism below explains the other 8 but not chr 3. Flag chr 3
explicitly as a FID-0054-relevant target distinct from the patrol set: its
fix is a spawn/station-position parity check, not a patrol-cadence one.
⚠ **CORRECTED (FID-0054-guard-state.md §4):** chr 3's SPAWN is byte-identical
on both sides (all 36 guards are, at `move.global` 0); chr 3 walks ~620u from
spawn to a scripted station, stock finishes the walk during its 1146-tick
pre-route window while the native direct-boot side is still mid-walk at onset —
the "stationary both sides" reading sampled a finished walk vs a paused one.
Under the age-aligned recipe it collapses to a constant 58u endpoint offset.
Patrol phase (chr 2, 39–45) is a function of the whole pre-route history
(menu-boot vs direct-boot + different natural boot seeds consuming
`rand()%120+180` wallcounts / `rand()%5+14` sleeps before the lock) — i.e.
upstream SIM STATE, unfixable at the RNG layer and unremovable by a mid-route
seed lock. ⚠ **CORRECTED (FID-0054-guard-state.md §5):** the pre-route-history
skew (1145 guard-age ticks) is real but is the SMALLER layer; the dominant
driver is a SIM-layer movement-semantics divergence — retail pause/warps
unseen patrol guards in WAYMODE_MAGIC while the port's chrlvTickPatrol
NATIVE_PORT patches (chrlv.c:11328/11348) make them walk continuously — which
persists at equal ages from byte-identical state (guards diverge from age 1)
and is tracked as FID-0014 (+FID-0012 ONSCREEN coupling). A level-load seed
lock alone (§9.5.1) will NOT collapse patrol state.

**First-frame ground truth (corrects §6.1):** retail 12 draws
(`SSS C C A TTT C gg`) vs native 13 (`SSS HHHH CC A T gg` — native's
bheadUpdateIdleRoll happened to hit its ≈40-frame phase in that exact frame;
retail's fired 20 frames later in the window). Both consumed LOCK at their
`shuffle_player_ids` triple.

**Effect:** cross-side per-frame call-count divergence on any cross-boot route
= perception-gate rate modulation by divergent patrol geometry → cumulative
stream phase drift (±1–2/frame, window-dependent sign of everything except
the perception class) → downstream probabilistic perception fires ticks off
retail (the FID-0054 +76/+108t offset). **D5 call-for-call closure is
therefore gated on guard-state parity (FID-0054 position lane / FID-0055 anim
lane), not on an RNG-layer fix.** Charter §4.2 assessed: no faithful
mechanically-small reintroduction exists because there is no missing
consumer; no code change made.

### 9.4 Machine-reproducible headline

`tools/fidelity/rng_callcount_diff.py --lock-seed 0x00000001D8F3CC2B` now
emits a `lock_frame` block: native `first_locked_frame_calls` via bounded
chain scan to the lock-consuming record (=13, f=42, global 121), stock via
per-global bucket walk from the application sample (lock bucket cum = 12,
matching the PC-trace ground truth; RDRAM-lag caveat embedded in the output).
Unit-tested in `tools/tests/test_rng_callcount_diff.py::TestLockFrameMode`
(native reachability, stock buckets, segment gate, absent-lock).

### 9.5 Round-3 map (if D5 is pursued past guard-state parity)

1. Re-run this differential after the FID-0054/0055 guard position/anim lanes
   land, on a route seed-locked from LEVEL LOAD on both sides (kills patrol
   phase divergence at its source).
2. The §8.2 per-frame seed pinning and §8.3 consumer-class isolation remain
   valid follow-ups but are now known to be insufficient alone (they pin
   VALUES, not patrol state).
3. Boot/level-load draw parity (§8.4) unchanged.
