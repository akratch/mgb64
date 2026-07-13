# AUDIT-0009: Low-32 Pointer Registry Silently Evicts Live Mappings

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - latent dropped draw or unresolved native address |
| Priority | P3 |
| Area | Rendering / 64-bit address resolution |
| Evidence level | Source proven; natural collision not yet observed |
| Confidence | High for registry behavior; medium for player frequency |
| Origin | Standardized from renderer audits 2026-07-06 finding 4 and 2026-07-07 R12 |
| Affected configurations | 64-bit native paths that encode a host pointer as a 32-bit GBI token |

## Summary

The compatibility registry that maps truncated 32-bit GBI tokens back to host
pointers probes only four slots. If all four are occupied, insertion silently
overwrites the first slot, making the evicted live token unresolvable. Range
invalidation also writes an empty marker into the middle of a probe chain while
lookup stops at the first empty slot, hiding later unrelated mappings.

Most native display-list commands now carry full `uintptr_t` values, which
reduces exposure. Raw N64-style commands and texture/address compatibility paths
still call the registry, so silent loss remains a core rendering robustness
defect rather than dead code.

## Evidence

[`gfx_ptr_store`](../../../src/platform/gfx_ptr.h#L29) hashes the low 32 bits,
checks exactly `GFX_PTR_PROBE_MAX == 4` slots, then unconditionally replaces the
base slot at lines 43-45. No count, failure result, log, or liveness check
accompanies that eviction.

[`gfx_ptr_resolve`](../../../src/platform/gfx_ptr.h#L66) checks the same four
slots and returns NULL on a miss. A deterministic sequence of five distinct keys
with the same base index therefore makes the first mapping disappear.

[`gfx_ptr_invalidate_range`](../../../src/platform/gfx_ptr.h#L53) clears keys to
zero. Lookup treats zero as end-of-chain at line 74, so clearing an earlier slot
can prevent lookup from reaching a still-live mapping stored later in the same
probe window. The comment calls this a benign miss that re-resolves, but
`gfx_resolve_addr` itself only returns NULL; not every command family has a
secondary resolver.

The table is populated by
[`osVirtualToPhysical_pc`](../../../src/platform/platform_os.h#L509), and raw
N64 display-list matrix, vertex, nested-list, image, and move-memory operations
resolve 32-bit words through it in `gfx_pc.c`.

## Reproduction

This requires no ROM:

1. Zero the registry arrays.
2. Generate five nonzero 32-bit keys whose hash expression produces the same
   base index and associate them with five distinct synthetic full pointers.
3. Store all five, then resolve the first key.
4. The first lookup returns NULL because the fifth store overwrote it.
5. In a second fixture, store two colliding keys, invalidate the first pointer's
   range, and resolve the second. Lookup stops at the zeroed first slot and
   returns NULL.

The low-32 collision case is even more fundamental: two simultaneously live
full pointers with identical low 32 bits cannot be disambiguated from the token
alone, and the second store currently overwrites the first value.

## Root Cause

The registry combines a bounded linear-probe cache with the semantics of an
authoritative address map. Silent eviction is valid for a cache only when every
miss can reconstruct the value; several display-list consumers cannot. Its
deletion operation also lacks the tombstone or backward-shift behavior required
by open addressing.

## Required End State

Make native token resolution lossless for every live registration:

- Never evict a live mapping silently. Grow/rebuild the table, use a stable
  handle map, or return a checked insertion failure that prevents command
  emission.
- Preserve probe chains with tombstones or backward-shift deletion.
- Track occupancy, maximum probe distance, collisions, ambiguous identical
  low-32 tokens, and failed registrations in render-health diagnostics.
- Detect two live full pointers with the same low 32 bits. Do not overwrite and
  return one arbitrarily; migrate that command path to a full `uintptr_t` field
  or an explicit unique handle.
- Keep real N64 segment addresses distinct from native handles.

Long term, native `Gfx` producers should store full pointers wherever the
in-memory command layout permits it, leaving the registry only at explicit
32-bit binary boundaries.

## Acceptance Criteria

- Five or more same-bucket synthetic registrations all resolve to their original
  values with no eviction.
- Removing a mapping at the start or middle of a collision chain does not hide
  later live mappings.
- Duplicate registration of the same token/pointer is stable.
- Ambiguous same-low-32 live pointers are detected and fail closed; they never
  resolve to the wrong allocation.
- Key zero, table wraparound, range invalidation, and table growth have focused
  tests.
- All native display-list address producers are classified as full pointer,
  N64 segment address, or explicit handle.
- All-stage renderer smokes report zero unresolved live tokens and zero silent
  evictions.

## Verification Plan

Add a ROM-free unit test around the registry arrays and adversarial collision
sets. Instrument current all-stage runs before choosing a replacement to measure
actual occupancy and probe distribution. Then retain counters as release-mode
render-health telemetry with rate-limited diagnostics on any failure.

## Related Work

- Prior monolithic finding:
  [`RENDERER_SIM_AUDIT_2026-07-06.md` finding 4](../../RENDERER_SIM_AUDIT_2026-07-06.md).
- The older headline overstated wrong-buffer frequency. The directly proven
  actionable defects are silent four-slot eviction, broken deletion chains, and
  undetected identical low-32 ambiguity.
