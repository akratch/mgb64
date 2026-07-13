# AUDIT-0020: Guard-Fire Regression Lane Hardcodes a Stale Opt-Out Hash

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - a valid combat parity fix is reported red by stale duplicate baseline data |
| Priority | P1 |
| Area | Fidelity validation / guard fire-rate symmetry |
| Evidence level | Test reproduced and baseline history traced |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | `port_guard_fire_rate_symmetry_smoke` on the current default world state |

## Summary

The guard fire-rate symmetry lane proves its primary combat contract, then
fails because its script contains an old whole-simulation opt-out hash. The
canonical `dam_forward_30s` baseline was re-recorded for two later default-world
changes, but the duplicate constant in the guard-specific script was not.

The result is a red configured CTest even though guard cadence is exactly the
required 3:1 ratio.

## Evidence

[`guard_fire_rate_symmetry_smoke.sh`](../../../tools/fidelity/guard_fire_rate_symmetry_smoke.sh)
hardcodes `OPTOUT_HASH="4dc07b71623b315c"`. Its adjacent comment says this is
the pre-FID-0066 behavior on the current default baseline world, but it was last
updated for FID-0014.

CTest test 103 produced:

```text
fix-ON:  hash=33f29cf9d50267c2, interval=9 frames
fix-OFF: hash=ea95db4614bcd952, interval=3 frames
OK CADENCE: 9 == 3 x 3
FAIL BYTE-IDENTITY: ea95db4614bcd952 != 4dc07b71623b315c
```

The authoritative
[`dam_forward_30s.expected.json`](../../../baselines/tapes/dam_forward_30s.expected.json)
history shows two subsequent default-world rebaselines:

- FID-0046 changed the hashed contents of dead effect-buffer storage.
- FID-0117 corrected character root-motion accumulation.

The current default hash `33f29cf9d50267c2` matches the tape baseline exactly,
and the cadence-specific observable remains correct. The stale constant alone
causes the failure.

## Reproduction

```sh
ctest --test-dir build --output-on-failure \
  -R '^port_guard_fire_rate_symmetry_smoke$'
```

The test reports `OK CADENCE`, then exits one on the hardcoded byte-identity
comparison.

## Root Cause

One scenario has multiple unsynchronized baseline owners. The tape JSON owns
the default hash, while a shell constant separately owns the fire-rate opt-out
hash. Rebaseline commits update the JSON and free-form provenance note but have
no schema field or dependency check for the variant hash in the script.

## Required End State

Move the fire-rate opt-out hash into structured baseline data beside the
canonical tape record, with explicit environment assignments and provenance.
Make both the generic tape runner and guard-specific cadence lane consume that
single source. Re-record the opt-out variant on the current default world and
review it against FID-0046 and FID-0117 evidence before accepting the new hash.

The cadence ratio must remain an independent assertion; a matching whole-state
hash cannot substitute for proving the guard fire interval.

## Acceptance Criteria

- The structured tape baseline records both default and
  `GE007_FIRE_RATE_AUTHENTIC=0` hashes with the exact environment that defines
  each variant.
- The current opt-out hash is reproduced in at least three independent runs
  before it is committed.
- The script contains no duplicate hash literal.
- Default cadence remains exactly 9 frames and opt-out cadence exactly 3 for
  this fixture.
- Reverting the guard divisor correction fails the cadence assertion even if a
  whole-state hash is changed.
- Any future default-world rebaseline identifies and regenerates all declared
  scenario variants atomically or fails closed.
- The dedicated lane and full CTest pass on a clean checkout.

## Verification Plan

Add schema/unit coverage for named tape variants and environment maps. Replay
the default and fire-rate opt-out three times each, verify byte identity and the
9:3 cadence ratio, then fault-inject a divisor revert to prove the cadence gate
reddens. Run generic tape regression and full CTest afterward.

## Related Work

- FID-0066 is the valid guard-cadence fix protected by this lane.
- FID-0046 and FID-0117 are valid later world-state changes whose tape
  rebaselines exposed the duplicate baseline owner.
- AUDIT-0013 covers a separate generated fidelity index synchronization defect.
