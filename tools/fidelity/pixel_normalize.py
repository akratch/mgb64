#!/usr/bin/env python3
"""Task 1.3 — pixel-oracle normalization.

Bring a native screenshot (640x480 BMP, drop-in from `--screenshot-*`) and an
ares stock capture (N64-resolution PPM) into one aligned coordinate space so the
S2 pixel sweep (`sense_pixel_sweep.sh`) can diff them pixel-for-pixel.

Normalization rules (charter rule 2 — the ares capture is ground truth, so the
ares image is the one we resample; the native image defines the target grid):

  * Decode either side with Pillow (BMP, PPM, PNG all handled by `Image.open`).
  * Target grid defaults to the native image's size (typically 640x480).
  * Scale the ares image to the target:
      - if BOTH axes scale by an exact integer factor -> NEAREST (an integer
        upscale must not invent intermediate colors; a 320x240->640x480 2x
        must stay a clean pixel-doubling so dither/edges survive intact).
      - otherwise -> area-average (BOX) so a non-integer ratio averages source
        coverage instead of point-sampling (avoids resample shimmer that would
        masquerade as a real difference).
  * Optional `--vi-deblur`: apply a horizontal 3-tap ([1,2,1]/4) filter to the
    ares image BEFORE scaling, approximating the N64 VI horizontal AA/de-dither
    pass. Off by default (raw framebuffer comparison); on when the ares capture
    is known to be pre-VI and native renders a VI-filtered equivalent.

Outputs (deterministic, ROM-free once given the two inputs):
  <out-dir>/native.png            native, converted to RGB, at target size
  <out-dir>/ares_normalized.png   ares, resampled to target size
  <out-dir>/normalize_meta.json   metadata (sizes, scale, resample, deblur)

The functions are importable so the calibration unittest can drive them without
touching disk-based ROM captures.
"""
from __future__ import annotations

import argparse
import json
import os
import sys

try:
    from PIL import Image
except ImportError:  # pragma: no cover - exercised only without Pillow
    sys.stderr.write(
        "FAIL: Pillow is required for pixel normalization. "
        "Install it with: python3 -m pip install pillow\n"
    )
    raise SystemExit(2)


def _resampling(name):
    # Pillow >= 9.1 moved the enum to Image.Resampling; keep both working.
    base = getattr(Image, "Resampling", Image)
    return getattr(base, name)


def parse_size(value):
    parts = str(value).lower().split("x")
    if len(parts) != 2:
        raise argparse.ArgumentTypeError("expected WIDTHxHEIGHT")
    try:
        w, h = int(parts[0]), int(parts[1])
    except ValueError:
        raise argparse.ArgumentTypeError("expected integer WIDTHxHEIGHT")
    if w <= 0 or h <= 0:
        raise argparse.ArgumentTypeError("width and height must be positive")
    return (w, h)


def load_rgb(path):
    """Decode any Pillow-supported image (BMP/PPM/PNG) to an RGB image."""
    with Image.open(path) as im:
        return im.convert("RGB")


def vi_deblur_horizontal(image):
    """Horizontal 3-tap [1,2,1]/4 filter approximating the N64 VI AA pass.

    Pure-Pillow (no numpy dependency): a horizontal box-ish blur built from
    two half-pixel shifts. Deterministic and edge-clamped.
    """
    w, h = image.size
    if w < 3:
        return image.copy()
    px = image.load()
    out = Image.new("RGB", (w, h))
    opx = out.load()
    for y in range(h):
        for x in range(w):
            xl = x - 1 if x > 0 else 0
            xr = x + 1 if x < w - 1 else w - 1
            l = px[xl, y]
            c = px[x, y]
            r = px[xr, y]
            opx[x, y] = (
                (l[0] + 2 * c[0] + r[0] + 2) >> 2,
                (l[1] + 2 * c[1] + r[1] + 2) >> 2,
                (l[2] + 2 * c[2] + r[2] + 2) >> 2,
            )
    return out


def choose_resample(src_size, target_size):
    """Return ('nearest'|'area', PIL_resample) per the integer-scale rule."""
    sw, sh = src_size
    tw, th = target_size
    integer_x = tw >= sw and tw % sw == 0
    integer_y = th >= sh and th % sh == 0
    if integer_x and integer_y:
        return "nearest", _resampling("NEAREST")
    return "area", _resampling("BOX")


def normalize(native_img, ares_img, target_size=None, vi_deblur=False):
    """Return (native_rgb, ares_normalized_rgb, meta_dict).

    `native_img` / `ares_img` are PIL RGB images. `target_size` defaults to the
    native image size.
    """
    native_rgb = native_img.convert("RGB")
    target = tuple(target_size) if target_size else native_rgb.size
    if native_rgb.size != target:
        native_rgb = native_rgb.resize(target, _resampling("BOX"))

    ares_rgb = ares_img.convert("RGB")
    ares_orig_size = ares_rgb.size

    deblurred = False
    if vi_deblur:
        ares_rgb = vi_deblur_horizontal(ares_rgb)
        deblurred = True

    resample_name, resample = choose_resample(ares_orig_size, target)
    if ares_rgb.size != target:
        ares_norm = ares_rgb.resize(target, resample)
    else:
        ares_norm = ares_rgb.copy()
        resample_name = "identity"

    scale = (
        target[0] / float(ares_orig_size[0]),
        target[1] / float(ares_orig_size[1]),
    )
    meta = {
        "schema": "mgb64.fidelity.pixel_normalize.v1",
        "target_size": list(target),
        "native_orig_size": list(native_img.size),
        "ares_orig_size": list(ares_orig_size),
        "scale": [round(scale[0], 6), round(scale[1], 6)],
        "resample": resample_name,
        "vi_deblur": deblurred,
    }
    return native_rgb, ares_norm, meta


def normalize_files(native_path, ares_path, out_dir, target_size=None,
                    vi_deblur=False):
    native_img = load_rgb(native_path)
    ares_img = load_rgb(ares_path)
    native_rgb, ares_norm, meta = normalize(
        native_img, ares_img, target_size=target_size, vi_deblur=vi_deblur
    )
    os.makedirs(out_dir, exist_ok=True)
    native_out = os.path.join(out_dir, "native.png")
    ares_out = os.path.join(out_dir, "ares_normalized.png")
    meta_out = os.path.join(out_dir, "normalize_meta.json")
    native_rgb.save(native_out)
    ares_norm.save(ares_out)
    meta["native_path"] = os.path.abspath(native_path)
    meta["ares_path"] = os.path.abspath(ares_path)
    meta["outputs"] = {
        "native_png": os.path.abspath(native_out),
        "ares_png": os.path.abspath(ares_out),
    }
    with open(meta_out, "w") as fh:
        json.dump(meta, fh, indent=2, sort_keys=True)
        fh.write("\n")
    return meta


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--native", required=True, help="native screenshot (BMP/PNG)")
    ap.add_argument("--ares", required=True, help="ares stock capture (PPM/PNG)")
    ap.add_argument("--out-dir", required=True, help="output directory")
    ap.add_argument("--target", type=parse_size, default=None,
                    help="target WIDTHxHEIGHT (default: native size)")
    ap.add_argument("--vi-deblur", action="store_true",
                    help="apply N64 VI horizontal AA approximation to ares")
    ap.add_argument("--print-meta", action="store_true",
                    help="print the metadata JSON to stdout")
    args = ap.parse_args(argv)

    meta = normalize_files(
        args.native, args.ares, args.out_dir,
        target_size=args.target, vi_deblur=args.vi_deblur,
    )
    if args.print_meta:
        json.dump(meta, sys.stdout, indent=2, sort_keys=True)
        sys.stdout.write("\n")
    else:
        print("normalized -> %s (ares %s -> %s, resample=%s, vi_deblur=%s)" % (
            args.out_dir,
            "x".join(map(str, meta["ares_orig_size"])),
            "x".join(map(str, meta["target_size"])),
            meta["resample"], meta["vi_deblur"],
        ))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
