#!/usr/bin/env python3
"""Score whether a glass projection candidate improves on a baseline run."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any


def load_json(path: Path) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as handle:
            data = json.load(handle)
    except OSError as exc:
        raise SystemExit(f"FAIL: cannot read {path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise SystemExit(f"FAIL: invalid JSON in {path}: {exc}") from exc
    if not isinstance(data, dict):
        raise SystemExit(f"FAIL: expected object JSON in {path}")
    return data


def finite_float(value: Any, default: float = 0.0) -> float:
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        return default
    return parsed if math.isfinite(parsed) else default


def finite_int(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def projection_score(payload: dict[str, Any]) -> dict[str, Any]:
    baseline = payload.get("baseline", {})
    test = payload.get("test", {})
    deltas = payload.get("deltas", {})
    if not isinstance(baseline, dict):
        baseline = {}
    if not isinstance(test, dict):
        test = {}
    if not isinstance(deltas, dict):
        deltas = {}

    max_area_error = abs(finite_float(deltas.get("max_screen_area_pct")))
    union_area_error = abs(finite_float(deltas.get("union_screen_area_pct")))
    onscreen_error = abs(finite_int(deltas.get("onscreen")))
    active_error = abs(finite_int(deltas.get("active")))
    projected_error = abs(finite_int(deltas.get("projected")))
    behind_error = abs(finite_int(deltas.get("behind")))
    score = (
        max_area_error
        + 0.25 * union_area_error
        + 0.10 * onscreen_error
        + 10.0 * (active_error + projected_error + behind_error)
    )
    return {
        "score": score,
        "scale_mode": test.get("scale_mode"),
        "stock": {
            "active": baseline.get("active"),
            "projected": baseline.get("projected"),
            "onscreen": baseline.get("onscreen"),
            "max_area_pct": baseline.get("max_screen_area_pct"),
            "union_area_pct": baseline.get("union_screen_area_pct"),
        },
        "test": {
            "active": test.get("active"),
            "projected": test.get("projected"),
            "onscreen": test.get("onscreen"),
            "max_area_pct": test.get("max_screen_area_pct"),
            "union_area_pct": test.get("union_screen_area_pct"),
        },
        "errors": {
            "max_area_pct": max_area_error,
            "union_area_pct": union_area_error,
            "onscreen": onscreen_error,
            "active": active_error,
            "projected": projected_error,
            "behind": behind_error,
        },
    }


def pct_improvement(before: float, after: float) -> float:
    if before == 0.0:
        return 0.0 if after == 0.0 else float("-inf")
    return 100.0 * (before - after) / before


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True,
                        help="projection comparison JSON from the current/default renderer")
    parser.add_argument("--candidate", type=Path, required=True,
                        help="projection comparison JSON from the candidate renderer")
    parser.add_argument("--min-score-improvement-pct", type=float, default=0.0)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args(argv)

    baseline = projection_score(load_json(args.baseline))
    candidate = projection_score(load_json(args.candidate))
    improvement = pct_improvement(baseline["score"], candidate["score"])
    status = "pass" if improvement >= args.min_score_improvement_pct else "fail"
    payload = {
        "status": status,
        "baseline": baseline,
        "candidate": candidate,
        "score_improvement_pct": improvement,
        "min_score_improvement_pct": args.min_score_improvement_pct,
    }

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n",
                                 encoding="utf-8")

    print(
        "=== glass projection win score: "
        f"score={baseline['score']:.3f}->{candidate['score']:.3f} "
        f"({improvement:.2f}% better)"
    )
    print(f"  baseline scale={baseline['scale_mode']} errors={baseline['errors']}")
    print(f"  candidate scale={candidate['scale_mode']} errors={candidate['errors']}")
    print(f"  stock:     {baseline['stock']}")
    print(f"  baseline:  {baseline['test']}")
    print(f"  candidate: {candidate['test']}")
    return 0 if status == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
