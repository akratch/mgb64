#!/usr/bin/env python3
"""Classify stock/native pixel deltas inside the traced glass projection ROI."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any

from PIL import Image

import compare_glass_projected_visual as projected
import compare_screenshots as screenshots


def luma(rgb: tuple[int, int, int]) -> float:
    r, g, b = rgb
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def saturation(rgb: tuple[int, int, int]) -> float:
    r, g, b = (channel / 255.0 for channel in rgb)
    hi = max(r, g, b)
    lo = min(r, g, b)
    return 0.0 if hi <= 0.0 else (hi - lo) / hi


def bucket(rgb: tuple[int, int, int]) -> str:
    r, g, b = rgb
    lum = luma(rgb)
    sat = saturation(rgb)
    if r > 200 and g > 200 and b > 170:
        return "near_white"
    if r > 170 and g > 170 and b > 120:
        return "bright"
    if r > 120 and g > 20 and b < 140 and r >= g and r > b + 40:
        return "warm"
    if b > r + 35 and b > g + 25:
        return "blue"
    if abs(r - g) <= 8 and abs(g - b) <= 8:
        return "gray"
    if sat < 0.12:
        return "low_sat"
    if lum < 45:
        return "dark"
    return "other"


def stats(values: list[float]) -> dict[str, float]:
    if not values:
        return {"mean": 0.0, "min": 0.0, "max": 0.0, "p05": 0.0, "p50": 0.0, "p95": 0.0}
    ordered = sorted(values)

    def percentile(frac: float) -> float:
        index = min(len(ordered) - 1, max(0, int(round((len(ordered) - 1) * frac))))
        return ordered[index]

    return {
        "mean": sum(values) / len(values),
        "min": ordered[0],
        "max": ordered[-1],
        "p05": percentile(0.05),
        "p50": percentile(0.50),
        "p95": percentile(0.95),
    }


def count_buckets(pixels: list[tuple[int, int, int]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for pixel in pixels:
        key = bucket(pixel)
        counts[key] = counts.get(key, 0) + 1
    return dict(sorted(counts.items()))


def transition_counts(
    baseline: list[tuple[int, int, int]],
    test: list[tuple[int, int, int]],
) -> dict[str, int]:
    counts: dict[str, int] = {}
    for a, b in zip(baseline, test):
        key = f"{bucket(a)}->{bucket(b)}"
        counts[key] = counts.get(key, 0) + 1
    return dict(sorted(counts.items(), key=lambda item: (-item[1], item[0]))[:32])


def strongest_cells(
    baseline: Image.Image,
    test: Image.Image,
    roi: tuple[int, int, int, int],
    grid: int,
    limit: int,
) -> list[dict[str, Any]]:
    x0, y0, width, height = roi
    base_px = baseline.load()
    test_px = test.load()
    cells: list[dict[str, Any]] = []
    cols = max(1, math.ceil(width / grid))
    rows = max(1, math.ceil(height / grid))
    for row in range(rows):
        for col in range(cols):
            left = x0 + col * grid
            top = y0 + row * grid
            right = min(x0 + width, left + grid)
            bottom = min(y0 + height, top + grid)
            if right <= left or bottom <= top:
                continue
            count = 0
            changed = 0
            luma_delta = 0.0
            abs_delta = 0.0
            for y in range(top, bottom):
                for x in range(left, right):
                    a = base_px[x, y]
                    b = test_px[x, y]
                    delta = abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2])
                    if delta > screenshots.DIFF_THRESHOLD:
                        changed += 1
                    luma_delta += luma(b) - luma(a)
                    abs_delta += delta
                    count += 1
            cells.append({
                "roi": [left, top, right - left, bottom - top],
                "pixels": count,
                "changed_pct": 100.0 * changed / count if count else 0.0,
                "mean_luma_delta": luma_delta / count if count else 0.0,
                "mean_abs_rgb_delta": abs_delta / count if count else 0.0,
            })
    return sorted(cells, key=lambda item: item["mean_abs_rgb_delta"], reverse=True)[:limit]


def load_projection_roi(args: argparse.Namespace) -> tuple[Image.Image, Image.Image, tuple[int, int, int, int], dict[str, Any]]:
    baseline_frame, baseline_record = projected.first_active_projection(projected.load_records(args.baseline_trace))
    test_frame, test_record = projected.first_active_projection(projected.load_records(args.test_trace))
    baseline_projection = projected.projection(baseline_record)
    test_projection = projected.projection(test_record)

    baseline_bbox = projected.list4(baseline_projection.get("union_screen_bbox"))
    test_bbox = projected.list4(test_projection.get("union_screen_bbox"))
    viewport = projected.list4(baseline_projection.get("viewport")) or [0.0, 10.0, 320.0, 220.0]
    if baseline_bbox is None or test_bbox is None:
        raise SystemExit("FAIL: both traces must contain active glass projection union bboxes")

    projected_bbox = projected.clamp_bbox_to_viewport(
        projected.expand_bbox(projected.union_bbox(baseline_bbox, test_bbox), args.projection_padding),
        viewport,
    )
    baseline_crop, baseline_crop_box = projected.open_logical_crop(
        args.baseline_image,
        active_threshold=args.active_threshold,
        logical_size=args.logical_size,
        logical_viewport=args.logical_viewport,
        frame_mode=args.baseline_logical_frame,
    )
    test_crop, test_crop_box = projected.open_logical_crop(
        args.test_image,
        active_threshold=args.active_threshold,
        logical_size=args.logical_size,
        logical_viewport=args.logical_viewport,
        frame_mode=args.test_logical_frame,
    )
    resized_test = False
    if baseline_crop.size != test_crop.size:
        resampling = getattr(Image, "Resampling", Image).BILINEAR
        test_crop = test_crop.resize(baseline_crop.size, resampling)
        resized_test = True

    roi = projected.map_projection_bbox_to_crop(projected_bbox, viewport, baseline_crop.size)
    meta = {
        "baseline_frame": baseline_frame,
        "test_frame": test_frame,
        "baseline_crop": list(baseline_crop_box),
        "test_crop": list(test_crop_box),
        "resized_test_to_baseline": resized_test,
        "projection_bbox": projected_bbox,
        "viewport": viewport,
    }
    return baseline_crop, test_crop, roi, meta


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-trace", type=Path, required=True)
    parser.add_argument("--test-trace", type=Path, required=True)
    parser.add_argument("--baseline-image", type=Path, required=True)
    parser.add_argument("--test-image", type=Path, required=True)
    parser.add_argument("--logical-size", type=screenshots.parse_size, required=True)
    parser.add_argument("--logical-viewport", type=screenshots.parse_roi, required=True)
    parser.add_argument("--baseline-logical-frame", choices=("active", "full"), default="active")
    parser.add_argument("--test-logical-frame", choices=("active", "full"), default="full")
    parser.add_argument("--active-threshold", type=screenshots.channel_threshold, default=0)
    parser.add_argument("--projection-padding", type=float, default=4.0)
    parser.add_argument("--cell-size", type=int, default=16)
    parser.add_argument("--top-cells", type=int, default=12)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args(argv)

    baseline_crop, test_crop, roi, meta = load_projection_roi(args)
    baseline_pixels = projected.roi_pixels(baseline_crop, roi)
    test_pixels = projected.roi_pixels(test_crop, roi)
    luma_delta = [luma(b) - luma(a) for a, b in zip(baseline_pixels, test_pixels)]
    sat_delta = [saturation(b) - saturation(a) for a, b in zip(baseline_pixels, test_pixels)]
    rgb_delta = [
        abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2])
        for a, b in zip(baseline_pixels, test_pixels)
    ]
    payload: dict[str, Any] = {
        **meta,
        "roi": list(roi),
        "pixels": len(baseline_pixels),
        "baseline_buckets": count_buckets(baseline_pixels),
        "test_buckets": count_buckets(test_pixels),
        "bucket_transitions_top": transition_counts(baseline_pixels, test_pixels),
        "luma_delta": stats(luma_delta),
        "saturation_delta": stats(sat_delta),
        "abs_rgb_delta": stats(rgb_delta),
        "strongest_cells": strongest_cells(baseline_crop, test_crop, roi, args.cell_size, args.top_cells),
    }

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        screenshots.write_json(args.json_out, payload)

    print(
        "=== glass projected pixel score: "
        f"roi={payload['roi']} pixels={payload['pixels']} "
        f"luma_delta_mean={payload['luma_delta']['mean']:.2f} "
        f"sat_delta_mean={payload['saturation_delta']['mean']:.3f} "
        f"abs_rgb_delta_mean={payload['abs_rgb_delta']['mean']:.2f}"
    )
    print(f"  baseline buckets: {payload['baseline_buckets']}")
    print(f"  test buckets:     {payload['test_buckets']}")
    if payload["strongest_cells"]:
        cell = payload["strongest_cells"][0]
        print(
            "  strongest cell: "
            f"roi={cell['roi']} changed={cell['changed_pct']:.1f}% "
            f"luma_delta={cell['mean_luma_delta']:.2f} "
            f"abs_delta={cell['mean_abs_rgb_delta']:.2f}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
