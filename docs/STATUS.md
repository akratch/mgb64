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
  source-archive smoke. Hosted GitHub Actions is not a public-launch gate.
- The port is **asset-free**: no ROM media is compiled into the binary. All bulk
  game data — textures, audio, animation frames, fonts, the Rareware logo — is
  read from *your* ROM at runtime (`rom_io.c`) and served through the
  stub/segment layer (`src/platform/asset_stubs.c`, `segment_stubs.c`). The
  repository ships only first-party + code-coupled source (display lists,
  descriptor/offset tables, model-header glue), never ROM media.
- **Save persistence has smoke coverage:** deterministic solo completions are
  written to two save folders across separate processes, then reloaded from disk
  after a final process restart (`tools/save_persistence_check.sh`).
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
  Developer ID signing, notarization, a controlled SDL2 deployment target, DMG
  polish, and any prebuilt release artifact remain deferred.

## Known issues

- Graphical inaccuracies and occasional rendering glitches versus original
  hardware.
- Authored level intro camera parity is incomplete: on Dam, Bond is not visible
  during the initial establishing camera and only appears once the later swirl
  phase begins.
- Some gameplay/behavior differences and occasional instability or crashes.
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

For the fuller pre-public and contributor roadmap, see
[../ROADMAP.md](../ROADMAP.md). For the current public-launch decision matrix,
see [PUBLIC_LAUNCH_READINESS.md](PUBLIC_LAUNCH_READINESS.md). If you want to
help but aren't sure where to start, open a discussion or a draft PR and ask.
See [../CONTRIBUTING.md](../CONTRIBUTING.md).
