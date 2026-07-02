# W5 — Gameplay Feel, UX & Accessibility (modern feel, faithful sim)

> Workstream 5 of the MGB64 AAA Remaster program. Owner doc for: ADS completion,
> the in-game remaster settings menu, input polish (gyro / rebinding / toggle-hold),
> accessibility (captions, colorblind palettes, flash/shake reduction), and
> quality-of-life (quick-restart, speedrun timer, onboarding).
>
> **Constitution:** [REMASTER_ROADMAP.md](../REMASTER_ROADMAP.md). Every task here is
> presentation/input-side, flagged (`GE007_*` env + `Video.*`/`Input.*` key), and
> default-identity (R3). Nothing in this workstream writes sim state from render
> code (R1); the few input-synthesis items (toggle-mode aim, gyro) are validated
> RAMROM replay-invariant per roadmap §5/§7. No art assets are introduced — all UI
> is code-drawn (Tier A1 by construction, R2).
>
> `file:line` anchors were read against `feat/metal-backend` on 2026-07-02; they
> are function-level and may drift a few lines.

---

## 1. Executive summary

The port already *plays* start-to-finish and already ships a modern-feel flag suite
(crosshair, hit markers, aim curves, FOV, ADS with rise-to-sights). What is missing
is the **last mile of product polish that reviewers and players actually touch**:
ADS finishing (per-weapon poses + determinism gates), a discoverable **in-game
settings menu** (today ~81 registered settings are reachable only via `ge007.ini`,
env vars, and CLI), modern input options (gyro aim, rebinding, toggle/hold aim),
an accessibility pass (captions, colorblind-safe reticle palettes, flash/shake
reduction), and quality-of-life features (quick-restart, speedrun timer,
first-run onboarding). All of it rides existing, verified engine seams — the
declarative settings registry (`src/platform/settings.c`), the front-end menu
dispatcher (`src/game/front.c:14360-14570`), the native input injection block
(`src/game/lvl.c:5786-5954`), and the HUD message system
(`src/game/bondview.c:22059`). The sim is untouchable throughout.

| # | Headline deliverable | Player-visible outcome |
|---|---|---|
| 1 | **ADS "done"** | Every weapon has an authored sighted pose; ADS passes the three determinism gates; feature graduates from "shipped flag" to "reviewed feature" |
| 2 | **In-game Remaster Settings menu** | Full settings UI in the front-end *and* in-mission overlay; live apply where safe, "restart required" tags, saves to `ge007.ini` |
| 3 | **Modern input suite** | Gyro aim on supported pads, keyboard/mouse rebinding, toggle-or-hold aim, per-weapon ADS sensitivity surfaced |
| 4 | **Accessibility pass** | Audio-event captions, 4 colorblind reticle/hitmarker palettes, flash & screen-shake reduction toggles |
| 5 | **Speedrun/QoL pack** | One-key mission quick-restart, mission timer overlay, `--remaster` first-run onboarding |

---

## 2. Current state (verified in code)

### 2a. ADS — much further along than the plan's status line

`docs/ADS_PLAN.md` says "design / not yet implemented" at the top but §10 documents
the real state: **MVP + rise-to-sights are SHIPPED** behind `Input.AdsEnabled`
(default 0). Verified in code:

- **All 14 ADS settings registered** with the 10-arg API at
  `src/platform/platform_sdl.c:1769-1838` (globals `:1055-1068`):
  `Input.AdsEnabled/Sensitivity/FovCoupleSens/CenterCrosshair/SpreadEnabled/`
  `MovePenalty/MoveScale/StrafeScale/FaithfulZoom/ModelPose/RecoilReduce/`
  `ModernReticle/SteadyView/BobFloor`.
- **Profile table** `src/game/ads_profiles.c` (241 lines): authored rows for
  PP7/PP7sil/RC-P90/KF7/AR33 + scope yields for sniper/camera
  (`ads_profiles.c:108-124`), computed defaults (`:129-172`), query-time FOV with
  mild-iron clamp (`adsResolveFovY`, `:218-239`), `GE007_ADS_*` env override loop
  (`:72-84, 197-213`).
- **Sens path live**: ADS-1.1 flat multiplier + ADS-1.2 FOV-couple applied once at
  the final `sens`, inside `pcNativeLiveLookAllowed()` (`src/game/lvl.c:5883-5910`;
  the gate itself is `lvl.c:93-100` — returns 0 under RAMROM).
- **Movement penalty live** at the post-crouch seam
  (`src/game/bondview.c:13331-13340`).
- **Center-pull** live (`bondview.c:12220-12276, 12950-12960`).
- **Pose** live in the *active* positioner (`gun.c` `portAdsResolvePose` /
  look-at flatten; dormant `portBuildFirstPersonWeaponRoot` copy removed —
  ADS_PLAN §10.1). **Modern ADS reticle** live (`drawModernAdsReticle`,
  `gun.c:33736`, callsites `:33834/:33851`; `gDPFillRectangle`-only =
  settex-safe + renderer-agnostic).
- **Authoring harness exists**: `tools/ads_pose_capture.sh` (headless
  boot→equip→aim→override→screenshot loop; usage `LABEL ITEM [X Y Z YAWDEG
  PITCHDEG ROLLDEG]`) + `GE007_ADS_POSE_X/Y/Z` and
  `GE007_ADS_POSE_{YAW,PITCH,ROLL}_DEG` hooks (ADS_PLAN §10.2). NOTE:
  `GE007_ADS_FORCE_POSE` is *proposed* in ADS_PLAN §10.3 but does **not** exist
  in the tree — do not depend on it.
- **Startup misconfiguration note** already prints
  (`src/platform/main_pc.c:805-820`).

**What is genuinely left** (= ADS_PLAN §10.3, re-verified): ~10-15 weapons still on
the universal default pose; per-class default tuning; the scripted equip/aim
harness is flaky (`GE007_AUTO_EQUIP_ITEM` sometimes doesn't swap; `ITEM_WPPKSIL`
persisted); the **three ADS-0.3 runtime gates were never run** (no RNG draw-count
counter exists anywhere in `src/` — verified by grep); recoil-reduction site
re-confirmation on the active path; optional ease-out.

### 2b. Settings machinery — registry done, UI absent

- **Declarative registry**: `settingsRegister{Int,UInt,Float,Enum,String}`
  (`src/platform/settings.c:52-167`) captures type, min/max, **scope
  (`SETTING_SCOPE_LIVE` / `SETTING_SCOPE_RESTART`)**, env, CLI, **label, help** —
  everything a UI needs. Introspection exists: `settingsCount()/settingsAt()/`
  `settingsFind()` (`settings.c:169-186`), enum tokens (`:221-233`),
  value/default/range formatters (`:391-465`), `settingsResetAllToDefaults()`
  (`:329-359`).
- **81 keys registered**: 79 across `platform_sdl.c:1490-1897` (Video 44, Input 35)
  plus `Audio.MasterVolume`/`Audio.DeviceSamples` (`src/platform/audio_pc.c:859-864`).
- **Persistence**: `configSave()` (`src/platform/config_pc.c:452`) writes
  `ge007.ini`; called on quit (`platform_sdl.c:2615`) and after `--config-set`
  (`main_pc.c:781`). `--faithful`/`--remaster` sessions suppress saving
  (`config_pc.c:445-449`, `main_pc.c:777-780`). **First run is already
  detectable**: `configInit()` prints "No config file found, writing defaults"
  (`config_pc.c:560-568`).
- **No UI**: front-end screens are the original hand-drawn quads
  (init/update/interface/constructor per screen, dispatched by four
  `switch (current_menu)` blocks: update `front.c:14360`, init `:14399`,
  interface `:14428` — all three inside **`menu_init(void)`**
  (`front.c:14282`) — and constructor `:14493` inside
  **`menu_jump_constructor_handler(Gfx *DL)`** (`front.c:14480`); enum `MENU`
  at `src/bondconstants.h:1537-1567`, tail =
  `MENU_SPECTRUM_EMU` (`:1565`), `MENU_MAX` (`:1566`)). A concrete example: the 007-options screen is
  `init_menu09_007difficultyselect` (`front.c:4935`), `interface_menu09_007options`
  (`front.c:4955` — cursor hit-boxes by raw `cursor_v_pos` Y ranges, sliders from
  `cursor_h_pos`, transitions via `frontChangeMenu(MENU_*, reload)` `front.h:342`),
  `constructor_menu09_007options` (`front.c:5095`, draws with `frontPrintText`
  (definition `front.c:1568`) and images — **not** `textRender`).

### 2c. Input — one hardcoded map, modern pad feel shipped, no gyro

- Keyboard/mouse → N64 pad is **hardcoded** in `osContGetReadData`
  (`src/platform/stubs.c:5912`, mapping at `:6196-6243`): WASD stick, mouse
  LEFT=`Z_TRIG` fire / RIGHT=`R_TRIG` aim / MIDDLE=`B_BUTTON`
  (`stubs.c:6240-6243`), `R/F/Backspace`=B, shift/alt triggers, etc. **No rebinding.**
- Gamepad: SDL_GameController, 4 slots (`platform_sdl.c:43-110`); right-stick look
  injected per player with radial deadzone, response curve, fps-scale
  (`lvl.c:5807-5867`); mouse-look P1-only (`lvl.c:5804-5806`). Mouse sens keys
  `Input.MouseSensitivity`/`MouseSensitivityAim`/`InvertY`
  (`platform_sdl.c:1721-1731`).
- **No gyro anywhere** (grep `SDL_GameControllerHasSensor|GYRO` = 0 hits). CMake
  does not pin an SDL2 minimum (`CMakeLists.txt:19-32`) — gyro API needs SDL
  2.0.14+, so compile-guard with `SDL_VERSION_ATLEAST(2,0,14)`.
- **Input-latency reorder LANDED** (robustness item 3): input is polled *after*
  the frame-pacing wait — commit `2a22ea2` (contained in `feat/metal-backend`),
  `platformFrameSync` in `platform_sdl.c` (pacing block now precedes
  `platformPollEvents()`).
- Aim hold-vs-toggle currently inherits the original "Control style" via
  `cur_player_get_aim_control()` (`src/game/watch.c:612`; consumed
  `bondview.c:11642, 11843`).

### 2d. Accessibility & QoL primitives

- **Dialogue is already on-screen text** — GoldenEye has no voice-over. AI-scripted
  mission dialogue/objective text flows `langGet(slotID)` (`src/game/lvl_text.c:479`)
  → chrai text commands (`src/game/chrai.c:4615-4633`) → `hudmsgBottomShow`/
  `hudmsgTopShow` (`src/game/bondview.c:22059/22290`, 5-slot bottom-left queue).
  So "subtitles" for dialogue is a **non-problem**; the real gap is **captions for
  audio-only events** (alarms, distant gunfire) — cleanly hookable at the SFX
  funnel `sndPlaySfxInternal` (`src/snd.c:3170`; see §4.4a for why *not*
  `sndPlaySfx` at `:871/:881`); ~220 `sndPlaySfx*` callsites in
  `src/game`, e.g. `chrlv.c:3316`.
- **Flash**: white pickup/damage flashes run through `currentPlayerDrawFade`
  (`bondview.c:10362`); a native env-only suppressor already exists —
  `portSuppressDamageFlash()` (`bondview.c:129-135`,
  `GE007_SUPPRESS_DAMAGE_FLASH`) zeroes the full-white flash at `bondview.c:10373-10376`.
  Damage red-flash timing comes from `g_DamageTypes[...].flash*Frame`
  (`bondview.c:13007-13036`).
- **Screen shake**: the live path is *scalar* — `explosionScreenShake`
  (`src/game/explosions.c:1037`) calls `viShake(intensity)`
  (`explosions.c:1120`); `viShake` (`src/fr.c:1617`) stores
  `g_ViShakeIntensity` (`:1629`), consumed as a VI vertical display offset in
  `src/fr.c:248-258` — presentation-side. The `coord3d` out-param written at
  the live callsite (`bondview.c:15641`) is **dead** (never read), and the
  4-arg `viShake(...)` at `bondview.c:15874` sits in a `#ifdef NONMATCHING`
  reference body (dead code).
- **Reticle colors**: modern reticle tints green-on-target else red via one
  `gDPSetFillColor(gdl++, fill_main)` (`gun.c:33792`); hit markers keyed by kind
  (`gun.c:33665-33715`). Single obvious palette seam.
- **Mission timer**: `getMissiontimer()` (`bondview.c:26448`) is the sim frame
  counter; the mission-complete screen already formats it as MM:SS
  (`front.c:9184, 9224-9227`). Reading sim from render is the *allowed* direction.
- **Quick-boot machinery**: `--level` boots via `pc_apply_level_selection(level,
  diff)` (`src/game/initmenus.c:237`, invoked `:567-573`);
  `frontChangeMenu(MENU_RUN_STAGE, TRUE)` is the standard "start the stage" call
  (`front.c:5073`). Mission-failed screen transitions show the retry idiom
  (`front.c:8854-8917`).
- **Renderer constraint (critical for anything we draw)**: the minimap overlay is
  **direct-GL and therefore skipped on the native Metal backend**
  (`src/platform/fast3d/gfx_pc.c:23262-23270`). Anything W5 draws must go through
  the game's own display-list path (`gDPFillRectangle`/`textRender`), which both
  backends execute — exactly what `drawModernAdsReticle` already proves out.

---

## 3. Target state — the AAA bar

A reviewer running `./build/ge007 --remaster` sees, without touching a text file:

1. **First launch**: a one-time, unobtrusive onboarding line ("Press F1 for
   Remaster Settings — everything is optional, `--faithful` is always the 1997
   game") on the title screen; never shown again.
2. **Settings menu**: press F1 (or select "Remaster Settings" in the front-end):
   a clean, controller-and-mouse navigable menu with pages **Display / Graphics /
   Gameplay & HUD / Input / ADS / Accessibility / Audio**. Sliders move and the
   frame visibly changes *live* (bloom, vignette, FOV, minimap); items that can't
   hot-apply are tagged `⟳ restart`. "Reset page", "Reset all", and "Save" work;
   a `--faithful` session shows a read-only banner.
3. **ADS**: enabling ADS in the menu gives every weapon — not just 5 — a correctly
   seated sighted pose, per-class timing, and it survives a recorded-replay
   byte-identity gate. Speedrunners can prove the off-state is vanilla.
4. **Input**: a DualSense/Switch-Pro pad can turn on gyro fine-aim; every keyboard
   key and mouse button is rebindable from the menu; aim can be hold or toggle.
5. **Accessibility**: a deuteranopia player switches the reticle/hitmarkers to a
   blue/orange palette; a photosensitive player caps flashes and shake; a deaf
   player gets "\[ALARM\]" captions.
6. **Speedrunners**: `Input.MissionTimer=1` shows a frame-accurate MM:SS.f overlay;
   one key restarts the mission instantly with a confirm tap.
7. **The purist sees nothing**: all-off remains byte-identical; `--faithful` still
   never writes config.

---

## 4. Technical design

### 4.0 Design invariants (apply to every item)

1. **Draw only via the game DL** (`Gfx*` chains: `gDPFillRectangle`,
   `textRender`, `frontPrintText`) — never direct GL. Rationale: Metal parity
   (`fast3d/gfx_pc.c:23262-23270`); split-screen correctness comes free from the
   per-player viewport, as proven by `drawModernAdsReticle` (`gun.c:33736`).
2. **Sim reads render: allowed. Render/input writes sim: only through the pad.**
   Anything that synthesizes *input* (toggle-aim, gyro) must be inert when
   `get_is_ramrom_flag()` (the `pcNativeLiveLookAllowed()` pattern,
   `lvl.c:93-100`) so replays stay bit-identical.
3. **Every new key** registered with the full `settingsRegister*` API
   (`settings.c:52-167`; 10 args for Int/UInt/Float — Enum takes an options
   table instead of min/max, String takes capacity+default — see the ADS block
   `platform_sdl.c:1769` for the Int idiom),
   default = identity, `GE007_*` env + `--config-override` CLI.
4. **No shader work in W5.** All items are DL/input/UI-side; nothing lands in
   `gfx_opengl.c` GLSL or `gfx_metal.mm` MSL. (If a future item ever needs a
   shader, it must land in both generators — W1's rule.)

### 4.1 ADS completion

**Data**: `struct AdsProfile` (`src/game/ads_profiles.h:17-41`) is already the
single source of truth. Finishing = filling `s_ads_authored[]`
(`ads_profiles.c:108-124`) for the long tail, using the shipped authoring loop.

**Authoring workflow (per weapon, ~20 min each once the harness is fixed):**

```bash
# 1. Baseline capture (universal default pose):
tools/ads_pose_capture.sh klobb_base 7   # ITEM = numeric ITEM_IDS value
                                         # 7=Klobb(SKORPION) 9=ZMG(UZI) 10=D5K —
                                         # enum src/bondconstants.h:3342; id
                                         # cheat-sheet in the script header
# 2. Dial: nudge X (negative = left), Y (up), Z (toward eye) until the sight
#    line sits under the centered reticle:
tools/ads_pose_capture.sh klobb_try 7 -4.5 10.0 -2.0
# 3. Bake: add a row to s_ads_authored[] with ADS_POSE_FIELDS replaced by the
#    dialed values (keep ADS_DEFAULT_* timing unless the class table says else).
# 4. Regression: re-run the capture with NO env overrides; compare:
python3 tools/compare_screenshots.py /tmp/ads_pose/klobb_try.png \
        /tmp/ads_pose/klobb_baked.png --max-changed-pct 0.10
```

**Harness fix (blocking)**: `GE007_AUTO_EQUIP_ITEM` did not always swap the weapon
(ADS_PLAN §10.3 item 4). Root-cause in the scripted-input path in
`src/platform/stubs.c`: the equip env parse + `pcMaybeApplyScriptedEquip` live at
`stubs.c:4840-4930` (env read `:4882-4883`; dual-wield variant `:5222-5224`);
scripted buttons/look are assembled inside `osContGetReadData` (`:5918+`). The
equip is frame-scheduled (`EQUIP_ADD=120; EQUIP_DO=160` in
`tools/ads_pose_capture.sh`) but
the inventory add can land before the player has control. Fix = make the equip
hook retry until `getCurrentPlayerWeaponId(GUNRIGHT) == target`
(`getCurrentPlayerWeaponId` definition `gun.c:1781`; poll in the
scripted-input block, not a one-shot), and emit a `[ADS-HARNESS] equipped item=N`
trace line the script greps as a hard precondition before capturing.

**RNG draw-count gate (new, tiny)**: add to `src/random.c` (`randomGetNext` is
defined there at `:270`):

```c
#if defined(NATIVE_PORT) && defined(GE007_RNG_AUDIT)
u32 g_rngDrawCount;                 /* incremented inside randomGetNext() */
#endif
```

plus a debug assert in `bullet_path_from_screen_center` (definition
`gun.c:29122`) that exactly 4 draws happen per shot with ADS on and off. Compile via
`-DGE007_RNG_AUDIT` in a CI variant; never in release.

**Class defaults**: replace the single computed default with a 4-row class table
(pistol/SMG/rifle/heavy) keyed off `WeaponStats` (read-only) in
`adsComputeDefault` (`ads_profiles.c:129`) — pose Y offsets scale with the base
anchor so long weapons don't overshoot (ADS_PLAN §10.3 item 3).

### 4.2 In-game Remaster Settings menu

**Architecture: one new port-side module, two mounts.**

New file `src/game/pc_settings_menu.{c,h}` (NATIVE_PORT-only), containing a
*model* (registry-driven), an *input controller*, and a *DL renderer*. Mounted
(a) as a real front-end screen and (b) as an in-mission overlay.

**Model — driven by the existing registry, curated by a static table:**

```c
typedef enum { SMW_TOGGLE, SMW_SLIDER, SMW_ENUM, SMW_ACTION, SMW_HEADER } SmWidget;

typedef struct SmEntry {
    const char *key;        /* settingsFind() key; NULL for HEADER/ACTION      */
    SmWidget    widget;     /* derived from Setting.type unless overridden     */
    f32         step;       /* slider step (e.g. FovY 1.0, Vignette 0.05)      */
    void (*apply)(void);    /* optional live-apply hook, NULL = write-only     */
    const char *note;       /* short caption; NULL = use Setting.help          */
    u8          future;     /* SM_FUTURE: key registered by a later WS (the W6
                               audio rows); row hidden until settingsFind()
                               resolves it, and whitelisted by the model test  */
} SmEntry;

typedef struct SmPage { const char *title; const SmEntry *entries; s32 count; } SmPage;
```

The curation table lists ~55 of the **81** registered keys across 7 pages
(window-geometry keys like `Video.WindowX/Y` stay ini-only).

**Missing-key tolerance (required behavior)**: any entry whose `key` makes
`settingsFind()` (`settings.c:183`) return NULL is *hidden* (one log line, no
crash). This is what decouples W5 from W6: the **Audio page** curates, today,
only `Audio.MasterVolume` + `Audio.DeviceSamples` (`audio_pc.c:859/:864`) —
**`Audio.MasterVolume` is tagged `SM_FUTURE` until W6.E3.T1 lands** (the key is
registered but does not affect the main synth mix today, doc 06 §2.4; surfacing
a dead slider is worse than hiding it) — plus
pre-listed rows for the W6 bus keys — `Audio.MusicVolume`, `Audio.SfxVolume`,
`Audio.RoomReverb`, `Audio.StereoWidth`, `Audio.OutputFilter` (LIVE) and
`Audio.OutputRate` (RESTART), key names per `06-audio-remaster.md` §4.1 — which
stay hidden until W6.E3 registers them (master-plan edge `W6.E3 → W5.E2`;
W6.E6.T1 is the registry-hygiene handoff that guarantees their labels/help/
scopes render correctly). **Scope handling is free**: render
`settingsScopeName()==“restart”` entries with a `⟳` glyph and do NOT call an
apply hook; live entries take effect immediately because nearly all Video/Input
globals are read per-frame (e.g. bloom/vignette/FXAA are consumed by the output
pass each frame; `Input.*` by the input block each frame). The exceptions that
need explicit apply hooks, all already existing functions in `platform_sdl.c`:
`Video.WindowMode → platformApplyWindowMode()` (`platform_sdl.c:1427`),
`Video.VSync → platformApplyVSync()` (`:1447`). `Video.RenderScale`/`MSAA` are
picked up on the next scene-target ensure — verify per-key during W5.E2.T5 and
mark `RESTART` anything that isn't (that is a 1-line scope change at its
registration site).

**Input**: navigation consumes the same sources as the front-end
(`joyGetButtonsPressedThisFrame`, stick via `frontUpdateControlStickPosition`
idiom — see `interface_menu09_007options`, `front.c:4955-5086`) plus direct
keyboard arrows/enter. NOTE: the arrows→menu-direction mapping in
`stubs.c:6179-6199` only runs when `pcNativeFrontendInputActive()`
(definition `stubs.c:5791`) (front-end
mount); the in-mission overlay must read arrow/enter scancodes itself (in-mission
those keys map to C-buttons, `stubs.c:6224-6228`). While the in-mission overlay is open, gameplay input is
swallowed by zeroing the pad the same way `g_freezeInput` does (full-pad swallow
in `osContGetReadData`, `stubs.c:6109-6116`; the look-delta half is
`lvl.c:5813-5816`) — but *only* outside RAMROM (`pcNativeLiveLookAllowed`-style guard), and the
overlay refuses to open at all when `g_deterministic` or `get_is_ramrom_flag()`.

**Renderer**: pure DL. Panel = `gDPFillRectangle` quads (same primitives as
`drawModernAdsReticle`, `gun.c:33736-33800`); text = `textRender` (definition
`textrelated.c:1942`) with `ptrFontZurichBoldChars/ptrFontZurichBold` — copy
the exact in-mission HUD idiom at `bondview.c:20062/:20108`; the hudmsg path
passes the same fonts (`bondview.c:6528-6531`). In the front-end mount, draw
via the constructor hook with `frontPrintText` (definition `front.c:1568`; call
idiom in `constructor_menu09_007options`, `front.c:5125`). Verify font
availability in-mission on level load (the hudmsg path proves they are resident).

**Mount A — front-end screen**: append `MENU_PC_SETTINGS` before `MENU_MAX`
(`src/bondconstants.h:1566`); add the four cases to the dispatcher — the
update/init/interface switches (`front.c:14360/14399/14428`) inside
`menu_init()` (`front.c:14282`) and the constructor switch (`:14493`) inside
`menu_jump_constructor_handler()` (`front.c:14480`) — calling into
`pc_settings_menu.c`; entry point = a new tab on the file-select/mode-select
screen (`constructor_menu06_modesel`) labeled "SETTINGS", transitioning with
`frontChangeMenu(MENU_PC_SETTINGS, FALSE)` and returning to `MENU_MODE_SELECT`.

**Mount B — in-mission overlay**: toggle key (default F1, rebindable via E3)
checked in the per-frame native block in `lvl.c` (next to the mouse-look block,
`lvl.c:5786`); when open, model+input tick and the DL is appended at the end of
the player HUD pass (same seam that draws hudmsgs/crosshair). Opening does not
pause the sim (live preview is the point); an explicit note row says "game is
live". Escape/START closes.

**Persistence**: on close-with-changes call `configSave()` (`config_pc.c:452`) —
automatically a no-op in faithful/remaster read-only sessions
(`config_pc.c:445-449`); the menu shows "session is read-only (--faithful)" by
checking the suppression state — static `s_configSaveSuppressed`
(`config_pc.c:445`), set via `configSetSaveSuppressed()` (`:447`, called from
`main_pc.c:732/:749`); **no getter exists today** — add
`configSaveSuppressedActive()` (3 lines, declared in `config_pc.h`).
"Reset all" = `settingsResetAllToDefaults()` (`settings.c:329`).

**Gate flag**: the menu itself is behind `Input.SettingsMenu` (int, default **1**
— the menu is UI chrome, not a look/feel change; with it 0 the build has zero new
behavior; the *frame* is unchanged either way as long as the overlay is closed;
identity screenshots run with the overlay closed).

### 4.3 Input polish

**(a) Rebinding.** Introduce a port-side action table replacing the hardcoded
scancode checks in `osContGetReadData` (`stubs.c:6196-6243`):

```c
typedef enum {  /* one per current hardcoded binding */
    ACT_FIRE, ACT_AIM, ACT_ACTION_B, ACT_START, ACT_TRIG_L,
    ACT_CUP, ACT_CDOWN, ACT_CLEFT, ACT_CRIGHT,
    ACT_FWD, ACT_BACK, ACT_LEFT, ACT_RIGHT,
    ACT_JPAD_U, ACT_JPAD_D, ACT_JPAD_L, ACT_JPAD_R,
    ACT_QUICK_RESTART, ACT_SETTINGS_MENU, ACT_COUNT
} PcAction;

typedef struct { s32 scancode; s32 scancode_alt; s32 mouse_button; } PcBinding;
PcBinding g_pcBindings[ACT_COUNT];   /* defaults == today's map exactly */
```

Serialization: one string setting per action,
`settingsRegisterString("Input.BindFire", buf, 32, "MOUSE1|LSHIFT", ...)` (the
current fire map is mouse-left + LShift, `stubs.c:6220/6241`), parsed
with `SDL_GetScancodeFromName` at load (`platform_sdl.c` registration block).
The N64-button OR-mask assembly in `stubs.c:6214-6243` becomes a loop over the
table; **defaults reproduce the current map bit-for-bit** (identity gate). The
menu's Input page gets a "press a key" capture widget (poll `SDL_GetKeyboardState`
diff for one frame). N64-pad *button* remapping (`SDL_CONTROLLER_BUTTON_*` →
N64 mask, `stubs.c:5865-5910`) uses the same table with a `pad_button` field.

**(b) Gyro aim.** New settings `Input.GyroAim` (0), `Input.GyroSensitivity`
(1.0, 0.1..5.0), `Input.GyroOnlyWhileAiming` (1). Platform side
(`platform_sdl.c`, next to `platformGetPadRightStick` `:2092`):

```c
#if SDL_VERSION_ATLEAST(2,0,14)
void platformGetPadGyroDelta(int k, float *dyaw_rad_s, float *dpitch_rad_s) {
    SDL_GameController *gc = /* slot k */;
    if (gc && SDL_GameControllerHasSensor(gc, SDL_SENSOR_GYRO)) {
        if (!SDL_GameControllerIsSensorEnabled(gc, SDL_SENSOR_GYRO))
            SDL_GameControllerSetSensorEnabled(gc, SDL_SENSOR_GYRO, SDL_TRUE);
        float v[3];                    /* rad/s: x=pitch, y=yaw, z=roll */
        if (SDL_GameControllerGetSensorData(gc, SDL_SENSOR_GYRO, v, 3) == 0) {
            *dpitch_rad_s = v[0]; *dyaw_rad_s = v[1]; return;
        }
    }
    *dyaw_rad_s = *dpitch_rad_s = 0.0f;
}
#endif
```

Game side: inject into the existing look block **as extra `mdx/mdy` pixels**
right after the right-stick fold (`lvl.c:5865-5867`), scaled
`px = rad_s * dt * GYRO_PX_PER_RAD * g_pcGyroSens`, gated by
`live_look_allowed` (already computed, `lvl.c:5803`) so replays are untouched,
and by `insightaimmode` when `GyroOnlyWhileAiming`. No new sim state; it is
literally more mouse delta. Flick-to-recenter etc. is out of scope (v1 = fine-aim).

**(c) Toggle-vs-hold aim.** `Input.AimToggleMode` enum {follow-game(0, default),
hold(1), toggle(2)}. Implementation is *pad-input synthesis*: in the native input
path (`stubs.c` `osContGetReadData`, where `R_TRIG` is assembled `:6221/6242`),
when mode==toggle convert the R-press rising edge into a latched R state per
player (side array, the `g_adsWasAiming[4]` idiom — declared `bondview.c:89`,
edge logic `:12291-12327`). Because
it edits the *pad image* before the sim reads it, replays record the synthesized
result — but for safety it is inert when `get_is_ramrom_flag()`/`g_deterministic`
(same rule as scripted input). Do not touch `cur_player_get_aim_control()`
(`watch.c:612`) or the `insightaimmode` assignments (`bondview.c:11845/:11849`
hold path, `:11644/:11648` stick path) directly.

**(d) Per-weapon sensitivity** is already delivered by `AdsProfile.sens_mult` ×
`Input.AdsSensitivity` (`lvl.c:5894-5896`); W5 only *surfaces* the two user knobs
(`AdsSensitivity`, `MouseSensitivityAim`) in the menu. No new hip-fire-per-weapon
table (scope creep; open question #4).

**(e) 120Hz-feel (input-side only)**: already landed — poll-after-pace commit
`2a22ea2`. Remaining W5 action: a docs/menu preset note ("high-refresh: set
`Video.FrameCap=display`, `Video.VSync=off`") + verify `SDL_SetRelativeMouseMode`
(on at `platform_sdl.c:2397`) delivers raw deltas (SDL hint
`SDL_HINT_MOUSE_RELATIVE_MODE_WARP` audit). Render-time view interpolation is
**out of scope for W5** (roadmap §5 marks it a separate big bet).

### 4.4 Accessibility

**(a) Audio-event captions** (`Input.AudioCaptions`, default 0). Hook: a tiny
shim `portCaptionSfx(s16 sfx_id)` called from **`sndPlaySfxInternal`**
(`src/snd.c:3170`, forward-declared `:1419`) — in the default build
(`PORT_SOUNDPLAYER_REAL=ON`, `CMakeLists.txt:402-416` ⇒ `PORT_SND_STUBS`
undefined, `snd.h:12-14`) that static funnel receives *every* play:
`sndPlaySfx` (`snd.c:3153`; DEBUG variant `:3126`), `sndPlaySfxTagged`
(`:3160`), and the deferred-event path (`:1999`). Call the shim only when the
`soundIndexIsPublic` parameter is nonzero (game callsites pass public ids; the
`:1999` replay path re-triggers an already-captioned sound). Do **not** hook
the `sndPlaySfx` at `snd.c:871/:881` — that is the `PORT_SND_STUBS` half,
compiled out by default. There
are ~220 callsites across `src/game` (`chrobjhandler.c` 89, `front.c` 63,
`gun.c` 31, …) so per-callsite hooking is off the table. A static whitelist maps ~15 gameplay-critical ids
→ caption strings (**our original English text = Tier A1**): alarm, glass break,
door open, grenade bounce, body fall. Render through the existing HUD queue
`hudmsgBottomShow(text)` (`bondview.c:22059`) prefixed `[SFX]`, throttled (same id
≤ once per 2s). Dialogue needs nothing — it is already text (`chrai.c:4615-4633`).

**(b) Colorblind reticle/hitmarker palettes** (`Input.ReticlePalette` enum:
`classic|deuteranopia|protanopia|tritanopia|high-contrast`, default classic).
One indirection at the fill-color seams: `gun.c:33792` (`fill_main` red/green),
the hitmarker kind colors (`gun.c:33686-33713`), and the classic modern-crosshair
tint (`gun.c:33818-33830`). Palette table:

| Palette | "on target" | neutral | hitmarker kill |
|---|---|---|---|
| classic | green | red | red |
| deuteranopia | #0072B2 blue | #E69F00 orange | #E69F00 |
| protanopia | #56B4E9 | #F0E442 | #F0E442 |
| tritanopia | #CC79A7 | #009E73 | #CC79A7 |
| high-contrast | white+black outline | white+black outline | white |

(Okabe-Ito colors — public domain values, code constants only.)

**(c) Flash reduction** (`Input.ReduceFlashes`, default 0): promote
`portSuppressDamageFlash` (`bondview.c:129-135`) from env-only to
`env || setting`, and additionally scale `frac` in `currentPlayerDrawFade`
(`bondview.c:10362`) by 0.4 for *all* full-screen fades brighter than 50% —
purely the drawn overlay; `colourscreen*` sim fields are untouched.

**(d) Screen-shake reduction** (`Input.ReduceScreenShake` float 0..1, default 1 =
full): scale `intensity` inside `viShake` (`src/fr.c:1617`) before the
`g_ViShakeIntensity` store (`:1629`) — the single choke point covering all
callers (`explosions.c:1120` is the live driver; consumption is the VI
vertical offset, `src/fr.c:248-258`). Do NOT scale the `coord3d` out-param of
`explosionScreenShake` — it is dead (§2d), and `bondview.c:15874` is
`NONMATCHING` reference code. Confirm with the
sim-invariance gate that the shake intensity never feeds back into collision/aim
(it is a VI presentation offset; the gate is the proof, not the claim).

**(e) FOV** already shipped (`Video.FovY` 45..105, `platform_sdl.c:1694`) —
menu-surfaced only.

### 4.5 Quality-of-life

**(a) Quick-restart** (`Input.QuickRestart`, default 1; bound to ACT_QUICK_RESTART,
default key `F5`, hold-to-confirm 0.5s): solo missions only, ignores RAMROM/
deterministic/MP. **Record point — must catch front-end boots, not just CLI**:
`pc_apply_level_selection` (`initmenus.c:237`) runs only for `--level`/`--mission`
boots (call sites `initmenus.c:357/:393/:573`), so recording there would make
quick-restart CLI-only. Instead record in `init_menu0B_runstage()` (the
`MENU_RUN_STAGE` init handler, dispatched at `front.c:14411`) — every mission
start funnels through `frontChangeMenu(MENU_RUN_STAGE, …)` (`front.c:5073/:5744/
:8601/:9106`, `ramromreplay.c:1940`), and by then `g_CurrentStageToLoad`
(`lvl.c:338`, set at `:576`) and `selected_difficulty` (setter
`set_selected_difficulty`, `file.c:101`) hold the values to snapshot (skip the
snapshot when the RAMROM path set it — check `ramromreplay` active). On trigger,
run the mission-failed exit flow into `frontChangeMenu(MENU_RUN_STAGE, TRUE)`
(`front.c:5073` idiom) after re-applying the recorded pair via
`pc_apply_level_selection`'s body idiom. The restart is *player input* — identical
in kind to choosing Retry on the failed screen (`front.c:8854-8917`), so no sim
rail issues; the flag only adds a trigger path.

**(b) Mission timer overlay** (`Input.MissionTimer`, default 0): render-side DL
text in the HUD pass: `t = getMissiontimer()` (`bondview.c:26448`), format
`MM:SS.ff` with the `front.c:9224-9227` division idiom (60 ticks/s). Draw with
`textRender` top-right, per-player viewport offsets for split-screen. Read-only
sim access; zero writes.

**(c) First-run onboarding**: `configInit()` already knows first-run
(`config_pc.c:565-567`) — export `s32 configWasFirstRun(void)`. In the front-end
legal/title screen constructor, if first-run and not faithful/deterministic, draw
2 lines ("F1 = Remaster settings · run with --faithful for the untouched 1997
game") for the first 15s. Also print the same to stdout at boot (`main_pc.c:822`
area). One-time: the created ini simply exists next launch.

**(d) Pause-menu polish**: the in-mission settings overlay (4.2 Mount B) gets a
top strip: `Resume · Restart mission (hold) · Settings pages · Quit to menu`,
making it the de-facto modern pause surface — while the authentic watch menu
remains untouched for purists.

### 4.6 File-by-file change map

| File | Change | Epic |
|---|---|---|
| `src/game/ads_profiles.c` | +10-15 authored rows; class-default table in `adsComputeDefault` | E1 |
| `src/platform/stubs.c` | scripted-equip retry loop + trace (`:4840-4930`); binding-table refactor of `:6196-6243` & `:5865-5910`; toggle-aim latch | E1/E3 |
| `src/game/gun.c` | RNG audit assert (`:29122`); palette indirection (`:33686-33830`) | E1/E4 |
| `src/random.c` | `g_rngDrawCount` under `-DGE007_RNG_AUDIT` (`randomGetNext`, `:270`) | E1 |
| `src/game/pc_settings_menu.{c,h}` | **new** — model/controller/DL renderer, both mounts | E2 |
| `src/bondconstants.h` | `MENU_PC_SETTINGS` before `MENU_MAX` (`:1566`) | E2 |
| `src/game/front.c` | 4 dispatcher cases (switches `:14360/14399/14428/14493`); modesel entry tab | E2 |
| `src/game/lvl.c` | overlay toggle + input swallow next to `:5786`; gyro delta injection at `:5865` | E2/E3 |
| `src/platform/platform_sdl.c` | ~12 new `settingsRegister*` calls; `platformGetPadGyroDelta`; binding parse | E2/E3/E4/E5 |
| `src/platform/config_pc.c` | `configWasFirstRun()`, `configSaveSuppressedActive()` getters | E2/E5 |
| `src/game/bondview.c` | flash-reduce in `currentPlayerDrawFade`; timer overlay draw; caption queue reuse | E4/E5 |
| `src/fr.c` | shake scale inside `viShake` (`:1617-1629`) | E4 |
| `src/snd.c` | `portCaptionSfx` hook inside `sndPlaySfxInternal` (`:3170`) | E4 |
| `src/game/front.c` | record `(level,diff)` in `init_menu0B_runstage()` for quick-restart (§4.5(a)) | E5 |
| `tools/ads_pose_capture.sh` | precondition grep on equip trace; bake-regression mode | E1 |
| `docs/VISUAL_MODES.md` | new flag rows | all |

---

## 5. Work breakdown

Estimates in **junior-engineer-days**. Rails column: R1 = gameplay-invariance
note, R3 = the gating flag (R2 is trivially satisfied — no assets anywhere in W5;
all UI/captions are code/original text = Tier A1).

### Epic W5.E1 — ADS completion (finish `docs/ADS_PLAN.md` §10.3)

| ID | Task | Files | Steps | Acceptance (runnable) | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W5.E1.T1 | **Fix the scripted equip/aim harness** | `stubs.c` (equip block `:4840-4930`; scripted buttons `:5918+`), `tools/ads_pose_capture.sh` | Retry-until-equipped loop keyed on `getCurrentPlayerWeaponId(GUNRIGHT)`; emit `[ADS-HARNESS] equipped item=N`; script hard-fails if the line is absent | `for i in 1 2 3; do tools/ads_pose_capture.sh kf7_$i 8; done` → all 3 captures show the KF7 (grep the trace line); flaky rate 0/10 | 3 | — | Debug/env-only hooks (`GE007_AUTO_*`), never in normal play; R3 n/a |
| W5.E1.T2 | **Author the remaining weapon poses** (DD44, Klobb, ZMG, D5K, Phantom, Spectre, shotguns, Golden Gun, Magnum, knives, launcher/heavy audit → pose 0 where odd). Scope note: the 5 existing "authored" rows also still use the shared universal pose (`ads_profiles.c:104-108` — only FOV/sens/movement are per-weapon today), so audit those 5 too and dial poses where the sight line is off | `ads_profiles.c:108-124` | §4.1 workflow per weapon; heavy/thrown items reviewed for pose-0 | Per weapon: capture with baked row == capture with dialed env (`compare_screenshots.py --max-changed-pct 0.10`); visual sign-off contact sheet | 4 | E1.T1 | `Input.AdsEnabled=0` untouched (profiles resolved via `adsGetProfile`, `ads_profiles.c:174`, consulted only under `g_pcAdsEnabled` at the call sites) |
| W5.E1.T3 | **Run the three ADS-0.3 gates** (build what's missing) | `gun.c:29122`, `src/random.c:270` RNG counter, `tools/` | (1) RNG audit counter + 4-draws/shot assert under `-DGE007_RNG_AUDIT`; (2) RAMROM byte-identity `AdsEnabled=0` vs pre-ADS baseline; (3) split-screen asymmetric-ADS smoke | (1) audit build fires on a seeded 5th draw, green on tree; (2) `tools/sim_invariance_gate.sh` variant (copy its `run()` harness, `sim_invariance_gate.sh:42-57`, swapping the toggled `--config-override` set for `Input.AdsEnabled=0` vs pre-ADS baseline) — sim hash identical + screenshot sha equal; (3) `tools/mp_smoke.sh` variant with P1 ADS-in shows asymmetric FOV in trace | 4 | — | R1: proves it; R3: `Input.AdsEnabled` |
| W5.E1.T4 | **Recoil-site confirmation + optional pose rotation wiring** | `gun.c` (ADS-7.1 sites; `portAdsResolvePose` rotation outputs) | Prove `hands[hand].field_A84/A88` consumption is on the *active* path (the `[VM-ANCHOR]` diagnostic technique from ADS_PLAN §10.1); wire `dyaw/dpitch/droll` into `mtx_d` only if a T2 weapon needs it | With `Input.AdsRecoilReduce=0.5`, recoil visibly halves in a 10-shot capture strip; `=0.0` byte-identical | 2 | E1.T2 | R3: `Input.AdsRecoilReduce` (default 0.0) |
| W5.E1.T5 | **Class-default table + ease-out** | `ads_profiles.c:129-172`; blend fraction consumer | 4-class default rows; optional smoothstep on the profile blend timed by `ads_in_time`/`ads_out_time` (`ads_profiles.h:23-24`) behind `Input.AdsEaseOut` (default 0) | Unauthored weapon (e.g. Taser) sits sanely; ease A/B captures at t=25/50/75% | 2 | E1.T2 | R3: `Input.AdsEaseOut` |

**Epic total: 15 jd.** Exit = ADS_PLAN §6 "Definition of done" fully checked.

### Epic W5.E2 — In-game Remaster Settings menu

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W5.E2.T1 | **Model + curation table** | new `pc_settings_menu.c` | `SmEntry/SmPage` (§4.2); 7 pages, ~55 keys; derive widget from `Setting.type`; hide entries where `settingsFind` (`settings.c:183`) returns NULL (§4.2 missing-key rule) | `ctest -R settings_menu_model` green — register `port_settings_menu_model` via `add_test` (idiom: existing guards, `CMakeLists.txt:38-48`) running a new `tools/check_settings_menu_model.py` that regex-extracts every `.key` string from the curation table and every `settingsRegister*("` key from `platform_sdl.c`/`audio_pc.c`, failing on any table key with no registration unless the entry is tagged `SM_FUTURE` (the W6 audio rows) | 2 | — | No behavior change; R3 n/a |
| W5.E2.T2 | **DL widget renderer** | `pc_settings_menu.c` | Panel/rows/slider/enum/toggle via `gDPFillRectangle`; text via `textRender` (Zurich-font idiom `bondview.c:20062/:20108` — §4.2 Renderer); per-player viewport safe | Headless capture on Dam with overlay forced open (`GE007_SETTINGS_MENU_FORCE=1` — new debug env added by this task) renders all widget types; **Metal parity**: same capture under `GE007_RENDERER=metal` (read `gfx_backend.c:16`) non-blank & identical layout | 4 | E2.T1 | Draw-only; overlay-closed frame byte-identical (screenshot sha) |
| W5.E2.T3 | **Front-end mount** | `bondconstants.h:1566`, `front.c` switches `:14360/14399/14428` in `menu_init` (`:14282`) + `:14493` in `menu_jump_constructor_handler` (`:14480`) + modesel tab (`constructor_menu06_modesel`, `front.c:4014`) | New `MENU_PC_SETTINGS`; init/update/interface/constructor quad; enter from mode-select, exit restores `MENU_MODE_SELECT` | Boot to menu → SETTINGS tab → navigate all pages with pad AND arrows; `--faithful` shows read-only banner and `ge007.ini` mtime unchanged after a session | 3 | E2.T2 | R3: `Input.SettingsMenu`; faithful read-only proven (`config_pc.c:445`) |
| W5.E2.T4 | **In-mission overlay mount** | `lvl.c:5786` block, HUD pass seam | F1 toggle (via binding table once E3.T1 lands; raw scancode until then); input swallow (`g_freezeInput` idiom `lvl.c:5813`); refuse under `g_deterministic`/`get_is_ramrom_flag()` | In Dam: F1 opens, gameplay input dead, look dead; close restores; `--deterministic` run: F1 inert, screenshot sha equals baseline | 3 | E2.T2 | R1: overlay never opens under replay; R3: `Input.SettingsMenu` |
| W5.E2.T5 | **Live-apply audit + hooks** | registration sites `platform_sdl.c:1490-1897`, apply fns | Per-key: verify per-frame consumption or add hook (`platformApplyWindowMode` `:1427`, `platformApplyVSync` `:1447`); demote to `SETTING_SCOPE_RESTART` anything unsafe; render `⟳` tags | Scripted pass: toggle every LIVE key in-menu on Dam → no crash, visible effect or documented no-op; RESTART keys visibly tagged; `tools/asan_smoke.sh` green with menu-thrash input script | 3 | E2.T3/T4 | Scope metadata already in registry (`settings.c:200-207`) |
| W5.E2.T6 | **Persistence + reset + validation sweep** | `pc_settings_menu.c`, `config_pc.c` getters | Save-on-close via `configSave()`; page/all reset via `settingsResetAllToDefaults()` (`settings.c:329`); first full A/B suite | `tools/config_roundtrip_check.py` green; set 5 keys in-menu → quit → relaunch → `--dump-config` shows them; identity gate: overlay-closed frame sha == pre-branch sha | 2 | E2.T5 | R3 identity screenshot required in PR |

**Epic total: 17 jd.**

### Epic W5.E3 — Input polish

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W5.E3.T1 | **Binding table + rebinding UI** | `stubs.c:6196-6243, 5865-5910`, `platform_sdl.c`, menu Input page | §4.3(a): `PcAction`/`PcBinding`, string settings `Input.Bind*`, parse with `SDL_GetScancodeFromName`; loop-ify the OR-mask assembly; capture widget | Defaults byte-identical: scripted-input smokes (`tools/scripted_look_smoke.sh`, `tools/playability_smoke.sh --all`) green unchanged; rebind fire→`G` works live; bad string falls back to default with a warning | 5 | E2.T3 (UI) | R3: defaults == today's map; keys are strings in `ge007.ini` |
| W5.E3.T2 | **Gyro aim** | `platform_sdl.c` (§4.3(b)), `lvl.c:5865` | `platformGetPadGyroDelta` under `SDL_VERSION_ATLEAST(2,0,14)`; inject as mdx/mdy behind `live_look_allowed`; 3 new settings | On a DualSense: physical rotation moves view only when `Input.GyroAim=1` (+ only-while-aiming honored); on sensor-less pad: zero-delta, no log spam; `--deterministic` unaffected (input frozen, `main_pc.c:596`) | 4 | — | R1: inside the live-look gate (`lvl.c:5803-5812`); R3: `Input.GyroAim=0` |
| W5.E3.T3 | **Toggle-vs-hold aim** | `stubs.c` R_TRIG assembly (`:6221/6242` + pad path) | §4.3(c) latched-R synthesis, per-player side array; inert under ramrom/deterministic | Toggle mode: tap R → aim persists, tap again → drops; hold mode unchanged; **RAMROM gate**: recorded replay hash identical with mode=toggle set (`tools/sim_invariance_gate.sh` variant — copy its `run()` harness `:42-57`, toggle `Input.AimToggleMode=0` vs `=2`) | 3 | — | R1: pad-image synthesis, replay-inert; R3: `Input.AimToggleMode=0` |
| W5.E3.T4 | **High-refresh audit + docs** | `platform_sdl.c` mouse path, VISUAL_MODES.md | Verify relative-mouse raw deltas; document `FrameCap=display`+`VSync=off` recipe; confirm `2a22ea2` reorder still in place post-merges | Manual 120Hz check + a one-para VISUAL_MODES section; no code change expected | 1 | — | n/a (docs/audit) |
| W5.E3.T5 | **Surface sens knobs in menu** | menu curation table | Add `Input.MouseSensitivity[Aim]`, `Input.AdsSensitivity`, gamepad feel keys to Input page | Slider changes take effect next frame (consumed `lvl.c:5870-5874`) | 1 | E2.T3 | already-shipped keys |

**Epic total: 14 jd.**

### Epic W5.E4 — Accessibility

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W5.E4.T1 | **Colorblind reticle/hitmarker palettes** | `gun.c:33686-33830` | Palette table (§4.4b); route the three fill-color sites through it; `Input.ReticlePalette` enum | Captures of all 5 palettes on-target/off-target/hitmarker; `classic` byte-identical to today (sha) | 2 | — | R3: enum default `classic` = identity |
| W5.E4.T2 | **Flash reduction** | `bondview.c:10362` (`currentPlayerDrawFade`), `:129-135`, `:10373-10376` | Promote env to `env||Input.ReduceFlashes`; scale bright full-screen fades | With flag: pickup flash gone, damage fade ≤40% alpha (capture A/B); flag off byte-identical; `tools/damage_hud_smoke.sh` green | 2 | — | R1: draws only (`colourscreen*` unread-back); R3: default 0 |
| W5.E4.T3 | **Screen-shake reduction** | `src/fr.c:1617-1629` (`viShake`); live caller `explosions.c:1120` | Scale `intensity` in `viShake` by `Input.ReduceScreenShake` before the `g_ViShakeIntensity` store (§4.4d — do NOT touch the dead offset path) | Grenade-at-feet capture pair (full vs 0.2 shake); **sim gate**: `tools/sim_invariance_gate.sh` variant (`run()` harness `:42-57`, toggle `Input.ReduceScreenShake=1.0` vs `=0.2`) — sim hash identical (proves intensity never feeds sim) | 2 | — | R1: gate-proven; R3: default 1.0 (full) |
| W5.E4.T4 | **Audio-event captions** | `src/snd.c:3170` (`sndPlaySfxInternal` — see §4.4a; NOT the `:871/:881` stub half), `bondview.c:22059` reuse | §4.4(a): `portCaptionSfx` + ~15-id whitelist + 2s throttle; `[SFX]`-prefixed via hudmsg queue | Trip Dam alarm with `Input.AudioCaptions=1` → `[SFX] Alarm` bottom-left; off = zero hook cost (guarded call); MP unaffected (per-player queue path `bondview.c:22078-22088`) | 4 | — | R2: caption strings are original text (A1); R3: default 0; R1: hudmsg is existing presentation state |

**Epic total: 10 jd.**

### Epic W5.E5 — Quality-of-life

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W5.E5.T1 | **Quick-restart** | record in `init_menu0B_runstage()` (`front.c:14411` dispatch — §4.5(a); NOT `initmenus.c:237`, which is CLI-only); trigger in `lvl.c` native block; `front.c:5073` idiom | §4.5(a): hold-F5-0.5s → confirm → re-apply `(level,diff)` → `MENU_RUN_STAGE` | Dam booted from the FRONT-END menu (not `--level`): F5-hold restarts to mission start in <3s; same via `--level dam` boot; inert in MP/ramrom/deterministic; 20-restart soak no leak (`tools/asan_smoke.sh` variant) | 3 | E3.T1 (binding) | R1: player-input path only; R3: `Input.QuickRestart` |
| W5.E5.T2 | **Mission timer overlay** | HUD pass in `bondview.c`; `getMissiontimer` (`bondview.c:26448`) | §4.5(b) MM:SS.ff `textRender` top-right, split-screen offsets | Timer matches mission-complete screen time (`front.c:9224-9227`) on a played run; off = byte-identical; works on Metal (`GE007_RENDERER=metal` capture) | 2 | — | R1: read-only sim access; R3: `Input.MissionTimer=0` |
| W5.E5.T3 | **First-run onboarding** | `config_pc.c:560-568` getter; title constructor; `main_pc.c:822` | §4.5(c): 15s two-line hint, first run only, suppressed under faithful/deterministic | `rm ./ge007.ini` (config is CWD-relative, `config_pc.h:20`) → boot → hint visible frame 100 capture; second boot → absent; `--faithful` → absent | 2 | — | R3: implicit (first-run only), plus `Input.OnboardingHints` master (default 1) |
| W5.E5.T4 | **Pause strip on the overlay** | `pc_settings_menu.c` | Resume / Restart(hold) / Quit-to-menu row atop Mount B | All three actions work in Dam; Quit lands on mode-select without leaking mission state (`tools/playability_smoke.sh` level-cycle green) | 2 | E2.T4, E5.T1 | same flags as menu |

**Epic total: 9 jd.**

**Workstream total: 65 junior-days ≈ 13 junior-weeks** (one junior ~5 jd/wk; two
juniors in parallel ≈ 7 calendar weeks — E1 and E2 are independent start points).

---

## 6. Milestones & deliverables

| M | Name | Contents | Demo script (reviewer runs) |
|---|---|---|---|
| M1 | **ADS Complete** (E1) | all poses authored, 3 gates green, recoil confirmed | `./build/ge007 --level 33 --config-override Input.AdsEnabled=1` → aim every Dam weapon; then `tools/sim_invariance_gate.sh` + the ADS split-screen smoke |
| M2 | **Settings menu** (E2) | front-end + in-mission menu, live apply, persistence | `./build/ge007 --remaster --level 33` → F1 → drag Bloom/Vignette/FOV live → save → relaunch shows persisted values |
| M3 | **Modern input** (E3) | rebinding, gyro, toggle-aim, sens surfaced | `./build/ge007 --level 33` → menu Input page → rebind fire to G, enable gyro on a DualSense, set aim=toggle; play 2 min |
| M4 | **Accessibility pack** (E4) | palettes, flash/shake reduction, captions | `./build/ge007 --level 33 --config-override Input.ReticlePalette=1 --config-override Input.AudioCaptions=1 --config-override Input.ReduceFlashes=1` → trip the alarm, take damage |
| M5 | **Speedrun/QoL** (E5) | quick-restart, timer, onboarding, pause strip | `rm ge007.ini; ./build/ge007 --remaster` (hint shows) → start Dam, `Input.MissionTimer=1`, F5-hold restart |

Each milestone independently landable; M2-M5 each end with the full §8 validation
sweep.

---

## 7. Risks & mitigations (ranked)

| # | Risk | Likelihood/Impact | Mitigation | Kill / de-scope criterion |
|---|---|---|---|---|
| 1 | **Live-apply of a "live"-scoped key crashes or corrupts render state mid-mission** (e.g. RenderScale reallocation on Metal) | M / H | E2.T5 per-key audit; anything not provably per-frame-consumed → `SETTING_SCOPE_RESTART` (1-line change); ASan menu-thrash script | If a key can't be made safe in <0.5d, tag restart — never chase hot-reload |
| 2 | **Toggle-aim / gyro perturbs replays** | M / H | Both inert under `get_is_ramrom_flag()`/`g_deterministic`; RAMROM hash gate in acceptance (E3.T2/T3) | Any residual divergence → ship hold-only / gyro-experimental-env-only and cut the menu rows |
| 3 | **Front-end integration regressions** (menu dispatcher is 1997 spaghetti; `MENU_MAX` append could collide with save/replay-recorded menu indices) | M / M | Append-only enum; new screen reachable only via new tab; `tools/playability_smoke.sh --all` + full front-end click-through per PR | If dispatcher wiring costs >1.5× estimate, drop Mount A and ship overlay-only (Mount B covers 90% of value) |
| 4 | **In-mission fonts/DL budget**: menu DL overflows the frame DL or fonts absent on some stage | L / M | hudmsg proves fonts resident; row-count cap (12 visible, scroll); measure DL usage on Jungle (worst) | If any stage lacks fonts, fall back to the front-end mount + a "settings unavailable here" toast |
| 5 | **Rebinding identity break** (loop-ified map ≠ old hardcoded map) | L / H | Defaults literally generated from the old map; scripted-input smokes are the oracle (they inject scancodes) | Any smoke diff → revert to hardcoded path behind `Input.RebindEnabled=0` |
| 6 | **Gyro hardware variance** (drift, axis conventions per pad) | M / L | v1 = rate-only fine-aim (no fusion, no recenter); sensitivity slider; documented as "beta" | If DualSense+SwitchPro can't both feel right in 4d, ship env-only `GE007_GYRO=1` and defer the menu row |
| 7 | **Caption hook touches the audio hot path** | L / L | Guarded single branch when `AudioCaptions=0`; whitelist lookup is a 15-entry switch | If the dispatch has >1 choke point, caption only the alarm (the one deaf-critical cue) |
| 8 | **Quick-restart leaks mission state across restarts** | L / M | Reuse the exact Retry flow (`front.c:8854`), which the game already soaks; 20-restart ASan soak | Leak found in engine (not ours) → keep feature, file the leak separately |

---

## 8. Validation strategy (per-commit, roadmap §7 pattern)

**Identity gate (every PR in this workstream):**

```bash
cmake --build build -j && \
SDL_AUDIODRIVER=dummy GE007_DETERMINISTIC_STABLE_COUNT=1 GE007_NO_VSYNC=1 \
GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
./build/ge007 --level 33 --deterministic --trace-state trace.jsonl \
  --screenshot-frame 400 --screenshot-label feature_off \
  --config-override Video.RemasterFX=0 --config-override Video.RenderScale=1 \
  --config-override Input.ModernCrosshair=0 --config-override Input.HitMarkers=0
# vs the same command on main; then:
python3 tools/compare_screenshots.py base.bmp feature_off.bmp --max-changed-pct 0
python3 tools/audit_screenshot_health.py feature_off.bmp
python3 tools/audit_render_trace.py trace.jsonl
```

All W5 flags default to identity, so the *feature-off* frame must be
**0 changed pixels** and the render-trace counters equal.

**Feature A/B:** same harness with only the target flag flipped;
per-feature `--max-changed-pct` budgets: reticle palette 0.10, flash-reduce 5.0
(whole-screen fade), timer overlay 0.50, menu overlay open = layout capture
(golden local, never committed — R2).

**Sim invariance (required for E3.T2/T3, E4.T3, E5.T1 — anything input- or
view-offset-adjacent):**

```bash
tools/sim_invariance_gate.sh          # dam1 RAMROM: flags OFF vs ON, sim hash equal
python3 tools/compare_state.py a.jsonl b.jsonl   # bootstrap trace compare
```

**Both renderers:** every DL-drawing task (menu, timer, captions, reticle)
captures once under default GL and once under `GE007_RENDERER=metal` — non-blank,
same layout (the minimap lesson, `fast3d/gfx_pc.c:23262`).

**Broad gates:** `tools/playability_smoke.sh --all` (level cycle),
`tools/mp_smoke.sh` (split-screen, incl. the asymmetric-ADS variant from E1.T3),
`tools/asan_smoke.sh` (+ menu-thrash and 20-restart scripts), `ctest` (all
ROM-free guards incl. the new `settings_menu_model` test),
`scripts/ci/check_no_rom_data.sh` (no captures committed),
`scripts/ci/check_sim_render_separation.sh` + `check_timing_lock.sh` (unchanged —
W5 adds no renderer-backend reads to `src/game` and never touches `g_ClockTimer`).

**ADS-specific:** the three ADS-0.3 gates (E1.T3) become release checklist items:
RNG-draw audit build, RAMROM byte-identity `AdsEnabled=0`, split-screen
asymmetric ADS.

---

## 9. Open questions

1. **Defaults-on chrome flags** — `Input.SettingsMenu`, `Input.QuickRestart`,
   and `Input.OnboardingHints` are all proposed default **1** (UI chrome ≠ look
   change; untouched-key frame is identity, and the §8 identity gate runs with
   no keys pressed so it still passes). If the program owner wants the strictest
   reading of R3/§1c ("all-off build has zero new code paths reachable"), flip
   all three defaults to 0 and have `--remaster` enable them — one decision
   covering all three.
2. **F1/F5 key choices** for menu/quick-restart — any conflict with a future
   debug-key plan? (Both rebindable after E3.T1, so only the *defaults* need
   blessing.)
3. **Caption wording/coverage** — the 15-id whitelist (alarm, glass, doors,
   grenade, body-fall, gunfire-near) needs a native-speaker/UX pass; do we also
   caption music stings (objective complete already has text)?
4. **Per-weapon hip-fire sensitivity** — deliberately not built (ADS already has
   per-weapon `sens_mult`; hip-fire per-weapon sens is unusual even in modern
   shooters). Confirm cut.
5. **Toggle-aim in recorded RAMROMs** — we synthesize the pad image, so a replay
   *recorded* with toggle mode plays back fine; is record-with-toggle a supported
   speedrun submission format, or should recording force hold mode for parity
   with console? (Affects one guard line in E3.T3.)
6. **Onboarding copy** — exact two lines + whether the stdout note should also
   mention `GE007_TEXTURE_PACK` (Tier B, user-built) or keep the first-touch
   surface license-silent.
