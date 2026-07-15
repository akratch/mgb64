# Audit Close-Out & Harness Strategy

Scope: the execution-ready plan to close the remaining native-port audit backlog
on `feat/webgpu-backend` and to extend this session's validation bar (tape
byte-exact + ROM-free unit tests) to the two surfaces it does not yet cover —
**pixels** and **UI wiring**. This document is the durable companion to
[PRIORITIZATION.md](PRIORITIZATION.md); the individual `issues/AUDIT-00NN-*.md`
reports remain authoritative for each defect.

Internal document (`docs/audit` is export-ignored). No ROM-derived data appears
here.

Status of this document: **plan, not yet executed.** Every file:line below was
verified against the working tree on 2026-07-14 by a nine-lane grounding sweep;
where the sweep contradicted prior notes, the correction is called out inline.

---

## 1. The reframe: "harness gap" is the wrong model for most of what's left

The prior handoff filed the remaining items under "needs a harness I don't
have." Grounding shows that is true for only a minority. The residue splits four
ways, and only one is real infrastructure:

| Category | Items | Reality |
| --- | --- | --- |
| **Not blocked** — logic is pure/unit-testable, or a red gate just needs an honest fixture | 0017, 0015, datathief(0066), cores of 0060 / 0036 / 0024, the 0003 call-site move | Deferred for care, not capability. Doable solo now. |
| **Real infra worth building** | backend-mock PPM cadence harness (0003), synthetic frame-counter generator (0040), optional ImGui Test Engine lane | Genuine engineering, smaller than it sounds — most pieces half-exist. |
| **Owner credentials** — no harness substitutes | 0052 signing private key, Apple cert | Scaffold fail-closed; owner supplies the secret. |
| **Owner hardware / gameplay** — no harness substitutes | 0040 GLES device, 0025 controller, 0022 re-exec, 0003 Metal, release gameplay | Physically un-automatable on this host. |

The strategic prize is not any one item — it is that the session's validation bar
covers sim and logic but not render or UI. The five open items are the forcing
function for extending it.

---

## 2. Authoritative open-item status (verified 2026-07-14)

A full 73-file sweep of `issues/AUDIT-00NN-*.md` `| Status |` rows gives **60
Fixed / 7 Deferred / 6 Open**. The genuinely-open set is:

| ID | Sev/Pri | Ledger says | Verified reality |
| --- | --- | --- | --- |
| **0017** | S3/P1 | Open | Open. 4 route fixtures stale after FID-0117 root-motion fix. Currently red in `port_campaign_route_smoke`. |
| **0060** | S3/P1 | Open | Open. Launcher discards `--rom/--level/--difficulty/--savedir`. Core is a pure ROM-free parser (not harness-gated). |
| **0015** | S3/P2 | Open | Open. `build_hashtable.sh` exits 0 on failure; also `${OPTARG,,}` breaks on the host bash 3.2.57. A lying gate. |
| **0024** | S3/P2 | Open | Open. System hotkeys render as billion-range `SliderInt`. Conflict primitives already extracted; residual is the SDL capture widget. |
| **0040** | S3/P2 | Open | Open. GLES-only stale back-buffer in the BMP screenshot path. Code-fixable solo; runtime verify needs handheld. |
| **0003** | S2/P2 | **Open (STALE)** | **Crash already fixed** in `6c1a23d`. Only a call-site move + backend-mock test remain. Ledger entry is the one that contradicts code. |

Adjacent, not in the open set:

- **0036** (S4/P2) is **Deferred**, not Open — but its tri-state *core is
  headlessly testable*, contradicting the deferral note. Scoped in Phase B.
- **0052 / 0025 / 0022** are **Fixed (core)** with an explicit owner-verifiable
  tail in each Status line. Treated as "core done, owner smoke pending."
- The `README.md` "Open Issues" index lists all 73 rows with **no Status
  column** — that index is the stale surface the memory warns about, not the
  per-item files.

**Correction to prior notes:** the 5 red campaign routes are **4 + 1**, not one
class. Four are AUDIT-0017 root-motion staleness; the fifth
(`bunker1_datathief`) is a stale AUDIT-0066 fixture introduced by *our own*
session-1 dump-path relocation. They are tracked separately below.

---

## 3. Phase overview

| Phase | Theme | Items | Who | Depends on |
| --- | --- | --- | --- | --- |
| **A** | Trustworthy green CI + honest ledger | train_aperture gate · datathief fixture (0066) · **0017** ×4 routes · **0015** gate rewrite · ledger hygiene | me | — |
| **B** | Pure app-logic cores (ROM-free ctests) | **0060** parser · **0036** tri-state · **0024**-core predicates | me | — |
| **C** | Render-capture correctness (solo code + WebGPU/GL verify) | **0003** move + backend-mock test · **0040** GLES fix + synthetic generator | me (owner: Metal, GLES) | A |
| **D** | Release attestation scaffold (fail-closed) | **0052** `--sign-manifest` minisign + verify + ephemeral ctest | me scaffolds; owner keys | `brew install minisign` |
| **E** | Interactive UI wiring + owner handoff | 0060 seed · 0024 widget · 0036 status · optional Test Engine · owner tail | me (UI) + owner | B, D |

```
A ─┐
   ├─► C ──┐
B ─┼───────┼─► E
   │       │
D ─┴───────┘
```

**Start with A.** A red CI silently erodes every per-commit "all green" claim,
and A closes 0017 (P1) and 0015 as a side effect. **B is the sleeper** — it
closes the correctness of 0060 (P1), 0036, and 0024 at unit-test cost, and it is
what makes E worth doing (the Test Engine needs testable state to assert
against). C and D are mutually independent. E is last: it depends on B's cores
and D's scaffold and holds the irreducible owner/hardware tail.

---

## 4. Phase A — Trustworthy green CI + honest ledger

Goal: `cd build && ctest --output-on-failure` is green with a real ROM, and the
ledger stops lying. Effort: ~1–1.5 days (dominated by the bunker1 route
re-authoring).

### A1 — train_aperture threshold recalibration

- **File:** `tools/train_faithful_aperture_regression.sh` (`WINDOW_MIN_DELTA=300`
  at :45, `WINDOW_ROI="265 150 565 270"` at :44; failing check at :224-225).
- **Reality:** the test fails on exactly one of four checks — the raw-pixel
  magic-300 window-ROI gate (`window ROI changed px: 28 < 300`). The three
  trace-based checks all **pass**: room 53 admitted draw-only (`draw=[51,53]
  rendered=[51]` :194-197), sim-purity byte-identical across 119 frames
  (:200-212), opt-out control valid (:215-216). This is renderer/threshold
  sensitivity (retina downsample + VI filter + letterbox), **not** a break in the
  FID-0008 draw-only-widener logic.
- **Change:** do **not** touch `bg.c` / FID-0008 — the trace checks already prove
  it correct. Instead make check 4 an honest corroborator: pin the backend
  (`GE007_RENDERER`) and native drawable size
  (`GE007_DIAG_SCREENSHOT_NATIVE_SIZE`), express the threshold as a **fraction of
  ROI area** rather than a hand-tuned absolute, recalibrate to the actual
  WebGPU-backend delta, and demote it to a soft check that fails only on a gross
  regression (the trace checks are the load-bearing gate).
- **Verify:** checks 1–3 already green; check 4 green across 3 runs, then the
  full ctest.
- **Effort:** low (one constant + a pin).

### A2 — datathief fixture (AUDIT-0066 follow-up, not 0017)

- **File:** `tools/campaign_routes/bunker1_datathief_equipment_contract.json:26`.
- **Reality:** asserts `required_log_patterns: ["[DUMP] Wrote /tmp/ge007_dump_"]`.
  Session-1 fix `8df8b82` (AUDIT-0066) moved dumps from `/tmp` to the savedir
  (`debug_dump.c:269-270` via `savedirPath`). The harness launches with
  `--savedir <case_dir>` (`campaign_route_smoke.py:2134`), so the logged line is
  `[DUMP] Wrote <case_dir>/ge007_dump_0000.txt`; the literal `/tmp/` substring is
  never present. All four state milestones pass; only the log pattern fails. The
  match is a plain `in` test (`campaign_route_smoke.py:1702`).
- **Change (1 line):** replace with the path-agnostic `"[DUMP] Wrote "`. Because
  `--savedir` carries a trailing slash, `ge007_dump_` is not adjacent to
  `[DUMP] Wrote `; to also re-assert the filename, make it a two-element array
  adding `"ge007_dump_"`. Track under AUDIT-0066.
- **Doc collateral (same fix):** `debug_dump.c:5` header comment and
  `docs/INSTRUMENTATION.md:725` still reference `/tmp/ge007_dump_*.txt` — update.
- **Effort:** trivial.

### A3 — AUDIT-0017 recapture (4 routes)

There is **no auto-regen subcommand** for campaign routes (unlike tapes) — all
thresholds/windows are hand-authored inline in each route JSON. "Recapture" =
run under the retail-correct FID-0117 default, read the achieved values from the
tool's own failure evidence, and hand-edit the JSON.

**Shared procedure (per route):**
1. Build once (`tools/campaign_route_smoke.sh` with build, or `--no-build`
   reusing `build/ge007`).
2. Capture both states:
   - default: `tools/campaign_route_smoke.sh --no-build --route <name> --out-dir /tmp/r-default`
   - control: `GE007_NO_ANIMFRAME2_ROOTFLAG_FIX=1 tools/campaign_route_smoke.sh --no-build --route <name> --out-dir /tmp/r-optout`
3. Read `/tmp/r-default/<name>/summary.json` + failure text for achieved
   `horizontal_delta`, per-milestone deltas, and setup-target `closest`
   distances. Cross-reference `trace.jsonl` (per-frame x/y/z) against
   `stage_pads.jsonl` (pad positions) to find the frame the new sim is actually
   closest to each anchor pad.
4. Edit the route JSON; add an FID-0117 provenance note to its `description`.
5. Re-run 3× for determinism, then the 34-route gate, then the full 128-ctest.

**Global guardrails (from the acceptance criteria):**
- **No route may set `GE007_NO_ANIMFRAME2_ROOTFLAG_FIX=1` to pass** — that
  validates the wrong sim.
- Re-fit only distance **floors**; never loosen the pad-proximity or
  door/collect anchors — instead **re-time** the input window so the anchor fires
  again.
- The FID-0117 negative-control tape must stay fail-on-revert and byte-identical.
- Run sequentially — concurrent tape/route runs contend on
  `validation_acquire_runtime_lock`.

**Per-route detail** (all in `tools/campaign_routes/`):

| Route | Class | Edit | Anchors — do NOT loosen |
| --- | --- | --- | --- |
| `surface1_native_multiwaypoint_input_traversal.json` (L36, 950f) | **(a) distance-floor only** | Lower top-level `min_horizontal_delta` 7500→~6900; lower `post_input_snowfield_rest` (7500/7000/2400 @949), `late_snowfield_reach` (7300/6900/2300 @900); check `mid_snowfield_reach` (4200 @600). Inputs unchanged. | setup-target pads 80/79/77 (560/1100/1200u) — verify still pass, keep as authored. |
| `surface2_native_multiwaypoint_input_traversal.json` (L43, 1250f) | **(a)+(b)** | (a) `min_horizontal_delta` 10500→~9900; late floors `post_input_surface_rest` (10500/9400/4600 @1240), `extended_surface_reach` (10000/9000/4000 @1100), `late_surface_reach` (6500 @830). (b) Re-time tight pads 68 (max_h 180 @830) and 69 (850 @736): move each `frame_at_or_after` to the recaptured closest-approach frame, keep the radius. | pad 30 stock-spawn (5u @36), pad 71 (220u @199), pad 68 (180u) — recalibrate window, never radius. |
| `frigate_native_multiwaypoint_input_traversal.json` (L26, 900f) | **(b) re-timing (touch of c)** | Early pad chain (176/153/…/60 @1-460) still passes; only `far_deck_pad_145` (max_h 85 @653) fails at 100.95. Tune `pad150_to_forward_right_deck_push` (start 260, dur 520, forward+right) so root motion re-lands within a tight bound. Opt-out reaches 23.96, so **aim ~85, do not widen to 105**. | pad 145 must stay a genuine "reach"; lifeboat-cluster / midship pad proximities. Verify `far_deck_push`/`late_far_deck_reach` (2250/2800). |
| `bunker1_spawn_two_door_collect_contract.json` (L9, 820f) | **(c) input re-authoring (hardest, ~½ day)** | Door 138 gets `allow` + state 0→1 but never "finish open" — FID-0117 shifted the approach so at the scripted press Bond is mis-faced or standing in door 138's own sweep. Re-choreograph: `second_door_turn_setup` (look_right 300-480), `second_door_approach` (forward 380-525), `second_door_face_adjust` (look_left 525-565), `second_door_open_press` (b 545-551). May also retime `first_door_open_press`/`next_room_collect_press`. | All 10 `log_count_assertions` (door 140 allow/0→1/displace≥45/finish; collect begin + free objtype=20; door 138 allow/0→1/displace≥30/finish); 3 setup pads (16@60u, 10052@110u, 14@70u); `post_second_door_survival` (active_stage=9, bond_kia=0, mission_failed=0). **Keep `GE007_FIRE_RATE_AUTHENTIC=0` (FID-0066).** |

**Effort:** surface1 ~30min, surface2 ~1–2h, frigate ~1–2h, bunker1 ~½ day.

### A4 — AUDIT-0015 fail-closed gate rewrite

- **File:** `scripts/make/build_hashtable.sh` (entire fix surface; last touched
  `8de295b`, pre-fix).
- **Five confirmed defects:** (1) `${OPTARG,,}` (:21-33) is bash-4-only —
  host `/bin/bash` is 3.2.57, so a valid `-v u` yields "bad substitution" without
  aborting and leaves `COUNTRY_CODE` empty; (2) `OUTFILE="full_hashtable_${version}.csv"`
  (:54) references undefined `version` (arg is in `ARG_VERSION`/`COUNTRY_CODE`) →
  always emits `full_hashtable_.csv`; (3) seven `for`-loops (:68,88,108,122,136,150,164)
  iterate `.../*.o` with no `nullglob` → run on the literal `*.o` on an empty
  tree; (4) `objcopy`/`md5sum` never status-checked, no `set -e`; (5) `rm -f`/`touch`
  before any input is proven (:59,61) and the final `rm -f ${TMP}` (:178) exit
  status becomes the process exit → **false success**. All five reproduced on
  this host with a `/usr/bin/true` objcopy shim.
- **Fail-closed change:** portable version parse (a `case` or `tr`, or bump
  shebang to `/usr/bin/env bash`); normalize `-v` to one canonical var and
  **reject** unsupported/missing values with `exit 2` before any output; derive
  the default filename from that validated code (`full_hashtable_u/j/e.csv`);
  `set -euo pipefail`; `shopt -s nullglob` or a `find`-built array; require ≥1
  object per required class; status-check every extractor/checksum; write to a
  temp file and `mv` atomically only after full validation; validate each row
  (32-hex digest, section, non-wildcard path); exit nonzero leaving **no** CSV on
  any failure.
- **Keep compatible / verify only:** `rebuild_allver_hashtables.sh` and
  `local_template/checksum.sh` both pass explicit `-o`; preserve the
  `md5,section,path` row format consumed by `scripts/test_files.sh` (`IFS=','`).
- **New test (net-new; no ctest covers this script):** a synthetic-ELF tree with
  the relevant sections for u/j/e compared against an independently computed
  oracle (row count + digests), plus negative fixtures — unsupported version,
  missing dir, empty required class, extractor failure (`/usr/bin/true` shim),
  checksum failure, unwritable dest — each asserting nonzero exit **and** no
  completed CSV. Gate: ShellCheck clean (present at `/opt/homebrew/bin/shellcheck`)
  **and** a macOS system-bash 3.2 execution test.
- **Docs to flip:** `issues/AUDIT-0015-*.md` (Open→Resolved), `README.md:62`,
  `PRIORITIZATION.md` (:159/199/248).
- **Effort:** moderate (~180-line rewrite + fixture harness; no engine/sim risk).

### A5 — Ledger hygiene

- **Flip AUDIT-0003 status:** the issue file still says "Open" and cites a live
  SIGSEGV at the old `glReadPixels` line 24024. The crash is fixed
  (`gfx_pc.c:24124-24127`, `[AUDIT-0003]` anchor). Change Status to
  `Fixed (crash; frame-correctness/visual-diff owner-verifiable)`, update the
  stale crash language, and correct `PRIORITIZATION.md:116/243`.
- **Add a Status column** to the `README.md` "Open Issues" index so it stops
  reading as all-73-open.
- **Reconcile the route count:** `issues/AUDIT-0017-*.md:28-29` says "34 routes,
  30 passed, 4 failed"; current reality is 29 passed / 5 failed because AUDIT-0066
  introduced the datathief failure. Note datathief belongs under AUDIT-0066, not
  0017.

---

## 5. Phase B — Pure app-logic cores (ROM-free ctests, no new infra)

These mirror the existing `env_ownership` / `binding_conflict` / `arg_triage`
extractions: an SDL-free/ROM-free TU under `if(MGB64_APP)` / `if(BUILD_TESTING)`
with a `CHECK`-counter test (never `assert()` — the ctest build is Release
`-DNDEBUG`, which strips it). Effort: ~2–4 days total.

### B1 — AUDIT-0060 `launch_intent` parser (P1)

- **Today:** the only argv inspection is `arg_triage.cpp:34`
  `mgb_is_automation_invocation()`, which classifies launcher-vs-headless and
  parses no values. `main_app.cpp:124` constructs `Launcher launcher;` with no
  argc/argv; the loop (:213-227) calls `launcher.draw(host)` and never reads
  argv. So `--rom/--level/--difficulty/--savedir` are silently dropped on the
  default `MGB64_APP` path — exactly AUDIT-0060.
- **The canonical value parser already exists** but is reachable only via the
  headless path: `main_pc.c:750-941` validates every interactive flag (`--rom`
  :752; `--faithful/-hd/--remaster` :762-767; `--mission` 1-20 :846; `--level`
  slug|name|raw with disambiguation :854-883; `--stage-id` :884; `--difficulty`
  :887; `--savedir` :901; `--multiplayer/--players` 2-4/`--mp-stage/--scenario/--mp-timelimit`
  :903-937; positional ROM :938-939). The reverse codec `mgb64_engine_boot`
  (`main_pc.c:1344`) synthesizes argv from `MgbBootConfig` (`engine_entry.h:30-40`).
- **The one design decision — table single-sourcing.** The slug/token tables are
  file-static in `main_pc.c`: `kPcStartStages[20]` (:140-168), `kPcMpStages`
  (:309-322), `kPcMpScenarios` (:330-344), plus `pcParseDifficultyArg` /
  `pcParseIntArg` / `pcFindStageBy*`. `ui_launch.cpp:25-34` **already duplicates**
  the 20-entry level-slug table (a pre-existing divergence). Preferred (faithful,
  matches `binding_conflict`): extract these into a new SDL-free/ROM-free C TU
  (`src/app/cli_stage_tables.{h,c}`) linked by both `main_pc.c` and
  `launch_intent.cpp`, killing the `ui_launch.cpp` duplicate. Cheaper fallback:
  duplicate the small tables (adds a third copy). Either way, **3-way-diff any
  table against retail asm** (this class of gotcha caught an equip[60]
  off-by-one).
- **Deliverable:** `src/app/launch_intent.{h,cpp}` —
  `bool parseLaunchIntent(int argc, char** argv, LaunchIntent& out, std::string& err)`.
  The struct tracks **presence per field** (`std::optional`/`has_*`), not
  sentinel-vs-default, because the AC requires "explicit CLI values override
  remembered prefs **for that run**" — seeding must apply only fields actually
  present.
- **Test:** `tests/test_launch_intent.cpp`, table-driven, wired ~`CMakeLists.txt:1911`.
  Cases: each flag→field; slug/display-name/raw-LEVELID level forms; every
  difficulty token; presets; mp/players bounds; positional ROM; invalid→`false`+err;
  conflicting (`--level` + mismatched raw id, players out of 2-4)→reject.
- **Unmodeled flags (policy):** `--scenario`, `--mp-timelimit`, `--stage-id`,
  MP difficulty, and `--savedir` have no `MgbBootConfig`/launcher slot.
  Recommended minimal-faithful first cut: fully seed the flags the launcher
  models (rom, level, difficulty, preset, multiplayer, players, savedir), and
  have the parser **explicitly error** on the unmodeled ones rather than silently
  drop them; widen `MgbBootConfig` + UI as a follow-up. Keep the
  `--no-ui`/automation path untouched (byte-identity — it is routed by
  `arg_triage` before any new parsing).
- **Effort:** parser + ctest small (on par with `env_ownership`). The seed wiring
  is Phase E.

### B2 — AUDIT-0036 `ConfigSaveResult` tri-state

- **The conflation:** `config_pc.c:685` `configSave()` returns `1` on **suppressed**
  (:694-696, faithful/remaster) **and** `1` on real success (:761), while
  returning `0` on `fopen`/`fclose`/replace failure (:704/743/750/756).
  Suppressed is indistinguishable from saved at the return boundary. The result
  is then dropped at every layer: `configStagingApply()` is `void` and discards
  the return (`config_schema.c:134/146`); `ui_settings.cpp:236` ignores
  `mgb_config_save()`; Apply sets `SETTINGS_APPLIED` unconditionally (:220-223);
  `ui_overlay.cpp:64` auto-applies on close with no feedback.
- **Core deliverable (headless, ROM-free):** add
  `typedef enum { CONFIG_SAVE_OK=0, CONFIG_SAVE_SUPPRESSED, CONFIG_SAVE_FAILED } ConfigSaveResult;`
  to `config_pc.h`. Prefer adding `configSaveResult()` and keeping `configSave()`
  as a 0/1 wrapper — this preserves the CLI `--config-set` nonzero-on-failure
  contract (`config_pc.c:764/824`). Map :694→SUPPRESSED, the fail branches→FAILED,
  :761→OK; thread the result up through `configStagingApply()` (make it return the
  enum) and expose via `mgb_config_save`/a new `mgb_config_apply`.
- **Test:** `tests/test_config_save_result.c` cloning the `test_config_staging`
  link set (`CMakeLists.txt:1959-1967`): assert SUPPRESSED after
  `configSetSaveSuppressed(1)`, FAILED with an unwritable savedir, OK on a
  writable temp savedir. This contradicts the deferral note's "requires
  interactive UI validation" — a meaningful testable slice exists below the UI.
- **Residual (Phase E):** expand `SettingsResult` (`ui_settings.h:8`), show a
  status line under Apply, add Retry that keeps edits, and stop `ui_overlay.cpp:64`
  from swallowing a FAILED on close.
- **Effort:** core ~0.5 day.

### B3 — AUDIT-0024 core predicates

- **Locations:** `platform_sdl.c:2255-2269` registers `Input.MenuToggleKey`
  (KEYCODE, default `SDLK_F1`), `Input.FpsToggleKey` (KEYCODE, `SDLK_F10`),
  `Input.MenuToggleButton` (button index, `BACK=4`). `ui_settings.cpp:93` renders
  every non-bool int via `SliderInt`, so all three become billion-range sliders;
  `ui_bindings.cpp` iterates only the gameplay tables and never the three system
  actions.
- **The conflict/ownership policy is already pure and tested:**
  `binding_conflict.{h,c}` (`bindingOwnerOf`, ctest `binding_conflict`) and
  `gp_reserved.{h,c}` (`gpButtonReserved`, `gpMenuConflict`, ctest
  `gamepad_menu_conflict`). So most of 0024's *logic* is already covered.
- **Namespace hazard:** gameplay keyboard bindings are **scancodes**; the system
  menu/fps hotkeys are **keycodes** (`SDLK_F1`, consumed via `SDL_GetKeyName`
  `ui_overlay.cpp:184`). A keyboard conflict *between* a system hotkey and a
  gameplay binding needs `SDL_GetScancodeFromKey` (an SDL call, not pure) — so
  only menu-key-vs-fps-key mutual exclusion and menu-button-vs-gameplay-gamepad
  (already covered) are purely testable.
- **Core deliverable (~0.5 day):** (1) expose a pure `gamepadButtonName(int)`
  over the static `kGpButtonName[SDL_CONTROLLER_BUTTON_MAX]` (`input_bindings.c:190-198`)
  so `MenuToggleButton` renders a name; (2) a tiny SDL-free predicate module —
  `int sysKeyValid(int keycode)` (nonzero, in `0..0x40000FFF`) and
  `int sysKeyMutualConflict(int candidate, int other)` (menu vs fps must differ);
  reuse `bindingOwnerOf`/`gpMenuConflict` for menu-button-vs-gameplay rejection.
  Test `tests/test_sys_hotkey.c` mirroring `test_binding_conflict.c`.
- **Residual (Phase E):** the SDL-coupled press-to-bind capture widget and slider
  removal.

---

## 6. Phase C — Render-capture correctness (solo code + WebGPU/GL verify)

Two **distinct** paths that share a theme but touch different files: the
screenshot **series/PPM** path (`gfx_diag_screenshot_series_capture_if_due()`,
static in `gfx_pc.c:24036`) is 0003; the **manual/auto BMP** path
(`platformSaveScreenshot()`, `platform_sdl.c:814`) is 0040. Effort: ~3–5 days.
Depends on Phase A (green baseline to diff against).

### C1 — AUDIT-0003 call-site move + backend-mock cadence test

- **Crash is already fixed** (`6c1a23d`): the raw `glReadPixels` was replaced by
  `gfx_backend_read_framebuffer_rgb` (`gfx_pc.c:24127`, def :24029 routing
  GL→`glReadPixels`, Metal→blit, WebGPU→scene-tex copy) with a no-partial-file
  guard (:24128-24132).
- **Remaining = the call-site move.** The series capture is called at
  `gfx_pc.c:24346` — **before** `end_frame()` (:24386) and **before** the minimap
  (`minimap_overlay_draw_queued_frames`, :24392-24393, `#ifdef NATIVE_PORT` ends
  :24394). GL framebuffer-state analysis proves the move is both correct and
  necessary: `gfx_opengl_end_frame` (:4145) resolves the scene FBO into default
  0, sets `g_scene_target_bound=false` (:4142), applies the VI filter (:4147),
  then the minimap draws into FBO 0. Before the move `g_scene_target_bound` is
  still true, so the readback captures the raw scene **without** post-FX or
  minimap; after, it reads FBO 0 = scene+post-FX+minimap. All three backends land
  on the composited final frame after `end_frame` (WebGPU persistent `s_scene_tex`
  :449 with minimap drawn in at :566-567; Metal composites at :3098-3174).
- **Change:** delete the call at :24346, re-insert immediately after the minimap
  `#endif` at :24394 (before `gfx_trace_glass_shard_coverage_frame_end` :24395);
  leave the `g_gfxRecoveryActive=0` clear (:24347-24350) in place. **Does not
  change cadence** — accounting keys on `g_frame_count_diag`/`g_BgCurrentRoom`,
  stable across `end_frame`. ~1 line + comment.
- **Backend-mock cadence/failure test:** extract the cadence gate +
  write/accounting from the static function into a small non-static ROM-free/GPU-free
  TU (`src/platform/fast3d/screenshot_series.c`) that takes a **readback function
  pointer** + frame/room inputs. `tests/test_screenshot_series.c` mocks readback
  (success + failure) and asserts: correct `AFTER_FRAME`/`EVERY`/`LIMIT`/room
  cadence, written-count increments only on a durable full file, failed readback
  writes no file. Wire like `CMakeLists.txt:819-824`. **This is the "backend-mock
  harness" the audit asks for.**
- **Verify:** solo = WebGPU (default) and GL (`GE007_RENDERER=gl`) captures
  include post-FX + minimap with correct top-left orientation and valid P6
  headers; backend-mock ctest green. **Owner:** Metal 5-frame series (exactly 5
  valid non-blank P6 PPMs).
- **Watch-outs:** after the move the readback runs past the recovery gate
  (acceptable — the function handles readback failure); the one-shot
  `GE007_SCREENSHOT` block (:24354-24384, frame 30) has the same pre-`end_frame`
  defect but is out of 0003's scope; do not alter the PPM/BMP flip conventions
  (:24156).

### C2 — AUDIT-0040 GLES fix + synthetic frame-counter generator

- **Root cause (GLES-only):** `platform_sdl.c:878-891`. Desktop GL reads
  `GL_FRONT` (:886, restored :890) because `platformSaveScreenshot` runs at the
  **top** of `platformFrameSync` (:3607-3634) — after the previous frame's
  `SDL_GL_SwapWindow` (the swap is in `gfx_end_frame`, `gfx_pc.c:25113`, GL-only).
  Under `MGB64_PORTMASTER_GLES` both `glReadBuffer` calls are `#ifndef`-excluded,
  leaving the read buffer at `GL_BACK`, which is **undefined post-swap** (comment
  at :884). Metal/WebGPU already bypass this via `gfx_backend_read_framebuffer_rgb`
  (:861-874), so this is strictly a GLES BMP defect.
- **Change (code solo; runtime verify owner-only):** the GLES branch must obtain
  a **defined** frame. Prefer the offscreen-final-frame model (mirrors WebGPU
  `s_scene_tex`) — capture a dedicated copy of the composited final frame — so the
  BMP includes minimap + post-FX and matches desktop/Metal orientation. (The
  cheaper route, `gfx_opengl_read_framebuffer_rgb` which is GLES-safe, reads the
  scene FBO and misses the minimap.) Preserve the screenshot-frame + gameplay-timer
  semantics (:3616-3630); add no extra swap or 1-frame delay.
- **Synthetic frame-counter color-pattern generator (NEW, none exists):** a
  `GE007`-gated hook that fills the final frame to a color deterministically
  encoding `g_frameSyncCallCount` (ROM-free, no geometry). Capture consecutive
  frames on desktop GL/WebGPU and assert the decoded counter + orientation +
  dimensions via the existing `tools/compare_screenshots.py` +
  `tools/audit_screenshot_health.py` (both already py_compile-gated in ctest).
  **The same test binary is what the owner re-runs on a real GLES/PortMaster
  device** — the only owner-gated step.
- **Verify:** solo = WebGPU + GL synthetic captures decode correctly with valid
  headers; **owner** = real GLES device shows consecutive distinct frames (no
  stale/lag) and Metal 5-frame series.

---

## 7. Phase D — Release attestation scaffold (fail-closed)

Depends on `brew install minisign` (absent on this host — see risks). Effort:
small–medium.

### D1 — AUDIT-0052 `--sign-manifest` (minisign)

- **Core already landed:** `stamp_provenance.sh` writes per-asset
  `<asset>.provenance.json`; `verify_provenance.sh` fails closed at publish and
  emits `SHA256SUMS` + `manifest.json` (`release.sh:176-185`); ctest
  `release_provenance_guard` (`test_release_provenance.sh`, 13/13, ROM-free).
- **The remaining gap:** `manifest.json` and `SHA256SUMS` are **unsigned** —
  nothing cryptographically binds them to the owner, and there is no user-side
  download verifier. An attacker who substitutes a binary and regenerates a
  matching manifest passes every current check.
- **Tool choice — minisign, decisively.** minisign (Ed25519, single static
  binary) signs **locally** inside `release.sh` (which already builds macOS on the
  owner's Mac): the private key never leaves the machine, never a GitHub secret,
  never in CI; the committed public key is a git-history trust anchor; verification
  is offline — a 1:1 match to the stated posture. cosign-**keyless** requires
  `id-token: write` (Fulcio + Rekor, moves signing into CI, inverts the posture —
  every workflow here is `contents: read`). SLSA requires `id-token: write` +
  `attestations: write` and asserts "built by this CI runner," but the
  authoritative macOS build is the owner's laptop. cosign key-mode is
  OCI/registry-heavy. **So minisign is solo; cosign/SLSA need owner
  authorization (a posture change) and are out of scope for a solo agent.**
- **Buildable solo (all inert/self-contained — no key, no secret, no id-token):**
  1. `--sign-manifest` flag + fail-closed scaffold in `release.sh` mirroring the
     `--sign` template at :71-78:
     `: "${MGB64_MANIFEST_SIGNING_KEY:?--sign-manifest requires a minisign secret key path}"`
     + a `command -v minisign` guard, **before** the build. Inert when the flag is
     absent.
  2. Signing wiring: after `verify_provenance.sh` emits `manifest.json` (:178-185),
     `minisign -Sm manifest.json` → `manifest.json.minisig`; add `*.minisig` to
     the publish set (:191) while keeping `*.provenance.json` **excluded** (the
     `c1c035d` sidecar-hygiene discipline).
  3. NEW user-side `scripts/release/verify_release.sh`
     (`minisign -Vm manifest.json -P <committed pub>` then `sha256sum -c SHA256SUMS`)
     — none exists today.
  4. Ephemeral-key ctest `tools/tests/test_manifest_signing.sh` modeled on
     `test_release_provenance.sh`: `minisign -G` a throwaway keypair in a tempdir,
     sign a fixture manifest, verify-accept, then tamper and assert verify
     **fails** — the full sign→verify→reject contract with zero owner secrets.
     Register beside `release_provenance_guard`.
  5. Docs: a `RELEASING.md` user-verify section + the AUDIT-0052 resolution
     update.
- **Strictly owner-only (a subagent must NOT do these):** `minisign -G` of the
  **long-term** keypair on the owner's Mac; committing the **authoritative**
  public key (a solo agent may only commit a placeholder clearly marked
  NOT-THE-RELEASE-KEY); signing a real manifest; the end-to-end publish
  (`gh` + pushed tag + `--confirm-gameplay`).
- **Risks:** minisign is **absent locally** — the ctest must `command -v minisign
  || skip`, and **no "green" claim is valid until `brew install minisign`**.
  Follow the assert-stripped rule (fail-counter, never `assert()`). Handle the
  passphrase: minisign secret keys are passphrase-protected; support
  `MINISIGN_PASSWORD`-style non-interactive use for the ctest's ephemeral key
  while leaving the real owner flow interactive/env-gated.

---

## 8. Phase E — Interactive UI wiring + owner handoff

Depends on B (cores to wire) and D (scaffold to key). This phase holds the
irreducible owner/hardware/credential tail.

### E1 — AUDIT-0060 launcher seed (from B1)

After the `arg_triage` gate returns on the interactive path, `main_app.cpp` calls
`parseLaunchIntent(argc, argv)`; on failure it prints `err` and returns nonzero
(AC: invalid args → actionable error + nonzero); on success it calls a new
`Launcher::seed(const LaunchIntent&)` that writes `LauncherState` fields **and**
sets the matching `*Initialized` flags true so the lazy `*_ensureInit` calls do
not clobber CLI values with `AppConfig`. Maps level (slug/name/mission →
`launchLevelIndex` 1..20), difficulty, preset, multiplayer/players, rom (validate
via `mgb_validate_rom`), savedir. **Owner-verifiable:** an app-smoke that asserts
the exact argv passed to `mgb64_headless_main` after "Play."

### E2 — AUDIT-0024 capture widget + slider removal (from B3)

Add a press-to-bind capture widget for the three system actions (new "System"
rows in `ui_bindings.cpp`, or a schema kind `MGB_CFG_KEYBIND`/`MGB_CFG_PADBUTTON`
dispatched in `ui_settings.cpp`) and remove the three ints from the generic
`SliderInt` path (:93). The widget uses `SDL_GetKeyName`/keycodes (system keys are
keycodes, distinct from the gameplay scancode tables). Persist already exists
(`mgb_config_set_int` + `gamepadBindingReconcileMenu`). **Owner/Test-Engine:**
capture/cancel/reset/name-render, keyboard-only and pad-only navigation — this
widget polls SDL hardware state directly and has **no injection seam**.

### E3 — AUDIT-0036 status surfacing (from B2)

Expand `SettingsResult`, show a concise status line under Apply/Save
("Saved to ge007.ini" / "Applied to this run only — faithful session" /
"Couldn't save — check permissions" + Retry that keeps edits), and stop
`ui_overlay.cpp:64` from silently discarding a FAILED on overlay close.
Retry-after-writable and not-closing-on-failure are interactive → owner/Test-Engine.

### E4 — (Optional) Dear ImGui Test Engine lane

Vendor `imgui_test_engine` behind `MGB64_UI_TESTS` on a GL/Linux CI lane. ImGui
1.92.9 is single-context/thread and the Test Engine hooks the **context**, not
the backend, so it is feasible (~3–5 days to vendor + wire; ~15k-LOC dependency +
a recompiled-imgui lane). **Do this only after B proves how much residual
UI-wiring risk actually remains.** One hard blocker: press-to-bind reads SDL
hardware state directly, so the engine cannot drive literal key-capture without
an `SDL_PushEvent` injection seam — or that one keystroke is left to an owner
manual check.

### E5 — Owner-only handoff checklist

No harness closes these; the best a solo agent does is scaffold them fail-closed
and hand over an exact checklist:

- [ ] **0052** — `minisign -G` the long-term keypair; commit the authoritative
      public key; sign a real release manifest; end-to-end publish.
- [ ] **0025** — real controller: verify runtime-apply + UI feedback of the
      dynamic menu-button reservation.
- [ ] **0022** — one re-exec smoke (fresh relaunch inherits a new shell value).
- [ ] **0003** — Metal 5-frame series capture (exactly 5 valid non-blank PPMs).
- [ ] **0040** — real GLES/PortMaster device: consecutive distinct frames, no
      stale/lag.
- [ ] **Release** — macOS + Windows gameplay attestation (`--confirm-gameplay`).
- [ ] **Apple** — signing/notarization enrollment decision (currently deferred).

---

## 9. Global guardrails & gotchas

- **ctest is Release `-DNDEBUG` → `assert()` is stripped.** Every new test uses a
  `CHECK`-counter that returns nonzero from `main()`. Never rely on `assert()`.
- **Concurrent tape/route runs contend** on `validation_acquire_runtime_lock` —
  run route recaptures and tape checks **sequentially**.
- **3-way-diff every fidelity/stage table against retail asm** before trusting
  it — this class of gotcha has caught an equip[60] off-by-one and the
  `ui_launch.cpp` slug duplicate.
- **ROM-gated port-validation smokes skip without a ROM**, so fixture staleness
  is latent — the datathief regression slipped past a "88/88 green" claim. Only a
  **full local run with a real ROM** exercises `port_campaign_route_smoke` and
  friends.
- **Verify-first.** The `README.md` index reads as all-73-open and AUDIT-0003 is
  mislabeled Open — trust the per-item Status rows and the code, not the index.
- `docs/audit` is **export-ignored / internal**; keep ROM-derived data out.

## 10. Verification ladder

| Tier | What | Runs where |
| --- | --- | --- |
| Unit (ROM-free ctest) | `launch_intent`, `config_save_result`, `sys_hotkey`, `screenshot_series` backend-mock, `manifest_signing` (post-install), `build_hashtable` oracle | CI-portable, any host |
| Integration (ROM-gated, sequential) | 34-route campaign gate, `train_faithful_aperture`, full 128-ctest | Local, owner ROM |
| Solo runtime (this Mac) | 0003 capture composition, 0040 synthetic frame-counter — WebGPU default + `GE007_RENDERER=gl` | This host |
| Owner / hardware / credential | Metal capture, GLES device, real controller, re-exec, signing key, release gameplay, Apple | Owner only |

## 11. What stays permanently owner-only

The signing private key, Apple enrollment, and real Windows / macOS / handheld /
controller gameplay verification. These are not harness gaps — no harness closes
them. Everything above them in the ladder is closeable solo.
