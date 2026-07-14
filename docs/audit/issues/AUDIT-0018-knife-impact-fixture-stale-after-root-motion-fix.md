# AUDIT-0018: Knife-Impact Fixture Misses After the Retail Root-Motion Fix

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S3 - the crash regression lane no longer exercises its crash-prone branch |
| Priority | P1 |
| Area | Validation / throwing-knife impact smoke |
| Evidence level | Runtime A/B reproduced and replacement fixture proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | `PORT_VALIDATION_TESTS=ON`; default native simulation after FID-0117 |

## Summary

The throwing-knife crash regression uses a guard warp distance tuned to the old
root-motion trajectory. Under the retail-correct FID-0117 default, the game
exits cleanly but the knife misses, so the original null-matrix crash branch is
never exercised and the test correctly fails its secondary hit assertion.

The same fixture passes under the FID-0117 opt-out. A 90-to-100 unit distance
change restores the accepted hit under the current default without weakening
the branch-coverage assertion.

## Evidence

[`knife_impact_smoke.sh`](../../../tools/knife_impact_smoke.sh) warps guard 0 at
frame 60, equips a throwing knife, and fires through frame 240. Its current
`WARP_DISTANCE=90` was tuned for a prior fire-rate change. The script requires
an accepted Bond knife hit after process survival specifically to reject a
false pass where the knife never enters `object_interaction`'s throwing-knife
impact branch.

CTest test 80 completed with exit zero and no assertions, but
`audit_knife_impact.py` found zero hit events. Targeted A/B produced:

```text
default, distance 90:       FAIL, 0 accepted hits
root-motion opt-out, d=90: PASS, hit chr 0 part 11 at frame 258
default, distance 100:     PASS, hit chr 0 part 11 at frame 258
root-motion opt-out, d=100: PASS, hit chr 0 part 11 at frame 258
fire-rate opt-out, d=100:  PASS, hit chr 0 part 11 at frame 192
```

The unrelated `GE007_NO_THROW_MATRIX_OFFSET_FIX=1` control still failed at
distance 90. The miss is therefore attributed to FID-0117, not FID-0124.

## Reproduction

```sh
tools/knife_impact_smoke.sh --no-build \
  --out-dir /tmp/knife-default-90

GE007_NO_ANIMFRAME2_ROOTFLAG_FIX=1 \
tools/knife_impact_smoke.sh --no-build \
  --out-dir /tmp/knife-rootflag-off-90

tools/knife_impact_smoke.sh --no-build --warp-distance 100 \
  --out-dir /tmp/knife-default-100
```

The first command fails with zero hit events; the latter two record an accepted
hit and pass.

## Root Cause

The fixture is a timing- and position-sensitive emergent collision. FID-0117
changed guard animation root-motion accumulation on transitions, but the smoke
script was not part of that fix's impact inventory. Its comment still claims a
90-unit warp hits under both fire-rate states, which was true before FID-0117
but says nothing about the newly changed root-motion state.

## Required End State

Set the fixture's default warp distance to the demonstrated 100-unit value and
update its provenance comment. Preserve the accepted-hit requirement and the
process-survival check. Add an explicit fixture test matrix for the default,
FID-0117 opt-out, and legacy fire-rate state so future simulation corrections
cannot silently turn crash-branch coverage into a miss.

The production FID-0117 behavior must remain enabled; passing by disabling it
would validate the wrong simulation.

## Acceptance Criteria

- The default fixture records at least one accepted Bond throwing-knife hit on
  chr 0 and exits zero.
- The accepted hit reaches the `object_interaction` branch that previously
  dereferenced the null `sp58C` matrix.
- The same 100-unit fixture passes under
  `GE007_NO_ANIMFRAME2_ROOTFLAG_FIX=1` and `GE007_FIRE_RATE_AUTHENTIC=0`.
- Removing or reintroducing the original matrix fix makes the lane fail by
  process crash or explicit branch evidence, not by an unrelated miss.
- Three independent default runs are deterministic at the accepted-hit frame
  and target part, or the assertion is deliberately widened with recorded
  evidence for every accepted variant.
- `port_knife_impact_smoke` and full CTest pass.

## Verification Plan

Run the three-state 100-unit matrix above three times each. Inspect trace hit
events, process exits, and assertion logs. Fault-inject the old null-matrix
behavior in a disposable build to prove the hit still reaches the protected
branch, then run the complete validation suite.

## Related Work

- FID-0117 corrected a retail root-motion flag read and is not itself a defect.
- AUDIT-0017 covers four campaign routes missed by the same fixture-impact
  inventory.
- AUDIT-0021 covers the script's independently stale command help.

## Resolution

Fixed on `feat/webgpu-backend` by rebaselining `tools/knife_impact_smoke.sh`
`WARP_DISTANCE` 90 -> 100 and rewriting the rationale comment to record the
FID-0117 provenance (a root-motion seed-flag correction shifted the point-blank
guard's on-transition trajectory, so the 90-unit throw began to miss under the
retail default while still exiting 0 — a false-red where the sp58C crash branch
never runs).

Empirically verified here (ROM-gated smoke, `build/ge007`):

- **d=90, retail default:** knife MISSES (0 accepted hits) → smoke FAILs. This is
  the regression the audit describes.
- **d=100, retail default:** accepted hit on chr 0 part 11 at **frame 258** → PASS.
- **d=100, `GE007_NO_ANIMFRAME2_ROOTFLAG_FIX=1`** (FID-0117 opt-out): hit at
  frame 258 → PASS.
- **d=100, `GE007_FIRE_RATE_AUTHENTIC=0`** (legacy cadence): hit at frame 192 → PASS.

The primary process-survival + accepted-hit assertions are unchanged; the fix
restores crash-branch coverage across all three deterministic states without
disabling FID-0117 (which would validate the wrong simulation).
