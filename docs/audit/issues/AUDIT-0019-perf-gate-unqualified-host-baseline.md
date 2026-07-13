# AUDIT-0019: Performance Gate Hard-Fails an Unqualified Host Baseline

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - valid builds can fail release validation for host load rather than code |
| Priority | P1 |
| Area | Validation / performance regression gate |
| Evidence level | Full census reproduced and contract contradiction source-proven |
| Confidence | High |
| Origin | Newly standardized by this audit |
| Affected configurations | Every `port_perf_budget_smoke` run outside the exact baseline host and cold-run conditions |

## Summary

The performance smoke unconditionally compares every census to one committed
cold-host CSV and treats a 15 percent delta as a hard failure. The repository
simultaneously documents that the baseline is machine-relative and that the 60
fps absolute floor is the real gate.

Consequently the configured CTest suite can fail on a build that clears every
absolute budget. The result cannot distinguish a code regression from a
different M3 Max configuration, thermal state, display/GPU load, OS/driver, or
the heat accumulated by preceding serial gameplay tests.

## Evidence

The full suite's test 98 measured all 20 stages between 8.01 and 12.58 ms per
frame, equivalent to 79-125 fps. No stage crossed the documented 16.6 ms hard
floor. Nevertheless 19 stages exceeded the committed baseline by more than 15
percent, so the test failed.

The run used macOS 26.1 on a 14-core Apple M3 Max MacBook Pro with 36 GB RAM.
[`perf_census_baseline.csv`](../../../baselines/perf_census_baseline.csv) only
identifies `Apple M3 Max, -O3 Release, macOS OpenGL-over-Metal`; it does not
record model identifier, core/GPU configuration, OS build, renderer settings,
display state, power mode, or cold-run conditions.

[`perf_budget_smoke.sh`](../../../tools/perf_budget_smoke.sh) says the hard
failure is the 60 fps floor and calls the test reference-hardware sensitive, but
always appends `--baseline baselines/perf_census_baseline.csv`. In
[`perf_budget_check.py`](../../../tools/perf_budget_check.py), any baseline
delta above 15 percent sets the same failing verdict as an absolute hard-budget
miss.

[`INSTRUMENTATION.md`](../../INSTRUMENTATION.md) explicitly calls the CSV
machine-relative and instructs users to rebaseline on a new machine. Fidelity
records FID-0009, FID-0014, and FID-0066 already adjudicate repeated red runs of
this lane as thermal/load artifacts where absolute budgets held.

## Reproduction

On any validation-capable checkout with a legal ROM:

```sh
tools/perf_budget_smoke.sh --no-build
```

The audit run printed `RESULT: budget check FAILED` solely because of relative
baseline regressions. Independently checking the emitted CSV without
`--baseline` passes the 16.6 ms hard floor.

## Root Cause

The checker supports two legitimate but different policies: portable absolute
budgets and controlled-machine relative regression detection. The wrapper
combines them unconditionally and has no host fingerprint, baseline selection,
thermal/load qualification, interleaved control, repeat policy, or explicit
mode that explains which contract is being enforced.

CTest serializes game processes but runs this census late in a long runtime
suite, which is specifically hostile to comparison with a cold baseline.

## Required End State

Split portable correctness gating from controlled performance regression
analysis. The default CTest lane must hard-fail missing data, invalid runs, and
the absolute 60 fps floor; target misses may remain warnings unless strict mode
is requested.

Relative baseline failure must require an explicit regression mode and a
matching, versioned environment fingerprint. Prefer interleaved before/after
measurements or a stable same-run control for change attribution. Record raw
samples and robust statistics, and retry or classify thermally elevated runs
instead of silently treating them as code regressions.

## Acceptance Criteria

- A run where all stages are below 16.6 ms passes the portable default gate
  even when it does not match another machine's cold baseline.
- `--baseline` regression mode fails a synthetic greater-than-15-percent change
  when the environment fingerprint matches.
- A missing or mismatched fingerprint produces a clear skip/error for relative
  comparison, never a regression verdict.
- The fingerprint includes hardware model/configuration, OS and renderer,
  build type and relevant settings, display/render resolution, and baseline
  provenance.
- Relative mode records enough samples to report median and tail variance and
  has a documented thermal/load policy.
- Absolute-floor failures remain hard failures in every mode.
- CTest, documentation, and release-readiness tooling describe the same verdict
  semantics.

## Verification Plan

Add ROM-free checker fixtures for absolute pass/fail, relative pass/fail,
fingerprint mismatch, missing/invalid rows, and strict-target behavior. Run a
full cold census and an intentionally loaded census on the same host to verify
portable verdict stability. Validate relative detection with an interleaved
known-slow build or a synthetic CSV before rerunning full CTest.

## Related Work

- The measured audit run is useful performance data: all stages cleared the
  absolute floor, but 17 missed the 120 fps target. Those target misses remain
  optimization work and are not erased by correcting the gate semantics.
- This report standardizes a recurring thermal/load failure already mentioned
  in fidelity evidence; it does not claim that every relative delta is false.
