# AUDIT-0030: Metal Texture Uploads Lack a Discrete-GPU Private-Storage Path

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S4 - potential discrete-GPU sampling inefficiency without measured regression |
| Priority | P3 |
| Area | Metal renderer / texture residency |
| Evidence level | Source proven; performance impact unmeasured |
| Confidence | Medium |
| Origin | Standardized from the prior monolithic audit and reconfirmed in current source |
| Affected configurations | Metal on supported Macs with non-unified GPU memory |

## Summary

Every uploaded game texture is permanently allocated with
`MTLStorageModeShared` and populated by `replaceRegion`. This is appropriate on
unified-memory Apple Silicon, but the renderer has no upload-to-private path for
older supported Intel Macs with discrete GPUs, where shader-read textures may
sample more efficiently from private GPU storage.

## Evidence

[`gfx_metal.mm`](../../../src/platform/fast3d/gfx_metal.mm) creates each normal
RGBA game texture in `mtl_upload_texture` with shader-read usage and
`MTLStorageModeShared`, then writes it using `replaceRegion`. There is no device
memory-architecture branch, staging buffer, or blit into a private texture.
Long-lived built-in lookup textures follow the same pattern.

The project still supports desktop macOS builds beyond Apple Silicon, including
universal builds. No audit benchmark on a discrete Intel Mac was available, so
this report records an optimization opportunity rather than a demonstrated
frame-rate regression.

## Reproduction

On a Mac with a discrete Metal GPU, instrument texture allocation and compare a
representative level using the current Shared path against a staging-buffer to
Private implementation. Record upload time, GPU frame time, texture memory,
and any synchronization stalls after the cache is warm.

## Root Cause

The initial upload implementation chose the simplest storage mode that works on
all Apple devices and did not specialize immutable shader-read resources by the
device's memory architecture.

## Required End State

Make storage policy device aware and evidence driven. Retain Shared storage on
unified-memory devices. On non-unified devices, use staged blits to Private
storage only if benchmarks show a meaningful warm-frame gain without excessive
upload latency, memory growth, or synchronization complexity. If no gain is
measurable, close the issue with recorded data rather than adding complexity.

## Acceptance Criteria

- The renderer identifies unified versus non-unified memory using a supported
  Metal device capability.
- Apple Silicon behavior and texture output remain unchanged.
- A discrete-GPU policy is backed by before/after measurements on real hardware.
- Upload failure leaves the prior cache entry valid and never binds a partial
  texture.
- Cache eviction releases any staging and private resources correctly.
- Pixel-identity tests pass for stock and HD replacement textures.

## Verification Plan

Build a texture-upload microbenchmark plus a full-level warm-cache trace on one
unified and one discrete-memory Mac. Compare CPU upload cost, GPU sampling time,
peak resident memory, and screenshots before selecting Shared or Private per
device.

## Related Work

- [`RENDERER_SIM_AUDIT_2026-07-06.md`](../../RENDERER_SIM_AUDIT_2026-07-06.md)
  originally raised the device-local texture opportunity.
