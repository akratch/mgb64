# FID-0135 — fail-closed 4:3 fidelity capture contract

## Divergence

`platformSaveScreenshot()` always writes a 640x480 canvas. When the actual
readback source is not 4:3, `platformResampleFramebufferToScreenshot()` preserves
its aspect, uniformly rescales the scene, and adds black bars. That is a
reasonable player screenshot policy, but it is not a valid raw pixel source for
a 4:3 N64/ares reference.

The measured Dam establishing-shot repro already isolated the entire headline
band mismatch to this boundary: a default 1440x810 drawable produced 75-pixel
top/bottom bands, while a 4:3 drawable produced 20/20, exactly matching stock.
The 55-pixel difference was capture-added, not rendered. The existing one-shot
warning disclosed the problem but did not prevent automation from accepting the
invalid BMP.

The audit found two common unpinned callers:

- `tools/regression_test.sh` used an isolated savedir but inherited the product
  default 1440x810 window;
- `tools/movement_oracle_capture.sh` emitted a native screenshot for every route,
  while only two visual route JSON files happened to declare a 4:3 config.

Startup and Bunker pixel workflows already pinned 640x480, but had no runtime
assertion and did not explicitly pin the DPI setting.

The live negative control exposed a second part of the same source-boundary bug:
on Retina, `SDL_Metal_GetDrawableSize()` reported a 1280x720 CAMetalLayer for a
640x360 logical window even with `Video.HiDPI=0`. WebGPU readback does not read
that SDL-reported backing allocation; it reads `s_scene_tex`, whose dimensions
are controlled by `gfx_current_dimensions` and RenderScale. Asking WebGPU for
the SDL size could therefore request pixels outside the scene target and fill
the excess black. The fixed path queries the active Metal/WebGPU backend for its
exact readable target size; OpenGL continues to query its actual drawable.

## Contract

`GE007_FIDELITY_CAPTURE=1` is an opt-in automation mode at the shared platform
boundary. `platformSaveScreenshot()` resolves the exact readback-source size and then:

- accepts a 4:3 source and follows the unchanged readback/write path;
- rejects any non-4:3 source before GPU readback, leaving
  `s_lastScreenshotFailed=1`, so `--screenshot-exit` returns 4 and writes no BMP;
- leaves ordinary screenshots unchanged when the flag is absent, including the
  existing aspect-preserving widescreen capture and explicit warning.

Hardware-reference harnesses pin these values after route/ad-hoc overrides so
the contract cannot be shadowed:

```text
Video.WindowWidth=640
Video.WindowHeight=480
Video.WindowMode=windowed
Video.HiDPI=0
GE007_FIDELITY_CAPTURE=1
```

The common movement-oracle config audit includes the four persisted settings.
The regression, S2 pixel, startup visual, and Bunker brightness capture paths
use the same boundary.

## Permanent negative control

`tools/fidelity_capture_aspect_smoke.sh` runs three default-renderer boots:

1. legacy 640x360: exit 0, BMP present, capture-added-bars warning present;
2. strict 640x360: exit 4, no BMP, strict refusal and auto-capture failure logged;
3. strict 640x480: exit 0, BMP present, no refusal.

It is registered as `port_fidelity_capture_aspect_smoke`, so both removing the
platform rejection and breaking the permitted 4:3 WebGPU readback redden the
standard `port_` suite.

## Local golden preservation

The 60 pre-refresh machine-local regression artifacts were preserved before any
overwrite at `/tmp/mgb64_fid0135_baselines_pre.CB9cRj` (116 MiB; aggregate
content-manifest SHA-256
`43a9ca07a953411d191989d2ae701aec51e68b63d9d5062439268f3996f32e1d`).

After recapturing the accepted pre-FID-0135 build at its production-default
16:9 presentation, the regression comparison passed 20/20 with exact pixels,
179-frame state, audio, render-health, and spawn lanes. That intermediate set is
preserved at `/tmp/mgb64_fid0135_baselines_wide.uwE6IL` (aggregate
`293ca801b850af3e7ad66e2fd9ee78e1fafebff23d1ce46555f1dc651f272bb9`). It is the
clean source for attributing the final 4:3 migration, independently of the stale
July 17 artifacts.

Both aggregates use the recipe `(cd DIR && shasum -a 256 * | shasum -a 256)`.
Because `/tmp` does not survive a reboot, both sets are additionally archived at
`~/mgb64_fid0135_baseline_archive/{pre,wide}`; the copies reproduce the same two
aggregates. The wide set was verified file-identical to the `baselines/`
regression artifacts it was preserved from before the 4:3 re-record replaced
them.

## 4:3 migration and verification

The three-case negative control passed on the final build. The strict-wide
refusal diagnostic reports `source framebuffer 640x360` — the WebGPU scene
target — on a machine whose CAMetalLayer for that window is 1280x720, which is
the live proof that `gfx_backend_get_framebuffer_size()` (not the SDL drawable
query) is the capture-source authority.

`tools/regression_test.sh --baseline` then re-recorded all 20 levels under the
pinned contract, and the follow-up comparison run passed 20/20 across the
pixel, 179-frame state, audio, render-health, and spawn lanes.

Band attribution against the preserved wide set (rows from each edge whose max
channel <= 8, the same measure as the ledger evidence): Dam 75/75 -> 20/20,
level 34 75/75 -> 20/20, level 22 78/75 -> 24/20. The new 4:3 captures carry
only the game's own retail 10/240 viewport band (20 rows at 480), matching the
stock-ares reference geometry; the 55-pixel capture-added component is gone.
