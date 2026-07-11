# FID-0054 — guard patrol-state divergence: onset classification + mechanism

Status: **root-caused (layered)** — the FID-0063 §9.3 divergent-guard set is fully
decomposed into (A) a capture-RECIPE guard-age skew (removed by the
`dam_combat_guard6_agealign` route, landed here) and (B) a SIM-layer patrol
movement-semantics divergence (the WAYMODE_MAGIC cluster already tracked as
**FID-0014**, coupled to **FID-0012**'s ONSCREEN semantics). Layer B persists
under a fully age-aligned recipe and under equal ages from byte-identical
initial state, so D5 (RNG call-for-call parity) is gated on **FID-0014/FID-0012**,
not on any recipe or RNG-layer work. No sim code changed this iteration
(charter §4.1: file precisely, stop before fixing).

## 1. Question being answered

FID-0063 round 2 attributed the entire RNG call-count residual to
`chrCheckTargetInSight`'s geometry-conditional draw firing off divergent guard
positions, and named the divergent set on `dam_combat_guard6`:
chrnum **{2, 3, 39, 40, 41, 42, 43, 44, 45}** at 127–2003u cross-side. This
derivation answers: are those guards divergent at the very first compared tick
(initial-state/recipe class) or do they drift during the route (sim-behavior
class)? Answer: **both, layered** — and the split is now quantified.

## 2. Method

Captures (determinism envelope, charter rule 6; Release native built in
`/tmp/mgb64-fid0054-build`; ares = the FID-0063 r2 instrumented movement-oracle
build, low-word fix + PC hook):

```
tools/movement_oracle_capture.sh --route dam_combat_guard6 --native-full-trace \
  --no-compare --no-build --binary /tmp/mgb64-fid0054-build/ge007 \
  --ares-bin <ares-movement-oracle>/ares --rom baserom.u.z64 \
  --out-dir /tmp/mgb64-fid0054-cap-baseline --timeout 400
# same again with --route dam_combat_guard6_agealign --out-dir .../cap-agealign
tools/compare_combat_trace.py --baseline <stock>.jsonl --test <native>.jsonl --align tick
```

Analyses pair guard snapshots two ways: by **relative sim-tick** (`move.global`
− onset global, the `--align tick` frame) and by **absolute `move.global`**
(= ticks since mission start = **guard age**; `g_GlobalTimer` resets to 0 at
mission start on both sides). Per-tick canonicalization = last max-roster
record per tick, empty rosters dropped (the §12.1 rule).

## 3. Onset classification: divergent at the first compared tick — but spawn is byte-identical

Fresh baseline (`dam_combat_guard6`, Release, 2026-07-11):

- **Spawn parity is exact.** At `move.global = 0` (mission start) all 36 guards,
  including every divergent-set member and chr 3, have **pos delta 0.00**
  native-vs-stock. There is **no spawn-variant or station-pick divergence**.
- **Pre-route history differs massively.** Native (direct `--level 33` boot)
  reaches motion onset at global **241** (= 3×80+1); stock (menu boot: folder
  screens + briefing + intro before gameplay input frame 0 at
  `stock_gameplay_start_global` 1146) reaches onset at global **1386** — the
  stock guards are **1145 ticks (~19.1 s) older** at every aligned tick.
- **At the first compared tick (rel 0)** the same 9 guards are already 292–1831u
  apart (chr 2: 1504, chr 3: 622, chr 39: 626, chr 40: 723, chr 41: 292,
  chr 42: 325, chr 43: 321, chr 44: 644, chr 45: 1831; chr 6: 3.95 — the known
  §9.3 constant). Divergent set over the full 116-tick window:
  {2, 3, 6, 39, 40, 41, 42, 43, 44, 45} (chr 6/7 cross 100u only post-onset via
  combat-engagement cascades).
- The 25 byte-identical guards are the non-walkers: they perform a small
  **identical** deterministic settle (e.g. chr 18–27 move exactly 99.74u on both
  sides) and then stand. The divergent set = exactly the guards whose AI walks
  (actiontype 14 ACT_PATROL / 15 GOPOS / chr 3's walk-to-station).

So in the comparator's frame the divergence is **initial-state**: it exists
before the route's first tick and does not originate mid-route.

## 4. Layer A — the capture-recipe guard-age skew (removed)

`tools/rom_oracle_routes/dam_combat_guard6_agealign.json` shifts every native
input event by +382 frames (80→462, 100→482; native_frames 300→682) so native
onset lands at global 3×462+1 = **1387**, within one tick of stock (3-tick frame
granularity floor; observed stock onset jitters 1386/1387 across runs). Captured
run: **native onset global == stock onset global == 1387 exactly**.

Effect on the divergent set (per-guard max/mean pos delta over the compared
window, baseline → agealign):

| chr | baseline max/mean | agealign max/mean | verdict |
|---|---|---|---|
| 3 | 650 / 260 | **58.7 / 58.2** | **collapsed to a constant station offset** |
| 39 | 652 / 580 | 617 / 295 | persists (mean −49%) |
| 40 | 782 / 646 | 338 / 196 | persists (mean −70%) |
| 41 | 789 / 521 | 665 / 215 | persists (mean −59%) |
| 42 | 779 / 524 | 707 / 278 | persists (mean −47%) |
| 43 | 737 / 447 | 344 / 143 | persists (mean −68%) |
| 44 | 1077 / 625 | 838 / 461 | persists (mean −26%) |
| 45 | 1831 / 1294 | 1462 / 721 | persists (mean −44%) |
| 2 | 1646 / 707 | 4157 / 2472 | persists (post-onset cascade noise) |

**chr 3 fully resolves as recipe-class**: it walks ~620u from its (identical)
spawn to a scripted station; stock completes the walk during its 1146-tick
pre-route window and stands (act 1); the baseline native side is still mid-walk
at onset (act 3, 57.6u). With age alignment both sides complete the walk
pre-route and the residual is a **constant 58u station offset** (max ≈ mean) —
a small Layer-B travel-endpoint residual, no longer a phase divergence. This
**corrects FID-0063 §9.3**: chr 3 is *not* a divergent spawn/STATION pick; the
"stationary on both sides" reading sampled a walk already finished (stock) vs
one paused mid-route (native).

The 8 patrol guards do **not** collapse — Layer B dominates them.

## 5. Layer B — the sim-layer patrol movement-semantics divergence (FID-0014/FID-0012)

### 5.1 Equal-age comparison from byte-identical state

Pairing by absolute `move.global` compares guards of equal age from the
byte-identical global-0 state. Both sides are input-free until stock's gameplay
window, so this is a pure sim-vs-sim differential (modulo RNG seed values).
Result (agealign pair, ages 0..1387, 174 sampled common ticks; reproduces
bit-for-bit on the older FID-0063 r2 capture — native is run-to-run
bit-identical, stock pre-onset 97.4% byte-stable, worst 14.9u):

- All 8 patrol guards diverge from **age 1** — the very first sim tick — with a
  deterministic signature: native walks **2.10u in XZ** (root-motion) while
  stock's position is frozen at the spawn pad; native Y **ground-snaps**
  immediately (chr 2: −0.48 vs stock −55.64; chr 45: −603.8 vs −633.5) while
  stock keeps the pad Y. By age 31 native has walked 55–117u (3D, incl. Y-snap); the magic-frozen stock subset (chr 2/39/40/44/45) has not moved, while stock chr 41/42/43 had walked ~67u onscreen (per §5.2) — all 8 nonetheless diverge deterministically from age 1.
  Max equal-age deltas within the pre-route window: chr 2: 4021, chr 45: 1460,
  chr 43: 641, chr 39: 638 …
- chr 3 diverges at age 130 by 0.02u (a paused-vs-walking scheduling tick),
  growing only while one side is mid-walk.

### 5.2 Movement-profile ground truth

Movement profile over the pre-onset window (speed threshold 0.05 u/tick):

| side | patrol guards (chr 2, 39–45) |
|---|---|
| **stock/N64** | **pause-then-warp**: stationary 34.4–99.7% of ticks unconditioned (magic-frozen subset chr 2/40/45: 98.1–99.7% — e.g. chr 40: 1359/1386 paused in 9 stops of ~100–260t; chr 41/42/44 sit lower, 34.4–46.8%, because onscreen intro-camera windows keep them walking), position steps of **100–1900u per tick** at warp instants; guards recently drawn walk normally (chr 41/42/43 while onscreen: 2.8 u/t) |
| **native** | **continuous walking**: 892/892 ticks of the full baseline capture (onset at 241; behavior uniform) moving at ~2.8 u/t for all 8 patrollers, zero pauses; a handful of small residual jumps |

### 5.3 Mechanism (code anchors)

Retail (`chrlvTickPatrol`, US `0x7F032548`, decomp-matched body
`src/game/chrlv.c:11303` minus the `NATIVE_PORT` blocks; magic mover
`chrlvTravelTickMagic`, `0x7F028600`, `chrlv.c:4656`): a patrolling guard not
recently visible (`act_patrol.lastvisible60 + CHRLV_DEFAULT_TIMER(0x96=150) <
g_GlobalTimer`) runs in `WAYMODE_MAGIC`(=6): its prop position is **frozen**
while `segdistdone` accumulates virtually (`+= speed × modelGetAbsAnimSpeed ×
delta`), and the guard **teleports to the next patrol pad** only on virtual
segment completion (`chrlvTravelTickMagic` sets `prop->pos = pad pos`). That is
the stock pause/warp profile above.

The port adds three deliberate patches that change this:

1. `chrlv.c:11316-11331` — per-tick `modelSetAnimLooping` forcing (the FID-0014
   headline workaround). Side effect measured here: walk root-motion keeps
   applying **even in WAYMODE_MAGIC**, so native magic-mode guards *creep
   continuously* instead of standing (live `GE007_TRACE_MAGIC_TRAVEL=1` run:
   chr 41/42/43/45 in `mode=6` at globals 1..19 with positions advancing
   ~1–5u/tick, plus the immediate ground-Y snap).
2. `chrlv.c:11336-11350` — `&& !chrlvPropHasRenderedRoom(self_prop)` added to
   the MAGIC **entry** condition: guards whose registered rooms are rendered
   (most of the outdoor Dam roster once gameplay renders) never enter MAGIC and
   walk normally forever.
3. `chrlv.c:274-290` — `chrlvMagicTravelShouldUseRenderedPathExit` narrows the
   `stan_related==0` magic-exit during frozen intro cameras (background-AI
   guards).

Coupled: native `PROPFLAG_ONSCREEN` is set far more broadly than stock (e.g.
chr 2/3/39/40 onscreen 68.6% of native ticks vs 0% of stock ticks — position
divergence confounds a direct flag comparison, but the flag feeds
`lastvisible60` and hence the magic gate — **FID-0012 / M2.3 territory**, and
the M2.5 note in `docs/BACKLOG_v0.4.0.md` already sequences these together).

### 5.4 Divergence statement (charter §4.1)

*Retail* keeps unseen patrolling guards frozen at their current pad in
`WAYMODE_MAGIC`, walking them virtually and teleporting them pad-to-pad
(`chrlvTickPatrol` `0x7F032548` + `chrlvTravelTickMagic` `0x7F028600`; oracle:
stock patrol guards stationary 34.4–99.7% of pre-onset ticks unconditioned (98.1–99.7% for the magic-frozen chr 2/40/45) with 100–1900u warp
steps). *The port* (`src/game/chrlv.c:11328` force-loop, `:11348` rendered-room
magic-entry suppression, `:274-290` intro exit-narrowing) makes all Dam patrol
guards walk smoothly on every tick (892/892 across the full capture, 2.8 u/t, zero pauses).
*Player-visible effect*: every patrolling guard's world position at any given
time diverges from retail (up to 2000u observed; up to ~640u within 22 s of
mission start at equal ages from byte-identical state); downstream it modulates
`chrCheckTargetInSight`'s geometry-conditional draw rate (the FID-0063 §9.2
perception-class +1.48/f residual — the sole RNG call-count gap), shifts guard
perception timing (FID-0054's +76 vs +108t alert offset), and gates D5.
*Repro*: §2 commands; movement-profile + equal-age analyses as described.

**Classification: parity-divergence** (deliberate, documented port patches with
a real problem behind them — the fog/alpha → ONSCREEN → magic-stall chain), not
a new port-defect. It is exactly **FID-0014** (+ the FID-0012 coupling); this
derivation supplies their first trace-grade machine evidence and the statement
above. Per the charter, no `chr*/stan*` change is made here; the faithful fix
(retail-shaped ONSCREEN + un-forced anim looping + retail magic entry/exit)
belongs to the FID-0014/FID-0012 ACT with its own A/B flag and oracle gate.

## 6. Consequences / routing

- **FID-0054**: the position lane is now fully attributed — Layer A (recipe)
  is closed by `dam_combat_guard6_agealign` (use it for all future guard-state
  parity work; the plain route remains for gameplay-shaped captures); Layer B
  is FID-0014/FID-0012. FID-0054's `blocked_on` moves from FID-0062 (landed,
  comparator trustworthy) to FID-0014 + FID-0012.
- **D5 / FID-0063**: round-3 step 1 ("re-run after guard position/anim lanes
  land") should additionally re-run under the agealign route; a level-load
  seed-lock alone will NOT collapse patrol state (the divergence is
  movement-semantics, not draw-values; equal-age equal-recipe guards diverge at
  age 1 deterministically).
- **FID-0011/0012/0013 re-validation**: the baseline `--align tick` numbers on
  current HEAD (116 aligned frames, 9705 divergences; guards.pos ~1170–1243
  per axis) reproduce §12.2 within run noise; the guard-pos portion is Layer
  A+B above, not chrTickBeams/stan logic.
- **Stock capture nondeterminism (instrumentation caveat)**: back-to-back stock
  runs of the identical `.ares-input` decohere post-onset (Bond death/reset at
  global 4639 vs 3685; onset 1386 vs 1387) — the ares-side input application
  jitters by ±1 VI frame and the natural seed differs per run. Pre-onset guard
  state is 97.4% byte-stable (worst 14.9u at a warp boundary). Single-capture
  cross-side comparisons are unaffected; cross-RUN comparisons of stock combat
  outcomes are not meaningful.

## 7. Artifacts

- `/tmp/mgb64-fid0054-cap-baseline/` — both-sides `dam_combat_guard6` +
  `combat_diff_tick.json` (ROM-derived, per charter rule 7 not committed).
- `/tmp/mgb64-fid0054-cap-agealign/` — both-sides `dam_combat_guard6_agealign`
  + `combat_diff_tick.json`.
- Native magic-mode live trace: `GE007_TRACE_MAGIC_TRAVEL=1` on the §2 binary,
  Dam `--level 33 --deterministic` (mode=6 creep, globals 1..19).
