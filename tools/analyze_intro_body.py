#!/usr/bin/env python3
"""Pixel + trace validator for the animated Bond level-intro (audit R8 / backlog M1.4).

The existing intro audit (audit_intro_trace.py) checks actor state -- present,
rendered, anim hash. Those pass even when the viewer body is shredded or floating
(a sharded Bond still reports bond_rendered=1, a hovering Bond still animates).
This validator adds the three pixel/geometry checks that actor state cannot see:

  1. presence  -- Bond's warm skin/tan silhouette covers the expected screen
                  region (a de-aliased body is visible, not absent/collapsed).
  2. grounding -- Bond's world root Y stays within a bounded offset of the floor
                  beneath him (from the trace `intro.bond_body`, projection-free);
                  fails on a large persistent vertical offset (floating Bond).
  3. shards    -- no dark saturated-red outlier pixels (the degenerate red shard
                  signature) anywhere in the rendered frame.

Defaults are calibrated for the Dam intro swirl at the deterministic screenshot
frame the regression script captures (GE007_ENABLE_LEVEL_INTRO=1,
GE007_INTRO_CAMERA_INDEX=5, --level 33). It is a Dam-route fixture, not a general
projector: the port's frozen-intro camera matrices do not project actor world
positions to screen reliably, so the Bond region is an empirically-measured box
rather than an engine projection. See docs/INSTRUMENTATION.md.

ROM-derived; keep captures local.
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys


def load_image(path):
    try:
        from PIL import Image
    except ImportError:
        raise SystemExit(
            "FAIL: Pillow is required (pip install pillow)"
        )
    with Image.open(path) as image:
        rgb = image.convert("RGB")
        return rgb.size, rgb.load()


def count_warm_body(px, size, region):
    """Warm skin/tan pixels (Bond's silhouette) inside the region.

    The Dam swirl background is desaturated grey rock; Bond's skin and tan
    trousers are the only warm mid-bright pixels in this box."""
    w, h = size
    x0, y0, x1, y1 = region
    x0 = max(0, x0); y0 = max(0, y0); x1 = min(w, x1); y1 = min(h, y1)
    n = 0
    bx0 = bx1 = by0 = by1 = None
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b = px[x, y]
            if r > 110 and r > g + 18 and g > b + 5 and r - b > 30 and r < 230:
                n += 1
                bx0 = x if bx0 is None else min(bx0, x)
                bx1 = x if bx1 is None else max(bx1, x)
                by0 = y if by0 is None else min(by0, y)
                by1 = y if by1 is None else max(by1, y)
    return n, (bx0, by0, bx1, by1)


def count_red_shards(px, size, content_top, content_bottom):
    """Dark saturated-red outlier pixels: the degenerate red-shard signature.

    Strict enough to ignore the bright red/white striped barrier and any warm
    body pixels -- only dark, strongly-red-dominant texels count."""
    w, h = size
    y0 = max(0, content_top)
    y1 = min(h, content_bottom)
    n = 0
    for y in range(y0, y1):
        for x in range(0, w):
            r, g, b = px[x, y]
            if 60 <= r <= 190 and g < 70 and b < 70 and r > g * 2 + 20 and r > b * 2 + 20:
                n += 1
    return n


def grounding_offsets(trace_path, first_frame):
    recs = []
    with open(trace_path, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                recs.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    offs = []
    counts = []
    for r in recs:
        intro = r.get("intro")
        if not isinstance(intro, dict):
            continue
        bb = intro.get("bond_body")
        if not isinstance(bb, dict):
            continue
        if bb.get("projected") != 1 or not bb.get("floor_valid"):
            continue
        if r.get("f", 0) < first_frame:
            continue
        wr = bb.get("world_root")
        fy = bb.get("floor_y")
        if isinstance(wr, list) and len(wr) == 3 and isinstance(fy, (int, float)):
            offs.append(wr[1] - fy)
            counts.append(bb.get("render_pos_count", 0))
    return offs, counts


def main(argv):
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--screenshot", required=True)
    p.add_argument("--trace", required=True)
    p.add_argument("--label", default="dam intro body")
    # presence region + threshold (Dam swirl, deterministic screenshot frame)
    p.add_argument("--region", default="340,235,460,335",
                   help="Bond search box x0,y0,x1,y1 (default Dam frame 900)")
    p.add_argument("--min-warm", type=int, default=60,
                   help="min warm body pixels in region (default 60)")
    # grounding
    p.add_argument("--first-frame", type=int, default=544,
                   help="first swirl frame Bond is present (default 544)")
    p.add_argument("--max-grounding-offset", type=float, default=250.0,
                   help="max |root_y - floor_y| median before floating (default 250)")
    p.add_argument("--min-render-pos-count", type=int, default=18,
                   help="min body joint render-position count (default 18)")
    # shards
    p.add_argument("--content-top", type=int, default=70)
    p.add_argument("--content-bottom", type=int, default=405)
    p.add_argument("--max-red-shards", type=int, default=0)
    p.add_argument("--json-out")
    args = p.parse_args(argv)

    region = tuple(int(v) for v in args.region.split(","))
    if len(region) != 4:
        raise SystemExit("FAIL: --region must be x0,y0,x1,y1")

    size, px = load_image(args.screenshot)
    warm, warm_bbox = count_warm_body(px, size, region)
    red = count_red_shards(px, size, args.content_top, args.content_bottom)
    offs, counts = grounding_offsets(args.trace, args.first_frame)
    ground_med = statistics.median(offs) if offs else None
    ground_max = max(offs) if offs else None
    rpc_min = min(counts) if counts else 0

    failures = []
    if warm < args.min_warm:
        failures.append(
            "presence: warm body pixels %d < %d (Bond absent/collapsed)"
            % (warm, args.min_warm))
    if ground_med is None:
        failures.append("grounding: no projected+floor-valid bond_body frames in trace")
    elif abs(ground_med) > args.max_grounding_offset:
        failures.append(
            "grounding: |root_y - floor_y| median %.1f > %.1f (Bond floating)"
            % (ground_med, args.max_grounding_offset))
    if counts and rpc_min < args.min_render_pos_count:
        failures.append(
            "grounding: min render_pos_count %d < %d (body de-alias regressed)"
            % (rpc_min, args.min_render_pos_count))
    if red > args.max_red_shards:
        failures.append(
            "shards: %d dark-red outlier pixels > %d" % (red, args.max_red_shards))

    status = "PASS" if not failures else "FAIL"
    print("=== %s ===" % args.label)
    print("  presence: warm=%d region=%s bbox=%s (min %d)"
          % (warm, region, warm_bbox, args.min_warm))
    print("  grounding: median=%s max=%s render_pos_min=%d frames=%d (max_off %.0f, min_rpc %d)"
          % (None if ground_med is None else round(ground_med, 1),
             None if ground_max is None else round(ground_max, 1),
             rpc_min, len(offs), args.max_grounding_offset, args.min_render_pos_count))
    print("  shards: red_outliers=%d (max %d)" % (red, args.max_red_shards))
    for f in failures:
        print("  - %s" % f)

    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as handle:
            json.dump({
                "label": args.label,
                "status": status.lower(),
                "presence": {"warm": warm, "bbox": warm_bbox, "region": list(region),
                             "min_warm": args.min_warm},
                "grounding": {"median": ground_med, "max": ground_max,
                              "render_pos_min": rpc_min, "frames": len(offs),
                              "max_offset": args.max_grounding_offset,
                              "min_render_pos_count": args.min_render_pos_count},
                "shards": {"red_outliers": red, "max": args.max_red_shards},
                "failures": failures,
            }, handle, indent=2, sort_keys=True)
            handle.write("\n")

    print("%s: %s" % (status, args.label))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
