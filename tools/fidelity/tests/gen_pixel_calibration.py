#!/usr/bin/env python3
"""Deterministic synthetic calibration images for the pixel oracle (Task 1.3).

These are NOT ROM captures. They are hand-built 128x96 RGB images that prove the
classifier: a known-good pair (every difference explained by an
APPROXIMATIONS.md class -> clusters_unexplained == 0) and a deliberately-broken
pair (a solid high-delta block no class covers -> clusters_unexplained >= 1).

The *generator* is the committed, auditable source of truth — the ROM-guard
(`scripts/ci/check_no_rom_data.sh`) forbids committing any image binary, so the
calibration unittest builds these in memory from the builder functions here, and
`--write DIR` only exists for humans who want to eyeball the fixtures locally
(the output is git-ignored).
"""
import argparse
import os

from PIL import Image

W, H = 128, 96
BASE = (100, 110, 120)


def _fill(px, x0, y0, w, h, color):
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            px[x, y] = color


def base_image():
    """Flat base — a clean canvas so every delta below is intentional."""
    return Image.new("RGB", (W, H), BASE)


def _add_explained(im):
    """One cluster per approximation class, each within its documented bound."""
    px = im.load()
    # coverage_aa: 1px-wide vertical edge band, strong amplitude (delta 150 R).
    for y in range(0, H):
        px[8, y] = (250, BASE[1], BASE[2])
    # vi_filter: low-amplitude diffuse patch (delta 12 G, mean 12 <= 16).
    _fill(px, 20, 20, 10, 10, (BASE[0], BASE[1] + 12, BASE[2]))
    # three_point_filter: moderate interior patch (delta 20 B, minor extent 20).
    _fill(px, 40, 20, 20, 20, (BASE[0], BASE[1], BASE[2] + 20))
    # rdp_dither: tiny speckle blob (3x3, delta 40 R, area 9 <= 64), clear of
    # the broken block so the two never merge.
    _fill(px, 112, 82, 3, 3, (BASE[0] + 40, BASE[1], BASE[2]))
    return im


def _add_broken(im):
    """A solid 40x30 high-delta block that no approximation class explains."""
    px = im.load()
    _fill(px, 60, 40, 40, 30, (255, 0, 255))
    return im


def native_image():
    return base_image()


def ares_good_image():
    return _add_explained(base_image())


def ares_broken_image():
    return _add_broken(_add_explained(base_image()))


def write(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    native_image().save(os.path.join(out_dir, "native.png"))
    ares_good_image().save(os.path.join(out_dir, "ares_good.png"))
    ares_broken_image().save(os.path.join(out_dir, "ares_broken.png"))
    return out_dir


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--write", metavar="DIR", required=True,
                    help="write native.png/ares_good.png/ares_broken.png here "
                         "(git-ignored; for local inspection only)")
    args = ap.parse_args(argv)
    write(args.write)
    print("wrote native.png, ares_good.png, ares_broken.png to", args.write)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
