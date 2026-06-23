# Instrumentation & Validation

MGB64 ships a small, dependency-clean subset of the instrumentation used to
develop the `ge007` native port. These tools let contributors check that a change
does not regress rendering, gameplay state, or boot health — using their own
build and their own ROM.

> The full internal validation matrix (emulator parity oracles, RAMROM replay,
> real-soundplayer packs, curated visual baselines — a few hundred scripts) is
> **not** shipped. It depends on dev-only artifacts and a stock ROM driven
> through a local emulator. See [Advanced / dev-only](#advanced--dev-only-not-shipped)
> for what exists and why it's omitted.

## Prerequisites

- A built native port at `build/ge007` (see [BUILDING.md](BUILDING.md)).
- Your own GoldenEye ROM at `baserom.u.z64` in the repo root.
- `python3` (standard library only) for the static check and the state/audio lanes.
- `python3` with **Pillow** (`pip install pillow`) for screenshot-health and
  pixel-comparison lanes.

The instrumentation runs the game headless and deterministically. The shipped
scripts set `GE007_DETERMINISTIC_STABLE_COUNT=1` so synthetic frame time advances
once per rendered frame instead of once per `osGetCount()` call; use the same env
var for hand-written deterministic probes, especially movement/timing captures.
Background runs should mute host audio:

```sh
export SDL_AUDIODRIVER=dummy
export GE007_MUTE=1
export PYTHONDONTWRITEBYTECODE=1
```

## The validation lanes

The public validation surface is organized into these lanes:

| Lane | What it catches | Tool |
|------|-----------------|------|
| Static | Raw native switch-node dereferences (no ROM) | `check_native_switch_access.py` |
| Boot | Spawn invariants, asserts, crashes, render-health counters on level load | `spawn_health_check.sh` |
| Playability | Deterministic gameplay input, movement records, actual player displacement, render-health counters | `playability_smoke.sh` |
| Scripted look | Deterministic input-freeze isolation while preserving authored `GE007_AUTO_LOOK_*` probes | `scripted_look_smoke.sh` |
| Damage HUD | Deterministic Bond damage, active health/damage timers, visible health/armor rings, optional HUD-class triangle check | `damage_hud_smoke.sh` |
| Soak | Long headless deterministic stability run; hard-fails on any crash/recovery/bad-cmd/NaN/DL-resolve failure | `soak_stability.sh` |
| Sanitizer | Short `-DSANITIZE=ON` ASan/UBSan pass over a few stages (report-only unless `--gate`) | `asan_smoke.sh` |
| Multiplayer | Split-screen deathmatch boot; asserts the two framebuffer halves are measurably dissimilar | `mp_smoke.sh` |
| Route contract | ROM-oracle route spec/adapters, optional native route captures | `route_contract_smoke.sh` |
| Save | Cross-process EEPROM persistence smoke | `save_persistence_check.sh` |
| Screenshot | Missing, wrong-size, blank, or nearly monochrome frame captures | `audit_screenshot_health.py` |
| Pixel | Renderer regressions (fog, texture, geometry) | `compare_screenshots.py` |
| State | Spawn pos, facing, floor height, collision, NaN | `compare_state.py` |
| Audio | Static/noise, silence, synth-chain breakage | `compare_audio.py` |

## Quick lane (start here)

The fast path is wired up in one script:

```sh
./tools/validate_quick.sh
```

It always runs the static switch-access guard (no ROM, no build needed). If a
`build/ge007` and a `baserom.u.z64` are present, it also runs a short boot/spawn
smoke. Missing prerequisites are reported as SKIPs, never hard failures, so the
static guard can run anywhere (including CI without a ROM).

## Running the lanes individually

### Static switch-access guard (always available)

```sh
./tools/check_native_switch_access.py
```

Fails if native-visible game code dereferences `ModelFileHeader->Switches[]`
directly outside the allowlisted low-level helper in `src/game/model.c`. Pure
source scan: no ROM, no build, standard-library Python only.

### Boot / spawn health

```sh
./tools/spawn_health_check.sh                 # Dam + Cradle
./tools/spawn_health_check.sh --all           # all 20 stages
./tools/spawn_health_check.sh --level 33      # single raw LEVELID
./tools/spawn_health_check.sh --no-build      # reuse an existing build
```

For each level it boots deterministically with `GE007_DEBUG=1` and checks:
camera-handoff seed, an initialized `standheight`, zero `[GEASSERT]` failures,
presence of guard `CHR_RENDER` calls, a clean exit, and a strict
`tools/audit_render_trace.py` pass over the generated JSONL trace. The render
audit fails on crash recoveries, unhandled GBI commands, display-list resolve
failures, room-render fallback records, or non-finite trace values. Exit code =
number of failed levels.

### Gameplay playability smoke

```sh
./tools/playability_smoke.sh                 # Dam + Cradle
./tools/playability_smoke.sh --all           # all 20 stages
./tools/playability_smoke.sh --level 33      # single raw LEVELID
./tools/playability_smoke.sh --no-build      # reuse an existing build
./tools/playability_smoke.sh --all --no-build \
  --config-override Video.RenderScale=2      # scene-target probe
```

This direct-native lane disables authored intros explicitly, holds a deterministic
gameplay stick window, captures a state trace plus screenshot, and requires:
clean process exit, zero `[GEASSERT]` failures, a valid 640x480 nonblank
screenshot, strict render-health audit, target player records for the requested
raw `LEVELID`, at least the configured nonzero `move.speed` record count, no
watch/pause state, and a minimum horizontal player position delta. The default
route tries `forward`, `right`, `back`, then `left` for each level and accepts
the first pattern that satisfies all checks. Use `--pattern`, `--input-window`,
`--min-moving-records`, and `--min-horizontal-delta` when validating a narrower
route.

The lane pins `Video.WindowWidth=640`, `Video.WindowHeight=480`, and
`Video.WindowMode=windowed` with command-line config overrides by default. This
keeps generated `ge007.ini` files and high-DPI desktop window sizes from
changing screenshot composition or scene-target size between attempts. Add
repeatable `--config-override Section.Key=value` options for targeted probes,
or pass `--no-default-config-overrides` when the point of the test is the user's
actual window/config profile.

After each process run, the lane audits the generated `ge007.ini` against every
requested override whose value should persist. This catches branch-vs-reference
mistakes where an older binary ignores `--config-override` and renders with a
different window/aspect while the harness log still lists the requested
settings. `Video.WindowX=-1` and `Video.WindowY=-1` are intentionally exempt
because the runtime persists the resolved centered window position.

The output directory defaults to `/tmp/mgb64_playability_smoke_*`. It includes a
`summary.tsv` row for each level's accepted pattern, a top-level `summary.json`
with the accepted-level list and pass/fail counts, a `contact_sheet.png` visual
review sheet of accepted screenshots, plus per-attempt screenshot, render, and
movement audit JSON files. Movement audit JSON contains moving-record counts,
displacement, target-player record counts, input-event counts, and any
failures. The top-level summary also records the config overrides applied to the
run. Keep generated JSONL traces, screenshots, summaries, contact sheets, and
audit logs local.

### Scripted look smoke

```sh
./tools/scripted_look_smoke.sh
./tools/scripted_look_smoke.sh --no-build --level 36
```

This lane exists because deterministic screenshot/oracle captures set
`g_freezeInput` to block live keyboard, mouse, and gamepad input. That freeze
must not suppress authored `GE007_AUTO_LOOK_*` probes; otherwise broad
look-direction screenshot comparisons silently capture a different camera angle
and mislabel composition drift as a renderer regression. The smoke direct-boots
a baseline and a `GE007_AUTO_LOOK_UP` run, then requires the baseline pitch to
stay near zero while the scripted run changes `move.pitch` by the configured
minimum. Keep the generated traces and screenshots local.

### Damage HUD smoke

```sh
./tools/damage_hud_smoke.sh                 # Dam, Surface 1, Facility
./tools/damage_hud_smoke.sh --level 33      # single raw LEVELID
./tools/damage_hud_smoke.sh --no-build      # reuse an existing build
```

This focused lane injects deterministic Bond damage, captures a state trace plus
screenshot, and requires: clean process exit, zero `[GEASSERT]` failures, valid
screenshot health, strict render-health audit, active `damage_show` and
`health_show` trace state, and visible warm health-ring plus cool armor-ring
pixels. HUD-class triangle counting is optional via `--min-hud-tris`; the
default level set includes Dam and Surface 1 because their level visibility scale
is `0.2`, plus Facility as the normal-scale control.

The output directory defaults to `/tmp/mgb64_damage_hud_smoke_*` and contains
per-level screenshots, logs, JSONL traces, render audit JSON, damage-HUD audit
JSON, and a `summary.tsv`. Keep these ROM-derived artifacts local.

### Stability soak

```sh
./tools/soak_stability.sh                 # Dam, Cradle, Caverns
./tools/soak_stability.sh --all           # all 20 stages
./tools/soak_stability.sh --frames 10800  # longer soak per stage
./tools/soak_stability.sh --no-build      # reuse an existing build
```

This long headless deterministic lane boots each stage with `GE007_DEBUG=1`,
runs it for `--frames` frames, captures the JSONL state trace, and pipes it
through `tools/audit_render_trace.py --max-crashes 0`. Any crash, recovery,
unhandled GBI command, non-finite trace value, or display-list resolve failure
hard-fails the lane. Per-frame render cost is now part of the trace via the
`tris` and `rooms_drawn` fields (triangle count and `g_BgNumberOfRoomsDrawn`),
so soak summaries can track rendering load over time. The output directory
defaults to `/tmp/mgb64_soak_stability_*`. Exit code = number of failed stages.

### ASan/UBSan smoke

```sh
./tools/asan_smoke.sh                 # report-only, exits 0
./tools/asan_smoke.sh --gate          # fail on any sanitizer finding
./tools/asan_smoke.sh --no-build      # reuse an existing build-asan binary
```

This configures a separate `-DSANITIZE=ON` build directory (`build-asan`) and
runs a short deterministic pass over a few stages with
`ASAN_OPTIONS=halt_on_error=1:detect_leaks=0` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. It is report-only by default
and always exits 0; pass `--gate` to make sanitizer findings fail the lane. The
output directory defaults to `/tmp/mgb64_asan_smoke_*`.

### Multiplayer split-screen smoke

```sh
./tools/mp_smoke.sh                                   # 2P temple deathmatch
./tools/mp_smoke.sh --players 4 --mp-stage complex    # 4P split-screen
./tools/mp_smoke.sh --timelimit 4                     # forced 4s match timer boundary
./tools/mp_smoke.sh --no-build                        # reuse an existing build
```

This boots a split-screen deathmatch via the native `--multiplayer`,
`--players`, `--mp-stage`, and `--scenario` flags, drives a short deterministic
P1 input window, and requires: clean process exit, zero `[GEASSERT]` failures,
a strict render-health audit (`--max-crashes 0`), and a valid 640x480
screenshot. It then crops the framebuffer at `SCREEN_HEIGHT/2` and uses
`tools/compare_screenshots.py` to assert the top (P1) and bottom (P2) halves are
measurably dissimilar; a duplicated-camera regression that renders identical
halves fails the lane. `--timelimit SECS` additionally passes
`--mp-timelimit` to the native direct-boot path and asserts the MP trace timer
reaches the requested limit crash-free. The output directory defaults to
`/tmp/mgb64_mp_smoke_*`.

Native-port feature patterns, current user-facing settings, and validation lane
expectations are summarized in [PORTING_AND_EXPANSION.md](PORTING_AND_EXPANSION.md).

### ROM-oracle route contract smoke

```sh
./tools/route_contract_smoke.sh
./tools/route_contract_smoke.sh --native-smoke --no-build --rom baserom.u.z64 --binary build/ge007
```

The ROM-free mode validates every built-in route spec plus generated native-env
and ares-input adapters. `--native-smoke` runs each route through the native side
of `tools/movement_oracle_capture.sh`, so route-level screenshots, render
audits, movement audits, and intro actor/animation audits execute without a
stock ares oracle build. The output directory defaults to
`/tmp/mgb64_route_contract_smoke_*`; generated traces, screenshots, logs, and
summaries are ROM-derived local artifacts and must not be committed.

With a local instrumented ares binary from
`tools/prepare_ares_movement_oracle_build.sh`,
`tools/movement_oracle_capture.sh --ares-bin ...` captures the same route from
stock ares. The stock lane now writes a local `stock_<route>.ppm` framebuffer
dump, audits it with `audit_screenshot_health.py`, and records it in
`summary_<route>.json`. By default the dump uses `stock_frames`, but a route may
set `stock_screenshot_frame` when the emulator needs extra menu/frontend frames
and the useful visual checkpoint happens earlier than process exit. Use the dump
as a ground-truth visual checkpoint next to the movement/intro comparator; it is
still a ROM-derived artifact and must stay out of git.

### Save persistence

```sh
./tools/save_persistence_check.sh --no-build
```

Runs deterministic processes against an isolated `--savedir`. The first process
seeds Dam/Agent completion into folder 0; the second process seeds a
Dam-through-Runway Secret Agent range into folder 1; the final process starts
without the seed helper and verifies the state trace reloads both folders from
`ge007_eeprom.bin`.

### Deterministic regression (pixel / state / audio)

```sh
# First, capture baselines from a known-good build:
./tools/regression_test.sh --baseline

# Then, before a change, compare against your baselines:
./tools/regression_test.sh

# Bootstrap / trace-only runs with no baselines yet:
./tools/regression_test.sh --allow-missing-baselines
```

Baselines are **not** shipped — they track a specific build's renderer output,
so they must be regenerated locally with `--baseline`. Without baselines the
default mode fails by design; use `--allow-missing-baselines` to skip
comparisons during bootstrap. The pixel lane needs Pillow; the state and audio
lanes are standard-library only.

### Renderer parity scenes

For known renderer compatibility defaults, capture compact local A/B scenes:

```sh
./tools/renderer_parity_capture.sh --no-build
```

The script writes screenshots, traces, comparison logs, screenshot-comparison
JSON, capture logs, per-capture JSON health files, and a top-level `summary.json` to
`/tmp/mgb64_renderer_parity_*` by default. Those artifacts are generated from
your ROM and must not be committed or redistributed. Each capture runs
`tools/audit_screenshot_health.py` and `tools/audit_render_trace.py` so
missing/blank screenshots, crash recoveries, unhandled GBI commands,
display-list resolve failures, non-finite trace values, and unexpected
room-render fallback records fail immediately. Scene comparisons are still
observational because they intentionally compare compatibility defaults against
diagnostic alternatives.

Current scenes:

| Scene | Default capture | Diagnostic capture | What to inspect |
| --- | --- | --- | --- |
| `facility_scissor` | Facility at frame 180 | `GE007_EXACT_ROOM_SCISSOR=1` | Interior seams and under-covered room edges when exact N64 scissor boxes are enabled. |
| `surface_sky_fog` | Surface 1 at frame 180 | `GE007_PORTAL_BFS=0` | Outdoor sky/fog state and room-admission side effects when falling back from portal BFS. |

Run one scene with:

```sh
./tools/renderer_parity_capture.sh --scene facility_scissor --no-build
./tools/renderer_parity_capture.sh --scene surface_sky_fog --no-build
```

### Level-intro census

To map native authored-intro coverage across direct-boot stages, run:

```sh
./tools/intro_census_capture.sh --no-build
./tools/intro_census_capture.sh --all --no-build
```

The default quick run captures Dam and Cradle; `--all` captures the 20 supported
solo stages. Each trace is captured with `GE007_ENABLE_LEVEL_INTRO=1`; its
screenshot is audited with `tools/audit_screenshot_health.py`; its trace is
audited with `tools/audit_render_trace.py`; then it is summarized by
`tools/summarize_intro_census.py`. The summary reports active intro camera
records, decoded swirl setup hashes, selected-camera fingerprints, Bond
render/animation counts, animation header hashes, held right-item counts, and
render-health maxima. The output directory defaults to
`/tmp/mgb64_intro_census_*` and includes both `summary.txt` and structured
`summary.json`, per-level screenshot/render audit JSON, a `captures.tsv` index,
and `capture_summary.json` for the capture-health layer; screenshots, JSONL
traces, logs, and summaries are ROM-derived local artifacts and must not be
committed.

For per-trace fingerprints that can be compared against a stock-ROM oracle trace,
use:

```sh
./tools/intro_trace_summary.py /tmp/mgb64_intro_census_*/level_33.jsonl

./tools/summarize_intro_census.py \
  --require-active-intro \
  --require-swirl \
  --require-selected-camera \
  --require-bond-rendered \
  --require-bond-anim \
  --require-bond-anim-hash \
  --json-out /tmp/intro_census_summary.json \
  /tmp/mgb64_intro_census_*/level_*.jsonl

./tools/intro_trace_summary.py \
  --baseline /tmp/stock_dam_intro_swirl_bond_anim.jsonl \
  --test /tmp/native_dam_intro_swirl_bond_anim.jsonl \
  --require-frozen \
  --camera-modes swirl \
  --start-intro-timer 11 \
  --end-intro-timer 97 \
  --compare-profile setup,selected-camera,bond-anim \
  --min-matched-timers 20
```

`intro_trace_summary.py` emits stable setup, selected-camera, camera-path,
Bond-animation, and full-active digests. Bond-animation digests include the
animation header hash and frame count in addition to frame progression. In
compare mode it aligns records by `intro.timer` and checks the selected fields
with explicit tolerances. Use this as the lightweight inventory/diff layer while
adding new stock-backed intro routes; keep `tools/compare_intro_trace.py` for
strict vector/path parity on a specific aligned route window.

`tools/audit_oracle_trace.py --json-out PATH` writes the same movement/control
metrics that it prints, including failure counts. Validation wrappers use this
for compact evidence files instead of scraping human-readable audit output.

## Reading the artifacts

`regression_test.sh` captures per level into a temp dir (kept on failure or with
`--keep-artifacts`):

- `screenshot_<lvl>.bmp` — frame capture. `audit_screenshot_health.py` checks
  basic validity; `compare_screenshots.py` reports changed-pixel %, unique
  colors, and a sample grid; default fail threshold is 3.0% changed pixels in
  `regression_test.sh`. The comparator itself exits nonzero for invalid inputs
  such as size mismatches and supports `--json-out` plus
  `--max-changed-pct N` for strict standalone gates.
- `trace_<lvl>.jsonl` — per-frame state trace (schema below). `compare_state.py`
  reports the first divergent frame and field path. `regression_test.sh` also
  runs `tools/audit_render_trace.py` against every captured trace, even when
  baselines are missing and state comparison is skipped.
- `audio_<lvl>.raw` — first 300 frames of PCM by default, s16le stereo
  22050 Hz. Set `GE007_AUDIO_DUMP_FRAMES=N` to capture a longer window.
  `compare_audio.py` fails on large RMS delta, a noise-like zero-crossing jump,
  or sudden silence.

### Reference audio comparison

For music-fidelity work, `tools/compare_audio_reference.py` compares an MGB64
audio dump against a WAV/raw capture from an emulator or hardware reference. It
does not require sample-exact timing: the tool aligns the captures by amplitude
envelope, then reports RMS level, envelope correlation, broad spectral-band
similarity, gain-normalized spectral tilt, stereo balance/width, and
high-frequency band drift. This is the preferred non-listening check for changes
that affect music/reverb/mixer behavior.

Keep reference captures local. Raw/WAV captures of game audio are generated from
your ROM or hardware output and must not be committed, attached to issues, or
redistributed.

#### Startup music A/B workflow

1. Capture the MGB64 pre-SFX music stream and local gain-staging evidence.

```sh
mkdir -p /tmp/mgb64_audio_ref

tools/startup_music_reference_check.sh \
  --no-build \
  --rom baserom.u.z64 \
  --out-dir /tmp/mgb64_audio_ref
```

This 2400-frame window captures roughly 80 seconds of boot audio on the port:
legal screen, Nintendo logo, Rare logo, eye intro, GoldenEye logo, and the first
unattended display-cast/attract loop. It intentionally does not synthesize a
Start press; do not set `GE007_AUTO_START` for this reference lane. The wrapper
keeps the local pre-SFX raw dump, final mixed raw dump, audio trace, log, save
directory, summary, and screenshot under the output directory so ROM-derived
validation artifacts do not land in the repo root.

2. Capture the same no-input startup window from a reference path.

Acceptable references include original hardware line-out capture, an emulator
movie whose audio is known-good for this scene, or a local instrumented emulator
build that dumps mixed N64 audio before the host speaker backend.

For a movie capture, convert the audio to plain PCM WAV:

```sh
ffmpeg -i reference_startup_capture.mov -vn -ac 2 -ar 44100 \
  -c:a pcm_s16le /tmp/mgb64_audio_ref/reference_boot.wav
```

For a local instrumented ares build, use the same no-input startup sequence and
dump mixed N64 AI audio before host playback. Stock ares builds normally do
not expose this dump hook; the wrapper below fails clearly if the selected
binary does not honor `ARES_AUDIO_DUMP`.

```sh
tools/prepare_ares_audio_dump_build.sh

tools/ares_startup_audio_reference.sh \
  --ares-bin build/ares-audio-dump/ares/build-audio-dump/desktop-ui/ares.app/Contents/MacOS/ares \
  --rom baserom.u.z64 \
  --out-dir /tmp/mgb64_audio_ref
```

`prepare_ares_audio_dump_build.sh` clones ares into ignored local build space,
applies a small `ARES_AUDIO_DUMP` hook, and builds a local frontend binary. It
does not vendor ares and it does not create redistributable audio. The capture
wrapper runs ares with an isolated settings file, an isolated blank save
directory, kiosk mode, and `Input/Defocus=Allow` so background captures keep
advancing without picking up a local `.eeprom` next to the ROM.

For the longer startup reference used while tuning the title sequence music,
capture about 90 seconds of 22050 Hz stereo PCM and allow extra wall-clock time
for shader compilation/startup overhead:

```sh
tools/ares_startup_audio_reference.sh \
  --ares-bin build/ares-audio-dump/ares/build-audio-dump/desktop-ui/ares.app/Contents/MacOS/ares \
  --rom baserom.u.z64 \
  --out-dir /tmp/mgb64_audio_ref \
  --seconds 110 \
  --rate 22050 \
  --dump-frames 1984500
```

`1984500` frames is 90 seconds at 22050 Hz stereo. If your reference dump uses a
different rate, channel count, endian, or length, pass the correct raw metadata
and use `--duration`/start offsets in the comparison command. A 44100 Hz
reference also works; it just adds a reference-resampling step to the comparison.

3. Compare the captures and write segmented JSON output.

The wrapper can recapture MGB64 and compare in one command:

```sh
tools/startup_music_reference_check.sh \
  --no-build \
  --rom baserom.u.z64 \
  --out-dir /tmp/mgb64_audio_ref \
  --reference /tmp/mgb64_audio_ref/reference_boot.wav \
  --min-compared-seconds 60 \
  --report-only
```

For a raw 22050 Hz ares reference:

```sh
tools/startup_music_reference_check.sh \
  --no-build \
  --rom baserom.u.z64 \
  --out-dir /tmp/mgb64_audio_ref \
  --frames 2700 \
  --reference /tmp/mgb64_audio_ref/ares_boot_22050.raw \
  --reference-format raw \
  --reference-raw-rate 22050 \
  --min-compared-seconds 80 \
  --report-only
```

Use `--report-only` while tuning or when first onboarding a new reference
capture. Omit it once the current thresholds describe an accepted baseline that
should fail on future regressions.

The lower-level commands below are equivalent and useful when comparing
previously captured files with custom offsets or durations.

For a WAV/movie-derived reference:

```sh
python3 tools/compare_audio_reference.py \
  /tmp/mgb64_audio_ref/reference_boot.wav \
  /tmp/mgb64_audio_ref/mgb64_boot_music.raw \
  --test-format raw --test-raw-rate 22050 \
  --target-rate 22050 --max-offset-seconds 20 \
  --min-compared-seconds 60 \
  --segment-seconds 10 --segment-hop-seconds 5 \
  --print-bands --print-segments \
  --json-out /tmp/mgb64_audio_ref/boot_audio_compare.json
```

For a raw 44100 Hz ares reference:

```sh
python3 tools/compare_audio_reference.py \
  /tmp/mgb64_audio_ref/ares_boot_44100.raw \
  /tmp/mgb64_audio_ref/mgb64_boot_music.raw \
  --reference-format raw --reference-raw-rate 44100 \
  --test-format raw --test-raw-rate 22050 \
  --target-rate 22050 --max-offset-seconds 20 \
  --min-compared-seconds 60 \
  --segment-seconds 10 --segment-hop-seconds 5 \
  --print-bands --print-segments \
  --json-out /tmp/mgb64_audio_ref/boot_audio_compare.json
```

#### Interpreting the metrics

Use the metrics as a regression signal, not a proof of bit-perfect N64 audio.

- `alignment_envelope_corr` reports how well the tool could align the two
  captures before comparing them. Low alignment usually means the captures do
  not contain the same scene, have different input timing, or drift too much.
- `envelope_corr` reports amplitude-shape similarity after alignment. Low
  values usually point to timing/sequence mismatch before they point to mixer
  tone.
- `spectral_cosine` close to `1.0` means the broad frequency shape is similar;
  it is less sensitive to simple volume changes.
- `band_mae_db` is the average absolute per-band energy error. Lower is better;
  a sudden increase after an audio change is a regression signal.
- `relative_band_mae_db` removes the overall band bias first. Use it when a
  hardware/movie capture has a different recording level but you still need to
  compare tone.
- `high_band_mae_db` isolates the upper bands. High values are useful for
  catching thin, harsh, overly bright, or missing-frequency output.
- `high_relative_band_delta_db` preserves the high-frequency direction after
  removing simple level mismatch. Positive values mean the port is relatively
  brighter than the reference; negative values mean the port is relatively
  duller.
- `stereo.balance_delta_db`, `stereo.width_delta_db`, and
  `stereo.possible_channel_swap` catch L/R bus, panning, or channel-order
  mistakes that a mono downmix can hide.
- The top-level `diagnosis` block is a triage hint. Treat it as a pointer to the
  next mixer/sequence area to inspect, not as proof by itself.
- The `segments` JSON and `--print-segments` output identify the worst 10-second
  windows. Use those windows to map a mismatch back to a logo, intro, or
  attract-loop portion of the startup sequence.

## State trace schema (`--trace-state path.jsonl`)

One JSON object per frame, one per line. Common fields:

| Field | Meaning |
|-------|---------|
| `f` | frame index |
| `p` | has-player (1/0) |
| `pos` | `[x,y,z]` player position |
| `tris` | triangles drawn |
| `crashes` | cumulative DL crash-recovery count |
| `bad_cmds` | unhandled GBI command count |
| `dl` | DL failure sub-counters (mtx/vtx/dl/movemem/texture/settimg/skips) |
| `rooms` | room lookup + visibility + fallback state |
| `fog` | `[r,g,b]` plus `fog_mul`, `fog_off` |
| `geom` | geometry-mode bits (hex string; cull bits ignored by comparator) |
| `segs` | segment-load mask (hex string) |
| `combat` | aim / autoaim / crosshair / health / shots |
| `move` | speed, raw gait inputs, boost, head/previous positions, and timers |
| `scan.nearest`, `target_x`, `target_y` | per-guard AI / sense / visibility / damage |
| `wr_*` / `wl_*` | right/left-hand weapon-render state and switch-node bases |
| `front` | frontend / menu state (folder, gamemode, stage, briefing, cursor…) |
| `inv` | inventory count, keyflags (hex), GoldenEye-key flags |
| `nan` | count of non-finite values detected this frame |
| `stub_hits` | optional; `--accuracy-lane` fails if any `snd_*` counter is non-zero |

Comparator behavior (`compare_state.py`): default float tolerance `0.1`; `geom`
masks the cull bits (`0x00003000`); a few late stages allow small `tris` drift;
corrupt lines (from DL crash-recovery longjmp) are skipped with a warning.

## Useful environment variables

| Variable | Effect |
|----------|--------|
| `GE007_MUTE=1` | start muted |
| `SDL_AUDIODRIVER=dummy` | silent SDL audio device (background runs) |
| `GE007_BACKGROUND=1` | hidden window, no input grab |
| `GE007_NO_VSYNC=1` | reliable `--screenshot-exit`, especially on macOS |
| `GE007_DETERMINISTIC_STABLE_COUNT=1` | stable synthetic frame time for deterministic validation |
| `GE007_DEBUG=1` | enable diagnostic stderr (used by spawn health) |
| `GE007_VERBOSE=1` | all diagnostic printf |
| `GE007_AUDIO_DUMP=path.raw` | dump first 300 final mixed PCM frames |
| `GE007_MUSIC_AUDIO_DUMP=path.raw` | dump first 300 libaudio/music PCM frames before native SFX mixing |
| `GE007_AUDIO_TRACE=path.jsonl` | trace per-frame audio queue, final PCM peak/rail hits, mixer, and SFX/player counters |
| `GE007_MUSIC_TRACE=path.jsonl` | trace music slot play/stop events |
| `GE007_MUSIC_TRACE_SNAPSHOT=1` | add per-frame music slot snapshots, including CSP ticks, tempo-derived `uspt`, queued events, and active voices |
| `GE007_MUSIC_MIDI_TRACE_JSONL=path.jsonl` | trace CSP MIDI note/control events with program, keymap, envelope, pitch, and wavetable metadata |
| `GE007_MUSIC_MUTE_PROGRAMS=list` / `GE007_MUSIC_SOLO_PROGRAMS=list` | mute or solo comma-separated music program numbers for local A/B diagnosis |
| `GE007_ENABLE_LIBAUDIO_LOWPASS=1` | opt into the experimental final libaudio/music de-emphasis filter |
| `GE007_DISABLE_LIBAUDIO_LOWPASS=1` | force the experimental final libaudio/music de-emphasis filter off even if enabled |
| `GE007_AUDIO_FILTER_TRACE_JSONL=path.jsonl` | trace ADPCM/resampler pulls; pair with `GE007_AUDIO_FILTER_TRACE_WAVE_BASE=0x...` to focus on one wavetable |
| `GE007_AUDIO_POLE_TRACE_JSONL=path.jsonl` | trace custom-FX pole-filter calls by coefficient, gain, state, and peak levels |
| `GE007_MIXER_POLE_SAMPLE_XOR=0|1` | diagnostic ABI1 pole-filter sample-lane override for A/B comparison |
| `GE007_MIXER_POLE_FC_XOR_MASK=mask` | diagnostic per-pole-section lane mask for startup music experiments; bits map to filter coefficients 4736, 6144, 6784, 8192, 8832 |
| `GE007_DISABLE_NATIVE_POLE_FILTER=1` | bypass native custom-FX pole filters for diagnosis |
| `GE007_SFX_TRACE_JSONL=path.jsonl` | trace SFX submits, voice starts/stops, volume/pan updates, owner-slot clears, stale post ignores, and native caller/line tags |
| `GE007_TRACE_CHRNUM=N` | add one guard's AI/action/render state to `--trace-state` |
| `GE007_TRACE_OBJECTIVES=1` | add objective data to the state trace |
| `GE007_NO_FOG=1`, `GE007_WIREFRAME=1`, `GE007_TEX_ONLY=1` | renderer debug toggles |
| `GE007_FOG_USE_LINEAR_DEPTH=1` | fog-depth negative control; remaps camera-space depth linearly and should not be used as the N64-parity default |
| `GE007_FORCE_POINT_FILTER=1`, `GE007_FORCE_LINEAR_FILTER=1`, `GE007_DISABLE_N64_FILTER=1` | texture-filter A/B probes for smearing, bilerp, and shader-filter issues |
| `GE007_DIAG_N64_FILTER_ALWAYS_3POINT=1`, `GE007_DIAG_N64_FILTER_NEAREST_THRESHOLD=N`, `GE007_DIAG_N64_FILTER_CLAMPED_NON_TEXEDGE_NEAREST_THRESHOLD=N` | N64 shader-filter controls; use to separate threshold policy from texture decode |
| `GE007_FORCE_ROOM_POINT_FILTER=1` | negative control for room-geometry filtering; this intentionally bypasses the default N64 shader filter and should look harsher on Dam/Cradle/Surface |
| `GE007_TRACE_TEX_FOOTPRINT=1`, `GE007_TRACE_TEX_FOOTPRINT_BUDGET=N` | log `G_SETTILESIZE` decode-footprint decisions, including row pitch, visible row width, LOD state, and room-DL context |
| `GE007_DISABLE_LOADBLOCK_STRIDED_FOOTPRINT=1` | negative control for row-pitch smearing; disables the default LOADBLOCK strided decode footprint without changing source texture bytes |
| `GE007_TINT_TEX=min:max`, `GE007_SKIP_TEX=min:max` | tint or skip `G_SETTEX` texture-number ranges; these match stable game texture numbers, not transient GL upload ids |
| `GE007_DIAG_DISABLE_SHADER_CLAMP=1` | negative control for shader-side UV clamp; use only to prove clamp policy/coordinates are involved |
| `GE007_NO_SKY=1`, `GE007_SKIP_SKY=1`, `GE007_SKY_SCREENSPACE=1`, `GE007_SKY_UV_SCALE=N` | sky isolation, legacy sky path, and UV-scale probes |
| `GE007_BUILD_JOBS=N` | cap build parallelism (default 4) |

Fog regressions can masquerade as texture loss on distant stages. N64
`gSPFogPosition` values are specified from near plane to far plane, but the
effect is nonlinear after perspective transform; a narrow range such as
Cradle's 996..1000 ramp can legitimately fog much earlier than a linear
world-distance reading suggests. Use `GE007_TRACE_FOG_TRIANGLES=1` before
changing texture decode/filtering. The parity path uses `clip_z / clip_w`;
for negative or near-zero homogeneous `w`, keep the legacy saturated reciprocal
behavior so clipped geometry does not become incorrectly clear.
`GE007_FOG_USE_LINEAR_DEPTH=1` is only a negative control for proving that a
linear remap is causing over-clear distant geometry.

Room geometry should not default to point sampling. The game's room display
lists commonly request N64 texture filtering, and the native path routes that
through the shader-side N64 filter. A blanket room-nearest override bypasses
that path and shows up as low color diversity/blocky texture regression on
Cradle, Dam, Surface, and similar large room surfaces.

Texture regressions tend to fall into three separate classes. Keep them
separate during review so one fix does not hide another:

1. **Filter policy:** `GE007_DISABLE_N64_FILTER=1` falls back to ordinary GL
   bilinear sampling and can produce diagonal/near-plane smear on Dam and
   Cradle room geometry. The default path uses the shader-side N64 filter plus a
   nearest threshold. The footprint is normalized to the N64 VI/logical pixel
   grid so RenderScale and high-DPI windows do not change shader branch policy.
   Clamped non-texture-edge room materials use the same `1.0` default threshold
   as the rest of the N64 filter path; a special `0.05` threshold is a known
   over-softening regression on Bunker's warning sign and similar close room
   textures. Use
   `GE007_DIAG_N64_FILTER_CLAMPED_NON_TEXEDGE_NEAREST_THRESHOLD=N` to isolate
   that class.
   `GE007_DIAG_N64_FILTER_ALWAYS_3POINT=1` is the negative control: it removes
   the threshold and should visibly soften large close surfaces.
2. **Decoded-source footprint:** texture-cache identity must include the decoded
   source row pitch as well as visible upload dimensions. N64 room materials can
   describe the same visible tile size from either packed `LOADBLOCK` bytes or a
   strided `LOADTILE`/sub-rect source; omitting the source pitch lets a later
   material reuse a GL texture decoded with the wrong stride, which looks like
   row smear even though the source bytes and sampler mode are correct.
   `GE007_DISABLE_LOADBLOCK_STRIDED_FOOTPRINT=1` is the negative control: if it
   barely changes a capture, the active defect is probably filter policy rather
   than decoded-source stride.
3. **Rare `G_SETTEX` tile state:** stale ordinary TMEM tile descriptors can
   survive next to the active texture-by-number state. A healthy
   `GE007_TRACE_SETTEX_MATERIAL_CC='*'` trace has shader clamp bits that agree
   with the decoded `settex` fallback tile state. If `tile0`/`tile1` says wrap
   but `opts` still carries `SHADER_OPT_TEXEL*_CLAMP_*`, native rendering will
   clamp repeated room coordinates to an edge row/column and stretch it across
   the surface. The renderer should only trust an authored room tile descriptor
   when its clamp, shift, offset, and dimensions match the current `G_SETTEX`
   state.
4. **Material attribution:** `G_SETTEX` texture numbers and OpenGL upload ids
   are not interchangeable. Use `GE007_TINT_TEX`/`GE007_SKIP_TEX` to isolate
   stable game texture numbers, then confirm the material with
   `GE007_TRACE_SETTEX_MATERIAL_CC='*'`. Runway is a good counterexample: the
   same visual band can include texture 22, 1267, and ordinary room materials,
   so a single broad crop can make the wrong texture look guilty.

The June 2026 renderer regression postmortem is in
[RENDERING_REGRESSION_NOTES.md](RENDERING_REGRESSION_NOTES.md). It summarizes the
glass, filter-footprint, decoded-footprint, `G_SETTEX`, and fog failure modes
that this instrumentation is meant to keep from recurring.

Renderer acceptance captures should use a clean config profile or explicit
overrides. A repo-root run can load local `ge007.ini`; values such as
`Video.FovY=75` deliberately change composition and pixel metrics. The
playability smoke lane pins its validation window by default; for stock visual
baselines, keep that default and add only the probe-specific overrides, such as
`--config-override Video.FovY=60 --config-override Video.RenderScale=1` or
`--config-override Video.RenderScale=2`. When comparing against an older binary
that does not support the current config-override path, use
`--no-default-config-overrides` on both captures or upgrade the reference binary
before trusting whole-frame screenshot deltas.

### Renderer diagnostics & experimental fixes (default OFF)

The renderer exposes a family of `GE007_DIAG_*` toggles for A/B-testing
candidate fixes without changing the default. The main default-off visual
experiment currently kept for reference is the **frontend/menu brightness**
color scale (see [PORT.md](../PORT.md)):

| Variable | Effect |
|----------|--------|
| `GE007_DIAG_NOPERSPECTIVE_SETTEX_TEXCOORDS=1` | noperspective texcoords for `settex` materials |
| `GE007_DIAG_SETTEX_CC_COLOR_SCALE=1` | enable a combiner color scale on `settex` materials (menu brightness experiment) |
| `GE007_DIAG_SETTEX_CC_COLOR_SCALE_VALUE=N` | the scale factor (default `1.02`) |
| `GE007_GLASS_BULLET_IMPACT_NORMAL_OFFSET=N` | tune the default glass-crack decal lift (default `2.0`) |
| `GE007_BULLET_IMPACT_NORMAL_OFFSET=N` | force a global decal lift for diagnostics |
| `GE007_FLAT_PROP_BULLET_IMPACTS=1` | force shade-only (legacy flat) prop-attached bullet impacts; textured bullet-hole decals are now the N64-faithful default for **all** props (crates/barrels/screens/glass), so this is the A/B escape hatch |
| `GE007_FLAT_BULLET_IMPACTS=1` | force shade-only bullet impacts globally |

Run visual experiments with these set, compare against the default visually (and
via the pixel lane), and promote to default only after sign-off. The full
`GE007_DIAG_SETTEX_*` set is enumerated in `src/platform/fast3d/gfx_pc.c` /
`gfx_opengl.c`.

Deterministic input/world scripting (`GE007_AUTO_*`, format `START:LEN` on
deterministic frames) can drive buttons, movement, mouse-look, guard warps,
damage, pickups, camera modes, and frontend navigation for focused probes. The
full set is in `src/platform/main_pc.c`, `src/platform/stubs.c`,
`src/game/gun.c`, and `src/platform/port_trace.c`.

Useful focused probes:

| Variable | Effect |
|----------|--------|
| `GE007_AUTO_CAMERA_MODE_FRAME=N` + `GE007_AUTO_CAMERA_MODE=swirl|death|death_mp|posend|fp` | force a camera mode for cinematic/viewer rendering probes; death modes wait for Bond to be dead |
| `GE007_AUTO_CAMERA_POSEND_PAD=P` | seed forced `posend` camera mode with a real setup pad, matching the AI `CameraLookAtBondFromPad` path |
| `GE007_AUTO_WARP_CHR_FRAME=N`, `GE007_AUTO_WARP_CHRNUM=C`, `GE007_AUTO_WARP_CHR_DISTANCE=D`, `GE007_AUTO_WARP_CHR_ANGLE=A` | place Bond near a guard at a deterministic frame |
| `GE007_AUTO_SET_CHR_AI_FRAME=N`, `GE007_AUTO_SET_CHR_AI_CHRNUM=C`, `GE007_AUTO_SET_CHR_AI_LIST=L` | force a guard onto a global AI list; use top-level lists only |
| `GE007_AUTO_DAMAGE_BOND_FRAME=N`, `GE007_AUTO_DAMAGE_BOND_AMOUNT=F` | inject deterministic Bond damage |
| `GE007_AUTO_PLAY_SFX_FRAME=N`, `GE007_AUTO_PLAY_SFX=S` | submit one SFX by public id; useful with `GE007_SFX_TRACE_JSONL` for chain/mix probes |
| `GE007_AUTO_FIRE=START:LEN` | hold fire for a deterministic input window |
| `GE007_AUTO_START=START:LEN` | hold Start for a deterministic frontend/gameplay input window |

Console diagnostics that can become noisy during normal gameplay are opt-in.
Use these when chasing the specific subsystem; leave them unset for release
smoke runs and human play sessions.

| Variable | Effect |
|----------|--------|
| `GE007_TRACE_AI_MOVE=1` | log selected guard AI movement/pathing decisions and route probes |
| `GE007_TRACE_TRAVEL_ANIM=1` | log guard travel-animation state transitions |
| `GE007_TRACE_DL_CONTEXT=1` | log display-list context overlap probes |
| `GE007_TRACE_N64_OML=1` | log selected N64 `G_SETOTHERMODE_L` state transitions |
| `GE007_TRACE_BG_ROOM_PTRS=1` | log primary/secondary room display-list pointers as rooms render |
| `GE007_TRACE_ROOM_ALPHA=1` | log the first alpha-blended room triangles each frame, including raw/effective render mode, depth flags, texture-edge classification, and alpha sources |
| `GE007_TRACE_BLEND_CLASSIFY=1` | log first-seen raw render-mode classifications |
| `GE007_BLEND_AUDIT=1` / `GE007_BLEND_AUDIT_INTERVAL=N` | summarize all raw render modes seen by the Fast3D renderer; use the `edge` column to catch accidental alpha-test classification |
| `GE007_TRACE_VEHICLE_AI=1` / `GE007_TRACE_VEHICLE_AI_BUDGET=N` | log vehicle AI commands as they bind authored paths and target speeds |
| `GE007_TRACE_VEHICLE_STATE=1` / `GE007_TRACE_VEHICLE_STATE_BUDGET=N` / `GE007_TRACE_VEHICLE_STATE_INTERVAL=N` | log per-frame vehicle path, speed, waypoint, and movement decisions; use the interval to reduce noise |
| `GE007_VERBOSE=1` | broad legacy diagnostic output, including the focused logs above |

`GE007_SFX_TRACE_JSONL` uses public SFX ids for `public_sound` and one-based
`bank_public_sound`; `bank_sound` is the zero-based decoded-bank index. Owner
cleanup appears as `sndp_owner_clear` / `snd_owner_clear`, while stale handle
defense emits `owner_stale_clear`, `state_position_ignored`, or
`sndp_post_ignored`.

`GE007_AUDIO_TRACE` reports final mixed PCM health after music and SFX are
combined. `output_peak` is the absolute peak sample value for the frame
(`32768` means the negative rail was hit), and `output_rail_hits` counts samples
that landed exactly on `-32768` or `32767`. Sustained nonzero rail hits are a
strong signal of clipping or bad gain staging; occasional single hits during
explosions are worth comparing against a known-good baseline before treating as
a regression. Mixer counters such as `adpcm_dec_calls`, `resample_calls`,
`env_mixer_calls`, `mix_calls`, and `pole_filter_calls` identify which native
RSP-audio command families ran during the capture. `env_sample_xor` and
`pole_sample_xor` record the active ABI1 sample-lane diagnostics for that run;
the default is `0` unless a `GE007_MIXER_*` override is set. The
`*_clamp_hits` mixer fields are cumulative sample-saturation counters per
command family; the matching `*_clamp_delta` fields report only the current
audio frame. Use the deltas with `output_rail_hits` to distinguish final-output
rail hits from earlier ADPCM, resample, envmixer, aux-return mix, or pole-filter
saturation.

Relevant `ge007` command-line flags: `--rom PATH`, `--level N`
(33=Dam, 34=Facility, 24=Archives, 36=Surface 1), `--deterministic`,
`--background`, `--no-input-grab`, `--trace-state path.jsonl`,
`--screenshot-frame N` / `--screenshot-label L` / `--screenshot-exit`.

For headless split-screen multiplayer validation there is a parallel
direct-boot path: `--multiplayer` enters a match directly (bypassing the
frontend), `--players N` sets the player count (2-4), `--mp-stage NAME-or-id`
picks a multiplayer stage (`temple`, `complex`, `caves`, `library`, `basement`,
`stack`, `facility`, `bunker`, `archives`, `caverns`, `egypt`, or `random`; a
raw `MP_STAGE` index also works), and `--scenario NAME-or-id` selects the combat
mode (`normal` aka `deathmatch`/`combat`, `yolt`, `flagtag`, `goldengun`, `ltk`,
`2v2`, `3v1`, `2v1`). Passing any of `--players`, `--mp-stage`, or `--scenario`
implies `--multiplayer`. The MP direct-boot honors the same deterministic env
knobs as the solo path (`GE007_DETERMINISTIC*`, `GE007_AUTO_*`, the screenshot
and `--trace-state` hooks), so a 2-player run is scriptable:

```sh
env SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_NO_VSYNC=1 \
  GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
  /path/to/build/ge007 --rom /path/to/baserom.u.z64 \
  --multiplayer --players 2 --mp-stage temple --scenario normal \
  --deterministic --trace-state mp_state.jsonl \
  --screenshot-frame 120 --screenshot-label mp_temple_2p \
  --screenshot-exit > run.log 2>&1
```

### Transparent room material probe

Glass, water, and similar authored level geometry live in secondary room display
lists. When these vanish while collision still works, first prove whether the
secondary room DL is missing or whether Fast3D classified the material wrong:

```sh
tmpdir="$(mktemp -d /tmp/mgb64_glass_probe.XXXXXX)"
cd "$tmpdir"
env SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_NO_VSYNC=1 \
  GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
  GE007_TRACE_BG_ROOM_PTRS=1 GE007_TRACE_ROOM_ALPHA=1 \
  GE007_TRACE_BLEND_CLASSIFY=1 GE007_BLEND_AUDIT=1 \
  GE007_BLEND_AUDIT_INTERVAL=120 \
  /path/to/build/ge007 --rom /path/to/baserom.u.z64 \
  --level dam --deterministic \
  --screenshot-game-timer 220 --screenshot-label dam_glass_probe \
  --screenshot-exit > run.log 2>&1
```

The important `BLEND-AUDIT` invariant is that true XLU surface modes such as
`0x0C1849D8`, `0xC41049D8`, and `0x00504240` remain `ALPHA` but report
`edge no`. Explicit TEX_EDGE/cutout modes should still report `edge yes`. If
`BG-ROOM-PTR` shows secondary pointers as null for rooms with nonzero secondary
sizes, investigate room loading before touching renderer state.

For visual parity probes that need the same gameplay state across branches, use
`--screenshot-game-timer N` instead of `--screenshot-frame N`. The frame trigger
counts SDL/render syncs and can fire before comparable player simulation time if
startup, intro, or loading behavior differs. The game-timer trigger captures once
`g_GlobalTimer >= N`, so branch-to-branch screenshots line up on the same mission
tick.

### Vehicle AI/path probe

Scripted vehicles should receive their authored AI list from setup data, bind a
path, then ramp toward the scripted speed while the object handler advances along
waypoints. Dam's intro truck is a compact regression probe:

```sh
tmpdir="$(mktemp -d /tmp/mgb64_truck_probe.XXXXXX)"
cd "$tmpdir"
env SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_NO_VSYNC=1 \
  GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
  GE007_ENABLE_LEVEL_INTRO=1 GE007_INTRO_CAMERA_INDEX=5 \
  GE007_TRACE_VEHICLE_AI=1 GE007_TRACE_VEHICLE_AI_BUDGET=40 \
  GE007_TRACE_VEHICLE_STATE=1 GE007_TRACE_VEHICLE_STATE_BUDGET=160 \
  GE007_TRACE_VEHICLE_STATE_INTERVAL=10 \
  /path/to/build/ge007 --rom /path/to/baserom.u.z64 \
  --level dam --deterministic --trace-state state.jsonl \
  --screenshot-frame 80 --screenshot-label dam_truck_probe \
  --screenshot-exit > run.log 2>&1
```

The expected startup signature is `obj=279`, `pad=317`, AI list `0x040A`,
`path_id=7`, `path_len=13`, `raw_speed=512`, `speedaim=3.333333`, and
`speedtime=120`. Subsequent `VEHICLE_STATE` lines should keep `path_id=7`, point
at `waypoint=17` / `target_pad=312` initially, emit `move_accepted`, and show the
position changing as speed ramps up.

If `VEHICLE_STATE` ticks appear with `path_id=-1`, `speed=0`, and `aim=0` while
no `VEHICLE_AI` lines appear, inspect setup prop conversion and `ailistFindById`
resolution for vehicle/aircraft props. If AI lines appear but movement stalls,
look for `missing_switch_3`, `move_rejected_nav`, `move_rejected_footprint`, and
waypoint-sentinel handling in the object handler. The frame-80 probe is short by
design: it proves authored vehicle startup movement without turning into a broad
Dam intro/rendering soak.

## ROM movement oracle

The public ROM-comparison lane for player movement is documented in
[ROM_COMPARISON.md](ROM_COMPARISON.md). It uses authored route specs from
`tools/rom_oracle_routes/`, native `--trace-state` captures, and an optional
local instrumented ares checkout that dumps the same movement fields from stock
RDRAM. The route audit rejects captures that keep frontend/menu input active
after gameplay starts and can require minimum gameplay-input and moving-record
counts before comparison. Movement counts are scoped to the requested
target-stage player. It can also require a minimum horizontal player position
delta for native/direct gameplay smokes; standard ROM movement routes cap
suppressed menu attempts and require a clean menu-to-gameplay gap so the
bootstrap script cannot keep trying to press Start/A after handoff. Stock-backed
movement and intro comparisons emit `compare_<route>.json` artifacts with the
same pass/fail, alignment, threshold, and max-delta evidence used by the text
logs. Intro routes can compare the decoded swirl setup
fingerprint, stock-selected authored camera fingerprint, authored camera timer,
and Bond actor/action/animation fields before checking camera-path vectors. The
Dam coverage is split into a static selected-camera path route and a
timer-aligned swirl/Bond-animation route so duplicate stock video samples do not
masquerade as game ticks. Routes can also opt into the native render-health
audit, which fails on crash recoveries, bad GBI commands, display-list resolve
failures, room-render fallbacks, or non-finite trace values.
Generated traces, screenshots, saves, emulator logs, and the local ares checkout
are ROM-derived/local artifacts and must stay out of git.

## Advanced / dev-only (not shipped)

These families live in the development tree only. They depend on artifacts that
aren't part of a contributor checkout, so they're intentionally excluded:

- **Broad emulator parity / oracle** (`compare_texrect_trace.py`,
  `parse_ares_dyn_gfx_dump.py`, legacy GDB-driven `ares_*`) — need a local ares
  emulator build, a GDB stack, screen-capture permissions, and a stock ROM
  driven through GDB. The public movement-only ares hook is the narrow supported
  exception; see the ROM movement oracle section above.
- **RAMROM replay** (`ramrom_*`, `build_ramrom_*_rom.py`,
  `native_ramrom_playback_smoke.sh`) — need stock-ROM demo tables, generated
  steering ROMs, the N64 toolchain, and `/tmp` golden artifacts.
- **Real soundplayer packs** (`sndplayer_real_*`) — need a separately built real
  `ALSndPlayer` binary and produce large reviewer WAV/listening packs.
- **Curated visual references** (`menu_visual_*`, `visual_parity_*`, `parity_*`)
  — compare against golden packs/baselines not present here.
- **The aggregate orchestrator** (`validate_suite.sh`) — wires dozens of lanes
  including all of the above; use `validate_quick.sh` instead.

Reintroducing these would pull in emulator / ROM-derived artifacts and break
clean-checkout reproducibility, which is why the public repo ships only the
self-contained subset above.
