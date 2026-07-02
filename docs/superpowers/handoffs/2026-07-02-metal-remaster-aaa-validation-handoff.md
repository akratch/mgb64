# Session Hand-off — Metal Backend Phases 2–5 + `--remaster` + AAA Program Plan
## Independent-validation dossier (2026-07-02)

**Audience:** a fresh agent (or engineer) with no context from the authoring session, tasked
with adversarially validating everything this session claimed and shipped.
**Branch of record:** `feat/metal-backend` (head at time of writing: `c133b89`).
**Rule zero:** the codebase and the commands below are ground truth. This document tells you
*what was claimed and how to re-verify it* — if a verification fails, the claim is wrong, not
your run (subject to the Gotchas in §6, which list the known environmental flakes).

---

## 1. What this session shipped (scope)

One session took the native Metal backend from "clear/present bring-up" (Phase 1) to a
**complete backend + full remaster**, then scoped the AAA program:

| Layer | Deliverable |
|---|---|
| **Phase 2** | Combiner→MSL shader translator (`gfx_metal.mm` mirrors `gfx_opengl.c`'s GLSL generator) |
| **Phase 3** | Full game rendering: geometry/textures/blend/depth, RDP/XLU framebuffer snapshot, CPU readback — at GL parity |
| **Hardening** | 24-finding adversarial review → 20 fixes (autorelease pools, ring vertex arena, region snapshot, growable shader pool, etc.) |
| **Phase 4** | Output-VI-filter post-FX chain on Metal (FXAA/CAS/bloom/grade/tonemap/gamma/vignette/dither/rgb555), faithful to GL's 2-pass double-gamma structure |
| **Phase 5** | **SSAO on native Metal — the op that hangs Apple's GL-over-Metal translator.** The backend's raison d'être, proven |
| **Product** | `--remaster` one-switch flag (Metal + all FX + SSAO), read-only session semantics, `SETTING_OVERRIDE_REMASTER` label |
| **Cross-fixes** | GL `ssao_on` RemasterFX-gate lockstep (latent leak fix); safe default `ge007.ini` (Ssao=0) |
| **Program plan** | `docs/remaster-aaa/` — master plan + 8 adversarially-verified workstream docs (~690 junior-days scoped) |

### Commit inventory (validate: `git log --oneline 9d6714d..c133b89` shows exactly these, oldest last)

| Commit | What | Validate via matrix row(s) |
|---|---|---|
| `72afff8` | docs: Phases 2–3 implementation plan | — (doc) |
| `ff2f16e` | Phase 2.1 combiner→MSL translator | V2, V3 |
| `641b0b0` | Phase 3.1/3.2/3.4 geometry/textures/readback | V3, V4 |
| `888474c` | Phase 3.3 RDP/XLU snapshot (surface 20.3→2.8%, jungle 9.0→2.8%) | V3 |
| `ade6602` | docs: Phases 2–3 complete | — |
| `ac350b7` | review fixes: coverage-alpha preserve + mid-frame readback flush | V12 |
| `dd45899` | hardening: 24-finding review → 20 fixes | V8, V9, V12 |
| `c03fb06` | docs: Phase 4/5 plan (incl. the **depth-crux refutation** — see §5.1) | P1 |
| `2642bba` | Phase 4 output-VI-filter chain | V5 |
| `982fa96` | Phase 4 review fix: faithful 2-pass (GL applies gamma/scale/bias TWICE) | V5, P2 |
| `6508985` | docs: Phase 4 complete | — |
| `635f3a2` | **Phase 5 SSAO** (no-hang payoff) | V6, V7 |
| `ab0c6a1` | `--remaster` flag | V10 |
| `b116237` | docs: Phase 5 + remaster complete | — |
| `197fc03` | review fixes: `--remaster` persistence hazard + SSAO gate lockstep + `remaster` label | V10, V11, P3 |
| `c133b89` | **AAA program plan set** (`docs/remaster-aaa/`, 9 docs) | V13, P4 |

---

## 2. Environment & preconditions

- **Machine context:** Apple M3 Max, macOS (Darwin 25.1). The GL path on this machine is
  Apple's deprecated GL-over-Metal translator and **intermittently hangs** (see §6.1) —
  this is documented reality, not a regression.
- **ROM:** `baserom.u.z64` (symlink in repo root; user-supplied; never committed).
- **Build dirs:** `build/` (release-ish, `PORT_STRICT=OFF`), `build-asan/` (`-DSANITIZE=ON`).
  Rebuild both before validating: `cmake --build build --parallel 8` etc.
- **Config state:** the working `ge007.ini` (untracked, CWD) was **reset to registered
  defaults** at session end: `Ssao=0` (GL-safe), `RemasterFX=1`, `RenderScale=2`,
  `Bloom/Fxaa/Tonemap=1`, `Saturation=1.15`. **Verify before starting** (`grep -E
  '^(Ssao|RemasterFX|RenderScale)=' ge007.ini`) and **restore after** (§6.2).
- **Screenshots are ROM-derived (Tier B):** never commit them; `rm -f screenshot_*.bmp`
  when done. The contamination guard will catch you if you forget.

---

## 3. The validation matrix

Run in order; V1 gates everything. `[LONG]` rows take minutes. Every capture uses the
deterministic harness. **Pin the faithful config first for V3/V4** (see the row).

| ID | Claim | Command | Expected |
|---|---|---|---|
| **V1** | Tree builds clean, zero warnings | `cmake --build build --parallel 8 2>&1 \| grep -icE 'warning\|error'` | `0`, ends `Built target ge007` |
| **V2** | Every live combiner compiles to MSL, zero failures | `GE007_RENDERER=metal GE007_METAL_DUMP_SHADERS=1 timeout 90 build/ge007 baserom.u.z64 --level dam --deterministic --screenshot-frame 5 --screenshot-exit --background 2>/tmp/d.txt; grep -c '^\[metal\] MSL for' /tmp/d.txt; grep -c FATAL /tmp/d.txt` | 23 shaders (deterministic; re-verified 2026-07-02); `0` FATAL; exit clean |
| **V3** | **Faithful-base GL↔Metal parity** ~2.4–4.0%/level `[LONG, ~10–20 min: 1 pin run + 10 captures, incl. G1 GL retries]` | Pin config: `build/ge007 baserom.u.z64 --level dam --deterministic --screenshot-frame 2 --screenshot-exit --config-override Video.RemasterFX=0 --config-override Video.RenderScale=1 --config-override Video.MSAA=0 --config-override Video.Ssao=0 --background`. Then per level L ∈ {dam, facility, surface, jungle, egyptian}: capture GL (`--screenshot-label g_L`) and Metal (`GE007_RENDERER=metal`, `--screenshot-label m_L`) at `--screenshot-frame 30`, then `python3 tools/compare_screenshots.py screenshot_g_L.bmp screenshot_m_L.bmp --exclude-region 470,72,175,110` | dam ≈4.0%, facility ≈2.8%, surface ≈2.8%, jungle ≈2.8%, egyptian ≈2.7% (±0.3pp). The exclude-region masks the GL-only minimap |
| **V4** | Cross-backend **gameplay invariance** | Both backends: `... --level dam --deterministic --trace-state /tmp/{gl,mtl}.jsonl --screenshot-frame 60 --screenshot-exit --background`, then `python3 tools/compare_state.py /tmp/gl.jsonl /tmp/mtl.jsonl` | `MATCH: 59 frames identical (tolerance=0.1, tri_tolerance=0)` |
| **V5** | Post-FX parity: flat regions byte-exact, most levels ≈2.6–3.0% `[LONG, ~5–10 min: 1 pin run + 4 captures]` | Re-pin config with `Video.RemasterFX=1 Video.Ssao=0 Video.RenderScale=1 Video.MSAA=0 Video.Saturation=1.15 Video.Gamma=1`; capture + compare as V3 on facility/surface | facility & surface ≈2.8%; sample-grid rows in compare output byte-exact or ±1 LSB. (dam/streets ≈11% is EXPECTED — §5 item 2) |
| **V6** | **SSAO renders on Metal with NO op-hang** (the payoff) | `GE007_RENDERER=metal timeout 90 build/ge007 baserom.u.z64 --remaster --level dam --deterministic --screenshot-frame 30 --screenshot-exit --background; echo $?` | `Auto-screenshot complete`, exit `0`, well under timeout |
| **V7** | Metal SSAO ≈ GL SSAO (4.0% on dam, == faithful base) `[LONG, ~5 min: 1 pin run + 2 captures]` | Pin SSAO-only preset (`RemasterFX=1 Ssao=1` + bloom/fxaa/tonemap/vignette/dither OFF, `Saturation=1 Contrast=1 Brightness=0 Gamma=1 RenderScale=1 MSAA=0`; persist it via a throwaway `--config-override` run as in V3); capture GL vs Metal dam frame 30; compare with the minimap exclude | ≈4.0%; both screenshots show contact darkening in rock crevices |
| **V8** | Memory bounded (autorelease-pool fix) | `GE007_RENDERER=metal /usr/bin/time -l build/ge007 baserom.u.z64 --level facility --deterministic --screenshot-frame 60 --screenshot-exit --background 2>&1 \| grep 'maximum resident'` then same with `--screenshot-frame 600` | Peak RSS within ~2% of each other (~140–160 MB); NOT growing 10× |
| **V9** | ASan clean on Metal + SSAO + RDP paths `[LONG, ~5–10 min incl. the ASan rebuild]` | `cmake --build build-asan --parallel 8`, then `ASAN_OPTIONS=detect_leaks=0 GE007_RENDERER=metal timeout 180 build-asan/ge007 baserom.u.z64 --remaster --level surface --deterministic --screenshot-frame 40 --screenshot-exit --background 2>&1 \| grep -iE 'AddressSanitizer\|SUMMARY'` | No output (no findings); run completes |
| **V10** | `--remaster` semantics | (a) `build/ge007 baserom.u.z64 --remaster --dump-config 2>&1 \| grep -E 'active_override=remaster' \| head -3` → non-empty; (b) `--faithful --remaster` → exits 2 with "mutually exclusive"; (c) `--remaster --config-set Video.FovY=77` → prints "read-only session … NOT written"; afterwards `grep '^FovY=' ge007.ini` unchanged; (d) startup log says "enabled 17 remaster setting(s) … native Metal backend" | all four hold |
| **V11** | GL `ssao_on` lockstep change is default-neutral | `git show 197fc03 -- src/platform/fast3d/gfx_opengl.c` shows a single hunk (`@@ -3198,7 +3198,11 @@`): the `g_pcRemasterFX` term added to `ssao_on` plus a 4-line explanatory comment — no other code change in that file; with default `Ssao=0` GL output is unchanged (any V3 GL capture pre/post this commit is identical) | as stated |
| **V12** | `gfx_metal.mm` strict-clean | `cmake -S . -B /tmp/bs -DPORT_STRICT=ON && cmake --build /tmp/bs --parallel 8 2>&1 \| grep -c 'gfx_metal'` (then `rm -rf /tmp/bs`) | `0` gfx_metal lines. **The full strict build FAILS on pre-existing** `ALBank`/`ModelNode` pointer-type errors in `chrai.c`/`chrobjhandler.c`/`chrlv.c`/`chrprop.c` — that predates this session (W8 doc owns the fix) |
| **V13** | AAA plan set integrity | `ls docs/remaster-aaa/` → 9 files (00–08); `for f in docs/remaster-aaa/0[1-8]*.md; do grep -cE '^## [0-9]\.' $f; done` → `9` each; every doc has task tables with acceptance commands + junior-day estimates | as stated |
| **V14** | Campaign-wide no-hang `[LONG, ~20 min]` | Loop the 18 solo levels (`dam facility runway surface bunker silo frigate statue archives streets depot train jungle control caverns cradle aztec egyptian`) with `GE007_RENDERER=metal timeout 120 … --remaster --level $lvl --deterministic --screenshot-frame 25 --screenshot-exit --background`, count `Auto-screenshot complete` | 18/18 |
| **V15** | Contamination guard green / no tracked images | `bash scripts/ci/check_no_rom_data.sh` (or make any commit — the hook runs it) | OK |

**Also expected but session-verified only once:** split-screen GL↔Metal 0.0% (2 px) —
`--multiplayer --players 2` captures, same compare; RDP-heavy 300-frame run exit 0.

---

## 4. Claims about *why* (verify the reasoning, not just the outputs)

These are the load-bearing technical judgments a validator should independently confirm:

- **P1 — The "depth-convention crux" refutation.** A planning workflow claimed Metal clip-Z
  needed a `(z+w)/2` remap in the scene VS (would break depth). REFUTED: the frontend already
  remaps — read `src/platform/fast3d/gfx_pc.c` ~18395: `if (z_is_from_0_to_1) z = (z + w) / 2.0f;`
  and `mtl_z_is_from_0_to_1()` returns true. Consequence: `ssaoLinZ` (with its `2d-1`) is
  reused verbatim in the Metal filter shader. If you find a scene-VS z remap in
  `gfx_metal.mm`'s `mtl_generate_msl` vertex body — that's a bug someone added later.
- **P2 — GL's filter really is 2-pass with double gamma/scale/bias.** Read
  `gfx_opengl.c` `gfx_opengl_apply_output_vi_filter` (~3250+): the pre-pass draw (~3401–3413,
  `apply_post=0`) and final (~3435–3438), and that `uColorScale/uColorBias` (~3046) and gamma
  (~3065) sit OUTSIDE the `if (uApplyPost==1)` guard in the fragment source. Metal's
  `mtl_run_output_filter` therefore runs two passes through `s_filter_low`. At `Gamma=1`
  the pre-pass is an identity 8-bit copy (that's why V5 stays byte-exact).
- **P3 — The `--remaster` persistence hazard was real.** `git show 197fc03 --
  src/platform/main_pc.c`: the fix adds `configSetSaveSuppressed(1)` to the remaster block.
  Without it, `--remaster --config-set X` persisted `Ssao=1` → a later plain GL launch on
  macOS re-triggers the SSAO op-hang. Verify the current behavior via V10(c).
- **P4 — The AAA plan docs were adversarially verified.** Each of docs 01–08 was
  fact-checked by an independent agent (12–46 anchors re-read per doc; errors fixed in
  place). Spot-check protocol: pick 5 random `file:line` anchors per doc and `Read` them —
  budget ≤1 wrong-by-more-than-a-few-lines per doc (anchors are function-level and drift).
  Highest-value spot checks: W4's claim that shoot-out-the-lights is fully implemented
  (`src/game/lightfixture.c:110-498`, population `src/game/bg.c:301-377`, and the claimed
  population-gap bug at `bg.c:9889`); W6's claim that `Audio.MasterVolume` doesn't touch the
  main synth output; W1's claim that SHADER_OPT bits 5,6,7,29,30,31 are free (`gfx_cc.h`).

## 5. Known-accepted deviations — do NOT report these as findings

1. **GL-only minimap** absent on Metal (guarded off — direct-GL overlay outside the vtable).
   Always compare with `--exclude-region 470,72,175,110`.
2. **dam/streets ≈11%, jungle ≈5.7% at post-FX presets** — the color grade amplifies the
   faithful base's sub-threshold rasterizer diffs on high-contrast content. Flat-region
   sample grids are byte-exact; that is the acceptance signal, not raw changed-%.
3. **`Gamma≠1` → ≈9% GL↔Metal** — cross-backend 8-bit-intermediate rounding amplified by the
   double-gamma. Non-default setting; accepted tolerance.
4. **DEC decal depth-bias** is a fixed constant on Metal vs GL's depth-adaptive
   `glPolygonOffset` units (slope term matches). Accepted, documented.
5. **Filter `filterMode` 1/2/3** (VI-downscale sampling) have a documented bottom-left
   orientation caveat and are **unreachable** (hardcoded mode 0). Deferred with the
   VI-downscale feature; a code comment marks the required remap.
6. **statue/depot faithful-base ≈11–15%** — grazing-angle tiled-grass filter sample-pattern
   difference on dark scenes; visually at parity (verified by eye + gross metrics).
7. **PORT_STRICT full-tree failure** — pre-existing `ALBank`/`ModelNode` errors (V12 note).

## 6. Gotchas that WILL waste your time

- **G1 — GL intermittently hangs on this machine.** Apple's GL-over-Metal translator flakes
  (worse at `RenderScale=2` / high frame counts). A GL timeout is NOT a regression: retry
  up to 3× at frame ≤30, `RenderScale=1`. Metal never hangs — that asymmetry is the
  project's thesis, live.
- **G2 — `--config-override` and `--config-set` PERSIST to `ge007.ini`.** Every parity
  test mutates the config. Pin explicitly before each suite; **restore defaults after**
  (`build/ge007 baserom.u.z64 --reset-config`, then re-check `Ssao=0`). The authoring
  session's biggest self-inflicted confusion was config drift between captures.
- **G3 — parity requires matched resolution.** GL screenshots the downsampled window;
  Metal reads its full-res offscreen. Always pin `Video.RenderScale=1` + `Video.MSAA=0`
  on BOTH sides, or SSAA mismatch inflates diffs to ~50%+.
- **G4 — `--ramrom` renders black headless** — use `--level` for screenshots (RAMROM is for
  the sim-hash gate only).
- **G5 — screenshots land in CWD as `screenshot_<label>.bmp`** (ROM-derived; delete them).
- **G6 — `GE007_RENDERER` is env/flag-only** (not a persistable config key) — Metal must be
  selected per-launch (`--remaster` does it on macOS).

## 7. Open / deferred items (the honest debt list)

| Item | Where tracked |
|---|---|
| VI-downscale filter 2-pass (`GE007_DIAG_OUTPUT_VI_FILTER`) + filterMode orientation remap | code comment in `mtl_fill_filter_uniforms`; W3 doc |
| SSAO-under-MSAA on Metal (depth resolve — GL can't) | W3 doc §SSAO |
| PORT_STRICT tree cleanup (pre-existing) | W8 doc |
| Metal-default-on-macOS flip decision | W8 doc §2.1 |
| Branch merge-train (5 branches → `main`) — **program task #1** | W8.E0 |
| Shoot-out-the-lights verification + `bg.c:9889` population fix | W4.E1 |
| `Audio.MasterVolume` main-output bug | W6 doc |
| Netplay (lockstep architecture supersedes old plan) | W7 doc |

## 8. Suggested validation order & sign-off

1. **V1, V13, V15** (fast, no ROM run) → environment sane.
2. **V2, V6, V10** (fast runs) → the headline claims.
3. **V4, V8, V11, V12** (medium) → invariance, memory, lockstep-diff, hygiene.
4. **V3, V5, V7** (capture suites — mind G2/G3) → parity numbers.
5. **V9, V14** (long) → ASan + campaign sweep.
6. **P1–P4** (code reading) → the reasoning.
7. Restore config (G2), delete screenshots (G5), report: matrix results table +
   any P-item disagreement + AAA-doc anchor spot-check error rate.

A validator who completes 1–7 with all-green (allowing §5's accepted deviations and §6's
retries) has independently confirmed everything this session claimed.
