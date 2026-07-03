#!/usr/bin/env python3
"""Score whether a glass projected-pixel candidate improves on a baseline."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any


BUCKETS = ("bright", "near_white", "gray", "low_sat", "dark", "warm", "blue", "other")


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


def count(payload: dict[str, Any], side: str, bucket: str) -> int:
    buckets = payload.get(f"{side}_buckets", {})
    if not isinstance(buckets, dict):
        return 0
    value = buckets.get(bucket, 0)
    try:
        return int(value)
    except (TypeError, ValueError):
        return 0


def stat(payload: dict[str, Any], name: str, field: str = "mean") -> float:
    value = payload.get(name, {})
    if not isinstance(value, dict):
        return 0.0
    return finite_float(value.get(field))


def glass_score(payload: dict[str, Any]) -> dict[str, Any]:
    pixels = max(1, int(payload.get("pixels") or 1))
    stock = {bucket: count(payload, "baseline", bucket) for bucket in BUCKETS}
    test = {bucket: count(payload, "test", bucket) for bucket in BUCKETS}

    highlight_distance = (
        abs(test["bright"] - stock["bright"])
        + 1.5 * abs(test["near_white"] - stock["near_white"])
    ) / pixels
    chroma_distance = (
        abs(test["warm"] - stock["warm"])
        + abs(test["blue"] - stock["blue"])
        + abs(test["other"] - stock["other"])
    ) / pixels
    gray_distance = abs(test["gray"] - stock["gray"]) / pixels
    dark_distance = abs(test["dark"] - stock["dark"]) / pixels
    gray_excess = max(0, test["gray"] - stock["gray"]) / pixels
    luma_abs = abs(stat(payload, "luma_delta"))
    saturation_abs = abs(stat(payload, "saturation_delta"))
    abs_rgb_mean = stat(payload, "abs_rgb_delta")

    material_score = (
        1000.0 * highlight_distance
        + 250.0 * chroma_distance
        + 150.0 * gray_excess
        + 75.0 * gray_distance
        + 50.0 * dark_distance
        + 8.0 * saturation_abs
        + 0.25 * luma_abs
    )
    broad_rgb_score = abs_rgb_mean + luma_abs + 30.0 * saturation_abs
    return {
        "pixels": pixels,
        "stock_buckets": stock,
        "test_buckets": test,
        "material_score": material_score,
        "broad_rgb_score": broad_rgb_score,
        "components": {
            "highlight_distance": highlight_distance,
            "chroma_distance": chroma_distance,
            "gray_distance": gray_distance,
            "dark_distance": dark_distance,
            "gray_excess": gray_excess,
            "luma_abs_mean": luma_abs,
            "saturation_abs_mean": saturation_abs,
            "abs_rgb_mean": abs_rgb_mean,
        },
    }


def pct_delta(before: float, after: float) -> float:
    if before == 0.0:
        return 0.0 if after == 0.0 else float("inf")
    return 100.0 * (before - after) / before


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True,
                        help="projected-pixel score JSON from the current/default renderer")
    parser.add_argument("--candidate", type=Path, required=True,
                        help="projected-pixel score JSON from the candidate renderer")
    parser.add_argument("--min-material-improvement-pct", type=float, default=0.0)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args(argv)

    baseline_score = glass_score(load_json(args.baseline))
    candidate_score = glass_score(load_json(args.candidate))
    material_improvement_pct = pct_delta(
        baseline_score["material_score"],
        candidate_score["material_score"],
    )
    broad_rgb_improvement_pct = pct_delta(
        baseline_score["broad_rgb_score"],
        candidate_score["broad_rgb_score"],
    )
    status = "pass" if material_improvement_pct >= args.min_material_improvement_pct else "fail"
    payload = {
        "status": status,
        "baseline": baseline_score,
        "candidate": candidate_score,
        "material_improvement_pct": material_improvement_pct,
        "broad_rgb_improvement_pct": broad_rgb_improvement_pct,
        "min_material_improvement_pct": args.min_material_improvement_pct,
    }

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n",
                                 encoding="utf-8")

    print(
        "=== glass pixel win score: "
        f"material={baseline_score['material_score']:.3f}->{candidate_score['material_score']:.3f} "
        f"({material_improvement_pct:.2f}% better) "
        f"broad_rgb={baseline_score['broad_rgb_score']:.3f}->{candidate_score['broad_rgb_score']:.3f} "
        f"({broad_rgb_improvement_pct:.2f}% better)"
    )
    print(f"  stock buckets:     {baseline_score['stock_buckets']}")
    print(f"  baseline buckets:  {baseline_score['test_buckets']}")
    print(f"  candidate buckets: {candidate_score['test_buckets']}")
    return 0 if status == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
