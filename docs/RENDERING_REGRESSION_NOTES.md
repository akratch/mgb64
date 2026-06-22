# Renderer Regression Notes

This note records the texture/fog/glass regressions that were chased during the
June 2026 playability pass. It is meant to keep future renderer work from
reintroducing the same failure modes.

Generated screenshots, traces, contact sheets, emulator logs, and ROM-derived
captures must stay local. Do not commit artifacts from `/tmp/mgb64_*`.

## What Failed

The visible symptoms were level-specific but shared renderer causes:

- Dam glass collision still worked while the visible glass and bullet crack
  decals disappeared, rendered in the wrong place, or only appeared during the
  authored intro.
- Cradle could crash during startup after animation/model setup and later
  showed severe texture smearing and blocky room surfaces.
- Dam, Surface, Bunker, Depot, and other large-room or sky-heavy levels showed
  texture regressions after modern display/FOV work even though the same levels
  were playable.
- Some fog-heavy captures looked like missing texture detail when the immediate
  issue was actually fog-depth semantics.

## Root Causes

1. **Room alpha and prop glass used multiple render paths.**
   Dam glass was not one effect. Secondary room alpha, prop-type glass material
   classification, and bullet-crack decal placement all had to survive the
   native display-list path. The collision path was correct, which is why Bond
   could hit invisible panes while the visible alpha/decal work was still wrong.

2. **N64 texture-filter threshold policy was too aggressive for clamped room
   materials.**
   The shader-side N64 filter uses screen derivatives to decide when to take the
   nearest branch versus the three-point path. Those derivatives come from the
   OpenGL framebuffer, so Retina/high render scale made the footprint smaller
   than it would be in the original VI/logical image. The default path
   normalizes that derivative through `uN64FilterScale` in
   `src/platform/fast3d/gfx_opengl.c`, but a later `0.05` threshold override for
   clamped, non-texture-edge materials pushed close room textures into constant
   three-point filtering. Bunker's yellow warning sign was the clearest probe:
   the `0.05` path smeared it, while the normal `1.0` threshold restored texel
   structure without forcing blanket point sampling.

3. **Decoded texture source footprint is part of cache identity.**
   The same visible tile dimensions can come from packed `LOADBLOCK` data or a
   strided `LOADTILE`/sub-rect source. Caching only the visible upload dimensions
   lets a later material reuse bytes decoded with the wrong row pitch, producing
   row smear while sampler state and source addresses look plausible. The native
   texture path tracks decoded line size, full-image line size, and decoded
   footprint in `src/platform/fast3d/gfx_pc.c`.

4. **Rare `G_SETTEX` can coexist with stale ordinary tile state.**
   Texture-by-number materials need clamp/wrap/shift/offset information from the
   active `G_SETTEX` command, not whatever ordinary tile descriptor happened to
   be live. If stale clamp bits survive, repeated room coordinates clamp to an
   edge row or column and stretch that across a surface. The renderer now derives
   `G_SETTEX` tile state from the active command and treats stale ordinary tile
   state as diagnostic evidence, not the source of truth.

5. **Fog can mimic texture failure.**
   GoldenEye's fog values are interpreted through the N64-style projected depth
   path, not a simple world-distance ramp. Cradle's narrow fog range is a good
   example: forcing linear depth can make distant geometry look clearer, but
   that is a negative control, not the parity default.

6. **Modern scene targets can clobber cached GL texture state.**
   `Video.RenderScale > 1` and MSAA render through an offscreen scene target.
   FBO setup temporarily binds the scene color texture on GL texture unit 0. If
   that binding is not restored, Fast3D's cached sampler state still believes the
   current game texture is live, but the GPU samples the scene target instead.
   The failure looks like sky/cloud or texture detail disappearing only under
   RenderScale/MSAA; the same capture at `Video.RenderScale=1` still looks
   correct. Scene-target setup in `src/platform/fast3d/gfx_opengl.c` must restore
   active texture and unit-0 binding before queued triangles flush.

7. **Validation config can masquerade as renderer state.**
   The native binary writes `ge007.ini` on clean exit, including env-applied
   settings. If a smoke run reuses an artifact directory or lets the default
   1440x810 window drive captures on a high-DPI display, later attempts can
   render through a much larger scene target than intended. That mostly changes
   sky/backdrop sampling and can look like a texture regression in pixel deltas.
   The playability harness now pins a 640x480 validation window by default and
   records any explicit config overrides in `summary.json`.

8. **Sky UV scale is a calibrated repeat factor, not a raw passthrough.**
   GoldenEye's sky coordinates arrive in world-repeat space and then pass
   through the native VBO texture packing path. Defaulting `GE007_SKY_UV_SCALE`
   to `1.0` stretched the cloud texture into broad bands on Depot, Surface,
   Frigate, Cradle, and other sky-heavy levels. The native default is `3.5`,
   which restores the dense cloud repeat seen in the older renderer path while
   keeping the split-screen viewport-aware sky draw.

9. **Texture attribution must use stable `G_SETTEX` texture numbers.**
   `settex_gl_tex_id` is an OpenGL upload id and changes with cache/upload
   order. Using it for `GE007_TINT_TEX` or `GE007_SKIP_TEX` made focused probes
   point at the wrong material after unrelated renderer changes. The tint/skip
   path now matches `settex_texturenum`, which is the game texture number logged
   by `GE007_TRACE_SETTEX_MATERIAL_CC`. Runway's foreground is the practical
   example: texture 22 and texture 1267 can both overlap the same screenshot
   band, so crop-level color metrics are not enough without texture-number tint
   and material traces at the actual capture frame.

## Guardrails

Use these habits before accepting renderer changes:

- Do not make `GE007_FORCE_POINT_FILTER`, `GE007_FORCE_LINEAR_FILTER`,
  `GE007_FORCE_ROOM_POINT_FILTER`, `GE007_DISABLE_N64_FILTER`, or
  `GE007_FOG_USE_LINEAR_DEPTH` default behavior. They are A/B probes.
- Treat renderer changes as suspect if they alter screenshot composition while
  a repo-root `ge007.ini` is changing `Video.FovY` or `Video.RenderScale`. For
  stock-ish captures, pass explicit config overrides.
- Before trusting a visual comparison, run the renderer from a build that
  matches the branch under review. Local untracked `.c` files are picked up by
  the CMake source glob, so use a clean worktree or check
  `git ls-files --others --exclude-standard 'src/**/*.c'` first.
- Keep automated renderer captures on the pinned 640x480 validation window
  unless the test is specifically exercising user-window/high-DPI behavior.
  Use `--config-override Video.RenderScale=2` or `Video.MSAA=4` for targeted
  scene-target probes, and keep the overrides visible in the artifact summary.
- When using an older checkout or `main` binary as a visual reference, verify
  that it actually honors the same config path. Older binaries can ignore
  `--config-override`, fall back to a 1440x810 default window, and make a
  healthy renderer look zoomed or composition-shifted when compared to the
  pinned 640x480 branch captures. Either compare two pinned builds or compare
  both with default overrides disabled. The current `playability_smoke.sh`
  audits generated `ge007.ini` values after each run so this fails fast for the
  normal validation lane.
- When reviewing texture-cache changes, check decoded row pitch and full-source
  footprint, not just visible width/height/format/address.
- When reviewing `G_SETTEX` changes, trace material clamp/shift/offset state
  with `GE007_TRACE_SETTEX_MATERIAL_CC='*'` before changing global sampler
  policy.
- When isolating a suspect `G_SETTEX` material, use
  `GE007_TINT_TEX=min:max` or `GE007_SKIP_TEX=min:max`. These match stable game
  texture numbers, not transient GL texture ids.
- Do not add special low nearest thresholds for clamped non-texture-edge room
  materials. Use
  `GE007_DIAG_N64_FILTER_CLAMPED_NON_TEXEDGE_NEAREST_THRESHOLD=N` to A/B that
  class; `0.05` is a known Bunker close-texture smear regression, and the
  default `1.0` threshold is the validated baseline.
- Keep `GE007_DIAG_DISABLE_SHADER_CLAMP=1` as a negative control only. It is
  useful for proving clamp policy is involved, but a Runway pass showed it does
  not explain every apparent material color delta.
- Treat `GE007_SKY_UV_SCALE=1.0` as a negative control. It is useful for proving
  sky UV bugs, but it is not the calibrated native default.
- Use fog traces before changing texture decode for distant low-detail scenes.
- When a visual change appears only with `Video.RenderScale > 1` or MSAA, first
  compare against `Video.RenderScale=1`, then audit raw GL state restored by the
  scene-target/output-filter helpers. Do not assume the material trace is wrong;
  queued VBOs can be correct while the GL binding they flush against is stale.

## Validation

The broad native acceptance lane is:

```sh
./tools/playability_smoke.sh --all --no-build \
  --rom /path/to/baserom.u.z64 \
  --binary build/ge007
```

That run direct-boots the 20 supported solo stages, drives deterministic
gameplay input, audits movement/render counters, audits screenshot health, and
writes a `contact_sheet.png` for manual visual review. It proves the levels
boot, move, render nonblank frames, and avoid known render-health counters; it
does not prove hardware-perfect output.

For branch-vs-reference visual comparisons, first confirm the captures have the
same viewport/composition. A high changed-pixel percentage is expected if one
binary was captured through the pinned 640x480 validation window and the other
used the old 1440x810 default. Once the aspect is matched, filter/sky/fog
changes should be reviewed by region and with material traces rather than by
whole-frame changed-pixel percentage alone.

`playability_smoke.sh` pins `Video.WindowWidth=640`,
`Video.WindowHeight=480`, and `Video.WindowMode=windowed` by default so
generated `ge007.ini` state and high-DPI desktop geometry do not change the
capture lane. For scene-target validation, add explicit overrides:

```sh
./tools/playability_smoke.sh --all --no-build \
  --rom /path/to/baserom.u.z64 \
  --binary build/ge007 \
  --config-override Video.RenderScale=2 \
  --config-override Video.FovY=60
```

For smaller renderer A/B captures:

```sh
./tools/renderer_parity_capture.sh --no-build \
  --rom /path/to/baserom.u.z64 \
  --binary build/ge007
```

For ROM-backed movement/intro parity, the public stock-oracle route set is
currently Dam-focused. `stock_level` in a route gates and audits the target raw
`LEVELID`; it does not navigate the stock frontend to arbitrary missions by
itself. Add a verified stock menu route or direct-stage hook before claiming
stock-backed parity for another level.

When an instrumented ares binary is available, `movement_oracle_capture.sh`
also asks stock ares to dump a local PPM framebuffer and health-checks that
screenshot. Use that as the first emulator-pixel sanity check before drawing
conclusions from native-only captures. For routes that spend extra emulator time
in menus, set `stock_screenshot_frame` to the gameplay-equivalent frame instead
of trusting the final process frame; the Dam forward route uses this to avoid
capturing the mission-failed report screen. The stock screenshot is generated
from the user's ROM and must remain local.

## Manual Priority List

After the automated run is green, manually inspect these first because they
exercise the failure modes above:

- Dam: guard-tower glass, bullet crack decals, truck body and wheels, outdoor
  rock/road textures.
- Surface 1/2: large snow fields, sky/fog boundary, close wall texture filtering.
- Cradle: long bridge texture stability, fog depth, guard faces/weapons, no
  startup crash.
- Silo, Depot, Train, Caverns: repeated room textures, long corridors, large
  flat surfaces, and alpha/transparency edges.
