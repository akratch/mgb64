# Release Notes

## Unreleased — pre-ship hardening

A stability/fidelity pass on top of v0.2.0: a rendering-correctness fix, a
one-time config migration, sim-determinism pins for the faithful presets, and
a Windows/Linux/MinGW portability batch. No new user-facing features.

### Fixes

- 📊 **New FPS overlay, on by default.** Live FPS, frame time, and 1%-low
  readout in the top-right corner. Toggle with `Video.FpsOverlay=0` in
  `ge007.ini` or `GE007_FPS_OVERLAY=0`. Automatically hidden in
  deterministic/headless capture sessions.
- 🎯 **Auto-gun tracer beams now tick by default.** Turret tracer beams
  previously drew but never advanced or expired (frozen tracers); they now
  update every tick, matching original N64 behavior
  (`GE007_AUTOGUN_BEAM_TICK=0` restores the old behavior for A/B).
- 🏔️ **Fixed a Dam rendering artifact** — sky-blue shard geometry could slice
  through the road from spawn at wider fields of view. Root cause was an
  overly-broad portal-adjacency check in the room-visibility supplement;
  narrowed to match the stock portal graph (`GE007_VIS_SUPPLEMENT_ANY_ROOM=1`
  restores the old unconditional behavior for A/B).
- 🔊 **One-time audio config migration.** Installs with a saved
  `Audio.MasterVolume=0.7` from before the music/SFX volume-bus split are
  migrated once to the new unity default (`1.0`) — the old value was a
  dead placeholder that is live now and would otherwise silently attenuate
  all audio.

### Determinism

- 🔒 **`--faithful` / `--faithful-hd` now pin sim behavior, not just visuals.**
  Both presets flip several port-added visibility/physics wideners back to
  their stock defaults (each individually overridable via its own `GE007_*`
  env var) and log the resolved state at boot.
- 🎥 **`--deterministic` now also pins FovY**, removing one more source of
  frame-to-frame/run-to-run divergence in deterministic capture and replay.

### Portability

- 🪟 **Windows saves** now go to `%APPDATA%\ge007\` with a real write-probe
  (rather than assuming the directory is writable), fixing save failures on
  locked-down installs.
- 🪟 **MinGW compile fixes** for several blockers that previously stopped a
  from-source Windows build, including a Windows-only field-name collision
  with the C library's `errno` macro (renamed to `errnum` internally).
- 🐧 **Linux build fix** — `-fno-strict-aliasing` added for GCC, resolving
  undefined-behavior-driven miscompiles under `-O3` (this is a decomp port
  with pervasive pointer punning between original struct layouts and native
  types).

### Robustness

- 🛡️ Crash/robustness hardening batch: safer fallback behavior when optional
  OS-level setup (e.g. the crash-handler alternate signal stack) isn't
  available, and a latch fix so a transient graphics-resource failure doesn't
  wrongly stay latched forever once the triggering condition changes.

## v0.2.0 — visual remaster + native Metal backend (release candidate)

**v0.2.0 adds an opt-in visual remaster and a native Metal graphics backend on
macOS**, layered on top of the v0.1.0 faithful port. Everything is behind flags:
the byte-faithful 1997 look is preserved (`--faithful`), and no visual option
changes the gameplay simulation. Still a community-iteration release — bring your
own legally-owned ROM.

### Highlights

- 🎬 **One-switch visual modes.** `--faithful` (byte-faithful original),
  `--faithful-hd` (the faithful look at 2× supersampling, HD-pack-ready), and
  `--remaster` (the full cinematic stack). See
  [docs/VISUAL_MODES.md](docs/VISUAL_MODES.md).
- ✨ **Remaster post-processing** — screen-space ambient occlusion, bloom, filmic
  tonemap, per-level colour grade, FXAA, contrast-adaptive sharpen, vignette,
  output dither, and 2× supersampling. Each effect is an independent
  `Video.*`/`GE007_*` toggle.
- 🖥️ **Native Metal graphics backend** on macOS (`GE007_RENDERER=metal`, selected
  automatically by `--remaster`), at parity with the GL reference path and without
  the Apple GL-over-Metal ambient-occlusion hang.
- 🧵 **Smoother frame pacing** — a sub-millisecond deadline pacer that locks the
  frame interval to the display, fixing periodic judder while turning/strafing.
- 🖼️ **HD texture pipeline** — build HD packs from *your own* ROM dump with
  `tools/texpack` (Real-ESRGAN / procedural synthesis). Packs are ROM-derived and
  are never distributed.
- 🔦 **Opt-in lighting features** (default-off, under review) — smoothed
  environment normals that remove baked per-quad lighting seams
  (`GE007_ENV_SMOOTH_NORMALS=1`) and destructible light fixtures
  (`GE007_SHOOT_OUT_LIGHTS=1`).
- 🕹️ **Modern feel options** (on by default; all reverted by `--faithful`) — a
  modern crosshair, hit markers, a tactical minimap, and widescreen FOV; plus
  opt-in aim-down-sights (`Input.AdsEnabled=1`) and 2–4 player split-screen
  (`--multiplayer --players N`).

### Rails

Every remaster feature is behind a named flag, defaults to a byte-identical
faithful baseline, and never reads or writes the gameplay simulation. All
HD/ROM-derived assets are local-only and never committed.

## v0.1.0 — first public release (community-iteration)

**v0.1.0 is a community-iteration release**, not a 1.0 replacement for the
original game. The goal is to get a genuinely playable, *honestly described*
native port into the community's hands so rendering, audio, and gameplay parity
can be improved together. You must bring your own legally-owned ROM — no game
data is included ([DISCLAIMER.md](DISCLAIMER.md)).

### Highlights

- 🎮 **Plays the whole single-player campaign.** All 20 levels boot, play, and
  reach mission-complete, with working combat, guard AI, weapons, pickups,
  objectives, menus, and audio.
- 🧱 **Native port of a decompilation** — runs the game's decompiled
  C/asm on PC/macOS via an OpenGL renderer + audio synthesis + keyboard/mouse and
  gamepad input.
- 🔒 **Asset-free.** Builds and runs from a clean checkout + *your* ROM; no ROM
  media is committed or compiled into the binary. The release guard
  (`scripts/ci/check_release_ready.sh`) checks for ROM/media contamination and
  is wired into the local release guard and source-archive smoke lane.
- 🖥️ **macOS app shell sources** and a local unsigned `.app` build script are
  included. Locally built app bundles can be verified asset-free; signing and
  notarization remain deferred.
- 🧪 **Documented instrumentation** — a contributor quick-lane
  (`tools/validate_quick.sh`) and [docs/INSTRUMENTATION.md](docs/INSTRUMENTATION.md).
- 📖 **Honest framing** — [PORT.md](PORT.md) documents exactly what is faithful,
  the known divergences, and the validation coverage.

### Known gaps (see [PORT.md](PORT.md) for the full list)

- Rendering/audio use documented compatibility approximations (prop-impact
  decals, room scissoring, sky fallback, a small frontend brightness residual,
  and remaining native music fidelity gaps versus hardware/reference output).
- Authored level intro cameras still differ from hardware/reference output in
  some scenes, including Bond being absent from Dam's early establishing camera.
- The repository still contains inventoried N64 SDK/libultra compatibility
  material that is not MIT-licensed first-party code; see
  [THIRD_PARTY.md](THIRD_PARTY.md).
- Save / mission-flow persistence has deterministic multi-folder EEPROM smoke
  coverage, but organic menu/mission flows are not yet validated end-to-end.
- Some N64-exact timing/AI cadence is still converging (does not affect normal play).
- No packaged prebuilt distributables are attached for this release. Build from
  source; the macOS `.app` can be built locally, but signed/notarized
  distribution is still deferred.
- Linux/GCC full native build plus ROM-free CTest is covered by the local
  preflight/source-archive smoke lane for launch; Linux ROM-backed runtime play
  smoke and Windows/MSYS2 setup are still less exercised than macOS.

### Reporting bugs

Please include: platform, ROM region (U/E/J), level/scene, expected vs. actual,
and steps to reproduce. **Do not attach ROMs, saves, or copyrighted game
imagery.** Parity issues: check [PORT.md](PORT.md) first, and note whether any
`GE007_*` env toggle changes the behavior.
