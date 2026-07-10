#!/usr/bin/env python3
"""Unit tests for the S1 trace-sweep candidate emitter (Task 2.1), ROM-free.

Proves candidate emission on synthetic comparator reports in each of the shapes
the real comparators emit (movement, combat, glass) and that a clean report
emits nothing. Registered with ctest as `sense_trace_candidates_unittest`.
"""
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
FID_DIR = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, FID_DIR)

import sense_trace_candidates as stc  # noqa: E402


class TestDivergenceDetection(unittest.TestCase):
    def test_clean_report_is_not_divergent(self):
        self.assertFalse(stc.is_divergent({"status": "pass", "divergence_count": 0}))

    def test_status_fail_is_divergent(self):
        self.assertTrue(stc.is_divergent({"status": "fail"}))

    def test_counter_is_divergent(self):
        self.assertTrue(stc.is_divergent({"divergences_total": 3}))


class TestFieldExtraction(unittest.TestCase):
    def test_movement_diffs_strings(self):
        report = {"status": "fail", "divergences": [
            {"diffs": ["pos.x baseline=1 test=2", "speed baseline=1 test=2"]}]}
        self.assertEqual(stc.extract_divergent_fields(report), ["pos.x", "speed"])

    def test_combat_by_field(self):
        report = {"divergences_total": 4,
                  "divergences_by_field": {"actiontype": 3, "floor.stan_id": 1}}
        self.assertEqual(
            set(stc.extract_divergent_fields(report)), {"actiontype", "floor.stan_id"})

    def test_glass_failures(self):
        report = {"status": "fail", "failures": ["center_glass roi mismatch"]}
        self.assertEqual(stc.extract_divergent_fields(report), ["center_glass"])

    def test_divergence_dict_with_field_key(self):
        report = {"status": "fail",
                  "divergences": [{"field": "aimode", "baseline": 1, "test": 2}]}
        self.assertEqual(stc.extract_divergent_fields(report), ["aimode"])

    def test_dedup_preserves_order(self):
        report = {"status": "fail", "divergences": [
            {"diffs": ["pos.x a", "pos.x b", "speed c"]}]}
        self.assertEqual(stc.extract_divergent_fields(report), ["pos.x", "speed"])


class TestCandidate(unittest.TestCase):
    def test_clean_report_yields_no_candidate(self):
        report = {"status": "pass", "divergence_count": 0}
        self.assertIsNone(stc.report_to_candidate(
            "dam_forward_stop", "compare_movement_trace.py", "movement",
            report, "/tmp/report.json"))

    def test_divergent_report_yields_candidate(self):
        report = {"status": "fail", "divergence_count": 1,
                  "divergences": [{"diffs": ["pos.x a", "speed b"]}]}
        cand = stc.report_to_candidate(
            "dam_forward_stop", "compare_movement_trace.py", "movement",
            report, "/tmp/report.json")
        self.assertIsNotNone(cand)
        self.assertEqual(cand["surface"], "movement")
        self.assertEqual(cand["route"], "dam_forward_stop")
        self.assertEqual(cand["evidence"], "/tmp/report.json")
        self.assertIn("pos.x", cand["divergent_fields"])
        # class is provisional ("candidate") — the loop assigns the taxonomy.
        self.assertEqual(cand["class"], "candidate")

    def test_title_truncates_many_fields(self):
        fields = [f"f{i}" for i in range(10)]
        report = {"status": "fail",
                  "divergences": [{"diffs": [f + " x" for f in fields]}]}
        cand = stc.report_to_candidate(
            "r", "c.py", "combat", report, "/tmp/r.json")
        self.assertIn("+6 more", cand["title"])


if __name__ == "__main__":
    unittest.main()
