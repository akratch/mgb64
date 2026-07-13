# AUDIT-0017: Four Campaign Routes Still Encode Pre-FID-0117 Root Motion

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - a configured campaign gate is red against the retail-correct simulation |
| Priority | P1 |
| Area | Validation / campaign route fixtures |
| Evidence level | Runtime A/B reproduced across all four failing routes |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | `PORT_VALIDATION_TESTS=ON`; default native simulation after FID-0117 |

## Summary

The campaign-route gate was not recalibrated after FID-0117 corrected the
character root-motion seed flag. Four routes retain input timing, distance
thresholds, and interaction checkpoints recorded against the old motion. The
full CTest suite therefore fails even though the same checkout's dedicated
FID-0117 tapes accept and document the new retail-correct behavior.

This is not evidence that FID-0117 should be reverted. All four routes return
to their exact prior passing trajectories under its explicit negative-control
flag.

## Evidence

CTest test 76, `port_campaign_route_smoke`, ran 34 routes. Thirty passed and
these four failed under the default simulation:

| Route | Default result | FID-0117 opt-out result |
| --- | --- | --- |
| `surface1_native_multiwaypoint_input_traversal` | 6920.53 horizontal units; required 7500 | PASS at 7776.69 |
| `bunker1_spawn_two_door_collect_contract` | door 138 starts moving but never records `finish open` | PASS with the required finish event |
| `frigate_native_multiwaypoint_input_traversal` | 100.95 horizontal units from pad 145; maximum 85 | PASS at 23.96 |
| `surface2_native_multiwaypoint_input_traversal` | 9923.22 horizontal units; required 10500, plus milestone misses | PASS at 10591.66 |

The failing thresholds and scripted timings are committed in the
[Surface 1](../../../tools/campaign_routes/surface1_native_multiwaypoint_input_traversal.json),
[Bunker 1](../../../tools/campaign_routes/bunker1_spawn_two_door_collect_contract.json),
[Frigate](../../../tools/campaign_routes/frigate_native_multiwaypoint_input_traversal.json),
and [Surface 2](../../../tools/campaign_routes/surface2_native_multiwaypoint_input_traversal.json)
route definitions.

Commit `78d76c6` changed `modelSetAnimFrame2WithChrStuff` to read the flag from
`rwdata->unk01`, matching the setter and retail ASM. That commit re-recorded the
five affected tape baselines and documents that
`GE007_NO_ANIMFRAME2_ROOTFLAG_FIX=1` reproduces all seven old tape hashes
byte-for-byte. It did not update any campaign route definition.

## Reproduction

Run the configured gate under the retail-correct default:

```sh
ctest --test-dir build --output-on-failure \
  -R '^port_campaign_route_smoke$'
```

Then isolate the four failures against the negative control:

```sh
GE007_NO_ANIMFRAME2_ROOTFLAG_FIX=1 \
tools/campaign_route_smoke.sh --no-build \
  --route surface1_native_multiwaypoint_input_traversal \
  --route bunker1_spawn_two_door_collect_contract \
  --route frigate_native_multiwaypoint_input_traversal \
  --route surface2_native_multiwaypoint_input_traversal
```

The default fails all four and the opt-out passes all four.

## Root Cause

Tape baselines and campaign route fixtures are separate consumers of the same
simulation trajectory, but the FID-0117 change workflow only enumerated the
tapes. Campaign routes embed absolute frame checkpoints, controller windows,
distance floors, pad proximity limits, and a door press time. No dependency or
freshness mechanism identifies those fixtures when a default simulation change
is rebaselined.

## Required End State

Recapture each route under the retail-correct default and adjust its controller
windows and checkpoints to preserve the route's semantic purpose. Do not simply
lower every threshold and do not restore the FID-0117 opt-out globally.

Surface routes must still traverse the intended authored pad regions, Frigate
must still reach the far-deck checkpoint, and Bunker 1 must still open both real
doors and collect the real object. Record FID-0117 provenance in each changed
fixture. Extend the simulation-change workflow so every deterministic route
that consumes affected movement is explicitly reviewed alongside tape
baselines.

## Acceptance Criteria

- All four routes pass under the default retail-correct FID-0117 behavior in at
  least three independent runs.
- Surface 1 and Surface 2 retain meaningful early, middle, and late spatial
  milestones rather than threshold-only movement checks.
- Frigate reaches the intended far-deck pad region with an evidence-backed
  proximity bound.
- Bunker 1 records both door allow/open/finish sequences and the object
  collection through controller input only.
- No route sets `GE007_NO_ANIMFRAME2_ROOTFLAG_FIX=1` to obtain a pass.
- The dedicated FID-0117 negative-control tape remains fail-on-revert and
  byte-identical to its documented old hashes.
- `port_campaign_route_smoke` and the full configured CTest suite no longer fail
  for these routes.

## Verification Plan

Capture default and root-motion-opt-out artifacts for each affected route.
Review trajectory samples and interaction logs before changing the JSON. Run
each corrected route three times, the complete 34-route gate once, the full tape
regression, and then full CTest.

## Related Work

- FID-0117 is a landed retail-parity correction and already has a dedicated A/B
  flag and re-recorded tape evidence.
- AUDIT-0018 covers the separate knife-impact fixture invalidated by the same
  simulation change.
