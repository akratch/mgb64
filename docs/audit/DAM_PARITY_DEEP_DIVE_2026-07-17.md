# DAM Parity Deep-Dive — every remaining renderer & simulation gap (2026-07-17)

**Scope:** Dam (LEVELID 33), branch `feat/webgpu-backend` @ `b845955`, WebGPU default backend.
**Method:** instrumented both-sides sweep against the ares stock oracle (fresh captures this session), three parallel source/asm root-cause lanes, full ledger/audit reconciliation, and prior-art fold-in of `docs/audit/DAM_PARITY_HUNT_2026-07-17.md` (the halted predecessor hunt).
**Evidence root (session-local, ROM-derived, do not commit):** `…/scratchpad/dam_sweep/` — combat/intro/glass traces, stock PPMs, screenshots, comparator outputs. Repro commands inline per finding.

Companion docs: fidelity ledger `docs/fidelity/LEDGER.md` (FID-*), `docs/audit/DAM_PARITY_HUNT_2026-07-17.md` (DAM-R1..R3), `docs/fidelity/combat_oracle_fields.md`, `docs/design/FAITHFULNESS_S_TIER_PLAN.md`.

---

## 0. Executive summary

Dam is the best-instrumented level in the port (the only one with oracle routes), and after the 2026-07 fix waves its **remaining gaps concentrate in seven areas**:

1. **Guard-AI/combat divergence cluster (sim, P1)** — live and re-confirmed on today's build with a tick-aligned both-sides capture: the PRNG *call-count phase* desyncs from combat onset (`rng_seed` diverged on 114/114 aligned ticks), dragging guard anim phase, positions, actiontype, and perception timing with it (FID-0054/0055/0063). Root cause is known (per-tick `randomGetNext()` call-count differences, not PRNG math); the fix path runs through FID-0011/0012/0014-residual chrTickBeams semantics.
2. **Glass rendering cluster (renderer, P1)** — the center-glass framebuffer-memory blend (FID-0001) is still not stock-exact; the exact per-triangle snapshot A/B (`GE007_XLU_SNAPSHOT_MODE=pertri`) exists **only on the GL fallback — the default WebGPU backend silently ignores it** (FID-0002, newly confirmed this session, `gfx_webgpu.c:2313-2331`); shatter/opacity/crack parity (FID-0004) has *nothing* validated yet.
3. **Non-retail renderer defaults active on every Dam frame** — three of them **unledgered until today**: a global G_FOG force-inject/strip in `gfx_sp_geometry_mode` (`gfx_pc.c:20148-20176`), the sky-backdrop depth special case, and the room-XLU coverage-memory gate; plus the ledgered scissor/XLU-sort/portal-clip trio (FID-0118/0041) and the WebGPU-only DAM-R1 sky seam from the predecessor hunt.
4. **Collision-primitive fidelity (sim, latent-but-hot)** — `intersectLineTriangle` (24.3% decomp match, every bullet) and the ray-vs-OBB float-division rewrite (doors/props) carry boundary-flip divergence classes on every Dam shot (FID-0099/0116). Conversely FID-0102 (waypoint groupNum stub) was **defused this session by ROM-byte proof** across all 21 retail setups.
5. **Dam intro divergence beyond waivers (sim) — root-caused this session**: both sides play the same phase-3 weapon-draw animation; only the *onset* differs. Native fires it at D43's hardcoded swirl timer (seg 4, t=40.0) while retail fires it from an RNG-jittered AI sleep-wake boundary (t=57.05 today, ≈41.05 when D43 was tuned). D43 replicated a point-sample of a scheduling-dependent event as a fixed timer — the D36 waiver removal was premature; FID-0117 exonerated (§3.3).
6. **The instruments themselves** — three harness defects found live this session: the S2 pixel ratchet has been **vacuously green** (empty stock cache ⇒ 0 checkpoints swept ⇒ "pass"); the movement comparator on combat routes still carries the FID-0062 frame-vs-tick skew (it manufactured a false "sprint-speed regression" that cost a bisect to disprove); and visual-oracle routes don't pin the faithful-HUD config keys, so the port-only minimap contaminates every parity frame.
7. **Monitor screens (NEW, root-caused to one hop this session)** — Dam's security-hut/alcove monitor screens render flat green instead of retail's animated green text. Trace-proven: the monitor animation machinery is healthy; the defect is the PC texture pool **not honoring retail's format-reinterpretation** (IMAGE_MONITOR_TEXT drawn through an I8 tile while the pool decodes it as its catalogued CI4) — a **generalized class** affecting any DL that re-tiles a texture under a different format (§4.7). A second, separate defect detaches two screens from their owner transform (floor-level quads).

**The generalized wins** (§6) are: comparator tick-alignment everywhere, camera-registered pixel checkpoints (FID-0115), faithful-HUD route pins, the non-retail-default census flag-by-flag, the raw-offset-cast static sweep completion, and content-bound proofs for silent static tables.

### Scorecard

| Area | Status | Verdict |
|---|---|---|
| Movement (player walk/sprint) | ✅ parity | Sprint boost engages at 3.0s on both sides; the failing gate was a comparator artifact |
| Floor/stan oracle | ✅ parity on Dam route | `floor.height` 1/114 divergent (noise); `stan_id` encoding non-comparable (FID-0053) |
| Guard AI / combat | ❌ diverges from onset | PRNG phase + anim_hash + pos + actiontype (FID-0054/0055/0063) |
| Glass composite | ❌ not stock-exact | FID-0001/0002/0004; pertri A/B missing on default backend |
| Renderer defaults | ⚠️ non-retail by design + 3 unledgered | G_FOG force, sky-depth, CVG gate, scissor, XLU sort, portal clip |
| Backend parity (WebGPU vs GL) | ❌ DAM-R1 sky seam | WebGPU-only over-bright sky/backdrop (predecessor hunt, unresolved) |
| Intro (camera) | ✅ camera path exact | swirl/selected-camera digests match |
| Intro (Bond anim) | ⚠️ onset-scheduling divergence | same anim both sides; D43 fixed-onset vs retail AI-wake grid (root-caused §3.3) |
| HUD | ⚠️ known divergences | ammo icon anchor/flip (FID-0067), health-bar GBI (FID-0080), minimap-in-parity-frames |
| Monitor screens | ❌ flat green + detached quads | format-reinterpretation dropped by texture pool; owner-transform detach (§4.7) |
| Collision primitives | ⚠️ boundary classes | FID-0099/0116 live; FID-0102/0119/0120/0121 defused/latent |
| Audio | ⚠️ unmeasured | FID-0025/0026/0027 open, blocked on route coverage |
| Instruments | ⚠️ three defects found | vacuous pixel gate, movement-comparator skew, HUD contamination |

---

## 1. What was run (and what the instruments can see)

Fresh captures this session (all serialized under the `/tmp/ge007_runtime_validation.lock` protocol; a concurrent autonomous session was active on this machine, so several steps waited on or lost the lock — see §7.6):

| Capture | Route | Result |
|---|---|---|
| Combat both-sides | `dam_combat_guard6` (native `--native-full-trace` + ares stock, 6500f) | movement comparator FAIL 29/120 (artifact, §3.1); tick-aligned combat census §3.2 |
| Speed A/B ×3 | same route, binaries `build-release` (Jul 5), `build-gl` (Jul 16), `build/ge007` (HEAD) | **byte-identical divergence numbers across all three** — disproved regression |
| Glass visual | `dam_glass_visual_probe` (screenshot @ game-timer 1190 both sides) | PASS thresholds; §3.4 findings; stock PPM seeded into `build/oracle_cache` |
| Intro swirl | `dam_intro_swirl_bond_anim` | FAIL: 54 unwaived (§3.3); camera path digests clean |
| Pixel lane | `sense_pixel_sweep.sh --route dam_glass_visual_probe` | see §3.5 |
| Registered monitor probe + DAM-R1 GL/WebGPU A/B | direct binary runs | see §3.5 |

**Instrument limits (read §7 before trusting any green):** the S2 pixel lane covers exactly **1 of 20 stages** (this glass checkpoint) and is blocked on camera registration for criterion-2 closure (FID-0115); the S3 RDP native-vs-ares differential comparator is **not built** (FID-0043 — only the native-side fingerprint sweep exists, 22 distinct RDP configs verified on Dam); ares stock captures are ±1 VI-frame nondeterministic post-onset (FID-0082); `stan_id/stan_flags` encodings are non-comparable across sides (FID-0053); ares-side `projectiles[]` and shot accumulators are unpopulated/non-retail-equivalent (FID-0047/0048).

---

## 2. Prior art folded in

- **`DAM_PARITY_HUNT_2026-07-17.md`** (halted mid-hunt): **DAM-R1 (P1)** WebGPU-only over-bright sky/backdrop seam at intro swirl f190 and on the gameplay walkway — GL clean, so a backend defect in the default backend; **DAM-R2 (P2)** room-admission gap at the reservoir establishing camera, same-wrong on both backends (FID-0008/T25 class); **DAM-R3 (P3)** far-horizon gaps + sky-through-rock specks, likely faithful (train-sky-leak class). The owner-reported "water bleeding through geometry" was **not reproduced** anywhere; DAM-R1's blue sky patch is the best candidate for what was seen. This session's §3.5 A/B re-tests DAM-R1 on HEAD.
- **FID-0122 (fixed)** — Dam door-panel white = DLCOLLISION 512-entry silent overflow; the class sweep that followed (2c2df9b) is the model for §6.2.
- **FID-0066/0056 (fixed)** — full-auto cadence (player + NPC) on Dam.
- **FID-0083 (landed)** — glass shot-depth tolerance now retail-exact by default.

---

## 3. Fresh instrumented findings

### 3.1 The "sprint speed divergence" that wasn't — movement comparator skew (harness defect, P1 for trust)

The gate route `dam_combat_guard6` FAILs its movement compare vs a live ares stock capture: native `move.speed[0]` ramps 1.08 → 1.35 from aligned record 60 while stock holds 1.08 (native walks 2731 units vs stock 2070 over the 120-record window; raw stick inputs identical, both sides inject stick_y=80).

**Root cause (proven):** comparator record-granularity skew, not a sim divergence. Native emits one movement record per rendered frame (= 3 ticks at `speedframes 3`, `move.global` advancing +3/record); the deduped stock trace is per-tick. The 1:1 "aligned" pairing therefore compares native t=3N against stock t=N — a 3× time-scale skew, the exact class FID-0062 fixed in `compare_combat_trace --align tick` but which still lives in the movement comparator path of `movement_oracle_capture.sh` (`tools/compare_movement_trace.py`).

The sprint mechanic itself is **at parity**: `speedforwards = clamp(±1) × 1.08 × speedboost` with boost engaging after `speedmaxtime60 ≥ THREE_SECOND_TICKS` (`src/game/bondview.c:13265-13320`). Native boost onset = tick 180 = aligned record 60 ✓. Stock reaches the same 1.35 by tick +292 from its own onset (measured from the per-tick deduped stock trace) — it simply lands past the 120-record compare window under skewed pairing.

**Disproof of regression:** `build-release/ge007` (Jul 5), `build-gl/ge007` (Jul 16) and HEAD produce **byte-identical** comparator output (same 1.0800→1.1124 first delta, same 2730.863 distance, same 29/120). Nothing moved; the lane had simply never been run against a live ares pair (FID-0076: gate routes lack paired stock captures).

**Fix shape (S):** `compare_movement_trace.py` already has an `--align global` mode (`align_records`, `tools/compare_movement_trace.py:295-342`) that would pair tick-exactly, but it can't be used raw because stock's `move.global` includes the menu boot (gameplay starts at global 1146 on stock vs 0 native). The correct fix is **onset-rebased global alignment**: rebase each side's `move.global` to its first-moving record, then key-pair — the movement-lane twin of FID-0062's `--align tick`. Until then, every movement-vs-ares verdict on a `speedframes≠1` route is untrustworthy in *both* directions.

### 3.2 Combat census on today's build — tick-aligned (`--align tick`, 114 real pairs)

`compare_combat_trace.py --align tick` over the fresh both-sides capture:

| Field | Divergent field-hits | Reading |
|---|---|---|
| `guards.anim_hash` | 4104 | FID-0055 live — animation phase desync across the roster |
| `guards.present` | 1584 | inflated by 44 synthetic pairs + 187/60 excluded records; treat as alignment artifact until re-measured |
| `guards.pos` (0/1/2) | 1199/865/1176 | FID-0054 live — patrol/movement semantics (FID-0014 residual Layer B + FID-0012 breadth) |
| `guards.actiontype` | 395 | H4 dispatch collapse (FID-0011) territory |
| `combat.rng_seed` | 114 (= every real pair) | **FID-0063 confirmed live**: PRNG phase desyncs at/before onset and never re-converges |
| `guards.target_visible` | 91 | perception timing (chrTickBeams visibility semantics) |
| `guards.health` | 62 | downstream of shot/perception timing |
| `floor.height` | **1** | floor/stan lane effectively at parity on this route |
| `floor.stan_id` | 114 | encoding mismatch — not comparable (FID-0053), ignore |
| `combat.shots_fired_total` | 94 | ares side lacks a retail-equivalent accumulator (FID-0048), ignore |

**Reading:** the sim-side story is unchanged from the 07-10/11 analyses but now re-proven on HEAD with correct alignment: **one systemic driver (PRNG call-count phase, FID-0063) plus the chrTickBeams/patrol semantics cluster (FID-0011/0012/0014-residual)** explain the guard divergences; there is no evidence of a *new* sim regression on Dam. `guards.present` needs a re-measure once the comparator emits per-guard presence stats excluding synthetic pairs.

### 3.3 Dam intro: phase-3 animation *onset* diverges — D43's fixed-onset replication is the defect (ROOT-CAUSED this session)

`dam_intro_swirl_bond_anim` both-sides: camera **byte-exact** (`selected_camera`/`path`/swirl digests match), 54 divergences unwaived by the D-ledger from `mode3:s4:t42.05`. Initial read ("native plays the wrong animation") was **corrected by the root-cause lane**: both sides play the *same* three-phase intro sequence — ACT_BONDINTRO → ACT_STAND 48f idle loop (entry 33144) → ACT_ANIM phase-3 weapon-draw pose `ANIM_aim_one_handed_weapon_left_right` (anim 99, entry 31420, played 0..96 @0.5, hold-last). **Only the phase-3 onset differs**:

- **Native**: D43's `bondviewMaybeDriveIntroPhase3()` (`src/game/bondview.c:7072-7144`, default ON, opt-out `GE007_NO_INTRO_PHASE3`) fires at a **hardcoded** swirl coordinate — `intro_camera_index ≥ 4 && transition_timer ≥ 40.0`. Proven by the native log line `intro phase-3: anim=99 end=96 fired at swirl seg=4 timer=40.05`.
- **Stock**: the Bond-cinema chr's **AI list** fires `PlayAnimation` at an **RNG-jittered AI sleep-wake boundary** (`chrlv.c:11478` sleep decrement; ACT_STAND re-arms `sleep = randomGetNext()%5 + 14` at `chrlv.c:6554` — a 14–18-tick wake grid whose phase accumulates from boot history). Today it fired at **t=57.05**; the 2026-07-08 capture D43 was tuned against fired at ≈41.05.

The 54 unwaived divergences are exactly the 6 aligned keys in the lead window (s4 t42.05→t55.05) × 9 `intro.bond_anim` fields; **after stock's onset the sides agree** on every compared anim field except the D41-waived ~8-frame lead. So: **this is the D36 family regressing because D43 replicated a point-sample of a scheduling-dependent event as a fixed timer — not a new class, and FID-0117 is NOT implicated** (root-motion accumulation only; also the failing fields measure the stock side, which native code cannot touch).

**Confirm/refute (cheapest first):** (E1) rerun native with `GE007_INTRO_PHASE3_ONSET=56` → predict all 54 vanish; (E2) double stock capture → measures onset stability; (E3) `tools/aiParse.c` on the Dam setup to name the AI wait preceding `PlayAnimation` (native `[AI_TICK]` log under `GE007_DEBUG=1`); (E4) `GE007_NO_ANIMFRAME2_ROOTFLAG_FIX=1` → predict no change. **Fix options** (fidelity order): reinstate the 9 scoped D36 `intro.bond_anim.*:mode3` waivers; teach the comparator phase-3 *event alignment* with an onset tolerance (~2 sleep periods); or mechanism-faithful — execute the Bond-cinema AI list during frozen-camera intros so native's onset is wake-grid-driven (noting even that cannot tick-match any single stock capture). Latent extra: native reverts to ACT_STAND at swirl end (icam 7 t=45.00) while stock holds ACT_ANIM — currently uncaught, will surface with a longer stock capture.

### 3.4 Glass checkpoint frames — what the eyes say beyond the numbers

The `dam_glass_visual_probe` pair (security-hut interior, game-timer 1190) passes route thresholds, but visual inspection of the native BMP vs stock PPM surfaced four concrete items:

1. **Monitor screens render flat green on native** where stock shows the animated green-text screen texture; the left desk's monitor may not be drawn at all (viewpoint-confounded — resolved by the registered probe, §3.5). First suspect was the `PROPDEF_MULTI_MONITOR` converter (FID-0036's untested risk case), but static audit this session cleared it: `ImageNums` is `u8[4]`, so the raw `memcpy` at `src/game/prop.c:2138` needs no byte-swap and fits the 596-byte N64 record exactly (offset 0x250+4). Suspicion moves to the monitor screen-image *rendering* path (`monitorSetImageByNum` / `setupMultiMonitor` runtime records, `prop.c:1117-1220`, and the screen-texture draw) — the visual signature is "green background quad renders, text/detail texture missing." If confirmed, this is a **Dam-visible, user-facing defect** (security-hut and tunnel-alcove monitors). Next instrument: trace `monitorSetImageByNum` image numbers on Dam boot + registered-camera ROI diff.
2. **Ammo HUD divergence visible live** — bullet icon tilted/shifted vs stock (FID-0067's anchor/flip math, already `verified` in the ledger; this is its first in-frame Dam evidence), and the ammo *count* renders green vs stock white.
3. **The port-only minimap renders inside the parity frame** (top-right wireframe inset). Native default `Input.MinimapEnabled=1`; the faithful-HUD ruling (b845955) changed **web-shell defaults only**. Every visual-oracle route inherits the modern HUD → guaranteed non-retail pixels in every checkpoint. **Fix shape (S, generalized):** pin the four faithful-HUD keys in route `native_config` (done for the registered probe below) or make the pixel lane force them.
4. Full-frame diff (85.7% changed) remains dominated by the FID-0115 viewpoint-registration offset — unchanged conclusion: **no per-stage pixel verdict is possible until checkpoints are camera-registered.**

### 3.5 Registered monitor probe, pixel-lane verdict, DAM-R1 A/B

- **DAM-R1 reproduces on HEAD**: intro establishing frame 190, identical faithful config both backends — the WebGPU frame shows a **blue sky sliver leaking at the top-center frame edge** that the GL frame does not render. The predecessor hunt's P1 WebGPU-only sky/backdrop defect is alive on the default backend. Evidence: `dam_sweep/damr1_{webgpu,gl}.bmp`. (Secondary observation: the GL frame is visibly softer than WebGPU under the same config — worth a filtering-parity look when DAM-R1 is fixed.)
- **Monitor defect confirmed and sharpened** (registered probe, faithful-HUD pins, minimap off): the guard-side desk monitor renders a **flat-green screen with no text texture**; two further monitor **screens render as bare green quads at floor level with no CRT bodies** — the screen sub-draw is detached from its owner/part transform. Converter asymmetry is the prime suspect: `PROPDEF_MONITOR` imports `OwnerOffset`/`OwnerPart`/`ImageNum` (`prop.c:2122-2129`) but `PROPDEF_MULTI_MONITOR` imports *only* `ImageNums` and memsets the rest (`prop.c:2134-2140`). Evidence: `dam_sweep/monitor_probe_registered.bmp` vs `glass_probe2/stock.png`. (Root-cause lane report folded in at §4.7.)
- **Center glass pane is present on native** — FID-0001 is a blend-exactness question, not a missing-pane question.
- **Ammo-total text color diverges**: native renders the total count green where stock renders white (both fresh frames) — small HUD divergence adjacent to FID-0067's anchor/flip findings.
- **Pixel lane crashed on its first real-route run**: `sense_pixel_sweep.sh` line 311 `NORMALIZE_FLAGS[@]: unbound variable` — empty-array expansion under `set -u` on macOS `/bin/bash` 3.2 (the `#!/bin/bash` shebang), **and the lane still exited 0**. The gate-mode self-test never exercises the real-route path, so this crash was invisible. Two fixes: `${NORMALIZE_FLAGS[@]+"${NORMALIZE_FLAGS[@]}"}` (or bash≥4 shebang), and a nonzero exit on route-loop crash.
- **Pixel lane verdict (rerun under bash 5, cache seeded)**: the lane now sweeps for real — `sense_pixel_20260717T081843Z.json`: **2 unexplained clusters, worst bbox [0,20,640,440] (frame-spanning), mean_delta 33.45** — the same viewpoint-registration signature FID-0115 recorded (97576/34.1 on 07-14); all other clusters auto-attributed to documented approximation classes. Conclusion unchanged: camera registration (FID-0115) is the sole blocker between this lane and a real per-checkpoint renderer verdict on Dam.

---

## 4. Renderer gaps on Dam — consolidated & ranked

Ranked by per-frame Dam exposure × severity. FID anchors re-verified against current source this session (several ledger line anchors had drifted; corrected ones noted).

### 4.1 Backend defect: DAM-R1 WebGPU sky/backdrop seam (P1)
GL-clean, WebGPU-wrong over-bright sky patch (intro swirl f190 + gameplay walkway). Predecessor evidence + this session's HEAD A/B. Since WebGPU is the default backend, this is the **most user-visible open Dam renderer item**. Not yet ledgered — should be filed as a new FID (renderer, port-defect, WebGPU). Discriminators: `GE007_TINT_SKY=1`, `GE007_RENDERER=gl`, `GE007_WEBGPU_DUMP_SURFACE`.

### 4.2 Glass cluster (P1 family)
- **FID-0001** (root-caused): center-glass (pad10092) framebuffer-memory blend not stock-exact. Native emulates RDP memory-color/coverage blending by snapshot-and-sample (`gfx_pc.c:5391-5476` gate; WGSL byte-model `gfx_webgpu_shader.c:465-506`); residual per `RENDERING_REGRESSION_NOTES.md#13` — the footprint-LOD fix corrected source sampling only. Fix path (L): MG.1 per-draw diag instrumentation → confirm/kill exact-CC hypotheses → ROI pixel oracle vs ares (`glass_pad10092_impact_visual_regression.sh`, currently unwired).
- **FID-0002** (triaged, scope extended this session): per-batch snapshot means overlapping coplanar panes composite against a stale snapshot. **`GE007_XLU_SNAPSHOT_MODE=pertri` is honored only by GL (`gfx_opengl.c:2183-2295`); Metal AND the default WebGPU backend silently ignore it** (`gfx_webgpu.c:2134-2331` — no read anywhere). Fix shape: pertri port to WebGPU is S (mechanical, slow-path A/B); the real fix is batch-split-at-overlap (M, MG.2).
- **FID-0004** (discovered, nothing validated): tinted-glass 16/255 min-opacity floor (`chrobjhandler.c:581-611`, A/B `GE007_TINTED_GLASS_MIN_OPACITY=0` never validated), shatter hit-count parity, crack accumulation/fade. The seven `dam_regular_glass_shatter_*` routes exist and map cleanly onto these sub-questions (see glass-lane inventory in the session report) but the key gates (`glass_route_parity_regression.sh`) are unwired.
- **FID-0003 / FID-0083** (landed): shard scale signed off (residual: pixel-level confirmation + coverage regression unwired); shot-depth tolerance retail-exact by default.

### 4.3 Non-retail defaults active on every Dam frame
Ledgered trio (FID-0118/0041) + **three NEW unledgered defaults found this session**:

| Default | Anchor | Dam exposure | Faithful-mode action |
|---|---|---|---|
| **G_FOG force-inject/strip (NEW)** | `gfx_pc.c:20148-20176` (opt-out `GE007_KEEP_TEXTURE_GEN_FOG` for the strip half) | every fogged Dam draw — i.e. nearly all of them | file FID; A/B route with force off; decide faithful default |
| **Sky-backdrop depth special case (NEW)** | `gfx_pc.c:5515-5535` + consume `17777-17782` (`GE007_DISABLE_SKY_BACKDROP_DEPTH`) | Dam sky every frame | file FID; fold into FID-0024 classification |
| **Room-XLU CVG-memory gate (NEW)** | `gfx_pc.c:5373-5389` (`GE007_DISABLE_ROOM_XLU_CVG_MEMORY`) | Dam backdrop strips | file FID; pixel-oracle the gate reasons |
| Per-room exact scissor OFF | `bg.c:4202-4279` (`GE007_EXACT_ROOM_SCISSOR=0`) | minor visible; fill-rate semantics | flip behind pixel-oracle evidence (S) |
| Room XLU depth-sort ON (FID-0041) | `bg.c:449-602` + `gfx_pc.c:13921-14471` (`GE007_SORT_ROOM_XLU`) | any overlapping translucent rooms (dam-base water thru tunnel mouths, glass) | faithful mode pins DL order — decision + A/B (S) |
| Portal near-plane clip ON | `bg.c:8294-8455` (`GE007_PORTAL_NEAR_CLIP`) | grazing frames (intro flyby, tunnel mouths); interacts with DAM-R2 | emit full-view bbox instead of discarding grazing verts (S/M) |

### 4.4 Interpreter sharp edges (FID-0020/0022/0023/0024/0104) — Dam status
- **FID-0024** (Z_UPD-without-Z_CMP global rule, `gfx_pc.c:17774-17798`): all game-code render modes pair the bits; exposure is via **baked ROM DLs**, unenumerable statically — the R15 logging lane (log draw-class + other_mode_l on Z_UPD-no-Z_CMP) has still never been run. Fix: ship the lane, classify on a Dam route (M).
- **FID-0104** (texWriteLoadToTmemAddr rewrite): GBI-stream divergence on every Dam CI4 prop-texture load (tile-5 vs retail 7, pipesync suppression, 16b fast path) — **pixel-neutral natively** as far as static analysis shows; one concrete hazard: tile 5 double-duty when `maxlod ≥ 5` shares a dedup slot retail never collides. Restore retail shape (S/M) mirroring the verified `texWriteLoadToTmemZero`.
- **FID-0020 seven edges**: all **latent on Dam** except texgen s16 truncation (plausible on env-mapped chrome; `gfx_pc.c:17113-17217`) and footprint-reject collateral eviction (`gfx_pc.c:15488-15509`, fires only behind `[TEX-REJECT]`). Item 7 (texSelect bounds) is **substantively mitigated** at the `texLoad` choke point since the ledger entry (`image.c:3352-3375`) — ledger stale.
- **FID-0022** (G_ZS_PRIM): also discarded by WebGPU (`gfx_webgpu.c:2003`) — ledger evidence should be extended to the default backend.
- **FID-0122 residual**: WebGPU `s_shaders[1024]`/`s_samplers[64]` remain correctly excluded from the silent-overflow class — content-bounded, warn-once, evict-not-alias (WEB-050/068); optional S hardening: invalidate gfx_pc's `ShaderProgram*` per-CC cache on eviction.

### 4.5 Room admission / geometry (DAM-R2, DAM-R3, FID-0086)
DAM-R2 reservoir under-admission at the establishing camera (same-wrong both backends; interacts with portal-clip default and the FID-0008 widener family). DAM-R3 horizon gaps likely faithful. FID-0086: intros under `--faithful` may under-fill rooms (wideners off). Instrument: `GE007_TRACE_ROOM_CLASSIFY=1` + stock capture at the same camera (now unblocked — ares harness verified live this session).

### 4.6 HUD (FID-0067/0080/0064/0084 + minimap contamination)
Ammo icon anchor/flip visible in the fresh Dam frames (§3.4); health/armour-bar GBI divergence (FID-0080, flagged "esp. 0.2-scale stages Dam/Surface"); plus the route-config minimap contamination (§3.4.3). Watch-menu tint/aspect items (FID-0068/0098) reachable from Dam pause.

---

### 4.7 Monitor screens: flat-green, texture term constant (ROOT-CAUSE NARROWED this session)

Pixel measurement of the registered probe pins the mechanism: the two floor-level screens measure **exactly (0,128,0)** — `COLOR_BARELYGREENOPAQUE`, the `MONRGBA` target of the `monAnim05GreenTextUp` command list (`chrobjhandler.c:1716`, color at `chrai.h:162`) — and the desk CRT measures constant (0,80,0). So the monitor *interpreter, color integrators, DLCOL runtime registration (FID-0122 machinery) and quad transform all work*; only the **TEXEL0 term is spatially constant**. Dam's green-text screens are an I8 intensity texture (`monitorimages[29]` = IMGTEXT, `assets/GlobalImageTable.c:592+`) modulated by the green vertex shade via `G_CC_MODULATEI` (`othermodemicrocode.c:735-737`); the text detail is *entirely* the texel term.

Draw path: `process_monitor_animation_microcode` (`chrobjhandler.c:28020`, = PD `tvscreenRender`) builds the quad + ST from `width*(xmid±xscale/2)*32`, `texSelect` emits SETTIMG with the native IMAGESEG token resolved lazily mid-DL (`gfx_pc.c:1299` → `texLoadFromTextureNum` → `import_texture_i8` `gfx_pc.c:15329`). No `[TEX-UPLOAD-FAIL]`/`[TEXLOAD][RENDER-HEALTH]` in any probe log — whatever fails, fails silently.

**Ranked hypotheses:** **H1** — per-vertex ST spread degenerates (scroll/scale integrator or the fast3d texcoord path for this GBI sequence collapses UVs to a single texel; uniquely explains *two different* constant greens — different screens freeze on different texels); **H2** — WebGPU white-fallback bind (1×1 white substitute, `gfx_webgpu.c:2345-2357`) — the WEB-068 bind-group-ABA class, plausible because monitorimages are lazily loaded (not in `texPreloadRuntimeImageEntries`), but doesn't explain (0,80,0); **H3** — SETTIMG token resolves NULL mid-DL, stale texture stays bound (weakest; would trip counters). Converter, blank-pool fallback and DLCOL exhaustion are **ruled out** (§3.5; blank-pool always logs and has no defined color).

**Discriminating experiments — all three RUN this session, verdict reached:**
1. `GE007_RENDERER=gl` A/B at the registered checkpoint: **GL renders the identical flat-green screens (and identical floor-level detachment) ⇒ H2 (WebGPU bind fallback) REFUTED** — backend-independent (`dam_sweep/monitor_probe_gl.bmp`).
2. `GE007_MONITOR_TRACE=1`: `monitor_cmd` records are **healthy** — `raw_tconfig=0x1d → idx=29` (IMGTEXT ✓), `rgba=0,128,0,255` ✓, `scale=1.000,1.000`, `mid` scrolling 0.494→0.488→0.481 per frame ⇒ **H1 (integrator/ST degeneration) REFUTED at the record level**.
3. `GE007_TRACE_TEXSELECT=1`, `tag=monitor` rows: **`table_fmt=4` (G_IM_FMT_I, 8b — what the monitor image table declares) but the pool texture reports `tex_fmt=2 tex_depth=0` (G_IM_FMT_CI, 4b)** for texnum 2224 (IMAGE_MONITOR_TEXT). **Root cause (one hop from proven): the retail *format-reinterpretation* trick — drawing a CI-stored texture through an I8 tile (raw-TMEM-bytes semantics) — is not honored by the PC decode-at-load texture pool**, which decodes per its catalogued format; the text intensity pattern never reaches the `G_CC_MODULATEI` combiner, leaving the flat MONRGBA green. Fix shape (M): make the static-game-texture import path honor the *tile's* fmt/siz over the table entry when they disagree (true TMEM reinterpretation semantics), or pre-decode dual-format variants for reinterpreted images; verify with the registered ROI diff. **Generalized class:** any retail DL that re-tiles a texture under a different format diverges the same way — worth a one-shot log on fmt-mismatch between tile and pool entry to census the whole game.

Setup-side data from the same trace **clears the converter for image selection**: the hut's 4-screen `MULTI_MONITOR` (obj 336) reads `ImageNums=48,48,48,48` = `monAnim30GreySolid` (a solid-grey screen — correct-looking setup data), and the singles read `image=5` (GreenTextUp). The **floor-level detachment of screens remains a separate open defect** (owner/part transform; native `monitorFindDrawableNode`/switch-node resolution at `chrobjhandler.c:963` diverges from retail's raw `Switches[n]` — next trace target), and per-object attribution of which quad belongs to obj 335 vs 336 needs one more instrumented look.

**Separate second defect:** two of the three monitor *screens* draw at floor level with no CRT bodies — screen sub-draw detached from owner/part transform; `PROPDEF_MULTI_MONITOR` conversion imports only `ImageNums` where the single-`MONITOR` path imports `OwnerOffset`/`OwnerPart` (`prop.c:2122-2140`). Needs its own trace of `monitorFindDrawableNode`/switch-node resolution (`chrobjhandler.c:963`, native diverges from retail's raw `Switches[n]` here). Both defects together are the FID-0036 "MULTI_MONITOR untested" risk case materializing — candidate new FID entries.

## 5. Simulation gaps on Dam — consolidated & ranked

### 5.1 The guard-AI divergence engine (P1): FID-0063 → 0054/0055
Confirmed live on HEAD (§3.2). The PRNG is bit-faithful (`random.c`); what diverges is **how many times per tick** `randomGetNext()` is called, phase-shifting every probabilistic perception/decision (`%distance==0` see-target checks fire ticks apart). Layer under it: chrTickBeams/patrol semantics (FID-0011 H4/H5/H6/H7 collapse, FID-0012 visibility→sim coupling behind `GE007_CHRBEAMS_FRUSTUM`, FID-0014 residual Layer B). **These are the items that close the Dam combat scorecard row.** Blockers all cleared (FID-0032/0062 verified) — this is now pure engineering, not instrumentation.

### 5.2 Collision primitives (boundary-flip classes, latent-per-shot)
- **FID-0099** `intersectLineTriangle` (`unk_092890.c:46-219`, 24.3% match): every bullet/LOS ray on Dam. Divergence classes: edge-grazing barycentric ulps, denominator-branch selection on axis-aligned tris, NaN path on sliver triangles (`:189` unchecked divide), facing-test boundary. Fix: forensic re-derivation or dual-run corpus ctest (M-L).
- **FID-0116** ray-vs-OBB float division (`model.c:12578-12652`): Dam doors/gates/alarm boxes; the port-added `1e-6` parallel-slab threshold is **box-size-dependent** (wider-than-ulp divergence for rays near-parallel to long door faces). Fix: transcribe retail's division-free cross-multiplication (M; sibling `sub_GAME_7F074CAC` is the template).
- **Defused this session:** FID-0102 (waypoint groupNum stub — ROM-byte proof: 205/205 Dam waypoints ship valid groupNum 0..22, all 21 setups clean; residual = S-size converter repair for romhack robustness; **do not enable the reference body — it is booby-trapped**, see agent report); FID-0119 (texture_s/t: zero compiled consumers); FID-0120 (unreached on Dam; data is deterministic zeros, not garbage); FID-0121 (no callers).
- **FID-0106** shot-scan lazy hit-list rebuild: port can land hits retail whiffs when the target chr was frustum-culled that frame (render→sim decoupling family with FID-0012/0058). Low frequency in Dam SP; direction is port-more-generous. Fix: faithful gate skipping the rebuild (S-M) + a fire-during-whip-turn route.
- **FID-0013** stan seam selection: **unblocked** (stale `blocked_on` in ledger); needs a stacked-floor-seam Dam route (tunnel roof / spillway catwalk) comparing `floor.stan_room`+`height` per tick (S-M verification task).

### 5.3 Intro anim divergence (NEW evidence, §3.3) — FID-0029 lane
Action-state 1→3 swap at swirl s4 t≈42s. Root-cause next step: trace the native intro action dispatcher at segment transitions with `GE007_TRACE_BOND_BUF` + diff against FID-0117's seeding change (M).

### 5.4 Remaining sim items reachable on Dam (tail)
Raw-cast tail FID-0130/0125 (triaged, MP-leaning); FID-0112 projectile room-source indirection; FID-0078 acosf association (projectile spin); FID-0111 patrol-init extra writes; FID-0057 locked-60 casualty checklist (menu/intro pacing); FID-0058 widescreen cull→auto-aim coupling (repro: same tape 4:3 vs wide, diff sim hashes).

---

## 6. Generalized wins (whole-codebase classes)

1. **Comparator tick-alignment (from §3.1)** — one alignment library for *all* trace comparators (movement, glass, intro, combat), pairing on `move.global`/tick, never record index. Kills a whole class of false reds/greens. **S effort, immediate trust payoff.**
2. **Silent-static-table sweep, round 2** — FID-0122's class sweep plus this session's verdicts: WebGPU tables proven content-bounded (document the proof), `BG_TRANSPARENT_ROOM_SORT_MAX`/`TEX_PALETTE_CACHE_SIZE` provably unreachable (add static_asserts), `obInit` extra entry (FID-0114) still open.
3. **Non-retail-default census** — §4.3's table is the Dam slice; the same census (grep default-on `GE007_*` renderer gates not derived from retail) should run repo-wide and every entry get: ledger row + faithful-mode polarity decision + A/B evidence. Three unledgered defaults found in one file in one session ⇒ more exist.
4. **Raw-offset-cast eradication** — 12 landed fixes + FID-0130/0125 open; protection = FID-0035 struct asserts (done) + FID-0036 converter risk cases (MULTI_MONITOR now has a live Dam symptom candidate — §3.4.1).
5. **Camera-registered pixel checkpoints (FID-0115)** — the single unlock for per-stage renderer parity verdicts on all 20 stages. The registered-probe recipe used this session (`GE007_AUTO_FACE_COORD_SCRIPT` at stock coords) is the pattern; routes should carry stock-matched face coords natively.
6. **Faithful-HUD pins for all visual-oracle routes** — 4 config keys; removes systematic non-retail pixels from every checkpoint (§3.4.3).
7. **Hot-primitive dual-run oracles** — the FID-0099/0116 corpus-ctest pattern (replay randomized inputs through old/new/reference implementations) generalizes to every low-match NATIVE_PORT reimpl of a sim primitive.
8. **Texture format-reinterpretation census (from §4.7)** — retail DLs may re-tile a texture under a format different from its catalogued one (raw-TMEM-bytes semantics); the PC decode-at-load pool silently substitutes the catalogued decode. One-shot log on tile-vs-pool fmt mismatch would census every affected material game-wide; the Dam monitor text screens are the first confirmed member.

---

## 7. Instrument gaps & hygiene (what to fix so green means green)

1. **S2 pixel ratchet was vacuously green**: with `build/oracle_cache` empty, `--gate` sweeps 0 checkpoints and exits 0 ("degraded" never fires at lane level). Yesterday's two gate runs swept nothing. Cache is reseeded for the glass route as of this session; the lane should **fail or report degraded when a gate:true checkpoint has no cached stock PPM**. (S)
2. **Movement comparator skew** — §3.1. (S)
3. **Route HUD pins** — §3.4.3. (S)
4. **FID-0043 RDP differential comparator unbuilt** — the S-Tier §2.3 checkboxes (`compare_rdp_stream.py`) are all open; this is the designated instrument for FID-0001/0104/0024 closure. (M)
5. **Ledger hygiene found this session**: FID-0013 `blocked_on` stale (both blockers verified); FID-0020 item 7 mitigated (texLoad choke point); FID-0022 evidence should name WebGPU; FID-0002 suspects should add `gfx_webgpu.c:2313`; FID-0120 severity note (zeros, not stale garbage); FID-0119 dead-output note; FID-0054's `--align move` repro line should be updated to `--align tick`.
6. **Concurrency**: the runtime lock works, but capture tooling gives up after 120s — under a concurrent autonomous session this session lost 2 capture slots. Lane scripts should take an env-tunable lock timeout. (S) Also reconfirmed: **never pipe a gate** (an early capture's FAIL exit was masked by `| tail` in this very session).

---

## 8. Prioritized close-out plan for Dam

> **Progress (2026-07-17, same day):** item 1 LANDED (`1cf7aa4` — `--align move-global`,
> real-pair 29/120→2/84 with the residual being genuine FID-0082 ±1-tick sprint-onset
> jitter; ctest `movement_comparator_align_unittest`); §7.1–7.3 LANDED (`f65a96b` pixel
> gate fail-closed + bash-3.2 fix + `--strict-coverage`; `2416ad6` faithful-HUD pins in
> all seven visual-probe routes, minimap out of parity frames).

| # | Item | Surface | Effort | Verifying instrument |
|---|---|---|---|---|
| 1 | Fix movement-comparator tick alignment (§3.1) | harness | S | dam_combat_guard6 movement lane goes green against live ares |
| 2 | File + fix DAM-R1 WebGPU sky seam | renderer | M | GL-vs-WebGPU screenshot A/B @ intro f190; pixel lane once registered |
| 3 | Root-cause intro bond_action 1→3 (§3.3) | sim | M | `dam_intro_swirl_bond_anim` unwaived count → 0 |
| 4 | Fix monitor-screen format reinterpretation + owner-transform detach (§4.7) | renderer | M | registered monitor-ROI diff shows text; fmt-mismatch census log clean |
| 5 | Camera-register the pixel checkpoints (FID-0115) | harness | M | criterion-2 per-stage verdict unlocked (all 20 stages) |
| 6 | chrTickBeams semantics + PRNG phase (FID-0011/0012/0063) | sim | L | tick-aligned combat census divergence counts → waiver-classified floor |
| 7 | Pertri snapshot on WebGPU + batch-split fix (FID-0002) | renderer | S+M | GL-pertri vs WebGPU diff on pad10092/10001 |
| 8 | Non-retail default census + faithful polarity decisions (§4.3) | renderer | M | per-flag A/B routes; pixel oracle |
| 9 | Glass validation wiring (FID-0004/0005: `glass_route_parity_regression.sh` lane) | harness | S-M | shatter/opacity/crack assertions on the 7 existing routes |
| 10 | Collision primitive oracles (FID-0099/0116) | sim | M-L | corpus ctest + combat-oracle impact cascade |
| 11 | RDP differential comparator (FID-0043) | harness | M | zero un-triaged clusters on gate routes; closes FID-0001 evidence loop |
| 12 | FID-0013 seam verification route | sim | S-M | stan_room/height per-tick parity on stacked-floor route |

Items 1–5 are a week-scale burst that converts the biggest unknowns into measured facts; 6 is the long pole for the combat scorecard; 7–12 close the tail with the instruments the earlier items build.

---

## Appendix A — session evidence index

- Combat pair + comparators: `dam_sweep/combat_guard6/` (`native_*.jsonl`, `stock_*.jsonl`, `combat_field_compare{,_tick}.txt`)
- Speed A/B: `dam_sweep/speed_ab_{jul5,jul16gl,head}/` + logs
- Glass pair: `dam_sweep/glass_probe2/` (BMP/PPM/PNGs, heatmap, `summary_*.json`)
- Intro pair: `dam_sweep/intro_swirl/`
- Registered monitor probe / DAM-R1 A/B: `dam_sweep/monitor_probe_registered.bmp`, `dam_sweep/damr1_{webgpu,gl}.bmp`
- Stock pixel cache seeded: `build/oracle_cache/dam_glass_visual_probe/<hash>/`
- Root-cause lane reports (renderer defaults / collision / glass): session agent outputs, key conclusions folded into §4-§6
