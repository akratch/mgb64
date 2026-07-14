# AUDIT-0021: Knife-Impact Help Advertises the Wrong Default Warp Distance

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S4 - diagnostic help gives an incorrect reproduction parameter |
| Priority | P3 |
| Area | Validation tooling / command help |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Every `tools/knife_impact_smoke.sh --help` invocation |

## Summary

The knife-impact smoke currently sets its default guard warp distance to 90,
but `--help` still says the default is 120. Audit reproduction based on the
published help therefore does not reproduce the default fixture.

## Evidence

[`knife_impact_smoke.sh`](../../../tools/knife_impact_smoke.sh) assigns
`WARP_DISTANCE=90` after its FID-0066 retune. The usage text independently says:

```text
--warp-distance D    warp distance in front of Bond (default: 120)
```

Running the script without the option prints `warped d=90`, proving which value
is actually used.

## Reproduction

```sh
tools/knife_impact_smoke.sh --help | grep warp-distance
tools/knife_impact_smoke.sh --no-build --frames 1 2>&1 | head -1
```

The two outputs advertise 120 and execute 90 respectively.

## Root Cause

The executable default and a manually duplicated value in a heredoc were not
updated together when commit `afa250f` retuned the fixture from 120 to 90.

## Required End State

Make the help text derive from the same default variable used by option parsing,
or enforce the duplication with a test. Update it to the final calibrated value
from AUDIT-0018 rather than applying an intermediate 90-only documentation fix.

## Acceptance Criteria

- `--help` reports the exact warp distance used when the option is omitted.
- Changing the default in one location cannot leave the help text stale.
- A ROM-free tool test compares declared defaults with parsed/executed defaults.
- Explicit `--warp-distance D` continues to override the default.

## Verification Plan

Run the new ROM-free help/default test for the current and one temporary changed
default. Then run the knife-impact smoke once with no option and once with an
explicit different value, confirming the printed command summary.

## Related Work

- AUDIT-0018 requires recalibrating the actual fixture after FID-0117 and owns
  selection of the final default value.

## Resolution <!-- triage-2026-07-14 -->

Verified already fixed in commit `6c1a23d` (ledger Status was stale). knife_impact_smoke.sh uses an unquoted heredoc so the usage text interpolates the real WARP_DISTANCE default (90) instead of a drifting literal.
