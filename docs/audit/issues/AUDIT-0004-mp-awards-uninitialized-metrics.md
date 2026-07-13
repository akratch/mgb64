# AUDIT-0004: Multiplayer Awards Evaluate Uninitialized Inactive Metrics

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - native C undefined behavior in match results |
| Priority | P3 |
| Area | Gameplay / multiplayer / awards |
| Evidence level | Analyzer and source proven |
| Confidence | High |
| Origin | Newly confirmed on 2026-07-12 |
| Affected configurations | Two- and three-player match completion |

## Summary

`mpCalculateAwards` declares four automatic `AwardMetrics` entries but only
initializes entries below the active player count. It then evaluates fields from
all four entries as function-call arguments for every award calculation. In a
two- or three-player game, those lvalue-to-rvalue conversions read indeterminate
automatic values and invoke undefined behavior before the helper can ignore the
inactive arguments.

No incorrect award was observed in the smoke run. The defect is nevertheless
real in the native C abstract machine and can produce analyzer failures,
nondeterministic stack-dependent behavior, or optimizer-dependent results.

## Evidence

- [`mpCalculateAwards`](../../../src/game/mp_watch.c#L306) declares
  `struct AwardMetrics metrics[4]` without an initializer at line 316.
- The population loop at lines 348-389 initializes only
  `metrics[0..player_count-1]`.
- Award selection at lines 394-473 passes fields from `metrics[0]` through
  `metrics[3]` by value fourteen times.
- [`mpFindMaxInt`](../../../src/game/mp_watch.c#L129) and the other helpers gate
  their comparisons for players 3 and 4, but function arguments are evaluated
  by the caller before those gates execute.
- Clang's static analyzer reports uninitialized arguments beginning at the
  first `mpFindMaxInt` call on line 394.

For example, a two-player call still evaluates both expressions below even
though `mpFindMaxInt` never compares them:

```c
metrics[2].num_suicides, metrics[3].num_suicides
```

## Reproduction

1. Analyze `src/game/mp_watch.c` with Clang's uninitialized-value checks under
   the native build defines.
2. Follow the `getPlayerCount() == 2` path through the population loop.
3. Continue to the first award helper call.

The analyzer reaches line 394 with `metrics[2].num_suicides` and
`metrics[3].num_suicides` uninitialized. MemorySanitizer on a focused end-of-
match harness should report the same read at runtime.

## Root Cause

The fixed-arity helpers model the original four registers/stack arguments and
use `numplayers` only inside the callee. The caller retained that interface but
did not give the inactive placeholder arguments defined values. Later control
flow cannot make an earlier uninitialized argument evaluation valid.

## Required End State

Give all four `AwardMetrics` entries a defined zero state before populating the
active players, using an aggregate initializer or `memset`. Preserve every
active-player calculation, helper call order, tie-break comparison, and RNG
call. Do not initialize active entries through code that changes floating-point
evaluation order.

An array-and-count helper redesign is also valid if it is proven against retail
assembly and never reads outside the active range, but it is not required to
close this defect. The minimal zero-initialization fix has the lowest fidelity
risk.

## Acceptance Criteria

- Every field of all four entries is defined before the first award helper call.
- Clang static analysis and MemorySanitizer report no uninitialized read for
  two-, three-, or four-player award calculation.
- Golden two-, three-, and four-player fixtures receive the same awards as the
  pre-fix implementation when the latter is run with zero-filled inactive
  stack slots.
- Active-player metric values and award bits are unchanged.
- The number and order of `randomGetNext` calls are unchanged for identical
  active metrics, including ties.
- No inactive player receives a menu or award side effect.

## Verification Plan

Build a focused fixture that supplies deterministic player stats and a counted
RNG stub. Exercise two, three, and four players with unique winners and with
ties for every integer and float award family. Run it under UBSan and
MemorySanitizer where supported, and retain static analysis as a CI check.

## Related Work

`mpFindMaxFloat` and `mpFindMinFloat` store their leading float in an `s32`, which
can change later comparisons for fractional values. That separate behavior
requires retail US assembly adjudication before it can be classified as a port
defect and is intentionally not folded into this report.
