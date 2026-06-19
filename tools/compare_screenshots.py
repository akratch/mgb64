#!/usr/bin/env python3
"""Compare two BMP screenshots and report pixel differences.

Usage:
  compare_screenshots.py before.bmp after.bmp
  compare_screenshots.py before.bmp after.bmp --heatmap diff.png
  compare_screenshots.py before.bmp after.bmp --roi 100,50,200,150

Options:
  --heatmap PATH   Save visual diff heatmap (red = changed pixels)
  --roi X,Y,W,H    Only compare a region of interest
  --per-channel     Show per-channel (R,G,B) diff statistics
"""
import sys
from PIL import Image

def compare(path_a, path_b, heatmap_path=None, roi=None, per_channel=False):
    a = Image.open(path_a)
    b = Image.open(path_b)
    if a.size != b.size:
        print(f"SIZE MISMATCH: {a.size} vs {b.size}")
        return

    if roi:
        rx, ry, rw, rh = roi
        a = a.crop((rx, ry, rx + rw, ry + rh))
        b = b.crop((rx, ry, rx + rw, ry + rh))

    pa = list(a.convert("RGB").getdata())
    pb = list(b.convert("RGB").getdata())
    total = len(pa)

    changed = sum(1 for x, y in zip(pa, pb) if abs(x[0]-y[0])+abs(x[1]-y[1])+abs(x[2]-y[2]) > 5)
    identical = total - changed

    ua = len(set(pa))
    ub = len(set(pb))
    ba = sum(1 for p in pa if p[2] > 10)
    bb = sum(1 for p in pb if p[2] > 10)
    ra = sum(1 for p in pa if p[0] > 60 and p[1] < 30)
    rb = sum(1 for p in pb if p[0] > 60 and p[1] < 30)

    roi_label = f" (ROI {roi[0]},{roi[1]} {roi[2]}x{roi[3]})" if roi else ""
    print(f"=== {path_a} vs {path_b}{roi_label} ===")
    print(f"Changed pixels: {changed}/{total} ({100*changed/total:.1f}%)")
    print(f"Identical:      {identical}/{total} ({100*identical/total:.1f}%)")
    print(f"Unique colors:  {ua} -> {ub}")
    print(f"Blue (B>10):    {ba} -> {bb}")
    print(f"Red-dom (R>60,G<30): {ra} -> {rb}")

    # Per-channel stats
    if per_channel:
        r_diffs = [abs(x[0]-y[0]) for x, y in zip(pa, pb)]
        g_diffs = [abs(x[1]-y[1]) for x, y in zip(pa, pb)]
        b_diffs = [abs(x[2]-y[2]) for x, y in zip(pa, pb)]
        print(f"\nPer-channel diff (avg / max):")
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

if __name__ == "__main__":
    args = sys.argv[1:]
    heatmap_path = None
    roi = None
    per_channel = False

    # Parse optional flags
    filtered = []
    i = 0
    while i < len(args):
        if args[i] == "--heatmap" and i + 1 < len(args):
            heatmap_path = args[i + 1]; i += 2
        elif args[i] == "--roi" and i + 1 < len(args):
            parts = args[i + 1].split(",")
            roi = tuple(int(x) for x in parts); i += 2
        elif args[i] == "--per-channel":
            per_channel = True; i += 1
        else:
            filtered.append(args[i]); i += 1

    if len(filtered) != 2:
        print(f"Usage: {sys.argv[0]} before.bmp after.bmp [--heatmap diff.png] [--roi X,Y,W,H] [--per-channel]")
        sys.exit(1)
    compare(filtered[0], filtered[1], heatmap_path, roi, per_channel)
