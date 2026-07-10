#!/usr/bin/env python3
"""Task 2.1 — candidate emission from an S1 trace comparator report.

The unit-testable core of `sense_trace_sweep.sh`: take one comparator's JSON
report (compare_movement_trace / compare_combat_trace / compare_intro_trace /
compare_glass_trace all emit a `--json-out` report) and turn a divergent report
into a sense candidate. A clean (status==pass, no divergences) report emits
nothing.

The report shapes differ per comparator, so the field extraction is tolerant:
  * movement / intro : {status, divergence_count, divergences:[{diffs|field...}]}
  * combat           : {divergences_total, divergences_by_field:{field:count}}
  * glass            : {status, failures:[str]}
We treat a report as divergent if `status=="fail"` OR any divergence counter is
non-zero OR `failures` is non-empty, and harvest the divergent field tokens from
whichever keys are present.

Candidates match the sense-lane schema used elsewhere (sense_coverage.py):
{title, class, surface, evidence, repro, suspect, priority, divergent_fields}.
The loop — not this tool — triages class/priority and files into the ledger.
"""
from __future__ import annotations

import argparse
import json
import sys


def _as_field_tokens(value):
    out = []
    if isinstance(value, str):
        # a diff string like "pos.x baseline=1.0 test=2.0" -> leading token
        tok = value.strip().split()
        if tok:
            out.append(tok[0])
    elif isinstance(value, dict):
        for k in ("field", "path", "name"):
            if k in value and isinstance(value[k], str):
                out.append(value[k])
                break
        else:
            for d in value.get("diffs", []) or []:
                out.extend(_as_field_tokens(d))
    return out


def extract_divergent_fields(report):
    """Return a de-duplicated, ordered list of divergent field tokens."""
    fields = []

    # combat: divergences_by_field {field: count}
    by_field = report.get("divergences_by_field")
    if isinstance(by_field, dict):
        fields.extend(by_field.keys())

    # movement / intro: divergences: [{diffs:[...]}] or [{field:...}]
    for d in report.get("divergences", []) or []:
        fields.extend(_as_field_tokens(d))

    # glass / generic: failures: [str]
    for f in report.get("failures", []) or []:
        fields.extend(_as_field_tokens(f))

    seen = set()
    ordered = []
    for f in fields:
        if f not in seen:
            seen.add(f)
            ordered.append(f)
    return ordered


def is_divergent(report):
    if report.get("status") == "fail":
        return True
    for key in ("divergence_count", "divergences_total"):
        if int(report.get(key, 0) or 0) > 0:
            return True
    if report.get("failures"):
        return True
    if report.get("divergences"):
        return True
    if report.get("divergences_by_field"):
        return True
    return False


def report_to_candidate(route, comparator, surface, report, evidence_path,
                        repro=None):
    """Return a candidate dict, or None if the report is clean."""
    if not is_divergent(report):
        return None
    fields = extract_divergent_fields(report)
    shown = ", ".join(fields[:4]) if fields else "unclassified divergence"
    if fields and len(fields) > 4:
        shown += ", +%d more" % (len(fields) - 4)
    return {
        # class/priority are provisional — the loop's triage assigns the real
        # taxonomy value (charter rule 8). "candidate" marks it un-triaged.
        "title": "%s %s divergence: %s" % (route, surface, shown),
        "class": "candidate",
        "surface": surface,
        "comparator": comparator,
        "route": route,
        "evidence": evidence_path,
        "repro": repro or ("tools/fidelity/sense_trace_sweep.sh --route %s" % route),
        "suspect": "",
        "priority": "P2",
        "divergent_fields": fields,
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--report", required=True, help="comparator JSON report")
    ap.add_argument("--route", required=True)
    ap.add_argument("--comparator", required=True)
    ap.add_argument("--surface", required=True,
                    help="movement|combat|intro|glass")
    ap.add_argument("--repro", default=None)
    ap.add_argument("--out", default=None,
                    help="write candidate JSON here (default: stdout)")
    args = ap.parse_args(argv)

    with open(args.report) as fh:
        report = json.load(fh)
    cand = report_to_candidate(args.route, args.comparator, args.surface,
                               report, args.report, args.repro)
    text = json.dumps(cand, indent=2, sort_keys=True) if cand else ""
    if args.out:
        with open(args.out, "w") as fh:
            fh.write(text + ("\n" if text else ""))
    elif text:
        print(text)
    # exit 0 always: a divergence is a finding, not a tool failure (the --gate
    # polarity lives in the shell wrapper).
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
