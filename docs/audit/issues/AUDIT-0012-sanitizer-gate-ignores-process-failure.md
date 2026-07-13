# AUDIT-0012: Sanitizer Gate Certifies Nonzero Process Exits as Clean

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S3 - release evidence can be falsely green after a fatal process failure |
| Priority | P1 |
| Area | Validation / sanitizer harness |
| Evidence level | Fault injected and source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Every `tools/asan_smoke.sh --gate` run |

## Resolution

Fixed 2026-07-13 — run_attempt now captures the process exit code and fails the stage (return 1 -> gate nonzero) on a nonzero exit with no sanitizer banner. Fault-injected: a binary that exits 1 now makes --gate exit nonzero.

## Summary

The ASan/UBSan harness notices when the game exits nonzero but does not include
that result in its stage verdict. If the log lacks one of four sanitizer text
patterns, the script records the stage as clean and `--gate` exits zero.

A crash without a sanitizer banner, timeout, loader failure, dynamic-linker
failure, bad command-line regression, or unusable sanitizer binary can
therefore produce green release evidence without executing the requested
gameplay interval.

## Evidence

[`run_attempt`](../../../tools/asan_smoke.sh#L120) executes the game under an
`if ! (...)` condition. Its nonzero branch at line 149 prints `process: NONZERO
EXIT` but does not save the exit status or return failure.

The only failing condition is a nonzero count from a text grep at line 154. If
that count is zero, lines 164-166 unconditionally print `sanitizer: clean`, write
`clean` to the TSV, and return zero. The outer loop increments `PASSED`, and the
gate's final exit code is only the number of grep findings.

Fault injection with a valid executable that always exits one produced:

```text
process: NONZERO EXIT
sanitizer: clean
level: CLEAN
=== ASan/UBSan Smoke: 1/1 clean, 0 with findings ===
gate_exit=0
33    clean    0
```

As a positive control, the rebuilt real sanitizer binary completed a 240-frame
run on all 20 supported solo stages with clean process exits and no sanitizer
diagnostics. That useful result does not mitigate the gate's false-pass branch.

## Reproduction

No game execution is needed, but a ROM path must exist for the harness's input
check:

```sh
tools/asan_smoke.sh \
  --no-build \
  --gate \
  --binary /usr/bin/false \
  --frames 1 \
  --level 33 \
  --out-dir /tmp/mgb64-asan-gate-fault
echo "$?"
cat /tmp/mgb64-asan-gate-fault/summary.tsv
```

The script reports the nonzero exit, then labels the level clean, writes
`33<TAB>clean<TAB>0`, and prints exit code zero.

## Root Cause

The harness treats sanitizer-banner detection as the complete definition of
success. Process completion and sanitizer cleanliness are independent
requirements, but only the latter contributes to the return value. Negating the
subshell in the `if` also discards the original exit code before it can be
classified.

## Required End State

Capture the game process exit code without allowing shell `set -e` to terminate
the harness. A stage is clean only when all of these are true:

- the process exited zero;
- it did not time out or terminate by signal;
- the expected screenshot/terminal-frame artifact exists and is structurally
  valid; and
- the log contains no ASan, UBSan, fatal assertion, or runtime-error signature.

Record process status, exit code, sanitizer-hit count, and completion-artifact
status as separate TSV columns. Under `--gate`, any failed requirement must
produce a nonzero overall exit. Report-only mode may still exit zero, but it
must label failures as failures rather than clean runs.

## Acceptance Criteria

- `/usr/bin/false`, a command that dies by signal, and a forced timeout each
  produce a failed stage and nonzero `--gate` exit.
- A zero-exit command with a synthetic ASan or UBSan signature produces a failed
  stage and nonzero gate exit.
- A zero-exit command that never creates the expected terminal artifact does
  not count as clean.
- A valid sanitizer run with zero findings and a valid completion artifact is
  clean.
- Multi-stage summaries retain every stage and the overall exit is nonzero if
  any one stage fails.
- Report-only mode exits zero while preserving accurate `failed`, `timeout`,
  `sanitizer`, and `clean` statuses.
- CI and documentation describe the same verdict semantics implemented by the
  script.

## Verification Plan

Factor the verdict classification into a shell function or small ROM-free
helper and add a table-driven test for exit zero, exit one, signal, timeout,
missing artifact, ASan text, UBSan text, and success. Run one real Dam sanitizer
smoke as an integration control. Finally rerun the 20-stage 240-frame matrix
and verify every row contains an explicit zero process exit and a valid terminal
artifact.

## Related Work

- [`INSTRUMENTATION.md`](../../INSTRUMENTATION.md#asanubsan-smoke) describes
  `--gate` as failing on any sanitizer finding; it should also state that a run
  which did not complete cannot be sanitizer-clean evidence.
- [`tools/README.md`](../../../tools/README.md) calls this script the sanitizer
  gate, increasing the importance of fail-closed verdicts.
