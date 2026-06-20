#!/usr/bin/env python3
"""Compare two screenshots and report pixel differences."""

from __future__ import annotations

import argparse
import json
import math
import sys
from PIL import Image


DIFF_THRESHOLD = 5


def rgb_pixels(image):
    data = image.convert("RGB").tobytes()
    return list(zip(data[0::3], data[1::3], data[2::3]))


def parse_roi(value):
    try:
        parts = [int(part) for part in value.split(",")]
    except ValueError:
        raise argparse.ArgumentTypeError("ROI must be X,Y,W,H") from None
    if len(parts) != 4:
        raise argparse.ArgumentTypeError("ROI must be X,Y,W,H")
    x, y, w, h = parts
    if w <= 0 or h <= 0:
        raise argparse.ArgumentTypeError("ROI width/height must be positive")
    if x < 0 or y < 0:
        raise argparse.ArgumentTypeError("ROI x/y must be non-negative")
    return x, y, w, h


def non_negative_finite(value):
    try:
        parsed = float(value)
    except ValueError:
        raise argparse.ArgumentTypeError("expected a number") from None
    if not math.isfinite(parsed) or parsed < 0.0:
        raise argparse.ArgumentTypeError("expected a non-negative finite number")
    return parsed


def write_json(path, payload):
    if not path:
        return
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True)
        handle.write("\n")


def compare(path_a, path_b, heatmap_path=None, roi=None, per_channel=False, max_changed_pct=None):
    a = Image.open(path_a)
    b = Image.open(path_b)
    if a.size != b.size:
        print(f"SIZE MISMATCH: {a.size} vs {b.size}")
        return {
            "status": "fail",
            "error": "size_mismatch",
            "baseline": path_a,
            "test": path_b,
            "baseline_size": list(a.size),
            "test_size": list(b.size),
            "roi": list(roi) if roi else None,
        }, 1

    if roi:
        rx, ry, rw, rh = roi
        if rx + rw > a.size[0] or ry + rh > a.size[1]:
            print(f"ROI OUT OF BOUNDS: {rx},{ry},{rw},{rh} for size {a.size}")
            return {
                "status": "fail",
                "error": "roi_out_of_bounds",
                "baseline": path_a,
                "test": path_b,
                "size": list(a.size),
                "roi": list(roi),
            }, 2
        a = a.crop((rx, ry, rx + rw, ry + rh))
        b = b.crop((rx, ry, rx + rw, ry + rh))

    pa = rgb_pixels(a)
    pb = rgb_pixels(b)
    total = len(pa)

    changed = sum(
        1
        for x, y in zip(pa, pb)
        if abs(x[0] - y[0]) + abs(x[1] - y[1]) + abs(x[2] - y[2]) > DIFF_THRESHOLD
    )
    identical = total - changed
    changed_pct = 100.0 * changed / total if total else 0.0
    identical_pct = 100.0 * identical / total if total else 0.0

    ua = len(set(pa))
    ub = len(set(pb))
    ba = sum(1 for p in pa if p[2] > 10)
    bb = sum(1 for p in pb if p[2] > 10)
    ra = sum(1 for p in pa if p[0] > 60 and p[1] < 30)
    rb = sum(1 for p in pb if p[0] > 60 and p[1] < 30)

    roi_label = f" (ROI {roi[0]},{roi[1]} {roi[2]}x{roi[3]})" if roi else ""
    print(f"=== {path_a} vs {path_b}{roi_label} ===")
    print(f"Changed pixels: {changed}/{total} ({changed_pct:.1f}%)")
    print(f"Identical:      {identical}/{total} ({identical_pct:.1f}%)")
    print(f"Unique colors:  {ua} -> {ub}")
    print(f"Blue (B>10):    {ba} -> {bb}")
    print(f"Red-dom (R>60,G<30): {ra} -> {rb}")

    payload = {
        "status": "pass",
        "baseline": path_a,
        "test": path_b,
        "size": list(a.size),
        "roi": list(roi) if roi else None,
        "diff_threshold": DIFF_THRESHOLD,
        "pixels": total,
        "changed_pixels": changed,
        "changed_pct": changed_pct,
        "identical_pixels": identical,
        "identical_pct": identical_pct,
        "unique_colors": {"baseline": ua, "test": ub},
        "blue_pixels": {"baseline": ba, "test": bb},
        "red_dominant_pixels": {"baseline": ra, "test": rb},
        "max_changed_pct": max_changed_pct,
        "per_channel": None,
        "heatmap": heatmap_path,
        "failures": [],
    }

    # Per-channel stats
    if per_channel:
        r_diffs = [abs(x[0]-y[0]) for x, y in zip(pa, pb)]
        g_diffs = [abs(x[1]-y[1]) for x, y in zip(pa, pb)]
        b_diffs = [abs(x[2]-y[2]) for x, y in zip(pa, pb)]
        payload["per_channel"] = {
            "r": {"avg": sum(r_diffs) / total if total else 0.0, "max": max(r_diffs) if r_diffs else 0},
            "g": {"avg": sum(g_diffs) / total if total else 0.0, "max": max(g_diffs) if g_diffs else 0},
            "b": {"avg": sum(b_diffs) / total if total else 0.0, "max": max(b_diffs) if b_diffs else 0},
        }
        print("\nPer-channel diff (avg / max):")
        print(f"  R: {sum(r_diffs)/total:.2f} / {max(r_diffs)}")
        print(f"  G: {sum(g_diffs)/total:.2f} / {max(g_diffs)}")
        print(f"  B: {sum(b_diffs)/total:.2f} / {max(b_diffs)}")

    # Sample grid
    w, h = a.size
    print(f"\nSample grid ({path_a} -> {path_b}):")
    for y in [h//6, h//3, h//2, 2*h//3, 5*h//6]:
        row = []
        for x in [w//6, w//3, w//2, 2*w//3, 5*w//6]:
            px_a = a.getpixel((x, y))
            px_b = b.getpixel((x, y))
            diff = abs(px_a[0]-px_b[0]) + abs(px_a[1]-px_b[1]) + abs(px_a[2]-px_b[2])
            marker = " " if diff <= 5 else "*"
            row.append(f"({px_a[0]:3},{px_a[1]:3},{px_a[2]:3})->({px_b[0]:3},{px_b[1]:3},{px_b[2]:3}){marker}")
        print(f"  y={y:3}: {'  '.join(row)}")

    # Heatmap generation
    if heatmap_path:
        heatmap = Image.new("RGB", a.size)
        hpx = heatmap.load()
        for iy in range(a.size[1]):
            for ix in range(a.size[0]):
                ax = a.getpixel((ix, iy))
                bx = b.getpixel((ix, iy))
                diff = abs(ax[0]-bx[0]) + abs(ax[1]-bx[1]) + abs(ax[2]-bx[2])
                if diff <= 5:
                    # Dim version of the baseline
                    hpx[ix, iy] = (ax[0]//3, ax[1]//3, ax[2]//3)
                else:
                    # Red intensity proportional to diff (scale 0-765 to 0-255)
                    intensity = min(255, diff * 255 // 200)
                    hpx[ix, iy] = (intensity, 0, 0)
        heatmap.save(heatmap_path)
        print(f"\nHeatmap saved: {heatmap_path}")

    if max_changed_pct is not None and changed_pct > max_changed_pct:
        payload["status"] = "fail"
        payload["failures"].append(
            f"changed pixels {changed_pct:.3f}% > threshold {max_changed_pct:.3f}%"
        )
        print(f"\nFAIL: changed pixels {changed_pct:.3f}% > threshold {max_changed_pct:.3f}%")
        return payload, 1

    return payload, 0


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", help="baseline screenshot")
    parser.add_argument("test", help="test screenshot")
    parser.add_argument("--heatmap", help="save visual diff heatmap")
    parser.add_argument("--roi", type=parse_roi, help="only compare X,Y,W,H")
    parser.add_argument("--per-channel", action="store_true", help="show per-channel diff statistics")
    parser.add_argument(
        "--max-changed-pct",
        type=non_negative_finite,
        help="fail if changed-pixel percentage exceeds this threshold",
    )
    parser.add_argument("--json-out", help="write comparison metrics as JSON")
    return parser.parse_args(argv)


def main(argv):
    args = parse_args(argv)
    try:
        payload, exit_code = compare(
            args.baseline,
            args.test,
            heatmap_path=args.heatmap,
            roi=args.roi,
            per_channel=args.per_channel,
            max_changed_pct=args.max_changed_pct,
        )
    except FileNotFoundError as exc:
        missing = exc.filename or str(exc)
        payload = {
            "status": "fail",
            "error": "missing_file",
            "baseline": args.baseline,
            "test": args.test,
            "missing": missing,
        }
        print(f"FAIL: missing file: {missing}")
        write_json(args.json_out, payload)
        return 2
    except Exception as exc:
        payload = {
            "status": "fail",
            "error": type(exc).__name__,
            "baseline": args.baseline,
            "test": args.test,
            "message": str(exc),
        }
        print(f"FAIL: {type(exc).__name__}: {exc}")
        write_json(args.json_out, payload)
        return 2

    write_json(args.json_out, payload)
    return exit_code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
