#!/usr/bin/env python3
"""perf_budget_check.py -- enforce per-level frame-time budgets for the MGB64
native port. Part of the M0 milestone in docs/PERFORMANCE_PLAN.md.

Reads a census CSV (as produced by tools/perf_census.sh) and checks each level's
mean per-frame work_ms against budgets:

  * HARD FAIL  (default 16.6 ms / 60 fps): a user-visible regression. Exit 1.
  * TARGET     (default  8.3 ms / 120 fps): the "rip" goal. Reported as WARN,
               does not fail the gate unless --strict is given.

Levels missing from the CSV, or with a non-numeric measurement (NA), are
reported and treated as a hard failure unless --allow-missing is given (a level
that will not direct-boot in this environment should be excluded explicitly).

Usage:
  tools/perf_budget_check.py CENSUS.csv
  tools/perf_budget_check.py CENSUS.csv --hard-ms 16.6 --target-ms 8.3
  tools/perf_budget_check.py CENSUS.csv --strict          # target failures fail too
  tools/perf_budget_check.py CENSUS.csv --baseline B.csv  # also flag regressions vs baseline
  tools/perf_budget_check.py CENSUS.csv --allow-missing caverns,cradle

Exit codes: 0 = all budgets met; 1 = a hard failure; 2 = usage/IO error.
"""
import argparse
import csv
import sys


def load_census(path):
    """Return {level: default_ms_or_None} from a census CSV, skipping # comments."""
    rows = {}
    try:
        with open(path, newline="") as fh:
            reader = csv.reader(line for line in fh if not line.lstrip().startswith("#"))
            header = None
            for parts in reader:
                if not parts:
                    continue
                if header is None:
                    header = [p.strip() for p in parts]
                    continue
                rec = dict(zip(header, [p.strip() for p in parts]))
                lvl = rec.get("level")
                if not lvl:
                    continue
                raw = rec.get("default_ms", "NA")
                try:
                    rows[lvl] = float(raw)
                except (TypeError, ValueError):
                    rows[lvl] = None
    except OSError as exc:
        print(f"error: cannot read census '{path}': {exc}", file=sys.stderr)
        sys.exit(2)
    return rows


def main():
    ap = argparse.ArgumentParser(description="Enforce MGB64 per-level frame-time budgets.")
    ap.add_argument("census", help="census CSV from tools/perf_census.sh")
    ap.add_argument("--hard-ms", type=float, default=16.6, help="hard-fail budget (default 16.6 = 60fps)")
    ap.add_argument("--target-ms", type=float, default=8.3, help="target budget (default 8.3 = 120fps)")
    ap.add_argument("--strict", action="store_true", help="treat target misses as failures too")
    ap.add_argument("--baseline", help="optional baseline CSV; flag >15%% regressions")
    ap.add_argument("--regress-frac", type=float, default=0.15, help="regression threshold vs baseline (default 0.15)")
    ap.add_argument("--allow-missing", default="", help="comma-separated levels allowed to be absent/NA")
    args = ap.parse_args()

    allow_missing = {s.strip() for s in args.allow_missing.split(",") if s.strip()}
    census = load_census(args.census)
    baseline = load_census(args.baseline) if args.baseline else {}

    hard_failures, target_warns, missing, regressions = [], [], [], []

    def fps(ms):
        return f"{1000.0 / ms:5.0f}fps" if ms and ms > 0 else "   NA"

    for lvl, ms in sorted(census.items()):
        if ms is None:
            (missing if lvl not in allow_missing else []).append(lvl)
            continue
        tag = "OK "
        if ms > args.hard_ms:
            hard_failures.append(lvl); tag = "FAIL"
        elif ms > args.target_ms:
            target_warns.append(lvl); tag = "WARN"
        base = baseline.get(lvl)
        reg = ""
        if base and ms > base * (1.0 + args.regress_frac):
            regressions.append(lvl)
            reg = f"  (regressed vs baseline {base:.1f}ms, +{(ms/base-1)*100:.0f}%)"
        print(f"  [{tag}] {lvl:10s} {ms:7.2f} ms  {fps(ms)}{reg}")

    for lvl in missing:
        print(f"  [FAIL] {lvl:10s}   MISSING/NA (not in census)")

    print()
    print(f"budgets: hard-fail > {args.hard_ms} ms ({1000/args.hard_ms:.0f} fps), "
          f"target > {args.target_ms} ms ({1000/args.target_ms:.0f} fps)")

    failed = bool(hard_failures) or bool(missing) or bool(regressions)
    if args.strict and target_warns:
        failed = True

    if hard_failures:
        print(f"HARD FAIL: {', '.join(hard_failures)} below 60fps floor", file=sys.stderr)
    if missing:
        print(f"MISSING: {', '.join(missing)} (use --allow-missing to exclude)", file=sys.stderr)
    if regressions:
        print(f"REGRESSION: {', '.join(regressions)} exceeded baseline by >{args.regress_frac*100:.0f}%", file=sys.stderr)
    if target_warns:
        sev = "FAIL" if args.strict else "WARN"
        print(f"{sev}: {', '.join(target_warns)} above target ({args.target_ms}ms/120fps)",
              file=sys.stderr)

    if failed:
        print("RESULT: budget check FAILED", file=sys.stderr)
        return 1
    print("RESULT: all budgets met")
    return 0


if __name__ == "__main__":
    sys.exit(main())
