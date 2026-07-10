#!/usr/bin/env python3
"""Task 2.2 — candidate emission from an S2 pixel-diff verdict.

The unit-testable core of `sense_pixel_sweep.sh`: turn a pixel_diff verdict
(from tools/fidelity/pixel_diff.py) at one route/checkpoint into a sense
candidate iff it has unexplained clusters. A verdict with zero unexplained
clusters emits nothing — every difference is accounted for by an accepted
approximation class (docs/fidelity/APPROXIMATIONS.md).

Candidates carry both source images + the diff visualization as evidence, and
match the sense-lane schema. The loop — not this tool — triages class/priority
and files into the ledger.
"""
from __future__ import annotations

import argparse
import json
import sys


def verdict_to_candidate(route, checkpoint, verdict, native_png=None,
                         ares_png=None, diff_png=None):
    """Return a candidate dict, or None if the verdict has no unexplained
    cluster."""
    if int(verdict.get("clusters_unexplained", 0) or 0) <= 0:
        return None
    unexplained = [c for c in verdict.get("clusters", []) if not c.get("explained", True)]
    unexplained.sort(key=lambda c: -c.get("area", 0))
    worst = unexplained[0] if unexplained else {}
    hues = sorted({c.get("dominant_hue", "?") for c in unexplained})
    return {
        # provisional taxonomy — the loop triages (charter rule 8).
        "class": "candidate",
        "surface": "renderer",
        "route": route,
        "checkpoint": checkpoint,
        "title": "%s @ timer %s: %d unexplained pixel cluster(s) "
                 "(worst area=%d mean_delta=%s hue=%s)" % (
                     route, checkpoint, len(unexplained),
                     worst.get("area", 0), worst.get("mean_delta", "?"),
                     worst.get("dominant_hue", "?")),
        "clusters_unexplained": len(unexplained),
        "worst_cluster": worst,
        "dominant_hues": hues,
        "evidence": {
            "verdict": verdict.get("_verdict_path"),
            "native_png": native_png,
            "ares_png": ares_png,
            "diff_png": diff_png,
        },
        "repro": "tools/fidelity/sense_pixel_sweep.sh --route %s" % route,
        "suspect": "",
        "priority": "P2",
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--verdict", required=True, help="pixel_diff verdict JSON")
    ap.add_argument("--route", required=True)
    ap.add_argument("--checkpoint", required=True, help="screenshot_game_timer value")
    ap.add_argument("--native-png", default=None)
    ap.add_argument("--ares-png", default=None)
    ap.add_argument("--diff-png", default=None)
    ap.add_argument("--out", default=None)
    args = ap.parse_args(argv)

    with open(args.verdict) as fh:
        verdict = json.load(fh)
    verdict.setdefault("_verdict_path", args.verdict)
    cand = verdict_to_candidate(
        args.route, args.checkpoint, verdict,
        native_png=args.native_png, ares_png=args.ares_png, diff_png=args.diff_png)
    text = json.dumps(cand, indent=2, sort_keys=True) if cand else ""
    if args.out:
        with open(args.out, "w") as fh:
            fh.write(text + ("\n" if text else ""))
    elif text:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
