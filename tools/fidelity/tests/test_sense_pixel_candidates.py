#!/usr/bin/env python3
"""Unit tests for the S2 pixel-sweep candidate emitter (Task 2.2), ROM-free.

Drives the real classifier end-to-end on the synthetic calibration images
(gen_pixel_calibration) via pixel_diff, then checks candidate wrapping:
broken pair -> candidate carrying image evidence, good pair -> None.
Registered with ctest as `sense_pixel_candidates_unittest`.
"""
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
FID_DIR = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, FID_DIR)
sys.path.insert(0, HERE)

import pixel_diff  # noqa: E402
import sense_pixel_candidates as spc  # noqa: E402
import gen_pixel_calibration as gen  # noqa: E402


class TestPixelCandidate(unittest.TestCase):
    def setUp(self):
        self.cfg = pixel_diff.load_approximations()
        self.native = gen.native_image()

    def test_good_pair_yields_no_candidate(self):
        verdict = pixel_diff.diff(self.native, gen.ares_good_image(), self.cfg)
        self.assertEqual(verdict["clusters_unexplained"], 0)
        self.assertIsNone(spc.verdict_to_candidate("r", "0", verdict))

    def test_broken_pair_yields_candidate_with_evidence(self):
        verdict = pixel_diff.diff(self.native, gen.ares_broken_image(), self.cfg)
        cand = spc.verdict_to_candidate(
            "dam_glass_visual_probe", "1190", verdict,
            native_png="/tmp/n.png", ares_png="/tmp/a.png", diff_png="/tmp/d.png")
        self.assertIsNotNone(cand)
        self.assertEqual(cand["surface"], "renderer")
        self.assertGreaterEqual(cand["clusters_unexplained"], 1)
        self.assertEqual(cand["class"], "candidate")  # provisional; loop triages
        self.assertEqual(cand["evidence"]["diff_png"], "/tmp/d.png")
        self.assertEqual(cand["evidence"]["native_png"], "/tmp/n.png")
        self.assertIn("dam_glass_visual_probe", cand["title"])
        # worst cluster is the big solid block, not a thin edge.
        self.assertGreaterEqual(cand["worst_cluster"]["area"], 1000)

    def test_verdict_missing_key_is_safe(self):
        self.assertIsNone(spc.verdict_to_candidate("r", "0", {}))


if __name__ == "__main__":
    unittest.main()
