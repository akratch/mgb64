# AUDIT-0057: Metal Shadow Bias Copies OpenGL Constants Across Depth Formats

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S4 - opt-in sun-shadow acne or detachment can differ by backend |
| Priority | P3 |
| Area | Metal renderer / sun-shadow depth bias |
| Evidence level | Source and mechanism proven; visual impact unmeasured |
| Confidence | Medium |
| Origin | Standardized from the prior monolithic audit and reconfirmed in current source |
| Affected configurations | Metal with `Video.SunShadow=1` |

## Summary

The OpenGL and Metal shadow passes use the same slope and constant bias numbers,
but their shadow maps use different depth formats and API offset semantics.
Copying OpenGL's `units=4` directly to Metal's `depthBias=4` does not establish
equivalent normalized displacement, so backend acne versus peter-panning balance
has not been demonstrated to match.

## Evidence

[`gfx_opengl.c`](../../../src/platform/fast3d/gfx_opengl.c) allocates the shadow
map as `GL_DEPTH_COMPONENT24` and calls:

```c
glPolygonOffset(2.0f, 4.0f);
```

[`gfx_metal.mm`](../../../src/platform/fast3d/gfx_metal.mm) allocates
`MTLPixelFormatDepth32Float` and calls:

```objc
[enc setDepthBias:4.0f slopeScale:2.0f clamp:0.0f];
```

The slope constants intentionally mirror one another, but the constant term is
defined relative to backend depth representation and rasterization rules. A
shared numeric literal is not proof of shared effective bias. The receiver adds
a separate normalized `Video.SunShadowBias`, which bounds practical impact but
does not make the caster offsets equivalent.

No new same-scene acne/peter-panning capture was produced during this audit, so
this is a parity/tuning issue rather than a claimed observed artifact.

## Reproduction

Enable sun shadows on OpenGL and Metal with identical camera, shadow matrix,
resolution, receiver bias, and caster geometry. Capture the raw depth map and
lit output while sweeping shallow and steep surface angles. Measure caster
depth displacement and visible acne/detachment around contact points.

## Root Cause

The Metal port translated the OpenGL call parameter-for-parameter rather than
calibrating bias in a backend-neutral depth domain or validating equivalent
raster output.

## Required End State

Express the shadow caster bias in comparable normalized or texel/depth-slope
terms and derive backend-specific API values, or independently tune Metal from
objective same-scene measurements. Document why the final values are equivalent
enough across depth range, resolution, and device families.

## Acceptance Criteria

- Raw shadow depth displacement is measured on both backends.
- Contact-shadow acne and peter-panning stay within a defined parity tolerance.
- Tests include shallow/steep slopes, near/far shadow depths, and supported
  resolutions.
- Receiver `Video.SunShadowBias` retains the same user-facing meaning.
- Default sun-shadow-off rendering is unchanged.
- Final constants have a test or derivation rather than a copied literal.

## Verification Plan

Create a synthetic caster/receiver grid and capture raw depth plus final output
on GL and Metal while sweeping bias and resolution. Select backend values from
measured error, then verify representative level contact shadows on multiple
Apple GPUs.

## Related Work

- [`RENDERER_SIM_AUDIT_2026-07-06.md`](../../RENDERER_SIM_AUDIT_2026-07-06.md)
  originally recorded this uncalibrated format divergence.
