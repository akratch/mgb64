# AUDIT-0001: OpenGL Silently Skips All Scene-Decor Meshes

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S2 - promised feature unavailable |
| Priority | P2 |
| Area | Rendering / OpenGL / scene decoration |
| Evidence level | Runtime reproduced |
| Confidence | High |
| Origin | Newly confirmed on 2026-07-12 |
| Affected configurations | Windows, Linux, and macOS when using the default OpenGL backend |

## Summary

The player-facing `Video.SceneDecor` setting loads and submits the committed
decor assets, but the OpenGL rendering API has no modern-mesh draw callback.
The display-list interpreter warns once and drops every submitted mesh. OpenGL
is the only backend on Windows and Linux and is the default backend on macOS, so
the setting has no visual effect for the default configuration on every desktop
platform.

## Evidence

- [`gfx_opengl_api`](../../../src/platform/fast3d/gfx_opengl.c#L4153) assigns
  `NULL` to `draw_modern_mesh` at line 4176 and labels it unsupported.
- [`gfx_handle_modern_mesh`](../../../src/platform/fast3d/gfx_pc.c#L22138)
  returns without drawing when that callback is absent and prints that modern
  decor models are skipped.
- [`decorRender`](../../../src/game/decor_native.c#L177) emits one
  `G_MODERNMESH` command per modern primitive and instance. There is no legacy
  fallback for `m->modern` models.
- [`platformRegisterConfig`](../../../src/platform/platform_sdl.c#L2066)
  registers `Video.SceneDecor` as a live player-facing setting and describes it
  as adding real trees and props.
- [`gfx_init`](../../../src/platform/fast3d/gfx_pc.c#L23883) selects OpenGL on
  every non-Apple build. On Apple it also selects OpenGL unless Metal was
  explicitly requested.
- The Metal backend implements the missing operation in
  [`mtl_draw_modern_mesh`](../../../src/platform/fast3d/gfx_metal.mm#L3901),
  demonstrating that the command and asset path are otherwise complete.

In a deterministic Surface 1 capture with the committed `assets/decor` data,
the enabled run reported 8 models, 45 instances, and 3,046,833 submitted
triangles per frame, followed by:

```text
[DECOR] WARN modern-mesh draw not supported by this renderer backend; modern decor models are skipped
```

The enabled capture and a clean disabled control had the exact same SHA-256:

```text
624aa5c0556768518a735735e8a4d17843b204471f8249033479654bac19e197
```

That byte identity rules out a merely subtle or off-camera visual difference in
the tested viewpoint.

## Reproduction

1. Use a clean save/config directory and the OpenGL backend.
2. Direct-boot Surface 1 with deterministic frozen input and
   `Video.SceneDecor=1` or `GE007_SCENE_DECOR=1`.
3. Capture a fixed frame and retain stderr.
4. Repeat from another clean directory with scene decor disabled.
5. Compare the two images and inspect stderr.

The enabled run loads and emits decor, prints the unsupported-backend warning,
and produces the same image as the disabled run.

## Root Cause

The shared rendering API made modern meshes optional, and only Metal supplied
the implementation. Capability absence is handled as a diagnostic skip rather
than being reflected in settings availability or falling back to an OpenGL
implementation. The asset loader and command emitter therefore succeed while
the last backend dispatch discards the work.

## Required End State

Implement `draw_modern_mesh` in OpenGL with the same observable contract as the
Metal path. It must support the committed float vertex/index layout, RGBA
textures with mipmaps, repeat sampling, alpha-cutout primitives, two-sided
cards, the interpreter-provided MVP and fog curve, depth test/write behavior,
and display-list ordering. Cache and release GPU resources without re-uploading
every instance or leaking them across level changes.

Backend capability must also be explicit. Until OpenGL support is present, the
setting must be unavailable or clearly disabled on that backend; silently
accepting a setting that cannot render is not an acceptable interim state.

The faithful/default `Video.SceneDecor=0` path must remain behaviorally and
visually unchanged and must not alter simulation state or RNG use.

## Acceptance Criteria

- OpenGL provides a non-NULL `draw_modern_mesh` callback on supported builds.
- Surface 1 with decor enabled no longer logs the skip warning.
- A fixed enabled Surface 1 capture differs from its disabled control in the
  expected decor regions.
- Golden viewpoints on OpenGL and Metal contain the same instances, primitive
  classes, texture orientation, alpha cutouts, fogging, depth relationships,
  and draw order within a documented image tolerance.
- Opaque and cutout models render correctly with and without MSAA and post-FX.
- Repeated level loads do not grow modern-mesh GPU allocations.
- `Video.SceneDecor=0` retains the existing image and simulation hashes.
- Windows, Linux, and macOS OpenGL smoke tests complete without renderer errors.

## Verification Plan

Add a backend contract test around modern-mesh capability plus an image test
using a small repository-owned synthetic mesh. Keep the ROM-backed Surface 1
OpenGL/Metal comparison in the local fidelity capture suite. Record draw counts
and resource-cache counts so an image success cannot hide skipped commands or
per-frame uploads.

## Related Work

- The architecture and committed assets are described in
  [`10-asset-replacement-architecture.md`](../../design/remaster-aaa/10-asset-replacement-architecture.md).
- This is not a retail parity defect when scene decor is off; it is a native
  feature/backend contract defect.
