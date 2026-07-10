# S-Tier Faithfulness Program — Full Architecture & Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Phase 0–1 tasks are executed directly from this document; Phase 2+ work is executed *by the loop this document builds* (§6), with this document as its charter.

**Goal:** Drive the faithful path (sim + renderer + audio) to S-tier: zero unexplained divergence from the retail N64 game as measured by machine oracles, zero visible artifacts (shards, confetti, sky leaks, texture garbage), zero unverified reimplementations — and build the fully automatable, agent-driven, evidence-gated loop that discovers, tracks, and fixes fidelity gaps for extended unattended runs.

**Architecture:** A closed loop ("Fidelity Flywheel") over four planes: (1) **Oracles** — ground truth from the instrumented ares emulator, inline retail MIPS ASM, and the ROM itself; (2) **Sense lanes** — automated differential sweeps (trace, pixel, RDP-stream, soak/fuzz, code-audit) that emit machine-readable candidate findings; (3) **Ledger** — a git-tracked findings database whose state transitions require evidence artifacts; (4) **Act/Verify** — a fix lifecycle where every change lands behind an A/B flag, oracle-verified, with a new regression lane, and the full gate suite green (ratchet: gates only get stricter).

**Tech stack:** existing repo tooling (bash harnesses in `tools/`, Python comparators, ctest, `--trace-state`/`--sim-state-hash-out`/`--screenshot-*` engine instrumentation, the instrumented ares build) + new `tools/fidelity/` loop infrastructure (Python + bash, no new dependencies beyond python3 stdlib + Pillow already required by CI).

---

## Global Constraints (Program Charter)

These bind every task and every loop iteration. They extend `docs/BACKLOG_v0.4.0.md` charter rules 1–10.

1. **Authority hierarchy (sim behavior):** retail ASM / jump tables (`GLOBAL_ASM` bodies, `glabel jpt_*`) > ares oracle captures > decomp C. The `#else` C reference bodies (`PORT_FIXME_STUBS && !NATIVE_PORT`) **lie** — never transcribe one into a native path. Re-derive from ASM/oracle and cite the anchor (e.g. `jpt_80054084`, oracle route name). Documented failure: the red-shard bug came from a skipped `#else`-only `totalsize` advance (`src/game/bondview.c:3307-3318`).
2. **Authority hierarchy (renderer behavior):** the renderer has no retail ASM — its ground truth is (a) ares RDP/pixel captures and (b) N64 RDP documentation. Renderer findings must cite an ares-side artifact (screenshot, RDP command trace) or a documented RDP semantic, never "looks wrong".
3. **Evidence monopoly:** no ledger finding is created, promoted, waived, or closed without a machine-checkable artifact (comparator JSON, trace path, gate log) referenced in the ledger entry. Agent assertion alone is never evidence.
4. **Ratchet invariant:** every fix lands with (a) a `GE007_*` negative-control A/B flag (fix default-ON for port-defects; default-OFF opt-in for n64-quirk mitigations), (b) a regression lane added to a gate suite, (c) **all** pre-existing gates green. No gate is ever weakened or deleted to make a change pass; waivers go through the waiver protocol (§4.6).
5. **Byte-identity contract:** the deterministic faithful path stays byte-identical under every default-off flag; `tools/sim_invariance_gate.sh` and the sim-hash lanes are hard gates. `Video.FovY` is sim state — never perturb it in gates.
6. **Determinism envelope for all automated runs:** `env -u GE007_DEBUG SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1 GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1` + `--deterministic` (fixed RNG seed 0x12345678, frozen input, synthetic clock). Never run game tests with audio audible. Leave `build/` on clean `main` after test sessions.
7. **Small, evidenced commits:** one finding (or one task) per commit; commit message cites the ledger ID and evidence path. ROM-derived artifacts never committed (`scripts/ci/check_no_rom_data.sh` is a pre-commit hook); goldens live in `baselines/`, per-run captures in `/tmp/mgb64_*_$$`.
8. **Classification taxonomy (from the 2026-07-06 audit):** every behavioral finding is exactly one of `port-defect` (port diverges from retail — fix, default-on), `parity-divergence` (intentional port behavior differing from retail — document + A/B flag + faithful-mode default decision), `n64-quirk` (retail bug faithfully reproduced — preserve; player-helping mitigation only behind opt-in flag), `instrumentation-gap`, or `coverage-gap`.
9. **No silent caps:** any lane that bounds coverage (top-N routes, sampled frames, skipped stages) must print what it skipped into its report artifact.
10. **Loop safety:** the loop never force-pushes, never touches `origin`/`public` remotes, never edits `.github/workflows/`, never deletes baselines, and stops (writes `docs/fidelity/ESCALATIONS.md` and halts the lane) rather than guessing when evidence is contradictory.

---

## 0. Current State (research baseline, 2026-07-09)

### 0.1 Ground-truth assets already in-tree

| Asset | Where | State |
|---|---|---|
| Instrumented ares oracle | `build/ares-movement-oracle/.../ares.app/Contents/MacOS/ares`; builder `tools/prepare_ares_movement_oracle_build.sh` | Working. Emits per-frame JSONL (pos, camera, view basis, move block, RNG seed, rooms, actors, glass, combat crosshair/health, watch, intro anim hashes) + PPM screenshots. Env-driven input scripts, frame limits, stage targeting, EEPROM seeding. |
| Ares audio oracle | `tools/prepare_ares_audio_dump_build.sh` → `build/ares-audio-dump/` | Working, separate build, pinned ref. |
| Native trace | `--trace-state` → `src/platform/port_trace.c:7693` (same JSONL schema as ares) | Working. Field set at `port_trace.c:6165-6201`. |
| Sim-state hash | `src/platform/sim_state_hash.c` (FNV-1a 64, ASLR-canonicalized), registry `sim_state_hash_registry.c:25-61` | Working. Regions: 8 MB `s_pcPool`, `g_ClockTimer`, `g_GlobalTimer`, `prop_pool` (prop BSS — closed by M8.1 partial). **Gap: stan/collision scratch + unaudited mutable BSS.** |
| Inline retail ASM | 388 `GLOBAL_ASM` bodies + 57 `glabel jpt_*` jump tables across 46 files (top: bg.c 59, chrobjhandler.c 39, stan.c 30, gun.c 29, bondview.c 28, model.c 27) | Complete instruction-level ground truth with ROM offset + VRAM addr per instruction. Compiled out on PC (`include/ultra64.h:30`), pure reference. |
| Route corpus | `tools/rom_oracle_routes/*.json` (54 routes), resolver `tools/rom_oracle_route.py` | Working but **Dam-heavy; ~12 stages have placeholder intro routes with empty waivers (M8.2)**. |
| Comparators | `tools/compare_movement_trace.py`, `compare_intro_trace.py`, `compare_combat_health_trace.py`, `compare_glass_trace.py`, `compare_bullet_impact_sequence.py`, `compare_screenshots.py`, `compare_state.py`, `audit_oracle_trace.py`, `audit_screenshot_health.py`, `audit_render_trace.py` | Working. |
| Regression harnesses | `tools/regression_test.sh` (20 levels, frame-180 screenshot/trace/audio lanes vs `baselines/`), `tools/soak_stability.sh`, `tools/perf_census.sh`, `tools/renderer_parity_capture.sh`, ~20 `PORT_VALIDATION_TESTS` ctest smokes, `tools/sim_invariance_gate.sh` | Working. CI is manual-dispatch only; no scheduled runs. |
| RAMROM replay | `src/game/ramromreplay.c` (2007 lines) via `g_ContPlaybackFunc`/`g_ContRecordFunc` hooks (`src/joy.c:111-112`), per-block RNG cross-check (`ramromreplay.c:1528`) | Plays 13 ROM-baked demos (`--ramrom <name>`). **No PC disk-persisted record/replay of arbitrary sessions.** |
| Automation env surface | ~902 `GE007_*` flags (`docs/ENV_FLAGS.md`, staleness-gated by ctest `env_reference_current`); `GE007_AUTO_*` scripted events (warp, guard spawn, chr AI, damage, tags, RNG seed) in `src/platform/stubs.c` / `src/random.c` | Working. |
| 0-tick purity fuzz gate | Spec complete at `docs/design/UNCAPPED_FPS_PLAN.md:897-1004` (`tools/uncap_purity_gate.sh`, `GE007_UNCAP_FUZZ`) | **Not built.** |

### 0.2 The verify surface (existing gates, to be unified in §4.5)

ROM-free ctest (always): `sim_state_hash`, `room_normals`, `port_env`, `arg_triage`, `rom_validate`, `update_check`, `env_reference_current`, `intro_tools_unittests`, ~13 analyzer guards, release/provenance guards, R1 `scripts/ci/check_sim_render_separation.sh`, R2 `scripts/ci/check_timing_lock.sh`.
ROM-gated (`-DPORT_VALIDATION_TESTS=ON` + `baserom.u.z64`): ~20 runtime smokes incl. `port_rom_oracle_route_contract`, `port_renderer_parity_smoke`, `port_campaign_route_smoke`, `port_ammo_crate_collect_smoke`, `port_perf_budget_smoke`, XLU/cvg memory regressions, `intro_oracle_dam_route` (ctest label `oracle`).
Standalone: `tools/regression_test.sh`, `tools/sim_invariance_gate.sh`, `tools/soak_stability.sh`, `tools/dam_visual_regression_suite.sh`, `tools/native_playability_regression_suite.sh`, `tools/train_window_backdrop_regression.sh`, `tools/audio_ab_gate.sh`.

### 0.3 Seeded findings ledger (everything known-open, 2026-07-09)

These become the initial `docs/fidelity/ledger/` entries (Task 0.2). Classes per charter rule 8. Statuses verified against git log and the 07-07 audit during research.

| ID | Title | Class | Pri | Status | Anchor |
|----|-------|-------|-----|--------|--------|
| FID-0001 | Center-glass RDP framebuffer-memory blend not stock-accurate (MG.1, Dam pad10092) | port-defect | P1 | root-caused | `gfx_pc.c` `ROOM_GLASS_CC=0x00738e4f020a2d12`; route `dam_regular_glass_shatter_pad10092_impact_visual_probe` |
| FID-0002 | Overlapping coplanar glass panes read stale FB snapshot; Metal ignores `GE007_XLU_SNAPSHOT_MODE=pertri` (MG.2) | port-defect | P2 | triaged | `gfx_opengl.c:2140-2279`, `gfx_metal.mm:3099` |
| FID-0003 | Glass shard geometry scale unverified — six competing A/B flags, none signed off (MG.3) | parity-divergence | P1 | triaged | `src/game/unk_0A1DA0.c:1266-1336`; scorer `tools/compare_glass_shard_pixel_oracle.py` |
| FID-0004 | Shatter-trigger hit-count / tinted-glass 16/255 min-opacity / crack-fade parity unvalidated (MG.4) | parity-divergence | P2 | discovered | `chrobjhandler.c:2560-2600`, `:572-628` |
| FID-0005 | Glass tooling debt: ~49 glass_* scripts, ~11 wired (MG.5) | instrumentation-gap | P2 | triaged | charter rule 8 |
| FID-0006 | Intro weapon sub-buffer overlap is a print, not a guard (R9/M1.1) | port-defect | P1 | root-caused | `bondview.c:3353` (print), `:3362` (unguarded size) |
| FID-0007 | Dyn-allocator residuals: `modelSetDistanceDisabled` early-return leak (T9b); glass float-mtx path lacks fail-closed contract | port-defect | P1 | fix-in-progress | `src/game/dyn.c:129/151/176/195`; `GE007_DYN_STRESS_LIMIT` |
| FID-0008 | Train room-51 windows show sky through grazing apertures; edge-rescue delta 0% on its own control (M3.4) | port-defect | P1 | root-caused | `bg.c:2094`, `:2160+`, `:2200`; `tools/train_window_backdrop_regression.sh` |
| FID-0009 | Silo aperture residual: rooms 28/44 + screen-edge sliver dropped via multi-hop grazing portals | port-defect | P2 | root-caused | reuse T13b draw-only walk (`g_BgRoomDrawOnly`) |
| FID-0010 | `bg.c:7441` frustum-culling stub marks all rooms visible | port-defect | P2 | root-caused | `src/game/bg.c:7441` |
| FID-0011 | chrTickBeams minimal reimpl: H4 actiontype dispatch collapsed (HIGH), H5 BACKGROUND_AI bit, H6 held-weapon obj leak, H7 visibility coarsening | port-defect | P1 | triaged, blocked_on FID-0032 | `chr.c:5122/:5204`; retail `chr.c:5566/6418/7344`; `docs/design/COMBAT_GLIDEPATH.md` §3.1 |
| FID-0012 | Guard visibility→sim coupling Phase C default-flip blocked on combat-field oracle (M2.3) | parity-divergence | P1 | triaged, blocked_on FID-0032 | `chr.c:5220-5238`, `lvl.c:1737-1743`; `GE007_CHRBEAMS_FRUSTUM` |
| FID-0013 | stan.c stacked-floor seam selection not frame-exact-verified vs ares (F2 landed, unverified) | parity-divergence | P1 | triaged, blocked_on FID-0032 | `stan.c:731/737` (`GE007_STAN_ONEDGE`); routes on Facility/Bunker stairwells |
| FID-0014 | Patrol force-loop workaround masks stalled WAYMODE_MAGIC (M2.5) | parity-divergence | P2 | root-caused | `chrlv.c:11216-11218`, `:11257` |
| FID-0015 | Movement-stick square deadzone (aim stick already radial) (M2.1) | port-defect | P1 | root-caused | `stubs.c:6322-6337`, `:5887-5900` |
| FID-0016 | Mouse-wheel weapon cycling collapses N notches to 1 (M2.2) | port-defect | P2 | root-caused | `stubs.c:6267-6269`; `bondview.c:11526/13085` |
| FID-0017 | `speedgraphframes` unclamped → HUD/watch timers jump on frame spikes (M2.4) | port-defect | P2 | root-caused | `unk_0C0A70.c:151`; ref clamp `lvl.c:2113-2126` |
| FID-0018 | Metal MSAA silently ignored, no A2C (M3.1) | port-defect | P1 | root-caused | `gfx_metal.mm:1209/2131/2430/2444/2760/3206` |
| FID-0019 | Metal shadow depth-clip clamp / depth read-write hazard / MSL autorelease leaks (M3.2) | port-defect | P2 | root-caused | `gfx_metal.mm:1631-1649`, `:3308-3311` |
| FID-0020 | Interpreter sharp edges: footprint-reject eviction, L7 moveMem OOB, mtx-stack depth-11 clobber, prim-color cache-key miss, texgen s16 overflow, `G_SETBLENDCOLOR` no-op, texSelect 3001-vs-2698 (M3.3) | port-defect | P2 | triaged | `gfx_pc.c:15276/20065/16005/15161/16901`; `othermodemicrocode.c:94/104/119/596` |
| FID-0021 | Metal degraded-target drops whole frame; mid-frame readback stalls (R16-R18/M3.6) | port-defect | P2 | discovered | `gfx_metal.mm:1005/3148+/2823` |
| FID-0022 | `G_ZS_PRIM` depth-source claimed but unimplemented in both backends (R19) | port-defect | P3 | triaged | `gfx_metal.mm:2854`; `gfx_opengl.c:1757/1763` |
| FID-0023 | gfx_ptr low-32 registry: 4-slot probe, silent slot-0 eviction (R12) | port-defect | P2 | triaged | `src/platform/gfx_ptr.h:29/43/53/66` |
| FID-0024 | `Z_UPD`-without-`Z_CMP` handled by one global rule; per-material correctness unverified (R15) | parity-divergence | P2 | discovered | `gfx_pc.c:17562-17583` |
| FID-0025 | Audio: env/pole sample-ordering XOR unvalidated vs ares (H2/M5.1) | parity-divergence | P2 | triaged, blocked_on FID-0031 | `mixer.c:558`, `:39-40` |
| FID-0026 | Audio: per-instrument bank loop/tuning deltas never measured (H3/M5.2, Surface 2) | parity-divergence | P2 | discovered | `audio_compat.c:99-341` |
| FID-0027 | Audio: DMA-window exhaustion / voice starvation hypotheses unmeasured (H4/H5) | instrumentation-gap | P2 | discovered | `audi_port.c:198-202` |
| FID-0028 | Audio hygiene: dead simple-synth + dead spatial branch (M5.4) | instrumentation-gap | P3 | triaged | `audio_pc.c:2361/:751/:1271` |
| FID-0029 | Intro D-ledger waiver retests pending post-D43: D32, D35 pose drift, D37/D39 static-shot camera seed, D38 duration, D41 menu-boot anim phase (M1.5) | parity-divergence | P2 | fix-in-progress | `docs/design/INTRO_OUTRO_LEDGER.md` |
| FID-0030 | Sim-hash blind spot: stan/collision scratch + unaudited mutable game BSS (M8.1 residual) | instrumentation-gap | P0 | root-caused | `sim_state_hash_registry.c:25-61` |
| FID-0031 | Oracle route coverage Dam-only; ~12 placeholder intro routes, no organic completion routes (M8.2) | coverage-gap | P0 | triaged | `tools/rom_oracle_routes/` |
| FID-0032 | Combat/floor-field oracle absent: ares reader extracts no guard/combat/stan state (COMBAT_DEFERRED §2.1, "single biggest blocker, XL") | instrumentation-gap | P0 | triaged | `tools/prepare_ares_movement_oracle_build.sh` |
| FID-0033 | 0-tick purity fuzz gate spec'd, not built | instrumentation-gap | P0 | root-caused | `docs/design/UNCAPPED_FPS_PLAN.md:897-1004` |
| FID-0034 | No PC disk input record/replay (RAMROM records only to RAM for Indy path) | instrumentation-gap | P0 | root-caused | `ramromreplay.c:1207/1226`; hooks `joy.c:111-112` |
| FID-0035 | Zero struct-layout static asserts for N64-layout ROM structs (StandTile, PROPDEF records); protection is compiler flags only | instrumentation-gap | P1 | root-caused | `src/bondtypes.h:387-449`; `CMakeLists.txt:708-723` |
| FID-0036 | PROPDEF converter risk cases untested: TANK `rect4f` union, KEY id/flags union, MULTI_MONITOR, VEHICLE/AIRCRAFT/AUTOGUN ptr-reuse; hand-maintained size tables unguarded | port-defect | P1 | triaged | `prop.c:1910-2380`, `:2321-2324`, `:2048-2049`; size tables `loadobjectmodel.c:56-104` |
| FID-0037 | `prop.c:2538` setup padnames never byte-swapped (nulled) | port-defect | P3 | discovered | `src/game/prop.c:2538` |
| FID-0038 | `ramromreplay.c:1766` PAL timing question unresolved | parity-divergence | P3 | discovered | `src/game/ramromreplay.c:1766` |
| FID-0039 | `model_convert.c:881` OP11 rendering unimplemented (field width note) | port-defect | P3 | discovered | `src/platform/model_convert.c:881` |
| FID-0040 | `g_ClockTimer` clamp asymmetry: free play clamps 1/4, RAMROM replay unclamped | parity-divergence | P3 | documented | `lvl.c:2115-2137` |
| FID-0041 | Room XLU depth-sort is intentional non-faithful default (audit #8) | parity-divergence | P2 | documented | `GE007_DISABLE_ROOM_XLU_SORT` |
| FID-0042 | ASM audit coverage: 388 GLOBAL_ASM reference bodies, no systematic reviewed/verdict tracking | coverage-gap | P1 | discovered | §5 Task 2.4 |
| FID-0043 | Native/ares RDP command-stream differential lane absent (analyzer guards are native-only) | instrumentation-gap | P1 | discovered | `MGB64_ARES_TRACE_RDP_COMMANDS`; `GE007_TRACE_RDP_RENDER_MODES` |
| FID-0044 | Pixel-oracle sweep absent: no systematic per-stage native-vs-ares screenshot lane (only per-fix ROI probes) | instrumentation-gap | P1 | discovered | `compare_screenshots.py`, ares PPM |
| FID-0045 | n64-quirk registry: known preserved retail bugs (e.g. `chraidata.c:715` guard shuffle) not centrally documented | coverage-gap | P3 | discovered | §4.6 |

Recently FIXED (regression-guard only, verify lanes exist): red shards `50f3f61`; Bond body alias fail-closed; D31/D43 intro grounding+anim; D44 post-intro spawn `845fa94`; T13b camera-seed walk (Silo/Dam blue-sky); ammo crate collect (PR #23); explosion confetti (`subload_clamp`); KF7 muzzle-flash fog; CC pool wrap; R4/R5/R7/R10 audit items; weapon jump-table groupings (`03becb6`..`13a4d70`); detonator; magic-travel gate.

---

## 1. Definition of S-Tier (program exit criteria)

The program is complete when **all** of the following are simultaneously true and enforced by always-on gates:

1. **Sim parity:** every one of the 20 solo stages has (a) an intro route and (b) at least one organic mission-completion route whose native trace matches the ares stock trace on **every compared field** within the published tolerance table (integer fields exact: `move.clock`, `max_t`, room ids, RNG seed at checkpoints; float fields within documented epsilon), verified by `tools/compare_movement_trace.py` + the combat comparator (§5 Task 1.1) with zero unexplained divergent frames.
2. **Renderer parity:** the per-stage pixel-oracle sweep (§5 Task 2.2) reports zero pixel-diff clusters that are not attributed to a documented, bounded approximation class (VI filtering, 3-point-filter derivative approximation, RDP dither) in the approximation registry (§4.6). No sky leaks, no garbage textures, no degenerate geometry in any sweep capture.
3. **RDP semantic parity:** the RDP-stream differential (§5 Task 2.3) shows no unexplained mismatch in combine mode, othermode, tile state, or draw counts per material class at matched frames on all sweep routes.
4. **Reimplementation audit:** 100% of the 388 `GLOBAL_ASM`-referenced reimplementations carry a recorded verdict (`verified-equivalent` / `fixed` / `waived-with-reason`) in the ASM audit queue (§5 Task 2.4); zero open `port-defect` findings.
5. **State coverage:** the hash-coverage audit (§3 Task 0.4) shows every mutable `src/game` data/BSS symbol either registered in the sim-hash or waived with a reason; purity-fuzz (FID-0033) green on all 20 stages; sim-invariance gate green.
6. **Flag hygiene:** every `GE007_*` fidelity fix-gate has a lane proving both sides (fix-ON matches oracle; fix-OFF reproduces the legacy defect) — no unverifiable flags.
7. **Endurance:** a 24-hour loop run (§6) completes with zero crashes, zero watchdog stalls, zero ASan reports across soak lanes, and an empty new-findings queue on two consecutive full sense sweeps (loop-until-dry, dry-count 2).
8. **Ledger closure:** zero findings in `discovered`/`triaged`/`root-caused`/`fix-in-progress`; only `verified`, `documented` (parity-divergence with faithful default), and `waived` (with retest condition) remain.

Criterion 8 is the machine-checkable definition the loop drives toward: `tools/fidelity/ledger.py stats --assert-closed` (Task 0.2).

---

## 2. Architecture: the Fidelity Flywheel

```
                        ┌──────────────────────────────────────────────┐
                        │                 ORACLE PLANE                 │
                        │  ares (movement+combat+pixel+RDP+audio)     │
                        │  inline retail ASM + jump tables             │
                        │  ROM data (baserom.u.z64, imagelist.u.csv)  │
                        └───────────────┬──────────────────────────────┘
                                        │ captures / references
        ┌───────────────────────────────▼───────────────────────────────┐
        │                          SENSE LANES                          │
        │ S1 trace sweep   S2 pixel sweep   S3 RDP-stream diff          │
        │ S4 soak/fuzz/ASan/purity   S5 code audit (ASM & renderer)     │
        │ S6 coverage critic                                            │
        └───────────────┬───────────────────────────────────────────────┘
                        │ candidate findings (JSON, evidence-attached)
        ┌───────────────▼───────────────┐      ┌────────────────────────┐
        │            LEDGER             │◄─────┤  TRIAGE (dedupe,       │
        │ docs/fidelity/ledger/*.json   │      │  classify, prioritize) │
        │ evidence-gated transitions    │      └────────────────────────┘
        └───────────────┬───────────────┘
                        │ top-priority actionable finding
        ┌───────────────▼───────────────────────────────────────────────┐
        │                        ACT PIPELINE                           │
        │ root-cause (ASM/oracle anchor) → fix behind A/B flag →        │
        │ oracle-verify → add regression lane → full VERIFY suite       │
        └───────────────┬───────────────────────────────────────────────┘
                        │ green → commit + ledger transition
        ┌───────────────▼───────────────┐
        │       VERIFY SUITE (ratchet)  │  tools/fidelity/verify_all.sh
        │ grows monotonically           │
        └───────────────────────────────┘
```

**Loop scheduling policy (one iteration = one unit of work):**

```
if verify_all is red on HEAD:            phase = REPAIR   (fix the regression first; nothing else)
elif ledger has fix-in-progress:         phase = ACT      (finish it; one finding at a time)
elif ledger has actionable P0/P1:        phase = ACT      (highest priority, oldest first)
elif any sense lane stale (> N iters):   phase = SENSE    (run stalest lane, harvest)
elif coverage critic has open items:     phase = EXPAND   (add routes/fields/lanes)
else:                                    phase = DRY-CHECK (full sense sweep; 2 consecutive
                                                            empty sweeps ⇒ program exit criteria audit)
```

Blocked findings (`blocked(oracle)`) become actionable automatically when the blocking instrumentation task's ledger entry reaches `verified` — the ledger stores the dependency edge (`blocked_on: FID-00NN`).

---

## 3. Phase 0 — Substrate (determinism + ledger + gates)

**Status (2026-07-10 audit): substrate complete.** Tasks 0.1–0.7 all landed on
main (CHARTER.md, ledger.py + 64-finding ledger, verify_all.sh + manifest,
hash-coverage audit, 0-tick purity-fuzz gate, input-tape record/replay,
struct-layout asserts) — 29/30 steps below carry a citable artifact. The one
exception (0.3 Step 2's one-time baseline run) has no persisted artifact by
design (`docs/fidelity/reports/*.json` is gitignored, per-run) and is left
unchecked rather than assumed.

Everything in Phase 0 is ROM-optional to build (unit-testable), ROM-gated to exercise. Order matters: 0.1 → 0.2 first (the loop's memory), then 0.3–0.7 in any order.

### Task 0.1: Program charter + directory scaffold

**Files:**
- Create: `docs/fidelity/CHARTER.md` (copy of Global Constraints above + the scheduling policy in §2 + the agent prompt in Appendix B)
- Create: `docs/fidelity/ledger/` (dir), `docs/fidelity/reports/.gitignore` (ignore `*`; reports are per-run, regenerated)
- Create: `tools/fidelity/` (dir)

**Steps:**
- [x] **Step 1:** Write `docs/fidelity/CHARTER.md` containing verbatim: Global Constraints §"Global Constraints", the scheduling policy block from §2, the fix lifecycle from §4, and Appendix B's iteration prompt. (done: docs/fidelity/CHARTER.md exists / commit 0c688bf)
- [x] **Step 2:** `git add docs/fidelity tools/fidelity && git commit -m "docs(fidelity): program charter + scaffold (S-tier program Phase 0)"` (done: commit 0c688bf)

### Task 0.2: Ledger tool (`tools/fidelity/ledger.py`)

The loop's persistent memory. One JSON file per finding in `docs/fidelity/ledger/FID-NNNN.json`; a generated human index `docs/fidelity/LEDGER.md`; a ctest guard that the ledger is schema-valid.

**Files:**
- Create: `tools/fidelity/ledger.py`
- Create: `tools/fidelity/ledger_schema.json` (Appendix A verbatim)
- Create: `tests/` — register ctest `fidelity_ledger_valid` in `CMakeLists.txt` next to the other python guards (`python3 tools/fidelity/ledger.py validate`)

**Interfaces:**
- Produces (CLI, used by every sense lane and the loop driver):
  - `ledger.py new --title T --class C --surface S --priority P --evidence PATH --repro CMD [--suspect file:line]...` → prints new FID id
  - `ledger.py transition FID-NNNN --to STATUS --evidence PATH [--note TEXT]` (fails without `--evidence` for any promotion; `refuted`/`waived` require `--note`)
  - `ledger.py list [--status S] [--class C] [--priority P] [--actionable]` (actionable = not waived/verified/documented/refuted, all `blocked_on` deps verified, no open escalation naming it in `docs/fidelity/ESCALATIONS.md`)
  - `ledger.py dedupe-check --title T --suspect file:line` → prints candidate duplicate FIDs (fuzzy title match + same suspect file)
  - `ledger.py render` → regenerates `docs/fidelity/LEDGER.md`
  - `ledger.py validate` → schema + transition-history integrity; exit 1 on violation
  - `ledger.py stats [--assert-closed]` → counts per status; `--assert-closed` exits 1 unless §1 criterion 8 holds

**Steps:**
- [x] **Step 1:** Write `tools/fidelity/ledger_schema.json` (Appendix A). (done: tools/fidelity/ledger_schema.json exists / commit 512e4be)
- [x] **Step 2:** Write `ledger.py` (~250 lines, python3 stdlib only). State machine: `discovered → triaged → root-caused → fix-in-progress → landed → verified`, plus terminal `refuted`, `waived` (requires `waiver.retest`), `documented` (parity-divergence only), and `blocked(...)` as an annotation (`blocked_on: [FID...]`), not a status. Every transition appends `{ts, from, to, evidence, note}` to `history`. Timestamps ISO-8601 UTC. (done: tools/fidelity/ledger.py implements the state machine / commit 512e4be)
- [x] **Step 3:** Write a unittest `tools/tests/test_fidelity_ledger.py` (create/list/transition-without-evidence-fails/dedupe/render roundtrip in a tmpdir via `--ledger-dir`), add to the existing `intro_tools_unittests` discovery dir convention. (done: tools/tests/test_fidelity_ledger.py exists / commit 512e4be)
- [x] **Step 4:** `python3 -m unittest tools.tests.test_fidelity_ledger -v` → PASS. (done: ran `python3 -m unittest tools.tests.test_fidelity_ledger -v` -- 9/9 PASS, verified 2026-07-10)
- [x] **Step 5:** Seed the ledger: create FID-0001..FID-0045 from §0.3 verbatim (title/class/priority/status/anchors as `suspect`, repro commands from §0.3 sources where listed; evidence = pointer to the source doc section, e.g. `docs/BACKLOG_v0.4.0.md#MG.1`). Set `blocked_on` edges: FID-0011/0012/0013/0025 → FID-0032 or FID-0031 as listed. (done: docs/fidelity/ledger/FID-0001..0045.json seeded / commit 512e4be)
- [x] **Step 6:** Register ctest `fidelity_ledger_valid` in `CMakeLists.txt` (pattern-match the `env_reference_current` registration). (done: ctest fidelity_ledger_valid registered, CMakeLists.txt:577)
- [x] **Step 7:** `ctest --test-dir build -R fidelity_ledger_valid --output-on-failure` → PASS. Commit: `feat(fidelity): evidence-gated findings ledger + seed backlog (45 findings)`. (done: ran `python3 tools/fidelity/ledger.py validate` -> "ledger OK: 64 findings valid", verified 2026-07-10 / commit 512e4be)

### Task 0.3: Unified verify suite (`tools/fidelity/verify_all.sh`)

One command, machine-readable verdict, monotonically growing. This is the ratchet.

**Files:**
- Create: `tools/fidelity/verify_all.sh`
- Create: `docs/fidelity/verify_manifest.txt` (ordered list of gate invocations; the script executes this manifest so adding a gate = appending a line — additions only, deletions require a waiver entry)

**Interfaces:**
- Produces: `docs/fidelity/reports/verify_<git-sha>.json` — `{sha, started, gates: [{name, cmd, status: pass|fail|skip, seconds, log}], verdict: green|red, skipped_reason?}`; exit 0 iff green (skips allowed only for missing-ROM/missing-ares prerequisites, and each skip is listed — charter rule 9).

**Initial manifest (tiers ordered cheap→expensive; later tasks append):**
```
# tier 1 — static + unit (ROM-free, ~2 min)
ctest --test-dir build --output-on-failure -E "port_"        # all ROM-free lanes incl. fidelity_ledger_valid
scripts/ci/check_sim_render_separation.sh
scripts/ci/check_timing_lock.sh
# tier 2 — runtime smokes (ROM-gated, serialized, ~30 min)
ctest --test-dir build --output-on-failure -R "port_"        # validation smokes incl. oracle route contract
tools/sim_invariance_gate.sh
# tier 3 — regression + endurance (ROM-gated, ~45 min)
tools/regression_test.sh
tools/renderer_parity_capture.sh --scene all
# appended by later tasks:
# tools/uncap_purity_gate.sh                                  (Task 0.5)
# tools/fidelity/tape_regression.sh                           (Task 0.6)
# tools/fidelity/sense_trace_sweep.sh --gate                  (Task 2.1, gate mode = known-good routes only)
```

**Steps:**
- [x] **Step 1:** Write the script: reads the manifest, runs each line under the charter-rule-6 env (reuse `tools/validation_common.sh` helpers `validation_automation_env` / `validation_run_with_timeout`), captures logs to the report dir, emits the JSON verdict. `--tier N` runs tiers ≤ N (loop uses `--tier 1` for inner-iteration checks, full suite before commit of sim-touching changes). (done: tools/fidelity/verify_all.sh exists / commit 69b5ab5)
- [ ] **Step 2:** Run `tools/fidelity/verify_all.sh --tier 1` → green. Run full suite once with ROM present → record baseline report; fix or ledger any red found (do not proceed with a red baseline — that is the loop's REPAIR phase done by hand once).
- [x] **Step 3:** Commit: `feat(fidelity): unified verify suite with machine-readable verdict`. (done: commit 69b5ab5)

### Task 0.4: Sim-hash coverage audit + close M8.1 (FID-0030)

Make "what the hash covers" a checked property instead of tribal knowledge.

**Files:**
- Create: `tools/fidelity/hash_coverage_audit.py`
- Create: `docs/fidelity/hash_waivers.txt` (symbol-pattern waivers with reasons)
- Modify: `src/platform/sim_state_hash_registry.c` (register stan/collision scratch and any audit-discovered mutable sim BSS)
- Test: extend `tests/test_sim_state_hash.c` region-count expectation; ctest `fidelity_hash_coverage`

**Interfaces:**
- Consumes: `nm --print-size --size-sort build/<game objects>` over `src/game/*.o` + `src/*.o` (top-level decomp TUs).
- Produces: report JSON listing every writable symbol (`.data`/`.bss`, `d/b/D/B`) with disposition `hashed` (address+size falls inside a registered region — verify by emitting the region table from a tiny `--print-sim-hash-regions` debug flag added to the binary) / `waived` (matches `hash_waivers.txt`, e.g. render-only counters, trace state, debug buffers) / `UNCOVERED`. Exit 1 on any `UNCOVERED`.

**Steps:**
- [x] **Step 1:** Add `--print-sim-hash-regions` to `main_pc.c` arg parse (prints `name base size` per region from `simHashRegistryBuild()` and exits; no ROM needed). (done: --print-sim-hash-regions in src/platform/main_pc.c / commit 830998e)
- [x] **Step 2:** Write `hash_coverage_audit.py`; first run will list the true blind-spot inventory (expected: stan/collision scratch in `stan.c`, plus assorted `src/game` globals). (done: tools/fidelity/hash_coverage_audit.py exists / commit 830998e)
- [x] **Step 3:** For each UNCOVERED symbol: if sim-relevant → register a region in `sim_state_hash_registry.c` (follow the `prop_pool` pattern at `:55-57`); if render/trace/debug-only → add to `hash_waivers.txt` with a one-line reason. Sim-relevance rule of thumb: written by `src/game` code during `lvlViewMoveTick`/AI/prop paths ⇒ sim; written only during DL build/trace ⇒ waiver candidate (but check for read-backs into sim — the room_rendered precedent, FID-0012). (done: docs/fidelity/hash_waivers.txt + sim_state_hash_registry.c regions / commit 830998e)
- [x] **Step 4:** Re-baseline: run `tools/sim_invariance_gate.sh` and `tools/regression_test.sh` (hash values change when regions are added — expected once; screenshot/trace lanes must stay identical). (done: commit c4b8f5b re-baselines input-tape hash post-region-expansion)
- [x] **Step 5:** Register ctest `fidelity_hash_coverage` (ROM-free: nm over built objects + region table from the binary). Append to verify manifest tier 1. (done: ctest fidelity_hash_coverage registered, CMakeLists.txt:616)
- [x] **Step 6:** Update the stale text at `docs/BACKLOG_v0.4.0.md:1317-1328` (M8.1) to reflect reality. Ledger: `FID-0030 → verified`, evidence = audit report. Commit. (done: docs/BACKLOG_v0.4.0.md:1370 "M8.1 ... (FID-0030 verified)"; ledger FID-0030 status=verified / commit 830998e)

### Task 0.5: Build the 0-tick purity fuzz gate (FID-0033)

The complete implementation spec — env flag `GE007_UNCAP_FUZZ`, xorshift schedule, `--screenshot-game-timer` alignment, `canon()` hash comparison minus run-shape fields, level/seed matrix — already exists at `docs/design/UNCAPPED_FPS_PLAN.md:897-1004` (Task 4 there). Execute it standalone (it does not require the rest of the F5 plan; the fuzz path must be reachable only under the harness, per that spec).

**Steps:**
- [x] **Step 1:** Implement per UNCAPPED_FPS_PLAN Task 4: the render-only-frame injection in `src/game/unk_0C0A70.c` gated by `GE007_UNCAP_FUZZ=<seed>` under `--deterministic`, and `tools/uncap_purity_gate.sh`. (done: GE007_UNCAP_FUZZ in src/game/unk_0C0A70.c + tools/uncap_purity_gate.sh / commit 04d9cec)
- [x] **Step 2:** Negative control: hand-inject a sim mutation into a render-only frame locally (do not commit) and confirm the gate goes red. (done: ledger FID-0033 history note records "negative-control RED proven")
- [x] **Step 3:** Extend the level matrix from the spec's 3 to all 20 (`LEVELS` list from `tools/regression_test.sh`), keeping the spec's 2 seeds; add `--quick` (3-level) mode for the verify suite tier 2 and full matrix for tier 3. (done: tools/uncap_purity_gate.sh FULL_LEVELS = 20 levels, QUICK_LEVELS = 3-level --quick mode)
- [x] **Step 4:** Append to verify manifest. Ledger `FID-0033 → verified`. Commit. (done: verify_manifest.txt tiers 2/3 list uncap_purity_gate.sh; ledger FID-0033 status=verified)

### Task 0.6: PC input tape — disk record/replay (FID-0034)

Byte-exact replay of arbitrary play sessions as regression fixtures — the missing substrate piece. Builds on the retail hook points, not a parallel input path.

**Files:**
- Create: `src/platform/input_tape.c`, `src/platform/input_tape.h`
- Modify: `src/platform/main_pc.c` (args `--record-tape <path>`, `--play-tape <path>`), `src/joy.c` hook wiring guarded `#ifdef NATIVE_PORT` (set `g_ContRecordFunc`/`g_ContPlaybackFunc` — `joy.c:111-112`, invoked at `:429/:440/:471`)
- Create: `tools/fidelity/tape_regression.sh`; ctest-style fixture dir `baselines/tapes/` (tapes are input-only — NOT ROM-derived — so committable)
- Test: `tests/test_input_tape.c` (format roundtrip, ROM-free)

**Format (`.ge7tape`, versioned):** header `{magic "GE7TAPE1", version u32, level id, difficulty, rng_seed u64, tick_hz u32, flags}` then per-tick records `{u32 tick, per-player {u16 buttons, s8 stick_x, s8 stick_y}}` (delta-encoded runs optional later — YAGNI now). Recording captures post-binding, pre-game samples (the same bytes `osContGetReadData` produced); playback substitutes them at the `g_ContPlaybackFunc` seam exactly as RAMROM does (`ramromreplay.c:1494-1496` is the reference for the substitution point and fields).

**Semantics:** `--record-tape`/`--play-tape` force `--deterministic` seeding (fixed seed recorded in header; assert on playback), require `--level`, and refuse `--ramrom`. Playback asserts tick alignment (`g_GlobalTimer` vs record tick) and emits `--sim-state-hash-out` at end-of-tape; `tape_regression.sh` replays each committed tape and compares the final sim hash + a `--trace-state` capture against `baselines/tapes/<name>.expected.json`.

**Steps:**
- [x] **Step 1:** Write failing unit test `tests/test_input_tape.c`: write a synthetic 600-tick tape via the writer API, read it back, assert byte-equality of records and header roundtrip. Register ctest `input_tape`. Run → FAIL (functions undefined). (done: ctest input_tape registered, CMakeLists.txt:416, tests/test_input_tape.c)
- [x] **Step 2:** Implement `input_tape.c` writer/reader + hook functions `tapeRecordFunc`/`tapePlaybackFunc` matching the `g_ContRecordFunc`/`g_ContPlaybackFunc` signatures at `joy.c:111-112`. Run test → PASS. (done: src/platform/input_tape.c/.h + joy.c hook wiring)
- [x] **Step 3:** Wire args + hooks; record a 30-second Dam tape locally under the determinism envelope with scripted `GE007_AUTO_*` movement (no human input needed: use `GE007_AUTO_LOOK_STEP` + warp scripts to generate motion); replay twice; assert identical final sim hash across (a) replay vs replay, (b) replay vs record run. (done: baselines/tapes/dam_forward_30s.ge7tape + .expected.json; ledger FID-0034 status=verified)
- [x] **Step 4:** Write `tape_regression.sh` (iterate `baselines/tapes/*.ge7tape`, replay, compare). Commit one starter tape per §5 Task 1.2 route as they land (tapes accompany routes). Append to verify manifest tier 3. (done: tools/fidelity/tape_regression.sh + verify_manifest.txt:19)
- [x] **Step 5:** Ledger `FID-0034 → verified`. Commit. (done: ledger FID-0034 status=verified / commit 5ae6fc7)

### Task 0.7: Struct-layout assert harness (FID-0035, FID-0036 first half)

**Files:**
- Create: `tests/test_struct_layout.c` (ROM-free ctest `struct_layout`)
- Create: `tools/fidelity/gen_struct_asserts.py` (one-time generator; output pasted into the test, then hand-maintained)

**Steps:**
- [x] **Step 1:** Generator: parse the two hand-maintained N64/PC size tables (`src/game/prop.c:1850-1866` `sizepropdef_pc_bytes` switch and `src/game/loadobjectmodel.c:56-104`) and emit `_Static_assert(sizeof(T) == N, "...")` per PROPDEF record type, plus offset asserts (`offsetof`) for every union / bitfield-adjacent field flagged in §0.3 FID-0036 (MultiAmmoCrateRecord slots at 0x80 pair-stride; TankRecord `rect4f` at 0x84; KeyRecord union at 0x80) and the StandTile family (`src/bondtypes.h:387-449`: `StandTilePoint`, `StandTileHeaderMid`, `StandTileHeaderTail`, `StandTile`, `StandFileTile`) with expected values taken from the current (known-good on all 3 shipping platforms) build — this locks in the `-mno-ms-bitfields` layout so any compiler/flag drift fails the build instead of corrupting navmesh reads at runtime. (done: tests/test_struct_layout.c, 47 _Static_assert lines + tools/fidelity/gen_struct_asserts.py / commit 0d7165e)
- [x] **Step 2:** Build + run `ctest -R struct_layout` on macOS → PASS. (CI's MinGW cross-check lane inherits it — the exact scenario this guards.) (done: ledger FID-0035 note "struct_layout ctest PASS (macOS clang) + MinGW cross-compile PASS")
- [x] **Step 3:** Append to verify manifest tier 1. Ledger `FID-0035 → verified`. Commit. (done: ctest struct_layout registered CMakeLists.txt:466; ledger FID-0035 status=verified / commit 0d7165e)

---

## 4. The Act Pipeline — fix lifecycle (normative for all Phase 2+ work)

Every finding the loop acts on moves through these stages; each stage's exit produces evidence attached to the ledger transition.

**4.1 Root-cause.** Locate the authoritative reference: for sim — the `GLOBAL_ASM` body + `jpt_*` tables for the function(s) involved (grep the VRAM address from the symbol name, e.g. `sub_GAME_7F0B0914`); for renderer — the ares capture (pixel/RDP-stream) demonstrating stock behavior; for converters — the raw ROM bytes at the documented offset. Write the divergence statement: *retail does X (anchor), port does Y (file:line), player-visible effect Z, repro command R*. Ledger → `root-caused`.

**4.2 Fix behind a flag.** Implement per charter rules 4–5. `port-defect` ⇒ fix default-ON with `GE007_NO_<FIX>`/`GE007_LEGACY_<X>` negative control; `n64-quirk` mitigation ⇒ opt-in default-OFF. Sim fixes must cite the ASM anchor in a comment at the change site (existing house style — see `gun.c:18327-18331`).

**4.3 Oracle-verify.** Run the narrowest oracle lane that demonstrates the fix: trace comparator on the relevant route (fix-ON matches stock where it previously diverged; fix-OFF reproduces the divergence — both artifacts saved). For visual fixes, pixel/ROI comparator before/after. Ledger → `landed` on commit.

**4.4 Regression lane.** Add a permanent guard: either a new scene/route in an existing suite (preferred — e.g. add a route JSON + comparator thresholds to the trace sweep gate set) or a new `tools/` smoke registered in ctest. Append to `verify_manifest.txt` if it's a new command.

**4.5 Full verify.** `tools/fidelity/verify_all.sh` (tier per change class: renderer-only ⇒ tiers 1–2 + parity captures; sim-touching ⇒ all tiers). Green ⇒ commit with ledger ID in message; ledger → `verified` with the verify report as evidence. Red ⇒ REPAIR phase (fix or revert; never commit red).

**4.6 Waivers, quirks, and the approximation registry.** Three documents make "explained divergence" auditable:
- `docs/fidelity/APPROXIMATIONS.md` — renderer approximation classes accepted as non-goals for pixel-exactness (VI output filter, 3-point-filter screen-derivative approximation, RDP dither, coverage AA), each with: why, bound (max expected pixel delta / affected region class), and the comparator masks/thresholds that encode it.
- `docs/fidelity/QUIRKS.md` — n64-quirk registry (FID-0045): retail bugs faithfully preserved (`chraidata.c:715` guard shuffle, etc.), each with ASM anchor and the opt-in mitigation flag if one exists.
- Ledger `waived` status — requires `waiver.reason` + `waiver.retest` (a concrete condition, e.g. "re-test when FID-0032 verified"). The coverage critic (S6) re-opens waivers whose retest condition has become true.

---

## 5. Phase 1 — Oracle & Sense-Lane Build-Out

### Workstream 1: Oracle expansion (unblocks the most findings)

#### Task 1.1: Combat/floor-field oracle extension (FID-0032) — XL, highest leverage

Extends the instrumented-ares tracer and the native tracer with the combat/floor state needed to evidence FID-0011/0012/0013/0025 and all future AI/combat parity work. Per `docs/design/COMBAT_DEFERRED_PLAN.md` §2.1 this is the single biggest blocker.

**Files:**
- Modify: `tools/prepare_ares_movement_oracle_build.sh` (the embedded ares patch: extend the per-frame JSONL emitter — current emitter fields at `:4553-4672`; symbol addresses arrive via the existing `MGB64_ARES_*` symbol-layout envs at `:1202-1246`, extend that table)
- Modify: `src/platform/port_trace.c` (emit the same new fields natively — schema parity is the contract; native emitters at `:7693/:7951`)
- Create: `tools/compare_combat_trace.py` (align + compare with per-field tolerance table, modeled on `compare_movement_trace.py:22-68`)
- Modify: `tools/rom_oracle_route.py` (new `compare_combat_*` route fields), `tools/audit_oracle_trace.py` (accept the new record kind)

**New per-frame fields (both sides, identical JSON keys):**
```
"guards": [ {"chrnum", "pos":[3], "actiontype", "aimode", "health", "shotbondsum",
             "flags_onscreen":0|1, "target_visible":0|1, "anim_hash":"0x…", "room"} ]   # active chrs only
"floor":  {"stan_id", "stan_room", "stan_flags", "height"}                              # player's current tile
"combat": {"player_health", "player_armor", "shots_fired_total", "hits_landed_total",
           "rng_seed":"0x…"}                                                            # extends existing block
"projectiles": [ {"kind", "pos":[3], "owner_chrnum"} ]                                  # mines/grenades/rockets
```
Address sources: on ares, read N64 RDRAM at the US symbol addresses (already how movement fields work — the VRAM addresses are encoded in decomp symbol names, e.g. guard array base and `PropRecord`/`ChrRecord` field offsets from `src/bondtypes.h`; add them to the symbol-layout env table). On native, read the same structs directly.

**Steps:**
- [x] **Step 1:** Field-offset dossier: for each new field, record `{struct, field, US VRAM address or base+offset, size, endianness}` in `docs/fidelity/combat_oracle_fields.md`, derived from `bondtypes.h` + the inline ASM users. This dossier is the review artifact for the patch. (done: docs/fidelity/combat_oracle_fields.md exists / commit 64547f1)
- [x] **Step 2:** Extend the ares patch + rebuild (`tools/prepare_ares_movement_oracle_build.sh`); capture a Dam route; validate JSON shape with `audit_oracle_trace.py`. (done: ledger FID-0032 landed note -- ares patch formatCombatOracleJson + Dam capture / commit 64547f1)
- [x] **Step 3:** Extend `port_trace.c` natively; capture the same route; run `compare_combat_trace.py` — expected: alignment succeeds, some fields diverge (those divergences are *findings* — file them via `ledger.py new`, don't fix them in this task). (done: port_trace.c traceBuildCombatOracleJson + tools/compare_combat_trace.py; divergences filed as FID-0053/FID-0054/FID-0055 / commit 64547f1)
- [x] **Step 4:** Tolerance table review: integer fields exact (chrnum, actiontype, aimode, flags, stan_id, health as fixed-point raw); float positions per movement-comparator epsilons. (done: tools/compare_combat_trace.py tolerance table (dossier #5) / commit 64547f1)
- [x] **Step 5:** Add a combat-route contract lane (extend `tools/route_contract_smoke.sh`) + ctest registration. Append gate-mode to verify manifest. (done: ctest combat_oracle_contract, CMakeLists.txt:147 / tools/combat_route_contract_smoke.sh)
- [x] **Step 6:** Ledger: `FID-0032 → verified`; auto-unblocks FID-0011/0012/0013/0025 (they flip to `actionable`). Commit. (done: ledger FID-0032 status=verified / commit 2b29860)

#### Task 1.2: Route coverage — all 20 stages (FID-0031)

**Goal state:** per stage: `intro` route (exists for Dam; 12 placeholders to make real), `traverse` route (scripted movement through 2–3 rooms exercising stan seams + combat contact), and `completion` route (organic mission completion). Each route = `tools/rom_oracle_routes/<stage>_<kind>.json` + `.ares-input` script + native `GE007_AUTO_*` env or input tape (Task 0.6) + expected-fields block, passing `audit_oracle_trace.py` route-control rules and the route contract smoke.

**Method (agent-executable per route, no human play required):**
- [ ] **Step 1 per stage:** author the ares input script (menu navigation with seeded EEPROM via `tools/make_unlocked_eeprom.py`, then gameplay inputs); iterate headlessly until `audit_oracle_trace.py` accepts (min gameplay-input/moving-record counts, no illegal menu input) and the stage's objective-complete flag appears in the trace.
- [ ] **Step 2 per stage:** mirror as a native tape (`--record-tape` replaying the same stick/button sequence via a small `tools/fidelity/ares_input_to_tape.py` converter — both formats are per-tick button/stick tuples).
- [ ] **Step 3 per stage:** capture both sides, run movement + combat comparators; divergences → `ledger.py new` (they are the product, not a task failure); attach route to the sweep set (Task 2.1).
- [ ] Priority order: stages with known suspect areas first — Facility & Bunker stairwells (FID-0013 seams), Train (FID-0008), Silo (FID-0009), Surface 2 (FID-0026 audio), then the rest.
- [ ] Exit: `ledger.py transition FID-0031 --to verified` when 20×3 routes pass contract; the placeholder-waiver rows in M8.2 are deleted.

#### Task 1.3: Pixel-oracle normalization layer (FID-0044 prerequisite)

Native renders 640×480 BMP; ares emits N64-native PPM (VI-filtered). Define the comparison contract once, centrally.

**Files:**
- Create: `tools/fidelity/pixel_normalize.py` — decode both, scale ares→640×480 (nearest for integer factors, area-average otherwise), optional VI-deblur toggle, output aligned PNG pairs + metadata
- Create: `tools/fidelity/pixel_diff.py` — per-pixel abs-delta histogram, connected-component clustering of super-threshold regions (component: bbox, area, mean delta, dominant hue), classification against `docs/fidelity/APPROXIMATIONS.md` masks (sky region, dithered gradients, filtered edges: ±1px dilated edge mask), verdict JSON: `{clusters_unexplained: N, clusters: [...]}`
- Create: `docs/fidelity/APPROXIMATIONS.md` seeded with the four classes from §4.6 and initial thresholds (edge-mask dilation 1px; per-class delta bound measured empirically in Step 2)

**Steps:**
- [x] **Step 1:** Implement both tools (Pillow only). (done: tools/fidelity/pixel_normalize.py + pixel_diff.py exist / commit de4afba)
- [x] **Step 2:** Calibrate on 5 known-good matched captures (Dam route frames where trace parity already holds): tune thresholds until known-good pairs report `clusters_unexplained: 0` and a deliberately-broken pair (e.g. `GE007_TILESIZE_CLAMP_SUBLOAD=0` confetti repro) reports ≥1 — both calibration artifacts saved as the tool's negative/positive controls in a unittest with tiny committed synthetic images (not ROM captures). (done: ran `python3 -m unittest tools.fidelity.tests.test_pixel_diff` -- 9/9 PASS incl. test_known_good_pair_zero_unexplained + test_broken_pair_surfaces_defect, verified 2026-07-10)
- [x] **Step 3:** Commit. (done: commit de4afba)

### Workstream 2: Sense lanes (the discovery engine)

Each lane: standalone script, emits `docs/fidelity/reports/sense_<lane>_<ts>.json` = `{lane, inputs, candidates: [{title, class-guess, surface, evidence-path, repro, suspect?}], skipped: [...]}`, then the loop (or the script with `--file-findings`) dedupe-checks and files new ledger entries.

#### Task 2.1: S1 — Trace differential sweep (`tools/fidelity/sense_trace_sweep.sh`)

- [x] For every route in the route corpus with a stock capture: run native capture (envelope per charter rule 6), run every applicable comparator (movement, combat, intro, glass), collect divergent-field reports. `--gate` mode runs only routes marked `known-good` in the route JSON (`"gate": true`) and exits 1 on any divergence — that mode joins the verify manifest (this is the ratchet: every route that reaches parity flips `gate: true` and can never silently regress). (done: tools/fidelity/sense_trace_sweep.sh --gate mode implemented / commit c7f964e)
- [x] Stock captures are cached (`/tmp` is per-run; cache dir `build/oracle_cache/<route>/<ares-build-hash>/` keyed on ares build + route JSON hash — re-capture only on miss). Cache is git-ignored. (done: CACHE_DIR=build/oracle_cache in sense_trace_sweep.sh, git-ignored)
- [x] Commit + append gate-mode to manifest. (done: verify_manifest.txt "tools/fidelity/sense_trace_sweep.sh --gate" / commit c7f964e)

#### Task 2.2: S2 — Pixel differential sweep (`tools/fidelity/sense_pixel_sweep.sh`) (FID-0044)

- [x] For each route: at each route-defined `screenshot_game_timer` checkpoint (add 2–4 checkpoints per route JSON — intro end, mid-traverse, combat moment, objective moment), capture native BMP + ares PPM at matched `g_GlobalTimer` (both sides support game-timer-keyed capture: `--screenshot-game-timer` / `MGB64_ARES_SCREENSHOT_GAME_TIMER`), normalize (Task 1.3), diff, cluster, classify. Unexplained clusters → candidates with both images + diff visualization attached. (done: tools/fidelity/sense_pixel_sweep.sh / commit 2c60892)
- [x] Sanity rails: `audit_screenshot_health.py` both sides (rejects blank/truncated captures before diffing). (done: tools/audit_screenshot_health.py invoked at sense_pixel_sweep.sh:149,280)
- [x] Commit; add a `--gate` mode (known-good checkpoints, zero unexplained clusters) to manifest tier 3. (done: verify_manifest.txt "tools/fidelity/sense_pixel_sweep.sh --gate" / commit 2c60892)

#### Task 2.3: S3 — RDP command-stream differential (`tools/fidelity/sense_rdp_sweep.sh`) (FID-0043)

Pixel diffs say *that* something differs; the RDP stream says *what*. Ares already has `MGB64_ARES_TRACE_RDP_COMMANDS`; native has `GE007_TRACE_RDP_RENDER_MODES`/`GE007_TRACE_N64_DL` + existing native-only analyzers (`port_stock_rdp_command_stream_analyzer_guard`).

- [ ] Create `tools/fidelity/compare_rdp_stream.py`: parse both traces at a matched frame; normalize into draw-events `{seq, combine_mode, othermode_h, othermode_l, tile: {fmt,size,line,mask,shift,clamp,mirror}, tri_count, class-tag}`; align by sequence with gap tolerance; report mismatches grouped by material class. Known-acceptable stream differences (the port's raw-RDP sky queue replay reordering, port-added scissor rects) get an explicit normalization list in the tool (documented in APPROXIMATIONS.md).
- [ ] Wire the sweep script: one matched frame per route checkpoint (same checkpoints as S2 — pixel cluster + RDP mismatch at the same checkpoint co-attach to one candidate).
- [ ] Calibrate on known-good Dam checkpoints; negative control: `GE007_LEGACY_WEAPON_FORCED_FOG=1` must produce a blender-field mismatch on a Bunker checkpoint.
- [ ] Commit.

#### Task 2.4: S5a — ASM audit queue (`tools/fidelity/asm_audit.py`) (FID-0042)

Systematizes what the 07-06/07-07 audits did by hand: every reimplemented function gets an adversarial ASM-vs-C review with a recorded verdict.

- [x] **Step 1:** Build the queue: scan `src/` for `GLOBAL_ASM` bodies; extract function symbol, file, line span, byte size (instruction count), referenced `jpt_*` tables, and the corresponding live C body location (`#ifdef NONMATCHING`/`NATIVE_PORT` sibling). Emit `docs/fidelity/asm_audit_queue.json` (388 entries) with per-entry status `unreviewed | verified-equivalent | finding-filed:FID-NNNN | waived:<reason>`. (done: docs/fidelity/asm_audit_queue.json -- 388 entries, ranks 1..388 contiguous, confirmed via `asm_audit.py validate` 2026-07-10 / commit c9bd6b5)
- [x] **Step 2:** Risk-rank the queue: (a) files with prior confirmed defects first (gun.c, bondview.c, chrobjhandler.c, stan.c, prop.c converters), (b) then by instruction count × has-jump-table (dispatch semantics are the proven failure mode), (c) bg.c/model.c bulk last. (done: risk-ranked queue, ranks 1..388 contiguous / commit c9bd6b5)
- [x] **Step 3:** Define the review contract (this is the prompt the loop's audit agent executes; store in `docs/fidelity/CHARTER.md` §audit): *given entry E: read the GLOBAL_ASM body + jump tables; reconstruct control flow (branches, switch groupings, early-outs, arithmetic including fixed-point shifts); diff against the live C body; for any semantic difference, write the divergence statement (§4.1) and file a candidate finding with the instruction-level citation; else mark verified-equivalent with a 2-line justification. Never consult the `#else` C reference body as authority.* (done: docs/fidelity/CHARTER.md "#audit -- ASM-vs-C review contract (Task 2.4 Step 3)" / commit 0c688bf)
- [x] **Step 4:** `asm_audit.py next [--count N]` / `asm_audit.py record <symbol> --verdict V [--fid FID-NNNN] --note ...` (validates verdict transitions; regenerates a progress table in `docs/fidelity/LEDGER.md`). (done: tools/fidelity/asm_audit.py cmd_next / cmd_record / commit c9bd6b5)
- [x] **Step 5:** Pilot: run the review contract on 5 entries from the top of the ranked queue by hand (as the loop would); calibrate the contract wording against the known-fixed weapon-switch case (`jpt_80054084` groupings must be re-derivable from it). Commit. (done: 14/388 reviewed -- pilot batch (5, c9bd6b5) + batch-2 (9, 05b80cc), confirmed via `asm_audit.py stats` 2026-07-10)

#### Task 2.5: S4 — Soak/fuzz/sanitizer lane (`tools/fidelity/sense_soak.sh`)

- [x] Wrap existing endurance tools into one candidate-emitting lane: `tools/soak_stability.sh` (crash/NaN/DL-resolve, `--max-crashes 0`), ASan build lane (`build-asan/` + `tools/asan_smoke.sh`), dyn-allocator stress (`GE007_DYN_STRESS_LIMIT` sweep across 5 levels watching `g_dyn_overflow_count` + `[RENDER-HEALTH]` lines), purity fuzz (Task 0.5) full matrix, and a `GE007_AUTO_*` event fuzzer: seeded random schedule of warp/guard-spawn/damage/collect scripted events on a deterministic run, asserting no crash/watchdog/RENDER-HEALTH anomalies and (same seed ⇒ same final sim hash) self-consistency. (done: tools/fidelity/sense_soak.sh wraps soak_stability.sh/asan_smoke/dyn-stress/purity-fuzz/AUTO_* fuzzer; ran --self-test 2026-07-10 -> 6 candidates / commit 00b3961)
- [x] Harvest rule: any new `[RENDER-HEALTH]`/watchdog/ASan/counter anomaly signature (dedupe by signature hash) → candidate finding with full log + repro seed. (done: --self-test run 2026-07-10 harvested distinct signatures [asan/dyn-overflow/render-health/soak-audit-fail/ubsan/watchdog])
- [x] Commit. (done: commit 00b3961)

#### Task 2.6: S6 — Coverage critic (`tools/fidelity/sense_coverage.py`)

The lane that keeps the loop honest. Pure-static, cheap, runs every iteration.

- [x] Checks: stages missing route kinds (vs Task 1.2 goal state); trace-schema fields present in JSONL but absent from every comparator's compared-field list; `GE007_*` flags matching `(FIX|LEGACY|NO_)` patterns with no lane referencing them (cross-ref `docs/ENV_FLAGS.md` vs `tools/` grep); ledger `waived` entries whose retest condition references a now-`verified` FID; ASM audit queue staleness (entries unreviewed > N iterations while queue rank ≤ 50); verify-manifest gates that were skipped in the last verify report (charter rule 9 visibility); doc-staleness sentinels (backlog text contradicting code — pattern from M8.1/COMBAT_GLIDEPATH §3.2 incidents: each closed FID includes doc-sync in its checklist, and the critic greps for the FID's backlog anchor). (done: tools/fidelity/sense_coverage.py -- ran 2026-07-10, emitted 22 candidates incl. route-kind + flag-hygiene checks / commit 6fae22e)
- [x] Output: candidates of class `coverage-gap`/`instrumentation-gap`. (done: 2026-07-10 run emitted candidates classed coverage-gap/instrumentation-gap)
- [x] Commit. (done: commit 6fae22e)

---

## 6. Phase 2 — The Loop (agent orchestration)

### Task 3.1: Loop driver (`tools/fidelity/loop_iteration.sh`) + iteration report

Mechanical part of an iteration the agent shells out to; the *decisions* stay with the agent per the §2 policy.

- [x] `loop_iteration.sh status` → emits `docs/fidelity/reports/loop_status.json`: `{head_sha, verify: <latest verdict + age>, ledger_stats, actionable: [top 10 by priority/age with blocked_on resolved], lane_staleness: {S1..S6: iterations-since-run}, escalations_open: N}` — everything the agent needs to pick a phase, in one read. (done: ran `tools/fidelity/loop_iteration.sh status` 2026-07-10 -> emits the documented JSON schema / commit ec4d9e6)
- [x] `loop_iteration.sh sense <lane>` → runs that lane, auto-files deduped candidates (`ledger.py dedupe-check` then `new`), prints filed/duplicate counts. (done: loop_iteration.sh cmd_sense dispatch / commit ec4d9e6)
- [x] `loop_iteration.sh verify [--tier N]` → verify_all wrapper. (done: loop_iteration.sh cmd_verify dispatch / commit ec4d9e6)
- [x] `loop_iteration.sh checkpoint "<msg>"` → asserts clean gates for the changed surface, commits (charter rule 7 format), regenerates `LEDGER.md`. (done: loop_iteration.sh cmd_checkpoint dispatch / commit ec4d9e6)
- [x] Iteration counter + lane-staleness ledger in `docs/fidelity/reports/loop_state.json` (committed, so the loop resumes across sessions/machines). (done: docs/fidelity/reports/loop_state.json tracked in git / commit ec4d9e6)

### Task 3.2: Agent charter + launch configuration

- [x] Append Appendix B (iteration prompt) to `docs/fidelity/CHARTER.md`. (done: docs/fidelity/CHARTER.md "Appendix B -- Loop iteration prompt" / commit 0c688bf)
- [ ] Launch modes (pick per run; all resume from git + ledger state, so they're interchangeable):
  - **Interactive-supervised:** run iterations in a Claude Code session; human watches.
  - **Unattended local:** `/loop` (self-paced) with the Appendix B prompt, or the ralph-loop plugin; each iteration ends at a `checkpoint` commit, so interruption is always safe.
  - **Scheduled:** cron/scheduled agent invoking the same prompt nightly with a token/time budget.
- [x] Budget policy in the prompt: per iteration, at most one ACT finding or one SENSE lane; hard-stop conditions (verify red twice on the same finding ⇒ mark `blocked(repair-failed)` + escalate; contradictory evidence ⇒ escalate; any gate-weakening temptation ⇒ escalate). (done: CHARTER.md Appendix B "Hard rules: one finding or one lane per iteration ... append an escalation entry")
- [x] Escalation file: `docs/fidelity/ESCALATIONS.md` — append-only; loop halts a finding, not the whole run, unless verify is red on HEAD. (done: docs/fidelity/ESCALATIONS.md exists / commit 86eb1b5)
- [ ] Dry-run acceptance: execute 3 supervised iterations end-to-end (expected: 1 SENSE harvest, 2 ACT fixes from P1 seeds like FID-0006/FID-0015 which are small and root-caused) before first unattended run.

### Task 3.3: Nightly endurance profile

- [ ] `tools/fidelity/nightly.sh`: full sense sweep (all lanes) + full verify (all tiers) + 2-hour soak; writes a dated report; intended for cron on the dev machine (CI stays manual-dispatch per repo policy — this runs locally).
- [ ] Exit criteria hook: when `ledger.py stats --assert-closed` passes and two consecutive nightly sense sweeps file zero candidates, emit `docs/fidelity/reports/S_TIER_CANDIDATE.md` — the human-review package for declaring §1 met.

---

## 7. Phase 3 — Fix Workstreams (the seeded queue the loop consumes)

Ordered by (priority, unblock-fanout). Each item = one-or-more loop ACT iterations following §4; contracts below give the anchor, evidence requirement, and gate. (These are contracts, not step-by-step code — the fix code is derived from the ASM/oracle evidence at execution time, per charter rule 1.)

### WS-A: Visible-artifact class (the "no more shards, no more artifacts" core)
- [ ] **FID-0006** intro weapon sub-buffer guard: convert the `bondview.c:3353` overlap print into a fail-closed guard (skip weapon load + `[RENDER-HEALTH]`), re-check after each of `:3269/:3276/:3346`; evidence: forced-small-buffer repro shows guard trip and no corruption; gate: intro visual regression + new forced-fail smoke. (Small, already root-caused — good first ACT.)
- [x] **FID-0007** dyn-allocator residuals: land T9b leak fix; extend fail-closed contract to the glass float-mtx path; evidence: `GE007_DYN_STRESS_LIMIT` sweep shows zero alias events at all pressures; gate: stress sweep joins S4. (done: ledger FID-0007 status=verified / ctest port_dyn_glass_stress_smoke, CMakeLists.txt:722 / commit b80abc5)
- [ ] **FID-0003** glass shard scale sign-off: capture ares shard coverage on the pad10092 shatter route (shard pixel columns via `MGB64_ARES_*` glass fields + pixel oracle); score all six candidate scale flags with `compare_glass_shard_pixel_oracle.py`; pick the winner as default, delete the other five flags; evidence: coverage-delta table; gate: shard-coverage checkpoint joins S2 gate set.
- [ ] **FID-0001** center-glass FB-memory blend: RDP-stream diff (S3) at the pane checkpoint to pin the exact combine/blend divergence; fix classification in `gfx_room_xlu_cvg_memory_*`; evidence: pixel cluster at pane ROI goes explained→matched; gate: pane checkpoint `gate: true`.
- [ ] **FID-0002** per-pane snapshot: honor `pertri` on Metal; evaluate default per-pane snapshot for overlapping-pane batches (perf-checked via `port_perf_budget_smoke`); evidence: two-pane overlap scene GL=Metal=ares.
- [ ] **FID-0004** shatter-trigger/tinted-glass/crack parity: combat-oracle (Task 1.1) glass fields give stock hit-counts and opacity ramps; align `chrobjhandler.c` constants; evidence: shatter-at-same-hit-count trace match.
- [ ] **FID-0008/0009/0010** portal/visibility trio: extend the T13b draw-only walk to aperture chains (multi-hop grazing portals) replacing both heuristics where oracle-backed; implement real frustum at `bg.c:7441` (retail reference: `sub_GAME_7F054D6C` users); evidence: Train room-51 + Silo 28/44 checkpoints show zero sky-leak clusters AND no over-admission (RDP tri counts vs ares within bound); gate: both checkpoints `gate: true`, train regression lane un-quarantined.

### WS-B: Sim parity vs ASM (combat/AI/movement)
- [ ] **FID-0013** stan seam parity (needs Task 1.1 floor fields): Facility/Bunker stairwell traverse routes; compare `floor.stan_id` per frame; evidence: zero seam mismatches over full routes; gate: routes `gate: true`.
- [ ] **FID-0012** guard-visibility Phase C default flip (needs Task 1.1): flip `GE007_CHRBEAMS_FRUSTUM` default with combat-field evidence; re-baseline the 2 calibrated 1P routes with justification files; gate: hidden-guard + combat contracts.
- [ ] **FID-0011** chrTickBeams H4–H7: ASM-audit contract (Task 2.4) on `chr.c:5566/6418/7344` region; restore actiontype dispatch + BACKGROUND_AI + held-weapon lifecycle per ASM; evidence: combat trace parity on guard-contact routes; gate: combat sweep.
- [ ] **FID-0014** patrol force-loop: replace workaround with root fix (ONSCREEN under fog per retail); must land with/after FID-0012.
- [x] **FID-0015/0016/0017** input/HUD trio: radial deadzone (mirror aim-stick implementation), wheel-notch accumulation, `speedgraphframes` clamp parity with `lvl.c:2113-2126`; all three are root-caused with exact sites — early ACT fodder. (done: ledger FID-0015/FID-0016/FID-0017 status=verified / ctests radial_deadzone, weapon_cycle_queue, speedgraph_clamp / commits fdd66d9, 06addab, da53337)
- [ ] **FID-0036** converter differential test: standalone `tools/fidelity/propdef_differential.py` — parse every stage's setup propdef stream with an independent big-endian reference reader (pure python, from struct defs), diff against the C converter's output (add `GE007_DUMP_PROPDEFS=<path>` dump after `propdef_convert_n64_to_pc`); evidence: byte-level agreement on all 20 stages or filed findings per mismatch; gate: new ctest lane.

### WS-C: Renderer interpreter & backend parity
- [ ] **FID-0020** seven interpreter sharp edges: one ACT iteration each; each gets a targeted repro (most have exact sites) + RDP/pixel evidence where player-visible; low-risk hardening otherwise.
- [ ] **FID-0018** Metal MSAA/A2C; **FID-0019** Metal shadow/depth/autorelease; **FID-0021** degraded-target/readback; **FID-0022** `G_ZS_PRIM`; **FID-0023** gfx_ptr registry; **FID-0024** Z_UPD rule: per-item contracts as in the backlog M3 entries; evidence: GL-vs-Metal parity captures + RDP semantics; gate: parity scenes.
- [ ] **FID-0041** room XLU sort: decide faithful-mode default (DL-order) with pixel-oracle evidence on fog/translucency-heavy checkpoints; keep depth-sort as remaster-path option.

### WS-D: Audio parity
- [ ] **FID-0025/0026/0027** (needs audio routes from Task 1.2 set): A/B `env_sample_xor`/pole ordering vs ares on a music-heavy route (`compare_audio_reference.py`); Surface-2 instrument solo traces (`GE007_MUSIC_SOLO_PROGRAMS`) vs ares stems for bank tuning; add DMA-window/voice-starvation counters, measure under Surface-2 load; evidence: spectral/sample deltas quantified, fixes or documented-waivers per finding.
- [ ] **FID-0028** delete dead synth paths (hygiene; after the above so no evidence surface is lost).

### WS-E: Intro/outro closure
- [ ] **FID-0029** M1.5 waiver retest batch: re-run D32/D35/D37/D38/D39/D41 oracle routes post-D43/D44; close or re-open each with fresh evidence; static-shot camera-seed (D37/D39) and duration source (D38) get root-cause iterations against ares captures at the static-shot entry.

### WS-F: Latent/low
- [ ] **FID-0037** pname byte-swap, **FID-0038** PAL note, **FID-0039** OP11, **FID-0040** clamp-asymmetry documentation, **FID-0045** quirks registry seeding.

---

## 8. Risks & Failure Policy

| Risk | Mitigation |
|---|---|
| Oracle itself wrong (ares inaccuracy vs real N64) | ares is the best available proxy; for pixel-level disputes, cross-check against RDP documentation + hardware capture if ever available. Findings that hinge on suspected ares error → escalate, never "fix" the port to match a suspected emulator bug (note the precedent: `train_window_backdrop_regression.sh` FALSE ALARM history). |
| Agent drift over long runs (hallucinated evidence, scope creep) | Evidence monopoly (rule 3) — `ledger.py` refuses transitions without artifacts; verify suite before every commit; one-finding-per-iteration budget; escalation file instead of guessing. |
| Route captures flaky (timing, EEPROM state) | Route-control audit (`audit_oracle_trace.py`) gates every capture; stock-capture cache keyed on ares build hash; determinism envelope mandatory (rule 6). |
| Baseline re-baselining abuse (regenerating goldens to hide regressions) | Re-baselines only in ACT with a ledger entry + justification file; coverage critic flags baseline changes without matching ledger evidence. |
| Perf regressions from fidelity fixes | `port_perf_budget_smoke` (60fps floor hard-fail) is in the verify suite; perf-sensitive fixes get a `perf_census.sh` before/after in evidence. |
| 8 MB pool / memory-pressure differences vs N64 | dyn stress lane (S4) covers the port's overflow contract; parity questions about *when* retail overflows are n64-quirk territory (registry). |
| Loop starves EXPAND while grinding P2s | Scheduling policy places lane-staleness above low-priority ACT: SENSE/EXPAND run when any lane is stale regardless of remaining P2/P3 queue. |

---

## Appendix A: Ledger entry JSON schema (`tools/fidelity/ledger_schema.json`)

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": ["id", "title", "class", "surface", "priority", "status", "evidence", "repro", "history"],
  "properties": {
    "id":       {"type": "string", "pattern": "^FID-[0-9]{4}$"},
    "title":    {"type": "string", "maxLength": 200},
    "class":    {"enum": ["port-defect", "parity-divergence", "n64-quirk", "instrumentation-gap", "coverage-gap"]},
    "surface":  {"enum": ["sim", "renderer", "audio", "converter", "infra"]},
    "priority": {"enum": ["P0", "P1", "P2", "P3"]},
    "status":   {"enum": ["discovered", "triaged", "root-caused", "fix-in-progress", "landed",
                           "verified", "documented", "waived", "refuted"]},
    "blocked_on": {"type": "array", "items": {"type": "string", "pattern": "^FID-[0-9]{4}$"}},
    "suspect":  {"type": "array", "items": {"type": "string"}},
    "repro":    {"type": "string"},
    "evidence": {"type": "array", "minItems": 1, "items": {
      "type": "object", "required": ["kind", "path"],
      "properties": {
        "kind": {"enum": ["trace-diff", "pixel-diff", "rdp-diff", "asm-citation", "gate-log",
                           "doc-anchor", "counter-log", "audio-diff"]},
        "path": {"type": "string"}, "route": {"type": "string"}, "note": {"type": "string"}}}},
    "gates":    {"type": "array", "items": {"type": "string"}},
    "waiver":   {"type": ["object", "null"], "required": ["reason", "retest"],
                 "properties": {"reason": {"type": "string"}, "retest": {"type": "string"}}},
    "history":  {"type": "array", "items": {
      "type": "object", "required": ["ts", "from", "to"],
      "properties": {"ts": {"type": "string"}, "from": {"type": "string"}, "to": {"type": "string"},
                      "evidence": {"type": "string"}, "note": {"type": "string"}}}}
  }
}
```

## Appendix B: Loop iteration prompt (the unattended-run charter)

```
You are running one iteration of the mgb64 Fidelity Flywheel. Read docs/fidelity/CHARTER.md
(binding rules) and run: tools/fidelity/loop_iteration.sh status

Pick exactly one phase per the scheduling policy in CHARTER.md §policy:
  REPAIR   — verify is red on HEAD: root-cause and fix the regression or revert the offending
             commit. Nothing else until green.
  ACT      — take the top actionable finding. Follow the fix lifecycle (charter §act):
             root-cause with an ASM/oracle anchor → fix behind the correct flag polarity →
             oracle-verify both flag sides → add a regression lane → run
             tools/fidelity/verify_all.sh at the tier for your change class →
             loop_iteration.sh checkpoint "fix(<area>): <title> [FID-NNNN]" →
             ledger.py transition. If evidence contradicts the ledger's root-cause,
             transition it back with a note instead of forcing the fix.
  SENSE    — run the stalest lane via loop_iteration.sh sense <lane>; triage its candidates
             (dedupe, classify per charter taxonomy, set priority); do not fix anything.
  EXPAND   — take the top coverage-critic item (usually: author one oracle route per
             Task 1.2 method, or add a comparator field).
  DRY-CHECK— full sense sweep; if zero candidates twice consecutively, run
             ledger.py stats --assert-closed and produce the S-tier candidate report.

Hard rules: one finding or one lane per iteration. Never commit with verify red. Never
weaken a gate, threshold, or baseline without a ledger entry created FIRST. If the same
finding fails verify twice, append an escalation entry naming it to
docs/fidelity/ESCALATIONS.md (escalated findings drop out of the actionable list) and
move on. If evidence is contradictory or an action is
irreversible (deleting baselines, force-push, remote push), escalate instead of acting.
All game executions use the determinism envelope from CHARTER.md rule 6. End the iteration
at a checkpoint commit and report: phase chosen, work done, evidence paths, ledger deltas.
```

## Appendix C: Command quick-reference

```
# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release -DPORT_VALIDATION_TESTS=ON && cmake --build build -j

# Oracles
tools/prepare_ares_movement_oracle_build.sh           # instrumented ares (movement+combat after Task 1.1)
ARES_BIN=build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares
tools/movement_oracle_capture.sh --route <r> --ares-bin "$ARES_BIN" --out-dir /tmp/o --timeout 300

# Loop
tools/fidelity/loop_iteration.sh status | sense <S1..S6> | verify [--tier N] | checkpoint "<msg>"
python3 tools/fidelity/ledger.py list --actionable
tools/fidelity/verify_all.sh                          # full ratchet
tools/fidelity/nightly.sh                             # endurance profile

# Determinism envelope (every game run)
env -u GE007_DEBUG SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1 \
    GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 build/ge007 --deterministic ...
```

---
*Program plan authored 2026-07-09 from a six-lane deep-research sweep (oracle/determinism infra, open-gap ledger, renderer pipeline, decomp/ASM surface, test/CI scaffolding, sim-tick architecture). Supersedes nothing; consumes `docs/BACKLOG_v0.4.0.md` M-items as seeded findings and defers to `docs/design/UNCAPPED_FPS_PLAN.md` for the F5 interpolation project (out of scope here; the purity gate is shared substrate).*
