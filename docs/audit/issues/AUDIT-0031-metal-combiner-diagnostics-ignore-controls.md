# AUDIT-0031: Metal Combiner Diagnostics Ignore OpenGL Runtime Controls

| Field | Value |
| --- | --- |
| Status | Deferred |
| Severity | S4 - backend A/B diagnostics silently test different shader behavior |
| Priority | P3 |
| Area | Fast3D diagnostics / Metal shader generation |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Standardized from two prior monolithic findings and reconfirmed in current source |
| Affected configurations | Metal runs using nondefault `GE007_DIAG_*` combiner controls |

## Summary

Several combiner diagnostics used for renderer parity work are only fully
implemented by OpenGL. Metal ignores `GE007_DIAG_QUANTIZE_COMBINER` entirely and
bakes default constants for four adjustable diagnostic values, so an A/B run
with a nondefault control compares different experiments across backends.

## Evidence

[`gfx_opengl.c`](../../../src/platform/fast3d/gfx_opengl.c) reads
`GE007_DIAG_QUANTIZE_COMBINER` and emits 8-bit combiner-result quantization.
No corresponding flag or quantization branch exists in
[`gfx_metal.mm`](../../../src/platform/fast3d/gfx_metal.mm).

OpenGL also reads and formats runtime values for:

- `GE007_DIAG_SETTEX_CC_COLOR_SCALE_VALUE`
- `GE007_DIAG_SETTEX_CC_ALPHA_SCALE_VALUE`
- `GE007_DIAG_ALPHA_FROM_TEX_INTENSITY_MIX`
- `GE007_DIAG_XLU_COVERAGE_WRAP_THIN_RATE`

Metal's shader generator instead bakes their defaults: 1.02, 1.0, 1.0, and
0.25. The backends agree only when the value overrides stay at default. Metal's
separate `GE007_DIAG_OUTPUT_*` post-processing controls are implemented and are
not part of this defect.

## Reproduction

Generate otherwise identical OpenGL and Metal shaders with each affected
diagnostic enabled and its value set away from default. Inspect the generated
source or render a synthetic combiner fixture. OpenGL contains the requested
constant or quantization operation; Metal contains the baked default or no
quantization operation.

## Root Cause

Metal reimplemented diagnostic feature bits but copied default literals into
MSL generation instead of sharing the frontend's resolved diagnostic values.
The quantization diagnostic was omitted altogether.

## Required End State

Resolve diagnostic controls once in backend-neutral code and pass them into
both shader generators or a shared uniform/feature descriptor. Unsupported
diagnostics must fail explicitly rather than silently running a different
experiment. Default-off normal rendering must remain byte-for-byte unaffected.

## Acceptance Criteria

- Quantize-combiner emits equivalent 8-bit rounding in GLSL and MSL.
- Every listed nondefault value reaches both backends exactly.
- Generated shader cache keys account for compile-time diagnostic values, or
  the values are supplied safely as uniforms.
- Unsupported values produce a clear error instead of silent fallback.
- A diagnostic matrix test compares output from both backends.
- With all diagnostic flags unset, shader output and performance are unchanged.

## Verification Plan

Add a ROM-free synthetic combiner suite that renders fixed inputs through both
backends for default, quantized, and two nondefault values per control. Compare
pixel output within an explicitly justified tolerance and inspect generated
shader source in a backend contract test.

## Related Work

- [`RENDERER_SIM_AUDIT_2026-07-06.md`](../../RENDERER_SIM_AUDIT_2026-07-06.md)
  originally recorded the missing quantization and hardcoded-value findings.

## Deferral (verify-before-fixing triage 2026-07-14) <!-- triage-2026-07-14 -->

Metal fallback-only; the combiner-diagnostic knobs are GL/Metal-specific and out of scope for the default WebGPU backend.
