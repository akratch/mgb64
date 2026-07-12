# Post-boot in-game UX — design spec (DRAFT for review)

**Date:** 2026-07-12 · **Status:** DRAFT — awaiting owner approval before implementation.
**Scope:** everything that happens *after* the game boots out of the launcher — window/fullscreen
behavior, pausing, the settings/overlay experience, quick toggles (FPS), and full parity between
keyboard/mouse and controller. Explicitly **not** the retail in-fiction watch menu's gameplay
role (weapon select, objectives) — that stays; this is the *system* layer around it.

> This spec is written against an AFK review: it makes recommended calls with rationale and
> flags the genuine decisions as **[DECISION]** for the owner to confirm. The current-state
> section (§3) is grounded in the code; the proposed design (§5) is the reviewable part.

---

## 1. Problems to solve (owner-reported)

1. **Windows: the game window still has a title bar.** The launcher goes proper fullscreen, but
   after boot the game reverts to a windowed, title-barred window — the fullscreen doesn't carry
   through. (Bug — fix before next build.)
2. **Changing settings mid-game is a bad experience.** Adjusting sensitivity/FOV/etc. while the
   game runs underneath is disruptive and awkward.
3. **No coherent post-boot model.** There isn't one obvious "how do I pause / change a setting /
   toggle the FPS overlay" flow that works the same for keyboard/mouse and controller and feels
   like a real game.

## 2. How real games handle this (the pattern we should match)

Modern games converge on a small, consistent set of conventions. We should adopt them:

- **The pause menu is the hub.** One button (Esc / Start) pauses the game *and* opens a menu:
  **Resume · Settings · Restart · Quit**. The world is frozen behind a dimmed backdrop. Settings
  are a *sub-screen of pause*, never a thing you fiddle with over live gameplay. This single move
  solves problem #2: you change sensitivity **while paused**, so there's no disruption, and you
  feel the result the moment you resume.
- **Live-preview where it's cheap; apply-on-confirm where it's not.** Sliders that are cheap to
  reflect (FOV, sensitivity, volume, crosshair) update a *preview* immediately; heavier changes
  (resolution, MSAA, renderer) are marked **"applies on restart"** or **"Apply"** and are not
  thrashed keystroke-by-keystroke. Nothing silently applies to the live game underneath a menu.
- **Quick toggles bypass the menu.** A dedicated hotkey/chord toggles the **FPS/perf overlay**
  instantly without opening the full menu. Power-user, non-disruptive, discoverable via the
  settings screen's "keybinds" list.
- **Controller and KB/M are first-class and identical.** The same menu is fully navigable by
  D-pad/stick + A/B (or Enter/Esc) *and* by mouse click + wheel. **Contextual button hints** show
  the *active* device's glyphs ("Ⓐ Select  Ⓑ Back" vs "Enter Select  Esc Back"), switching on the
  last-used input. Sliders adjust with ◄►/mouse-drag. No feature is mouse-only.
- **One system layer, clearly separated from the fiction.** The pause/settings overlay is the
  "system" (engine/host) layer; the in-fiction watch menu remains the diegetic layer. They must
  not fight over the same button or the same "is the game paused?" state.

## 3. Current state (grounded in code)

Two UI systems share one SDL window + GL context: the **app shell** (ImGui/C++, `src/app/`) and the
**engine** (C, `src/platform/` + `src/game/`, retail watch menu + N64 HUD). Seam = `AppOverlayHooks`
+ `platformSetHostWindow`.

- **The F1 overlay is already a solid pause hub.** `src/app/ui_overlay.cpp` + `ui_settings.cpp`.
  Opened by **F1 (kb)** or **Back/View (gamepad)** — the toggle button is *hardcoded*
  (`ui_overlay.cpp:70,221`), not rebindable. It draws a dimming backdrop + centered ImGui panel over
  the (frozen) game frame from `gfx_pc.c:24999`. Menu = **Resume · Settings · Return to Launcher ·
  Quit** (Quit/Return gated by confirm). **Settings** is the same tabbed panel the launcher uses
  (`ui_settings.cpp:179`, Video/Input/Game/Audio/UI, player tier + "Advanced" disclosure + per-tab
  reset). **It is fully controller-navigable** (`app_host.cpp:82` enables `NavEnableGamepad` +
  `NavEnableKeyboard`; B = back; frees the mouse on open; sets nav focus; inline help text for
  pad/touch). *This is a strong foundation — not a gap.*
- **Pause already works (solo).** Opening the overlay pauses the sim in single-player:
  `portOverlaySoloPauseActive()` (`lvl.c:2094`) zeroes `g_ClockTimer` (`lvl.c:2183`) only when the
  overlay wants input AND `getPlayerCount()==1` AND not deterministic. The retail watch (Start) is a
  *separate*, orthogonal pause path (`mp_watch.c:625`, `lvl.c:2176`). **MP intentionally never
  pauses** (can't freeze other players — `lvl.c:2105`); the overlay footer says so per-mode. Drives
  the stall watchdog so a paused sim isn't flagged as a hang.
- **Input routing is clean.** `platformPollEvents()` swallows all input to the game when the overlay
  captures (`platform_sdl.c:2985`); `osContGetReadData` returns neutral pads + a release-latch so a
  held close-button can't leak (`stubs.c:6242`); mouse-look/right-stick zeroed when the overlay
  wants input (`lvl.c:5938`). Split-screen: the overlay toggle is filtered to P1's instance id
  (`ui_overlay.cpp:79`) so P2–P4 Start/Back reach their own pads. *Not a gap.*
- **Settings apply model — THE core gap (problem #2).** Every setting's backing pointer **is the
  game's live global**; the ImGui controls write **directly into it** via `configSetValue`
  (`config_schema.c:130`). **There is NO apply/cancel staging** — every slider drag / checkbox
  toggle mutates the running game immediately, re-read next frame. `Save Settings` only persists to
  `ge007.ini` — it is *not* the apply step. `SETTING_SCOPE_LIVE`/`RESTART` is **UI-label metadata
  only** (`settings.h:15`); nothing consults it to defer application. So `Input.MouseSensitivity`
  (`platform_sdl.c:2166`), `Video.FovY` (`:2134`), `Video.MSAA` (`:2127`, rebuilds the FBO live) all
  apply **per-slider-drag**. In solo the game is frozen behind the overlay so it isn't *visible*
  chaos, but there is no preview/commit model and no way to feel a change before committing; in MP
  (no pause) the mutation is live under running gameplay.
- **Window / fullscreen — root cause of #1 confirmed.** ONE shared window; TWO decoupled fullscreen
  settings. The shell creates the window windowed+titlebar (`app_host.cpp:54`); the **launcher**
  fullscreens it via `UI.LauncherFullscreen` → `SDL_SetWindowFullscreen(FULLSCREEN_DESKTOP)`
  (`app_host.cpp:122`). On **Play**, the engine *adopts* the same window
  (`platformSetHostWindow`, `main_app.cpp:110`) then immediately runs `platformApplyWindowMode()`
  (`platform_sdl.c:2900`) applying the **separate** `Video.WindowMode` (default **WINDOWED**,
  `platform_sdl.c:403`) → `SDL_SetWindowFullscreen(g_sdlWindow, 0)` (`:1748`) — **reverting the
  launcher's fullscreen to a windowed, title-barred window.** The split is even documented in the
  `UI.LauncherFullscreen` help ("affects only the pre-game launcher — the in-game window follows
  Video.WindowMode"). Caveat for the fix: `platformApplyWindowMode` is intentionally run on the
  host-window path (RX.3 Fix A) and Alt+Enter routes through it, so the fix must keep those
  consistent (don't just skip it).
- **FPS overlay** = an N64 DL HUD widget (not ImGui), toggled by `Video.FpsOverlay` (default **on**,
  `platform_sdl.c:388`), rendered from `bondview.c:21612` → `pc_fps_overlay.c:95`; force-suppressed
  under deterministic/screenshot/background so automation is byte-identical.

**Net:** the overlay/pause/input/nav layer is already good. The *actual* gaps are three: (a) the
launcher↔game fullscreen hand-off (the bug), (b) the settings apply model (per-keystroke, no
stage/commit), and (c) hardcoded, non-rebindable overlay toggle + no device-specific button glyphs.

## 4. User stories (post-boot)

**Keyboard/mouse**
- As a KB/M player, I press **Esc** to pause; the world freezes and a menu appears with Resume /
  Settings / Restart / Quit. I click or arrow-key to a choice.
- In Settings, I drag the **Sensitivity** slider and see a live number/preview; when I Resume, the
  new sensitivity is in effect — I never had to feel it change mid-fight.
- I press the **FPS hotkey** any time to toggle the perf overlay without opening the menu.
- I change **resolution**; it's marked "applies on restart" and doesn't thrash the live window.

**Controller**
- As a controller player, I press **Start** to pause (same menu). I navigate with the D-pad/stick,
  **A** selects, **B** backs out — button hints on-screen show controller glyphs.
- I adjust **FOV** with ◄►; the preview updates. **B** backs to pause, **Start**/Resume returns me
  to the frozen-then-unfrozen game.
- A documented **chord** (e.g. Select+Start, or a bindable button) toggles the FPS overlay.
- Everything I can do with KB/M I can do with the controller — no mouse-only settings.

**Both**
- Pausing always *actually pauses* solo play (sim frozen), and never desyncs or double-advances the
  timer on resume.
- The game window matches the launcher: if the launcher was fullscreen, the game is fullscreen.

## 5. Proposed design (the reviewable part)

### 5.1 Window / fullscreen (fixes #1)
**[DECISION A]** The game window should **inherit the launcher's presentation** rather than being
an independent, always-windowed default. Recommended: on the launcher→game transition, if the
shared window is currently fullscreen (launcher went fullscreen) **and** `Video.WindowMode` was not
*explicitly* set by the user, seed `g_windowMode` from the live window state (borderless) so the
game stays fullscreen. If the user explicitly set `Video.WindowMode`, honor it. Net effect: launcher
fullscreen → game fullscreen; explicit user choice always wins. *(Alternative: default
`Video.WindowMode` to `borderless` for everyone — simpler but changes the default for windowed
users. Recommendation: the inherit approach.)*

### 5.2 Pause = the hub (mostly done — refine, don't rebuild)
The overlay **already is** a pause hub (Resume/Settings/Return/Quit, freezes solo, pad-navigable).
The refinements: (a) rename/re-order to a clear **Resume · Settings · Restart mission · Return to
Launcher · Quit** and add **Restart mission** if missing; (b) resolve the button story below.
**[DECISION B — button ownership]** Today: **Start → retail watch** (diegetic; weapon select,
objectives), **F1 / Back-View → modern overlay** (system). Modern players reach for **Start** to get
a system pause menu, so this split can feel wrong. Options: **(B1, rec.)** keep the split but make
the overlay toggle **rebindable** and clearly documented, and add a first-run hint ("Back/View =
menu, Start = watch"); **(B2)** make **Start** open the system overlay and move the retail watch to
**Select/Back** (more conventional, but retrains muscle memory and touches the engine's Start
handling); **(B3)** unify — the system overlay gains a "Watch/Inventory" entry and Start opens the
overlay (largest change). Rec: **B1 for this release**, B2/B3 tracked for later.

### 5.3 Settings **apply model** (the real fix for problem #2)
Root cause (§3): controls write **straight into live globals**, per-keystroke, no staging. Design a
thin **staging layer**:
- ImGui controls edit a **staged copy** of each setting, not the live global.
- **Apply** commits staged → live (and to `ge007.ini`); **Cancel/Back** discards; **Resume** applies
  then closes. Nothing mutates the running game mid-drag.
- **Live-preview opt-in** for the few settings where feeling it helps (FOV, sensitivity): a "Preview"
  affordance that temporarily pushes the staged value to the live global while held/hovering, then
  reverts on release unless Applied. Cheap ones only.
- **Restart-scoped** settings (HiDPI, fullscreen size/refresh, texture pack): the existing "restart"
  badge stays; Apply writes the ini but doesn't recreate context until next boot.
- **MP correctness:** because MP doesn't pause, staging is what makes mid-MP settings edits safe —
  they apply on Apply, not live under running gameplay.
This is the highest-leverage change and the one that makes it "feel like a real game." It's contained
to the settings/config layer (`config_schema.c` / `ui_settings.cpp` / `settings.c`), not the engine.

### 5.4 FPS / quick toggles
`Video.FpsOverlay` already toggles the DL FPS widget live. Add an **instant hotkey** (KB) + a
**controller affordance** to toggle it without opening the menu, and list it in a **Keybinds** view
in Settings so it's discoverable. (Small; builds on the existing global.)

### 5.5 Controller + KB/M parity — add glyphs + rebind (nav already works)
The overlay is already fully pad-navigable, so this is **polish, not plumbing**:
- **Contextual button glyphs**: show the *active device's* hints ("Ⓐ Select  Ⓑ Back  ▸ Adjust" vs
  "Enter Select  Esc Back  ◄► Adjust"), switching on last-used input.
- **Rebindable overlay/menu toggle** (currently hardcoded F1 / Back — `ui_overlay.cpp:70,221`).
- Confirm every control is reachable by both devices (sliders via ◄►/drag) — already largely true.

## 6. Out of scope (YAGNI)
- Rebindable full keymap UI beyond what exists (track separately).
- Netplay/MP pause semantics (this is solo pause; MP keeps running).
- Cosmetic theming of the overlay beyond legibility + hints.

## 7. Open decisions for the owner
- **[DECISION A]** Window inheritance vs. defaulting `Video.WindowMode=borderless`. *(Rec: inherit.)*
- **[DECISION B]** Controller button for the pause hub + how it coexists with the retail watch menu.
- **[DECISION C]** How aggressively to converge the two menu systems now vs. later (minimal: pause
  hub wraps the existing overlay; maximal: unify watch+system menus). *(Rec: minimal for this
  release.)*
