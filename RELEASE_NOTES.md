# Release Notes

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
