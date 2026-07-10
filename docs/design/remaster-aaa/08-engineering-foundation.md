# W8 — Engineering Foundation: Metal-default, Packaging, CI, Distribution

*Workstream 8 of the MGB64 AAA Remaster program. Base branch: `main` (merge train complete
2026-07-03, tag `v0.2.0-rc1`; originally authored against `feat/metal-backend`).
Constitution: [docs/design/REMASTER_ROADMAP.md](../REMASTER_ROADMAP.md) — rails R1 (gameplay-invariant,
§1a), R2 (copyright-bulletproof, §1b), R3 (opt-in/default-identity, §1c) are cited per-task below.*

> **Status (2026-07-03):** ✅ **E6.T2 merge train DONE** (`main` consolidated, tag `v0.2.0-rc1`;
> §2.5 branch table + §4.7 train mechanics are historical record). E6.T1 (VERSION/CHANGELOG)
> still open. **Accepted deviation:** GitHub Actions is deliberately **repo-disabled** — the
> local §8 validation ladder is the binding merge gate. Every acceptance criterion below that
> says "PR CI shows the job" / "Dispatch run produces X" (E2.T2, E3.T1, E3.T5) must be read as
> its local-ladder equivalent (local strict build / MSL ctest / local package script) unless an
> explicit CI-re-enable decision is taken (§9 Q2). Frame pacing was rewritten post-authoring
> (sub-ms deadline pacer; `Video.VSync` → `CAMetalLayer.displaySyncEnabled`) — do not
> reintroduce integer-ms sleeps.

---

## 1. Executive summary

Every other workstream in this program ships *content* (shaders, textures, lighting, audio); W8
ships the **machine that lets them ship safely**: the decision path and gates to make native Metal
the macOS default (the GL-over-Metal translator it replaces intermittently hangs *today* on the
maintainer's M3 Max — see §2.1 evidence trail), a signed/notarized macOS `.app`, a CI matrix with a
real macOS lane and a `PORT_STRICT`-clean tree so `-Werror` can gate merges, a Metal perf-census
lane with regression budgets, crash/diagnostics capture, a versioned release process that binds
every validation gate, and the merge train that consolidates five in-flight branches onto `main`.
Payoff: the program stops being "one engineer's working tree" and becomes a releasable product with
enforced invariants.

| # | Headline deliverable | Observable result |
|---|---|---|
| 1 | Branch consolidation | `main` contains Metal backend + perf + robustness work; stale branches deleted; every merge step validated |
| 2 | Metal-default-on-macOS | Fresh macOS run boots native Metal with no env vars; `GE007_RENDERER=gl` opt-out; 7 parity gates archived as evidence |
| 3 | CI expansion + PORT_STRICT clean | PR CI runs Linux + macOS lanes incl. ROM-free MSL compile test; `PORT_STRICT=ON` builds with **0 errors** (154 today) and gates CI |
| 4 | Signed `MGB64.app` + diagnostics | Notarized DMG installs on a clean Mac; first-launch ROM picker; `--diagnostics-bundle` produces a shareable, asset-free report |
| 5 | Release v0.2.0 process | `VERSION` file + CHANGELOG + release checklist that runs *all* gates; binaries + pipelines shipped, never assets |

---

## 2. Current state (verified on `feat/metal-backend`, 2026-07-02)

### 2.1 Renderer selection & the GL-over-Metal hang — the evidence trail

- **Backend selector**: `gfx_backend_use_metal()` in `src/platform/fast3d/gfx_backend.c:12-23` —
  returns true only when `GE007_RENDERER=metal` (or `Metal`) is set, `__APPLE__`-guarded, result
  cached on first call. **There is no `Video.*` config key for the renderer today** — env only.
- **`--remaster` already forces Metal on macOS**: `src/platform/main_pc.c:556-561` does
  `setenv("GE007_RENDERER", "metal", 1)` before gfx/SDL init, because the preset enables SSAO
  (`s_remasterPreset[]`, `src/platform/platform_sdl.c:1960-1977`: `Video.Ssao=1`, `RenderScale=2`).
- **`--faithful` preset**: `s_faithfulPreset[]` at `src/platform/platform_sdl.c:1917-1932` pins
  identity values (`Video.RemasterFX=0`, `RenderScale=1`, `MSAA=0`, `TexturePack=`, FOV 60, modern
  input helpers off). It does **not** touch the renderer — today it runs on whatever backend the
  env selects (GL by default).
- **The Metal backend**: `src/platform/fast3d/gfx_metal.mm` (1,946 lines, Obj-C++/ARC — ARC set at
  `CMakeLists.txt:325-327`) implements the full `GfxRenderingAPI` vtable, combiner→MSL translator,
  offscreen BGRA8 + `Depth32Float` scene, RDP/XLU snapshot, 2-pass output-VI-filter, native-depth
  SSAO. GL remains default and byte-identical (verified by `cmp`, `docs/design/METAL_BACKEND_PLAN.md:9`).
- **Hang evidence trail** (all documented, all reproduced on M3 Max / macOS 15+):
  1. *SSAO op-hang*: reference SSAO reconstruction math (proj-divide + cross/normalize) hangs
     Apple's deprecated GL-over-Metal translator at the **op level** — hang persists even with the
     source bound as a *color* texture (`GE007_SSAO_COLORDEPTH` isolation, exit 137 after ~45 s
     force-kill; GPU needs ~4 min to reset). Recorded in `docs/design/METAL_BACKEND_PLAN.md` §0
     ("TESTED 2026-07-01 → RULED OUT") and the robustness branch commits `dc95a24`/`7467b8f`.
  2. *Intermittent whole-path hangs*: "on this M3 Max the GL path now frequently HANGS (the flaky
     GL-over-Metal translator) while native Metal runs clean + deterministic every time"
     (`docs/design/METAL_BACKEND_PLAN.md`, Phase-1 field note).
  3. *Metal parity evidence*: 18-level GL-vs-Metal sweep at matched faithful config — 16 levels at
     2.4–4.0% changed-px, split-screen 0.0%, statue/depot 11–15% but visually at parity (sample-
     pattern delta on grazing-angle tiled grass); **cross-backend `compare_state.py` MATCH, 59
     frames identical (tri_tolerance=0)**; 18/18 levels boot `--remaster` with zero hangs
     (`docs/design/METAL_BACKEND_PLAN.md:9` and Phase-5 bullet).
- **Cross-backend sim identity is already proven once**: Spike A — GL and Metal produce the
  identical sim-state hash `5c2983a3f0b7345f` for the same deterministic run
  (`docs/design/METAL_BACKEND_PLAN.md`, Phase-1 Spike A).

### 2.2 Packaging

- A full macOS app shell **already exists** under `macos/`: Swift sources (`AppDelegate.swift`,
  `ROMPickerView.swift`, `OnboardingFlow.swift`, `PerformanceOverlay.swift`, `GameBridge.c/h`),
  `Resources/Info.plist` (`CFBundleShortVersionString` = `0.1.0`, `Info.plist:15-16`),
  `Entitlements.plist`, icon generator (`macos/Scripts/generate_app_icon.py`), and scripts:
  `build_universal.sh`, `build_app_bundle.sh` (371 lines), `sign_and_notarize.sh` (257 lines),
  `create_dmg.sh` (171 lines), `verify_asset_free.sh`.
- CMake side: `option(MACOS_APP_BUNDLE ...)` at `CMakeLists.txt:531` builds the game as a static
  lib (`ge007_lib`) for the Swift shell, universal `arm64;x86_64`, deployment target 13.0
  (`CMakeLists.txt:536-541`). When bundled, `main()` is excluded — the Swift AppDelegate drives
  `game_init()/game_run()` via `GameBridge.h` (`src/platform/main_pc.c:506-508`).
- CI: `.github/workflows/macos-release.yml` (manual `workflow_dispatch`, `macos-14` runner) builds
  the universal lib + an **unsigned** local `.app` and verifies both asset-free; the
  sign/notarize/DMG/upload job is **commented out** pending Developer ID credentials.
- Linux: builds via the generic branch (`CMakeLists.txt:29-32`, `find_package(OpenGL/SDL2)`); no
  AppImage/flatpak packaging exists.
- Windows: `CMakeLists.txt:27-28` and `:514-518` have `WIN32` branches (find + link OpenGL/SDL2),
  and `_WIN32` code paths exist (`src/platform/config_pc.c:18-19,433,509`,
  `src/platform/port_trace.c:29,1800-1832`) — but the flag set at `CMakeLists.txt:444-463` is
  GCC/Clang-style (`-Wall`, `-fms-extensions` at `:462`), so **MSVC cannot build this tree**; a
  clang-based toolchain (MSYS2 clang64) is the only plausible route. Never CI-built, never tested.

### 2.3 CI today

`.github/workflows/ci.yml` runs on `pull_request` + `workflow_dispatch` with two jobs, both
`ubuntu-latest`:
1. **Release hygiene** — `check_release_ready.sh`, `check_high_risk_ignored_artifacts.sh`,
   `check_timing_lock.sh` (R2 of the P0 rails), Docker context check, `validate_quick.sh`,
   `py_compile` of ~30 tools + 4 regression checkers.
2. **linux-build** — CMake Release build with `--max-total 0` warnings
   (`tools/summarize_build_warnings.py`), `check_sim_render_separation.sh` (R1), ROM-free
   `ctest`, public source-archive smoke.

**There is no macOS lane in PR CI** — the Metal backend TU (`gfx_metal.mm`) is currently compiled
only on the maintainer's machine and in the manual `macos-release.yml` dispatch. ROM-gated lanes
are deliberately local (`docs/RELEASE_CHECKLIST.md:60-71`; `tools/sim_invariance_gate.sh:22-23`:
"Requires a ROM, so this is a LOCAL gate; CI runs the ROM-free hash unit test").

### 2.4 PORT_STRICT — measured inventory (fresh `-DPORT_STRICT=ON` build, this session)

`option(PORT_STRICT ...)` at `CMakeLists.txt:433`; promotes 8 pointer-safety warnings to `-Werror`
(`CMakeLists.txt:483-494`) plus `-Werror=deprecated-declarations` on APPLE (`:496-498`). A full
`make -k` strict build produces **154 errors in 16 files** (note: `docs/design/METAL_BACKEND_PLAN.md:9`'s
"Zero warnings under PORT_STRICT" claim was scoped to the Metal backend surface; the tree as a
whole does NOT pass — code wins):

| Class | Count | Sites | Root cause |
|---|---|---|---|
| `ALBank *` → `struct ALBankAlt_s *` at `sndPlaySfx` call sites | **144** | chrobjhandler.c (91), front.c (23), gun.c (13), watch.c (5), lvl.c (4), chrai.c:382,389, title.c, stubs.c, mp_watch.c, chrprop.c:4884, chrlv.c:7114, unk_0A1DA0.c:192 | `sndPlaySfx` is declared `(struct ALBankAlt_s *)` at `src/snd.h:200` (the `PORT_SND_STUBS` variant at `:193` takes `void *`); callers pass `g_musicSfxBufferPtr`, declared `ALBank *` at `src/music.h:101` and assigned from `sfxBank->bankArray[0]` at `src/music.c:921` |
| `struct ModelRoData_GroupRecord *` → `ModelNode *` | 2 | chrobjhandler.c:980,988 | `monitorFindDrawableNode` recursion passes `rodata->Header.FirstGroup` / `rodata->Group.ChildGroup` (decomp union punning) |
| Implicit function declarations | 3 | image_bank.c:204 (`texLoadFromDisplayList`), lightfixture.c:440 (`getRoomPositionScaledByIndex`), prop.c:3331 (`sub_GAME_7F0B9E04`) | missing prototypes |
| Mixed pointer types | 3 | unk_0A1DA0.c:1260 (`f32*`→`coord3d*`), :1331 ×2 (`Mtxf*`→`f32(*)[4]`/`s32(*)[4]`) | decomp matrix-type punning |
| `volatile uintptr_t` → `const void *` | 2 | src/platform/fast3d/gfx_pc.c:18235,18262 | diag globals passed to pointer params |

### 2.5 Perf, crash handling, release process, branches

- **Perf harness**: `tools/perf_census.sh` (76 lines) — deterministic headless per-level census via
  `GE007_PERF_TRACE=1` (read at `src/platform/platform_sdl.c:142-144`; `work_ms` printed at
  `:2596-2601`); CSV `level,default_ms,default_fps,xluoff_ms,speedup` to
  `baselines/perf_census_latest.csv`. `tools/perf_budget_check.py` enforces HARD 16.6 ms / TARGET
  8.3 ms with `--baseline` regression flagging. **The census does not know about backends** —
  no Metal column. No in-game frame-time HUD exists (the Swift shell's `PerformanceOverlay.swift`
  is bundle-only; the game binary has none).
- **Crash handler**: `crashHandler` at `src/platform/main_pc.c:315`, installed at `:529-537` unless
  `GE007_NO_CRASH_HANDLER` set. Two tiers: (a) GFX-recovery — if `g_gfxRecoveryActive`, writes an
  async-signal-safe `[GFX-RECOVER]` diag line (last DL opcode/tex state) and `siglongjmp`s to skip
  the frame (cap 10 in Release via `NDEBUG`, `main_pc.c:326-330`); (b) fatal — writes a diag block
  to **stderr only**. Nothing is persisted to disk; no diagnostics bundle exists.
- **Release process**: `docs/RELEASE_CHECKLIST.md` + `scripts/release_preflight.sh` (source
  hygiene, ROM-backed runtime lanes, archive smoke). No `VERSION` file, no `CHANGELOG.md`, no git
  tags; the bundle hardcodes `0.1.0` (`macos/Resources/Info.plist:15-16`).
- **Branch state** (re-measured 2026-07-02 at metal HEAD `daf711e`; `git rev-list --left-right
  --count origin/main...BRANCH`, `origin/main` = `c3e3eec`):

| Branch | vs origin/main | Pushed? | Contents / status |
|---|---|---|---|
| `feat/dam-hd-remaster` | 0 ahead / 0 behind | yes | **Already merged** — `origin/main` == its tip `c3e3eec` (the watch-ammo/portal decomp fixes are in) |
| `feat/split-screen-multiplayer` | 0 ahead / 51 behind | remote exists but stale (`3dd3c23`, itself ⊂ origin/main) | **Already merged**, stale — delete local **and** remote |
| `perf/make-it-rip` | 5 ahead (`83fb90f`) | **no — local only** | **Ancestor of** `robustness/remaster-hardening` (verified `merge-base --is-ancestor`) |
| `robustness/remaster-hardening` | 25 ahead (`9336d49`) | **no — local only** | **Ancestor of** `feat/metal-backend`. All 7 hardening items are committed in-range: item 1 `df2f1de`, items 2–6 `2a22ea2`, item 7 `a873ec4`+`edd1254`, plus monitor-overflow fix `5ef1ec5` |
| `feat/metal-backend` | **50 ahead / 0 behind** (`daf711e`; drifts as work lands — re-measure) | **no — local only** | Superset: origin/main ⊂ perf ⊂ robustness ⊂ metal. Mergeable without conflicts by construction |
| `fix/train-distance-fog` | 1 ahead / 0 behind (`3c69924`; +22 lines, `src/platform/fast3d/gfx_pc.c` only) | **no — local only** | Independent, default-OFF concealment flag. `git merge-tree --write-tree feat/metal-backend fix/train-distance-fog` = clean, no conflicts. **Checked out in worktree** `.claude/worktrees/agent-af7d6a1b5553877cd` — see §4.7 step 0 |
| local `main` | 86 behind origin/main | — | Stale — needs `git pull --ff-only` |

**Consequence**: "five branches to consolidate" is really **one linear merge train (metal tip) +
one cherry (train-fog) + housekeeping** — and since none of the train branches has ever been
pushed, the train begins with `git push -u origin <branch>` (§4.7 step 0b).

---

## 3. Target state — the AAA bar

What a player/reviewer observes when W8 is done:

1. **Download → play in 3 clicks**: reviewer downloads `MGB64-0.2.0.dmg` from a GitHub release,
   drags `MGB64.app` to Applications, launches — Gatekeeper shows a normal notarized-app prompt
   (no right-click bypass). First launch shows the onboarding ROM picker with a plain-language
   legality note ("supply your own GoldenEye 007 (U) ROM; this app contains no game assets").
   The picked ROM is remembered; the game boots **native Metal by default** — no env vars, no
   terminal.
2. **No hangs**: 20/20 levels, faithful and `--remaster`, run indefinitely with zero GPU hangs on
   Apple Silicon. `GE007_RENDERER=gl` still works as a documented escape hatch.
3. **Every PR is machine-gated**: Linux build + macOS build (Metal TU compiled, MSL combiner corpus
   compiles on a real `MTLDevice`), zero warnings, `PORT_STRICT=ON` zero errors, R1/R2 rails
   scripts, ROM-free ctest — all green before merge. ROM-gated evidence (parity sweep, sim-hash,
   perf census) is produced locally by one command and attached to the release record.
4. **A crash is a report, not a shrug**: any crash writes a local crash log;
   `ge007 --diagnostics-bundle` (or the app's Help menu) produces a single asset-free `.tar.gz` a
   user can attach to an issue.
5. **Perf is a contract**: `main` carries committed census baselines for both backends; a PR that
   regresses a level >10% vs baseline fails the local perf gate; players can toggle a frame-time
   HUD (`Video.PerfHud=1`) on both backends.

---

## 4. Technical design

### 4.1 Metal-default-on-macOS — decision path

**Decision (justified): the "faithfulness anchor" and the "macOS default" are split.**

- **GL remains the reference implementation and the byte-identity anchor** (R3: "all-off =
  byte-identical faithful port", `REMASTER_ROADMAP.md` §1c). Byte-identity is a property we can
  only assert on the pipeline it was proven on; GL is also the only cross-platform backend
  (Linux/Windows). All identity regression lanes (`cmp`-level screenshot checks, Linux CI) stay GL.
- **The macOS *runtime default* — including `--faithful` — flips to Metal** once the G-gates below
  pass. Rationale: a renderer that intermittently hangs the GPU (§2.1 items 1–2) cannot anchor
  faithfulness *in practice*; R3's purpose is protecting gameplay and visual identity, and on Metal
  gameplay identity is **exact** (sim-hash equal, §2.1 item 4) while visual identity is bounded by
  a measured, per-level budget (§2.1 item 3). So: **`--faithful` under Metal = Metal-with-identity-
  flags + parity budget**, with `GE007_RENDERER=gl` retained indefinitely as the opt-out and as the
  lane that continues to *prove* the byte-identity contract.
- The alternative (faithful stays GL) was rejected: it would make the most conservative mode the
  most crash-prone one on the flagship platform, and Apple can remove the deprecated translator in
  any macOS release.

**Selection precedence** (new, replaces env-only `gfx_backend.c:12-23`):

```c
/* gfx_backend.c — new resolution order, cached once:
 * 1. GE007_RENDERER env ("gl"|"metal")           — absolute override (scripts, A/B)
 * 2. Video.Renderer config key ("gl"|"metal"|"auto", default "auto")
 * 3. "auto": __APPLE__ ? metal : gl              — THE FLIP (Stage D below)
 * Before the flip, "auto" == gl everywhere. */
bool gfx_backend_use_metal(void);
const char *gfx_backend_name(void);   /* "gl" | "metal" — for logs, perf CSV, crash reports */
```

`Video.Renderer` registers next to the other `Video.*` keys in `platform_sdl.c` (same
`settingsRegister*` pattern as `Video.RemasterFX`); read **before** `SDL_CreateWindow` (GL vs
Metal window flags are mutually exclusive — `docs/design/METAL_BACKEND_PLAN.md` §2.1).

**Gates that must hold before flipping "auto" to Metal (G1–G7)** — each produces an artifact
archived as committed evidence (text/JSON/CSV only — no images in git,
R2):

| Gate | Criterion | Command (see §8 for env preamble) |
|---|---|---|
| G1 sim identity | Identical `--sim-state-hash-out` GL vs Metal on ≥3 RAMROM replays (`dam1`, `facility3`, `runway1` — alias table at `src/game/ramromreplay.c:209-215`) × 600 frames | `tools/backend_invariance_gate.sh` (new, §4.1.1) |
| G2 visual parity | 20-level sweep, faithful config: changed-px ≤ 4.2% per level; documented waivers (statue/depot ≤ 15% w/ side-by-side sign-off) | `tools/metal_parity_sweep.sh` (new) → `compare_screenshots.py --max-changed-pct` |
| G3 stability soak | 20 levels × 1800 frames × faithful AND `--remaster`, exit 0, zero `[GFX-RECOVER]` lines | soak mode of `tools/metal_parity_sweep.sh` |
| G4 sanitizer | ASan+UBSan run (`-DSANITIZE=ON`, `CMakeLists.txt:477-481`) on 3 representative levels, Metal | `tools/asan_smoke.sh` with `GE007_RENDERER=metal` |
| G5 perf | Metal census: every level ≥ 60 fps hard budget and within 1.10× of GL `work_ms` | `tools/perf_census.sh` + `perf_budget_check.py --baseline` (§4.4) |
| G6 crash path | `crashHandler` GFX-recovery verified functional under Metal (fault-injection env `GE007_DIAG_FAULT_INJECT`, new) | manual + soak log audit |
| G7 opt-out | `GE007_RENDERER=gl` and `Video.Renderer=gl` both force GL (asserted in log line `[GFX] backend=`) | smoke in `tools/metal_parity_sweep.sh` |

**Staged rollout**:
- **Stage A (shipped)**: opt-in `GE007_RENDERER=metal`.
- **Stage B (shipped)**: `--remaster` implies Metal (`main_pc.c:556-561`).
- **Stage C**: `Video.Renderer` key lands, default `auto`==gl; announce; one soak week where the
  maintainer + testers run `Video.Renderer=metal` daily-driver.
- **Stage D (the flip)**: `auto`⇒metal on macOS. One commit, one line, trivially revertable. The
  commit message links the G1–G7 evidence files.
- **Stage E**: `--faithful` documentation updated (VISUAL_MODES.md §1) to state the two-anchor
  model explicitly; GL identity lane stays in the release checklist forever.

#### 4.1.1 New harness: `tools/backend_invariance_gate.sh`

Clone of `tools/sim_invariance_gate.sh` (same env preamble, `sim_invariance_gate.sh:38-50`) but the
A/B axis is `GE007_RENDERER=gl` vs `metal` at **identical** identity config, comparing
`--sim-state-hash-out` JSON. ~60 lines, junior-friendly. Exit 1 on hash mismatch with a pointer to
`tools/compare_state.py` for localization.

### 4.2 Packaging

#### macOS (finish what exists — do not rebuild)

1. **Codesign + notarization**: `macos/Scripts/sign_and_notarize.sh` already exists (257 lines).
   Work = wire real credentials via environment (`DEVELOPER_ID_APPLICATION`, `APPLE_ID`,
   `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD` — the exact vars the commented-out job in
   `macos-release.yml` expects), run end-to-end locally, then uncomment/enable the `package` job in
   `.github/workflows/macos-release.yml` with `permissions: contents: write` scoped to that job
   only (the workflow's own NOTE). `xcrun notarytool submit --wait` + `xcrun stapler staple`.
   Hardened runtime entitlements already in `macos/Resources/Entitlements.plist`.
   Exact local sequence (placeholders ONLY for identity/credentials; **prerequisite: Apple
   Developer Program membership + a "Developer ID Application" cert in the login keychain — user
   action, §9 Q1**):

   ```sh
   export DEVELOPER_ID_APPLICATION="Developer ID Application: <Name> (<TEAMID>)"
   export APPLE_ID="<apple-id-email>"
   export APPLE_TEAM_ID="<TEAMID>"                    # 10-character team id
   export APPLE_APP_PASSWORD="@keychain:<label>"      # app-specific password (or literal)
   ./macos/Scripts/build_app_bundle.sh --release --build-dir build-macos-app \
       --output build-macos-app/MGB64.app
   ./macos/Scripts/sign_and_notarize.sh build-macos-app/MGB64.app \
       # runs codesign → notarytool submit --wait → stapler staple internally
   spctl --assess --type execute -vv build-macos-app/MGB64.app
   # expected: "accepted" + "source=Notarized Developer ID"
   ```

   The script's usage + required env vars are its documented interface
   (`sign_and_notarize.sh:7-13`); `--skip-notarize` exists for sign-only iteration.
2. **Metal-in-bundle verification**: the bundle builds `ge007_lib` from the same `FAST3D_SOURCES`
   list that appends `gfx_metal.mm` on APPLE (`CMakeLists.txt:322-327`), so Metal is compiled in;
   add a bundle smoke that launches the app with `GE007_RENDERER=metal` + `--screenshot-frame` via
   the existing `GameBridge` arg path and asserts exit 0.
3. **ROM-selection UX**: `ROMPickerView.swift` + `OnboardingFlow.swift` exist. Work = verify the
   flow against a *missing* ROM (first launch), a *wrong* ROM (bad header — reuse the ROM
   validation from the robustness branch), and add the legality copy: *"MGB64 contains no
   Nintendo/Rare assets. You must provide your own legally-obtained GoldenEye 007 (U) ROM
   (`baserom.u.z64`). The ROM never leaves your machine."* (R2 restated in UI.)
4. **Versioning plumb-through**: `build_app_bundle.sh` templates `Info.plist` from the `VERSION`
   file (§4.6) instead of the hardcoded `0.1.0`.

#### Linux — AppImage first, flatpak sketch

- **AppImage** (chosen for v0.2.0: single file, no store review, matches "ship binaries + never
  assets"): CMake `install()` rules for `ge007` + a new `packaging/linux/` dir with
  `mgb64.desktop`, an **original** icon (Tier A1 — reuse the macOS icon source from
  `generate_app_icon.py`, which is generated art), and an AppRun that execs `ge007`. Build with
  `linuxdeploy --plugin appimage`, bundling SDL2; GL comes from the system. First-run ROM UX on
  Linux = CLI `--rom` plus a zenity/kdialog fallback prompt in AppRun when no config exists
  (10-line shell; no new UI code).
- **flatpak (sketch only, post-v0.2.0)**: `org.freedesktop.Platform 24.08`, `--filesystem=home:ro`
  is *not* acceptable — use the file-chooser portal so the sandbox never gets blanket FS access;
  manifest builds from the public source archive (`scripts/make_public_source_archive.sh`). Blocked
  on AppImage learnings; do not start until M4.

#### Windows — assessment verdict

Ships **no Windows binary this program**. Evidence: WIN32 CMake branches exist
(`CMakeLists.txt:27-28,514-518`) and `_WIN32` guards exist in platform code
(`config_pc.c:18,433,509`, `port_trace.c:29,1800+`), but the compile flags are Clang/GCC-only
(`-fms-extensions`, `CMakeLists.txt:462`; the whole `-W` block `:444-463`), so MSVC is out, and the
tree has never been built on any Windows toolchain. W8 includes one **timeboxed (5-day) MSYS2
clang64 build attempt** (T8.2.5) whose deliverable is a *report* (what links, what's missing —
likely audio backend + timer + `mmap` shims), not a port. Kill criterion: if >2 subsystem shims are
needed, file as a future workstream and stop.

### 4.3 CI expansion

**Design principle — the ROM split** (already the house posture, `sim_invariance_gate.sh:22-23`):
hosted CI proves everything provable without assets; ROM-gated lanes run locally via
`scripts/release_preflight.sh` and their *evidence* (hashes, CSVs, percentages — never images) is
committed to the repo.

New/changed jobs in `.github/workflows/ci.yml`:

1. **`macos-build` (new, PR-blocking)** — `runs-on: macos-14`; `brew install sdl2 cmake`;
   Release build (compiles `gfx_metal.mm` + ObjC++/ARC + frameworks, `CMakeLists.txt:7-10,
   322-327,500-509`); zero-warning gate via `summarize_build_warnings.py --max-total 0`; ROM-free
   `ctest` (includes `macos_app_asset_free_verifier`, `CMakeLists.txt:178-188`, and
   `sim_state_hash`); universal-lib bundle smoke (`macos/Scripts/build_universal.sh`, reusing the
   `macos-release.yml` steps). ~12 min. Job skeleton (keep the SHA-pinned checkout style of the
   existing `ci.yml` jobs):

   ```yaml
   macos-build:
     name: CMake build (macOS + Metal TU)
     runs-on: macos-14
     timeout-minutes: 30
     steps:
       - uses: actions/checkout@34e114876b0b11c390a56381ad16ebd13914f8d5 # v4
       - run: brew install sdl2 cmake pkg-config
       - run: cmake -B build -DCMAKE_BUILD_TYPE=Release
       - run: |
           set -o pipefail
           cmake --build build --parallel 4 2>&1 | tee build/mgb64-build.log
       - run: python3 tools/summarize_build_warnings.py build/mgb64-build.log --max-total 0
       - run: ctest --test-dir build --output-on-failure
       - run: ./macos/Scripts/build_universal.sh --release --build-dir build-macos-universal
       - run: ./macos/Scripts/verify_asset_free.sh build-macos-universal/libge007_lib.a
   ```
2. **`msl-combiner-compile` (new ctest inside macos-build)** — the key ROM-free Metal *runtime*
   test. GitHub `macos-14` runners expose a real Metal device (Apple Silicon VM); we do not render
   the game (needs ROM), we compile shaders:
   - New test hook in `gfx_metal.mm`: `int gfx_metal_test_compile_combiners(const uint64_t *ids,
     int n, char *err, size_t errlen);` — runs the existing MSL string-builder per id and
     `newLibraryWithSource` on the default device.
   - New file `tests/data/combiner_corpus.txt`: the ~40 distinct combiner ids observed across Dam/
     Surface/Jungle sweeps (64-bit RDP combine-mode encodings — configuration words, not ROM asset
     data; R2-safe. `check_no_rom_data.sh` blocks by extension — images/audio/blobs *including
     `.jsonl`* — plus >3 MB files and embedded base64; a small `.txt` of hex words passes).
   - New `tests/test_msl_combiners.m` registered `if(APPLE)` in `CMakeLists.txt` next to
     `test_sim_state_hash` (`CMakeLists.txt` BUILD_TESTING block).
   - **This is the lane that catches "shader landed in GLSL but broke MSL"** — the program-wide
     rule that every shader change lands in both generators gets a machine check.
3. **`strict-build` (new, PR-blocking after E3 cleanup)** — Linux job:
   `cmake -B build-strict -DPORT_STRICT=ON` + build; zero errors. Gated in only after T8.3.1-4
   land (§4.3.1). Until then it runs `continue-on-error: true` with the error count surfaced, so
   regressions are visible immediately.
4. **Parity-capture artifacts (local lane, formalized)** — `tools/metal_parity_sweep.sh` (new,
   ~120 lines; generalizes the ad-hoc Phase-3 sweep): for each level, deterministic boot
   (`--level N --deterministic --screenshot-frame 120 --screenshot-exit`) under GL then Metal,
   `compare_screenshots.py --max-changed-pct` per-level budget table, `compare_state.py` on
   `--trace-state` JSONL, summary CSV written to `baselines/metal_parity_latest.csv` (text —
   committable). Screenshots stay in `/tmp` (R2).

#### 4.3.1 PORT_STRICT cleanup — fix plan for the 154

Ordered to kill 144 errors with one semantic change:

1. **T8.3.1 — the ALBank class (144 errors)**: change the non-stub prototypes at `src/snd.h:200`
   and `:202` from `struct ALBankAlt_s *soundBank` to `void *soundBank` (exactly matching the
   `PORT_SND_STUBS` variant at `snd.h:193-197`), update the definitions in `src/snd.c`
   (`SND_IMPLEMENTATION` TU) to accept `void *` and cast to `struct ALBankAlt_s *` once at the top.
   Rejected alternatives: casting ~144 call sites (churn, obscures diffs vs decomp); retyping
   `g_musicSfxBufferPtr` (`music.h:101`) breaks the `ALBank` assignment at `music.c:921`. ABI is
   identical (pointer arg) ⇒ expect byte-identical codegen; verify per §8.
2. **T8.3.2 — ModelNode punning (2)**: explicit `(ModelNode *)` casts with a `/* decomp union pun
   (Header.FirstGroup IS the first child node) */` comment at `chrobjhandler.c:980,988` — same
   pattern already used at `chrprop.c:2349`.
3. **T8.3.3 — implicit decls (3)**: add prototypes — `texLoadFromDisplayList` (declare in the tex
   header included by `image_bank.c:204`), `getRoomPositionScaledByIndex` (`lightfixture.c:440` —
   coordinate with the shoot-out-lights owner; prototype only, no behavior),
   `sub_GAME_7F0B9E04` extern at `prop.c:3331` (match the pattern at `chrprop.c:1896`).
   **Danger note for juniors**: `#else` reference bodies in decomp files LIE — never transcribe
   from them; only add declarations matching the *called* signature.
4. **T8.3.4 — matrix punning + diag pointers (6)**: `unk_0A1DA0.c:1260,1331` — cast via the
   existing coord3d/Mtxf idioms used elsewhere in the file; `gfx_pc.c:18235,18262` — cast
   `(const void *)(uintptr_t)` around the volatile diag globals.
5. **T8.3.5 — flip CI**: remove `continue-on-error` from the `strict-build` job; add
   `-DPORT_STRICT=ON` to the release checklist.

### 4.4 Perf: Metal census lane, HUD, budgets

1. **Census backend column**: `tools/perf_census.sh` `runmeasure` already forwards extra env
   (`perf_census.sh:37-46`); add a third measurement `metal_ms` via
   `runmeasure "$lvl" "$tmp/${lvl}_mtl.log" GE007_RENDERER=metal` (APPLE-only, `uname` guard) and
   extend the CSV header to `level,default_ms,default_fps,xluoff_ms,speedup,metal_ms,metal_fps`.
   `perf_budget_check.py` gains `--column metal_ms` (it currently reads fixed columns) so the same
   HARD 16.6 ms / TARGET 8.3 ms budgets apply per backend. Baselines committed:
   `baselines/perf_census_gl_baseline.csv`, `baselines/perf_census_metal_baseline.csv`
   (the GL one starts as a copy of the existing committed `baselines/perf_census_baseline.csv` —
   the only baseline file in the tree today).
2. **Regression budget**: local pre-merge gate = `perf_budget_check.py latest.csv --baseline
   <backend>_baseline.csv --regress-frac 0.10` fails on >10% per-level regression (the tool
   already supports `--baseline`, but its default threshold is 15% — `--regress-frac` default
   `0.15`, `perf_budget_check.py:65` — so the 10% contract must pass the flag explicitly).
   Runs in the release checklist and before any Stage-D flip.
3. **Frame-time HUD (`Video.PerfHud`, `GE007_PERF_HUD`)** — in-engine, both backends, font-free:
   - Data: reuse `perf_work_ms` (`platform_sdl.c:2488-2510`); keep a 120-entry ring
     `float g_perfHudHistory[120]` updated once per frame in the same block that prints `[PERF]`.
   - Draw: in the **final output-filter pass** of both generators (GL: the output-VI-filter FS in
     `gfx_opengl.c`; MSL: the mirrored filter FS in `gfx_metal.mm` `mtl_end_frame` chain), append a
     gated block; history uploaded as a 120×1 R32F texture (GL) / 480-byte buffer (Metal).
   - Shader sketch (identical logic both generators — GLSL shown; MSL is the mechanical port, same
     as every other filter feature):

   ```glsl
   #ifdef PERF_HUD   // compiled only when Video.PerfHud=1 (recompile like other filter toggles)
   // bottom-left 128x48 px: per-frame bars, green<8.3ms, yellow<16.6ms, red above
   vec2 hud = (fragPx - vec2(8.0, 8.0)) / vec2(128.0, 48.0);
   if (all(greaterThanEqual(hud, vec2(0.0))) && all(lessThan(hud, vec2(1.0)))) {
       float ms   = texture(uPerfHist, vec2(hud.x, 0.5)).r;      // ring, newest at right
       float h    = clamp(ms / 33.3, 0.0, 1.0);                   // 33.3ms full scale
       if (hud.y < h) {
           vec3 c = ms < 8.3 ? vec3(0.2,0.9,0.3) : ms < 16.6 ? vec3(0.95,0.85,0.2) : vec3(0.95,0.25,0.2);
           color.rgb = mix(color.rgb, c, 0.85);
       }
       if (abs(hud.y - 16.6/33.3) < 0.015) color.rgb = mix(color.rgb, vec3(1.0), 0.5); // 60fps line
   }
   #endif
   ```

   - Rails: `Video.PerfHud` default 0 ⇒ shader block not compiled ⇒ byte-identical (R3). Render-
     only, reads a platform timer already read by `[PERF]` ⇒ R1-clean (no sim reads render).

### 4.5 Crash & diagnostics

1. **Persist crash logs** (`main_pc.c` changes only):
   - At startup (next to handler install, `main_pc.c:529`), open
     `<save_dir>/crash/ge007-crash-<pid>.log` `O_CREAT|O_APPEND` and stash the fd in
     `g_crashLogFd` (save dir resolution already exists in `config_pc.c`).
   - In `crashHandler`, everywhere `pc_diag_write_stderr(msg, n)` is called, also
     `write(g_crashLogFd, msg, n)` — `write()` to a pre-opened fd is async-signal-safe, preserving
     the handler's existing discipline (comment at `main_pc.c:317-322`).
   - Fatal tier additionally writes: version string (§4.6), `gfx_backend_name()`, level id, frame
     counters (`g_frame_count_diag` is already extern'd in the handler, `main_pc.c:356`). A
     backtrace already exists — `backtrace_symbols_fd(bt, n, STDERR_FILENO)` at `main_pc.c:374` —
     so just call it a second time with `g_crashLogFd` (document: best-effort, not strictly
     async-safe; acceptable in a dying process).
   - Startup prints a one-liner if a previous crash log exists ("found crash report from last run:
     <path> — attach it via --diagnostics-bundle").
2. **`ge007 --diagnostics-bundle [out.tar.gz]`** (new CLI in `main_pc.c` arg loop, exits after):
   collects into a tarball — version/commit, `--dump-config` output (flag exists,
   `main_pc.c:547-548`), OS + SDL + GPU/Metal device name, the crash/ directory, the last census
   CSV if present, `ge007.ini`. **Excludes** screenshots, traces, saves, anything ROM-derived by
   default (R2 — the checklist's own rule, `RELEASE_CHECKLIST.md:73-76`); `--include-traces` opt-in
   with a printed warning. Implemented by spawning `tar` (POSIX) — 100 lines, no new deps. Exposed
   in the app shell as Help → "Save Diagnostics Bundle…" via `GameBridge`.

### 4.6 Release process & versioning

- **`VERSION` file** at repo root (`0.2.0`), consumed by: `project(ge007 VERSION ...)` in
  `CMakeLists.txt:2` → `-DGE007_VERSION_STRING="${PROJECT_VERSION}+g<shorthash>"` compile def →
  printed at boot, in crash logs, in `--diagnostics-bundle`; `build_app_bundle.sh` templates
  `CFBundleShortVersionString` from it (replaces hardcoded `Info.plist:15-16`).
- **SemVer stance**: 0.x while assets/features move fast; MINOR = feature release (Metal default
  flip = 0.2.0), PATCH = fixes. Tag `v0.2.0` annotated; `CHANGELOG.md` keep-a-changelog format,
  entry required by the release checklist.
- **Release checklist v2** (extends `docs/RELEASE_CHECKLIST.md` — do not fork it): adds, on top of
  the existing preflight (`scripts/release_preflight.sh --deep-runtime --rom ... 
  --macos-app-bundle-sdl2 --strict-ignored --github`):
  1. `cmake -B build-strict -DPORT_STRICT=ON && cmake --build build-strict` — zero errors.
  2. `tools/backend_invariance_gate.sh` ×3 replays — hashes identical.
  3. `tools/metal_parity_sweep.sh --all` — budgets green, CSV archived.
  4. `tools/sim_invariance_gate.sh` (existing R1/R3 lane).
  5. `tools/perf_census.sh` + `perf_budget_check.py --baseline ... --regress-frac 0.10` both
     backends — no >10% regression (tool default is 15%; pass the flag).
  6. ASan smoke both backends.
  7. `scripts/ci/check_no_rom_data.sh` + `git clean -ndX` review (existing).
  8. DMG built, signed, notarized, stapled; installed + booted on a non-dev account.
- **Distribution stance (restating the rails, R2)**: releases ship **binaries and pipelines,
  never assets** — the app, the AppImage, the texpack *generator* (`tools/texpack/build_pack.py`,
  `synth_texture.py` with generic presets = Tier A1). ROM-derived packs (Tier B) are user-built
  locally; release notes link the how-to, never the output.

### 4.7 Branch consolidation — the merge train

Because origin/main ⊂ perf ⊂ robustness ⊂ metal (re-verified 2026-07-02, §2.5), the train is
**linear**: every step's merge fast-forwards `main` along one ancestry chain, so **steps 1–3
cannot produce a merge conflict** unless `main` gains unrelated commits mid-train.

**Pre-flight** — run before step 0; every line must exit 0, else STOP and escalate with the output:

```sh
git fetch origin
git merge-base --is-ancestor origin/main feat/metal-backend
git merge-base --is-ancestor perf/make-it-rip robustness/remaster-hardening
git merge-base --is-ancestor robustness/remaster-hardening feat/metal-backend
git merge-tree --write-tree feat/metal-backend fix/train-distance-fog   # prints a tree id; any "CONFLICT" line = escalate
```

| Step | Action (exact commands) | Validation before advancing |
|---|---|---|
| 0 | `git checkout main && git pull --ff-only origin main` (local main is 86 behind). Housekeeping: `git worktree list`, then `git worktree prune` and `git worktree remove .claude/worktrees/agent-af7d6a1b5553877cd` (that worktree holds `fix/train-distance-fog` checked out, which would block step 4's checkout); then `git branch -d feat/split-screen-multiplayer feat/dam-hd-remaster worktree-agent-af7d6a1b5553877cd` and `git push origin --delete feat/split-screen-multiplayer` (remote tip `3dd3c23` ⊂ origin/main — verified) | `git log main -1 --format=%h` == `c3e3eec` (if newer: main moved — re-run pre-flight before continuing) |
| 0b | Push the train branches (none has ever been pushed): `git push -u origin perf/make-it-rip robustness/remaster-hardening feat/metal-backend fix/train-distance-fog` | `git for-each-ref --format='%(refname:short) %(upstream:short)' refs/heads/` shows an upstream for all four |
| 1 | PR `perf/make-it-rip` (5 commits) → main: `gh pr create --base main --head perf/make-it-rip`; merge with a **merge commit** (no squash) | Fresh Release build (`cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8`); `ctest --test-dir build --output-on-failure`; `tools/perf_census.sh` full run, then `python3 tools/perf_budget_check.py baselines/perf_census_latest.csv --baseline baselines/perf_census_baseline.csv` (the branch ships both tools + the committed baseline); GL screenshot identity vs a step-0 baseline capture (`cmp`, §8) |
| 2 | PR `robustness/remaster-hardening` (20 more commits) → main; merge (no squash) | Build; ctest; `tools/playability_smoke.sh --all --no-build --rom /path/outside/repo/baserom.u.z64 --binary build/ge007`; all 7 hardening items verified committed in-range (§2.5 hashes) — residual: the two default-off items (shoot-out-lights `a873ec4`/`edd1254`, bullet-spark `df2f1de`) lack an in-game visual pass ⇒ file follow-up issues, not blockers |
| 3 | PR `feat/metal-backend` tip → main (25 more commits at `daf711e`; re-count with `git rev-list --count robustness/remaster-hardening..feat/metal-backend`); merge (no squash) | GL byte-identity (`cmp`, §8); cross-backend sim-hash A/B — `tools/backend_invariance_gate.sh dam1 600` if W8.E1.T2 has landed, else the manual equivalent: run `env $ENVV GE007_RENDERER=<gl\|metal> build/ge007 --rom <rom> --ramrom dam1 --deterministic --screenshot-frame 600 --screenshot-exit --sim-state-hash-out /tmp/train_<gl\|metal>.json` once per backend, `diff` the two JSONs (must be identical); ASan smoke both backends (§8); `./scripts/ci/check_sim_render_separation.sh`; `./scripts/ci/check_timing_lock.sh`. (`metal_parity_sweep.sh --all` too if W8.E1.T3 has landed — at train time it usually has NOT; the 18-level parity evidence in `docs/design/METAL_BACKEND_PLAN.md:9` stands in) |
| 4 | `git checkout fix/train-distance-fog && git rebase main && git push --force-with-lease origin fix/train-distance-fog`; PR → main (merge-tree vs metal tip verified clean, §2.5) | Default-off byte-identity (§8 `cmp`); A/B screenshot on Train (`--level 13`) with the branch's flag on vs off |
| 5 | `git tag -a v0.2.0-rc1 -m "consolidated main: metal+perf+robustness"; git push origin v0.2.0-rc1`; delete merged branches (`git branch -d` each + `git push origin --delete` each) | CI green on all train PRs; `git tag --list 'v0.2.0-rc1'` prints the tag |

**Conflict-resolution policy** (follow literally):
- **Steps 1–3**: a conflict is impossible by ancestry. If the PR or `git merge` reports one, `main`
  moved after step 0 — do **not** resolve by hand: `git merge --abort`, re-run the pre-flight
  block, and escalate to the senior with both outputs.
- **Step 4**: the branch touches only `src/platform/fast3d/gfx_pc.c` (+22 lines). On a rebase
  conflict *inside that hunk*, the branch side wins; anything wider ⇒ escalate to the renderer
  owner. Stuck >1 day ⇒ risk #6 kill criterion: drop the cherry from v0.2.0-rc1 and file an issue.
- Never force-push `main`; `--force-with-lease` is allowed only on `fix/train-distance-fog` after
  its rebase.

Steps 1–3 are PRs (CI runs on PR, `ci.yml:8`), merged without squash to preserve the validated
history. All merges done by a human after checklist sign-off — W8 designs the train; it does not
auto-merge.

---

## 5. Work breakdown

Estimates in **junior-engineer-days** (jd). IDs: W8.E#.T#.
**Execution-order note: W8.E6.T2 (the merge train, §4.7) runs FIRST — it is program task #1 in
00-MASTER-PLAN.md despite its ordinal position here.**

### E1 — Metal-default-on-macOS (13 jd)

| ID | Task | Files | Steps | Acceptance (runnable) | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W8.E1.T1 | `Video.Renderer` config key + precedence + `gfx_backend_name()` | `src/platform/fast3d/gfx_backend.c`, `gfx_backend.h`, `src/platform/platform_sdl.c` (settings registry — `Video.RemasterFX` pattern at `:1677`) | Implement §4.1 precedence; log `[GFX] backend=<name> source=<env\|config\|auto>` at init; read before `SDL_CreateWindow` | `GE007_RENDERER=gl build/ge007 --level 1 --deterministic --screenshot-frame 60 --screenshot-exit` logs `backend=gl`; same with `--config-override Video.Renderer=metal` (no env) logs `backend=metal source=config`; env beats config (test both set) | 2 | — | R3: default `auto`==gl pre-flip ⇒ byte-identical; A/B = the key itself |
| W8.E1.T2 | `tools/backend_invariance_gate.sh` (G1) | new tool | Clone `tools/sim_invariance_gate.sh:38-63` structure; axis = `GE007_RENDERER` gl/metal at identity config; 3 replays (`dam1`, `facility3`, `runway1`) × 600 frames | `tools/backend_invariance_gate.sh dam1 600` prints `MATCH <hash>` and exits 0; negative test: run the two backends at different frame counts (e.g. 600 vs 599) → hashes differ → exit 1 (NOTE: perturbing a render-only key like RenderScale must NOT trip it — that invariance is exactly what `sim_invariance_gate.sh` proves) | 2 | W8.E1.T1 | R1: proves sim never reads backend state |
| W8.E1.T3 | `tools/metal_parity_sweep.sh` (G2/G3/G7) + budget table | new tool, `baselines/metal_parity_budgets.csv` | §4.3 item 4; per-level budget CSV (4.2% default; statue/depot 15% waiver rows w/ rationale column); `--soak` mode (1800 frames, both presets); asserts G7 opt-out log line | `tools/metal_parity_sweep.sh --all` exits 0, writes `baselines/metal_parity_latest.csv`; `--soak` exits 0 with `grep -c GFX-RECOVER == 0` on all logs | 4 | W8.E1.T1 | R2: screenshots stay /tmp; CSV text committable |
| W8.E1.T4 | G4–G6 evidence run: ASan both backends, census compare, fault-injected crash-path check | `tools/asan_smoke.sh` (exists), new `GE007_DIAG_FAULT_INJECT` hook in `gfx_pc.c` DL loop (debug-only, env-gated) | Run gates; archive outputs (text) as committed evidence | All G-gate artifacts present; `GE007_DIAG_FAULT_INJECT=120 GE007_RENDERER=metal build/ge007 --level 1` produces exactly one `[GFX-RECOVER]` line and exits 0 | 3 | W8.E1.T2-3, W8.E4.T1 | R3: fault-inject env default-off, debug builds only |
| W8.E1.T5 | Stage D flip + docs + decision artifact | `gfx_backend.c` (1 line), `docs/VISUAL_MODES.md`, `docs/design/METAL_BACKEND_PLAN.md` status, a new `DECISION.md` evidence file | Write DECISION.md: gate-by-gate G1–G7 result table with links to each evidence file + explicit go/no-go; then flip `auto`⇒metal on `__APPLE__`; update two-anchor doc language (§4.1); link DECISION.md in the flip commit msg | On macOS, `build/ge007 --level 1 ...` with **no** env logs `backend=metal source=auto`; `GE007_RENDERER=gl` still forces GL; Linux unaffected (CI green); `DECISION.md` exists with all 7 gate rows green | 2 | G1–G7 all green | R3: two-anchor model documented; opt-out retained |

### E2 — Packaging (18 jd)

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W8.E2.T1 | Codesign + notarize end-to-end locally | `macos/Scripts/sign_and_notarize.sh`, credentials in keychain/env | Obtain Developer ID cert (user action, §9 Q1); export the 4 env vars and run the exact §4.2-item-1 command block (`sign_and_notarize.sh <path-to.app>` — usage + env interface documented at `sign_and_notarize.sh:7-13`; `--skip-notarize` for sign-only iteration) | `spctl --assess --type execute -vv build-macos-app/MGB64.app` prints `accepted` + `Notarized Developer ID`; clean-VM install shows no Gatekeeper block | 3 | user credentials | R2: `verify_asset_free.sh` re-run on final .app |
| W8.E2.T2 | Enable `package` job in `macos-release.yml` | `.github/workflows/macos-release.yml` | Uncomment job; secrets via repo settings; `permissions: contents: write` on package job only; keep `workflow_dispatch` (release remains a manual act) | Dispatch run produces a signed, notarized DMG artifact; hygiene jobs still pass | 2 | W8.E2.T1 | R2: workflow keeps asset-free verifier step |
| W8.E2.T3 | ROM-picker UX hardening + legality copy + Metal bundle smoke | `macos/Sources/ROMPickerView.swift`, `OnboardingFlow.swift`, `macos/Tests/` | §4.2 item 3; wrong-ROM error path; bundle-launch smoke with `GE007_RENDERER=metal` | Manual: first launch on account with no config shows picker + legality note; feeding a renamed JPEG shows the validation error, not a crash; `ctest -R macos_app` green | 3 | W8.E1.T1 | R2: legality note text in UI; no ROM bundled |
| W8.E2.T4 | Version plumb-through to bundle | `macos/Scripts/build_app_bundle.sh`, `macos/Resources/Info.plist` | Template `CFBundleShortVersionString`/`CFBundleVersion` from `VERSION` (§4.6) | Built app's `Info.plist` shows the `VERSION` value; `ctest -R macos_app_asset_free_verifier` green | 1 | W8.E6.T1 | — |
| W8.E2.T5 | Windows assessment (timeboxed) | report only: a Windows-assessment report | MSYS2 clang64: `cmake -B build-win`; log every failure; classify (toolchain / POSIX API / audio / GL loader); STOP at 5 days | Report exists with a build log and a go/no-go recommendation; **no tree changes** | 5 | — | — |
| W8.E2.T6 | Linux AppImage | new `packaging/linux/{mgb64.desktop,AppRun,icon}`, `CMakeLists.txt` install rules, `scripts/make_appimage.sh` | §4.2 Linux; icon from `generate_app_icon.py` source (A1); zenity ROM prompt fallback | On Ubuntu 22.04+: `./MGB64-x86_64.AppImage --rom <path> --level 1 --deterministic --screenshot-frame 60 --screenshot-exit` exits 0; no ROM ⇒ dialog appears | 4 | W8.E6.T1 | R2: AppImage contains no assets; `check_no_rom_data.sh` covers packaging/ |

### E3 — CI expansion + PORT_STRICT (14 jd)

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W8.E3.T1 | `macos-build` PR job | `.github/workflows/ci.yml` | §4.3 item 1 | PR CI shows the job; a deliberate `.mm` syntax error on a scratch branch fails it | 3 | — | R2: job runs `check_no_rom_data`-adjacent hygiene via existing ctest |
| W8.E3.T2 | ROM-free MSL combiner-compile ctest | `src/platform/fast3d/gfx_metal.mm` (test hook), `tests/test_msl_combiners.m`, `tests/data/combiner_corpus.txt`, `CMakeLists.txt` | §4.3 item 2 | `ctest --test-dir build -R msl_combiners` passes locally and on `macos-14` runner; breaking the MSL generator string (scratch) fails it | 3 | W8.E3.T1 | R2: corpus = config words, no asset data (documented in file header) |
| W8.E3.T3 | PORT_STRICT: ALBank class (144 errs) | `src/snd.h:193-206`, `src/snd.c` | §4.3.1 T8.3.1 | `cmake -B build-strict -DPORT_STRICT=ON && cmake --build build-strict -j8 -- -k 2>&1 \| grep -c error:` ≤ 10; default build byte-identical screenshot (`cmp`) + `ctest` green + `tools/sim_invariance_gate.sh dam1 600` hash unchanged vs pre-change run (RAMROM gate — snd is sim-adjacent) + one audio-audible manual check (SFX still play, Dam) | 2 | — | R1/R3: no flag needed — provable no-op (ABI-identical); RAMROM sim-hash + byte-identity validated per §8 |
| W8.E3.T4 | PORT_STRICT: remaining 10 | `chrobjhandler.c:980,988`, `image_bank.c:204`, `lightfixture.c:440`, `prop.c:3331`, `unk_0A1DA0.c:1260,1331`, `gfx_pc.c:18235,18262` | §4.3.1 T8.3.2–4; prototypes must match called signatures — never transcribe `#else` reference bodies | strict build: **0 errors**; default build byte-identical (`cmp` screenshots, 3 levels); `tools/sim_invariance_gate.sh dam1 600` hash unchanged (RAMROM gate — lightfixture/prop are sim files); ctest green | 2 | W8.E3.T3 | R1/R3: casts/prototypes only; no behavior change |
| W8.E3.T5 | `strict-build` CI job + flip to blocking | `.github/workflows/ci.yml` | §4.3 item 3 (Linux runner; APPLE-only `deprecated-declarations` clause `CMakeLists.txt:496-498` means macOS strict runs in T1's job too — add `-DPORT_STRICT=ON` variant there) | PR with a new `-Wincompatible-pointer-types` violation (scratch) fails CI | 2 | W8.E3.T3-4 | gates R-rail enforcement quality tree-wide |
| W8.E3.T6 | Formalize parity-evidence flow | `tools/metal_parity_sweep.sh` (from E1.T3), the evidence-flow README | Document what is committable (hashes/CSV/percent) vs local-only (images/traces); wire `release_preflight.sh` to call the sweep when `--rom` given | `scripts/ci/check_no_rom_data.sh` green with evidence dir populated; preflight run produces the CSV | 2 | W8.E1.T3 | R2: the whole point |

### E4 — Perf lane (7 jd)

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W8.E4.T1 | Census `metal_ms` column + baselines | `tools/perf_census.sh`, `tools/perf_budget_check.py`, `baselines/perf_census_{gl,metal}_baseline.csv` | §4.4 items 1–2 | `tools/perf_census.sh dam jungle` CSV has 7 columns on macOS (5 on Linux); `python3 tools/perf_budget_check.py baselines/perf_census_latest.csv --column metal_ms` exits 0; budget: all 20 levels < 16.6 ms both backends | 2 | W8.E6.T2 step 1 if targeting `main` (harness already on the `feat/metal-backend` base branch — can start there immediately) | R2: CSVs are text, committable |
| W8.E4.T2 | `Video.PerfHud` frame-time HUD (GLSL + MSL) | `src/platform/fast3d/gfx_opengl.c` (output filter FS), `gfx_metal.mm` (mirrored), `platform_sdl.c` (key + ring buffer near `:2510`) | §4.4 item 3; land in BOTH generators in the same commit (program rule) | A/B: `--config-override Video.PerfHud=1` shows bars on GL and Metal (manual screenshot); `Video.PerfHud=0` byte-identical (`cmp` vs baseline); `ctest` green | 4 | W8.E1.T1 | R3: default 0, key + `GE007_PERF_HUD` env; R1: render-only |
| W8.E4.T3 | Perf gate in release checklist + train | `docs/RELEASE_CHECKLIST.md` | Add §4.6 checklist item 5 wording | Checklist PR merged; dry-run executed once end-to-end | 1 | W8.E4.T1 | — |

### E5 — Crash & diagnostics (5 jd)

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W8.E5.T1 | Crash-log persistence | `src/platform/main_pc.c:300-420` region, `config_pc.c` (save-dir helper) | §4.5 item 1; keep async-signal-safety discipline (`write()` to pre-opened fd only inside handler) | `GE007_DIAG_FAULT_INJECT` run produces `<save_dir>/crash/*.log` containing the `[GFX-RECOVER]` line + backend name + version; ASan build unaffected (SIGBUS clause `main_pc.c:531-535` preserved) | 2 | W8.E1.T4 (fault inject) | R3: `GE007_NO_CRASH_HANDLER` still bypasses everything |
| W8.E5.T2 | `--diagnostics-bundle` + app menu hook | `src/platform/main_pc.c` (arg loop), `macos/Sources/MenuBarManager.swift`, `GameBridge.h` | §4.5 item 2 | `build/ge007 --diagnostics-bundle /tmp/diag.tar.gz` exits 0; `tar tzf` shows config/version/crash logs and **no** `.bmp/.png/.z64` entries; Help-menu item produces the same | 3 | W8.E5.T1 | R2: ROM-derived data excluded by default, opt-in flagged with warning |

### E6 — Release process & branch consolidation (9 jd)

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W8.E6.T1 | `VERSION` + version string + CHANGELOG scaffold | `VERSION`, `CMakeLists.txt:2`, `src/platform/main_pc.c` (boot print), `CHANGELOG.md` | §4.6 | Boot log prints `MGB64 0.2.0+g<hash>`; `ctest` green; `check_release_ready.sh` green | 2 | — | — |
| W8.E6.T2 | Merge train steps 0–5 (**program task #1 — runs before everything else in W8**) | git only | §4.7 exactly: pre-flight ancestry block, step-0 housekeeping (incl. worktree removal), step-0b pushes (the train branches have never been pushed), then steps 1–5; each step a PR with its validation block pasted in the description | `git log origin/main -1` == metal tip merge; all 4 validation blocks archived; CI green each PR; `v0.2.0-rc1` tag exists | 4 | none — §4.7 step-3 fallbacks cover the not-yet-built E1 tools; local gates suffice | R1/R3: step-3 runs the §8 harness (with §4.7 fallbacks) |
| W8.E6.T3 | Release checklist v2 + first executed release | `docs/RELEASE_CHECKLIST.md`, GitHub release | §4.6 checklist; run it for v0.2.0; publish DMG + AppImage + source archive | Release page has DMG (notarized) + AppImage + checklist evidence summary; `check_public_history_text.sh`/`check_github_*` tools green | 3 | E1–E5 M-gates, W8.E2.T2, W8.E2.T6 | R2: distribution stance restated in release notes |

**Total: 66 junior-days ≈ 13 junior-weeks** (E1 13 + E2 18 + E3 14 + E4 7 + E5 5 + E6 9).

---

## 6. Milestones & deliverables

| M | Deliverable | Contents | Demo script (reviewer runs) | Est |
|---|---|---|---|---|
| M1 | **Consolidated main** | E6.T2 train steps 0–4; E6.T1 versioning | `git log --oneline origin/main -3` shows metal merge; `build/ge007 --level 1 --deterministic --screenshot-frame 60 --screenshot-exit` boots from a fresh `main` clone | 1.5 wk |
| M2 | **CI matrix + strict tree** | E3 complete: macOS lane, MSL ctest, PORT_STRICT 0 errors, blocking | Open a scratch PR adding `int *x = 5;` to `chrai.c` → watch `strict-build` fail; `ctest --test-dir build -R msl_combiners` | 3 wk |
| M3 | **Metal default on macOS** | E1 complete (G1–G7 evidence), E4.T1-2 | On a Mac, `build/ge007 --level 9 --deterministic --screenshot-frame 120 --screenshot-exit` with no env → log shows `backend=metal source=auto`; `GE007_RENDERER=gl` flips it back | 2.5 wk |
| M4 | **Signed app + diagnostics** | E2.T1-4, E2.T6, E5 | Download DMG artifact from a `macos-release.yml` dispatch → install on a clean account → ROM picker → play; then `ge007 --diagnostics-bundle /tmp/d.tgz && tar tzf /tmp/d.tgz` | 3.5 wk |
| M5 | **v0.2.0 released** | E6.T3, E2.T5 report, E4.T3 | Open the GitHub release page: notarized DMG + AppImage + changelog + evidence summary; `git tag --list 'v0.2.0'` | 1.5 wk |

(Weeks assume ~5 jd/wk one junior + review overhead; M2/M3/M4 can overlap across two juniors —
critical path ≈ 8 weeks with 2 engineers.)

---

## 7. Risks & mitigations (ranked)

| # | Risk | Likelihood/Impact | Mitigation | Kill / de-scope criterion |
|---|---|---|---|---|
| 1 | **Apple Developer credentials unavailable** (personal project; $99/yr + identity) | M / H — blocks notarization (M4) | Everything else proceeds; ship ad-hoc-signed zip with documented right-click-open as interim | If no credentials by M4 start: de-scope T2.T1-2 to "unsigned + docs", keep DMG job manual |
| 2 | **GitHub `macos-14` runner lacks a usable Metal device or times out** | L / M — kills the MSL ctest lane | The test needs only `MTLCreateSystemDefaultDevice` + `newLibraryWithSource` (no drawable); known to work on GH Apple-Silicon runners; fallback = compile-only via `xcrun metal` CLI on the corpus | If flaky >1wk: demote msl ctest to local-only, keep macOS *build* lane blocking |
| 3 | **PORT_STRICT fix perturbs decomp behavior** (esp. snd.h prototype change) | L / H — audio regression | ABI-identical change; byte-identity screenshots + audible smoke + `compare_audio_reference.py` if doubt; land as its own commit for easy revert | Any non-identical frame or audio diff ⇒ revert, fall back to per-site casts (mechanical, 144 edits) |
| 4 | **Metal parity budget hides a real regression** (4.2% is generous) | M / M | Budgets are *per-level* + waivers documented; `compare_state.py` MATCH is the hard gate (sim exact); side-by-side review for waiver levels each release | If a level's delta grows release-over-release >1.5×: freeze flip/rollback default to gl until root-caused |
| 5 | **Flip breaks a niche path** (screenshot tooling, split-screen, HD packs on Metal) | M / M | G3 soak includes `--remaster` + split-screen scene; the two direct-GL frontend couplings are already guarded (commit `6498da6`); keep `GE007_RENDERER=gl` forever | Any G-gate red ⇒ stay Stage C; flip is a 1-line revert |
| 6 | **Merge train uncovers latent conflicts with post-merge main work** | L / M — train is linear (ancestry verified) | Steps are PRs with full validation between; fix/train-distance-fog rebased last | Conflict >1 day ⇒ merge metal tip directly (single step) since ancestors add no risk reduction |
| 7 | **Windows attempt scope-creeps** | M / L | Hard 5-day timebox, report-only deliverable | Day 5 = pencils down regardless of state |
| 8 | **AppImage GL variance on random distros** | M / L | Target Ubuntu 22.04 baseline; SDL2 bundled, GL system-provided; document known-good matrix | If >3 distro-specific bugs: ship tarball + build docs instead for 0.2.0 |

---

## 8. Validation strategy

**Standard env preamble** (all deterministic lanes, per roadmap §7 and `sim_invariance_gate.sh:38-40`):

```sh
ENVV="SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_BACKGROUND=1 GE007_NO_VSYNC=1 \
      GE007_NO_INPUT_GRAB=1 GE007_DETERMINISTIC_STABLE_COUNT=1"
```

**Per-commit (every W8 task)**:

```sh
cmake --build build --parallel 8                       # zero new warnings (summarize_build_warnings.py --max-total 0)
ctest --test-dir build --output-on-failure             # ROM-free suite
./scripts/ci/check_sim_render_separation.sh            # R1
./scripts/ci/check_timing_lock.sh                      # R2 (timing)
./scripts/ci/check_no_rom_data.sh                      # R2 (assets) — contamination guard
```

**Identity (R3) — GL anchor, byte-level**: capture baseline before / after with identity flags and
`cmp` (not tolerance):

```sh
env $ENVV build/ge007 --rom baserom.u.z64 --level 1 --deterministic \
    --screenshot-frame 120 --screenshot-label base --screenshot-exit
# ...apply change, recapture as 'after', then:
cmp /tmp/ge007_shot_base.bmp /tmp/ge007_shot_after.bmp   # byte-identical required for default-off
```

**Cross-backend (Metal gates G1–G3)**:

```sh
tools/backend_invariance_gate.sh dam1 600          # G1: sim-hash GL==Metal (new, W8.E1.T2)
tools/metal_parity_sweep.sh --all                  # G2: per-level compare_screenshots.py budgets
tools/metal_parity_sweep.sh --all --soak           # G3: 1800-frame soak, zero [GFX-RECOVER]
tools/compare_state.py /tmp/gl_trace.jsonl /tmp/mtl_trace.jsonl   # localization on any mismatch
```

**Sim invariance (R1/R3, existing lanes — run for anything touching presets/backend/timing)**:

```sh
tools/sim_invariance_gate.sh dam1 600 2            # post-FX off vs on, hash identical (LOCAL, ROM)
ctest --test-dir build -R sim_state_hash           # ROM-free CI primitive
```

**Sanitizers** (hot-path or handler changes):

```sh
cmake -B build-asan -DSANITIZE=ON && cmake --build build-asan -j8
GE007_RENDERER=metal tools/asan_smoke.sh --binary build-asan/ge007   # and default (GL)
```

**Perf**:

```sh
tools/perf_census.sh                                                   # writes baselines/perf_census_latest.csv
python3 tools/perf_budget_check.py baselines/perf_census_latest.csv \
        --baseline baselines/perf_census_metal_baseline.csv --column metal_ms \
        --regress-frac 0.10        # tool default is 0.15; the W8 contract is 10%
```

**Strict / packaging / release**:

```sh
cmake -B build-strict -DPORT_STRICT=ON . && cmake --build build-strict -j8   # 0 errors (post-E3)
./macos/Scripts/verify_asset_free.sh build-macos-app/MGB64.app               # every bundle
scripts/release_preflight.sh --deep-runtime --rom /outside/baserom.u.z64 \
        --macos-app-bundle-sdl2 --strict-ignored --github                    # release candidates
```

Artifacts: images/traces stay in `/tmp` (ROM-derived, R2); hashes/CSVs/reports go to
committed evidence and `baselines/`.

---

## 9. Open questions (need the user)

1. **Apple Developer Program enrollment** — notarization (W8.E2.T1) requires the user's $99/yr
   Developer ID and a one-time cert setup. Without it we ship unsigned (risk #1 fallback). Which?
2. **Public release appetite for v0.2.0** — the repo posture is "local preflight evidence, public
   launch deliberate" (`macos-release.yml` header). Is v0.2.0 a *public* GitHub release with
   binaries, or a private/tagged milestone? The checklist supports both; the announcement does not.
3. **Perf budget for the Metal flip (G5)** — proposed "within 1.10× of GL work_ms per level". If
   Metal turns out *faster* everywhere (likely — no translator), should GL regressions vs Metal
   also gate? Proposed: no — GL is reference for correctness, not speed.
4. **statue/depot parity waivers** — the 11–15% deltas are documented as visually-at-parity
   (`docs/design/METAL_BACKEND_PLAN.md:9`). Sign-off wanted from the user (one side-by-side viewing) before
   they're enshrined as permanent budget rows in `baselines/metal_parity_budgets.csv`.
5. **robustness branch residuals — RESOLVED 2026-07-02 (git-verified)**: all 7 hardening items
   are committed in `9336d49`'s range — item 1 bullet-spark UB `df2f1de`, items 2–6 bundle
   `2a22ea2`, item 7 shoot-out-lights `a873ec4`+`edd1254`, plus monitor-overflow `5ef1ec5`.
   Remaining residual is only that the two default-off items (shoot-out-lights, bullet-spark deep
   fix) have not had an in-game visual pass — train step 2 files follow-up issues; not a blocker.
