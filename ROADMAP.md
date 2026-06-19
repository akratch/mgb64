# MGB64 Roadmap

This roadmap tracks the work that matters most for a credible public release and
for new contributors. It is intentionally practical: each item should either
reduce legal/provenance risk, improve playability, improve parity with original
hardware, or make contributions easier to review.

## Launch blockers

The current go/no-go matrix lives in
[`docs/PUBLIC_LAUNCH_READINESS.md`](docs/PUBLIC_LAUNCH_READINESS.md). As of the
latest pre-public audit, the hard blockers are operational GitHub release gates,
not local source hygiene:

- hosted GitHub Actions must start and pass on the exact `main` launch commit;
- reachable public git history must not contain removed local-only matching-tool
  source; publish from a fresh single-root launch repository or approved history
  rewrite before flipping public;
- stale hidden `refs/pull/*` refs must be purged by GitHub Support or removed by
  replacing the GitHub repository from the clean single-root launch branch;
- branch protection and security settings must be configured once the repository
  state allows GitHub to expose those endpoints.

### Operational GitHub release gates

Desired end state:

- `scripts/check_github_launch_ready.sh` passes without `--allow-private`;
- the latest `main` CI run is green for the exact launch commit;
- `tools/check_public_history_paths.py` passes against the exact public git
  history;
- advertised `refs/heads/*` and `refs/tags/*` refs point only at commits inside
  current public history;
- `git ls-remote origin 'refs/pull/*'` exposes no commits outside current public
  history;
- public repository metadata, label, release note/asset, issue, comment,
  Discussion, workflow-history, and commit-reference surfaces expose no
  pre-public commit links or high-risk private/provenance text;
- contributor triage labels for audio, renderer, parity, validation,
  provenance, build, and newcomer-friendly work are present after the repository
  replacement/migration step;
- branch protection requires the release hygiene and Linux CMake build checks.

### SDK/libultra provenance cleanup

The public tree no longer tracks proprietary SDK notice text, but it still
contains inventoried Nintendo 64 SDK/libultra-lineage compatibility material for
the in-progress matching target. This is documented in `THIRD_PARTY.md`; it
remains the largest conservative provenance area.

Desired end state:

- native-port-only builds depend on clean-room compatibility declarations and
  implementations;
- unused SDK implementation source is removed from the public branch;
- any matching-target-only SDK interface that remains is documented with precise
  provenance and replacement status;
- `scripts/ci/check_release_ready.sh` fails if proprietary SDK notice text is
  reintroduced anywhere outside the release guard itself.

Good starting points:

- replace narrow native-port header dependencies under `include/PR/` beyond the
  already clean-room
  `include/PR/{R4300,abi,gbi,gu,libaudio,mbi,os,os_internal,rcp,region,ultratypes}.h`,
  top-level compatibility headers, internal libultra helper headers
  `src/libultra/{audio/synthInternals.h,gu/guint.h,io/viint.h}`,
  and the `NATIVE_PORT` forwarding path inside `include/PR/os.h`;
- inventory which SDK headers and non-audio implementation files are still
  required by the native path versus only by the N64 matching target.
- audit native-compiled game/system files that historically carried SDK/demo
  lineage comments, especially audio-manager and sound-player support code, and
  either prove they are decompiled original-game code or replace/isolate any
  external SDK example implementation. The current audit is recorded in
  `docs/PROVENANCE_AUDIT.md`. Direct SDK/devkit/demo source-path comments are
  now forbidden by the public contamination guard.

Current native CMake surface:

- the native target explicitly lists zero `src/libultra/audio/` C sources; this
  list is guarded so SDK-derived libultra audio cannot expand by wildcard;
- the native target no longer compiles SDK/Rare libaudio implementation files,
  and unbuilt split libaudio helper/wrapper files under `src/libultra/audio/`
  have been removed from the public tree. Native and matching-target builds
  share the clean-room implementation in `src/platform/audio_compat.c`; the
  matching target reaches it via `src/libultra/audio/clean_compat.c`;
- the native target now compiles zero `src/libultrare/audio/` C sources as well
  as zero `src/libultra/audio/` C sources;
- the native CMake targets no longer expose broad `src/libultra/`,
  `src/libultra/gu/`, or `src/libultrare/audio/` include directories. Native
  audio compatibility keeps only the narrow `src/libultra/audio/` include path
  for the clean-room `synthInternals.h` / `seqp.h` surface, and
  `tools/check_native_sdk_surface.py` guards this boundary;
- the matching-target makefile now routes the libaudio C surface through
  `src/libultra/audio/clean_compat.c`, a notice-free wrapper around the
  project-owned `src/platform/audio_compat.c`, instead of compiling the
  historical notice-bearing libaudio C files;
- the matching-target linker section scripts now route the libaudio object
  surface through `src/libultra/audio/clean_compat.o` only. The SDK inventory
  guard fails if historical SDK/Rare libaudio object references are
  reintroduced there;
- most matching-target GU C helpers
  `src/libultra/gu/{align,cosf,coss,lookat,lookatref,mtxutil,normalize,ortho,perspective,rotate,scale,sinf,sins,translate}.c`
  and `src/libultra/io/viblack.c` are now clean-room compatibility sources,
  while the native executable continues to use the clean-room
  `src/platform/gu_trig.c` replacement for fixed-angle trig;
- `include/PR/` headers remain broadly included by both native and matching
  paths, but `include/PR/{R4300,abi,gbi,gu,libaudio,mbi,os,os_internal,rcp,region,ultratypes}.h`
  and the top-level compatibility headers
  `include/{assert,bstring,limits,math,sgidefs,stddef,stdlib,svr4_math}.h` are now
  clean-room declarations/constants, as are internal libultra helper headers
  `src/libultra/{audio/synthInternals.h,gu/guint.h,io/viint.h}`.
  The `NATIVE_PORT` branch of `include/PR/os.h` forwards to the native platform
  surface without overriding native OS/address macros. Native `include/PR/R4300.h`
  address macros remain guarded as
  fallbacks so they cannot clobber the platform address-conversion surface.
  These native surfaces are guarded against notice and macro-regression mistakes.
- `tools/check_sdk_inventory.py` is the explicit public inventory guard for the
  remaining SDK-shaped compatibility directories. It fails release validation if
  any tracked file under `include/PR/`, `src/libultra/`, or `src/libultrare/`
  appears outside the reviewed inventory, if the inventory still names a file
  that has been removed, or if matching-target linker scripts point at removed
  SDK/Rare libaudio objects. This prevents the matching-target provenance
  surface from growing silently.

### Public-claim alignment

The README, release notes, status docs, and macOS docs must describe only what is
actually wired today. In particular, do not claim a packaged macOS `.app`, signed
binary, DMG, or first-launch ROM picker release until the Swift app bundle is
built and verified end to end.

Desired end state:

- release notes, `PORT.md`, `docs/STATUS.md`, and `macos/README.md` agree;
- every public status claim has a local command or document backing it;
- the release checklist is run from a clean checkout before the repository is
  flipped public.

## High-priority parity work

### Music and audio fidelity

SFX mapping and owner-slot playback are validated, and the native mixer has been
improved substantially. Remaining music work should now be driven by reference
captures, not only human listening.

Useful tools:

- `GE007_AUDIO_TRACE` for final PCM peaks/rail hits and SFX counters;
- `GE007_MUSIC_AUDIO_DUMP` for pre-SFX music PCM capture;
- `tools/compare_audio_reference.py` for spectral/envelope comparison against an
  emulator or hardware reference.

Desired end state:

- startup music has stable segmented comparison metrics against a reference;
- remaining high-band/reverb deltas are tied to specific ABI commands or mixer
  stages;
- at least one reference-capture procedure is documented well enough for a new
  contributor to reproduce.

### Renderer parity

The renderer is playable and uses compatibility defaults for several N64-specific
paths. The highest-value work is narrow, reference-backed fixes rather than broad
renderer rewrites.

Current known areas:

- prop-attached bullet impacts use the safe shade-only path by default;
- per-room N64 scissor boxes are disabled by default because they can expose PC
  renderer seams;
- sky rendering falls back to fog color in some cases;
- a frontend/menu material has a small brightness residual versus stock.

Desired end state:

- each renderer compatibility default has a small reference scene and diagnostic
  toggle;
- exact paths can be promoted only when they no longer corrupt adjacent state;
- `tools/compare_screenshots.py` or a successor can be used for repeatable visual
  A/B checks.

### Level intro and frontend parity

The native port has had several frontend-vs-direct-level differences fixed, but
intro camera parity is still incomplete. Bond is still absent during Dam's early
authored establishing camera and appears only during later swirl-style sequences.

Desired end state:

- start-menu and `--level` entry paths produce the same relevant gameplay state
  after level start;
- intro camera scripts render the same actors as original hardware/reference
  captures;
- validation covers both direct-level and menu-start entry paths.

## Build and release infrastructure

### N64 byte-matching target

The native port is the supported play target today. The N64 matching target still
needs local, user-supplied SGI IDO/IRIX compiler files for the matching
toolchain, plus extracted animation/image-table data wired into the link using
the existing `.incbin` pattern.

Desired end state:

- a clean checkout plus a user-provided ROM and documented local matching
  toolchain inputs can rebuild the target N64 ROM;
- `COMPARE=1 make` verifies the expected SHA-1 for supported regions;
- matching-target gaps are listed by file/function instead of described broadly.

### CI and validation

Public CI cannot use a ROM, but it should still catch the mistakes public
contributors are most likely to make.

Desired end state:

- release hygiene and no-ROM-data checks run on every PR;
- Python and shell validation tools get syntax checks;
- Linux CMake configure, GCC build, and ROM-free CTest remain green without
  extracted assets;
- optional maintainer-only local lanes cover spawn health, save persistence,
  audio reference comparisons, and representative screenshot comparisons.

### macOS distribution

The Swift/AppKit app shell sources build into a local unsigned `.app` via
`macos/Scripts/build_app_bundle.sh`, and `macos/Scripts/verify_asset_free.sh`
passes against the packaged app. The local bundle links against the builder's
SDL2 install and is meant for source builds, not as a redistributable binary.

Desired end state:

- signed/notarized Developer ID builds are wired for maintainers;
- the SDL2 runtime/deployment target is controlled so signed builds do not
  inherit an unexpectedly high minimum macOS version from a local Homebrew
  dylib;
- DMG creation is verified against the signed app bundle;
- any prebuilt macOS release artifact passes asset-free verification before it
  is attached to a release.

## Contributor on-ramps

Before a broad public announcement:

- enable GitHub Discussions;
- enable private vulnerability reporting;
- add labels such as `good first issue`, `audio`, `renderer`, `matching`,
  `provenance`, `macos`, `validation`, and `docs`;
- seed issues from the roadmap with focused reproduction steps and expected
  validation commands.
