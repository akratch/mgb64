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
- Dam, Surface, and other large-room levels showed texture regressions after
  modern display/FOV work even though the same levels were playable.
- Some fog-heavy captures looked like missing texture detail when the immediate
  issue was actually fog-depth semantics.

## Root Causes

1. **Room alpha and prop glass used multiple render paths.**
   Dam glass was not one effect. Secondary room alpha, prop-type glass material
   classification, and bullet-crack decal placement all had to survive the
   native display-list path. The collision path was correct, which is why Bond
   could hit invisible panes while the visible alpha/decal work was still wrong.

2. **N64 texture-filter footprint was accidentally tied to modern framebuffer
   resolution.**
   The shader-side N64 filter uses screen derivatives to decide when to take the
   nearest branch versus the three-point path. Those derivatives come from the
   OpenGL framebuffer, so Retina/high render scale made the footprint smaller
   than it would be in the original VI/logical image. Close room surfaces then
   took the wrong branch and looked blocky or smeared. The default path now
   normalizes that derivative through `uN64FilterScale` in
   `src/platform/fast3d/gfx_opengl.c`.

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

## Guardrails

Use these habits before accepting renderer changes:

- Do not make `GE007_FORCE_POINT_FILTER`, `GE007_FORCE_LINEAR_FILTER`,
  `GE007_FORCE_ROOM_POINT_FILTER`, `GE007_DISABLE_N64_FILTER`, or
  `GE007_FOG_USE_LINEAR_DEPTH` default behavior. They are A/B probes.
- Treat renderer changes as suspect if they alter screenshot composition while
  a repo-root `ge007.ini` is changing `Video.FovY` or `Video.RenderScale`. For
  stock-ish captures, pass explicit config overrides.
- When reviewing texture-cache changes, check decoded row pitch and full-source
  footprint, not just visible width/height/format/address.
- When reviewing `G_SETTEX` changes, trace material clamp/shift/offset state
  with `GE007_TRACE_SETTEX_MATERIAL_CC='*'` before changing global sampler
  policy.
- Use fog traces before changing texture decode for distant low-detail scenes.

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

