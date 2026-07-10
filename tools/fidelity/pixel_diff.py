#!/usr/bin/env python3
"""Task 1.3 — per-pixel diff, clustering, and approximation classification.

Consumes two ALIGNED same-size RGB images (produced by pixel_normalize.py) and
produces the S2 verdict: which super-threshold difference clusters are explained
by an accepted renderer approximation (docs/fidelity/APPROXIMATIONS.md) and which
are unexplained and therefore candidates.

Pipeline:
  1. per-pixel delta = max over channels of |native - ares|.
  2. histogram of the delta magnitudes (0..255).
  3. threshold at global.delta_threshold -> binary super-threshold mask.
  4. connected-component labelling (4-connectivity, union-find) -> clusters,
     each with {bbox, area, mean_delta, dominant_hue}.
  5. classify each cluster against every APPROXIMATIONS.md class predicate; a
     cluster accepted by any class is `explained`, else `unexplained`.

Verdict JSON:
  {clusters_unexplained: N, clusters: [{...}], histogram:{...}, threshold, ...}

The classifier config is loaded from the first ```json fenced block in
docs/fidelity/APPROXIMATIONS.md so that document is the single source of truth.
Pure Pillow (no numpy) so it runs anywhere the CI Pillow dep is present.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    sys.stderr.write("FAIL: Pillow is required for pixel_diff.\n")
    raise SystemExit(2)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
DEFAULT_APPROX = os.path.join(REPO_ROOT, "docs", "fidelity", "APPROXIMATIONS.md")


# --------------------------------------------------------------------------- #
# Approximation-registry loading
# --------------------------------------------------------------------------- #
def load_approximations(path=DEFAULT_APPROX):
    """Parse the first ```json fenced block out of APPROXIMATIONS.md."""
    with open(path) as fh:
        text = fh.read()
    m = re.search(r"```json\s*(.*?)```", text, re.DOTALL)
    if not m:
        raise ValueError("no ```json block in %s" % path)
    cfg = json.loads(m.group(1))
    g = cfg.setdefault("global", {})
    g.setdefault("delta_threshold", 24)
    g.setdefault("connectivity", 4)
    g.setdefault("min_cluster_area", 4)
    cfg.setdefault("classes", [])
    return cfg


# --------------------------------------------------------------------------- #
# Delta / histogram
# --------------------------------------------------------------------------- #
def delta_map(native_img, ares_img):
    """Return (width, height, delta_bytes) where delta = max-channel abs diff."""
    a = native_img.convert("RGB")
    b = ares_img.convert("RGB")
    if a.size != b.size:
        raise ValueError("images differ in size: %s vs %s" % (a.size, b.size))
    w, h = a.size
    ab = a.tobytes()
    bb = b.tobytes()
    delta = bytearray(w * h)
    dr = bytearray(w * h)  # signed-ish dominant-channel bookkeeping via abs sums
    # Track per-pixel dominant channel for hue: store 0/1/2 in dchan.
    dchan = bytearray(w * h)
    n = w * h
    for i in range(n):
        j = i * 3
        r = abs(ab[j] - bb[j])
        gg = abs(ab[j + 1] - bb[j + 1])
        bl = abs(ab[j + 2] - bb[j + 2])
        d = r
        c = 0
        if gg > d:
            d = gg
            c = 1
        if bl > d:
            d = bl
            c = 2
        delta[i] = d
        dchan[i] = c
    return w, h, delta, dchan


def histogram(delta):
    hist = [0] * 256
    for d in delta:
        hist[d] += 1
    # compress to a sparse dict of non-zero buckets for the report
    return {str(k): v for k, v in enumerate(hist) if v}


# --------------------------------------------------------------------------- #
# Connected components (union-find, 4-connectivity)
# --------------------------------------------------------------------------- #
def _find(parent, x):
    root = x
    while parent[root] != root:
        root = parent[root]
    while parent[x] != root:
        parent[x], x = root, parent[x]
    return root


def connected_components(w, h, delta, threshold, connectivity=4):
    """Label super-threshold pixels into clusters. Returns list of pixel-index
    lists (one per cluster)."""
    n = w * h
    mask = [1 if delta[i] > threshold else 0 for i in range(n)]
    parent = list(range(n))
    for y in range(h):
        base = y * w
        for x in range(w):
            i = base + x
            if not mask[i]:
                continue
            # left neighbour
            if x > 0 and mask[i - 1]:
                ri, rj = _find(parent, i), _find(parent, i - 1)
                if ri != rj:
                    parent[rj] = ri
            # up neighbour
            if y > 0 and mask[i - w]:
                ri, rj = _find(parent, i), _find(parent, i - w)
                if ri != rj:
                    parent[rj] = ri
            if connectivity == 8:
                if x > 0 and y > 0 and mask[i - w - 1]:
                    ri, rj = _find(parent, i), _find(parent, i - w - 1)
                    if ri != rj:
                        parent[rj] = ri
                if x < w - 1 and y > 0 and mask[i - w + 1]:
                    ri, rj = _find(parent, i), _find(parent, i - w + 1)
                    if ri != rj:
                        parent[rj] = ri
    groups = {}
    for i in range(n):
        if mask[i]:
            groups.setdefault(_find(parent, i), []).append(i)
    return list(groups.values())


_HUE_NAMES = ("red", "green", "blue")


def cluster_stats(w, indices, delta, dchan):
    xs_min = ys_min = 1 << 30
    xs_max = ys_max = -1
    total = 0
    chan_counts = [0, 0, 0]
    for i in indices:
        x = i % w
        y = i // w
        if x < xs_min:
            xs_min = x
        if x > xs_max:
            xs_max = x
        if y < ys_min:
            ys_min = y
        if y > ys_max:
            ys_max = y
        total += delta[i]
        chan_counts[dchan[i]] += 1
    area = len(indices)
    bw = xs_max - xs_min + 1
    bh = ys_max - ys_min + 1
    dominant = _HUE_NAMES[chan_counts.index(max(chan_counts))]
    return {
        "bbox": [xs_min, ys_min, bw, bh],
        "area": area,
        "mean_delta": round(total / float(area), 3),
        "minor_extent": min(bw, bh),
        "dominant_hue": dominant,
    }


# --------------------------------------------------------------------------- #
# Classification
# --------------------------------------------------------------------------- #
def classify_cluster(stats, classes):
    """Return the id of the first class that accepts this cluster, else None.

    Also returns per-class near-miss detail for auditability (charter rule 9).
    """
    reasons = {}
    for cls in classes:
        cid = cls["id"]
        pred = cls.get("predicate", {})
        fails = []
        if "max_mean_delta" in pred and stats["mean_delta"] > pred["max_mean_delta"]:
            fails.append("mean_delta %.1f > %.1f" % (stats["mean_delta"], pred["max_mean_delta"]))
        if "max_area" in pred and stats["area"] > pred["max_area"]:
            fails.append("area %d > %d" % (stats["area"], pred["max_area"]))
        if "min_area" in pred and stats["area"] < pred["min_area"]:
            fails.append("area %d < %d" % (stats["area"], pred["min_area"]))
        if "max_minor_extent" in pred and stats["minor_extent"] > pred["max_minor_extent"]:
            fails.append("minor_extent %d > %d" % (stats["minor_extent"], pred["max_minor_extent"]))
        if "min_minor_extent" in pred and stats["minor_extent"] < pred["min_minor_extent"]:
            fails.append("minor_extent %d < %d" % (stats["minor_extent"], pred["min_minor_extent"]))
        if not fails:
            return cid, reasons
        reasons[cid] = fails
    return None, reasons


def diff(native_img, ares_img, cfg):
    w, h, delta, dchan = delta_map(native_img, ares_img)
    g = cfg["global"]
    threshold = g["delta_threshold"]
    min_area = g.get("min_cluster_area", 1)
    conn = g.get("connectivity", 4)
    raw = connected_components(w, h, delta, threshold, conn)

    clusters = []
    dropped_small = 0
    unexplained = 0
    for indices in raw:
        stats = cluster_stats(w, indices, delta, dchan)
        if stats["area"] < min_area:
            dropped_small += 1
            continue
        cid, reasons = classify_cluster(stats, cfg["classes"])
        stats["classification"] = cid
        stats["explained"] = cid is not None
        if cid is None:
            stats["classify_reasons"] = reasons
            unexplained += 1
        clusters.append(stats)

    clusters.sort(key=lambda c: (-c["area"], c["bbox"][0], c["bbox"][1]))
    verdict = {
        "schema": "mgb64.fidelity.pixel_diff.v1",
        "image_size": [w, h],
        "threshold": threshold,
        "connectivity": conn,
        "min_cluster_area": min_area,
        "clusters_total": len(clusters),
        "clusters_explained": len(clusters) - unexplained,
        "clusters_unexplained": unexplained,
        "clusters": clusters,
        "dropped_subminimum_clusters": dropped_small,
        "histogram": histogram(delta),
    }
    return verdict


def write_diff_visualization(native_img, ares_img, cfg, out_path):
    """Grayscale delta heat image (super-threshold pixels lit) for candidate
    evidence."""
    w, h, delta, _ = delta_map(native_img, ares_img)
    threshold = cfg["global"]["delta_threshold"]
    viz = Image.new("L", (w, h))
    px = bytearray(w * h)
    for i in range(w * h):
        d = delta[i]
        px[i] = 255 if d > threshold else min(d, 255)
    viz.frombytes(bytes(px))
    viz.save(out_path)
    return out_path


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--native", required=True, help="aligned native image (PNG)")
    ap.add_argument("--ares", required=True, help="aligned ares image (PNG)")
    ap.add_argument("--approximations", default=DEFAULT_APPROX,
                    help="APPROXIMATIONS.md path (default: repo doc)")
    ap.add_argument("--out", default=None, help="verdict JSON output (default: stdout)")
    ap.add_argument("--viz", default=None, help="write a delta-heat PNG here")
    ap.add_argument("--fail-on-unexplained", action="store_true",
                    help="exit 1 if any cluster is unexplained (gate mode)")
    args = ap.parse_args(argv)

    cfg = load_approximations(args.approximations)
    with Image.open(args.native) as ni, Image.open(args.ares) as ai:
        native_img = ni.convert("RGB")
        ares_img = ai.convert("RGB")
        verdict = diff(native_img, ares_img, cfg)
        if args.viz:
            write_diff_visualization(native_img, ares_img, cfg, args.viz)

    text = json.dumps(verdict, indent=2, sort_keys=True)
    if args.out:
        with open(args.out, "w") as fh:
            fh.write(text + "\n")
    else:
        print(text)

    if args.fail_on_unexplained and verdict["clusters_unexplained"] > 0:
        sys.stderr.write("FAIL: %d unexplained cluster(s)\n" % verdict["clusters_unexplained"])
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
