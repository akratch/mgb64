# Project Status

**MGB64 is experimental, proof-of-principle software.** It demonstrates that the
game can be decompiled and run through a native port, but it is not a finished,
one-command, plug-and-play release. This document is the honest current state so
you know what to expect and where help is most valuable.

## What works

- A large portion of the game is **decompiled** to readable C/assembly:
  rendering, character AI, combat, level logic, menus, and audio systems.
- **The native port builds and runs from a clean checkout + your own ROM.**
  `cmake -B build && cmake --build build` produces `ge007`; running it loads
  your ROM, decodes its audio, brings up the OpenGL renderer, and enters the
  game loop. Verified end-to-end on macOS/arm64. The repository also wires a
  Linux/GCC native build plus ROM-free CTest suite through local preflight and
  source-archive smoke. Hosted GitHub Actions is not a required release gate.
- The port is **asset-free**: no ROM media is compiled into the binary. All bulk
  game data — textures, audio, animation frames, fonts, the Rareware logo — is
  read from *your* ROM at runtime (`rom_io.c`) and served through the
  stub/segment layer (`src/platform/asset_stubs.c`, `segment_stubs.c`). The
  repository ships only first-party + code-coupled source (display lists,
  descriptor/offset tables, model-header glue), never ROM media.
- **Save persistence has smoke coverage:** deterministic solo completions are
  written to two save folders across separate processes, then reloaded from disk
  after a final process restart (`tools/save_persistence_check.sh`).
- **Direct gameplay input has smoke coverage:** `tools/playability_smoke.sh`
  direct-boots levels deterministically, applies real gameplay stick input, and
  requires movement records, horizontal player displacement, clean watch state,
  zero assertions, screenshot-health-clean captures, and render-health-clean
  traces. It writes per-attempt audit JSON plus a top-level `summary.json` so
  local ROM-backed playability evidence can be reviewed without scraping logs.
- **2-player split-screen multiplayer is wired (input + launch + aim):** the
  native port opens every connected pad into its own player slot, fills
  `data[1..3]` so `joyGetControllerCount() >= 2` unblocks the MP menus, direct-boots
  a deterministic split-screen match via `--multiplayer/--players N/--mp-stage/
  --scenario`, and routes aim per player (mouse-look → P1, pad `k` → player `k`).
  `tools/mp_smoke.sh` is the 2-player measurement lane: it boots a deathmatch,
  drives a scripted player-1 input window, and asserts the two framebuffer halves
  are measurably dissimilar so a duplicated-camera bug fails. What is **proven**
  today: the 2-player lane is **green** — boot, two distinct viewports (~97%
  dissimilar halves), render-health clean, zero crashes; and 4-player boots and
  renders distinct viewports in the same smoke window. What is **pending**:
  sustained-load frame budget, the higher-risk 3-player asymmetric split, and a
  full end-of-round scoreboard run are not yet validated. See
  [../docs/MULTIPLAYER_PLAN.md](MULTIPLAYER_PLAN.md).
- **Recent playability fixes are landed and traceable:** Dam's intro truck now
  binds its authored vehicle AI path and target speed, and vehicle geometry uses
  the corrected native vector path so the truck body/wheels render together.
  Transparent glass no longer disappears while collision remains active: the
  native renderer handles secondary room alpha plus prop-type glass material
  paths, and glass bullet-crack decals stay surface-aligned instead of vanishing
  edge-on. The maintained probes live in
  [INSTRUMENTATION.md](INSTRUMENTATION.md): the vehicle probe expects Dam truck
  `obj=279`, AI list `0x040A`, `path_id=7`, and a moving `target_pad=312`
  startup; the glass probe audits secondary alpha material classification and
  glass draw ranges. The latest local MP timer acceptance run reached the forced
  `60/60` tick match boundary with about `98%` split-half dissimilarity and zero
  render-health failures; scoreboard/results transition proof remains open.
- **Modern display controls are available through the native settings schema:**
  window mode, display selection, fullscreen mode sizing, VSync, frame cap,
  render scale, MSAA, gamma, retro filtering, and gameplay FOV are configurable
  while defaulting back to the original 4:3-style presentation. See
  [DISPLAY_INPUT_PLAN.md](DISPLAY_INPUT_PLAN.md) and
  [PORTING_AND_EXPANSION.md](PORTING_AND_EXPANSION.md).
- **ROM-vs-native comparison tooling exists for targeted parity work:**
  `docs/ROM_COMPARISON.md` documents route specs, native traces, optional
  instrumented ares stock traces, movement/intro comparators, and structured
  JSON artifacts. The built-in Dam routes cover forward/strafe scalar movement
  speed dynamics plus selected-camera and timer-aligned swirl/Bond-animation
  intro checks. Captured traces/screenshots remain local ROM-derived artifacts
  and are not shipped.
- **A local unsigned macOS app bundle can be built from source:**
  `macos/Scripts/build_app_bundle.sh` links the Swift/AppKit shell against
  `build-macos/libge007_lib.a`, assembles `build-macos/MGB64.app`, and keeps the
  app asset-free (`macos/Scripts/verify_asset_free.sh build-macos/MGB64.app`).

## Remaining work

- **N64 ROM (byte-matching) build:** the native port is the supported target.
  Rebuilding the original N64 ROM from source additionally requires local,
  external IDO static-recompilation tooling plus user-supplied SGI IDO/IRIX
  compiler files for the matching toolchain; neither the recompilation source
  nor the proprietary compiler inputs are redistributed here. It also requires
  the ROM-derived data tables to be laid out at their ROM addresses; wiring the
  extracted `.bin` of the animation / image tables into the N64 link (the repo's
  existing `.incbin` pattern, as used by `music.s`/`ob_seg.s`) is the main
  code/data-link task there.
- **Release hygiene / SDK replacement:** the tree no longer tracks proprietary
  SDK notice text, but it still contains inventoried SDK/libultra-lineage
  compatibility material for the in-progress matching target. This remains the
  largest conservative provenance area, so do not describe the repository as
  fully clean-room. The current native CMake build no longer compiles any
  `src/libultra/audio/*.c` or
  `src/libultrare/audio/*.c` sources. The top-level compatibility headers
  `include/{assert,bstring,limits,math,sgidefs,stddef,stdlib,svr4_math}.h` and
  `include/PR/{R4300,abi,gbi,gu,libaudio,mbi,os,os_internal,rcp,region,ultratypes}.h` have
  been replaced clean-room, as have internal libultra helper headers
  `src/libultra/{audio/synthInternals.h,gu/guint.h,io/viint.h}`.
  The `NATIVE_PORT` branch of `include/PR/os.h` now forwards to the clean native
  platform surface instead of redefining OS/address macros. The native `include/PR/R4300.h`
  address macros are now guarded as fallback definitions so they do not override
  the native platform address conversions. Most matching-target GU C helpers
  `src/libultra/gu/{align,cosf,coss,lookat,lookatref,mtxutil,normalize,ortho,perspective,rotate,scale,sinf,sins,translate}.c`
  and `src/libultra/io/viblack.c` are now clean-room compatibility sources. The
  native CMake targets no longer expose the broad `src/libultra/`,
  `src/libultra/gu/`, or `src/libultrare/audio/` include directories; native
  audio compatibility keeps only the narrow `src/libultra/audio/` include path
  for the clean-room `synthInternals.h` / `seqp.h` surface.
  The former split libaudio helper/wrapper files under `src/libultra/audio/` are no
  longer tracked as standalone sources. Native and matching-target builds share
  the project-owned implementation in `src/platform/audio_compat.c`; the
  matching-target makefile reaches it through
  `src/libultra/audio/clean_compat.c` instead of compiling historical SDK
  libaudio implementation files or unbuilt duplicate wrappers.
  The matching-target linker scripts likewise place only
  `src/libultra/audio/clean_compat.o` for libaudio sections; historical
  SDK/Rare libaudio object references are release-guarded against returning.
  The native SDK/demo-lineage support-code audit is recorded in
  [PROVENANCE_AUDIT.md](PROVENANCE_AUDIT.md). The remaining tracked
  SDK-shaped compatibility paths under `include/PR/`, `src/libultra/`, and
  `src/libultrare/` are explicitly inventoried and guarded by
  `../tools/check_sdk_inventory.py`, so the public provenance and libaudio
  linker surfaces cannot grow without a deliberate inventory/docs update.
- **Renderer/audio accuracy** and assorted gameplay parity vs. original
  hardware (see below).
- **Signed/notarized macOS distribution:** the local `.app` build exists and
  remains asset-free, but it links against the builder's local SDL2 dylib.
  The build script can fail closed on a requested deployment target and bundle
  SDL2 for app-candidate testing, but Developer ID signing, notarization, a
  controlled SDL2 deployment target, DMG polish, and any prebuilt release
  artifact remain deferred.

## Known issues

- Graphical inaccuracies and occasional rendering glitches versus original
  hardware. The renderer now has strict render-health counters, screenshot
  health gates, and a small renderer parity scene lane, but visual accuracy is
  still not claimed as hardware-perfect.
- Authored level intro parity is still incomplete across the full game, but Dam
  now has local ROM-vs-native selected-camera/static-camera coverage plus
  timer-aligned swirl/Bond-animation coverage and native actor/render/held-item
  trace auditing. A local native intro-census lane can now sweep direct-boot
  stages for active intro cameras, decoded swirl setup hashes, Bond
  render/animation coverage, animation header hashes, screenshot health, and
  render-health counters.
- Some gameplay/behavior differences and occasional instability or crashes.
  Movement-speed parity has targeted ROM-backed Dam coverage and all-level
  deterministic native playability smoke coverage, but broader movement edge
  cases, mission flow, menus, combat behavior, and organic input paths still
  need reference-backed expansion. `tools/soak_stability.sh` is the headless
  deterministic measurement path for crash/render-health regressions over long
  per-stage runs (no numeric stability budget is claimed yet); `tools/asan_smoke.sh`
  adds a report-only ASan/UBSan lane.
- Audio is functional and the SFX mapping/owner-slot path has been validated.
  Native music now follows ABI1 little-endian sample-lane ordering for the
  envmixer and custom pole-filter paths, with additive aux-return mixing. It
  still needs hardware/reference parity work for final reverb balance and any
  remaining command-level edge cases.
- This is **not** a 1:1 replacement for the original ROM on real hardware.

## Good first issues

- Wire the extracted animation/image-table `.bin` into the **N64 ROM build** via
  the `.incbin` pattern so `make` rebuilds the ROM from a clean clone + ROM.
- Replace one narrow SDK compatibility surface with clean-room declarations or
  implementation, starting with native-facing graphics/OS/audio declarations.
- Improve renderer accuracy for a specific effect (compare against a reference
  emulator).
- Expand organic menu/mission-flow validation beyond the current deterministic
  multi-folder EEPROM persistence smoke check.
- Port build ergonomics: local build coverage on more platforms, packaging.
- Documentation: expand build notes for your platform.

For the fuller contributor roadmap, see [../ROADMAP.md](../ROADMAP.md). If you
want to help but aren't sure where to start, open a discussion or a draft PR and
ask. See [../CONTRIBUTING.md](../CONTRIBUTING.md).
