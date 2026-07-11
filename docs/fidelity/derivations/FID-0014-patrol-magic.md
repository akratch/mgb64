# FID-0014 — faithful WAYMODE_MAGIC patrol semantics (root cause + fix)

Status: **fix landed** (default ON, negative control `GE007_NO_PATROL_MAGIC_FIX=1`).
Builds on `FID-0054-guard-state.md` (mechanism, reviewer-confirmed) and closes its
Layer B for the semantics component. Charter §act lifecycle evidence anchor.

## 1. Root cause of the ORIGINAL stall (why the workarounds existed)

The two `NATIVE_PORT` patches in `chrlvTickPatrol` (the per-tick
`modelSetAnimLooping` force and the `!chrlvPropHasRenderedRoom` magic-entry
suppression) were added in the pre-2026-06-22 era for "stalled patrols". The
stall was a three-part native defect chain, none of it inside
`chrlvTravelTickMagic` (US `0x7F028600`, decomp-matched C at `chrlv.c` — its
virtual-walk math is retail-exact, proven in §4):

1. **The native `chrTickBeams` reimpl never refreshed the patrol visibility
   stamp.** Retail `chrTickBeams` (US `0x7F020EF0`) re-stamps
   `act_patrol.lastvisible60` (US `7F021254-7F02126C`, `sw g_GlobalTimer,
   0x78($s0)`) / `act_gopos.unk9c` (US `7F021274-7F021284`, `0x9C($s0)`) on
   EVERY tick a walking patroller passes the real visibility test
   `sub_GAME_7F054D6C`. The native minimal reimpl (chr.c) had no refresh at
   all, so every patroller's stamp went stale `CHRLV_DEFAULT_TIMER` (150)
   ticks after patrol start regardless of being watched → guards "perpetually
   re-enter WAYMODE_MAGIC" (the workaround comment's own wording).
2. **The fog-era native renderer never set `PROPFLAG_ONSCREEN`** ("fog
   aggressively zeros alpha", chrlv.c comments), so the magic EXIT
   (`chrlvTickPatrol` reads the flag, chrlv.c) never fired → guards froze in
   plain view.
3. **The native anim-looping lifecycle broke walking root-motion**:
   `modelSetAnimation` resets `animlooping=0`; when the walk anim was never
   (re-)set looping, the loop-flip processing in `modelTickAnimQuarterSpeed`
   was skipped and the root-bone XZ delta driving guard movement was never
   computed — guards stood still even in the normal walk path.

(2) and (3) have since been fixed by other work: the room-rendered render
bypass sets `PROPFLAG_ONSCREEN` (broadly — FID-0012), and model.c's scoped
locomotion loop-restore (`portChrActionNeedsLooping`) repaired walking. That
left the two chrlv.c patches **redundant**: an instrumented A/B on current
HEAD (Release, `dam_combat_guard6_agealign`, native-only) with BOTH patches
hard-disabled produced a **bit-identical** movement profile to the patched
build (all 8 patrollers 0–0.7% paused, chr45's two 68–69u steps identical) —
the loop-force is fully shadowed by `portChrActionNeedsLooping`, and remote
guards enter magic anyway (entry-gate diagnostic: chr 41/42/43/45 in mode=6
from `g_GlobalTimer=1`) but **creep-walk at full speed** instead of freezing.

## 2. The LIVE divergence (what this fix corrects)

Retail `chrTickBeams` dispatch (US `.L7F021178`, reconstructed
instruction-level):

- `ACT_PATROL`(14)/`ACT_GOPOS`(15) with `waydata.mode == WAYMODE_MAGIC`(6)
  (`lb 0x38/0x5C($s0)`, US `7F02119C-7F0211C0`) → **frozen**: no
  `chrPositionRelated7F020E40` (US `0x7F020E40` — anim tick + root-motion +
  model→prop position sync), no `modelTickAnimQuarterSpeed`. Only if
  `sub_GAME_7F054D6C` passes at the frozen position (US `.L7F0211C4`) is the
  position synced WITHOUT an anim advance (US `7F0211EC-7F02121C`; the
  reference body's `"VISIBLE MAGIC MODE!!!!"` branch) and the guard rendered
  (→ `PROPFLAG_ONSCREEN` → magic exit next patrol tick).
- Non-magic patrol/gopos → `chrPositionRelated7F020E40` + visibility test +
  the lastvisible60/unk9c refresh of §1.1.
- Magic-entry gate (retail `chrlvTickPatrol` US `0x7F032548`, matched C):
  `lastvisible60 + 150 < g_GlobalTimer` AND `chrlvStanRoomRelated` (US
  `0x7F027DB0`: no room along the chr→pad tile path rendered, via
  `getROOMID_isRendered`, start room included). Nothing else.

The native port ran `chrPositionRelated7F020E40` for EVERY chr every frame,
so magic guards' looping walk anims kept applying root motion to the prop —
continuous creep ≈ normal walking. Combined with the entry suppression this
made all 8 Dam patrollers walk 892/892 ticks vs stock's pause/warp profile
(FID-0054-guard-state.md §5.2).

## 3. Fix (default ON; `GE007_NO_PATROL_MAGIC_FIX=1` = legacy, byte-identical)

- `src/game/chr.c` `chrTickBeams`: retail magic dispatch — freeze + single
  frustum-union visibility evaluation (`chrBeamsFrustumVisibleUnion`, ==
  `sub_GAME_7F054D6C` in 1P) reused as the render gate for magic chrs;
  visible-magic position sync without anim advance; retail
  lastvisible60/unk9c refresh for walking patrol/gopos chrs. The global
  render/ONSCREEN gate for NON-magic chrs stays the room-rendered bypass
  (FID-0012 Phase C unchanged).
- **Sim-determinism scope (`chrPatrolMagicRetailVisible`)**: the port's
  frustum planes are widened by `widenCullHorizontal` (bondview.c), whose
  `gfx_get_aspect_x_factor()` reads the LIVE window drawable (1.0 until the
  window dims are known) — consuming them in sim state coupled the sim to
  window-server timing (observed: a one-off run-to-run `dam_ak47_sustained`
  sim-hash flip, `ca055c77…` vs `1cea291e…`). Retail `sub_GAME_7F0785DC` (US
  `0x7F0785DC`) has no widening — the retail frustum is pure sim state. The
  fix recomputes the planes UNWIDENED (suppression scope
  `portCullWidenSuppressPush/Pop`, also covering `camIsPosInScreenBox`'s
  widen inside `sub_GAME_7F054D6C`), runs the test, then restores the widened
  render planes. Render-side culling unaffected. Post-change: all 7 tape
  replays reproduce bit-exact across repeated runs (ak47 3x
  `8c1c6f13a440c0eb`), and the oracle profile/comparator numbers in §4 are
  unchanged.
- `src/game/chrlv.c`: the per-tick anim-loop force and the
  `!chrlvPropHasRenderedRoom` entry suppression become legacy-only
  (flag-gated). Retail entry gate is restored verbatim.
- Untouched (documented residuals): `chrlvMagicTravelShouldUseRenderedPathExit`
  intro narrowing (chrlv.c, FID-0012-coupled recipe guard);
  `portChrActionNeedsLooping` (model.c — never reached by frozen magic chrs);
  retail's per-actiontype anim gating for OTHER actiontypes (ACT_ANIM/STAND/
  etc. tick unconditionally natively — separate finding class, not patrol).

## 4. Oracle evidence (Release `/tmp/mgb64-fid0014-build`, determinism envelope)

Pinned stock capture per FID-0082: the 2026-07-11 FID-0054
`dam_combat_guard6_agealign` stock trace (single capture, reused for every
comparison; copies under `/tmp/mgb64-fid0014-cap/pinned/`).

Movement profile, pre-onset window (ticks 0–1386; pause = XZ rate < 0.05u/t,
warp = step > 50u):

| chr | stock paused% / warps (max u) | native BEFORE | native AFTER (fix ON) |
|---|---|---|---|
| 2  | 99.7% / 1 (1051) | 0.0% / 0 | 0.0% / 0 (path rooms rendered natively — retail gate refuses entry) |
| 39 | 63.6% / 3 (535)  | 0.7% / 0 | 0.7% / 0 (ditto) |
| 40 | 98.0% / 8 (633)  | 0.4% / 0 | 0.4% / 0 (ditto) |
| 41 | 34.3% / 5 (261)  | 0.0% / 0 | **85.7% / 7 (707)** |
| 42 | 34.2% / 5 (328)  | 0.0% / 0 | **85.7% / 7 (711)** |
| 43 | 46.7% / 6 (654)  | 0.0% / 0 | **85.7% / 7 (699)** |
| 44 | 46.7% / 4 (347)  | 0.0% / 0 | 0.0% / 0 (path rooms rendered natively) |
| 45 | 99.5% / 2 (1461) | 0.0% / 2 (69) | **99.6% / 2 — warp steps [1301.0, 1460.8] BIT-IDENTICAL to stock** |

- **chr 45 (the clean fully-unseen case): equal-age position delta vs pinned
  stock collapsed 566.5u mean / 1460u max → 0.00 for all 174 common ticks
  (ages 0..1387)** — entry timing, virtual-walk rate, warp positions and the
  150-tick constant all retail-exact end-to-end.
- Post-onset `--align tick` comparator vs pinned stock: `guards.pos` axes
  1468/1339/1424 → 1211/849/1149 (−18/−37/−19%), `guards.room` 132 → 49
  (−63%), total 11981 → 10868. `combat.*`/`projectiles` unchanged.
- Residual (chr 2/39/40/44 walking, chr 41/42/43 pause-fraction offsets):
  the native camera timeline genuinely renders their patrol-path rooms where
  stock's menu/briefing+intro-camera timeline does not — a capture-recipe
  visibility-set difference feeding retail's own path-room gate, plus
  FID-0012's rendered-set breadth. Not patrol semantics; not closable here.
- **Downstream RNG domino (FID-0063): NOT moved on this route.** The native
  `rng.call_count` stream is bit-identical before vs after the fix (0/681
  records differ); `rng_callcount_diff` vs pinned stock unchanged
  (cum −5979 over 114 common ticks, per-tick min/max −487/+397). The
  pre-onset patrollers sit outside `chrCheckTargetInSight`'s pre-checks here,
  so their positions never reach the probabilistic draw on this route. The
  FID-0063 §9.2 perception-class residual needs re-measurement on routes
  where patrollers approach the player before it can be attributed further.
- Negative control: `GE007_NO_PATROL_MAGIC_FIX=1` reproduces ALL seven
  committed tape hashes byte-identically (incl. `dam_combat_guard6`
  `3c8939968e0eb50e`, `dam_ak47_sustained` `e228f3a765adc3eb` via its smoke)
  and the legacy movement profile. Fix-ON tape hashes shift (patrol positions
  are sim state) — re-recorded separately citing FID-0014.

## 5. Regression lane

`tools/fidelity/patrol_magic_profile_smoke.sh` (ctest
`port_patrol_magic_profile_smoke`, tier 3 + verify manifest): native-only
`dam_combat_guard6_agealign` capture, profile via
`tools/fidelity/patrol_profile.py`; asserts fix-ON stock-shaped profile
(chr45 ≥90% paused with a ≥1000u warp; chr41/42/43 ≥50% paused) AND the
negative control (flag=1 → chr45 ≤5% paused, no ≥1000u warp — fail-on-revert:
a workaround revert or flag-polarity bug reddens the lane).

## 6. Full-verify adjudications (2026-07-11, tree b1fe896)

First full-verify pass (on c7f51ff) was green except four reds, each
attributed exactly:

1. **gate_routes / runway_traverse hash flip** (`d5dd956c…` once vs recorded
   `56c32149…`): the fix scope's exit path recomputed widened planes and
   re-read the live window drawable mid-tick (§3 sim-determinism note).
   REPAIRED in b1fe896 (byte-restore of the frame's plane state:
   `portRetailFrustumEnter/Leave`, bondview.c). Runway replay 12/12
   bit-stable at `56c32149…` incl. under concurrent build load; all other
   tape hashes unchanged; flag-OFF byte-identity re-proven 7/7.
2. **port_dyn_glass_stress_smoke**: its pane break is EMERGENT guard return
   fire (~frame 126, 56 frames after Bond's scripted burst — flag A/B +
   GLASS-SHATTER tracing), which legitimately disappears under the faithful
   patrol default. The FID-0007 allocator contract is sim-world-independent
   ⇒ scenario pinned to the legacy world (`GE007_NO_PATROL_MAGIC_FIX=1` in
   the lane env, header-documented). A guard-independent deterministic
   shatter refit (canonical `dam_regular_glass_shatter_probe` targeting,
   which DOES shatter under fix-ON — glass destruction itself is not
   regressed) has no overflow window (fits at 8192, frame-starved at 7168).
   Lane PASS both dyn sides after the pin.
3. **port_perf_budget_smoke**: thermal/load flake class (in-suite under
   sustained load); solo re-run on the fix build: ALL budgets met
   (worst 5.8 ms vs 16.6 ms hard budget).
4. **regression_test (20-level)**: local ROM-derived goldens absent in the
   fresh worktree; populated from the primary checkout. Flag-OFF replay:
   **state PASS 20/20** (gameplay-trace byte-identity campaign-wide; pixel
   lanes stale-golden vintage, pre-existing). Fix-ON: 13 levels diverge in
   `combat_oracle.guards[N].pos[*]` ONLY (frozen-at-pad signatures, e.g.
   Dam guard32 y −603.8→−633.5 = the chr45 pad-Y freeze), 7 levels
   byte-identical — exactly the expected patrol-position baseline-shift
   class. Local goldens re-recorded on the fix default (20/20 PASS;
   gitignored per-checkout artifacts — other checkouts re-record via
   `tools/regression_test.sh --baseline`).

**Verify status on the final tree (b1fe896): `landed`, NOT promoted to
`verified`.** Two full-suite re-runs on b1fe896 collapsed in a machine-wide
OOM cascade unrelated to the change (`Cannot allocate memory` killing
`mkdir`/`sleep`/`head` inside validation_common; Docker Desktop VM holding
11 GB RSS + concurrent sibling-agent sessions; one transient
runtime-lock-timeout FAIL from cross-agent lock contention). Per-lane
evidence on the final build is green where runnable: runway tape 12/12
whole-sim-hash bit-stable (incl. under load), 20-level flag-OFF gameplay-
trace identity, fix-ON regression goldens 20/20, dyn_glass PASS both dyn
sides, perf_budget solo PASS, patrol lane PASS both flag sides, and the
first full verify pass (tree c7f51ff, pre-OOM) was green apart from the
four adjudicated items above. The next loop iteration on an unloaded
machine should run `tools/fidelity/verify_all.sh` to promote
landed→verified.

## Note (2026-07-11): full uncap purity gate found a render-only leak this fix exposed — repaired under FID-0089

The FID-0014 verify ran only the QUICK 3-level uncap purity mode (33/9/24). The
FULL `tools/uncap_purity_gate.sh` (FID-0033) then went **RED on levels 22 and 24**
("render-only frames perturbed sim state (vanilla stable)" —
`docs/fidelity/reports/verify_fb9beceae183_uncap_purity_gate_2.log`).

Root cause is **not** in FID-0014 code. Evidence: every FID-0014 visibility
evaluation is byte-identical vanilla-vs-fuzz (3496/3496 traced lines), the sole
divergent pool word is `ChrRecord.ptr_SEbuffer1` (a guard's fired-weapon SFX
handle, native offset 0x1b0), and with `GE007_NO_PATROL_MAGIC_FIX=1` the fuzz run
is byte-pure. The defect is a **pre-existing render→sim audio-pump leak**:
`portAudioFrame()` advances SFX voice playback once per loop iteration (including
render-only 0-tick frames), and a finished voice is disposed by the next
`sndGetPlayingState()`, which NULLs the owning `ptr_SEbuffer*` slot (hashed pool
state). FID-0014's faithful magic-freeze correctly shifted guard fire timing on
22/24 so a gunshot's finish straddled the gate's exit tick cadence-sensitively,
exposing the latent leak. Repaired by skipping the audio pump on 0-tick frames —
**FID-0089** (`docs/fidelity/derivations/FID-0089-uncap-audio-pump.md`,
`GE007_NO_UNCAP_AUDIO_FIX=1` negative control). FID-0014's sim behavior is
unchanged (tapes 7/7 byte-exact, patrol-magic profile smoke PASS both polarities).
