<!-- Authored 2026-07-08, refined same day to junior-executable granularity. The F5
project (BACKLOG_v0.4.0.md:138-140 "deferred past 0.4.0") — full engineering plan to
uncap the render rate while the sim stays a bit-exact 60 Hz integer-tick model.
Supersedes the sketch in FRAME_TIMING_ARCHITECTURE.md §5 Path A. Every file:line and
code excerpt below was verified against the working tree on 2026-07-08; if a line
number has drifted, search for the quoted anchor text instead. Companion docs:
FRAME_TIMING_ARCHITECTURE.md (the two-gate map), design/PERFORMANCE_PLAN.md (budgets),
CINEMATICS.md (camera modes). -->

# F5 — Uncapped FPS Implementation Plan (sim-60 / render-uncapped)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking. Execute tasks strictly in order;
> each task's exit criteria must be green before starting the next.

**Goal:** Present frames at an uncapped (or display-rate) cadence while the simulation
stays a bit-exact, deterministic 60 Hz integer-tick model — render-rate mouse look,
interpolated camera and world motion, competitive-shooter feel.

**Architecture:** Make the 60 Hz gate non-blocking: a drift-free wall-clock accumulator
fires whole sim ticks; every other loop iteration is a **render-only frame**
(`g_ClockTimer == 0` — the pause-proven held-frame path) that rebuilds the display list
from unchanged sim state and presents. Smoothness comes from three independent,
individually-A/B-able mechanisms: (1) live-look injection already runs per loop
iteration, so mouse look becomes render-rate automatically; (2) the camera eye position
is lerped between the last two sim ticks on the game's render side; (3) world entity
matrices (DRAWCLASS_CHRPROP modelview loads) are lerped in fast3d at the single
`gfx_sp_matrix` chokepoint, keyed by the prop-identity context the renderer already
receives. Everything is render-side of the R1 wall; sim bytes, RNG draw counts, input
cadence, and audio cadence are unchanged — enforced by a new **0-tick purity fuzz gate**.

**Tech stack:** C (N64 decomp port), SDL2, fast3d (GL/Metal), CMake + ctest, the
GE007_* env/config system, sim_state_hash + RAMROM determinism harnesses.

---

## §0. Orientation for the implementing engineer

Read this section fully before touching code. It defines the vocabulary, the build/test
loop, and the repo conventions every task assumes.

### 0.1 Build, run, test

```bash
# Configure + build (build/ already exists in this repo)
cmake -B build && cmake --build build -j

# Run a level directly (needs the ROM, baserom.u.z64, in the repo root).
# --level takes a stage NAME (dam, facility, runway, surface1, bunker1, silo,
# frigate, surface2, bunker2, statue, archives, streets, depot, train, jungle,
# control, caverns, cradle, aztec, egypt) or a raw LEVELID int — always use
# names in scripts (raw ints are NOT mission numbers; see main_pc.c:738-760).
./build/ge007 --level dam

# Deterministic headless-ish run (synthetic clock, frozen input, auto-exit):
./build/ge007 --deterministic --level dam \
    --screenshot-frame 600 --screenshot-exit \
    --sim-state-hash-out /tmp/hash.json

# Unit tests (ROM-free)
ctest --test-dir build -R sim_state_hash -V

# Local CI (the rails)
scripts/ci/ci_local.sh
```

Notes:
- `--deterministic` **implies frozen input** (`main_pc.c:721-724` sets `g_freezeInput = 1`).
- `--screenshot-frame N` keys on `g_frameSyncCallCount` (a **render-frame** counter);
  `--screenshot-game-timer N` keys on `g_GlobalTimer` (**sim time**). This distinction is
  load-bearing in Task 4 — sim-aligned exits must use `--screenshot-game-timer`.
- macOS still needs a GUI session even for "headless" runs (GL needs a window).
- Commit messages in this repo follow `type(scope): summary` — look at
  `git log --oneline -20` and match. Every commit in this plan compiles and passes
  `ctest --test-dir build` on its own.

### 0.2 Glossary (repo-specific timing vocabulary)

| Term | Meaning |
|---|---|
| **field** | One 1/60 s unit of wall time (NTSC VI field). In `osGetCount()` units: 775875 counts @46.875 MHz (PAL: 931050). |
| **sim tick** | One integer advance of the game simulation. `g_GlobalTimer += 1`. Physics/AI/timers are tuned to this step. |
| **`speedgraphframes`** | `src/game/unk_0C0A70.c:16`. Raw fields elapsed since the last frame (1..4 on PC, clamped). Set by `updateFrameCounters()`. Keeps advancing during pause (HUD/watch timers read it directly). |
| **`g_ClockTimer`** | `src/game/lvl.c:364`. Sim ticks to run *this frame*: `= speedgraphframes`, but forced 0 when paused/controls-locked (`lvl.c:2105-2112`). ~200 consumers integrate the sim by it. |
| **`g_GlobalTimer`** | `src/game/lvl.c:372`. Monotonic sum of `g_ClockTimer` — the master 60 Hz timestamp. |
| **`currentFrameCounter`** | `unk_0C0A70.c:11`. Monotonic sum of fields (advances during pause too, unlike `g_GlobalTimer`). |
| **render-only frame** | An F5 loop iteration where zero fields elapsed: `speedgraphframes = 0` → `g_ClockTimer = 0`. The frame body still runs (DL rebuild + present) — identical to how a paused frame behaves today. |
| **alpha** | `[0,1]` fraction of the current field elapsed at frame start. Interpolation parameter. |
| **tick frame** | A loop iteration where ≥1 field elapsed (sim advanced). |
| **dyn buffers** | Double-buffered per-frame DL/matrix/vertex scratch (`src/game/dyn.c`). Flipped once per frame by `dynSwapBuffers()` (`boss.c:684`). |
| **draw class** | `enum DrawClass` (`src/platform/gfx_pc.h:68-77`): ROOM / WEAPON / CHRPROP / EFFECT / HUD / FRONTEND. The game tags DL ranges per subsystem; fast3d tracks the current class in `g_current_draw_class` (`gfx_pc.c:454`). |
| **prop context** | `gfx_set_prop_context(...)` (`gfx_pc.c:565`): the game hands fast3d the prop/model pointers around each object's DL. Our stable cross-frame identity key. |
| **RAMROM** | Recorded input replay system (N64 demo format). Replays consume exactly one input record per sim tick — sacred. |
| **R1 rail** | `scripts/ci/check_sim_render_separation.sh`: `nm -u` check — no `src/game/*.o` may reference GL-backend symbols (denylist `_gfx_opengl_|_texture_pack_|_g_pcTexturePack|_gl[A-Z]|_glad_gl`). The `gfx_*` submission API is explicitly allowed. |
| **R2 rail** | `scripts/ci/check_timing_lock.sh`: only `lvl.c`/`front.c` may write `g_ClockTimer`; the 1–4 clamp + RAMROM bypass must survive. |

### 0.3 The determinism harnesses (you will run these constantly)

1. **`ctest -R sim_state_hash`** — ROM-free unit test of the hash primitive.
2. **`tools/sim_invariance_gate.sh`** — replays a RAMROM twice (post-FX off/on), asserts
   identical hashes. Proves render features touch no sim byte.
3. **`tools/uncap_purity_gate.sh`** — created in Task 4. THE F5 oracle: deterministic run
   with render-only frames injected must hash identically to the vanilla run.
4. **`scripts/ci/ci_local.sh`** — R1 + R2 + everything else.

### 0.4 Code conventions this plan follows (match them exactly)

- All PC-only code sits under `#ifdef NATIVE_PORT`.
- Env flags are read once and cached in a `static int` initialized to `-1`
  (see `pcGetDeterministicSpeedframesOverride`, `unk_0C0A70.c:39-67`, for the idiom).
- Game TUs call platform functions via **inline `extern` declarations at the use site**
  (see `lvl.c:5835-5839`), not by including platform headers.
- New game-side globals that the platform layer reads are declared `extern` at the
  platform use site (see `platform_sdl.c:2996` reading `g_GlobalTimer`).
- `src/game/*.c` is globbed into the build (`CMakeLists.txt:468`) — new game TUs need
  **no** CMake edit. `FAST3D_SOURCES` is an **explicit list** (`CMakeLists.txt:476-482`)
  — new fast3d TUs must be appended there.

---

## §1. Decision record (why this shape — read once, then trust it)

Full background: `docs/FRAME_TIMING_ARCHITECTURE.md`. The load-bearing facts:

1. **Path B (fractional-dt sim) is rejected, permanently.** ~200 `g_ClockTimer`
   consumers integrate the sim in integer 60 Hz substeps (`for (i2=0; i2<g_ClockTimer; i2++)`
   loops in bondview/gun/chr; cooldown subtraction; `g_GlobalTimer` timestamp scheduling
   in chrlv/chrobjhandler). Converting to float dt breaks determinism, RAMROM, netplay,
   and N64-tuned physics. CS:GO itself runs a fixed authoritative tick with render-side
   entity interpolation — we build the same split at 60 Hz.
2. **`g_ClockTimer == 0` is already a shipped frame.** Pause and controls-lock run the
   entire frame body — `lvlManageMpGame`, `lvlRender`, present — with a zero tick
   (`lvl.c:2105-2112`). Render-only frames reuse this exact, tested state.
3. **The DL is rebuilt from live state every frame** (`dynSwapBuffers`, `dyn.c:285`).
   On a render-only frame the state is unchanged, so the rebuilt DL carries the *same*
   matrices as the tick frame — we substitute lerped ones at the single point where
   matrices enter fast3d (`gfx_sp_matrix`, `gfx_pc.c:15914`). No display-list
   retention/replay machinery needed; every render frame is a normal full frame, so the
   non-idempotent per-frame systems (sun-shadow capture ring, native sky queue, XLU
   deferred batches) keep working untouched.
4. **Live-look already runs per loop iteration** (`boss.c:626` → `lvl.c:5833-6006`
   drains mouse deltas into `vv_theta`/`vv_verta` and refreshes the view basis). Once
   frames decouple, mouse look is render-rate *for free*, and it is semantically
   identical: the next sim tick consumes the same accumulated angle it would have —
   applied incrementally instead of all at once. Deterministic/RAMROM paths already
   exclude live-look (`pcNativeLiveLookAllowed`, zeroed at `lvl.c:5856-5860`).
5. **`waitForNextFrame()`'s math must NOT be reused for the accumulator.** Its
   round-to-nearest bias (`+387937`, `unk_0C0A70.c:224`) credits a tick at *half* a
   field — the documented >60 Hz-panel 2×-speed bug (`platform_sdl.c:1548-1558`) — and
   `updateFrameCounters` resets the baseline to `now`, discarding the fractional
   remainder. The remainder IS the interpolation alpha. The F5 accumulator truncates
   whole fields and advances its baseline by exactly `n * field`.
6. **Interpolation displays the world ≤1 tick behind** (lerp between tick N−1 and N,
   alpha from wall clock). Standard trade — CS:GO interpolates entities ~2 ticks behind.
   The camera *rotation* is exempt (rebuilt fresh per render frame from live angles), so
   perceived look latency drops to render-frame latency. Hit registration stays 60 Hz
   quantized, like every tick-based shooter.
7. **What interpolates and what doesn't:**

| State | Mechanism | Task |
|---|---|---|
| Look rotation (mouse/stick) | fresh per render frame (live-look) | T5 |
| Camera eye position (incl. bob) | game-render-side lerp of `collision_position` | T6 |
| Chr/prop/door/lift matrices (placement + bones) | fast3d matrix lerp, prop-keyed | T7/T8 |
| Rooms | static matrices × smooth camera — nothing to do | — |
| Viewmodel | rebuilt per frame from the fresh camera — pass through | — |
| Projection / FOV zoom | pass through (60 Hz stepping invisible on slow zooms) | — |
| HUD / frontend / menus | screen-space — pass through | — |
| Particles/effects (raw verts), texture scrolls, muzzle flashes | step at 60 Hz (v1) | §F5.x |

8. **Expected performance:** per `design/PERFORMANCE_PLAN.md` all 20 levels already do
   100–189 fps of *work* on the M3 Max reference box. Day-one uncapped delivers that
   range (render-only frames skip the sim, so they're slightly cheaper than today's
   frames). 300+ fps requires DL-rebuild elision — deferred (§F5.x.4), profiling first.

---

## §2. Global constraints (every task implicitly includes these)

1. **Sim cadence:** render rate must never change how many sim ticks advance.
   `g_ClockTimer` stays integer 0–4/frame; RAMROM bypass + 1–4 clamp intact
   (`lvl.c:2123-2130`; rail: `check_timing_lock.sh`).
2. **Writer confinement (R2):** only `lvl.c`/`front.c` write `g_ClockTimer`. F5 never
   writes it — it drives it to 0 via `speedgraphframes = 0` in `unk_0C0A70.c` (which
   already owns that variable).
3. **Separation (R1):** interpolation code references no GL-backend symbols from game
   objects. The allowed game→fast3d surface is the `gfx_*` submission API.
4. **Determinism:** all interpolation state lives in TU statics — never in `s_pcPool`,
   `pos_data_entry`, `g_ClockTimer`, `g_GlobalTimer` (the four hashed regions,
   `sim_state_hash_registry.c:25-60`).
5. **Harness keying:** GE007_AUTO_* schedules and sim-aligned exits key on **sim
   time** (Task 2 / `--screenshot-game-timer`). Uncap is force-disabled under
   `--deterministic` and RAMROM (the fuzz harness is the sole, seeded exception).
6. **Audio:** `portAudioFrame()` pumps once per **field** (i.e., when
   `currentFrameCounter` advances — includes pause), never per render frame.
7. **Input:** one `joyPoll()`/`joyConsumeSamplesWrapper()` pair per tick frame. SDL
   event polling (mouse-delta accumulation) stays per render frame.
8. **Freeze handling:** paused/frozen sim ⇒ both interpolation generations are equal ⇒
   lerp is a no-op. Never extrapolate in v1.
9. **Camera cuts:** interpolation snaps on `g_CameraMode` transitions
   (`src/bondconstants.h` `CAMERAMODE` enum; global at `bondview.c:1378`), stage load,
   and any per-key translation jump > `GE007_INTERP_SNAP_DIST` (default 120 units).
10. **Split-screen:** per-player state is per-player (max 4). Matrix-map keys are
    viewport-invariant (world transforms are identical across viewports).
11. **Identity-off:** with `Video.FrameCap` at `30`/`60` (default) the legacy blocking
    path runs byte-identically, zero new overhead. All F5 behavior is opt-in.

---

## §3. Milestone map

Execute in order. Do not start a milestone until the previous one's exit criteria are
all green.

### M1 — Foundations (Tasks 1–2)
*Goal:* config surface + harness re-keying, both provably identity at capped 60.
*Exit criteria:*
- [ ] `GE007_FRAME_CAP=uncapped` accepted; game still runs at 60 (gate #1 still blocks).
- [ ] Deterministic hash before/after Task 2 is byte-identical.
- [ ] `ctest --test-dir build` and `scripts/ci/ci_local.sh` green.
*Rollback:* each task is a single revertable commit with no behavioral coupling.

### M2 — Decoupled loop (Tasks 3–4)
*Goal:* uncapped duplicate-frame mode — sim bit-exact at 60, presents at render rate.
*Exit criteria:*
- [ ] `GE007_FRAME_CAP=uncapped GE007_VSYNC=off` shows >60 fps on the overlay; game
      speed, audio, pause, menus all normal (manual checklist in Task 3 Step 6).
- [ ] `tools/uncap_purity_gate.sh` green on 3 levels × 2 seeds (dam, bunker1, archives).
- [ ] Capped-60 deterministic hash still identical to M1.
*Rollback:* `Video.FrameCap=60` (default) bypasses everything at runtime; revert = 2 commits.

### M3 — Responsive feel (Tasks 5–6)
*Goal:* render-rate look + smooth camera translation. After M3 the game *feels* uncapped.
*Exit criteria:*
- [ ] Mouse look smooth at render rate; gamepad 360° turn time identical to capped run.
- [ ] Strafing shows smooth room geometry; A/B env flags flip behavior live.
- [ ] Purity gate + all M2 criteria still green.
*Rollback:* `GE007_UNCAP_NO_LATE_LOOK=1`, `GE007_UNCAP_NO_EYE_LERP=1` at runtime.

### M4 — World interpolation (Tasks 7–8)
*Goal:* guards/doors/props move smoothly. Full visual uncap.
*Exit criteria:*
- [ ] `ctest -R gfx_interp` green (ROM-free unit test).
- [ ] Manual matrix checklist (Task 8 Step 5) passes; ASan lane clean.
- [ ] Capped-60 screenshot byte-identical to a pre-F5 `main` build.
*Rollback:* `GE007_UNCAP_NO_MTX_LERP=1` at runtime.

### M5 — Hardening & ship (Tasks 9–11)
*Goal:* observability, CI rails, validation matrix, docs, release decision.
*Exit criteria:*
- [ ] All rails (incl. new F5 checks) in `ci_local.sh` green.
- [ ] 20-level uncapped soak recorded; docs updated; default stays 60.

---

## §4. File map

**New files:**

| Path | Purpose | Build wiring |
|---|---|---|
| `src/game/pc_render_interp.c` / `.h` | per-player camera-eye lerp + camera-mode gate | auto (GLOB `src/game/*.c`, `CMakeLists.txt:468`) |
| `src/platform/fast3d/gfx_interp.c` / `.h` | matrix identity map + lerp (dependency-free) | append to `FAST3D_SOURCES` (`CMakeLists.txt:476`) |
| `tests/test_gfx_interp.c` | ROM-free unit test for the map | new `add_executable` + `add_test` |
| `tools/uncap_purity_gate.sh` | THE F5 oracle (0-tick purity fuzz) | none (shell) |
| `tools/uncap_fps_census.sh` | presented-fps census, uncapped lane | none (shell) |

**Modified files:** `src/game/unk_0C0A70.c/.h`, `src/boss.c`,
`src/platform/platform_sdl.c`, `src/platform/stubs.c`, `src/game/lvl.c`,
`src/game/bondview.c`, `src/platform/fast3d/gfx_pc.c`, `src/game/rsp.c`,
`src/game/pc_fps_overlay.c`, `scripts/ci/check_timing_lock.sh`, `CMakeLists.txt`,
docs (`FRAME_TIMING_ARCHITECTURE.md`, `ENV_FLAGS.md`, `BACKLOG_v0.4.0.md`).

**Config / env surface (all default-off):**

| Knob | Values / default | Meaning |
|---|---|---|
| `Video.FrameCap` (`GE007_FRAME_CAP`) | `30\|60\|120\|display\|uncapped`, default `60` | `120`/`display`/`uncapped` engage the uncap loop; `display` paces to the real panel rate |
| `GE007_UNCAP_FUZZ=<seed>` | unset | deterministic render-only-frame injection (test harness only) |
| `GE007_UNCAP_NO_LATE_LOOK=1` | unset | apply look only on tick frames (A/B) |
| `GE007_UNCAP_NO_EYE_LERP=1` | unset | disable camera-eye lerp (A/B) |
| `GE007_UNCAP_NO_MTX_LERP=1` | unset | disable fast3d matrix lerp (A/B) |
| `GE007_INTERP_SNAP_DIST=<units>` | 120 | per-key teleport snap threshold |
| `GE007_TRACE_INTERP=1` | unset | per-frame interp diagnostics |

---

# §5. Tasks

---

## Task 1 (M1): FrameCap plumbing — enum values, pacer periods, `platformFrameCapWantsUncap()`

Safe to land alone: until Task 3 wires the loop, `waitForNextFrame()` still blocks the
sim at 60 no matter what the pacer does. The pacer already skips itself when the period
is `0.0` (`platform_sdl.c:2923` `if (period_ms > 0.0)`), so **no `platformFrameSync`
edit is needed** — only the period function and the enum.

**Files:**
- Modify: `src/platform/platform_sdl.c` — three places, all anchored below.

**Interfaces:**
- Produces: `int platformFrameCapWantsUncap(void)` — nonzero iff the cap mode is
  120/display/uncapped. Consumed by Task 3's `pcUncapActive()` (via inline extern).
- Produces: `platformFrameCapPeriodMs()` → `1000/120`, real panel period, or `0.0`.

- [ ] **Step 1.1 — Extend the enum + options table.**

Anchor: `platform_sdl.c:280-291` currently reads exactly:

```c
typedef enum PlatformFrameCapMode {
    PLATFORM_FRAME_CAP_DISPLAY = 0,
    PLATFORM_FRAME_CAP_30 = 30,
    PLATFORM_FRAME_CAP_60 = 60
} PlatformFrameCapMode;

static s32 g_frameCapMode = PLATFORM_FRAME_CAP_60;
static const ConfigEnumOption k_frameCapOptions[] = {
    { "30", PLATFORM_FRAME_CAP_30 },
    { "60", PLATFORM_FRAME_CAP_60 },
    { "display", PLATFORM_FRAME_CAP_DISPLAY },
};
```

Replace with (config files persist the *name* strings, so numeric values are free —
verified: `ConfigEnumOption` maps name→value and `settingsRegisterEnum` takes the
options table, `settings.h:62`):

```c
typedef enum PlatformFrameCapMode {
    PLATFORM_FRAME_CAP_DISPLAY = 0,
    PLATFORM_FRAME_CAP_30 = 30,
    PLATFORM_FRAME_CAP_60 = 60,
    PLATFORM_FRAME_CAP_120 = 120,     /* F5: uncap loop, paced to 120 */
    PLATFORM_FRAME_CAP_UNCAPPED = 999 /* F5: uncap loop, pacer off (vsync may still bind) */
} PlatformFrameCapMode;

static s32 g_frameCapMode = PLATFORM_FRAME_CAP_60;
static const ConfigEnumOption k_frameCapOptions[] = {
    { "30", PLATFORM_FRAME_CAP_30 },
    { "60", PLATFORM_FRAME_CAP_60 },
    { "120", PLATFORM_FRAME_CAP_120 },
    { "display", PLATFORM_FRAME_CAP_DISPLAY },
    { "uncapped", PLATFORM_FRAME_CAP_UNCAPPED },
};
```

- [ ] **Step 1.2 — Rewrite `platformFrameCapPeriodMs()` and add the predicate.**

Anchor: `platform_sdl.c:1542-1564` (the function whose DISPLAY case has the long
"fixed 60 Hz integer-tick model" comment). Replace the whole function with:

```c
static double platformFrameCapPeriodMs(void)
{
    switch (g_frameCapMode) {
        case PLATFORM_FRAME_CAP_30:
            return 1000.0 / 30.0;
        case PLATFORM_FRAME_CAP_120:
            return 1000.0 / 120.0;
        case PLATFORM_FRAME_CAP_UNCAPPED:
            /* Pacer disarms itself on 0.0 (the `if (period_ms > 0.0)` guard in
             * platformFrameSync). VSync, if on, remains the only bind. */
            return 0.0;
        case PLATFORM_FRAME_CAP_DISPLAY:
            /* F5: pace to the real panel rate. Historical note — before the
             * uncap loop existed this deliberately returned 1000/60, because
             * waitForNextFrame()'s round-to-nearest tick counter treats any
             * frame >= ~1/120 s as one full tick, so a >60 Hz panel ran the
             * sim 1.25-2x too fast. That hazard is gone on the uncap path
             * (pcUncapActive: the field accumulator holds the sim at 60), and
             * on the legacy path (deterministic/RAMROM force it) gate #1
             * still blocks at 60, so a fast pacer merely spins the loop. */
        {
            SDL_DisplayMode m;
            int di = g_sdlWindow ? SDL_GetWindowDisplayIndex(g_sdlWindow) : 0;
            if (di >= 0 && SDL_GetCurrentDisplayMode(di, &m) == 0 && m.refresh_rate > 0) {
                return 1000.0 / (double)m.refresh_rate;
            }
            return 1000.0 / 60.0;
        }
        case PLATFORM_FRAME_CAP_60:
        default:
            return 1000.0 / 60.0;
    }
}

/* F5: does the configured cap request the uncapped-render loop?
 * Game-side pcUncapActive() (unk_0C0A70.c) calls this via inline extern. */
int platformFrameCapWantsUncap(void)
{
    return g_frameCapMode == PLATFORM_FRAME_CAP_120 ||
           g_frameCapMode == PLATFORM_FRAME_CAP_DISPLAY ||
           g_frameCapMode == PLATFORM_FRAME_CAP_UNCAPPED;
}
```

(Confirm the variable holding the SDL window is named `g_sdlWindow` — it is used at
`gfx_pc.c:24910` `SDL_GL_SwapWindow(g_sdlWindow)`; if platform_sdl.c uses a different
local name, grep `SDL_CreateWindow` in platform_sdl.c and use that one.)

- [ ] **Step 1.3 — Update the registration help text.**

Anchor: `platform_sdl.c:1629-1635`, the `settingsRegisterEnum("Video.FrameCap", ...)`
call. Change only the final help string:

```c
                         "Frame cap",
                         "Frame pacing cap: 30, 60, 120, display, or uncapped. Values above 60 present extra interpolated frames; the simulation always runs at 60 Hz.");
```

- [ ] **Step 1.4 — Build + behavioral check (must be a no-op today).**

```bash
cmake --build build -j
GE007_FRAME_CAP=uncapped GE007_NO_VSYNC=1 ./build/ge007 --level dam
```

Expected: game runs at normal speed; FPS overlay still shows ~60 (gate #1 blocks; the
loop spins in `waitForNextFrame`'s busy-wait instead of the pacer's sleep — CPU use may
rise, that's fine and temporary). `GE007_FRAME_CAP=60` behaves exactly as before.

```bash
scripts/ci/ci_local.sh    # all rails still green
```

- [ ] **Step 1.5 — Commit.**

```bash
git add src/platform/platform_sdl.c
git commit -m "feat(F5): FrameCap 120/display/uncapped values + real pacer periods (loop still 60-gated)"
```

---

## Task 2 (M1): Key GE007_AUTO_* scripted-input schedules on sim time (identity refactor)

**Why:** deterministic smoke tests script inputs with GE007_AUTO_* env vars; the
schedule is keyed on `input_frame = g_frame_count_diag + 1` (`src/platform/stubs.c:5975`)
where `g_frame_count_diag` is a **gfx-frame** counter (incremented per `gfx_run_dl`,
`gfx_pc.c:23988`). Once render-only frames exist, gfx frames ≠ sim ticks and every
scripted harness shifts. At capped 60 the counters are 1:1, so re-keying now is an
identity change — land it and prove it before anything else moves.

**Files:**
- Modify: `src/game/unk_0C0A70.c` (accessor), `src/game/unk_0C0A70.h` (decl)
- Modify: `src/platform/stubs.c` (the scripted-input block inside `osContGetReadData`,
  ~`:5959-6146`)

**Interfaces:**
- Produces: `s32 pcSimFieldCount(void)` — returns `currentFrameCounter` (fields elapsed;
  advances during pause; +1 per frame in deterministic mode). Consumed by: this task,
  Task 3's audio gate, Task 6's generation tracking.

- [ ] **Step 2.1 — Record the before-hash (this is the "failing test" baseline).**

```bash
cmake --build build -j
./build/ge007 --deterministic --level dam --screenshot-frame 600 --screenshot-exit \
    --sim-state-hash-out /tmp/f5_t2_before.json
```

If any GE007_AUTO_*-scripted smoke exists and a ROM is present, run one and keep its
output too (e.g. `tools/dam_progression_smoke.sh`).

- [ ] **Step 2.2 — Add the accessor.**

In `src/game/unk_0C0A70.c`, inside the existing `#ifdef NATIVE_PORT` region (after
`pcTraceFrameTiming`), add:

```c
/* F5: sim-field counter for harness scheduling. Advances only when the sim
 * clock advances (updateFrameCounters), so GE007_AUTO_* schedules stay aligned
 * to sim time when render frames decouple from sim ticks. 1:1 with gfx frames
 * at capped 60 — switching the key is an identity change there. */
s32 pcSimFieldCount(void)
{
    return currentFrameCounter;
}
```

In `src/game/unk_0C0A70.h`, after the existing prototypes
(`void updateFrameCounters(s32 deltaFrames);`), add:

```c
#ifdef NATIVE_PORT
s32 pcSimFieldCount(void);
#endif
```

- [ ] **Step 2.3 — Re-key the scripted-input block.**

In `src/platform/stubs.c`, find the exact line (currently `:5975`):

```c
    input_frame = g_frame_count_diag + 1;
```

Replace with:

```c
    {
        extern s32 pcSimFieldCount(void);
        input_frame = pcSimFieldCount() + 1;
    }
```

Then audit the rest of `osContGetReadData` for other `g_frame_count_diag` reads used
for *scheduling* (not logging):

```bash
awk '/int osContGetReadData|void osContGetReadData/,0' src/platform/stubs.c | grep -n g_frame_count_diag
```

Convert scheduling uses identically; leave pure log/printf uses alone.

- [ ] **Step 2.4 — Prove identity.**

```bash
cmake --build build -j
./build/ge007 --deterministic --level dam --screenshot-frame 600 --screenshot-exit \
    --sim-state-hash-out /tmp/f5_t2_after.json
diff /tmp/f5_t2_before.json /tmp/f5_t2_after.json && echo IDENTICAL
ctest --test-dir build
```

Expected: `IDENTICAL`; ctest green. **If the hashes differ, the counters were not 1:1
somewhere — stop and root-cause (likely a startup frame where gfx ran without a sim
field). Do not paper over with a fudge offset.**

- [ ] **Step 2.5 — Commit.**

```bash
git add src/platform/stubs.c src/game/unk_0C0A70.c src/game/unk_0C0A70.h
git commit -m "refactor(F5): key GE007_AUTO_* input schedules on sim fields, not gfx frames (identity at 60)"
```

---

## Task 3 (M2): The decoupled loop — non-blocking sim advance + render-only frames

The enabling task. After it, `Video.FrameCap=uncapped` presents **duplicate frames** at
render rate with the sim bit-exact at 60. No interpolation yet — this task is about the
loop being *correct*, which Task 4's gate then proves mechanically.

**Files:**
- Modify: `src/game/unk_0C0A70.c` / `.h` — advance function, fuzz mode, exported globals
- Modify: `src/boss.c` — retrace-handler wiring (4 edits, all anchored)
- Modify: `src/platform/platform_sdl.c:3011` — field-gated audio pump

**Interfaces (all in `unk_0C0A70.h`, consumed by Tasks 4–9):**
- `int pcUncapActive(void)` — uncap loop live? False under RAMROM; under
  `--deterministic` true only when `GE007_UNCAP_FUZZ` is set.
- `s32 pcUncappedFrameAdvance(void)` — returns fields advanced (0 ⇒ render-only frame).
- `extern s32 g_pcUncapRenderOnlyFrame;` — 1 during a render-only iteration. **Always 0
  in capped mode** (that's what makes every capped-path gate an identity).
- `extern f32 g_pcInterpAlpha;` — [0,1] fraction of the current field elapsed.
- `extern f32 g_pcUncapDtFields;` — last frame's wall duration in fields (rate scaling).

- [ ] **Step 3.1 — Add the advance + fuzz code to `unk_0C0A70.c`.**

Append inside the existing `#ifdef NATIVE_PORT` region (after `pcSimFieldCount` from
Task 2). This is complete, drop-in code:

```c
/* ============================ F5: uncapped render ============================
 * Non-blocking replacement for waitForNextFrame(). The sim advances by whole
 * 1/60 s fields from a drift-free accumulator; iterations between fields are
 * render-only frames (speedgraphframes = 0 -> g_ClockTimer = 0, the same held
 * frame pause produces). waitForNextFrame()'s round-to-nearest (+387937 bias,
 * baseline reset to `now`) is deliberately NOT reused: it credits a tick at
 * half a field (the >60Hz-panel 2x-speed bug) and discards the remainder -
 * and the remainder IS the interpolation alpha. */

s32 g_pcUncapRenderOnlyFrame = 0;
f32 g_pcInterpAlpha = 1.0f;
f32 g_pcUncapDtFields = 1.0f;

#ifdef REFRESH_PAL
#define PC_FIELD_COUNTS 931050u
#else
#define PC_FIELD_COUNTS 775875u
#endif

static u32 s_uncapBase;
static u32 s_uncapPrevNow;
static int s_uncapBaseValid = 0;

static int s_fuzzChecked = 0;
static u32 s_fuzzState = 0;

static int pcUncapFuzzEnabled(void)
{
    if (!s_fuzzChecked) {
        const char *env = getenv("GE007_UNCAP_FUZZ");
        s_fuzzState = (env && env[0]) ? ((u32)atoi(env) | 1u) : 0u;
        s_fuzzChecked = 1;
    }
    return s_fuzzState != 0u;
}

int pcUncapActive(void)
{
    extern int g_deterministic;
    extern s32 get_is_ramrom_flag(void);
    extern int platformFrameCapWantsUncap(void);

    if (get_is_ramrom_flag() != 0) {
        return 0; /* replay timing is sacred - never uncap under RAMROM */
    }
    if (g_deterministic) {
        /* Purity harness only: deterministic + GE007_UNCAP_FUZZ injects
         * render-only frames on a seeded schedule (no wall clock involved). */
        return pcUncapFuzzEnabled();
    }
    return platformFrameCapWantsUncap();
}

static s32 pcUncapFuzzAdvance(void)
{
    /* xorshift32 - the schedule is a pure function of the seed */
    s_fuzzState ^= s_fuzzState << 13;
    s_fuzzState ^= s_fuzzState >> 17;
    s_fuzzState ^= s_fuzzState << 5;

    if ((s_fuzzState % 4u) != 0u) {
        /* ~75% of iterations: render-only frame with a pseudo-random alpha */
        g_pcUncapRenderOnlyFrame = 1;
        speedgraphframes = 0;
        g_pcInterpAlpha = (f32)(s_fuzzState % 1000u) / 1000.0f;
        g_pcUncapDtFields = 0.25f;
        return 0;
    }
    g_pcUncapRenderOnlyFrame = 0;
    g_pcInterpAlpha = 0.0f;
    g_pcUncapDtFields = 1.0f;
    updateFrameCounters(1);
    return 1;
}

s32 pcUncappedFrameAdvance(void)
{
    extern int g_deterministic;
    u32 now;
    u32 elapsed;
    s32 n;

    if (g_deterministic) {
        /* Reachable only via the fuzz harness (see pcUncapActive). Do not
         * touch osGetCount() here - the deterministic synthetic clock jumps a
         * full field per call and would corrupt the schedule. */
        return pcUncapFuzzAdvance();
    }

    now = osGetCount();
    if (!s_uncapBaseValid) {
        s_uncapBase = now;
        s_uncapPrevNow = now;
        s_uncapBaseValid = 1;
    }

    g_pcUncapDtFields = (f32)(u32)(now - s_uncapPrevNow) / (f32)PC_FIELD_COUNTS;
    if (g_pcUncapDtFields > 4.0f) g_pcUncapDtFields = 4.0f;
    s_uncapPrevNow = now;

    elapsed = now - s_uncapBase;
    n = (s32)(elapsed / PC_FIELD_COUNTS);

    if (n > 0) {
        if (n > 4) {
            /* Stall (alt-tab, load spike): drop the missed fields beyond the
             * sim's own 1..4 clamp so the accumulator can't wind up and
             * fast-forward the next seconds of play. */
            s_uncapBase += (u32)(n - 4) * PC_FIELD_COUNTS;
            n = 4;
        }
        s_uncapBase += (u32)n * PC_FIELD_COUNTS;
        g_pcUncapRenderOnlyFrame = 0;
        updateFrameCounters(n); /* speedgraphframes = n (mirror-clamped there) */
    } else {
        g_pcUncapRenderOnlyFrame = 1;
        /* Do NOT call updateFrameCounters(0): it would reset the osGetCount
         * baselines and fields would never accumulate. Render-only frames
         * only zero the per-frame tick source. */
        speedgraphframes = 0;
    }

    /* Fractional field elapsed at frame start = the interpolation alpha.
     * After the baseline advance above, (now - base) is exactly the remainder. */
    g_pcInterpAlpha = (f32)(u32)(now - s_uncapBase) / (f32)PC_FIELD_COUNTS;
    if (g_pcInterpAlpha < 0.0f) g_pcInterpAlpha = 0.0f;
    if (g_pcInterpAlpha > 1.0f) g_pcInterpAlpha = 1.0f;

    pcTraceFrameTiming(n > 0 ? "uncap-tick" : "uncap-render",
                       s_uncapBase, now, n);
    return n;
}
```

Also add one line to the **existing** `store_osgetcount()` body (`unk_0C0A70.c:123-133`)
— it is called on stage load (`boss.c:509`, `:531`) to discard init time, and the
accumulator must re-arm at the same moments. After the
`copy_of_osgetcount_value_0 = copy_of_osgetcount_value_1;` line, add:

```c
#ifdef NATIVE_PORT
    s_uncapBaseValid = 0; /* F5: re-arm the uncap accumulator on stage-load resets */
#endif
```

(Note: `store_osgetcount` sits *above* the F5 block in the file, and `s_uncapBaseValid`
must be declared before use — either move the three `static` accumulator variables up
next to the file's other statics at `:34-37`, or forward-declare. Moving them up is
cleaner; do that.)

- [ ] **Step 3.2 — Header declarations.**

In `src/game/unk_0C0A70.h`, extend the Task 2 block to:

```c
#ifdef NATIVE_PORT
s32 pcSimFieldCount(void);
int pcUncapActive(void);
s32 pcUncappedFrameAdvance(void);
extern s32 g_pcUncapRenderOnlyFrame;
extern f32 g_pcInterpAlpha;
extern f32 g_pcUncapDtFields;
#endif
```

(`boss.c:49` already includes `game/unk_0C0A70.h`, so boss.c sees all of this.)

- [ ] **Step 3.3 — Wire the boss loop (4 anchored edits in `src/boss.c`).**

**Edit A — bypass the half-tick gate.** Anchor (`boss.c:548-549`):

```c
                    mainTickElapsed = (u32) (osGetCount() - copy_of_osgetcount_value_1);
                    if (mainTickElapsed < MAIN_LOOP_TICK_INTERVAL)
```

`copy_of_osgetcount_value_1` only advances on tick frames, so this gate would eat every
render-only frame within ~8.3 ms of a tick (capping presentation at ~120 fps with bad
phase). Change the condition to:

```c
                    mainTickElapsed = (u32) (osGetCount() - copy_of_osgetcount_value_1);
#ifdef NATIVE_PORT
                    if (mainTickElapsed < MAIN_LOOP_TICK_INTERVAL && !pcUncapActive())
#else
                    if (mainTickElapsed < MAIN_LOOP_TICK_INTERVAL)
#endif
```

**Edit B — dispatch the advance.** Anchor (`boss.c:557-570`), currently:

```c
                            if (get_is_ramrom_flag())
                            {
                                ...
                                iterate_ramrom_entries_handle_camera_out();
                                ...
                            }
                            else
                            {
                                waitForNextFrame();
                            }
```

Change the `else` to:

```c
#ifdef NATIVE_PORT
                            else if (pcUncapActive())
                            {
                                pcUncappedFrameAdvance();
                            }
#endif
                            else
                            {
                                waitForNextFrame();
                            }
```

**Edit C — tick-gate the joy calls.** Anchor (`boss.c:575-584`), the block containing
`joyPoll();` and `joyConsumeSamplesWrapper();`. Wrap **only those two calls** (keep the
`pcRamromTraceLoopPhase` markers and `permit_stderr(0)` outside the gate; keep
`gdl = firstGdl = dynGetMasterDisplayList();` at `:587` untouched — it must run every
frame):

```c
#ifdef NATIVE_PORT
                            if (!g_pcUncapRenderOnlyFrame)
#endif
                            {
                                joyPoll(); /* On N64, scheduler thread calls joyPoll; on PC we must call it explicitly */
                                joyConsumeSamplesWrapper();
                            }
```

(Fold the existing `pcRamromTraceLoopPhase("mainloop_before/after_joy_*")` markers
inside the braces with the calls they bracket — RAMROM never runs uncapped, so their
placement relative to the gate is inert; keeping them adjacent to their calls preserves
trace semantics.)

**Edit D — tick-gate the debug-menu processor.** Anchor: the block starting at
`boss.c:589` (`#ifdef DEBUGMENU` ... `debug_menu_processor` ... ends `:609`). It reads
joy buttons, which don't update on render-only frames. Wrap the whole
`#ifdef DEBUGMENU`/`#ifndef DEBUGMENU` construct:

```c
#ifdef NATIVE_PORT
                            if (!g_pcUncapRenderOnlyFrame)
#endif
                            {
                                /* ...existing DEBUGMENU / debug_menu_processor construct,
                                 *    boss.c:589-609, unchanged inside... */
                            }
```

Everything else in the frame body — `lvlManageMpGame()` (sees `speedgraphframes == 0`
→ `g_ClockTimer = 0`, the pause path), `shuffle_player_ids()`, the per-player
`lvlViewMoveTick()` loop, `lvlRender`, `dynSwapBuffers`, `rspGfxTaskStart` — runs on
**every** iteration, unchanged.

- [ ] **Step 3.4 — Field-gate the audio pump.**

Anchor (`src/platform/platform_sdl.c:3011`), currently exactly:

```c
    { extern void portAudioFrame(void); portAudioFrame(); }
```

Replace with:

```c
    {
        /* F5: pump audio once per 60 Hz sim field (currentFrameCounter
         * advances per field, INCLUDING pause - menus need sound), never per
         * render frame: at 240 fps an unguarded pump would 4x the queue and
         * trip the AI_QUEUE_LIMIT drop path (stubs.c). Identity at capped 60,
         * where the counter advances every frame. A multi-field stall pumps
         * once; the adaptive buffer sizing in audi_port.c targets a ~2-frame
         * backlog and synthesizes a correspondingly larger buffer. */
        extern s32 pcSimFieldCount(void);
        extern void portAudioFrame(void);
        static s32 s_lastAudioField = -2147483647;
        s32 field = pcSimFieldCount();
        if (field != s_lastAudioField) {
            s_lastAudioField = field;
            portAudioFrame();
        }
    }
```

- [ ] **Step 3.5 — Build + capped identity proof.**

```bash
cmake --build build -j
./build/ge007 --deterministic --level dam --screenshot-frame 600 --screenshot-exit \
    --sim-state-hash-out /tmp/f5_t3_det.json
diff /tmp/f5_t2_after.json /tmp/f5_t3_det.json && echo IDENTICAL
ctest --test-dir build && scripts/ci/ci_local.sh
```

Expected: `IDENTICAL` (the capped/deterministic path is untouched); all rails green.

- [ ] **Step 3.6 — Uncapped manual smoke (duplicate-frame mode).**

```bash
GE007_FRAME_CAP=uncapped GE007_VSYNC=off ./build/ge007 --level dam
```

Checklist (all must hold):
- FPS overlay reports > 60 (typically 100–190 on the reference box).
- Game speed is normal: the mission stopwatch (watch → briefing screen) advances 30
  seconds in 30 wall seconds.
- Audio is clean (no stutter/pitch shift); pause menu opens/closes; menu music plays
  while paused.
- Mouse look works; firing works; a full level is playable.
- `GE007_FRAME_CAP=display` on a >60 Hz panel paces to the panel rate.
- Alt-tab away for 5 s and back: no fast-forward burst (the >4-field drop).
- Known-accepted at this task: motion looks 60 Hz-steppy (duplicate frames). That is
  the *expected* state until Tasks 5–8.

- [ ] **Step 3.7 — Commit.**

```bash
git add src/game/unk_0C0A70.c src/game/unk_0C0A70.h src/boss.c src/platform/platform_sdl.c
git commit -m "feat(F5): decoupled main loop - non-blocking field accumulator, render-only frames, tick-gated input/audio"
```

---

## Task 4 (M2): The 0-tick purity gate + fallout fixes

The project's central oracle: a deterministic run with render-only frames injected on a
seeded schedule must produce the **same sim-state hash** as the vanilla run. Anything a
render-only frame mutates in hashed state is a bug this catches mechanically — now and
for every future commit.

Two alignment facts make this work with zero new CLI surface:
1. `--screenshot-game-timer N` exits at `g_GlobalTimer == N` (**sim time** — identical
   in both runs; `main_pc.c:711-713`, check at `platform_sdl.c:2995-3001`).
2. The hash JSON (`--sim-state-hash-out`, emitted in
   `platformFinishAutoScreenshotIfRequested`, `platform_sdl.c:1062-1068`) embeds a
   `frame` field keyed on `g_frameSyncCallCount` (**render frames** — differs under
   fuzz by design). The gate therefore compares the JSON **minus** the `frame`/`replay`
   fields, not the raw file.

**Files:**
- Create: `tools/uncap_purity_gate.sh`
- Modify: whatever the gate flushes out (candidate list in Step 4.3)

- [ ] **Step 4.1 — Write the gate.**

Create `tools/uncap_purity_gate.sh` (then `chmod +x`):

```bash
#!/usr/bin/env bash
# uncap_purity_gate.sh — F5 rail: render-only frames are sim-pure.
#
# Runs a deterministic session (input frozen by --deterministic) twice per
# level — vanilla vs GE007_UNCAP_FUZZ=<seed> (seeded render-only-frame
# injection, ~75% of loop iterations) — and asserts the final sim-state
# hashes are identical. Exit alignment uses --screenshot-game-timer, which
# keys on g_GlobalTimer (sim time), so both runs hash the same sim moment.
# The emitted JSON's `frame` field counts RENDER frames and legitimately
# differs under fuzz; compare everything else.
#
# ROM-gated: skips cleanly when build/ge007 or the ROM is absent (CI runs the
# ROM-free ctest lanes instead; see tools/sim_invariance_gate.sh for the
# precedent).
set -euo pipefail
cd "$(dirname "$0")/.."

BIN="${BIN:-build/ge007}"
LEVELS="${LEVELS:-dam bunker1 archives}"   # exterior + interior + guard-dense (stage NAMES)
GAMETIMER="${GAMETIMER:-900}"    # 15 sim-seconds of gameplay
SEEDS="${SEEDS:-1337 4242}"

[ -x "$BIN" ] || { echo "SKIP: $BIN not built"; exit 0; }
[ -f baserom.u.z64 ] || { echo "SKIP: no ROM"; exit 0; }

canon() {
  # Print the hash JSON minus run-shape fields (frame = render-frame count,
  # replay = ramrom path). Everything else must match exactly.
  python3 - "$1" <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
d.pop("frame", None)
d.pop("replay", None)
print(json.dumps(d, sort_keys=True))
EOF
}

fail=0
for lvl in $LEVELS; do
  base_json=$(mktemp)
  "$BIN" --deterministic --level "$lvl" \
      --screenshot-game-timer "$GAMETIMER" --screenshot-exit \
      --sim-state-hash-out "$base_json" >/dev/null 2>&1 || true
  [ -s "$base_json" ] || { echo "FAIL: level $lvl vanilla run emitted no hash"; fail=1; continue; }
  base_canon=$(canon "$base_json")

  for seed in $SEEDS; do
    fuzz_json=$(mktemp)
    GE007_UNCAP_FUZZ=$seed "$BIN" --deterministic --level "$lvl" \
        --screenshot-game-timer "$GAMETIMER" --screenshot-exit \
        --sim-state-hash-out "$fuzz_json" >/dev/null 2>&1 || true
    [ -s "$fuzz_json" ] || { echo "FAIL: level $lvl seed $seed emitted no hash"; fail=1; continue; }
    fuzz_canon=$(canon "$fuzz_json")

    if [ "$base_canon" != "$fuzz_canon" ]; then
      echo "FAIL: level $lvl seed $seed — render-only frames perturbed sim state"
      echo "  vanilla: $base_canon"
      echo "  fuzzed:  $fuzz_canon"
      fail=1
    fi
  done
  echo "level $lvl: OK"
done

if [ "$fail" -ne 0 ]; then
  echo "UNCAP PURITY: FAIL" >&2
  exit 1
fi
echo "UNCAP PURITY: PASS — render-only frames are sim-pure."
```

- [ ] **Step 4.2 — First run + JSON sanity.**

```bash
chmod +x tools/uncap_purity_gate.sh
tools/uncap_purity_gate.sh
```

On the very first run, also inspect one emitted JSON by hand
(`python3 -m json.tool < the-file`). If it contains any *other* run-shape field that
legitimately differs (e.g. a wall-clock timestamp), add exactly that key to the `pop`
list in `canon()` with a comment naming it. Per-register hashes (pool / prop_pool /
g_ClockTimer / g_GlobalTimer) must NOT be popped — they are the signal.

- [ ] **Step 4.3 — Fix what it catches (audit list, in order of likelihood).**

The hash covers the four registered regions, not every `src/game` file-scope global —
so work this list even if the gate passes first try:

1. **`D_80048380 += 1` (`lvl.c:2132`)** — increments per *unpaused frame*, so
   render-only frames inflate it. It only matters as the `== 0` first-frame-clamp test,
   but if it's pool-resident the gate trips on it. Fix (lvl.c is an allowed R2 writer;
   place inside the existing `#ifdef NATIVE_PORT` clamp block):

   ```c
        else
        {
            g_ClockTimer = speedgraphframes;
            /* ...existing clamp block unchanged... */
    #ifdef NATIVE_PORT
            if (!g_pcUncapRenderOnlyFrame)
    #endif
            {
                D_80048380 += 1;
            }
        }
   ```

   (Add `extern s32 g_pcUncapRenderOnlyFrame;` near lvl.c's other externs, or rely on
   `unk_0C0A70.h` if lvl.c includes it — check with `grep -n unk_0C0A70.h src/game/lvl.c`.)
2. **RNG draws on render-only frames.** Add a temporary diagnostic: under
   `GE007_TRACE_INTERP=1`, print `pcRandomGetNextCallCount()` (`src/random.h:9`) delta
   per frame from the boss loop; render-only frames must show delta 0 in a fuzz run.
   Suspects from the clock census: `model.c` (8 call sites — verify none run during DL
   build/skinning), HUD RNG in `mp_watch.c`/`bondview.c`. Fix by gating the *draw* on
   `g_ClockTimer > 0` — never by reseeding.
3. **`explosionScreenShake` (`explosions.c:1037`, called from the camera builder at
   `bondview.c:16293`)** — verified read-only over sim state in its body (sums
   magnitudes from the explosion buffer; writes only `*result` and calls `viShake`,
   which is vi/render state). No action expected; confirm `viShake` writes no pool state.
4. **`minimap_tick()` (`lvl.c:2139`)** — runs unconditionally in `lvlManageMpGame`;
   its delta is `g_ClockTimer` (0 on render-only frames). Confirm no unconditional
   accumulation.
5. **`lvlViewMoveTick` non-look side effects** — the PC room-camera sync
   (`lvl.c:6008+`) writes only `g_pcCam*` render globals (unhashed, render-side —
   fine). Under `--deterministic`, mouse/stick deltas are zeroed via
   `pcNativeLiveLookAllowed`/`g_freezeInput` (`lvl.c:5851-5864`), so the fuzz runs
   never take the angle-write branch. No action expected.

Re-run the gate after each fix until green on all levels × seeds.

- [ ] **Step 4.4 — Commit.**

```bash
git add tools/uncap_purity_gate.sh src/game/lvl.c   # plus any other fallout fixes
git commit -m "test(F5): 0-tick purity fuzz gate - render-only frames are sim-pure (+fallout fixes)"
```

---

## Task 5 (M3): Render-rate look — gamepad real-dt fix + A/B gate

Mouse look on render-only frames comes free (decision record §1.4) because mouse input
is **delta-based**. The **gamepad right stick is rate-based** (pixels *per frame*,
tuned for 60 fps): at 240 fps it turns 4× too fast, and with `Input.GamepadFpsScale` on
it goes dead on render-only frames (`g_GlobalTimerDelta == 0`). Fix the scaling; add
the A/B escape hatch; verify mouse semantics by hand.

**Files:**
- Modify: `src/game/lvl.c` — two anchored edits inside `lvlViewMoveTick`'s
  `#ifdef NATIVE_PORT` look-injection block (`:5833-6006`).

- [ ] **Step 5.1 — Gamepad real-dt scaling.**

Anchor (`lvl.c:5901`), currently exactly:

```c
                f32 fps = g_pcGamepadFpsScale ? g_GlobalTimerDelta : 1.0f;
```

Replace with:

```c
                f32 fps;
                {
                    /* F5: the stick term is a rate (px/frame tuned for 60fps);
                     * under the uncap loop it must scale by real frame time or
                     * turn speed multiplies with render rate (and FpsScale's
                     * g_GlobalTimerDelta reads 0 on render-only frames).
                     * g_pcUncapDtFields == 1.0 at exactly 60 fps, so capped
                     * play is identity regardless of the FpsScale knob. */
                    extern int pcUncapActive(void);
                    extern f32 g_pcUncapDtFields;
                    if (pcUncapActive()) {
                        fps = g_pcUncapDtFields;
                        if (fps > 4.0f) fps = 4.0f;
                    } else {
                        fps = g_pcGamepadFpsScale ? g_GlobalTimerDelta : 1.0f;
                    }
                }
```

(If `lvl.c` includes `unk_0C0A70.h` — check — the two `extern`s can be dropped in
favor of the header. Either way compiles; prefer the header if already included.)

- [ ] **Step 5.2 — A/B gate for late look.**

Anchor: the start of the injection body, immediately after the line (`lvl.c:5848`):

```c
        if (!g_pcDebugFlyCamera && g_CurrentPlayer->prop != NULL) {
```

Insert:

```c
#ifdef NATIVE_PORT
            {
                /* F5 A/B: GE007_UNCAP_NO_LATE_LOOK=1 defers look application
                 * to tick frames. Deltas keep accumulating in the platform
                 * layer (platformGetMouseDelta drains on read, so skipping the
                 * read loses nothing) - the next tick frame applies them all. */
                static int s_noLateLook = -1;
                extern s32 g_pcUncapRenderOnlyFrame;
                if (s_noLateLook < 0) {
                    const char *env = getenv("GE007_UNCAP_NO_LATE_LOOK");
                    s_noLateLook = (env && env[0] && env[0] != '0') ? 1 : 0;
                }
                if (s_noLateLook && g_pcUncapRenderOnlyFrame) {
                    goto pc_skip_live_look;
                }
            }
#endif
```

Place the label immediately **before** the room-camera-sync comment block
(`lvl.c:6008`, anchor text `/* Sync the PC rendering camera from the player's world
position.`) so the sync still runs on skipped frames:

```c
#ifdef NATIVE_PORT
pc_skip_live_look: ;
#endif
```

(Both goto and label sit inside the same `if (!g_pcDebugFlyCamera && ...)` block scope
— verify the label lands inside that brace pair, not after it.)

- [ ] **Step 5.3 — Validate.**

```bash
cmake --build build -j && tools/uncap_purity_gate.sh && ctest --test-dir build
GE007_FRAME_CAP=uncapped GE007_VSYNC=off ./build/ge007 --level dam
```

Manual checklist:
- Mouse look is visibly smoother than world motion (world still 60 Hz-stepped — expected).
- Gamepad: time a full-deflection 360° turn; must match a `GE007_FRAME_CAP=60` run
  (stopwatch, ±5%). Repeat with `Input.GamepadFpsScale` on and off.
- `GE007_UNCAP_NO_LATE_LOOK=1` restores 60 Hz-steppy look; turning still loses no input
  (aim lands in the same place after an identical physical mouse motion).
- Fire at a wall while turning: bullet decals appear at the crosshair (the sim consumes
  the same angles it always did).
- Tank level check (Streets/Runway cheat or `--level` with the tank): turret mouse-turn
  speed unchanged vs capped (the `in_tank_flag` branch shares `sens`, not `fps` — no
  change expected, verify anyway).

- [ ] **Step 5.4 — Commit.**

```bash
git add src/game/lvl.c
git commit -m "feat(F5): render-rate look - gamepad real-dt scaling + GE007_UNCAP_NO_LATE_LOOK A/B"
```

---

## Task 6 (M3): Camera-eye interpolation (`pc_render_interp.c`)

The eye position (`coll->collision_position`, view bob baked in) steps at tick rate;
lerp it between the last two ticks on the game's render side. With the eye smooth, all
static world geometry renders smoothly — the single highest-value interpolation in the
project.

**Files:**
- Create: `src/game/pc_render_interp.c`, `src/game/pc_render_interp.h`
  (auto-built via the `src/game/*.c` glob — no CMake edit)
- Modify: `src/game/bondview.c` — one insertion in `sub_GAME_7F087A08`
- Modify: `src/boss.c` — reset call on stage load

**Interfaces:**
- Produces: `void pcRenderInterpResolveEye(s32 playernum, struct coord3d *eye);`
  (in-place: replaces `*eye` with the lerped value when allowed), and
  `void pcRenderInterpReset(void);`
- Consumes: `pcUncapActive()`, `g_pcInterpAlpha`, `pcSimFieldCount()` (Task 3);
  `g_CameraMode` (`enum CAMERAMODE`, global at `bondview.c:1378`, enum in
  `src/bondconstants.h`); `coord3d` (`src/bondtypes.h:233-245`).

- [ ] **Step 6.1 — Create the header** `src/game/pc_render_interp.h`:

```c
#ifndef PC_RENDER_INTERP_H
#define PC_RENDER_INTERP_H

#include <ultra64.h>

struct coord3d;

/* F5: replace *eye (freshly read from sim state) with the interpolated
 * display-eye for this render frame. No-op unless the uncap loop is live and
 * the camera is in a first-person mode. Never writes sim state. */
void pcRenderInterpResolveEye(s32 playernum, struct coord3d *eye);

/* Snap all per-player eye state (stage load). */
void pcRenderInterpReset(void);

#endif
```

- [ ] **Step 6.2 — Create the TU** `src/game/pc_render_interp.c` (complete, drop-in):

```c
/* pc_render_interp.c — F5: per-player camera-eye interpolation.
 *
 * Render-side only: state lives in this TU's BSS (never s_pcPool), reads sim
 * state, writes nothing back — sim-hash invariant by construction, verified
 * by tools/uncap_purity_gate.sh (deterministic runs never take these branches:
 * pcUncapActive() is false under --deterministic except the fuzz harness, and
 * under fuzz this code reads sim state without writing it).
 *
 * Generation tracking keys on pcSimFieldCount() (fields, advances during
 * pause too): when the field count changes, the sim MAY have moved the eye,
 * so shift generations and capture. While paused the sim is frozen, so
 * prev == curr and the lerp is a no-op — no drift. */

#include <ultra64.h>
#include <stdlib.h>

#include "bondtypes.h"
#include "bondconstants.h"
#include "pc_render_interp.h"
#include "unk_0C0A70.h"

extern enum CAMERAMODE g_CameraMode;

#define PC_INTERP_MAX_PLAYERS 4

static struct {
    coord3d prev;
    coord3d curr;
    s32 field_curr;
    s32 field_prev;
    s32 valid;
} s_eye[PC_INTERP_MAX_PLAYERS];

static f32 s_snapDist2 = -1.0f;
static int s_disabled = -1;

static f32 pcEyeSnapDist2(void)
{
    if (s_snapDist2 < 0.0f) {
        const char *env = getenv("GE007_INTERP_SNAP_DIST");
        f32 d = (env && env[0]) ? (f32)atof(env) : 120.0f;
        s_snapDist2 = d * d;
    }
    return s_snapDist2;
}

static int pcEyeLerpAllowed(void)
{
    if (s_disabled < 0) {
        const char *env = getenv("GE007_UNCAP_NO_EYE_LERP");
        s_disabled = (env && env[0] && env[0] != '0') ? 1 : 0;
    }
    if (s_disabled || !pcUncapActive()) {
        return 0;
    }
    /* First-person modes only. Cutscene/death/swirl cameras are authored at
     * 60 Hz and cut discontinuously — hold them (global constraint #9). */
    return g_CameraMode == CAMERAMODE_FP || g_CameraMode == CAMERAMODE_FP_NOINPUT;
}

void pcRenderInterpReset(void)
{
    s32 i;
    for (i = 0; i < PC_INTERP_MAX_PLAYERS; i++) {
        s_eye[i].valid = 0;
    }
}

void pcRenderInterpResolveEye(s32 playernum, struct coord3d *eye)
{
    s32 field;
    f32 dx, dy, dz, a;

    if (playernum < 0 || playernum >= PC_INTERP_MAX_PLAYERS || eye == NULL) {
        return;
    }
    if (!pcEyeLerpAllowed()) {
        s_eye[playernum].valid = 0;   /* re-prime on the next allowed frame */
        return;
    }

    field = pcSimFieldCount();

    if (!s_eye[playernum].valid) {
        s_eye[playernum].prev = *eye;
        s_eye[playernum].curr = *eye;
        s_eye[playernum].field_prev = field;
        s_eye[playernum].field_curr = field;
        s_eye[playernum].valid = 1;
        return;                       /* first sighting: draw true state */
    }

    if (field != s_eye[playernum].field_curr) {
        /* the sim ticked since the last capture: shift generations */
        s_eye[playernum].prev = s_eye[playernum].curr;
        s_eye[playernum].field_prev = s_eye[playernum].field_curr;
        s_eye[playernum].curr = *eye;
        s_eye[playernum].field_curr = field;
    }

    dx = s_eye[playernum].curr.x - s_eye[playernum].prev.x;
    dy = s_eye[playernum].curr.y - s_eye[playernum].prev.y;
    dz = s_eye[playernum].curr.z - s_eye[playernum].prev.z;

    if (dx * dx + dy * dy + dz * dz > pcEyeSnapDist2()) {
        return;                       /* teleport / respawn / warp: snap */
    }

    a = g_pcInterpAlpha;
    eye->x = s_eye[playernum].prev.x + dx * a;
    eye->y = s_eye[playernum].prev.y + dy * a;
    eye->z = s_eye[playernum].prev.z + dz * a;
}
```

- [ ] **Step 6.3 — Hook the camera builder.**

Anchor: `src/game/bondview.c`, function `sub_GAME_7F087A08` (`:16277`). The gameplay
branch currently reads exactly (`:16298-16316`):

```c
#ifdef NATIVE_PORT
        /* On NATIVE_PORT, coll->pos (field_488.pos) is only set at spawn and ... */
        cam_pos.x = coll->collision_position.x;
        cam_pos.y = coll->collision_position.y;
        cam_pos.z = coll->collision_position.z;
#else
        cam_pos = coll->pos;
#endif
        cam_look = coll->applied_view;
        cam_up = coll->applied_view2;
#ifdef NATIVE_PORT
        bondviewApplyNativeRenderCameraClearance(&cam_pos, &cam_look, "gameplay_matrix");
#endif
```

Insert the resolve **between the `cam_pos.z = ...` line and the `#else`** — i.e., after
the eye is assembled and *before* the wall-clearance push-out (clearance must act on the
final displayed position):

```c
        cam_pos.x = coll->collision_position.x;
        cam_pos.y = coll->collision_position.y;
        cam_pos.z = coll->collision_position.z;
        /* F5: substitute the interpolated display eye (no-op when capped). */
        {
            extern s32 get_cur_playernum(void);
            pcRenderInterpResolveEye(get_cur_playernum(), &cam_pos);
        }
```

Add `#include "pc_render_interp.h"` next to bondview.c's other local includes
(grep `#include "` in the first 100 lines and match placement). The intro-camera branch
(`playerHasFrozenIntroCamera`, `:16280-16286`) is untouched — the mode gate holds there
anyway.

- [ ] **Step 6.4 — Reset on stage load.**

Anchor: `boss.c:506-510`:

```c
#ifdef NATIVE_PORT
        /* Reset frame timer before first waitForNextFrame to avoid huge delta
         * from init time being counted as elapsed game frames */
        store_osgetcount();
#endif
```

Extend to:

```c
#ifdef NATIVE_PORT
        /* Reset frame timer before first waitForNextFrame to avoid huge delta
         * from init time being counted as elapsed game frames */
        store_osgetcount();
        {
            extern void pcRenderInterpReset(void);
            pcRenderInterpReset(); /* F5: drop stale eye snapshots across stage loads */
        }
#endif
```

- [ ] **Step 6.5 — Validate.**

```bash
cmake --build build -j && tools/uncap_purity_gate.sh && ctest --test-dir build
GE007_FRAME_CAP=uncapped GE007_VSYNC=off ./build/ge007 --level dam
```

Manual checklist:
- Strafe along the Dam wall watching geometry: motion is now smooth at render rate.
  A/B: `GE007_UNCAP_NO_EYE_LERP=1` makes it visibly steppy again.
- Pause mid-run: image rock-still, zero drift (frozen sim ⇒ prev == curr).
- Intro cutscene → gameplay handoff: no position smear (mode gate + first-sighting).
- Die and respawn: no smear (snap distance).
- Sprint forward and stop abruptly: no rubber-banding past the stop point (we
  interpolate, never extrapolate).
- 2P split-screen: both viewports smooth independently.
- Known-accepted: guards/doors still steppy (Task 8), and while *riding* the Dam lift
  the platform is steppy relative to you (also Task 8).

- [ ] **Step 6.6 — Commit.**

```bash
git add src/game/pc_render_interp.c src/game/pc_render_interp.h src/game/bondview.c src/boss.c
git commit -m "feat(F5): camera-eye interpolation - per-player lerp of collision_position, FP-mode gated"
```

---

## Task 7 (M4): The matrix interpolation map (`gfx_interp.c`) — TDD, ROM-free

A pure, dependency-free TU: an open-addressed map from (prop, model, renderpass,
ordinal) → two matrix generations, with lerp + snap policy. **No includes from gfx_pc
or the game** — that is what makes it unit-testable without a ROM. Write the test
first; it defines the contract Task 8 wires up.

**Files:**
- Create: `tests/test_gfx_interp.c`, `src/platform/fast3d/gfx_interp.h`,
  `src/platform/fast3d/gfx_interp.c`
- Modify: `CMakeLists.txt` — two places (test target + `FAST3D_SOURCES`)

**Interfaces (produces — consumed verbatim by Task 8):**

```c
void gfx_interp_frame_begin(float alpha, int sim_ticks, int uncap_active);
void gfx_interp_set_context(const void *prop, const void *model, int renderpass);
void gfx_interp_clear_context(void);
int  gfx_interp_modelview_load(const float in[4][4], float out[4][4]); /* 1 => use out */
void gfx_interp_reset(void);
int  gfx_interp_map_occupancy(void);
```

Contract highlights the test encodes:
- `uncap_active == 0` ⇒ `gfx_interp_modelview_load` always returns 0 and captures
  nothing (capped-60 byte-identity, zero overhead beyond one branch).
- Substitution happens on tick frames too (alpha ≈ 0 ⇒ ≈ prev) — otherwise tick frames
  would jump ahead of the surrounding render-only frames and judder.
- Pass-through (return 0) on: no context; first sighting; the key skipped a tick
  (culled/appeared — generation gap); translation delta > snap threshold; probe
  exhaustion. Every failure mode degrades to "draw the true current state".

- [ ] **Step 7.1 — Write the failing test** `tests/test_gfx_interp.c`:

```c
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "gfx_interp.h"

static void mat_trans(float m[4][4], float x, float y, float z) {
    memset(m, 0, sizeof(float) * 16);
    m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
    m[3][0] = x; m[3][1] = y; m[3][2] = z;
}

int main(void) {
    float a[4][4], b[4][4], out[4][4];
    int hit;
    char propA[1], modelA[1];

    /* 1. First sighting: no prev generation -> pass-through */
    gfx_interp_reset();
    gfx_interp_frame_begin(0.0f, 1, 1);              /* tick frame, gen 1 */
    gfx_interp_set_context(propA, modelA, 0);
    mat_trans(a, 100.0f, 0.0f, 0.0f);
    hit = gfx_interp_modelview_load(a, out);
    assert(hit == 0);
    gfx_interp_clear_context();

    /* 2. Second tick captures prev=100/curr=110; alpha 0 -> prev; then a
     *    render-only frame at alpha 0.5 -> 105. (On render-only frames the
     *    game rebuilds the DL from unchanged state, so the incoming matrix
     *    equals curr — the test feeds `b` again to model exactly that.) */
    gfx_interp_frame_begin(0.0f, 1, 1);              /* tick frame, gen 2 */
    gfx_interp_set_context(propA, modelA, 0);
    mat_trans(b, 110.0f, 0.0f, 0.0f);
    hit = gfx_interp_modelview_load(b, out);
    assert(hit == 1 && out[3][0] == 100.0f);
    gfx_interp_clear_context();

    gfx_interp_frame_begin(0.5f, 0, 1);              /* render-only frame */
    gfx_interp_set_context(propA, modelA, 0);
    hit = gfx_interp_modelview_load(b, out);
    assert(hit == 1 && out[3][0] == 105.0f);
    gfx_interp_clear_context();

    /* 3. Teleport: translation beyond snap distance -> pass-through */
    gfx_interp_frame_begin(0.0f, 1, 1);              /* gen 3 */
    gfx_interp_set_context(propA, modelA, 0);
    mat_trans(a, 9000.0f, 0.0f, 0.0f);
    hit = gfx_interp_modelview_load(a, out);
    assert(hit == 0);
    gfx_interp_clear_context();

    /* 4. Ordinals: two loads under one context are independent keys */
    gfx_interp_reset();
    gfx_interp_frame_begin(0.0f, 1, 1);              /* gen 1 */
    gfx_interp_set_context(propA, modelA, 0);
    mat_trans(a, 10.0f, 0.0f, 0.0f); gfx_interp_modelview_load(a, out);
    mat_trans(a, 20.0f, 0.0f, 0.0f); gfx_interp_modelview_load(a, out);
    gfx_interp_clear_context();
    gfx_interp_frame_begin(0.0f, 1, 1);              /* gen 2 */
    gfx_interp_set_context(propA, modelA, 0);
    mat_trans(b, 12.0f, 0.0f, 0.0f);
    hit = gfx_interp_modelview_load(b, out);
    assert(hit == 1 && out[3][0] == 10.0f);          /* ordinal 0 pairs with 10 */
    mat_trans(b, 22.0f, 0.0f, 0.0f);
    hit = gfx_interp_modelview_load(b, out);
    assert(hit == 1 && out[3][0] == 20.0f);          /* ordinal 1 pairs with 20 */
    gfx_interp_clear_context();

    /* 5. Generation gap (prop culled for a tick) -> pass-through */
    gfx_interp_frame_begin(0.0f, 1, 1);              /* gen 3, propA absent  */
    gfx_interp_frame_begin(0.0f, 1, 1);              /* gen 4, propA returns */
    gfx_interp_set_context(propA, modelA, 0);
    mat_trans(b, 30.0f, 0.0f, 0.0f);
    hit = gfx_interp_modelview_load(b, out);
    assert(hit == 0);
    gfx_interp_clear_context();

    /* 6. uncap_active == 0 -> hard pass-through */
    gfx_interp_frame_begin(0.5f, 1, 0);
    gfx_interp_set_context(propA, modelA, 0);
    hit = gfx_interp_modelview_load(b, out);
    assert(hit == 0);

    printf("test_gfx_interp: all OK\n");
    return 0;
}
```

- [ ] **Step 7.2 — Create the header** `src/platform/fast3d/gfx_interp.h`:

```c
#ifndef GFX_INTERP_H
#define GFX_INTERP_H

/* F5: matrix interpolation map for uncapped rendering. Dependency-free
 * (no gfx_pc/game includes) so it unit-tests ROM-free. See gfx_interp.c. */

void gfx_interp_frame_begin(float alpha, int sim_ticks, int uncap_active);
void gfx_interp_set_context(const void *prop, const void *model, int renderpass);
void gfx_interp_clear_context(void);
/* Returns 1 and fills `out` with the display matrix; 0 => caller uses `in`. */
int  gfx_interp_modelview_load(const float in[4][4], float out[4][4]);
void gfx_interp_reset(void);
int  gfx_interp_map_occupancy(void);

#endif
```

- [ ] **Step 7.3 — CMake wiring, then watch the test fail to link.**

In `CMakeLists.txt`, after the `test_room_normals` block (~`:257-270`), add:

```cmake
    # F5 matrix-interpolation map — ROM-free unit test.
    add_executable(test_gfx_interp
        "${CMAKE_SOURCE_DIR}/tests/test_gfx_interp.c"
        "${CMAKE_SOURCE_DIR}/src/platform/fast3d/gfx_interp.c"
    )
    target_include_directories(test_gfx_interp PRIVATE "${CMAKE_SOURCE_DIR}/src/platform/fast3d")
    add_test(NAME gfx_interp COMMAND test_gfx_interp)
```

And append the TU to `FAST3D_SOURCES` (anchor `CMakeLists.txt:476-482`):

```cmake
set(FAST3D_SOURCES
    src/platform/fast3d/gfx_pc.c
    src/platform/fast3d/gfx_opengl.c
    src/platform/fast3d/gfx_cc.c
    src/platform/fast3d/gfx_backend.c
    src/platform/fast3d/gfx_room_normals.c
    src/platform/fast3d/gfx_interp.c
)
```

```bash
cmake -B build && cmake --build build --target test_gfx_interp 2>&1 | tail -5
```

Expected: **compile/link failure** (gfx_interp.c doesn't exist yet). That's the red bar.

- [ ] **Step 7.4 — Implement** `src/platform/fast3d/gfx_interp.c` (complete, drop-in):

```c
/* gfx_interp.c — F5: matrix interpolation map for uncapped rendering.
 *
 * On tick frames, captures every eligible modelview LOAD keyed by
 * (prop, model, renderpass, ordinal-within-context). On any frame while
 * uncapped, substitutes lerp(prevTick, currTick, alpha) so world entities
 * move at display time (~1 tick behind sim — the standard interpolation
 * trade; the camera is exempt and stays fresh, see Task 8 / gfx_sp_matrix).
 *
 * Dependency-free by design: no gfx_pc.h, no game headers — unit-tested
 * ROM-free by tests/test_gfx_interp.c, which is the authoritative contract.
 *
 * Identity guarantees:
 *  - uncap_active == 0  => gfx_interp_modelview_load returns 0 without
 *    capturing: capped-60 frames are byte-identical.
 *  - Substitution happens on tick frames too (alpha≈0 => ≈prev); otherwise
 *    tick frames would jump ahead of surrounding render-only frames.
 * Every pass-through (return 0) degrades to "draw the true current state". */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "gfx_interp.h"

#define INTERP_MAP_SLOTS 8192u   /* power of two; busy MP frames use ~2k keys */
#define INTERP_PROBE_MAX 8u

typedef struct {
    uint64_t key;                /* 0 = empty */
    float prev[4][4];
    float curr[4][4];
    uint32_t gen_prev;
    uint32_t gen_curr;
} InterpSlot;

static InterpSlot s_map[INTERP_MAP_SLOTS];
static uint32_t s_tick_gen = 0;  /* bumps once per sim-tick frame while active */
static float s_alpha = 1.0f;
static int s_is_tick_frame = 0;
static int s_active = 0;
static int s_occupancy = 0;

static const void *s_ctx_prop = NULL;
static const void *s_ctx_model = NULL;
static int s_ctx_renderpass = 0;
static uint32_t s_ctx_ordinal = 0;
static int s_ctx_set = 0;

static float s_snap_dist2 = -1.0f;

static float interp_snap_dist2(void) {
    if (s_snap_dist2 < 0.0f) {
        const char *env = getenv("GE007_INTERP_SNAP_DIST");
        float d = (env && env[0]) ? (float)atof(env) : 120.0f;
        s_snap_dist2 = d * d;
    }
    return s_snap_dist2;
}

static uint64_t interp_key(void) {
    /* FNV-1a over the identity tuple. Ordinal makes multiple matrix loads
     * under one context (bone chains) independent keys. */
    uint64_t h = 1469598103934665603ull;
    uint64_t parts[4];
    const unsigned char *p;
    size_t i;

    parts[0] = (uint64_t)(uintptr_t)s_ctx_prop;
    parts[1] = (uint64_t)(uintptr_t)s_ctx_model;
    parts[2] = (uint64_t)(uint32_t)s_ctx_renderpass;
    parts[3] = (uint64_t)s_ctx_ordinal;

    p = (const unsigned char *)parts;
    for (i = 0; i < sizeof(parts); i++) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h ? h : 1ull;         /* reserve 0 for empty slots */
}

void gfx_interp_reset(void) {
    memset(s_map, 0, sizeof(s_map));
    s_tick_gen = 0;
    s_occupancy = 0;
}

void gfx_interp_frame_begin(float alpha, int sim_ticks, int uncap_active) {
    s_active = uncap_active;
    s_alpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
    s_is_tick_frame = sim_ticks > 0;
    if (s_active && s_is_tick_frame) {
        s_tick_gen++;
        if (s_tick_gen == 0) {   /* u32 wrap after ~2.2y: nuke, don't alias */
            gfx_interp_reset();
            s_tick_gen = 1;
        }
    }
    s_ctx_set = 0;
}

void gfx_interp_set_context(const void *prop, const void *model, int renderpass) {
    s_ctx_prop = prop;
    s_ctx_model = model;
    s_ctx_renderpass = renderpass;
    s_ctx_ordinal = 0;
    s_ctx_set = 1;
}

void gfx_interp_clear_context(void) {
    s_ctx_set = 0;
}

int gfx_interp_map_occupancy(void) {
    return s_occupancy;
}

int gfx_interp_modelview_load(const float in[4][4], float out[4][4]) {
    uint64_t key;
    uint32_t idx, probe;
    InterpSlot *slot = NULL;
    float dx, dy, dz;
    int i, j;

    if (!s_active || !s_ctx_set) {
        return 0;
    }

    key = interp_key();
    s_ctx_ordinal++;

    idx = (uint32_t)(key & (INTERP_MAP_SLOTS - 1u));
    for (probe = 0; probe < INTERP_PROBE_MAX; probe++) {
        InterpSlot *cand = &s_map[(idx + probe) & (INTERP_MAP_SLOTS - 1u)];
        if (cand->key == key) {
            slot = cand;
            break;
        }
        if (cand->key == 0ull ||
            (s_is_tick_frame && cand->gen_curr + 2u < s_tick_gen)) {
            /* Empty (or stale ≥2 gens — reclaim on tick frames only). */
            if (!s_is_tick_frame) {
                return 0;        /* unseen key mid-interval: draw true state */
            }
            if (cand->key == 0ull) {
                s_occupancy++;
            }
            cand->key = key;
            memcpy(cand->curr, in, sizeof(cand->curr));
            cand->gen_curr = s_tick_gen;
            cand->gen_prev = 0;
            return 0;            /* first sighting: pass through */
        }
    }
    if (slot == NULL) {
        return 0;                /* probe exhausted: draw true state */
    }

    if (s_is_tick_frame && slot->gen_curr != s_tick_gen) {
        memcpy(slot->prev, slot->curr, sizeof(slot->prev));
        slot->gen_prev = slot->gen_curr;
        memcpy(slot->curr, in, sizeof(slot->curr));
        slot->gen_curr = s_tick_gen;
    }

    /* Lerp only across two consecutive ticks with a plausible delta. */
    if (slot->gen_curr != s_tick_gen || slot->gen_prev + 1u != slot->gen_curr) {
        return 0;
    }
    dx = slot->curr[3][0] - slot->prev[3][0];
    dy = slot->curr[3][1] - slot->prev[3][1];
    dz = slot->curr[3][2] - slot->prev[3][2];
    if (dx * dx + dy * dy + dz * dz > interp_snap_dist2()) {
        return 0;                /* teleport: snap to true state */
    }

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            out[i][j] = slot->prev[i][j] +
                        (slot->curr[i][j] - slot->prev[i][j]) * s_alpha;
        }
    }
    return 1;
}
```

- [ ] **Step 7.5 — Green bar.**

```bash
cmake --build build --target test_gfx_interp && ctest --test-dir build -R gfx_interp -V
```

Expected: `test_gfx_interp: all OK`, ctest PASS. Also full build still links
(`cmake --build build -j` — gfx_interp.c is now in ge007 too, unused until Task 8).

- [ ] **Step 7.6 — Commit.**

```bash
git add src/platform/fast3d/gfx_interp.c src/platform/fast3d/gfx_interp.h tests/test_gfx_interp.c CMakeLists.txt
git commit -m "feat(F5): matrix interpolation map (prop-keyed two-generation lerp) + ROM-free unit test"
```

---

## Task 8 (M4): Wire the map into fast3d — three surgical hooks

Eligibility rule: modelview `G_MTX_LOAD`s under **`DRAWCLASS_CHRPROP`** while a prop
context is set — chr placement + bone matrices, props, doors, lifts. Everything else
passes through: rooms (`is_room_matrix_addr`), the camera (`is_field_10e0_addr` — it is
rebuilt fresh per render frame by Tasks 5/6; lerping it here would re-add a tick of
look latency), intro matrices, projections, weapon/viewmodel (follows the fresh
camera), HUD/frontend.

**Files:**
- Modify: `src/game/rsp.c` — per-frame begin call
- Modify: `src/platform/fast3d/gfx_pc.c` — context hook + substitution hook
- Modify: `src/boss.c` — map reset on stage load

- [ ] **Step 8.1 — Per-frame begin call in `rsp.c`.**

Anchor (`src/game/rsp.c:227-240`), the `#ifdef NATIVE_PORT` body of `rspGfxTaskStart`:

```c
    extern void gfx_run_dl(Gfx *dl);
    extern void gfx_end_frame(void);
```

Extend the extern block and call before `gfx_run_dl(firstGdl);`:

```c
    extern void gfx_run_dl(Gfx *dl);
    extern void gfx_end_frame(void);
    extern void gfx_interp_frame_begin(float alpha, int sim_ticks, int uncap_active);

    /* F5: publish this frame's interpolation parameters to fast3d. Alpha and
     * the render-only flag come from the loop's field accumulator
     * (unk_0C0A70.c). In capped mode uncap_active=0 and the interp layer is
     * a hard pass-through. */
    {
        extern f32 g_pcInterpAlpha;
        extern s32 g_pcUncapRenderOnlyFrame;
        extern int pcUncapActive(void);
        gfx_interp_frame_begin(g_pcInterpAlpha,
                               g_pcUncapRenderOnlyFrame ? 0 : 1,
                               pcUncapActive());
    }
```

(`gfx_interp_*` passes the R1 rail: the denylist is
`_gfx_opengl_|_texture_pack_|_g_pcTexturePack|_gl[A-Z]|_glad_gl`; the `gfx_*`
submission surface is explicitly allowed — `check_sim_render_separation.sh:5-9`.)

- [ ] **Step 8.2 — Context hook in `gfx_pc.c`.**

Add `#include "gfx_interp.h"` with gfx_pc.c's other local includes (top of file).

Anchor: `gfx_set_prop_context` (`gfx_pc.c:565`). After the line
`g_current_prop_context.active = true;`, add:

```c
    /* F5: mirror the identity tuple into the interpolation map's context. */
    gfx_interp_set_context(prop, model, renderpass);
```

Anchor: `gfx_clear_prop_context` (`gfx_pc.c:616`), currently:

```c
void gfx_clear_prop_context(void) {
    memset(&g_current_prop_context, 0, sizeof(g_current_prop_context));
}
```

becomes:

```c
void gfx_clear_prop_context(void) {
    memset(&g_current_prop_context, 0, sizeof(g_current_prop_context));
    gfx_interp_clear_context();
}
```

- [ ] **Step 8.3 — Substitution hook in `gfx_sp_matrix`.**

Anchor: `gfx_pc.c:16024-16025`, the modelview LOAD branch:

```c
        if (parameters & G_MTX_LOAD) {
            memcpy(rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1], matrix, sizeof(matrix));
```

Insert between those two lines (so capture/substitution sees the **final** matrix —
the level-visibility scale at `:15944-15950` has already been applied to `matrix`,
keeping both stored generations in the same space):

```c
        if (parameters & G_MTX_LOAD) {
            /* F5: substitute the interpolated display matrix for world
             * entities. Eligibility mirrors the draw-class segmentation:
             * chr/prop/door/lift matrices only. Rooms are static; the camera
             * (field_10E0) is rebuilt fresh per render frame by the late-look
             * + eye-lerp path (lerping it here would re-add a tick of look
             * latency); weapon follows the camera; HUD is screen-space.
             * GE007_UNCAP_NO_MTX_LERP=1 is the A/B kill switch. */
            if (g_current_draw_class == DRAWCLASS_CHRPROP &&
                !is_room_matrix_addr && !is_field_10e0_addr && !is_intro_matrix_addr &&
                !gfx_interp_mtx_lerp_disabled()) {
                float f5_lerped[4][4];
                if (gfx_interp_modelview_load(matrix, f5_lerped)) {
                    memcpy(matrix, f5_lerped, sizeof(matrix));
                }
            }
            memcpy(rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1], matrix, sizeof(matrix));
```

`g_current_draw_class` is a file-static in the same TU (`gfx_pc.c:454`) — readable
directly. Add the kill-switch helper near the other env caches (e.g. below
`gfx_set_draw_class` at `:527`):

```c
static int gfx_interp_mtx_lerp_disabled(void) {
    static int v = -1;
    if (v < 0) {
        const char *env = getenv("GE007_UNCAP_NO_MTX_LERP");
        v = (env && env[0] && env[0] != '0') ? 1 : 0;
    }
    return v;
}
```

Note: `G_MTX_MUL` (non-LOAD) matrices and `G_MTX_PUSH` duplications inherit whatever
the loaded parent was — lerped parent × static local = correctly lerped child. No hook
needed there.

- [ ] **Step 8.4 — Map reset on stage load.**

Anchor: the Task 6 reset block in `boss.c` (Step 6.4). Extend it:

```c
        {
            extern void pcRenderInterpReset(void);
            extern void gfx_interp_reset(void);
            pcRenderInterpReset();  /* F5: drop stale eye snapshots across stage loads */
            gfx_interp_reset();     /* F5: drop stale matrix generations too */
        }
```

- [ ] **Step 8.5 — Validate: the full-feel milestone.**

```bash
cmake --build build -j
ctest --test-dir build -R "gfx_interp|sim_state_hash"
tools/uncap_purity_gate.sh
GE007_FRAME_CAP=uncapped GE007_VSYNC=off ./build/ge007 --level dam
```

Manual matrix checklist (all in uncapped mode; each has an A/B via
`GE007_UNCAP_NO_MTX_LERP=1`):
- Patrolling guards walk smoothly (steppy with the kill switch).
- Dam doors and the surface lift move smoothly; **ride the lift** — platform and world
  stay coherent (eye lerp and prop lerp share the same alpha).
- Kill a guard: death animation smooth; body settles without smearing.
- Guard spawn (alarm), despawn, and room-transition culling: no one-frame stretched
  matrices (generation-gap pass-through covers appear/disappear).
- Throw a grenade: the thrown prop arcs smoothly; the explosion sprite steps at 60 Hz
  (expected v1 — §F5.x.1).
- Look around fast: nothing in the world lags the camera except the documented ≤1-tick
  entity delay; the gun does NOT lag (weapon class passes through).
- Intro cutscene plays normally (camera 60 Hz-stepped there — accepted; world entities
  may lerp, which is fine).
- 2P split-screen match: both viewports smooth; no cross-viewport smearing (same world
  matrix captured once per key).
- `GE007_TRACE_INTERP=1` (after Task 9 adds the print): map occupancy stays well under
  8192 and constant-ish per scene.

**Byte-identity check (capped):** build a pre-F5 binary from `main` in a second
worktree (`git worktree add /tmp/mgb64-main main && cmake -B /tmp/mgb64-main/build ...`),
then run both with `--screenshot-label` to tag the outputs:

```bash
./build/ge007 --deterministic --level dam --screenshot-frame 600 --screenshot-exit \
    --screenshot-label f5   | tee /tmp/f5_shot.log
/tmp/mgb64-main/build/ge007 --deterministic --level dam --screenshot-frame 600 --screenshot-exit \
    --screenshot-label main | tee /tmp/main_shot.log
# The saved path is printed on capture — extract it from each log and compare:
cmp "$(grep -o '[^ ]*f5[^ ]*\.png'   /tmp/f5_shot.log   | tail -1)" \
    "$(grep -o '[^ ]*main[^ ]*\.png' /tmp/main_shot.log | tail -1)" && echo IDENTICAL
```

Expected: `IDENTICAL`. (If the capture line's format makes the grep miss, read the
`platformSaveScreenshot` print in platform_sdl.c and adjust the pattern — the invariant
being tested is the byte-equality, not the extraction method.)

**ASan soak:**

```bash
tools/asan_smoke.sh          # report-only lane; add GE007_FRAME_CAP=uncapped to its
                             # env if the script forwards environment (read its head)
```

Expected: no sanitizer reports.

- [ ] **Step 8.6 — Commit.**

```bash
git add src/platform/fast3d/gfx_pc.c src/game/rsp.c src/boss.c
git commit -m "feat(F5): world matrix interpolation - CHRPROP modelview lerp wired at gfx_sp_matrix"
```

---

## Task 9 (M5): Overlay + interp diagnostics + uncapped fps census

**Files:**
- Modify: `src/game/pc_fps_overlay.c` — sim/render split readout
- Modify: `src/boss.c` — GE007_TRACE_INTERP frame print
- Create: `tools/uncap_fps_census.sh`

- [ ] **Step 9.1 — Overlay split.**

Anchor (`pc_fps_overlay.c:140-146`) — the reformat block inside the
`stats->generation != s_lastGeneration` guard, currently exactly:

```c
    if (stats->generation != s_lastGeneration) {
        s_lastGeneration = stats->generation;
        snprintf(s_line1, sizeof(s_line1), "%3.0f FPS %4.1fms",
                 (double)stats->fps, (double)stats->frame_ms);
        snprintf(s_line2, sizeof(s_line2), "1%% low %3.0f",
                 (double)stats->low1_fps);
    }
```

Replace with:

```c
    if (stats->generation != s_lastGeneration) {
        extern int pcUncapActive(void);
        s_lastGeneration = stats->generation;
        snprintf(s_line1, sizeof(s_line1), "%3.0f FPS %4.1fms",
                 (double)stats->fps, (double)stats->frame_ms);
        if (pcUncapActive()) {
            /* F5: line 1 is now the presented (render) rate; make the fixed
             * sim rate explicit so 60-vs-240 confusion can't restart. */
            snprintf(s_line2, sizeof(s_line2), "1%% low %3.0f sim60",
                     (double)stats->low1_fps);
        } else {
            snprintf(s_line2, sizeof(s_line2), "1%% low %3.0f",
                     (double)stats->low1_fps);
        }
    }
```

(The stats source already measures real presented frames — `platformFrameStatsTick`
runs once per `platformFrameSync` — so line 1 automatically becomes the render rate
under uncap. `s_line2` is 32 bytes; `"1% low 999 sim60"` fits.)

- [ ] **Step 9.2 — GE007_TRACE_INTERP frame print.**

In `src/boss.c`, immediately after the advance dispatch (Task 3 Edit B), add:

```c
#ifdef NATIVE_PORT
                            {
                                static int s_traceInterp = -1;
                                if (s_traceInterp < 0) {
                                    s_traceInterp = (getenv("GE007_TRACE_INTERP") != NULL) ? 1 : 0;
                                }
                                if (s_traceInterp) {
                                    extern int gfx_interp_map_occupancy(void);
                                    extern u64 pcRandomGetNextCallCount(void);
                                    static u64 s_lastRng = 0;
                                    u64 rng = pcRandomGetNextCallCount();
                                    fprintf(stderr,
                                            "[INTERP] renderonly=%d alpha=%.3f dt=%.3f occ=%d rngDelta=%llu\n",
                                            g_pcUncapRenderOnlyFrame,
                                            g_pcInterpAlpha,
                                            g_pcUncapDtFields,
                                            gfx_interp_map_occupancy(),
                                            (unsigned long long)(rng - s_lastRng));
                                    s_lastRng = rng;
                                }
                            }
#endif
```

(`rngDelta` on render-only frames must print 0 — this is the permanent, greppable form
of the Task 4 RNG audit.)

- [ ] **Step 9.3 — Uncapped fps census** — create `tools/uncap_fps_census.sh`:

```bash
#!/usr/bin/env bash
# uncap_fps_census.sh — F5: presented-fps census in uncapped mode.
#
# Real-clock (NOT deterministic) runs per level with the uncap loop live,
# bounded by --screenshot-game-timer (sim seconds * 60). Parses the [PERF]
# tracer's interval_ms (frame-to-frame present interval) and reports
# mean/1%-low fps. Requires build/ge007 + ROM + a GUI session.
#
# Usage: tools/uncap_fps_census.sh            # all 20 levels
#        tools/uncap_fps_census.sh dam jungle # subset (names as in perf_census.sh)
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${BIN:-$REPO/build/ge007}"
SECONDS_PER_LEVEL="${SECONDS_PER_LEVEL:-20}"
GAMETIMER=$((SECONDS_PER_LEVEL * 60))

# --level accepts these stage NAMES directly (pcFindStageByName, main_pc.c:738)
# — the same list tools/perf_census.sh passes to --level. No id mapping needed.
ALL_LEVELS=(dam facility runway surface1 bunker1 silo frigate surface2 bunker2 \
            statue archives streets depot train jungle control caverns cradle aztec egypt)
LEVELS=("$@"); [ ${#LEVELS[@]} -eq 0 ] && LEVELS=("${ALL_LEVELS[@]}")

[ -x "$BIN" ] || { echo "SKIP: $BIN not built"; exit 0; }

echo "level,mean_fps,low1_fps,frames"
for name in "${LEVELS[@]}"; do
  log=$(mktemp)
  GE007_FRAME_CAP=uncapped GE007_NO_VSYNC=1 \
  GE007_PERF_TRACE=1 GE007_PERF_TRACE_AFTER_FRAME=120 GE007_PERF_TRACE_BUDGET=100000 \
    "$BIN" --level "$name" --no-input-grab \
    --screenshot-game-timer "$GAMETIMER" --screenshot-exit >"$log" 2>&1 || true
  python3 - "$name" "$log" <<'EOF'
import re, sys
name, path = sys.argv[1], sys.argv[2]
iv = [float(m.group(1)) for m in re.finditer(r'\[PERF\].*?interval_ms=([0-9.]+)', open(path, errors="ignore").read())]
iv = [x for x in iv if x > 0.01]
if not iv:
    print(f"{name},NA,NA,0"); sys.exit(0)
fps = sorted(1000.0 / x for x in iv)
mean = sum(fps) / len(fps)
low1 = fps[max(0, int(len(fps) * 0.01) - 1)]
print(f"{name},{mean:.1f},{low1:.1f},{len(fps)}")
EOF
  rm -f "$log"
done
```

(`--no-input-grab` mirrors perf_census.sh's usage so the run doesn't steal the mouse;
these are real-clock windowed runs, so expect ~7 minutes for the full 20-level sweep at
the default 20 s per level.)

- [ ] **Step 9.4 — Record the baseline.**

```bash
chmod +x tools/uncap_fps_census.sh
tools/uncap_fps_census.sh | tee /tmp/f5_census.csv
```

Paste the CSV into `docs/design/PERFORMANCE_PLAN.md` as a new "F5 uncapped presented
fps" subsection with date + hardware. Expectation on the M3 Max reference: every level
≥ 100 mean fps.

- [ ] **Step 9.5 — Commit.**

```bash
git add src/game/pc_fps_overlay.c src/boss.c tools/uncap_fps_census.sh docs/design/PERFORMANCE_PLAN.md
git commit -m "feat(F5): render/sim split FPS overlay, GE007_TRACE_INTERP diagnostics, uncapped fps census"
```

---

## Task 10 (M5): CI rails extension

**Files:**
- Modify: `scripts/ci/check_timing_lock.sh` (extends the existing R2 script — it
  already runs in `ci_local.sh:53` and `.github/workflows/ci.yml:32`; no workflow edits)

- [ ] **Step 10.1 — Append the F5 invariants.**

`check_timing_lock.sh` is plain bash with a `rogue` accumulator (see its current body —
40 lines). Append before the final `if [ "$rogue" -ne 0 ]` block, matching its style
(portable grep/awk only — macOS BSD grep has no `-P`):

```bash
# ---- F5 invariants (uncapped render) ----

# (3) pcUncapActive must fail closed under determinism + RAMROM. Extract the
#     function body and require both guards inside it.
body="$(awk '/^int pcUncapActive\(void\)/,/^}/' src/game/unk_0C0A70.c)"
echo "$body" | grep -q 'get_is_ramrom_flag' \
  || { echo "R2/F5 VIOLATION — pcUncapActive lost its RAMROM guard"; rogue=1; }
echo "$body" | grep -q 'g_deterministic' \
  || { echo "R2/F5 VIOLATION — pcUncapActive lost its deterministic guard"; rogue=1; }

# (4) speedgraphframes writers stay confined to unk_0C0A70.c (F5 drives the
#     zero-tick frame through it; a rogue writer elsewhere would bypass the
#     clamp/pause policy).
while IFS= read -r hit; do
  [ -z "$hit" ] && continue
  f="${hit%%:*}"
  case "$f" in
    src/game/unk_0C0A70.c) : ;;
    *) echo "R2/F5 VIOLATION — unexpected writer of speedgraphframes: $hit"; rogue=1 ;;
  esac
done < <(grep -rnE 'speedgraphframes[[:space:]]*(=[^=]|\+\+|--|[-+*/|&^]=)' src \
           --include='*.c' | grep -vE 's32[[:space:]]+speedgraphframes[[:space:]]*=' || true)

# (5) the audio pump must stay field-gated (Task 3.4).
grep -q 's_lastAudioField' src/platform/platform_sdl.c \
  || { echo "R2/F5 VIOLATION — portAudioFrame field gate removed"; rogue=1; }
```

Also update the final PASS echo to mention F5:

```bash
echo "R2: PASS — timing lock intact (g_ClockTimer writers confined; clamp+bypass present; F5 uncap guards present)."
```

- [ ] **Step 10.2 — Run everything.**

```bash
scripts/ci/ci_local.sh
```

Expected: all lanes green — including R1 (`check_sim_render_separation.sh`), which now
also proves `pc_render_interp.o` and the modified game objects picked up no backend
symbols. If invariant (4) flags a pre-existing writer (`front.c`'s
`portSetClockTimerFromEnv` writes `g_ClockTimer` but check whether it also writes
`speedgraphframes` — the clock census says it does not), investigate before allowlisting;
extend `ALLOWED`-style handling only with a comment explaining why.

- [ ] **Step 10.3 — Commit.**

```bash
git add scripts/ci/check_timing_lock.sh
git commit -m "ci(F5): timing-lock rail covers uncap guards, speedgraphframes confinement, audio field gate"
```

---

## Task 11 (M5): Validation matrix, docs, ship decision

- [ ] **Step 11.1 — Full regression sweep (record results in the PR body):**

```bash
tools/uncap_purity_gate.sh                       # + a wider run:
LEVELS="dam bunker1 archives train" SEEDS="1337 4242 777" tools/uncap_purity_gate.sh
tools/sim_invariance_gate.sh                     # pre-existing lane still green
ctest --test-dir build                           # all unit lanes
scripts/ci/ci_local.sh                           # all rails
tools/uncap_fps_census.sh                        # 20-level fps table
tools/asan_smoke.sh                              # sanitizer lane
```

- [ ] **Step 11.2 — Manual QA matrix** (all in `GE007_FRAME_CAP=uncapped`, then spot-check
`display` and `120`):

| Area | Check |
|---|---|
| Pause | open/close repeatedly; watch menu animates; zero world drift |
| Cutscenes | Dam intro → gameplay handoff; outro (POSEND); no camera smear at cuts |
| Death | death cam + respawn; no eye/matrix smear |
| Transitions | level → level; menu → level; alt-tab 5 s stall recovery |
| Split-screen | 2P match, both viewports smooth, scoreboard fine |
| Tank | drive + turret aim; turn rates match capped |
| ADS | `Input.AdsEnabled=1`: pose blend + sens trims feel unchanged |
| Timing truth | mission stopwatch 30 s == 30 wall seconds; music tempo normal |
| Settings menu | FrameCap row shows 30/60/120/display/uncapped and live-applies (rows are registry-driven, `pc_settings_menu.c:50` — new enum options appear automatically; verify, don't assume) |
| 60 Hz identity | default config: overlay pinned at 60, purity/screenshot identity green |

- [ ] **Step 11.3 — Docs.**
  - `docs/FRAME_TIMING_ARCHITECTURE.md`: rewrite §5 (Path A is now implemented —
    describe the accumulator, render-only frames, the three interpolators, and the
    fuzz gate) and §6 (new FrameCap values + all GE007_UNCAP_*/GE007_INTERP_* flags).
    Fix the now-stale sentence in §6 ("There is no `120` frame-cap value…").
  - `docs/ENV_FLAGS.md`: add every flag from the §4 table.
  - `docs/BACKLOG_v0.4.0.md:138-140`: move "120 Hz render interpolation (F5)" from
    the deferred list to shipped-behind-config; list §F5.x residuals as the new
    deferred items.

- [ ] **Step 11.4 — Ship decision.** Default stays `Video.FrameCap=60` for at least one
public release (faithful default; uncap is opt-in via settings menu / env / config).
Revisit flipping the default to `display` only after a release soak with zero
purity-gate regressions. Record the decision + date in the backlog entry.

- [ ] **Step 11.5 — Integrate.** Branch `feat/uncapped-fps`, PR against `main`, run
`superpowers:requesting-code-review` before merge. The PR body includes: the census
CSV, the QA matrix with results, and links to the green gate runs.

---

## §F5.x — Deferred follow-ups (tracked, deliberately out of v1 scope)

1. **Effect vertex interpolation** — explosions/sparks/shards emit raw world-space
   vertices per tick (`explosions.c:1965`, `unk_0A1DA0.c:4170`, via
   `dynAllocate7F0BD6C4`). Lerp whole vertex buffers keyed by (effect ptr, allocation
   ordinal) when counts match between ticks; snap otherwise. 60 Hz-stepped fireballs
   read acceptably — polish, not a blocker.
2. **Texture-scroll fractional advance** — sky cloud UV (`player.c:270-276`) and
   conveyor scrolls step at 60 Hz; a render-side fractional offset is a small win.
3. **Extrapolation mode** — `GE007_INTERP_EXTRAPOLATE=1`: `lerp(prev, curr, 1+alpha)`
   for zero world latency at the cost of overshoot on direction changes. Experiment.
4. **DL-rebuild elision** — render-only frames pay the full game-side DL build
   (`lvlRender`) + interpretation. If the census shows DL-build-bound levels, retained
   command replay buys the 300+ fps ceiling (the dyn double-buffer keeps frame N−1
   valid through frame N — `dyn.c:285` — but the sun-shadow capture ring
   (`gfx_pc.c:15701/24134`), native sky queue reset (`:24194`), and XLU deferred
   batches (`:24070`) need idempotence work first). Profile before starting.
5. **Bone-ordinal churn hardening** — per-context load-count tables to snap a whole
   context when its matrix count changes between ticks (LOD/attachment edges; the snap
   distance + generation-gap guards catch most of it today).
6. **PAL** — `PC_FIELD_COUNTS` is PAL-aware, but no PAL validation is planned; NTSC is
   the shipping mode.
7. **Menu/frontend render-rate animation** — menus tick at 60 Hz under uncap (fine);
   `g_MenuTimer` transitions could consume `g_pcUncapDtFields` for extra polish.

## §6. Known limitations (accepted, documented — restate these in user-facing docs)

- World entities display ≤1 tick (≤16.7 ms) behind the sim — the standard
  interpolation trade; the camera is exempt. Hit registration remains 60 Hz-quantized
  (as in every tick-based shooter).
- Muzzle flashes / one-tick effects hold for a full field across render frames
  (correct duration, discrete onset).
- Component-wise matrix lerp slightly denormalizes rotation mid-step for fast-spinning
  objects (sm64ex ships the same approach; the snap threshold bounds the damage).
- Multi-tick catch-up frames (`n ≥ 2` after stalls) lerp across the last captured pair
  — brief half-speed appearance during recovery, consistent with today's clamp.
- Cutscene cameras render at 60 Hz steps (deliberate: authored cuts must not smear).

---

## Recomp survey imports (2026-07-10) — amend the interpolators before executing F5

From `docs/RECOMP_LANDSCAPE_SURVEY_2026-07-10.md` (F14, F18). sm64ex's 60fps
patch interpolates at the same hook this plan chose (matrix submission); three
correctness patterns transfer directly and MUST be folded into the interpolators
here before this plan is executed:

1. **Timestamp validity gating** — every interpolated state stamps `prevTick`;
   blend only when `curTick == prevTick + 1`, else snap-and-restamp. Apply to the
   CHRPROP matrix lerp at `gfx_sp_matrix` (`gfx_pc.c:15914`, keyed by the existing
   `gfx_set_prop_context` identity) as a per-prop stamp. Self-heals teleports,
   spawns, level loads (~10 sites in the sm64ex patch).
2. **Explicit hard-cut suppression** — a `skip_interpolation` stamp API called
   from warp, intro camera cuts, death, and level transitions (add to the eye lerp).
3. **Per-frame alpha, never t=0.5** — interpolators take `alpha = elapsed/16.667ms`
   (sm64coopdx's `patch_interpolations(delta)` generalization), not a constant midpoint.

**Field evidence (both directions):** Zelda64Recomp *ships exactly this plan's
Path A* (fixed-rate sim + interpolated render at any framerate) with zero reported
gameplay divergence — only visual edge cases (uninterpolated HUD/reticle, >120fps
pacing jitter); import that edge-case list as F5 QA items. GoldenRecomp's
interpolation attempt *failed and was rolled back* (RT64's heuristic draw-call
matching can't handle GE's CPU-built matrices) — interpolation is a race a source
port wins by construction. Also: extend the 0-tick purity fuzz (FID-0033) to assert
widescreen/cull state purity on render-only frames (ties FID-0058 / survey F3).

**F18 fire-rate remediation** is tracked as **FID-0056** (survey F1) — contingent
on that oracle lane's adjudication; if N64-practical cadence is ruled truth, scale
the per-frame gun counters by `g_ClockTimer` behind `Input.FireRateAuthentic`,
A/B-verified against ares. The demo-cadence gate (FID-0057 / F2) is prior art for
"replay paths must honor recorded frame costs."
