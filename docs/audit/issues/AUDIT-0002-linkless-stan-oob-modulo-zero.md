# AUDIT-0002: OOB Recovery Divides by Zero on Linkless STAN Tiles

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S2 - data-dependent crash or undefined behavior |
| Priority | P2 |
| Area | Gameplay / collision / navigation |
| Evidence level | Shipped data reachable; source and analyzer proven |
| Confidence | High |
| Origin | Newly confirmed on 2026-07-12 |
| Affected configurations | Native builds when out-of-bounds recovery starts from a linkless valid tile |

## Summary

The player out-of-bounds recovery walk counts the outgoing links on the current
STAN tile, then unconditionally computes a random index modulo that count. The
shipped US stage data contains three valid polygon tiles with zero outgoing
links. If movement recovery enters this block while one of those tiles is
current and the player point lies outside its bounds, the divisor is zero.

This report does not claim that an ordinary playthrough has already hit the
trigger. It establishes that the zero count is not malformed hypothetical data:
it exists in the supported ROM and reaches an unconditional undefined
operation once the documented recovery precondition holds.

## Evidence

[`bondviewMovePlayerCollision`](../../../src/game/bondview.c#L9092) performs:

1. A link-count loop at lines 9096-9104.
2. `randomGetNext() % linked_tile_count` at line 9106 with no zero guard.
3. A second loop that selects the chosen link.

Clang's static analyzer reports division by zero on this path. A temporary
read-only census probe then enumerated every valid STAN tile in all 20 solo
stages. Seventeen stages contained no linkless tiles. The remaining valid tiles
were:

| Stage | Room | Tile | Points | Polygon center |
| --- | ---: | --- | ---: | --- |
| Cradle (41) | 17 | `10000c` | 3 | `(1019.60, 16.97, -103.23)` |
| Cradle (41) | 18 | `1003b7` | 3 | `(-1019.60, 16.97, 103.23)` |
| Aztec (28) | 8 | `0801e1` | 3 | `(2224.70, 110.48, -938.61)` |

The Cradle census covered 747 tiles and found two with zero links. The Aztec
census covered 1,560 tiles and found one. All census runs exited normally. The
temporary probe was removed after recording these aggregate results.

## Reproduction

A targeted harness can reproduce the failing operation without a full
playthrough:

1. Load Cradle or Aztec and select one of the tiles above as
   `current_tile_ptr`.
2. Put `collision_position` just outside that tile's polygon so
   `stanTestPointWithinTileBoundsMaybe` returns zero.
3. Call the movement/collision routine with recovery enabled.
4. Run under UBSan, or stop immediately before line 9106 and inspect
   `linked_tile_count`.

The count is zero and the current implementation evaluates unsigned remainder
with a zero divisor. Depending on compiler/runtime behavior this can trap,
signal, or be optimized unpredictably.

## Root Cause

The recovery algorithm assumes every valid tile has at least one navigation
link. That assumption is false for terminal or isolated polygons in shipped
stage data. The five-hop bound limits the walk length but does not validate the
candidate set before selecting a random member.

## Required End State

Before consuming RNG or taking a remainder, handle `linked_tile_count == 0` as
a terminal recovery failure. Restore the pre-move collision position and tile
captured in `rollback_collision_position` and `rollback_tile`, stop the recovery
walk, and continue through the routine's existing room-registration tail with a
coherent player state. Movement speeds or offsets that must be cleared should
use the same rollback policy already used for non-finite collision output.

For `linked_tile_count > 0`, preserve the current link ordering, the exact one
RNG call per attempted hop, selected-link calculation, and five-hop behavior.
The guard must not perturb normal retail-faithful simulation trajectories.

## Acceptance Criteria

- No remainder or division executes when a candidate tile has zero links.
- A targeted test using each of the three shipped tiles exits without a signal,
  restores a valid collision position/tile pair, and registers a valid room.
- The zero-link path consumes no RNG value.
- Linked-tile fixtures select the same link for fixed RNG values as before the
  fix and consume the same number of RNG values.
- UBSan and static analysis report no zero-divisor path in this routine.
- Deterministic movement and simulation hashes for representative ordinary
  stages are unchanged.

## Verification Plan

Extract the candidate-selection decision into a ROM-free helper only if doing
so can preserve call order exactly; otherwise test the routine through a narrow
fixture. Cover counts 0, 1, and several links, plus a five-hop failure. Add a
local ROM-backed regression that positions the player at each listed tile and
asserts the final tile, position, room registration, and RNG counter.

## Related Work

This is an inherited defensive gap unless retail assembly comparison proves a
port-specific divergence. The native fix must therefore be narrowly gated to
the impossible selection and leave the valid retail path byte-for-byte in
behavior.
