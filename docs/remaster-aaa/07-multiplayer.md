# W7 — Multiplayer: Split-Screen to AAA + the Netplay Pathway

**Workstream of the MGB64 AAA Remaster program.** Branch base: `feat/metal-backend`.
Constitution: [docs/REMASTER_ROADMAP.md](../REMASTER_ROADMAP.md) — rails R1 (gameplay-invariant),
R2 (copyright-bulletproof), R3 (opt-in/default-identity) govern every task below.
Prior art this document supersedes-in-part: [docs/MULTIPLAYER_PLAN.md](../MULTIPLAYER_PLAN.md)
(split-screen, Phases 0–4 shipped), [docs/NETPLAY_PLAN.md](../NETPLAY_PLAN.md) +
[docs/NETPLAY_PLAN_REVIEW.md](../NETPLAY_PLAN_REVIEW.md) + [docs/NETPLAY_PORT_MAP.md](../NETPLAY_PORT_MAP.md)
(the 2026-06-25 puppet+event design and its adversarial correction record — kept as the
**fallback** architecture, see §4.9).

---

## 1. Executive summary

GoldenEye *is* its multiplayer. This workstream takes the shipped 2-player split-screen
(validated by `tools/mp_smoke.sh`, GL↔Metal parity 0.0% — `docs/METAL_BACKEND_PLAN.md:9`)
to a AAA couch experience — hardened 3/4-player, per-pane settings, pane-correct HUD/post-FX
on both backends, a 4-pane 60 fps performance budget, and real controller-assignment UX — and
then builds the netplay pathway on the architecture the engine has since *earned*:
**deterministic lockstep**. The old puppet+event netplay plan was written before the P0 rails
existed; its adversarial review found 8 load-bearing gaps. Three pieces of shipped
infrastructure change the calculus: the sim is replay-deterministic (RAMROM gate), the
whole-sim state hash exists and is cross-backend identical (`5c2983a3f0b7345f`,
`docs/METAL_BACKEND_PLAN.md:20`), and all stage-heap sim state lives inside one 8 MB arena
(`src/boss.c:244-254` → `src/memp.c:50-51`) — exactly the state inventory lockstep desync
detection and rollback need. Lockstep makes 6 of the 8 review gaps vanish by construction;
the two that remain (pane/player-count coupling, two-process harness) are scoped tasks here.

| # | Headline deliverable | Demo |
|---|---|---|
| 1 | Hardened 3/4-player split-screen, validated end-to-end into the scoreboard on Metal + GL | `tools/mp_smoke.sh --players 4 --timelimit 120` |
| 2 | AAA couch polish: per-pane FOV, pane-correct post-FX, modern 3P layout, controller claim UX | `build/ge007 --remaster --multiplayer --players 3` |
| 3 | 4-pane performance budget: ≥60 fps on Metal at 4K across all 11 MP stages | `tools/mp_perf_census.sh` (new) |
| 4 | Lockstep netplay: two-process loopback proof with per-tick input exchange + sim-hash desync oracle | `tools/net_smoke.sh` (new) |
| 5 | LAN 2-box play + rollback go/no-go assessment + internet beta (direct/join-code) | `--host` / `--connect IP` |

---

## 2. Current state (verified in code, this session)

### 2a. Split-screen — what ships today

- **CLI boot**: `--multiplayer --players N` accepts **2/3/4** (`src/platform/main_pc.c:660-666`);
  stage table (12 slugs = 11 concrete stages + `random`, `main_pc.c:225-238`), scenario table (all 8 scenarios incl. team
  variants, `main_pc.c:246-260`). Boot goes through `pc_apply_mp_selection()`
  (`src/game/initmenus.c:263-323`, invoked at `:566`) → `bossSetLoadedStage()` — the same exit
  the solo direct boot uses. Control style forced to 1.1 Honey for determinism (`initmenus.c:283-285`).
- **Player model**: `g_playerPointers[4]` allocated per player from **MEMPOOL_STAGE**
  (`src/game/player_2.c:154`); `getPlayerCount()` counts live slots (`player_2.c:92-102`) and
  has **~180 callsites** across `src/` (grep count: 179) — it is simultaneously the match-player
  count, the viewport count, the gfx budget index, and the tick-loop bound (see 2c).
- **Pane geometry** (classic N64 layout, all in 320×240 VI space, scaled by the backend):
  widths/heights from `bondviewGetCurrentPlayerViewportWidth/Height/Uly/Ulx`
  (`src/game/bondview.c:14825-14945`): 2P = two full-width 135-tall rows (ULY 10/121 NTSC);
  3P/4P = 159-wide columns (ULX 0/161 — `bondview.c:14850-14861`; only playernums 1 and 3
  get 161, so P3 in 3P is already bottom-left at ULX 0) in quadrants.
  **3P leaves the bottom-right quadrant black** (filled by `viSetupScreensForNumPlayers`,
  `src/fr.c:1806-1810`); dividers drawn at `fr.c:1797-1804`. Constants: `src/fr.h:44-132`.
- **Render loop**: `lvlRender` loops panes `for (i = 0; i < getPlayerCount(); i++)`
  (`src/game/lvl.c:1581-1592`), per pane sets view size/pos/FOV/aspect and renders the **full
  scene** — sky (`lvl.c:1673`), rooms via `bgLevelRender` (`lvl.c:1733`), props, HUD. So 4P =
  4× full scene cost per frame. Per-player viewport → `gSPViewport` + projection at
  `src/fr.c:1716-1731`; the fast3d layer honors them (`gfx_calc_and_set_viewport`,
  `src/platform/fast3d/gfx_pc.c:19158`; scissor `:19333`), and Metal implements both
  (`mtl_set_viewport`/`mtl_set_scissor`, `src/platform/fast3d/gfx_metal.mm:1595/1610`).
- **⚠ Sim work runs inside the per-pane loop.** `determing_type_of_object_and_detection()`,
  `chraiUpdateOnscreenPropCount()`, `chrpropUpdateAutoaimTarget()`, `chraiCheckUseHeldItems()`
  execute **once per pane per frame inside `lvlRender`'s player loop** (`lvl.c:1716-1719`),
  and `boss.c` runs `lvlViewMoveTick()` per player (`src/boss.c:626`). Consequence: you cannot
  "just render fewer panes" — skipping a pane skips sim. This is the single most important
  structural fact for both online-split-screen and any pane-count decoupling (§4.4).
- **Input**: all pads flow through `osContGetReadData` (`src/platform/stubs.c:5912`):
  keyboard/mouse merge onto `data[0]` (`:6345-6348`), pads 2–4 fill `data[k]` directly
  (`:6353-6358`, `pcFillPadFromController` `:5852`). **But mouse-look and gamepad right-stick
  aim bypass the pad entirely** — they are injected straight into player angles in
  `lvl.c:5786-5955` (mouse gated to player 0 at `:5804`; per-pad right stick at `:5807`).
  Note the shaping order: gamepad deadzone/curve fold into the integer `(mdx, mdy)`
  (`:5827-5870`), but **mouse/aim sensitivity, ADS multipliers, and invert-Y are applied as
  a float multiply at the injection itself** (`(f32)mdx * sens` into `vv_theta`/`vv_verta`,
  `:5911-5925`). Any input-replication scheme must capture this second seam *after* the
  sens multiply (see §4.7). Pad slots are assigned
  **in order of connection** (`src/platform/platform_sdl.c:74-96`); slot k = player k
  (`platform_sdl.c:2039+`); there is **no reassignment UX**.
- **Per-pane settings**: none. FOV is one global `g_pcFovY` for all players
  (`platform_sdl.c:193`, consumed via `bondviewGetNativeBaseFovY`, `bondview.c:14995-15007`).
- **HUD**: pane-aware rect squeeze shipped (commit `f1b36b8` — rects squeeze about the pane
  centre, not the window; fixes crosshair drift and overlay strips in 3/4P columns).
- **Gfx budget** scales by player count (`g_GfxSizesByPlayerCount`, `src/game/dyn.c:45`,
  consumed `:105-107`).
- **Validation**: `tools/mp_smoke.sh` — 2P Temple deathmatch boot, scripted P1 window,
  asserts the two half-frames are ≥2% dissimilar; `--players 2-4` accepted, `--timelimit`
  asserts the match timer elapses. Open items from `docs/MULTIPLAYER_PLAN.md`: scoreboard
  transition assertion (4b), 3/4P rigor (5a-ii), frontend nav for mixed input (3b),
  high-DPI scissor validation (0d). **Metal parity: split-screen frame diffed GL↔Metal at
  0.0% (2 px)** (`docs/METAL_BACKEND_PLAN.md:9`).

### 2b. Determinism infrastructure — what netplay inherits

- **RAMROM replay** (`src/game/ramromreplay.c`) replays recorded input+seed bit-identically;
  the tick clamp is **bypassed for replays** because playback records full speed-frames
  (`src/game/lvl.c:2076-2078`) — precedent that "ticks-per-frame is part of the input record."
- **Live tick source**: `g_ClockTimer = speedgraphframes` (wall-clock frames) in the live
  `lvlManageMpGame` (`lvl.c:2030` — the `NONMATCHING`+`PORT_FIXME_STUBS` branch is the one
  compiled: both defined at `CMakeLists.txt:290-292`; write at `:2068`, native `[1,4]` clamp
  `:2079-2082`); `g_GlobalTimerDelta = (f32)g_ClockTimer` (`:2088-2090`) scales all
  float-integrated motion. Timing-lock rail: only `lvl.c`+`front.c` may write it
  (`scripts/ci/check_timing_lock.sh` — the script labels itself "R2" in the *P0-gate*
  numbering; under this doc's rail numbering it belongs to R1 gameplay-invariance).
- **RNG**: one global `u64 g_randomSeed` (`src/random.c:25`); seeded from wall clock at boot,
  or fixed `0x12345678` under `g_deterministic` (`src/boss.c:399-411`).
- **Sim-state hash** (P0.2): FNV-1a with ASLR-canonicalized pointers over registered regions
  (`src/platform/sim_state_hash.c`); registry today = **8 MB pool + 2 timer globals only**
  (`src/platform/sim_state_hash_registry.c:24-39`); raw-pool dump hook for divergence diffing
  (`registry.c:49-58`); emitted via `--sim-state-hash-out`. Proven **cross-backend identical**
  GL vs Metal (`5c2983a3f0b7345f`, `docs/METAL_BACKEND_PLAN.md:20`).
- **The arena**: `s_pcPool` is a single 8 MB malloc (`src/boss.c:244-254`) handed to memp as
  `MEMPOOL_TOTAL` (`src/memp.c:50-51`) — **every `mempAllocBytesInBank` allocation (players,
  chrs, props' stage heap) lives inside it**. File-scope sim `.bss` (e.g.
  `pos_data_entry[600]`, `src/game/chrprop.c:152`; `g_randomSeed`) lives **outside** it —
  the registry is not yet a complete restore inventory (§4.7).
- **Frame seam** (`src/boss.c`): `joyPoll` `:577` → `joyConsumeSamplesWrapper` →
  `lvlManageMpGame()` `:611` → per-player `lvlViewMoveTick()` `:626` → `lvlRender` `:638`.
  Single-threaded.
- **Netcode**: none exists (`src/platform/net/` absent; no enet; `CMakeLists.txt:306` glob is
  non-recursive over `src/platform/*.c`).

### 2c. The old netplay plan and its review — status

`docs/NETPLAY_PLAN.md` (PD-style puppet+event) + `docs/NETPLAY_PLAN_REVIEW.md` (13-agent
adversarial review). The review's verdict: architecture buildable but **8 load-bearing gaps**:
(1) in-process loopback can't diverge (sim = file-scope singletons); (2) host-side hitscan
replay refuted (auto-aim latch + 4×`RANDOMFRAC` spread + FOV + implicit firer —
`bullet_path_from_screen_center`, now `gun.c:29122`, draws at `:29159-29164`; the review's
table cites the pre-drift `:28756/:28789-28795`); (3) rollback budget understated; (4) LE wire primitive wrong;
(5) 6 of 8 MP scenarios carry unreplicated scoring state (`mp_watch.c:791-842` dispatch);
(6) `getPlayerCount()` couples match size to viewports/budget/dividers; (7) lifecycle
(pause/disconnect/results/names) design-absent; (8) N5 packet memory-safety absent.
The review's corrected-anchor table (§5 there) remains the authoritative *symbol* map for any
game-code hook — but its line numbers predate this branch (see the `gun.c` drift above), so
re-grep the symbol before hooking. §4.9 below scores all 8 against the lockstep architecture.

---

## 3. Target state — the AAA bar

A reviewer sitting on the couch, and later two reviewers on two Macs, observe:

1. **Couch (4P)**: four pads connected in any order; a pre-match overlay says "press FIRE to
   claim a pane"; each player claims their quadrant. Match runs at a locked ≥60 fps at 4K on
   Apple Silicon (Metal), every pane a full independent camera with correct crosshair, hit
   markers, per-pane FOV if configured, and post-FX (grade/FXAA/vignette) that respects pane
   boundaries — no vignette dimming the middle of the screen where four panes meet, no bloom
   bleeding across dividers. 3-player mode optionally uses the full bottom half for P3 instead
   of a dead black quadrant. A complete match ends on the real scoreboard, advances cleanly.
2. **Netplay (v1 = LAN, 2 boxes)**: `--host` on one machine, `--connect <ip>` on the other;
   both load the same stage; play feels local at LAN latencies (2-tick input delay ≈ 33 ms);
   every mode — including Golden Gun, Flag Tag, YOLT, team — scores correctly **because both
   machines run the identical full simulation**; a desync (should one occur) is detected
   within 60 ticks by the sim-hash oracle, reported on screen, and produces a divergence dump
   pair for offline diffing. Pausing pauses both. A dropped peer's player stands idle
   (neutral input); the match continues and ends on the scoreboard.
3. **Faithfulness**: with no `Net.*`/`GE007_NETPLAY*` flags and stock pane settings, the build
   is byte-identical to today's faithful port (R3), all P0 gates green.

---

## 4. Technical design

### Track A — split-screen polish

#### 4.1 Per-pane FOV & settings

Today `bondviewGetNativeBaseFovY()` clamps one global (`bondview.c:14995-15007`). Add a
per-player override array in `platform_sdl.c`:

```c
/* platform_sdl.c — settings registration (near Video.FovY, :1694) */
f32 g_pcFovYPerPlayer[4] = { 0.f, 0.f, 0.f, 0.f };   /* 0 = inherit Video.FovY */
settingsRegisterFloat("Video.FovY.P2", &g_pcFovYPerPlayer[1], 0.0f, 0.0f, 105.0f, ...);
/* ... P3, P4. P1 keeps Video.FovY. */
```

```c
/* bondview.c — resolver becomes player-aware */
static f32 bondviewGetNativeBaseFovY(void) {
    extern f32 g_pcFovY, g_pcFovYPerPlayer[4];
    f32 v = g_pcFovYPerPlayer[get_cur_playernum()];
    if (v < 45.0f) v = g_pcFovY;              /* 0/unset → inherit */
    return v < 45.0f ? 45.0f : (v > 105.0f ? 105.0f : v);
}
```

FOV feeds targeting (`currentPlayerSetPerspective` → `c_perspaspect`/`c_scalex`/`c_scaley`,
`bondview.c:1692-1707`; the review's `:1147` anchor has drifted) — this is the same accepted,
user-visible knob as the shipped global `Video.FovY` (default 50); per-player extension is the
same rail class: opt-in, identity default (unset = inherit = today's behavior). **Under
netplay, per-player FOV becomes match-replicated config** (§4.6) so all sims agree.

#### 4.2 Output-pass FX pane semantics (GLSL + MSL, both generators)

The output VI filter (Engine A: FXAA/CAS/bloom/grade/tonemap/gamma/vignette/dither) runs
**fullscreen** once per frame in both backends. Two pane bugs to fix, identically in
`gfx_opengl.c` (GLSL) and `gfx_metal.mm` (MSL — the 2-pass output-filter generator at
`gfx_metal.mm:1125-1360`; the existing vignette term is at `:1313`):

- **Vignette**: computed from window UV → darkens the window edges, i.e. the *outside* of the
  4-pane grid and nothing at pane centers. Fix: pass a per-frame `uPaneGrid` uniform
  (`vec4(cols, rows, unused, unused)`; 1×1 in SP) and fold UV per pane:
  `vec2 paneUv = fract(uv * uPaneGrid.xy);` then evaluate vignette on `paneUv`. Gate:
  `Video.VignettePerPane` (default **off** = today's behavior, identity).
- **FXAA/CAS/bloom divider bleed**: the pass samples across the 2-px divider. Measure first
  (T-task): expected ≤2 px halo along dividers. If visible, clamp sample neighborhoods to the
  pane via the same `uPaneGrid` (branchless `min/max` on sample coords), gated by its own flag
  `Video.PaneClampFX` (default **off** = today's behavior). Only ship if the A/B shows a
  visible artifact; otherwise document as accepted.

SSAO depth linearization is safe in split-screen as-is: all panes share `znear/zfar/fovy`
defaults so `A=P[2][2], B=P[3][2]` are pane-invariant; per-pane FOV (4.1) does not perturb
`P[2][2]/P[3][2]` either (they depend only on near/far).

#### 4.3 Modern 3-player layout (opt-in)

Classic 3P wastes a quadrant (`fr.c:1806-1810`). Add `Video.ThreePlayerWideBottom` (default
off): P1/P2 keep the top quadrants; P3 gets the full-width bottom half. Mechanically this is
pane-geometry only — all four functions live in one place:

- `bondviewGetCurrentPlayerViewportWidth` (`bondview.c:14825`): for playercount==3 &&
  playernum==2 return `SCREEN_WIDTH` (320) instead of `VIEWPORT_WIDTH_4P` (159).
- `get_curplayer_viewport_ulx` (`:14850`): **no change needed** — it returns 161 (`0xa1`)
  only for playernums 1 and 3, so playernum 2 already gets ULX 0; verify in the A/B.
- Height/ULY (`:14869/:14915`): unchanged (bottom half already 135 tall at ULY 121 NTSC).
- `viSetupScreensForNumPlayers` (`fr.c:1800-1810`): suppress the vertical divider below
  mid-line and the black-quadrant fill when the flag is on.

Aspect follows automatically — `lvl.c:1592` passes `g_CurrentPlayer->aspect`, computed from
the pane dims (`bondview.c:15095-15099`). Rails: pure presentation (pane rects feed the
renderer; sim reads none of them), flag default-off, A/B via screenshot.

#### 4.4 The pane/player-count decoupling — `getLocalViewportCount()`

Required by netplay (a 2-box 4-player match must render 1–2 panes per box while simulating 4
players) and useful standalone (spectator/HUD work). This is the review's §4.4 blocker, now
sharpened by the `lvl.c:1716-1719` finding: **sim functions execute inside the pane loop**, so
the split is *not* "loop over fewer players" — it is a two-loop refactor:

```c
/* new: src/game/player_2.c */
s32 getLocalViewportCount(void);   /* == getPlayerCount() unless netplay/spectate */
s32 getNthLocalPlayer(s32 i);      /* pane i -> player number */
```

- `lvl.c:1581-1592` pane loop: split into (a) a **sim loop over ALL players** running
  `lvl.c:1716-1719`'s four sim calls + reload handling `:1721-1725` exactly once per player
  per frame in shuffled order (preserving today's call order/counts when local==match), and
  (b) a **draw loop over local viewports** doing viewport/FOV/sky/`bgLevelRender`/HUD.
  **Trap for the unwary**: `bgRoomVisibilityRelated()` (`lvl.c:1680`) and the `room_rendered`
  marking it drives are NOT pure presentation — prop/auto-aim visibility reads
  `getROOMID_isRendered` (`src/game/chr.c:5205`). The per-player visibility pass must stay in
  the **sim loop** (or its `room_rendered` writes must be reproduced there) or the hash test
  below will fail for every non-rendered pane.
- `boss.c:626` `lvlViewMoveTick` loop: **stays on `getPlayerCount()`** (it is sim: movement).
- `fr.c:1779-1806` dividers + `dyn.c:105-107` gfx budget: switch to `getLocalViewportCount()`.
- Audit the remaining ~180 `getPlayerCount()` callsites with the review's classification
  (match-semantics keep / local-presentation switch); the review's §4.4 list is the seed.

**Acceptance is mechanical thanks to P0.2**: run the same deterministic 4P match with
`GE007_LOCAL_PANES=4` vs `=1` on one machine — `--sim-state-hash-out` must be **identical**.
That test is impossible to cheat and directly proves the refactor moved zero sim work.
Default identity: with the env unset, `getLocalViewportCount()==getPlayerCount()` and the
generated DL is byte-identical (screenshot-hash A/B).

#### 4.5 4-pane performance + controller UX

- **Perf**: each pane is a full scene render (`lvl.c:1733` inside the loop) → 4P ≈ 4× draw
  cost, plus Metal's per-batch XLU blit-snapshot cost multiplies per pane. Build
  `tools/mp_perf_census.sh` (clone of the perf-census pattern from `docs/PERFORMANCE_PLAN.md`)
  sweeping all 11 concrete MP stages × {2,3,4} players × {GL, Metal} at 4K, budget ≥60 fps. MP stages
  are small subsets of solo levels (solo runs 101–189 fps), so the expectation is pass;
  the lane exists to catch regressions and the XLU multiplier.
- **Controller claim UX**: (a) `Input.PadOrder` setting + `--pad-map 2,0,1,3` CLI to permute
  SDL slot→player (one indirection table in `platform_sdl.c` accessors `:2039+`); (b) a
  pre-match "press FIRE to claim P#" overlay: during the MP lobby/briefing, first FIRE press
  on an unclaimed pad binds it to the next open player slot (writes the same permutation
  table — frontend-only, no sim contact); (c) hot-unplug mid-match: pad reads become neutral
  (already the case — absent pads zeroed at `stubs.c:5924`), plus a HUD banner.

### Track B — netplay: deterministic lockstep

#### 4.6 The architecture decision (reversal, justified)

The 2026-06-25 plan chose puppet+event *because* lockstep's preconditions looked unreachable.
They have since been built and measured:

| Precondition | 2026-06-25 status | Today |
|---|---|---|
| Sim determinism, same binary | assumed shaky | **Proven**: RAMROM replay gate (P0.2) runs OFF-vs-ON to identical whole-arena hashes; deterministic run-to-run |
| Cross-config invariance | unknown | **Proven cross-renderer**: GL and Metal produce the identical sim hash `5c2983a3f0b7345f` (`docs/METAL_BACKEND_PLAN.md:20`) |
| State inventory for desync/rollback | "~85 KB, wrong" (review §3.3) | **One 8 MB arena** holds all stage-heap state (`boss.c:244-254` → `memp.c:50-51`) + enumerable `.bss` globals |
| Tick-count replication precedent | none | RAMROM records speed-frames; clamp bypassed on replay (`lvl.c:2076-2078`) |

**Why lockstep wins on correctness surface**: every peer runs the complete simulation on
identical inputs — so auto-aim, RNG spread, all 8 scenarios' scoring, mines/projectiles/
pickups/doors/explosions, pause, and the results screen replicate **by construction**. The
puppet model had to wrap each of those (the review's "hidden bulk"); lockstep's wire is
~20 bytes/player/tick of inputs, period.

**What lockstep costs, honestly**: (a) *cross-architecture determinism is unproven* — the
review's strongest standing objection (793 transcendental calls across 31 sim files;
`sinf/cosf` differ between libms). v1 therefore targets **same-build, same-arch** peers
(macOS/ARM ↔ macOS/ARM), enforced at handshake; cross-arch is a measured gate (W7.E5.T5),
not an assumption. (b) *Input latency* — delay-based lockstep adds the delay to local
actions including aim; at LAN (delay 2 ticks ≈ 33 ms) this is acceptable; internet play is
gated on the rollback assessment (§4.7). (c) *No late join* — v1 requires all peers at stage
start (state-transfer join is a stretch goal: the state *is* one arena, compressible and
sendable). (d) *Cheating* — full-information client; accepted for a preservation/party port
exactly as the old plan accepted client-auth movement.

#### 4.7 Lockstep core design

**Module layout** (new, `src/platform/net/` — add explicitly to both targets;
`CMakeLists.txt:306` glob is non-recursive):

```
src/platform/net/net.c        — enet host/peer, session FSM, frame pump
src/platform/net/netbuf.[ch]  — own LE codec (per review §3.4 — NOT byteswap.h)
src/platform/net/netinput.c   — input ring, delay scheduling, agreement
src/platform/net/netmsg.[ch]  — message (de)serialization + validation
lib/enet/                     — vendored enet (MIT; update THIRD_PARTY.md +
                                tools/check_third_party_notices.py allowlist)
```

**The tick record.** One mainloop iteration = one lockstep tick. The inputs the sim consumes
per tick are exactly: 4×`OSContPad` (from `osContGetReadData`, `stubs.c:5912`), per-player
look deltas (the `lvl.c:5786-5955` direct-injection seam), and — host-only — the frame's
`speedgraphframes` value (`lvl.c:2068`). Wire format, fixed size, LE:

```c
typedef struct NetPlayerInput {  /* 16 B per player per tick */
    u16 button;                  /* OSContPad.button */
    s8  stick_x, stick_y;        /* OSContPad sticks */
    u32 look_dx_bits, look_dy_bits; /* FINAL look deltas as raw IEEE-754 f32 bit patterns:
                                    the (f32)mdx*sens / (f32)mdy*sens*invert products the
                                    engine adds to vv_theta/vv_verta (lvl.c:5911-5925) —
                                    the full local chain (deadzone/curve/sens/ADS/invert)
                                    is applied on the ORIGINATING peer only; the wire
                                    carries bits, so injection is bit-exact everywhere */
    u8  flags;                   /* reserved: quit-request, ready */
    u8  _pad[3];
} NetPlayerInput;

typedef struct NetTickMsg {      /* client→host: own players; host→all: all 4 + ticks */
    u32 tick;                    /* lockstep tick index */
    u8  clock_ticks;             /* HOST ONLY: g_ClockTimer for this tick, clamped [0,4]
                                    (0 = paused/locked frame, lvl.c:2058-2064) */
    u8  player_mask;             /* which slots this msg carries */
    NetPlayerInput in[4];
} NetTickMsg;
```

Post-shaping look deltas are deliberate — but note the shaping order (verified in code):
gamepad deadzone/curve fold into the integer `(mdx, mdy)` (`lvl.c:5827-5870`), while
**mouse/aim sensitivity, ADS multipliers, and invert-Y are float multiplies applied at the
injection itself** (`lvl.c:5911-5925`). So the wire must carry the **post-sens float
products** (as raw bit patterns — no cross-peer float math, hence bit-exact), not the
pre-sens ints. All knobs stay **local** settings; peers with different knobs still feed
identical bits to the sim. (`insightaimmode` sens selection at `lvl.c:5827-5829`/`:5874-5876`
reads sim state on the originating peer; the shipped product already includes it. The tank
branch multiplies the same `mdx*sens` product by π/180, `lvl.c:5911-5921`, so one seam
covers both.)

**Injection seams** (all `#ifdef`-free, guarded by `g_netActive`):

1. `osContGetReadData` (`stubs.c:5912`): when netplay is active, after the normal local fill,
   *record* local players' pads into the outgoing tick and *overwrite* `data[0..3]` with the
   agreed tick's inputs (playernum→local-pad map from §4.5's permutation table).
2. Look seam: factor the final post-sens look products (the `(f32)mdx * sens` /
   `(f32)mdy * sens * invert` terms at `lvl.c:5911-5925`, shared by the tank-turret and
   normal branches) into `pcResolveLookDelta(playernum, f32 *dx, f32 *dy)`; netplay mode
   records local values (as raw f32 bit patterns) and returns the agreed tick's values for
   every player.
3. Tick agreement — **timing-lock-compliant**: `lvl.c:2068` becomes
   `g_ClockTimer = g_netActive ? netAgreedClockTicks() : speedgraphframes;` — the *write*
   stays in `lvl.c`, so `scripts/ci/check_timing_lock.sh` still passes with its two owners.
   Host computes ticks from its own clock (still `[1,4]`-clamped, `:2079-2082`) and stamps
   them into `NetTickMsg.clock_ticks`. Pause works untouched: `g_ControlsLockedFlag`/
   `checkGamePaused()` zeroing (`lvl.c:2058-2064`) is sim state driven by replicated inputs.
4. Frame pump in `boss.c`: `netStartFrame()` immediately before `joyPoll` (`boss.c:577`) —
   pumps enet, sends local inputs for tick `T+delay`, and **stalls** (bounded spin with
   `waitForNextFrame`) until all peers' inputs for tick `T` are present; `netEndFrame()`
   after `lvlRender` (`boss.c:638`).

**RNG + match start.** Host broadcasts the match config `{mp_stage, scenario, num_players,
timelimit, weapon set, team assignment, handicaps, per-player FOV}`; every peer calls the
*same* `pc_apply_mp_selection()` (`initmenus.c:263`) with it. At the post-load barrier
(all peers ack "stage loaded"), host sends its `g_randomSeed`; peers `randomSetSeed()` it —
reusing the `g_deterministic` precedent (`boss.c:399-411`). Tick 0 begins.

**Desync oracle.** Every 60 ticks each peer computes `sim_state_hash_compute()` over the
**extended registry** (below) and piggybacks the 8-byte hash on its input msg. Host compares;
mismatch → on-screen banner + both sides write `GE007_SIM_HASH_DUMP` arena dumps
(`sim_state_hash_registry.c:49-58`) + the tick number, then the match ends cleanly. Cost:
FNV-1a over 8 MB ≈ 3–6 ms — acceptable at 1 Hz; do it on the frame *after* present.

**Registry completion (prerequisite, also serves rollback).** Today's registry is pool + 2
timers (`sim_state_hash_registry.c:24-39`) — incomplete for both divergence detection outside
the arena and restore. Task: generate the sim `.data`/`.bss` symbol table at build time —
`nm -m` over the `src/game/*.o` + sim `src/*.o` objects (the same object set
`check_sim_render_separation.sh` already enumerates), emit a C table
`{name, &sym, sizeof}` for non-render symbols, with an explicit denylist for legitimately
varying render bookkeeping (the P0.2 comment `registry.c:20-23` names the criterion). The
existing `ctest -R sim_state_hash` unit stays green; the invariance gate re-baselines.

**Lifecycle.**
- *Join*: lobby-only (handshake: protocol version, build hash, arch triple, ROM region;
  reject mismatches — same-build enforcement). No late join v1.
- *Leave/drop*: peer timeout → its players' inputs become neutral zeros from the next
  unacked tick; HUD banner; match continues; scoreboard preserved. The results-screen gate
  `menu_count == player_count` (`mp_watch.c:651-662`) replicates via inputs, but a *dropped*
  peer can never press — so when any slot is in disconnected state, host substitutes a
  synthetic "ready" input after a 10 s timeout (input-stream substitution, not a game-code
  change).
- *Host leave*: broadcast end-of-session; everyone returns to lobby (no migration v1).
- *Pause*: replicated by construction (input-driven), verify in the two-process lane.

**Security (LAN v1 → internet).** The wire is fixed-size input records — validate length,
tick monotonicity, `player_mask` ⊆ handshake-assigned slots, `clock_ticks ∈ [0,4]`. No
attacker-controlled indices touch game arrays (contrast: the old event model's `syncid`
deref, review §4.7). Internet beta still adds per-peer rate caps + a fuzz pass over
`netmsg` decoding before any join-code exposure.

#### 4.8 Rollback assessment (gate, not commitment)

Delay-based lockstep is the ship vehicle for LAN. Rollback (GGPO-style: apply local input
immediately, re-simulate on late remote input) is what makes **internet** feel good. The
assessment is now cheap to run because save/restore is concrete:

- **Snapshot** = `memcpy` of the 8 MB arena (same process, same base → interior pointers
  remain valid) + the completed globals registry (§4.7). Expected ~0.5–1.5 ms on M3-class;
  ring of 8 snapshots = 64 MB RAM. Restore = the reverse memcpy.
- **Correctness gate**: deterministic run to tick N, snapshot, run to N+k, restore, re-run to
  N+k → `sim_state_hash` must equal the straight run's. Any mismatch enumerates a missed
  region — the registry hardens as a side effect.
- **Re-sim cost**: the honest unknown. One rollback of depth d re-runs d sim ticks in one
  frame, and sim work is entangled with the DL build (`lvl.c:1716-1719` inside `lvlRender`)
  — a "tick without draw" mode must reuse §4.4's two-loop split with the draw loop skipped.
  Instrument: sim-only tick time across MP stages; rollback of depth 4 must fit < 8 ms.
- **Go/no-go**: GO if snapshot+restore correctness holds and depth-4 re-sim < 8 ms → build
  rollback for internet beta. NO-GO → internet beta ships delay-based with higher delay
  (4–6 ticks) and the option of region-pooled matchmaking; document and stop.

#### 4.9 The 8 review gaps, scored under lockstep

| # | Review gap (NETPLAY_PLAN_REVIEW.md) | Under lockstep |
|---|---|---|
| 1 | §3.2 in-process loopback can't diverge | **Still true — inherited.** Two-OS-process `net_smoke.sh` over 127.0.0.1 is the correctness lane (W7.E4.T4) |
| 2 | §3.1 host can't replay hitscan (auto-aim+RNG+FOV) | **Vanishes.** Both sims run the same shot from the same inputs+seed; FOV replicated in match config |
| 3 | §4.3 6/8 scenarios unreplicated | **Vanishes.** Full sim = full scenario state on every peer; all 8 scenarios in the acceptance matrix |
| 4 | §4.4 `getPlayerCount()` couples panes to match size | **Still true — scoped** as W7.E3.T1 (shared with split-screen), with the pane-invariance hash test |
| 5 | §4.6 lifecycle absent (pause/results/disconnect) | **Mostly vanishes** (input-replicated); residual = drop-substitution + host-leave, designed in §4.7 |
| 6 | §4.7 packet memory-safety | **Shrinks** to fixed-size input validation; fuzz lane still gates internet |
| 7 | §3.3 rollback budget understated | **Corrected & bounded**: arena memcpy + registry completion; explicit go/no-go gate (§4.8) |
| 8 | §3.4 `byteswap.h` is the wrong wire primitive | **Inherited fix**: own LE codec in `netbuf.[ch]` |

The puppet+event plan + port map remain on file as the **fallback** if W7.E5.T5 measures
cross-arch divergence AND cross-arch play is later prioritized — that model is arch-agnostic
by design.

---

## 5. Work breakdown

Estimates in **junior-engineer-days** (jd). Rails column: R1/R2/R3 compliance note.
Build/validation command patterns per roadmap §7 (see §8 for full invocations).

### W7.E1 — Split-screen 3/4P correctness (10 jd)

| ID | Task | Files | Steps / acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|
| W7.E1.T1 | 3P/4P smoke matrix | `tools/mp_smoke.sh` | Extend the half-frame dissimilarity assert to quadrant-wise (crop 4 quadrants; assert pairwise ≥2% delta among *active* panes; 3P: assert bottom-right quadrant ≈100% black in classic mode). Run `--players 3` and `--players 4` × {temple, complex, caves} × {GL, `GE007_RENDERER=metal`}. **Accept**: all 12 combos pass `--max-crashes 0` + dissimilarity; `tools/audit_render_trace.py` clean. | 3 | — | Validation-only; no flags; no assets |
| W7.E1.T2 | End-of-round scoreboard assertion | `tools/mp_smoke.sh`, `src/game/mp_watch.c` (trace hook only) | Add a `port_trace` event when the results screen becomes active (mission state transition consumed by `mp_watch.c`); `--timelimit 60` asserts the event fires and no crash through +300 frames. **Accept**: `tools/mp_smoke.sh --players 2 --timelimit 60` green; solo lanes untouched (`tools/playability_smoke.sh --all`). | 2 | — | Trace emit only (no sim write); default-on tracing is existing pattern |
| W7.E1.T3 | High-DPI pane scissor validation | none (validation) | On a 2× backing-scale window, screenshot 4P; assert divider/scissor alignment (no pane bleeding) via `compare_screenshots.py` masked ROIs on the divider lines; GL + Metal. **Accept**: 0 bleed px in divider ROIs. | 1 | W7.E1.T1 | Validation-only |
| W7.E1.T4 | Frontend nav for mixed input (plan 3b) | `src/game/front.c` (menu input routing), `src/platform/stubs.c` | Route char/control-style menu cursors per player pad (menus already per-slot on N64; verify pad k drives cursor k, fix any `data[0]`-only reads in the frontend path). **Accept**: scripted per-pad menu navigation reaches a 4P match from the menu chain (extend mp_smoke with `--menu-drive`); direct-boot path byte-identical. | 4 | — | R3: behavior change only in MP menus with ≥2 pads; solo path untouched (screenshot-hash A/B) |

### W7.E2 — Split-screen AAA polish (20 jd)

| ID | Task | Files | Steps / acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|
| W7.E2.T1 | Per-pane FOV settings | `src/platform/platform_sdl.c`, `src/game/bondview.c:14995` | Implement §4.1. **Accept**: `--config-override Video.FovY.P2=70` visibly widens only P2's pane (screenshot quadrant A/B); all keys unset → byte-identical frame vs baseline. | 2 | — | R3: `Video.FovY.P2..P4`, unset=inherit=identity. R1: same class as shipped Video.FovY (user setting feeding sim targeting, opt-in) |
| W7.E2.T2 | Output-FX pane semantics | `src/platform/fast3d/gfx_opengl.c` (GLSL gen), `src/platform/fast3d/gfx_metal.mm` (MSL gen) | Implement §4.2 `uPaneGrid` + per-pane vignette; measure divider bleed first, clamp only if visible. **Both generators, structurally parallel.** **Accept**: 4P `--remaster` screenshot shows vignette per pane with `Video.VignettePerPane=1`; flag off → byte-identical; GL↔Metal parity ≤0.5% on the MP frame; `sim_invariance_gate.sh` green. | 4 | — | R3: `Video.VignettePerPane` + `Video.PaneClampFX`, both default off. R1: output pass reads scene color only |
| W7.E2.T3 | Modern 3P wide-bottom layout | `src/game/bondview.c:14825-14945`, `src/fr.c:1764-1815` | Implement §4.3. **Accept**: `Video.ThreePlayerWideBottom=1 --players 3` screenshot: P3 spans full bottom width, no black quadrant, dividers correct; flag off → byte-identical 3P frame; HUD rects follow the pane (f1b36b8 squeeze verified in the wide pane). | 5 | W7.E1.T1 | R3 flag default off. R1: pane rects are presentation; aspect feeds sim same as any pane change — covered by hash gate run 3P flag-off vs on… **expect hash diff (FOV/aspect are player state)** → acceptance uses gameplay-field `compare_state.py` instead, like RenderScale (`sim_invariance_gate.sh` header note) |
| W7.E2.T4 | Controller claim UX + pad map | `src/platform/platform_sdl.c:74-96,2039+`, `src/platform/main_pc.c`, frontend overlay | §4.5(b): permutation table + `Input.PadOrder`/`--pad-map`; claim overlay in MP lobby; hot-unplug banner. **Accept**: `--pad-map 1,0` swaps which pad drives P1/P2 (mp_smoke scripted per-pad input proves it); unset → byte-identical; unplug mid-match → no crash (simulated `CONTROLLERDEVICEREMOVED`). | 6 | — | R3: `Input.PadOrder` default identity permutation. R2: n/a |
| W7.E2.T5 | 4P perf census (Metal+GL) | new `tools/mp_perf_census.sh` | §4.5(a): sweep the 11 concrete MP stages × {2,3,4}P × 2 backends at 4K, log fps + `rooms_drawn`/`tris` from `port_trace`. **Accept**: report table; ≥60 fps at 4P on Metal for all stages or a ranked fix list (XLU-snapshot-per-pane is the prime suspect). | 3 | W7.E1.T1 | Validation-only; artifacts local (ROM-derived captures never committed — R2) |

### W7.E3 — Shared foundations (21 jd) — *gates both tracks*

| ID | Task | Files | Steps / acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|
| W7.E3.T1 | `getLocalViewportCount()` + sim/draw pane split | `src/game/player_2.c`, `src/game/lvl.c:1581-1733`, `src/boss.c:626`, `src/fr.c:1779-1806`, `src/game/dyn.c:105-107` + callsite audit | Implement §4.4 two-loop refactor + classify the ~180 callsites (review §4.4 list is the seed); keep `bgRoomVisibilityRelated()`/`room_rendered` marking in the per-player **sim** loop (§4.4 trap — `chr.c:5205` auto-aim visibility reads it). **Accept**: (1) `GE007_LOCAL_PANES` unset → screenshot + sim hash byte-identical to baseline in SP and 2P/4P; (2) the killer test: 4P deterministic match, `GE007_LOCAL_PANES=4` vs `=1`, `--sim-state-hash-out` **identical**; (3) all smokes green. | 10 | — | R3: env `GE007_LOCAL_PANES` (dev) — netplay sets it internally; identity when unset. R1: the hash test *is* the rail |
| W7.E3.T2 | Sim-hash registry completion | `src/platform/sim_state_hash_registry.c`, new build-time generator script, `CMakeLists.txt` | §4.7: nm-generated `.data`/`.bss` table over the sim object set (reuse `check_sim_render_separation.sh`'s object enumeration), denylist for render bookkeeping, wire into `simHashRegistryBuild` (bump `SIM_HASH_MAX_REGIONS` as needed or emit a dedicated table). **Accept**: `ctest -R sim_state_hash` green; `tools/sim_invariance_gate.sh` green with the extended registry (re-baseline); registry lists ≥ the review §3.3 set (`pos_data_entry`, `g_playerPlayerData`, `g_randomSeed`, chr/prop globals). | 6 | — | R1 rail hardening itself; ROM-free CI unit stays |
| W7.E3.T3 | Nondeterminism audit + two-run divergence lane | `src/` audit, new `tools/determinism_soak.sh` | Grep+review sim TUs for wall-clock reads (`osGetCount` outside boss seed), pointer-value-dependent logic (sorts/compares on addresses), uninitialized reads (ASan already covers); lane: run the same deterministic MP match twice in two processes, compare final sim hash + per-60-tick hash traces. **Accept**: 2P and 4P deterministic matches hash-identical across two process runs, 10× repetitions; findings fixed or documented. | 5 | W7.E3.T2 | Validation + surgical fixes; any sim fix needs RAMROM golden validation (roadmap §7 rule 4) |

### W7.E4 — Lockstep netplay to LAN (29 jd)

| ID | Task | Files | Steps / acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|
| W7.E4.T1 | Transport + session FSM + build plumbing | new `src/platform/net/*`, `lib/enet/`, `CMakeLists.txt:306+`, `THIRD_PARTY.md`, `tools/check_third_party_notices.py` | Vendor enet (MIT); LE `netbuf`; connect/handshake FSM (version+build-hash+arch+region); add sources to **both** targets explicitly. **Accept**: two processes handshake over 127.0.0.1, mismatched build hash rejected with a clear message; ctest green; notices gate green. | 5 | — | R2: enet MIT + NOTICE entry. R3: all inert without `--host/--connect` |
| W7.E4.T2 | Input seam + tick agreement | `src/platform/stubs.c:5912+`, `src/game/lvl.c:2068,5786-5955`, `src/boss.c:577/638`, `src/platform/net/netinput.c` | Implement §4.7 seams 1–4 (`NetPlayerInput` record/inject, `pcResolveLookDelta`, `netAgreedClockTicks()` write kept in lvl.c, frame pump + stall). **Accept**: `check_timing_lock.sh` passes; netplay-off build byte-identical (screenshot hash + `cmp` on `--sim-state-hash-out` vs baseline) **and** a RAMROM replay hash matches baseline (roadmap §7 rule 4 — the seam touches the sim input path); loopback single-peer (host alone) plays normally. | 6 | W7.E4.T1 | R1: timing-lock gate explicitly preserved (write stays in lvl.c) + RAMROM golden run. R3: `g_netActive` only via CLI/`Net.*` |
| W7.E4.T3 | Match config + seed sync + load barrier | `src/platform/net/netmsg.c`, `src/game/initmenus.c:263` | Host broadcasts config; peers call `pc_apply_mp_selection()`; post-load ack barrier; seed message → `randomSetSeed` (`boss.c:399-411` pattern); per-player FOV in config (§4.1/§4.6). **Accept**: two processes load the same stage+scenario+weapon set from one host selection; logged configs identical. | 4 | W7.E4.T1 | R3; config replicates existing public enums only (R2: no ROM data on the wire) |
| W7.E4.T4 | Two-process `net_smoke.sh` + desync oracle | new `tools/net_smoke.sh`, `src/platform/net/net.c` | Launch host+client processes on 127.0.0.1, per-process scripted local input (`GE007_AUTO_*` drives each box's local player), run 600 ticks; exchange per-60-tick hashes; assert **all hash pairs equal** and both exit clean. On mismatch dump arenas (`GE007_SIM_HASH_DUMP`) and diff offsets. **Accept**: lane green 10× consecutively for 2P deathmatch; seeded fault (skip one input) trips the oracle. | 5 | W7.E4.T2, W7.E4.T3, W7.E3.T2/T3 | The R1-style proof for netplay; ROM needed → local lane + ROM-free unit tests in CI |
| W7.E4.T5 | LAN 2-box playable + panes | `src/platform/net/*`, uses W7.E3.T1 | Real LAN: input-delay tuning (`Net.InputDelayTicks`, default 2), stall/timeout handling, disconnect→neutral-input substitution + HUD banner, each box renders only local panes (`getLocalViewportCount`). **Accept**: manual 2-box LAN match to scoreboard, `--max-crashes 0` traces on both; net_smoke extended with injected 30 ms delay/jitter (toxiproxy or `Net.SimulateLatencyMs`) stays hash-equal. | 6 | W7.E4.T4, W7.E3.T1 | R3: `Net.*` keys default-off |
| W7.E4.T6 | Lifecycle verification (all 8 scenarios) | `tools/net_smoke.sh` matrix | Scenario matrix {normal, ltk, yolt, tld, goldengun, 2v2, 3v1, 2v1} × pause/unpause mid-match × results-screen advance × peer-drop-and-continue. **Accept**: every cell hash-equal to end or clean disconnect-continue; results advance ≤10 s after drop (synthetic ready). | 3 | W7.E4.T5 | Scenario logic untouched — replication by construction; validation-only |

### W7.E5 — Rollback gate + internet beta (20 jd)

| ID | Task | Files | Steps / acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|
| W7.E5.T1 | Snapshot/restore prototype | new `src/platform/net/netsave.c`, `src/boss.c:72-73` accessors | §4.8: arena memcpy + registry-driven global save/restore; ring buffer of 8. **Accept**: the snapshot-restore-resim hash gate (deterministic run: hash(straight N+k) == hash(restore@N → resim k)) green on 3 MP stages × 2 scenarios; any miss produces a named-region diff. | 6 | W7.E3.T2 | Dev-only (`GE007_NETSAVE_TEST`); zero effect when off |
| W7.E5.T2 | Sim-only tick cost instrumentation | uses W7.E3.T1 split, `port_trace` | Measure sim-loop-only tick time (draw loop skipped) across MP stages/4P; measure snapshot+restore time. **Accept**: numbers logged into a report; depth-4 rollback budget verdict computed. | 4 | W7.E5.T1, W7.E3.T1 | Instrumentation only |
| W7.E5.T3 | Rollback go/no-go decision | report | Apply §4.8 criteria; if GO, write the rollback implementation plan (separate doc); if NO-GO, fix internet delay policy. **Accept**: 1-page decision recorded in this doc's changelog. | 1 | W7.E5.T2 | — |
| W7.E5.T4 | Internet beta: direct + join-code decision + hardening | `src/platform/net/*` | Direct-IP + UPnP port-map attempt; fuzz lane over `netmsg` decode (libFuzzer target on the deserializer, ROM-free); per-peer rate caps; decide relay: **v1 = direct/ZeroTier documented, no bespoke relay** (ops/cost per review §4.7 — revisit only post-beta demand). **Accept**: two machines on different networks connect via forwarded port; fuzz 1 h clean; handshake rejects wrong arch/build. | 6 | W7.E4.T5 | R2: no third-party service dependency shipped; fuzz gate before exposure |
| W7.E5.T5 | Cross-arch determinism measurement | `tools/net_smoke.sh --cross` | Run the deterministic replay + hash-trace on ARM64 vs x86_64 builds (Rosetta or a second machine); compare per-60-tick hashes. **Accept**: a measured answer. Divergence ⇒ document same-arch pools as the standing policy (and keep the puppet+event fallback on file); agreement ⇒ open cross-arch play. | 3 | W7.E3.T3 | Measurement only |

**Totals**: E1 10 + E2 20 + E3 21 + E4 29 + E5 20 = **100 junior-days ≈ 20 junior-weeks**
(netplay share E3–E5 = 70 jd ≈ 14 jw — inside the honest 8–16 jw band for the largest
gameplay-side item; split-screen polish = 30 jd ≈ 6 jw).

---

## 6. Milestones & deliverables

| M | Name | Contents | Demo script (reviewer runs) |
|---|---|---|---|
| M1 | **4-way couch, proven** | W7.E1.*, W7.E2.T5 | `tools/mp_smoke.sh --players 4 --timelimit 120 && tools/mp_smoke.sh --players 3` (GL + `GE007_RENDERER=metal`) |
| M2 | **AAA couch polish** | W7.E2.T1–T4 | `build/ge007 --remaster --multiplayer --players 3 --config-override Video.ThreePlayerWideBottom=1 --config-override Video.FovY.P2=70` — claim pads, play; then the identity check: same command minus overrides diffs 0 px vs baseline |
| M3 | **Loopback lockstep proof** | W7.E3.*, W7.E4.T1–T4 | `tools/net_smoke.sh` — two processes, 600 ticks, hash-equal; `tools/net_smoke.sh --fault drop-input` trips the oracle |
| M4 | **LAN 2-box beta** | W7.E4.T5–T6 | Box A: `build/ge007 --host --multiplayer --players 2 --mp-stage temple`; Box B: `build/ge007 --connect <ipA>` — play to scoreboard; pause syncs; pull B's ethernet → A finishes the match |
| M5 | **Rollback verdict + internet beta** | W7.E5.* | `tools/net_smoke.sh --netsave-gate` (restore-resim hash green) + two-network `--connect` session; go/no-go report in repo |

Each milestone is independently demoable and landable; M1/M2 ship value even if netplay stalls.

---

## 7. Risks & mitigations (ranked)

| # | Risk | L×I | Mitigation | Kill / de-scope criterion |
|---|---|---|---|---|
| 1 | **Hidden nondeterminism in the sim** (wall-clock reads, pointer-order behavior, uninit) breaks lockstep off-replay | Med×High | W7.E3.T3 audit + two-process 10× soak *before* any netcode UX; oracle finds the tick, arena dumps find the byte | If 2P same-machine two-process runs still diverge after 2 weeks of fixes → fall back to the puppet+event plan (docs on file, review-corrected) |
| 2 | **Pane/sim split regression** (W7.E3.T1) — sim work inside `lvlRender` (`lvl.c:1716-1719`) is subtle to relocate | Med×High | The `GE007_LOCAL_PANES=4 vs =1` sim-hash equality test is a mechanical proof; land behind env with identity default | If hash equality can't be reached, netplay renders **all** panes off-screen (correct but wasteful) — de-scope local-pane skip, keep netplay |
| 3 | **Input-delay feel** — mouse aim at +2 ticks reads "floaty" to some players | Med×Med | LAN default 2 ticks (~33 ms); expose `Net.InputDelayTicks`; rollback assessment (M5) is the structural fix for internet | If LAN playtest rejects 2-tick feel and rollback is NO-GO → netplay ships as "LAN/party-grade", internet demoted to experimental |
| 4 | **Cross-arch float divergence** (the review's standing objection) | High×Med (scoped) | v1 handshake enforces same build+arch; W7.E5.T5 measures instead of assumes | Divergence measured → same-arch pools are the permanent policy; no further spend |
| 5 | **Registry completeness** — restore/desync misses a region | Med×Med | Generated (not hand-curated) from the object set; snapshot-resim gate enumerates misses by construction | n/a (self-hardening) |
| 6 | **4P perf on Metal** — XLU per-pane snapshot cost | Low×Med | W7.E2.T5 census first; M1-scoped fix list; MP stages are small | If any stage <60 fps after 3 fix days → document per-stage, ship with note |
| 7 | **Stall storms** — one slow peer stalls all (lockstep property) | Med×Low (LAN) | Bounded stall + timeout→disconnect policy (W7.E4.T5); host tick clamp already bounds catch-up `[1,4]` | n/a |
| 8 | **Scope creep into relay infra** | Med×Med | Decision pre-made: no bespoke relay in this workstream (W7.E5.T4) | Any relay work = out of scope, full stop |

---

## 8. Validation strategy

Canonical harness (roadmap §7): every run uses `SDL_AUDIODRIVER=dummy GE007_DETERMINISTIC_STABLE_COUNT=1
GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1` + `--deterministic`.

```bash
# Build
cmake -S . -B build && cmake --build build -j && (cd build && ctest -R sim_state_hash)

# Identity (R3) — every E2/E4 commit: flags off ⇒ byte-identical
build/ge007 --rom baserom.u.z64 --multiplayer --players 4 --mp-stage temple \
  --deterministic --screenshot-frame 300 --screenshot-label base --screenshot-exit
# ... re-run with the feature flag on its identity value; then:
tools/compare_screenshots.py /tmp/base.png /tmp/candidate.png --max-changed-pct 0.0

# Split-screen lanes (M1)
tools/mp_smoke.sh --players 2 && tools/mp_smoke.sh --players 3 && tools/mp_smoke.sh --players 4
GE007_RENDERER=metal tools/mp_smoke.sh --players 4          # Metal parity per pane
tools/mp_smoke.sh --players 2 --timelimit 60                # scoreboard transition (E1.T2)

# R1 gates — unchanged and mandatory on every sim-adjacent commit
scripts/ci/check_sim_render_separation.sh
scripts/ci/check_timing_lock.sh                             # E4.T2 must keep this green
tools/sim_invariance_gate.sh dam1 600 2                     # incl. after E3.T2 re-baseline

# The pane-decoupling proof (E3.T1)
GE007_LOCAL_PANES=4 build/ge007 ... --multiplayer --players 4 --sim-state-hash-out /tmp/h4.json
GE007_LOCAL_PANES=1 build/ge007 ... --multiplayer --players 4 --sim-state-hash-out /tmp/h1.json
cmp <(jq .hash /tmp/h4.json) <(jq .hash /tmp/h1.json)       # MUST be equal

# Netplay correctness lane (M3+) — two OS processes, the only divergence-capable topology
tools/net_smoke.sh                        # host+client on 127.0.0.1, 600 ticks, hash-equal
tools/net_smoke.sh --fault drop-input     # oracle must trip
tools/net_smoke.sh --latency 30 --jitter 10   # adversarial network, still hash-equal
tools/determinism_soak.sh --repeat 10     # E3.T3

# Rollback gate (M5)
GE007_NETSAVE_TEST=1 tools/net_smoke.sh --netsave-gate      # restore-resim hash equality

# Gameplay-field cross-check when whole-arena hash legitimately differs (e.g. E2.T3 layout)
tools/compare_state.py /tmp/off.jsonl /tmp/on.jsonl

# Memory safety — hot paths touched (stubs input seam, net decode, pane loop)
tools/asan_smoke.sh && <libFuzzer netmsg target, 1h>        # fuzz before internet exposure

# Contamination guard (R2) — every commit; enet vendoring updates notices
scripts/ci/check_no_rom_data.sh && tools/check_third_party_notices.py
```

CI split (CI is ROM-free): unit tests (`sim_state_hash`, netbuf codec round-trip, NetTickMsg
fuzz corpus smoke) run in CI; every ROM-dependent lane above is a documented local preflight,
same as the existing invariance gate.

---

## 9. Open questions (need the user)

1. **Keyboard+mouse as a second local player?** Today KBM is hard-bound to P1
   (`stubs.c:6345-6348`, `lvl.c:5804`). Supporting KBM-P1 + 3 pads is what ships here; KBM as
   P2+ (two keyboards / split keyboard) is extra input plumbing — is there demand?
2. **Netplay identity**: derive player identity from the chosen MP character (no text entry
   exists in the engine — review §4.6) or budget real SDL text-input UI for names/chat?
   This plan assumes **character-derived, no chat** for v1.
3. **Classic 3P black quadrant**: keep classic as the default forever (faithful), or make
   wide-bottom the `--remaster` preset default once validated? (R3 allows either; presets are
   the precedent — `platform_sdl.c` `s_remasterPreset`.)
4. **Internet beta distribution**: same-arch macOS-only pools are the safe v1. Is x86 Linux/
   Windows lockstep interop worth the E5.T5 investigation *now*, or defer until someone asks?
5. **Match-start seed for netplay when `--deterministic` is unset**: host wall-clock seed
   broadcast (proposed) means matches differ run-to-run like the N64. Any desire for a
   "tournament seed" option (fixed seed per match code)? Trivial either way.
