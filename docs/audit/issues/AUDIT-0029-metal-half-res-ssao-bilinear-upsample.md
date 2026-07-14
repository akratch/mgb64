# AUDIT-0029: Metal Half-Resolution SSAO Uses a Non-Depth-Aware Upsample

| Field | Value |
| --- | --- |
| Status | Deferred |
| Severity | S4 - an opt-in quality mode can bleed AO across depth silhouettes |
| Priority | P3 |
| Area | Metal renderer / SSAO composite |
| Evidence level | Source and mechanism proven |
| Confidence | High |
| Origin | Standardized from the prior monolithic audit and reconfirmed in current source |
| Affected configurations | Metal with `Video.SsaoMode=2` and `Video.SsaoHalfRes=1` |

## Summary

Metal's half-resolution SSAO is filtered bilaterally at AO resolution, but its
final expansion to the full-resolution scene is an ordinary linear sample with
no scene-depth comparison. Samples that straddle an object silhouette can
therefore blend foreground and background occlusion and create a faint edge
halo.

## Evidence

[`gfx_metal.mm`](../../../src/platform/fast3d/gfx_metal.mm) composites SSAO mode
2 with:

```metal
uSsaoTex.sample(ssaoSmp, in.vTexCoord).r
```

The comment explicitly identifies `ssaoSmp` as the linear half-resolution
upsampler. The sample uses only UV coordinates; it does not compare the
full-resolution scene depth with neighboring AO depths. The preceding blur is
depth weighted, but it runs within the smaller AO target and cannot prevent a
later bilinear footprint from crossing a full-resolution silhouette.

The option and SSAO mode are nondefault Advanced settings. A new natural-scene
screenshot pair was not captured during this audit; the source establishes the
filtering mechanism and its bounded configuration.

## Reproduction

On Metal, enable SSAO hemisphere mode and bilateral blur, then capture the same
high-contrast object silhouette with `Video.SsaoHalfRes=0` and `1`. Inspect a
one- to two-pixel band around the foreground edge. A synthetic depth step with
dark AO on only one side provides a deterministic ROM-free reproduction of the
linear cross-edge blend.

## Root Cause

The half-resolution optimization reuses the normal color-style linear sampler
for expansion instead of implementing a joint bilateral upsample keyed by the
full-resolution depth buffer.

## Required End State

Use a depth-guided upsample for half-resolution AO, selecting or weighting
nearby AO samples whose depths agree with the full-resolution destination.
Alternatively, fall back to full-resolution AO when a correct upsample is not
available. The full-resolution and SSAO-off paths must remain unchanged.

## Acceptance Criteria

- A synthetic depth discontinuity does not transfer AO to the opposite depth
  surface during upsampling.
- Half-resolution output retains a measurable performance benefit.
- Flat-depth regions remain smooth and visually equivalent to the current
  bilinear result.
- Metal full-resolution SSAO and OpenGL behavior are unaffected.
- Debug raw-AO output documents whether it shows pre- or post-upsample data.

## Verification Plan

Add a renderer test with a two-plane depth/AO fixture and assert bounded
cross-edge error. Capture representative indoor and outdoor silhouettes at
full and half resolution, compare edge crops and GPU timing, and run with blur
both on and off.

## Related Work

- [`RENDERER_SIM_AUDIT_2026-07-06.md`](../../RENDERER_SIM_AUDIT_2026-07-06.md)
  first recorded this quality defect.

## Deferral (verify-before-fixing triage 2026-07-14) <!-- triage-2026-07-14 -->

Metal is a deprecated macOS-only fallback now that WebGPU is the default backend; this half-res SSAO-upsample quality item does not affect the shipped default path.
