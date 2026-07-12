# Post-boot In-Game UX — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the post-boot experience feel like a real game — a single Start-driven pause hub, settings that stage-then-apply (never mutate the live game mid-drag), device-aware button hints, a rebindable menu toggle, a quick FPS-overlay hotkey, and the in-game window inheriting the launcher's fullscreen (already landed).

**Architecture:** The app shell (ImGui/C++, `src/app/`) and the engine (C, `src/platform/`+`src/game/`) share one SDL window; the seam is `AppOverlayHooks` + `platformSetHostWindow`. We keep that seam. Settings gain a **staging layer** in the config module (edit a staged copy, commit on Apply/Resume). The existing controller-navigable overlay becomes the **single hub**; "Watch/Inventory" delegates to the engine's retail watch rather than re-implementing it. Start is rerouted from the engine watch to the overlay.

**Tech Stack:** C11 (engine + platform), C++17 (app shell), Dear ImGui, SDL2, CMake + CTest. Settings via `src/platform/settings.c` / `config_pc.c` / `config_schema.c`; overlay in `src/app/ui_overlay.cpp` / `ui_settings.cpp`.

## Global Constraints

- **Faithful-by-default & determinism:** no change may alter sim state under `--deterministic`, `--screenshot-frame`, or `GE007_BACKGROUND` (automation must stay byte-identical). New UI/overlay behavior is host-side only and must not touch `g_ClockTimer` beyond the existing solo-pause path.
- **MP never pauses** (`lvl.c:2105`): all pause/staging work must keep MP running; staging is what makes mid-MP settings edits safe.
- **Warnings budget 0:** `tools/summarize_build_warnings.py --max-total 0` must stay green (`-Werror`-equivalent gate).
- **Keep an always-available opener:** the F1/Back overlay toggle must keep working through every phase (never leave the user with no way into the menu).
- **No new external deps.** ImGui + SDL2 only.
- **Env/settings hygiene:** any new registered setting regenerates `docs/ENV_FLAGS.md` (`tools/gen_env_reference.py --out docs/ENV_FLAGS.md`) and keeps `env_reference_current` + `app_config` + `settings_menu_model` ctest lanes green.
- **Branch:** all work on `dev/post-v0.4.0-backlog` (isolated from the v0.4.0 release at `main`).

---

## File Structure

| File | Responsibility | Phase |
|---|---|---|
| `src/game/pc_fps_overlay.c` / `platform_sdl.c` | FPS overlay toggle already lives here; add a hotkey path | 1 |
| `src/app/ui_overlay.cpp` | Overlay toggle (currently hardcoded F1/Back → make rebindable); Start routing; hub menu; button-hint footer | 2,3,5 |
| `src/platform/settings.c` / `settings.h` | Add a bindable "action" concept for the menu-toggle + FPS-toggle keys/buttons | 2 |
| `src/app/ui_settings.cpp` | Render device-aware button glyphs; a Keybinds view; drive the staging UI (Apply/Cancel/Preview) | 3,4 |
| `src/platform/config_schema.c` / `config_pc.c` | **Staging layer**: staged copy of values, commit/discard API | 4 |
| `src/game/mp_watch.c` + `ui_overlay.cpp` | Reroute Start → overlay; overlay "Watch/Inventory" raises the engine watch | 5 |
| `tests/test_app_config.cpp` (+ ctest) | Config-staging round-trip test | 4 |
| `docs/ENV_FLAGS.md` | Regenerated when settings change | 2,4 |

---

## Phase 1 — FPS overlay quick-toggle hotkey (safe, isolated)

### Task 1: A dedicated hotkey toggles `Video.FpsOverlay` without opening the menu

**Files:**
- Modify: `src/app/ui_overlay.cpp` (event handling in `onProcessEvent`, near the F1 toggle ~`:70`)
- Reference: `src/platform/platform_sdl.c:1889` (`g_pcFpsOverlay` global + `Video.FpsOverlay`)

**Interfaces:**
- Consumes: `g_pcFpsOverlay` (extern `s32`), the SDL keydown event already reaching `onProcessEvent`.
- Produces: nothing new for later tasks (self-contained).

- [ ] **Step 1: Add the hotkey handler.** In `onProcessEvent`, alongside the F1 overlay toggle, add a branch for a distinct key (default **F10**) that flips `g_pcFpsOverlay` and writes it back through config so it persists:

```cpp
// FPS overlay quick-toggle (does NOT open the menu). Default F10.
if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_F10 && !e->key.repeat) {
    extern int g_pcFpsOverlay;                 // src/platform/platform_sdl.c
    mgb_config_set_int("Video.FpsOverlay", g_pcFpsOverlay ? 0 : 1);
    return true;                               // swallow so it doesn't reach the game
}
```

- [ ] **Step 2: Build.** Run: `cmake --build build --target ge007 -j`. Expected: `Built target ge007`, zero warnings.
- [ ] **Step 3: Gameplay-validate (owner or manual).** Boot a level, press F10 repeatedly: the FPS box in the top-right toggles on/off without opening the overlay. `--deterministic` unaffected (the hotkey path is host-side; `pc_fps_overlay.c:111` still force-suppresses under automation).
- [ ] **Step 4: Commit.**

```bash
git add src/app/ui_overlay.cpp
git commit -m "feat(overlay): F10 quick-toggles the FPS overlay without opening the menu"
```

---

## Phase 2 — Rebindable menu/FPS toggle (settings-backed)

### Task 2: Register the menu-toggle key/button + FPS-toggle key as real settings

**Files:**
- Modify: `src/platform/platform_sdl.c` (`platformRegisterConfig`, near the other `Input.*` registrations ~`:2166`)
- Modify: `src/app/ui_overlay.cpp` (read the configured key/button instead of the hardcoded `SDLK_F1` / `SDL_CONTROLLER_BUTTON_BACK`)
- Modify: `docs/ENV_FLAGS.md` (regenerated)

**Interfaces:**
- Produces: `s32 g_cfgMenuToggleKey` (SDL scancode/keycode), `s32 g_cfgMenuToggleButton` (SDL_GameControllerButton), `s32 g_cfgFpsToggleKey`. Consumed by `ui_overlay.cpp`.

- [ ] **Step 1: Add globals + registrations.** In `platform_sdl.c`:

```c
s32 g_cfgMenuToggleKey    = SDLK_F1;                         /* keyboard opener */
s32 g_cfgMenuToggleButton = SDL_CONTROLLER_BUTTON_BACK;      /* gamepad opener */
s32 g_cfgFpsToggleKey     = SDLK_F10;
```
```c
settingsRegisterInt("Input.MenuToggleKey", &g_cfgMenuToggleKey, SDLK_F1, 0, 0x40000FFF,
    SETTING_SCOPE_LIVE, "GE007_MENU_TOGGLE_KEY", "--config-override Input.MenuToggleKey=VALUE",
    "Menu key", "SDL keycode that opens the in-game menu (default F1).");
settingsRegisterInt("Input.MenuToggleButton", &g_cfgMenuToggleButton, SDL_CONTROLLER_BUTTON_BACK, 0, 20,
    SETTING_SCOPE_LIVE, "GE007_MENU_TOGGLE_BUTTON", "--config-override Input.MenuToggleButton=VALUE",
    "Menu button", "SDL_GameController button that opens the in-game menu.");
settingsRegisterInt("Input.FpsToggleKey", &g_cfgFpsToggleKey, SDLK_F10, 0, 0x40000FFF,
    SETTING_SCOPE_LIVE, "GE007_FPS_TOGGLE_KEY", "--config-override Input.FpsToggleKey=VALUE",
    "FPS key", "SDL keycode that toggles the FPS overlay (default F10).");
```

- [ ] **Step 2: Consume in the overlay.** In `ui_overlay.cpp`, replace the hardcoded `SDLK_F1` and `SDL_CONTROLLER_BUTTON_BACK` comparisons (`:70`, `:221`) and the Task-1 `SDLK_F10` with `extern s32 g_cfgMenuToggleKey/…Button/g_cfgFpsToggleKey;` reads.
- [ ] **Step 3: Regenerate + build.** Run: `python3 tools/gen_env_reference.py --out docs/ENV_FLAGS.md` then `cmake --build build -j`. Expected: built, zero warnings, one-line-per-flag ENV_FLAGS diff.
- [ ] **Step 4: ctest.** Run: `ctest --test-dir build -R "app_config|settings_menu_model|env_reference_current" --output-on-failure`. Expected: 3/3 PASS.
- [ ] **Step 5: Commit.**

```bash
git add src/platform/platform_sdl.c src/app/ui_overlay.cpp docs/ENV_FLAGS.md
git commit -m "feat(input): rebindable menu-toggle + FPS-toggle keys/button (Input.MenuToggle*/FpsToggleKey)"
```

---

## Phase 3 — Device-aware button glyphs (overlay polish)

### Task 3: Track last-used input device and render contextual button hints

**Files:**
- Modify: `src/app/ui_overlay.cpp` (last-device tracking in `onProcessEvent`; hint footer in `onRender`)
- Reference: `src/app/ui_settings.cpp:99-106` (existing inline help pattern)

**Interfaces:**
- Produces: `enum LastInputDevice { KBM, PAD }` tracked in `ui_overlay.cpp`; a `renderButtonHints()` helper.

- [ ] **Step 1: Track the last device.** In `onProcessEvent`, set a static `g_lastInputDevice = PAD` on any `SDL_CONTROLLER*`/`SDL_JOY*` event and `= KBM` on `SDL_KEY*`/`SDL_MOUSE*`.
- [ ] **Step 2: Render hints.** In `onRender`, replace the current Paused/keeps-running footer with a hint row that switches on `g_lastInputDevice`:

```cpp
const char* hints = (g_lastInputDevice == PAD)
    ? "(A) Select   (B) Back   <>  Adjust   (Start) Resume"
    : "Enter Select   Esc Back   <-/->  Adjust   F1 Resume";
ImGui::TextDisabled("%s", hints);
```

- [ ] **Step 3: Build + gameplay-validate.** `cmake --build build -j`; boot, open the menu, alternate between keyboard and a controller — the hint row switches glyph style on the last-used device.
- [ ] **Step 4: Commit.**

```bash
git add src/app/ui_overlay.cpp
git commit -m "feat(overlay): device-aware button hints (controller glyphs vs keyboard)"
```

---

## Phase 4 — Settings staging (the core UX fix)

### Task 4: A staging layer in the config module (edit staged, commit/discard)

**Files:**
- Modify: `src/platform/config_schema.c` (staged store + commit/discard API)
- Modify: `src/platform/config_pc.h`/`.c` (declare the API)
- Test: `tests/test_app_config.cpp` (round-trip)

**Interfaces:**
- Produces (C API, callable from `ui_settings.cpp`):
  - `void configStagingBegin(void);`  — snapshot current live values into the staged store; route `mgb_config_set_*` writes to staged.
  - `void configStagingApply(void);`  — copy staged → live globals (+ persist to ini).
  - `void configStagingDiscard(void);`— drop staged, live untouched.
  - `bool configStagingActive(void);`
  - `void configStagingPreview(const char* key, bool on);` — temporarily push one staged value to live (for FOV/sensitivity hover-preview), reverting on `off`.

- [ ] **Step 1: Write the failing test.** In `tests/test_app_config.cpp`:

```cpp
// staging: setting a value while staging must NOT change the live global until Apply.
configStagingBegin();
float before = mgb_config_get_float("Video.FovY", 0.0f);
mgb_config_set_float("Video.FovY", before + 13.0f);
assert(mgb_config_get_float("Video.FovY", 0.0f) == before);   // live unchanged while staged
configStagingApply();
assert(mgb_config_get_float("Video.FovY", 0.0f) == before + 13.0f); // applied
// discard path
configStagingBegin();
mgb_config_set_float("Video.FovY", 0.0f);
configStagingDiscard();
assert(mgb_config_get_float("Video.FovY", 0.0f) == before + 13.0f); // discard kept live
```

- [ ] **Step 2: Run it, verify it fails.** Run: `cmake --build build --target test_app_config -j && ctest --test-dir build -R app_config --output-on-failure`. Expected: FAIL (undefined `configStagingBegin`).
- [ ] **Step 3: Implement the staging store.** In `config_schema.c`: a keyed staged-value map + a `s_stagingActive` flag; `configSetValue` writes to the staged map when active, else to the live global as today (`config_schema.c:130`). `configStagingApply` iterates staged → live via the existing setters + persists; `configStagingDiscard` clears the map. Guard: staging is host-side only; never engaged under `--deterministic`.
- [ ] **Step 4: Run test, verify pass.** Run: `ctest --test-dir build -R app_config --output-on-failure`. Expected: PASS.
- [ ] **Step 5: Commit.**

```bash
git add src/platform/config_schema.c src/platform/config_pc.h tests/test_app_config.cpp
git commit -m "feat(config): settings staging layer (begin/apply/discard/preview) + round-trip test"
```

### Task 5: Wire the overlay Settings screen to staging (Apply / Cancel / Resume / Preview)

**Files:**
- Modify: `src/app/ui_settings.cpp` (call `configStagingBegin` on open; add Apply/Cancel buttons; Preview affordance on FOV/sensitivity)
- Modify: `src/app/ui_overlay.cpp` (Resume applies staged; closing without Apply on the Settings child discards)

**Interfaces:**
- Consumes: the Task-4 C API (declare `extern "C"` in the C++ shell).

- [ ] **Step 1: Begin staging when Settings opens.** Where `ui_overlay.cpp:200` toggles the Settings child, call `configStagingBegin()` on entry.
- [ ] **Step 2: Apply/Cancel controls.** In `Settings_draw()` add an **Apply** button (`configStagingApply()`), **Cancel** (`configStagingDiscard()` + close Settings). Change `Save Settings` to persist only after Apply. Resume in the parent overlay calls `configStagingApply()` then closes.
- [ ] **Step 3: Preview affordance.** For `Video.FovY` and `Input.MouseSensitivity`, add a small "Preview" toggle that calls `configStagingPreview(key, true/false)` so the owner can feel the value on the frozen frame; it reverts on toggle-off unless Applied.
- [ ] **Step 4: Build + gameplay-validate.** `cmake --build build -j`; open Settings, drag sensitivity — the running game is NOT affected until Apply/Resume; Cancel restores; Preview lets you feel FOV/sensitivity.
- [ ] **Step 5: ctest + commit.** Run: `ctest --test-dir build -R "app_config|settings_menu_model" --output-on-failure` (2/2 PASS). Then:

```bash
git add src/app/ui_settings.cpp src/app/ui_overlay.cpp
git commit -m "feat(overlay): Settings stages changes — Apply/Cancel/Resume commit model + FOV/sens preview"
```

---

## Phase 5 — Menu unification (Start → one hub; highest risk, ship last)

### Task 6: Reroute Start to the overlay; overlay "Watch/Inventory" raises the engine watch

**Files:**
- Modify: `src/app/ui_overlay.cpp` (accept Start as an opener; add a "Watch / Inventory" menu row that calls the engine to raise the watch)
- Modify: `src/game/mp_watch.c` (Start no longer opens the watch directly; expose a `void portRaiseWatchMenu(void)` the overlay can call to open the watch on demand)
- Reference: `mp_watch.c:625` (current Start→watch trigger), `lvl.c:2094`/`:2176` (the two pause paths to unify)

**Interfaces:**
- Produces: `void portRaiseWatchMenu(void)` (C, engine) — programmatically raises the retail watch. Consumed by `ui_overlay.cpp` (via `extern "C"`).

- [ ] **Step 1: Expose the watch opener.** In `mp_watch.c`, factor the watch-raise side of `:625` into `portRaiseWatchMenu()` (sets `g_pausedFlag`/raises the watch as the Start path does today) without coupling it to the Start button read.
- [ ] **Step 2: Stop Start from opening the watch directly.** Gate the engine's Start→watch so Start is available to the overlay opener; keep a fallback env (`GE007_LEGACY_START_WATCH=1`) that restores the old direct Start→watch for A/B during testing.
- [ ] **Step 3: Route Start to the overlay + add the Watch row.** In `ui_overlay.cpp`, treat the configured menu button (now defaulting Start for gamepad) as an opener; add a **Watch / Inventory** menu entry whose action calls `portRaiseWatchMenu()` and closes the ImGui overlay so the diegetic watch renders. Respect the P1-instance filtering (`:79`) so split-screen P2–P4 keep their own pads.
- [ ] **Step 4: Unify the pause.** Confirm the overlay-open solo pause (`lvl.c:2094`) and the watch pause (`lvl.c:2176`) don't double-handle when transitioning overlay→watch→overlay; verify the stall-watchdog + resume-edge latch (`stubs.c:6266`) hold. Keep `GE007_TRACE_SOLO_PAUSE` proof green.
- [ ] **Step 5: Build + heavy gameplay-validate (owner, both devices).** Start opens the hub; Watch/Inventory opens the retail watch (weapon select works); Resume unfreezes cleanly; F1/Back still open the hub; split-screen pads isolated; MP never freezes. A/B against `GE007_LEGACY_START_WATCH=1`.
- [ ] **Step 6: Determinism guard.** Run: `bash tools/fidelity/tape_regression.sh --no-build` — expected all 7 tapes byte-exact (the menu routing is host-side; sim untouched). If any tape changes, STOP — the reroute leaked into sim input.
- [ ] **Step 7: Commit.**

```bash
git add src/app/ui_overlay.cpp src/game/mp_watch.c
git commit -m "feat(overlay): Start opens the unified hub; Watch/Inventory delegates to the engine watch"
```

---

## Self-Review

**Spec coverage:** §5.1 fullscreen (done, prior commit) · §5.2 unified Start hub → Task 6 · §5.3 staging → Tasks 4–5 · §5.4 FPS hotkey → Task 1 · §5.5 glyphs → Task 3, rebindable toggle → Task 2. All covered.

**Placeholder scan:** each code step carries real code; validation steps that are gameplay-based say so explicitly (this codebase validates UI/engine by gameplay + the determinism tape gate, not unit tests) — that's honest, not a placeholder. The one true unit test (Task 4 staging round-trip) has full assert code.

**Type consistency:** `configStagingBegin/Apply/Discard/Preview/Active` used identically in Tasks 4–5; `portRaiseWatchMenu()` defined in Task 6 step 1 and consumed in step 3; `g_cfgMenuToggleKey/Button`, `g_cfgFpsToggleKey` defined in Task 2 and consumed in Tasks 2–3, 6.

**Risk ordering:** Phases 1–4 are host-side/config and independently shippable; Phase 5 (engine-touching) is last and carries the tape-regression + A/B-flag guards.

---

## Execution notes
- Every phase is independently shippable and gameplay-testable — good checkpoints for owner review.
- Phase 5 is the one to gate on owner gameplay testing (both devices) before merging toward a release.
- Determinism: Tasks 1–5 are host-side and cannot change sim state; Task 6 adds an explicit tape-regression gate (step 6) because it touches the Start-button input path.
