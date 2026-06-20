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
- `python3` with **Pillow** (`pip install pillow`) only if you want the pixel lane.

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

The pipeline thinks in three parallel lanes plus a static guard:

| Lane | What it catches | Tool |
|------|-----------------|------|
| Static | Raw native switch-node dereferences (no ROM) | `check_native_switch_access.py` |
| Boot | Spawn invariants, asserts, crashes on level load | `spawn_health_check.sh` |
| Save | Cross-process EEPROM persistence smoke | `save_persistence_check.sh` |
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
presence of guard `CHR_RENDER` calls, and a clean exit. Exit code = number of
failed levels.

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

The script writes screenshots, traces, and logs to `/tmp/mgb64_renderer_parity_*`
by default. Those artifacts are generated from your ROM and must not be
committed or redistributed. Each scene prints exact local comparison commands
using `tools/compare_screenshots.py` and `tools/compare_state.py`.

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

## Reading the artifacts

`regression_test.sh` captures per level into a temp dir (kept on failure or with
`--keep-artifacts`):

- `screenshot_<lvl>.bmp` — frame capture. `compare_screenshots.py` reports
  changed-pixel %, unique colors, and a sample grid; default fail threshold is
  3.0% changed pixels.
- `trace_<lvl>.jsonl` — per-frame state trace (schema below). `compare_state.py`
  reports the first divergent frame and field path.
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
| `GE007_BUILD_JOBS=N` | cap build parallelism (default 4) |

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
| `GE007_TEXTURED_PROP_BULLET_IMPACTS=1` | opt into original textured prop-attached bullet impacts |
| `GE007_FLAT_PROP_BULLET_IMPACTS=1` | force shade-only prop-attached bullet impacts |
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

## ROM movement oracle

The public ROM-comparison lane for player movement is documented in
[ROM_COMPARISON.md](ROM_COMPARISON.md). It uses authored route specs from
`tools/rom_oracle_routes/`, native `--trace-state` captures, and an optional
local instrumented ares checkout that dumps the same movement fields from stock
RDRAM. Generated traces, screenshots, saves, emulator logs, and the local ares
checkout are ROM-derived/local artifacts and must stay out of git.

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
