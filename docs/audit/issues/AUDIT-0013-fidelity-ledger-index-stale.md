# AUDIT-0013: Fidelity Ledger Index Omits Nine Authoritative Records

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - authoritative fidelity reporting is incomplete and its freshness gate is red |
| Priority | P1 |
| Area | Fidelity ledger / generated documentation |
| Evidence level | Test reproduced and source-data census |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Repository documentation, release-readiness checks, and every workflow that reads `LEDGER.md` instead of the JSON records |

## Summary

The generated human-readable fidelity index is stale. It stops at `FID-0121`
while the authoritative ledger directory contains valid records through
`FID-0130`. Nine recent findings are absent, including landed raw-offset fixes
and unresolved multiplayer/gameplay defects.

The repository's own freshness test fails, so the current checkout cannot pass
the complete configured CTest suite.

## Evidence

`docs/fidelity/ledger` contains 129 uniquely named `FID-NNNN.json` records.
[`LEDGER.md`](../../fidelity/LEDGER.md) contains 120 unique IDs and no row for
any of these records:

```text
FID-0122 FID-0123 FID-0124 FID-0125 FID-0126
FID-0127 FID-0128 FID-0129 FID-0130
```

The missing set includes both open and landed findings, so neither an
open-only filter nor terminal-state filtering explains the omission.

[`ledger.py render --check`](../../../tools/fidelity/ledger.py) reports:

```text
LEDGER.md is STALE - run `tools/fidelity/ledger.py render`
(index has drifted from the 129 JSON findings)
```

CTest exposes the same result as failed test 72,
`fidelity_ledger_index_current`. The preceding schema and lifecycle tests,
`fidelity_ledger_valid` and `fidelity_ledger_unittest`, pass; the defect is the
generated index state, not invalid JSON.

Git history shows that the newest missing record was updated by the current
HEAD's recent `FID-0129` fix, while `LEDGER.md` was last regenerated earlier in
the audit sequence.

## Reproduction

No ROM is required:

```sh
python3 tools/fidelity/ledger.py render --check
ctest --test-dir build --output-on-failure \
  -R '^fidelity_ledger_index_current$'
```

Both commands exit nonzero and report the index as stale. A direct set
difference between JSON basenames and `FID-NNNN` tokens in `LEDGER.md` yields
exactly `FID-0122` through `FID-0130`.

## Root Cause

Individual JSON entries are the source of truth, but the mutation/fix workflow
does not update the generated index atomically. The freshness check catches the
drift only later. Several ledger commits therefore accumulated after the last
render without committing the generated result.

## Required End State

Regenerate and commit `LEDGER.md` from all 129 current JSON records. Then make
ledger mutation commands and fidelity-fix completion fail closed unless the
generated index is current. Prefer one command that writes the JSON transition
and index together, or automatically renders the index after a successful
mutation.

Keep `render --check` in CTest and the release gate as an independent defense.
The generated output must remain deterministic so a clean render produces no
diff on every supported host.

## Acceptance Criteria

- `LEDGER.md` contains one correct row for every current JSON record, including
  `FID-0122` through `FID-0130`, with no duplicate or orphan rows.
- `ledger.py render --check` exits zero on a clean checkout.
- `fidelity_ledger_index_current` passes.
- Adding, transitioning, reopening, or otherwise editing a ledger record cannot
  complete its normal workflow while leaving a stale index.
- A test fixture proves that a new record reddens `render --check`, and the
  supported mutation/render command restores a clean result.
- Rendering twice is byte-identical and does not depend on locale, timezone, or
  directory enumeration order.
- The full CTest suite no longer fails because of ledger freshness.

## Verification Plan

Run the renderer, inspect the nine added rows against their JSON source, then
run ledger validation, lifecycle unit tests, freshness check, and full CTest.
Add an isolated temporary-ledger test that creates and transitions a finding
through the supported CLI and asserts the generated index is current after
each successful operation.

## Related Work

- The JSON schema/lifecycle gate is healthy; this report is limited to generated
  index synchronization.
- The release-readiness test ran before the dedicated freshness test in this
  checkout and did not surface the stale index, so the dedicated CTest remains
  necessary unless release readiness explicitly incorporates it.
