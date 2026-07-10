# Display & Input Options — Execution Plan and Regression Matrix

GoldenEye was authored for a 4:3 CRT, one hardwired controller, and a ~20–60 fps
variable frame rate. The native port runs the campaign and split-screen, but
presents that experience the way the N64 did. This track makes the port feel
native to **modern displays and modern input** — widescreen/4K, real fullscreen
modes, FOV control, anti-aliasing, and fully rebindable keyboard/mouse/gamepad —
surfaced through a port-owned settings UI that a first launch configures
sensibly on its own.

It is the "polish track" from [ROADMAP.md](../../ROADMAP.md). It is rated
high-value / low-difficulty because — as the grounding below shows — the renderer
already has a draw-class signal and an FBO post-pass, the config system already
round-trips, and the input layer already has a single authoritative write point.
The work is composition in `src/platform/`, off the decompiled game logic.

Status legend: ✅ landed · 🟡 partial · ⬜ not started. Effort: S/M/L/XL.

---

## What "S-tier" means here (acceptance bar for the whole track)

1. **One declarative source of truth.** Every option is one row in a settings
   schema. Config load/save, env override, CLI parse, the UI, and `--dump-config`
   introspection all derive from that one table. Adding an option is one row.
2. **Lossless, atomic, self-documenting config.** Unknown keys survive a
   load→save cycle (forward/back-compat). Saves are atomic (temp+rename). The
   file carries per-key comments, ranges, and defaults so it is hand-editable.
3. **Live where safe, restart where not — never a lie.** Settings that can apply
   instantly do; the rest are badged "restart required" and never silently no-op.
4. **Determinism and replay are inviolable.** No new state perturbs
   `--deterministic`, demo/attract playback, or RAMROM replay. This is a *tested*
   invariant, not a hope.
5. **Authentic defaults, modern ceiling.** Out of the box it looks/feels like
   people remember (4:3-correct geometry, classic feel); every modern affordance
   is one toggle away and nothing that alters the look is forced.
6. **Every phase ships with a gate.** Each lands with a headless test extending
   the existing lanes (`docs/INSTRUMENTATION.md`). No feature is "done" by eye.

---

## Cross-cutting architecture (the generalizable patterns)

These five patterns are the backbone. Every phase is an application of them, which
is what keeps the surface small and uniform.

### P1 — Settings Schema Registry (`src/platform/settings.c`, new)

Today config is register-by-pointer with three types and no metadata
(`config_pc.c:88-104`), env overrides are hand-checked with scattered `getenv`
calls, and CLI flags are a hand-rolled `if/strcmp` ladder (`main_pc.c:480-592`).
Replace the *front end* of all three with one declarative table; keep
`config_pc.c` as the INI back end it already is.

```c
typedef struct {
    const char *key;        /* "Video.Aspect"                              */
    SettingType type;       /* INT | UINT | FLOAT | ENUM | STRING | BIND   */
    void       *ptr;        /* live variable                               */
    Range       range;      /* min/max or enum-token list                  */
    Variant     def;        /* default (for reset + self-documenting save) */
    SettingScope scope;     /* LIVE | RESTART                              */
    const char *env;        /* "GE007_ASPECT" or NULL                      */
    const char *cli;        /* "--aspect" or NULL                          */
    void      (*apply)(void);/* push live value into the subsystem (P2)    */
    const char *label, *help;/* UI text                                    */
} Setting;
```

One table → derives: INI load/save, env override, CLI parse, UI rows, `--list-
settings`/`--dump-config`. `config_pc.c` gains `configRegisterEnum/String` and
**raw-line passthrough** so unregistered keys round-trip instead of being dropped
by `configSave` (`config_pc.c:202-235`), plus **atomic save** (write `*.tmp`,
`rename`) to replace the truncating direct `fopen("w")` at `config_pc.c:205`.

Do not force every process flag through the persistent settings schema. Keep
boot/test flags such as `--rom`, `--savedir`, `--level`, `--ramrom`,
`--deterministic`, screenshot/trace flags, and provenance/debug `getenv()` probes
as runtime/instrumentation controls. The schema owns user-facing settings only.
Implementation detail: parse **early args** first (`--savedir`, ROM/direct-boot,
automation flags needed before initialization), then register the schema, load the
file, apply env overrides, and finally apply schema-owned CLI overrides.

### P2 — Apply / observer

Each `Setting.apply()` pushes the current value into its subsystem. `LIVE`
settings call it on change; `RESTART` settings badge instead. Display apply re-
uses the existing fullscreen path (`SDL_SetWindowFullscreen` `platform_sdl.c:
1127/1225`) and adds a window-size apply path (`SDL_SetWindowSize`, then renderer
resize invalidation). Aspect/FOV apply re-call `viSetAspect/viSetFovY`
(`fr.c:1929-1952`). FOV/aspect are also rewritten by game-side viewport code
(`bondviewMovePlayerUpdateViewport` sets `FOV_Y_F` and recomputes aspect at
`bondview.c:13921-13990`), so those apply callbacks need a persistent desired
value that is consumed at the per-player viewport/render handoff, not only a
one-shot call. No subsystem polls config; config pushes to subsystems.

### P3 — Input action model, two outputs, one binding source, live-only

The port has **two** input output paths and a binding must drive both:

| Path | Live entry | Sink | Replay handling |
|---|---|---|---|
| Digital | `osContGetReadData` `stubs.c:4844`, writes `OSContPad.button` `:5269` | fills `CONTDATA_REGULAR` | replay overwrites a *different* buffer (`CONTDATA_PLAYBACK`) downstream |
| Analog look | `platformGetMouseDelta` `platform_sdl.c:937`, `platformGetPadRightStick` `:1027` | injected to `player->vv_theta/vv_verta` `lvl.c:5742/5744` | no replay path today; must be gated explicitly |

The remap layer resolves `physical → action → (button bit | analog axis)` from a
per-player binding table, applied **only to live input** inside `osContGetReadData`
and inside the two analog getters. For digital input, RAMROM replay reads
`CONTDATA_PLAYBACK` (`joy.c:71`, `joySetContDataIndex:1149`) and the demo handler
writes canned bytes straight into the sample buffer (`ramromreplay.c:1494-1505`),
so remapping live digital input cannot touch replayed digital samples. Analog
look is different: it bypasses the controller sample buffer entirely. Add one
shared `pcLiveLookAllowed()` gate (or equivalent) used by `lvl.c`/the analog
getters that returns false under `g_freezeInput`, RAMROM playback
(`get_is_ramrom_flag()` / `joyGetContDataIndex()==CONTDATA_PLAYBACK`), demo
playback, and overlay input capture. This fixes the latent freeze bug
(`platformGetMouseDelta` ignores `g_freezeInput` today) and makes replay-safety a
property of both output paths, not just `OSContPad`.

### P4 — DrawClass-driven 2D layout policy

The renderer already tags 2D draws. `DrawClass` (`gfx_pc.h:68-76`),
`gfx_set_draw_class()` (`fast3d/gfx_pc.c:481`), read via `g_current_draw_class`
(`fast3d/gfx_pc.c:434`). Game code sets `DRAWCLASS_HUD` immediately before HUD
draws (`lvl.c:1803`) and `DRAWCLASS_WEAPON` around the viewmodel; menus/cutscenes
never set HUD. So widescreen 2D handling is a **table** keyed on draw class,
consumed at the shared rectangle path (`gfx_draw_rectangle` `fast3d/gfx_pc.c:
11854`) and its two primitive callers (`gfx_dp_texture_rectangle` `:11938`,
`gfx_dp_fill_rectangle` `:12062`):

| DrawClass / state | Layout policy |
|---|---|
| `DRAWCLASS_HUD` | anchor to true viewport edges/center |
| `DRAWCLASS_WEAPON` | viewmodel FOV compensation (2c) |
| else (menus/cutscene, `lvlGetCurrentStageToLoad()==LEVELID_TITLE` `lvl.c:745`, or `current_menu` `front.c:832`) | **pillarbox to 4:3** (never stretch authored art) |

This retires the diagnostic-only X-scale hack (`fast3d/gfx_pc.c:8246-8282`).

### P5 — Render-target chain

Generalize the existing single output-filter FBO (`g_output_filter_fbo`
`gfx_opengl.c:949`; scene→texture→shader→window with full GL state save/restore
`:1422-1565`) into a named offscreen target the scene renders **into** at a chosen
scale/sample-count, then resolves through a post chain (downsample → gamma →
optional VI filter) to the window. This is the seam the future lighting/post-FX
track also plugs into.

---

## What already exists (build on, don't rebuild)

| Capability | State | Evidence |
|---|---|---|
| INI config, clamp, round-trip save, public lookup | ✅ int/float/uint/enum/string | `config_pc.c:88-235`, `configFindEntry:239` |
| Savedir (portable + `$HOME/.ge007`) | ✅ | `savedir.c:43-94` |
| FBO + fullscreen-tri shader post-pass w/ GL state save/restore | ✅ | `gfx_opengl.c:1362-1565` |
| Window mode enum + Alt+Enter live toggle | 🟡 windowed/borderless/exclusive token path; no mode picker yet | `platform_sdl.c:1126-1226` |
| HiDPI drawable sizing per-frame | ✅ | `fast3d/gfx_pc.c:14345-14351` |
| Active widescreen correction | 🟡 Fast3D screen-space X scale, not first-class aspect modes | `fast3d/gfx_pc.c:8246-8282`, applied to vertices at `:8816` and rectangles at `:11879`; the old projection tweak lived in the legacy `src/platform/gfx_pc.c`, which has since been removed from the tree (see `CHANGELOG.md`) |
| **DrawClass tags on 2D draws** | ✅ already set by game | `gfx_pc.h:68-76`, `lvl.c:1803`, `fast3d/gfx_pc.c:434/481` |
| Per-player FOV/aspect setters | ✅ | `viSetFovY/viSetAspect/viSetFov` `fr.c:1929-1952` |
| Mouse sens / aim-sens / invert-Y | ✅ configurable | `platform_sdl.c:791-793,931-933`, applied `lvl.c:5726-5744` |
| Single authoritative live-button write point | ✅ | `osContGetReadData` `stubs.c:5269-5272` |
| Replay isolation (separate cont buffer) | ✅ | `joy.c:71`, `joySetContDataIndex:1149`, `ramromreplay.c:1494` |
| Adaptive-vsync logic | ✅ exposed as `Video.VSync` | `platform_sdl.c:1160-1162` |

### Gaps this plan closes

- No bind config yet; exact comment/order raw-line preservation is still pending.
- Env overrides hand-checked, CLI hand-parsed, no introspection/UI — no single schema.
- Scene renders straight to the window (`on_resize` is a no-op `gfx_opengl.c:1662`); no render-scale, no MSAA, no gamma.
- No in-game display settings UI yet; display/mode selection is available through
  the settings schema and `--list-displays`.
- Split-screen aspect needs validation/hardening. `player_2.c:477` initializes
  `DEFAULT_ASPECT`, but `bondviewMovePlayerUpdateViewport` recomputes from the
  current viewport (`bondview.c:13982-13990`) before `lvl.c:1515-1522` consumes
  `g_CurrentPlayer->aspect`; prove the native path is geometrically correct for
  2/3/4-player before changing it.
- 2D widescreen handling is a diag-only X hack, not a draw-class policy.
- Input fully hardcoded (`stubs.c:5129-5169`, `:4784-4842`); no action layer, presets, gamepad curve/deadzone, or per-device profiles; freeze doesn't zero analog look.
- No runtime SDL settings overlay; the macOS `PreferencesView` exists but is
  currently `UserDefaults`-backed, and `game_config_get/set_*` in
  `GameBridge.c:568-592` are stubs. Most config values are load-at-startup with
  no live apply path.

---

## Phase 0 — Settings Schema Registry & config hardening (M)

The foundation (P1). Everything else is rows + apply callbacks.

| Task | Effort | Status | Detail |
|---|---|---|---|
| 0a. `src/platform/settings.c` schema + `Setting` table | M | 🟡 | Baseline schema exists for scalar, enum, and string settings; BIND rows and richer apply metadata remain. |
| 0b. `configRegisterEnum`/`configRegisterString` | S | ✅ | Enums serialize as stable tokens (`borderless`), and strings round-trip with capacity-bounded storage. |
| 0c. Unknown-key passthrough | S | 🟡 | Unregistered key/value entries survive load→save; exact comment/order raw-line preservation remains future work. |
| 0d. Atomic save | S | ✅ | Write `ge007.ini.tmp` then `rename`; never truncate the live file (`config_pc.c:205`). |
| 0e. Self-documenting save | S | ✅ | Emits schema comments with label, help, type, scope, default, and range above each registered key. |
| 0f. Precedence + env-shadow surfacing | S | 🟡 | Runtime precedence **CLI > env > file > default** is wired and tested; UI read-only surfacing waits for the settings overlay. |
| 0g. `--dump-config` / `--list-settings` / `--config-set k=v` / `--reset-config` | S | ✅ | Introspection plus scriptable no-ROM config set/reset are wired. |
| 0h. CLI split + macOS bridge cleanup | S | ✅ | Early runtime flags stay outside the schema; `GameBridge.c` config get/set now uses the public registry lookup/string-set path. |

**Gate:** `tools/settings_schema_check.py` verifies `--list-settings`,
`--dump-config`, default values, custom `ge007.ini` loading, and no ROM startup.
`tools/config_roundtrip_check.py` verifies `--config-set`, `--reset-config`,
registered-value reload, transient env/CLI override precedence, self-documenting
save comments, atomic-save temp cleanup, and unknown-key preservation.
`tools/config_schema_types_check.py` verifies enum/string registration, enum
token load/save, string load/save, default reset, schema comments, and unknown
key preservation. Next, extend the gate for BIND rows and a simulated crash
between temp write and rename.

---

## Phase 1 — Display & output (L)

### 1a. Window & fullscreen modes (M)

| Task | Effort | Status | Detail |
|---|---|---|---|
| `Video.WindowMode = windowed\|borderless\|exclusive` | S | ✅ | Replaced the bool `Video.Fullscreen` with a token enum; startup and Alt+Enter use the shared `SDL_SetWindowFullscreen` apply path. Windowed remains default until the first-run display flow exists. |
| Exclusive fullscreen + mode select | M | ✅ | `exclusive` maps to `SDL_WINDOW_FULLSCREEN`; optional `Video.FullscreenWidth/Height/Refresh` selects an SDL display mode, with zero values falling back to SDL/default desktop behavior. |
| Display enumeration + monitor select | S | ✅ | `Video.Display` selects a zero-based SDL display index and clamps missing monitors to display 0; `--list-displays` prints SDL display bounds, usable bounds, current mode, and fullscreen modes without loading a ROM. |
| Remember + sanitize window geometry | S | ✅ | Persist `Video.WindowX/Y/W/H`; positions are relative to `Video.Display`, and startup clamps to the live display bounds. |
| `Video.VSync = off\|on\|adaptive` | S | ✅ | Exposed the existing swap-interval path through the settings schema; focus regain restores the configured mode. |
| `Video.FrameCap = 30\|60\|display` | S | ✅ | Exposes 30/60/software pacing plus display-backed pacing; if VSync is off, `display` falls back to the 60 fps software cap, so there is still no uncapped option. |

### 1b. Render-scale, MSAA, gamma (M) — extends the existing FBO chain (P5)

| Task | Effort | Status | Detail |
|---|---|---|---|
| Scene render target | M | ✅ | Binds an offscreen color+depth FBO at frame start when render scale differs from 1.0, resolves to the window before output post-processing, and invalidates on resize. |
| `Video.RenderScale = 1.0…2.0` | S | ✅ | Schema-backed render scale; Fast3D viewport/scissor math uses the scaled scene dimensions, then blits back to the drawable. |
| `Video.MSAA = 0\|2\|4\|8` | S | ✅ | Schema-backed multisampled scene target. `MSAA=0` keeps the single-sample path; 2/4/8 render to MSAA renderbuffers, resolve to the scene target, then present through the existing output chain. |
| `Video.Gamma` | S | ✅ | Added a schema-backed gamma uniform to the output-filter shader; gamma 1.0 keeps the old no-op path, non-default gamma runs the full-resolution post pass. |
| `Video.RetroFilter` | S | ✅ | Exposes the existing VI filter as `auto|off|on`; `auto` preserves menu softening and opt-in gameplay behavior, `on` enables gameplay softening too. |

**Gate:** `audit_screenshot_health.py` at 3840×2160 and at `RenderScale 2.0`
asserts correct FBO dimensions + non-blank/non-monochrome; `compare_screenshots.py`
confirms MSAA/scale change edges but not geometry vs. a 1.0/no-AA baseline within
tolerance; `mp_smoke.sh` still green (FBO must not break split-screen).

---

## Phase 2 — Aspect ratio, widescreen & FOV (L) — headline

### 2a. First-class aspect modes + split-screen fix (3D) (M)

| Task | Effort | Status | Detail |
|---|---|---|---|
| `Video.Aspect = 4:3\|16:9\|16:10\|21:9\|auto` | S | ⬜ | Replace/promote the active Fast3D screen-space X-scale heuristic (`fast3d/gfx_pc.c:8246-8282`) with an explicit, tested aspect path. `4:3` pillarboxes; `auto` matches window. |
| Split-screen per-player aspect | M | ⬜ | First capture/trace `g_CurrentPlayer->aspect`, viewport, and horizon geometry for 2/3/4-player. If the active path diverges, compute aspect from final `viewx/viewy` at the per-player handoff (`lvl.c:1515-1522`) and keep `bondviewMovePlayerUpdateViewport` (`bondview.c:13982-13990`) and render state consistent. |
| Ultrawide FOV/visibility guard | S | ⬜ | At 21:9+, clamp horizontal FOV so MP visibility/aim parity isn't broken; documented as deliberate. |

### 2b. 2D HUD anchoring + art pillarboxing (M) — was the long pole, now a policy table (P4)

| Task | Effort | Status | Detail |
|---|---|---|---|
| DrawClass→layout policy at the 2D primitives | M | ⬜ | Consume `g_current_draw_class` at `gfx_dp_texture_rectangle`/`gfx_dp_fill_rectangle`: `HUD`→anchor, else→pillarbox. Retire the diag X-hack (`fast3d/gfx_pc.c:8246-8282`). |
| Pillarbox bars for 4:3 art | S | ⬜ | Reuse scissor/fill infra (`fr.c:1764-1799`, `GE007_FORCE_FULLSCREEN_SCISSOR`). |
| HUD corner/center anchoring | M | ⬜ | Map HUD groups (health/armor/ammo/radar/objective/crosshair) to true viewport edges via `viGetViewLeft/Top` + ratio. |
| Crosshair true-center per player | S | ⬜ | Reticle tracks viewport center at every aspect and split. |

### 2c. FOV control (M)

| Task | Effort | Status | Detail |
|---|---|---|---|
| `Video.FovY = 45…90` (default 60) | S | ✅ | Schema-backed normal gameplay FOV. Native builds replace the base `FOV_Y_F` reset inside `bondviewMovePlayerUpdateViewport` and the non-zooming `bondviewTriggerWatchZoom(60)` path; `lvlRender` still applies `g_CurrentPlayer->fovy` per live player, so scoped/watch zoom values remain separate. |
| Viewmodel FOV compensation | M | ⬜ | Gun draws in-scene (`DRAWCLASS_WEAPON`), so high FOV misframes it; apply a separate viewmodel FOV/position fix and test every weapon. |

**Gate:** pixel/screenshot baselines at 4:3/16:9/16:10/21:9 — 3D widens hor+
(identical vertical framing across aspects), 2D art stays 4:3 (no stretch), HUD at
correct corners; a split-screen capture asserts each viewport's horizon line is
geometrically correct (aspect fix); `mp_smoke.sh` dissimilar-halves still passes.

---

## Phase 3 — Input rebinding (L)

### 3a. Action model — behavior-preserving refactor (M)

| Task | Effort | Status | Detail |
|---|---|---|---|
| `InputAction` enum + resolver | S | ⬜ | Full GE verb set: `FIRE, AIM, MOVE_*, LOOK_*, LEAN_*, CROUCH, RELOAD/USE, WEAPON_NEXT/PREV, PAUSE, MENU_*`. |
| Two-output binding tables (P3) | M | ⬜ | `physical → action → (button bit \| analog axis)`. Rewrite keyboard (`stubs.c:5129`) and `pcFillPadFromController` (`:4784`) and wrap the analog getters (`platform_sdl.c:937/1027`) to resolve through the same per-player table. |
| Default profile == today, bit-for-bit | S | ⬜ | Shipped defaults reproduce the current hardcoded map exactly. |
| Freeze/replay safety | S | ⬜ | Resolve live input only; honor `g_freezeInput` and RAMROM/demo playback for both digital and analog look; never touch `CONTDATA_PLAYBACK`. |

### 3b. Configurable bindings + presets + per-device profiles (M)

| Task | Effort | Status | Detail |
|---|---|---|---|
| `BIND`-type serialization | M | ⬜ | `Input.P1.Bind.Fire = MouseLeft,LShift` (multi-bind), built on P1 string/bind config. |
| Per-device profiles keyed by SDL controller GUID | S | ⬜ | A profile follows the physical pad, not the slot — modern best practice. |
| Control presets | S | ⬜ | Classic GE styles **1.1 Honey / 1.2 Solitaire / 2.1 Domino / 2.2 Goodnight** + **Modern Dual-Stick** (pad default) + **KB+Mouse** (P1 default). |
| Per-player profile assignment | S | ⬜ | P1 KB+M, P2–4 pads; each independently rebindable. |

### 3c. Feel & accessibility (M)

| Task | Effort | Status | Detail |
|---|---|---|---|
| Gamepad look speed + response curve | S | ⬜ | Replace the lone `GamepadLookSpeed` scalar with per-axis speed + linear/exponential curve. |
| Configurable deadzone | S | ⬜ | Expose hardcoded `GAMEPAD_DEADZONE 8000` (`stubs.c:631`) per stick. |
| Per-axis invert, ADS multiplier, raw mouse (no smoothing) | S | ⬜ | Extends existing aim-sens model; raw input default-on for KB+M. |
| Hold-vs-toggle for aim/crouch | S | ⬜ | Accessibility; extends existing `g_pcCrouchToggle`. |

**Gate:** `tools/input_map_check.py` feeds synthetic SDL events through the action
layer and asserts resulting `OSContPad.button`/stick bytes; the default profile
matches a captured baseline of today's behavior **byte-for-byte**. **Replay
invariant test:** run a RAMROM replay under a *non-default* binding profile and
with synthetic live mouse/right-stick noise enabled, then assert the state trace
is identical to the default-profile/no-noise run (proves remap never reaches
`CONTDATA_PLAYBACK` and analog look is gated during replay).

---

## Phase 4 — Settings overlay UI (L)

A port-owned, immediate-mode overlay (default hotkey `F10` + a pad shortcut),
drawn by the port — **not** injected into the decompiled frontend (`front.c`).

> **DX decision — no C++ dependency.** The native target is pure C (the `.cpp` in
> the tree are the vendored `build/ares-*` oracle, not compiled into `ge007`).
> Dear ImGui would force `enable_language(CXX)` + a C++ TU, against the project's
> dependency-clean ethos. **Recommendation:** a small port-owned immediate-mode
> overlay with an embedded bitmap font, drawn through its own GL program mirroring
> the output-filter pattern (own VAO/shader, GL state saved/restored exactly like
> `gfx_opengl_apply_output_vi_filter` `:1422`). ImGui stays a documented fallback
> if richer DX is later judged worth the C++ TU.

| Task | Effort | Status | Detail |
|---|---|---|---|
| Overlay shell + embedded font + input capture | M | ⬜ | Hotkey open/close; navigable by KB+M and pad; grabs input while open; dims game behind; pauses sim. |
| Tabs auto-generated from the schema (P1) | M | ⬜ | Display / Graphics / Aspect+FOV / Audio / Controls (P1–4) / About — rows generated from `Setting` metadata, so new options appear with zero UI code. |
| Live preview + restart badges + Apply/Reset/Cancel | S | ⬜ | LIVE settings update behind the overlay; RESTART badged; env-shadowed rows read-only (P1 0f). |
| Rebind capture + conflict detection | M | ⬜ | "Press a key/button…" writes into P3 tables; warns on conflicts. |
| First-run autodetect | S | ⬜ | Detect native resolution + ultrawide; propose borderless@native, matching aspect, sane FOV; player confirms. |
| Save policy | S | ⬜ | Persist on overlay close and on exit (debounced); never mid-frame. |
| macOS Preferences bridge alignment | S | ⬜ | Either make `PreferencesView.swift` consume the same schema through `GameBridge` or clearly demote it to an app-shell wrapper around the in-game overlay; avoid two divergent settings stores. |

**Gate:** scripted overlay screenshot per tab (`audit_screenshot_health.py`); a
change made via the overlay is asserted present in `ge007.ini` after close;
opening/closing mid-mission leaves the deterministic state trace unchanged.

---

## Phase 5 — Presets, platform profiles & docs (S–M)

| Task | Effort | Status | Detail |
|---|---|---|---|
| Quality presets | S | ⬜ | `Authentic` (4:3, retro filter, classic controls) · `Modern` (native widescreen, MSAA, dual-stick) · `Performance` (native scale, no AA). One click. |
| Steam Deck / handheld profile | S | ⬜ | 16:10 1280×800, pad preset, comfortable FOV, GPU-tuned RenderScale. |
| Docs + claim alignment | S | ⬜ | User options guide + contributor note on the schema registry and action layer; update `README`/`PORT.md`/`docs/STATUS.md` to claim only what's wired. |
| CI wiring | S | ⬜ | Keep no-ROM checks in `tools/validate_quick.sh`; keep ROM-backed display/input/playability lanes as explicit scripts documented in `docs/INSTRUMENTATION.md`. |

---

## Robustness & edge cases (the difference between B and S)

| Area | Hazard | Handling |
|---|---|---|
| Config | Crash mid-write / disk full | Atomic temp+rename (0d); failure leaves prior file intact. |
| Config | New build drops old keys | Unknown-key passthrough (0c). |
| Config | Env set + user edits UI | Env-shadow surfaced read-only (0f); no silent no-op. |
| Display | Monitor unplugged while fullscreen / saved geometry off-screen | Clamp restore to a live display; fall back to borderless@primary. |
| Display | GL context loss / DPI change on monitor move | Re-query `SDL_GL_GetDrawableSize` (already per-frame `fast3d/gfx_pc.c:14345-14351`); reallocate scene FBO on size change. |
| Input | Controller hot-plug mid-match | Stable slot model already present (`platform_sdl.c:39-98`); profile re-binds by GUID (3b). |
| Input | Keyboard layout (AZERTY etc.) | Bind on physical `SDL_SCANCODE_*` (already used `stubs.c:5129`), not keycodes. |
| Input | Binding conflicts | Conflict detection in rebind capture (Phase 4). |
| Input | Demo/replay corruption | Live-only remap; `CONTDATA_PLAYBACK` untouched; analog look disabled during playback; proven by 3d replay test. |
| Determinism | Analog look leaks under freeze/replay | Bug fix in 3a (single live-look gate checks `g_freezeInput`, RAMROM/demo playback, and overlay capture). |
| MP | FBO/aspect regress split-screen | `mp_smoke.sh` dissimilar-halves gate on 1b and 2a. |
| Render | RenderScale × 4-way overdraw tanks frame budget | Surface `rooms_drawn`/`tris` telemetry (already emitted) in the gate; document a per-tier cap. |

---

## Execution slices

Land this as small, reviewable behavior slices. Each slice must leave the native
port playable, keep defaults equivalent unless the slice explicitly changes a
default, and keep generated ROM-derived traces/screenshots under `/tmp` or another
ignored path.

| Slice | Scope | Exit criteria |
|---|---|---|
| 0. Baseline inventory | Add trace probes only where needed for aspect/input gates; no behavior change. | `cmake --build build --parallel`; `./tools/validate_quick.sh --no-spawn`; no user-facing claim changes. |
| 1. Settings skeleton | Add `src/platform/settings.c`, schema rows for existing settings, `--list-settings`, `--dump-config`; keep existing config values and CLI behavior. | Dumped defaults match current hardcoded defaults; old `ge007.ini` still loads. |
| 2. Config durability | Enum/string/bind support, unknown-key passthrough, atomic save, `--config-set`, `--reset-config`, env-shadow surfacing. | `tools/config_roundtrip_check.py` covers every setting type plus crash-before-rename. |
| 3. Display modes | `Video.WindowMode`, display index, exclusive mode, window geometry restore, `Video.VSync`. | Manual window/fullscreen smoke on macOS + Linux; monitor-missing restore falls back safely. |
| 4. Render target chain | Scene FBO, resize invalidation, render scale, MSAA, gamma, retro filter. | Screenshot health at native and 4K-size targets; `tools/mp_smoke.sh --no-build` still passes. |
| 5. Aspect/FOV diagnostics | Trace per-player viewport/aspect/FOV and draw class layout decisions before changing output. | Captures prove the active Fast3D path and split-screen math; no visual behavior change yet. |
| 6. Aspect/FOV behavior | First-class aspect modes, 2D policy table, HUD anchors, viewmodel FOV compensation. | Per-aspect screenshots pass geometry/art/HUD assertions; 2P and 4P smoke stay green. |
| 7. Input parity refactor | Action enum and resolver wired to digital + analog paths with defaults matching today. | Synthetic SDL events produce byte-for-byte current `OSContPad` output; analog look honors freeze/replay gates. |
| 8. Rebinding and presets | Bind serialization, per-device profiles, GE classic presets, modern pad, KB+mouse, per-player assignment. | Non-default bindings work in live input; RAMROM/demo replay traces remain identical with live input noise present. |
| 9. Overlay | In-game settings overlay generated from the schema; rebind capture; Apply/Reset/Cancel; save-on-close. | Per-tab screenshot health; config writes only on close/exit; deterministic mid-mission trace unchanged. |
| 10. Platform/docs polish | macOS bridge alignment, quality presets, Steam Deck profile, README/user-options docs, status claims. | `README.md`, `docs/STATUS.md`, `PORT.md`, and macOS docs claim only wired behavior. |

Do not combine slices 6 and 7. Aspect/FOV and input rebinding touch unrelated
failure modes; keeping them separate makes any regression bisectable.

## Regression Matrix

Use a tiered matrix instead of one giant "run everything" command. The quick tier
is for every commit. ROM-backed and oracle lanes are required before merging the
slices that touch their surface.

| Tier | When | Commands / gates | Notes |
|---|---|---|---|
| Static release safety | Every public-facing commit | `./scripts/ci/check_no_rom_data.sh`; `python3 tools/check_markdown_links.py --repo-root .`; `git diff --check` | No ROM required. Keeps docs/assets legally clean. |
| Quick native smoke | Every behavior commit | `cmake --build build --parallel`; `./tools/validate_quick.sh --rom /path/to/baserom.u.z64` | If no ROM is available, run `./tools/validate_quick.sh --no-spawn` and record the skip. |
| Config | Slices 1-2 and every new setting | `tools/config_roundtrip_check.py` once added; `--dump-config`; `--list-settings`; `--config-set k=v`; `--reset-config` | Include unknown-key preservation and atomic-save failure injection. |
| Display/FBO | Slices 3-4 | Deterministic screenshots at 640x480, 1920x1080, 3840x2160, and `RenderScale=2.0`; `python3 tools/audit_screenshot_health.py ...`; edge diff with `tools/compare_screenshots.py` | Generated screenshots stay local. Verify FBO dimensions in trace/log output. |
| Aspect/FOV | Slices 5-6 | 4:3, 16:9, 16:10, 21:9 captures on Dam/Facility/Frigate; split-screen horizon/viewport trace assertions; `tools/mp_smoke.sh --players 2 --no-build`; `tools/mp_smoke.sh --players 4 --no-build` | Baselines should assert vertical framing stays stable while horizontal view expands. |
| Input | Slices 7-8 | `tools/input_map_check.py` once added; default-profile byte golden; scripted live input with KB+mouse and pad; replay invariant under non-default bindings + synthetic live mouse/right-stick noise | The key invariant is that remapping affects live input only. |
| Overlay | Slice 9 | Per-tab screenshot health; config-write-on-close check; deterministic trace before/after opening overlay mid-mission | Overlay input capture must suppress game input and analog look. |
| Broad regression | Before merging slice 4, 6, 8, 9, or 10 | `./tools/regression_test.sh --allow-missing-baselines --rom /path/to/baserom.u.z64`; `./tools/playability_smoke.sh --no-build --rom /path/to/baserom.u.z64`; `./tools/soak_stability.sh --no-build --rom /path/to/baserom.u.z64` when touching renderer/input timing | `regression_test.sh` gives screenshot/state/audio coverage where local baselines exist and trace/render-health coverage where they do not. |
| Release guard | Before tagging or announcing | `./scripts/ci/check_release_ready.sh`; `./scripts/release_preflight.sh --require-rom-smoke --rom /path/to/baserom.u.z64` | Keep this heavier than per-commit validation; it audits public docs, provenance, ignored artifacts, and source-archive hygiene. Use `--deep-runtime` for final maintainer sweeps. |

Minimum per-slice rule: a slice cannot land with only manual playtesting unless
it also adds the missing automated gate it depends on. The gate can be narrow,
but it must fail on the bug the slice is designed to prevent.

---

## Sequencing & dependencies

```
Phase 0 (schema)  ──┬─> 1 (display)  ──┐
                    ├─> 2 (aspect/FOV) ─┼─> 4 (overlay, schema-driven) ─> 5
                    └─> 3 (input)  ─────┘
```

- **Phase 0 gates everything** (schema, enum/string/bind config, passthrough,
  atomic save). Small; do first.
- **1, 2, 3 are independent** and parallelizable; each is usable via `ge007.ini`/
  env the moment it lands (no "nothing works until the UI" cliff).
- **Phase 4 is mostly free** once 0–3 exist because tabs auto-generate from the
  schema — its real cost is the embedded-font overlay shell and rebind capture.
- **Highest satisfaction-per-hour:** 1a (fullscreen/gamma) + 2c (FOV) — small,
  instantly felt. **Most care:** 2c viewmodel compensation and 3a's byte-for-byte
  default-profile parity.

## Risk register

| Risk | Phase | Mitigation |
|---|---|---|
| Frame-coupled sim → any cap change alters game speed | 1a | Verify tick/render coupling first; ship only 30/60/display; defer high-refresh to the sim-decouple track. |
| High FOV misframes the in-scene weapon | 2c | Separate viewmodel FOV; test every weapon before default-on. |
| FOV setting gets overwritten by gameplay viewport/zoom update | 2c | Apply desired FOV at the same point that `bondviewMovePlayerUpdateViewport` updates player FOV/aspect and at the normal gameplay watch-zoom target, not only through a one-shot `viSetFovY`. |
| Active renderer evidence confused with excluded legacy renderer | 2a/2b | The legacy `src/platform/gfx_pc.c` has been removed from the tree (see `CHANGELOG.md`); the only renderer is `src/platform/fast3d/gfx_pc.c`. |
| DrawClass misses an art draw that doesn't set HUD | 2b | Per-aspect pixel baselines; `LEVELID_TITLE`/`current_menu` as secondary signal; default-to-pillarbox is the safe failure. |
| Action-layer drifts from current feel | 3a | Byte-for-byte default-map golden + replay-invariant test before any rebinding lands. |
| Analog live-look perturbs RAMROM replay | 3a | Gate analog look on replay/freeze state and include live-noise replay invariant test. |
| New surface trips provenance/SDK guards | all | Everything in `src/platform/` under `NATIVE_PORT`; run `check_native_sdk_surface.py` + release preflight. |
| Overlay pulls in C++/ImGui against ethos | 4 | Default to port-owned embedded-font overlay; ImGui only as an explicit, documented opt-in. |
| macOS preferences diverge from SDL overlay/config | 0/4 | Wire `GameBridge` to the schema or make the native Preferences UI a thin launcher/app-shell surface, not a second source of truth. |
