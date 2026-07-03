#!/usr/bin/env python3
"""Analyze whether scaling a native glass contribution can explain stock pixels.

Given stock, default-native, and skip-underlay screenshots, this read-only tool
models pixels as:

    synthetic(t) = underlay + t * (default - underlay)

It reports whether any scalar t along that actual native contribution vector
moves each route ROI toward stock. This separates "same draw, wrong opacity" from
"wrong source color/order/coverage semantics".
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compare_actor_masked_visual as actor_masked  # noqa: E402
import compare_roi_pixel_semantics as roi_semantics  # noqa: E402
import compare_screenshots as screenshots  # noqa: E402


Pixel = tuple[int, int, int]


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def route_regions(route: dict[str, Any]) -> dict[str, tuple[int, int, int, int]]:
    return roi_semantics.route_regions(route)


def route_excludes(
    route: dict[str, Any],
    regions: dict[str, tuple[int, int, int, int]],
) -> list[dict[str, Any]]:
    return roi_semantics.route_excludes(route, regions)


def aligned_three_images(
    route: dict[str, Any],
    stock_image: Path,
    default_image: Path,
    underlay_image: Path,
    active_threshold: int,
) -> tuple[Image.Image, Image.Image, Image.Image, dict[str, Any]]:
    source_stock = Image.open(stock_image).convert("RGB")
    source_default = Image.open(default_image).convert("RGB")
    source_underlay = Image.open(underlay_image).convert("RGB")

    logical_size = tuple(route.get("visual_logical_size", [320, 240]))
    logical_viewport = tuple(route.get("visual_logical_viewport", [0, 10, 320, 220]))
    stock_frame = route.get("visual_baseline_logical_frame", "active")
    native_frame = route.get("visual_test_logical_frame", "full")

    stock_crop = screenshots.logical_crop_bbox(
        source_stock, active_threshold, logical_size, logical_viewport, stock_frame
    )
    default_crop = screenshots.logical_crop_bbox(
        source_default, active_threshold, logical_size, logical_viewport, native_frame
    )
    underlay_crop = screenshots.logical_crop_bbox(
        source_underlay, active_threshold, logical_size, logical_viewport, native_frame
    )

    stock = screenshots.crop_bbox(source_stock, stock_crop)
    default = screenshots.crop_bbox(source_default, default_crop)
    underlay = screenshots.crop_bbox(source_underlay, underlay_crop)

    resized_default = False
    resized_underlay = False
    if default.size != stock.size:
        resampling = getattr(Image, "Resampling", Image).BILINEAR
        default = default.resize(stock.size, resampling)
        resized_default = True
    if underlay.size != stock.size:
        resampling = getattr(Image, "Resampling", Image).BILINEAR
        underlay = underlay.resize(stock.size, resampling)
        resized_underlay = True

    return stock, default, underlay, {
        "source_size": {
            "stock": list(source_stock.size),
            "default": list(source_default.size),
            "underlay": list(source_underlay.size),
        },
        "aligned_size": list(stock.size),
        "logical_viewport": {
            "logical_size": list(logical_size),
            "viewport": list(logical_viewport),
            "stock_frame": stock_frame,
            "native_frame": native_frame,
            "crop": {
                "stock": list(stock_crop),
                "default": list(default_crop),
                "underlay": list(underlay_crop),
            },
        },
        "resized_to_stock": {
            "default": resized_default,
            "underlay": resized_underlay,
        },
    }


def clamp_u8(value: float) -> int:
    if not math.isfinite(value):
        value = 0.0
    if value < 0.0:
        return 0
    if value > 255.0:
        return 255
    return int(value + 0.5)


def synth_pixel(underlay: Pixel, default: Pixel, t: float) -> Pixel:
    return tuple(
        clamp_u8(float(underlay[channel]) + t * float(default[channel] - underlay[channel]))
        for channel in range(3)
    )  # type: ignore[return-value]


def mean(values: list[float]) -> float:
    return sum(values) / len(values) if values else 0.0


def luma(pixel: Pixel) -> float:
    return roi_semantics.luma(pixel)


def changed(a: Pixel, b: Pixel) -> bool:
    return roi_semantics.changed(a, b)


def score_pairs(stock_pixels: list[Pixel], synthetic_pixels: list[Pixel]) -> dict[str, Any]:
    total = len(stock_pixels)
    abs_rgb: list[float] = []
    sq_rgb: list[float] = []
    luma_delta: list[float] = []
    changed_count = 0
    brighter = 0
    darker = 0
    for stock, synth in zip(stock_pixels, synthetic_pixels):
        if changed(stock, synth):
            changed_count += 1
        synth_luma = luma(synth)
        stock_luma = luma(stock)
        delta_luma = synth_luma - stock_luma
        luma_delta.append(delta_luma)
        if delta_luma > 8.0:
            brighter += 1
        if delta_luma < -8.0:
            darker += 1
        for channel in range(3):
            delta = float(synth[channel] - stock[channel])
            abs_rgb.append(abs(delta))
            sq_rgb.append(delta * delta)
    return {
        "pixels": total,
        "changed_pixels": changed_count,
        "changed_pct": 100.0 * changed_count / total if total else 0.0,
        "mean_abs_rgb": mean(abs_rgb),
        "rmse_rgb": math.sqrt(mean(sq_rgb)) if sq_rgb else 0.0,
        "luma_delta_mean": mean(luma_delta),
        "synthetic_brighter_pct": 100.0 * brighter / total if total else 0.0,
        "synthetic_darker_pct": 100.0 * darker / total if total else 0.0,
    }


def evaluate_t(
    stock_pixels: list[Pixel],
    default_pixels: list[Pixel],
    underlay_pixels: list[Pixel],
    t: float,
) -> dict[str, Any]:
    synthetic = [
        synth_pixel(underlay, default, t)
        for default, underlay in zip(default_pixels, underlay_pixels)
    ]
    payload = score_pairs(stock_pixels, synthetic)
    payload["t"] = t
    return payload


def least_squares_t(
    stock_pixels: list[Pixel],
    default_pixels: list[Pixel],
    underlay_pixels: list[Pixel],
) -> float | None:
    numerator = 0.0
    denominator = 0.0
    for stock, default, underlay in zip(stock_pixels, default_pixels, underlay_pixels):
        for channel in range(3):
            contribution = float(default[channel] - underlay[channel])
            target = float(stock[channel] - underlay[channel])
            numerator += target * contribution
            denominator += contribution * contribution
    if denominator <= 0.0:
        return None
    return numerator / denominator


def pixel_lists_for_points(
    stock: Image.Image,
    default: Image.Image,
    underlay: Image.Image,
    points: set[tuple[int, int]],
) -> tuple[list[Pixel], list[Pixel], list[Pixel]]:
    stock_px = stock.load()
    default_px = default.load()
    underlay_px = underlay.load()
    ordered = sorted(points)
    return (
        [stock_px[x, y] for x, y in ordered],
        [default_px[x, y] for x, y in ordered],
        [underlay_px[x, y] for x, y in ordered],
    )


def analyze_point_set(
    stock: Image.Image,
    default: Image.Image,
    underlay: Image.Image,
    points: set[tuple[int, int]],
    source_pixels: int,
    excluded_pixels: int,
    t_min: float,
    t_max: float,
    t_step: float,
) -> dict[str, Any]:
    stock_pixels, default_pixels, underlay_pixels = pixel_lists_for_points(
        stock, default, underlay, points
    )
    t_values: list[float] = []
    steps = int(round((t_max - t_min) / t_step))
    for index in range(steps + 1):
        t_values.append(t_min + index * t_step)

    scores = [
        evaluate_t(stock_pixels, default_pixels, underlay_pixels, t)
        for t in t_values
    ]
    best = min(scores, key=lambda item: (item["mean_abs_rgb"], item["changed_pct"]))
    lsq_t = least_squares_t(stock_pixels, default_pixels, underlay_pixels)
    lsq_score = (
        evaluate_t(stock_pixels, default_pixels, underlay_pixels, lsq_t)
        if lsq_t is not None
        else None
    )

    by_t = {
        "underlay_t0": evaluate_t(stock_pixels, default_pixels, underlay_pixels, 0.0),
        "default_t1": evaluate_t(stock_pixels, default_pixels, underlay_pixels, 1.0),
        "best_grid": best,
        "least_squares": lsq_score,
    }
    if lsq_t is not None:
        by_t["least_squares"]["t_unclamped"] = lsq_t

    contribution_changed = sum(
        1 for default_px, underlay_px in zip(default_pixels, underlay_pixels)
        if changed(default_px, underlay_px)
    )
    contribution_nonzero = sum(
        1 for default_px, underlay_px in zip(default_pixels, underlay_pixels)
        if default_px != underlay_px
    )
    default_abs = by_t["default_t1"]["mean_abs_rgb"]
    best_abs = by_t["best_grid"]["mean_abs_rgb"]

    return {
        "source_pixels": source_pixels,
        "sampled_pixels": len(points),
        "excluded_pixels": excluded_pixels,
        "excluded_pct": 100.0 * excluded_pixels / source_pixels if source_pixels else 0.0,
        "contribution_changed_pixels": contribution_changed,
        "contribution_changed_pct": 100.0 * contribution_changed / len(points) if points else 0.0,
        "contribution_nonzero_pixels": contribution_nonzero,
        "contribution_nonzero_pct": 100.0 * contribution_nonzero / len(points) if points else 0.0,
        "scores": by_t,
        "best_grid_improvement_abs_rgb": default_abs - best_abs,
        "best_grid_changed_delta": by_t["best_grid"]["changed_pct"] - by_t["default_t1"]["changed_pct"],
    }


def analyze(args: argparse.Namespace) -> tuple[dict[str, Any], int]:
    failures: list[str] = []
    route = load_json(args.route_json)
    route_name = str(route.get("name") or "unknown")
    if route_name == "unknown":
        failures.append("route JSON is missing name")

    stock_image = args.stock_image or args.base_case_dir / f"stock_{route_name}.ppm"
    default_image = args.default_image or args.base_case_dir / f"native_{route_name}.bmp"
    underlay_image = args.underlay_image
    for label, path in (
        ("stock image", stock_image),
        ("default native image", default_image),
        ("underlay native image", underlay_image),
    ):
        if path is None or not path.exists():
            failures.append(f"missing {label}: {path}")

    regions = route_regions(route)
    excludes = route_excludes(route, regions)
    selected_regions = args.region or ["tower_pane", "projected_impact", "impact_side"]
    for name in selected_regions:
        if name not in regions:
            failures.append(f"route has no visual region: {name}")

    payload: dict[str, Any] = {
        "status": "fail" if failures else "pass",
        "failures": failures,
        "route": route_name,
        "inputs": {
            "route_json": str(args.route_json),
            "stock_image": str(stock_image),
            "default_image": str(default_image),
            "underlay_image": str(underlay_image),
            "model": "synthetic(t)=underlay+t*(default-underlay)",
            "t_scan": {"min": args.t_min, "max": args.t_max, "step": args.t_step},
        },
        "regions": {},
        "interpretation": [],
    }
    if failures:
        return payload, 1

    stock, default, underlay, alignment = aligned_three_images(
        route, stock_image, default_image, underlay_image, args.active_threshold
    )
    payload["alignment"] = alignment

    for name in selected_regions:
        region = regions[name]
        unmasked_points, unmasked_excluded = actor_masked.points_for_region(
            stock.size[0], stock.size[1], region=region, exclude_regions=[]
        )
        masked_points, masked_excluded = actor_masked.points_for_region(
            stock.size[0], stock.size[1], region=region, exclude_regions=excludes
        )
        source_pixels = region[2] * region[3]
        payload["regions"][name] = {
            "unmasked": analyze_point_set(
                stock, default, underlay, unmasked_points, source_pixels,
                unmasked_excluded, args.t_min, args.t_max, args.t_step
            ),
            "route_masked": analyze_point_set(
                stock, default, underlay, masked_points, source_pixels,
                masked_excluded, args.t_min, args.t_max, args.t_step
            ),
        }

    for name in selected_regions:
        metrics = payload["regions"][name]["unmasked"]
        default_score = metrics["scores"]["default_t1"]
        underlay_score = metrics["scores"]["underlay_t0"]
        best = metrics["scores"]["best_grid"]
        lsq = metrics["scores"].get("least_squares")
        lsq_text = ""
        if lsq is not None:
            lsq_text = f", lsq_t={lsq['t_unclamped']:.3f}"
        payload["interpretation"].append(
            f"{name}: default changed={default_score['changed_pct']:.3f}% "
            f"abs_rgb={default_score['mean_abs_rgb']:.3f}; "
            f"underlay changed={underlay_score['changed_pct']:.3f}% "
            f"abs_rgb={underlay_score['mean_abs_rgb']:.3f}; "
            f"best scalar t={best['t']:.3f} changed={best['changed_pct']:.3f}% "
            f"abs_rgb={best['mean_abs_rgb']:.3f}{lsq_text}"
        )
        masked = payload["regions"][name]["route_masked"]
        if masked["excluded_pixels"] > 0:
            masked_default = masked["scores"]["default_t1"]
            masked_underlay = masked["scores"]["underlay_t0"]
            masked_best = masked["scores"]["best_grid"]
            payload["interpretation"].append(
                f"{name} route_masked: default changed={masked_default['changed_pct']:.3f}% "
                f"abs_rgb={masked_default['mean_abs_rgb']:.3f}; "
                f"underlay changed={masked_underlay['changed_pct']:.3f}% "
                f"abs_rgb={masked_underlay['mean_abs_rgb']:.3f}; "
                f"best scalar t={masked_best['t']:.3f} "
                f"changed={masked_best['changed_pct']:.3f}% "
                f"abs_rgb={masked_best['mean_abs_rgb']:.3f}"
            )

    return payload, 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--route-json", type=Path, required=True)
    parser.add_argument("--base-case-dir", type=Path, required=True)
    parser.add_argument("--stock-image", type=Path)
    parser.add_argument("--default-image", type=Path)
    parser.add_argument("--underlay-image", type=Path, required=True)
    parser.add_argument("--active-threshold", type=screenshots.channel_threshold, default=0)
    parser.add_argument("--region", action="append")
    parser.add_argument("--t-min", type=float, default=-0.5)
    parser.add_argument("--t-max", type=float, default=2.0)
    parser.add_argument("--t-step", type=float, default=0.01)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args(argv)
    if not math.isfinite(args.t_min) or not math.isfinite(args.t_max) or not math.isfinite(args.t_step):
        parser.error("t range must be finite")
    if args.t_step <= 0.0 or args.t_max < args.t_min:
        parser.error("invalid t range")

    payload, status = analyze(args)
    encoded = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
