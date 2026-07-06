# Level Intro / Outro Cinematics

Reference for the animated mission-intro (the camera fly-in / swirl) and the
outro / death cameras: what plays, the settings that shape it, the A/B escape
hatches, and how to validate faithfulness against a stock ROM.

Companion documents: `docs/ROM_COMPARISON.md` (the stock-ROM oracle
workflow) and `docs/VISUAL_MODES.md` (the post-FX / remaster look, which is
separate from these cutscene cameras).

## What plays

A mission intro is a sequence of authored camera modes:

1. **INTRO** — one randomly-selected static "establishing" shot with the
   mission-title text. The camera is chosen per attempt from the stage's
   authored set (Dam has 6), rolling the RNG like the original game.
2. **FADESWIRL** → **SWIRL** — the descend-and-spin fly-around while Bond
   performs an intro animation (draw weapon, look around), ending by handing
   control to first-person gameplay.

Outros are level-scripted: an **AI-driven pose/orbit camera** (`POSEND`) on the
stages that use one, then a fade to the mission-report screen; **death** runs
the `DEATH_CAM` replay(s). Most stages simply fade to the report screen on
completion — there is no orbit-on-completion in the original game.

## Settings (persisted, in `ge007.ini`)

| Key | Default | Effect |
|-----|---------|--------|
| `Video.CutsceneFovY` | `60` | Vertical FOV for intro/outro/death cinematics, matching the N64's original framing regardless of the gameplay `Video.FovY`. `0` = follow `Video.FovY`. |
| `Game.IntroSkipStyle` | `0` | `0` = original staged skip (a face/trigger button advances one intro stage). `1` = instant skip (any input jumps straight to gameplay). |

Both are also settable per-run via `--config-override Key=VALUE` and are pinned
to their defaults under `--deterministic` for reproducible captures. The
`--faithful` / `--faithful-hd` presets pin `Video.CutsceneFovY=60` and
`Game.IntroSkipStyle=0`.

Whether the animated intro plays at all follows the launch path: it plays on a
normal menu-started mission, and is skipped (short first-person handoff) on a
direct `--level` / `--mission` / `--deterministic` boot. Force it either way
with the env hatches below.

## A/B escape hatches (environment)

Every faithfulness fix ships with a hatch that restores the pre-fix behavior,
for bisecting a regression or comparing looks. Set the variable to `1`.

| Env var | Restores |
|---------|----------|
| `GE007_ENABLE_LEVEL_INTRO` / `GE007_DISABLE_LEVEL_INTRO` | Force the authored intro on / off regardless of launch path. |
| `GE007_INTRO_CAMERA_INDEX=N` | Pin the establishing camera to index N (else per-attempt RNG). |
| `GE007_NO_CAMERA_SEED_FIX` | Pre-fix room admission — the establishing/outro shot renders only the player's spawn room (near-blank when the camera is elsewhere). |
| `GE007_NO_CINEMA_INTRO_FIX` | Pre-fix gate that skipped a stage's intro when it had cinema cameras but no swirl data. |
| `GE007_NO_INTRO_CHR_TIMING_FIX` | Pre-fix Bond intro-chr spawn timing at swirl entry (one record early vs stock). |
| `GE007_NO_BOND_BODY_FIX` | Pre-fix aliased viewer body (invisible / spiky Bond in the swirl and outro pose). |
| `GE007_INTRO_ANIM_LEGACY_SEED` | The pre-T12 hand-tuned intro anim seed constant (value-identical; documentation A/B). |
| `Video.CutsceneFovY=0` (via `--config-override`) | Cinematics follow the gameplay FOV instead of the faithful 60. |

Diagnostic traces (no behavior change): `GE007_TRACE_INTRO_PARSE`,
`GE007_SETUP_DIAG`, `GE007_VERBOSE` (spawn/room/camera dumps),
`GE007_TRACE_FOV`, `GE007_TRACE_CAMERA`.

## Validating against a stock ROM

The intro/outro camera paths are validated frame-for-frame against a patched
`ares` emulator running the stock ROM. See `docs/ROM_COMPARISON.md` for the
oracle build. Quick commands (all outputs are ROM-derived — keep them local):

```bash
# Headless establishing-shot screenshot (never use --ramrom for visuals):
SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
GE007_ENABLE_LEVEL_INTRO=1 GE007_INTRO_CAMERA_INDEX=5 \
./build/ge007 --rom baserom.u.z64 --level 33 --deterministic \
  --screenshot-frame 120 --screenshot-label dam_intro --screenshot-exit

# Camera-path oracle comparison (native vs stock ares):
tools/movement_oracle_capture.sh --route dam_intro_camera_path --no-build \
  --ares-bin build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares \
  --out-dir <local-dir>

# Emulator-free parse-determinism gate + full-stage census:
tools/intro_parse_digest_gate.sh
tools/intro_census_capture.sh --all
```

The comparator (`tools/compare_intro_trace.py`) aligns per camera mode and
reports divergences by field, with a ledger-tagged waiver mechanism for
characterized, not-yet-fixed differences (see the plan doc's defect ledger).

## Regression gates (`ctest`, ROM-gated — skip cleanly without a ROM)

- `intro_tools_unittests` — the comparator/digest/route tooling unit tests.
- `intro_parse_digest_smoke` — parsed intro-stream determinism.
- `intro_oracle_dam_route` — the full Dam camera-path oracle comparison.
- `intro_census_dam_smoke` — intro render-health counters.
