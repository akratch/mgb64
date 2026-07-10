#!/usr/bin/env python3
"""Calibration unittest for the pixel oracle (Task 1.3), ROM-free.

Proves the classifier on TINY synthetic images built in memory from the
committed, deterministic generator (gen_pixel_calibration.py) — the ROM guard
forbids committing image binaries, so the generator source is the fixture:
  * known-good pair  -> clusters_unexplained == 0 (every diff explained)
  * broken pair      -> clusters_unexplained >= 1 (a defect surfaces)
plus the normalization resample-selection rule and the diff plumbing.

Registered with ctest as `pixel_diff_unittest`.
"""
import os
import sys
import tempfile
import unittest

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
FID_DIR = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, FID_DIR)
sys.path.insert(0, HERE)

import pixel_diff  # noqa: E402
import pixel_normalize  # noqa: E402
import gen_pixel_calibration as gen  # noqa: E402


class TestApproximationsRegistry(unittest.TestCase):
    def test_registry_parses_and_has_four_classes(self):
        cfg = pixel_diff.load_approximations()
        ids = [c["id"] for c in cfg["classes"]]
        self.assertEqual(
            set(ids),
            {"vi_filter", "three_point_filter", "rdp_dither", "coverage_aa"},
        )
        self.assertIn("delta_threshold", cfg["global"])
        # Floor must sit below the smallest class bound, else vi_filter is
        # unreachable (a cluster's mean is always > the floor).
        self.assertLess(cfg["global"]["delta_threshold"], 16.0)


class TestCalibration(unittest.TestCase):
    def setUp(self):
        self.cfg = pixel_diff.load_approximations()
        self.native = gen.native_image()

    def test_known_good_pair_zero_unexplained(self):
        ares = gen.ares_good_image()
        verdict = pixel_diff.diff(self.native, ares, self.cfg)
        self.assertEqual(
            verdict["clusters_unexplained"], 0,
            "known-good pair must report zero unexplained clusters; got %r"
            % verdict["clusters"],
        )
        # And every accepted class actually fired (the images exercise all four).
        fired = {c["classification"] for c in verdict["clusters"]}
        self.assertEqual(
            fired,
            {"vi_filter", "three_point_filter", "rdp_dither", "coverage_aa"},
        )

    def test_broken_pair_surfaces_defect(self):
        ares = gen.ares_broken_image()
        verdict = pixel_diff.diff(self.native, ares, self.cfg)
        self.assertGreaterEqual(
            verdict["clusters_unexplained"], 1,
            "broken pair must surface at least one unexplained cluster",
        )
        unexplained = [c for c in verdict["clusters"] if not c["explained"]]
        # The defect is the big solid block; carries a classify-reasons trail.
        self.assertTrue(any(c["area"] >= 1000 for c in unexplained))
        self.assertTrue(all("classify_reasons" in c for c in unexplained))

    def test_identical_pair_is_clean(self):
        verdict = pixel_diff.diff(self.native, self.native, self.cfg)
        self.assertEqual(verdict["clusters_total"], 0)
        self.assertEqual(verdict["clusters_unexplained"], 0)


class TestNormalization(unittest.TestCase):
    def test_integer_upscale_uses_nearest(self):
        name, _ = pixel_normalize.choose_resample((320, 240), (640, 480))
        self.assertEqual(name, "nearest")

    def test_non_integer_upscale_uses_area(self):
        name, _ = pixel_normalize.choose_resample((292, 240), (640, 480))
        self.assertEqual(name, "area")

    def test_normalize_scales_ares_to_native_grid(self):
        native = Image.new("RGB", (128, 96), (10, 20, 30))
        ares = Image.new("RGB", (64, 48), (10, 20, 30))
        n, a, meta = pixel_normalize.normalize(native, ares)
        self.assertEqual(a.size, (128, 96))
        self.assertEqual(meta["resample"], "nearest")  # 2x integer
        self.assertEqual(meta["ares_orig_size"], [64, 48])

    def test_vi_deblur_is_deterministic_and_optional(self):
        native = Image.new("RGB", (16, 16), (0, 0, 0))
        ares = Image.new("RGB", (16, 16), (0, 0, 0))
        px = ares.load()
        px[8, 8] = (255, 255, 255)
        _, a_off, m_off = pixel_normalize.normalize(native, ares, vi_deblur=False)
        _, a_on, m_on = pixel_normalize.normalize(native, ares, vi_deblur=True)
        self.assertFalse(m_off["vi_deblur"])
        self.assertTrue(m_on["vi_deblur"])
        # Deblur spreads the lone bright pixel horizontally.
        self.assertNotEqual(a_off.getpixel((7, 8)), a_on.getpixel((7, 8)))

    def test_normalize_files_roundtrip(self):
        with tempfile.TemporaryDirectory() as d:
            npath = os.path.join(d, "n.png")
            apath = os.path.join(d, "a.ppm")
            Image.new("RGB", (128, 96), (5, 5, 5)).save(npath)
            Image.new("RGB", (64, 48), (5, 5, 5)).save(apath)
            meta = pixel_normalize.normalize_files(npath, apath, os.path.join(d, "out"))
            self.assertTrue(os.path.exists(meta["outputs"]["native_png"]))
            self.assertTrue(os.path.exists(meta["outputs"]["ares_png"]))


if __name__ == "__main__":
    unittest.main()
