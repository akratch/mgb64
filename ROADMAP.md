# MGB64 Roadmap

This roadmap tracks the work that matters most for public development. It is
intentionally practical: each item should either reduce legal/provenance risk,
improve playability, improve parity with original hardware, or make
contributions easier to review.

## Current Priorities

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
- the release checklist is run from a clean checkout before tagging releases or
  posting major announcements.

## High-priority parity work

### Split-screen multiplayer

GoldenEye's defining feature is local 2–4 player split-screen. The MP game logic,
per-player data model, and split-screen renderer are already decompiled and
running; the work is wiring multi-controller input and a deterministic MP launch
path on the native port. The full plan, phase tables, and acceptance gates live in
[docs/MULTIPLAYER_PLAN.md](docs/MULTIPLAYER_PLAN.md).

Landed / in progress (2-player focus):

- **Phase 0 telemetry + lanes (landed):** render-cost telemetry emits
  `rooms_drawn` (`g_BgNumberOfRoomsDrawn`) alongside `tris` in the port trace, and
  `tools/soak_stability.sh`, `tools/asan_smoke.sh`, and `tools/mp_smoke.sh` were
  added as headless deterministic lanes.
- **Phase 1 multi-controller input (landed):** the native port now opens every
  connected pad into a stable per-player slot, fills `data[1..3]` in
  `osContGetReadData`, and reports `joyGetControllerCount() >= 2` so the MP menus
  stop bouncing.
- **Phase 2 deterministic MP launch (landed):** `--multiplayer/--players N/
  --mp-stage/--scenario` plus `pc_apply_mp_selection()` direct-boot a split-screen
  match analogous to the solo `--level` path.
- **Phase 3a aim routing (landed):** mouse-look is gated to player 1 and
  right-stick aim is keyed off `get_cur_playernum()` so pad `k` aims player `k`.
- **Phase 4a `tools/mp_smoke.sh` (landed, green):** a headless 2-player Temple
  deathmatch boots, renders, and passes every gate — process, render-health,
  screenshot-health, and a per-player dissimilar-halves check (~97% delta between
  the two viewports, so a duplicated-camera bug fails). Three MP-only native
  crashes found during bring-up (missing per-player char load, an enum-`PROP`
  signedness OOB, and a raw N64 anim offset used as a pointer) were root-caused
  and fixed under `NATIVE_PORT` guards; the solo lanes remain green.
- **Phase 4b timer-boundary smoke (partial):** `--mp-timelimit SECS` gives the
  MP direct-boot path a deterministic short match length, and
  `tools/mp_smoke.sh --timelimit` proves the timer reaches the forced boundary
  crash-free. A latest local acceptance run reached `60/60` match ticks with
  about `98%` split-half dissimilarity. The remaining gap is the
  `mp_watch.c` scoreboard/results transition assertion.

Validation status:

- **2-player split-screen: green** in the deterministic `mp_smoke` window (boot +
  two distinct viewports + render-health clean, zero crashes).
- **Forced MP time limit: boundary green, scoreboard pending** in the
  deterministic `mp_smoke --timelimit` window.
- **4-player: boots and renders distinct viewports** in the `mp_smoke` window, but
  sustained frame-budget, the higher-risk 3-player asymmetric split, and a full
  end-of-round scoreboard run are **not** yet validated (Phase 4b/5).

Next:

- **Phase 5 — split-screen performance + 3/4-player hardening:** validate the
  2-player frame budget, then the higher-risk 3-player asymmetric split and the
  4-way viewport math, with `room_render_fallback_records==0` under load.
- **Parallel display/input polish track (default-off, config-gated):** display
  selection, window modes, exclusive fullscreen, VSync/frame cap, render
  scale/MSAA/gamma, retro filtering, and FOV are landed through the native
  settings schema. True hor+ widescreen, settings UI, and input rebinding remain
  open. The execution plan and regression matrix live in
  [docs/DISPLAY_INPUT_PLAN.md](docs/DISPLAY_INPUT_PLAN.md).

### Music and audio fidelity

SFX mapping and owner-slot playback are validated, and the native mixer has been
improved substantially. Remaining music work should now be driven by reference
captures, not only human listening.

Useful tools:

- `GE007_AUDIO_TRACE` for final PCM peaks/rail hits and SFX counters;
- `GE007_MUSIC_AUDIO_DUMP` for pre-SFX music PCM capture;
- `tools/compare_audio_reference.py` for spectral/envelope comparison against an
  emulator or hardware reference.

Current known issue:

- The startup intro/gunbarrel music still has a focused parity gap in the
  program-34-heavy windows around the Bond shooting-camera sequence. A local
  YouTube-derived reference and the local Ares audio dump agree closely enough
  to treat the port as the outlier. The current trace evidence points at the
  native custom-FX pole-filter HLE: changing pole-filter lane ordering improves
  the dull high-band loss in the gunbarrel windows, but causes adjacent
  over-bright regressions, so no lane/mask override has been promoted to the
  default. Continue from the `GE007_MUSIC_MIDI_TRACE_JSONL`,
  `GE007_AUDIO_FILTER_TRACE_JSONL`, and `GE007_AUDIO_POLE_TRACE_JSONL`
  diagnostics rather than hand-tuning by ear.

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

- transparent room/model glass now renders through the native secondary-alpha
  and prop-material paths, and glass bullet impacts use a surface-aligned crack
  path so edge-on panes do not swallow the decal. The Dam guard-tower probe in
  [docs/INSTRUMENTATION.md](docs/INSTRUMENTATION.md) is the regression hook;
- prop-attached bullet impacts outside glass still use the safe shade-only path
  by default;
- room/portal visibility culling is live on the native path: the `NATIVE_PORT`
  portal-BFS room-visibility walk runs by default (`GE007_PORTAL_BFS=0` is only a
  fall-back-to-frustum-all escape hatch). The per-room N64 projected scissor boxes
  are a separate fill-rate optimization that defaults off
  (`GE007_EXACT_ROOM_SCISSOR`) because the projected bounds can under-cover
  interior room seams on the PC path; this is a fill-rate option, not visibility
  culling, and disabling it does not draw more rooms;
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

### Local validation

Public validation cannot ship or depend on a ROM, but the local lanes should
still catch the mistakes public contributors are most likely to make.

Desired end state:

- release hygiene and no-ROM-data checks are easy to run before every PR;
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

Keep the public contributor path easy to use:

- keep GitHub Discussions available for contributor questions;
- keep private vulnerability reporting available when GitHub exposes it;
- keep labels such as `good first issue`, `audio`, `renderer`, `matching`,
  `provenance`, `macos`, `validation`, and `docs`;
- seed new issues from the roadmap with focused reproduction steps and expected
  validation commands.
