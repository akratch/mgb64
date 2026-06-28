# Split-Screen Multiplayer — Execution Plan

GoldenEye 007's defining feature is local split-screen multiplayer. The native
port already plays the solo campaign start-to-finish, so wiring up 2–4 player
split-screen is the highest-leverage next step toward making the port "the
GoldenEye people remember."

## Why this is wiring, not decompilation

The hard parts are already decompiled and running:

- **MP game logic** — `src/game/mp_watch.c` (~8.7k lines: scoreboard, results,
  `mpFindMaxInt/Float`), `mp_weapon.c`, `mp_music.c`.
- **Per-player data model** — `src/game/player_2.c:68`
  (`init_player_data_ptrs_construct_viewports`) allocates `PLAYER_1..PLAYER_4`;
  `getPlayerCount()` counts live slots; hundreds of `get_cur_playernum()`
  callsites already branch SP vs MP.
- **Split-screen renderer** — `src/game/lvl.c:1515` loops per player setting
  viewport/FOV/aspect; `src/fr.c:1718` emits a real per-player `gSPViewport`;
  `src/fr.c:1764` (`viSetupScreensForNumPlayers`) draws the 2/3/4-way dividers;
  the native renderer honors per-player viewport (`gfx_pc.c:10916`) and scissor
  (`gfx_pc.c:11091`).
- **Frontend** — MP menu state machine in `menu_init` (`src/game/front.c:14273`;
  dispatch switches ~`14351/14390/14419`) plus the full classic MP stage table
  (`front.c:1123+`).

The entire feature *was* gated behind one deficiency, **now resolved by Phase 1
below**: originally the port presented exactly one controller
(`platform_sdl.c:1003`) and `osContGetReadData` filled only `data[0]`, so
`joyGetControllerCount()` returned 1 and the MP menus bounced out. **Current state
(✅ Phase 1):** `osContGetReadData` (func **`stubs.c:5000`**) does a local `data[0]`
merge (`:5426`) then a real multi-pad fill loop `for k=1..MAXCONTROLLERS-1
pcFillPadFromController(&data[k],k)` (**`:5436-5439`**); the menu gate is at
`front.c:3888` (`>=2` enables Secret/Multi) and the `<2` bounce is `front.c:5567`.

> **⚠ Anchor note (2026-06-25):** earlier revisions of this paragraph cited
> `stubs.c:4738/:4750` with "`data[1..3]` zeroed" — **both stale and superseded**
> (the fill loop landed in Phase 1; see the ✅ row in Phase 1 §1b). Canonical
> stubs anchors: func `5000`, local merge `5426`, multi-pad fill `5436`. Full
> corrected-anchor table: [NETPLAY_PLAN_REVIEW.md](NETPLAY_PLAN_REVIEW.md) §5.

## Scope guard

- GoldenEye MP is **human-only** — there are **no simulants/bots** (that is
  Perfect Dark). Scope is strictly 2–4 humans.
- MP character/control/handicap/unlock persistence **already rides the existing
  `file.c`/`file2.c` save system** — no separate MP save format.
- MP stages route through `multi_stage_setups[].stage_id` into the normal level
  loader — no separate MP ROM-data extraction.

## Phased roadmap

Effort: S/M/L/XL. Every milestone has an evidence-backed acceptance gate that
extends the existing validation lanes.

Status legend: ✅ landed (evidence-backed), 🟡 in progress / wired but not fully
validated, ⬜ not started.

### Phase 0 — Stability & viewport floor (prerequisite for 4-way)

| Task | Effort | Status | Detail |
|---|---|---|---|
| 0a. Soak/stability lane | M | ✅ | `tools/soak_stability.sh`: long headless deterministic runs per stage piped to `audit_render_trace.py --max-crashes 0`. |
| 0b. ASan/UBSan lane | M (lane) + L (triage) | ✅ | `tools/asan_smoke.sh` over `build-asan` (`CMakeLists.txt:364`); **report-only first, off the MP critical path.** |
| 0c. Render-cost telemetry | S (hard blocker for M2/M5 gates) | ✅ | Emits `g_BgNumberOfRoomsDrawn` as `rooms_drawn` alongside `tris` into `port_trace` JSON so overdraw is measurable before MP loads it. |
| 0d. Validate single-viewport scissor | S | ⬜ | Confirm `bgScissorCurrentPlayerViewDefault` (`front.c:5483`) composes with `gfx_ratio_x/y` at high-DPI. |

### Phase 1 — Multi-controller input (the linchpin)

| Task | Effort | Status | Detail |
|---|---|---|---|
| 1a. Opened-pad table | M | ✅ | Replaced single `g_gameController` with `PlatformPad g_pads[PLATFORM_MAX_PADS]` `{handle, instance_id, slot}`; opens all pads at init + `CONTROLLERDEVICEADDED`; handles `CONTROLLERDEVICEREMOVED`. Added per-pad `platformGetPadCount/Buttons/LeftStick/RightStick/Triggers`. |
| 1b. Fill `data[1..3]` | M | ✅ | `osContGetReadData` loops `k=1..MAXCONTROLLERS-1` filling each pad via `pcFillPadFromController`. Keyboard+mouse stay on `data[0]`. |
| 1c. Controller-count shim | S | ✅ | `osContInit`/`osContGetQuery` set `g_ContStatus` from the opened-pad set so `joyGetControllerCount() >= 2` unblocks `front.c:3888` (`>=2` gate) / `:5567` (`<2` bounce); hot-plug re-derived per frame. |

### Phase 2 — Deterministic MP launch + first 2-player match

| Task | Effort | Status | Detail |
|---|---|---|---|
| 2a. CLI + boot path | M | ✅ | `--multiplayer`/`--players N`/`--mp-stage`/`--scenario` in `main_pc.c` + `pc_apply_mp_selection()` in `initmenus.c` (mirror of `pc_apply_level_selection()`). |
| 2b. Menu-chain verification | S | 🟡 | Direct-boot path verified; menu-driven two-viewport render path not separately validated. |

### Phase 3 — Per-player look/aim routing

| Task | Effort | Status | Detail |
|---|---|---|---|
| 3a. Route aim by player | S/M (pad table from 1a) | ✅ | Mouse-look gated to P1 (`local_player_number==0`); right-stick aim switched to `platformGetPadRightStick(local_player_number, ...)` in `lvl.c` so pad `k` aims player `k`. |
| 3b. Frontend nav for mixed input | S | ⬜ | Per-player cursor routing in char/team/control-style menus. |

### Phase 4 — MP validation lane + end-of-round proof

| Task | Effort | Status | Detail |
|---|---|---|---|
| 4a. `tools/mp_smoke.sh` | M | ✅ | Green for 2-player: boots a Temple deathmatch via CLI, drives a scripted P1 window, `--max-crashes 0`, crops the framebuffer, and asserts the two halves are dissimilar (~97% delta — a duplicated-camera bug fails). 4-player also boots + renders distinct viewports in the smoke window; sustained-load/3-player-asymmetric rigor is Phase 5. |
| 4b. End-of-round assertion | S | 🟡 | `--mp-timelimit SECS` forces a deterministic match length; `tools/mp_smoke.sh --timelimit` asserts the match timer reaches the configured limit crash-free. **Open:** headless direct-boot still needs a scoreboard/results-screen transition assertion. |

### Phase 5 — Split-screen performance + 3/4-player hardening (real hidden L)

| Task | Effort | Detail |
|---|---|---|
| 5a-i. 2-player validate | M | `dyn.c:62` `g_GfxSizesByPlayerCount` + frame budget at 2-way. |
| 5a-ii. 3/4-player viewport math | L+ | **Highest-risk geometry: the 3-player asymmetric split** (`viSetupScreensForNumPlayers` `==3`). |
| 5b. Split-screen audio | S/M | Single-listener SFX/music with N players. |

## Parallel polish track (default-off, config-gated)

The display/input portion of the old polish list now has its own execution plan:
[DISPLAY_INPUT_PLAN.md](DISPLAY_INPUT_PLAN.md). That plan covers true hor+
widescreen, FOV control, fullscreen modes, render scale/MSAA/gamma, settings UI,
and input rebinding, with concrete regression lanes for split-screen safety.
The native feature-extension pattern is summarized in
[PORTING_AND_EXPANSION.md](PORTING_AND_EXPANSION.md).

Drop-shadows remain renderer-parity work, separate from the display/input track.

Hard constraint: every render-math change is default-off, and the ares 4:3
movement/intro oracle must stay **bit-stable** as a regression gate.

## Acceptance gates (key ones)

- **M1:** scripted pad-1 input moves `g_playerPointers[PLAYER_2]` independently
  of P1 (`port_trace` deltas diverge); `joyGetControllerCount() >= 2`; menus stop
  bouncing. Hot-plug: simulated `CONTROLLERDEVICEREMOVED` mid-match → no crash.
- **M2 (two cameras, not two halves):** spawn P1/P2 apart facing different
  geometry; assert the two framebuffer halves are **measurably dissimilar** via
  `compare_screenshots.py` (a duplicated-camera bug must fail).
- **M5:** 4-player boot sustains a logged frame budget with `--max-crashes 0`;
  `room_render_fallback_records==0` under 4-way on Streets/Train/Control.

## Definition of done

`tools/mp_smoke.sh` boots a 4-player split-screen deathmatch via
`--multiplayer --players 4`, runs `--max-crashes 0` through a complete round into
the `mp_watch.c` scoreboard, with all four viewports rendering distinct
(dissimilarity-verified) 3D and four independently-controlled players — while
every existing solo lane plus the new soak + ASan lanes stay green.
