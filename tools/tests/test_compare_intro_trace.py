"""T2 (D27) -- pins the oracle-comparator hardening: full-window evaluation (no
early-return divergence cap), per-mode alignment (no cross-mode-boundary ghosts),
mode-duration assertions, and the waiver mechanism.

All fixtures are synthetic JSONL built in-test -- nothing here is ROM-derived, so
nothing is committed by running the suite. Tests that exercise the CLI end-to-end
invoke `tools/compare_intro_trace.py` as a subprocess so argparse wiring, JSON
emission, and exit codes are covered for real, not just the internal helpers.

Run: python3 -m unittest tools.tests.test_compare_intro_trace
 or: python3 tools/tests/test_compare_intro_trace.py
"""
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools import compare_intro_trace as cit  # noqa: E402

SCRIPT = Path(__file__).resolve().parent.parent / "compare_intro_trace.py"


def write_jsonl(path: Path, records: list[dict]) -> None:
    with path.open("w", encoding="utf-8") as handle:
        for record in records:
            handle.write(json.dumps(record) + "\n")


def run_cli(args: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(SCRIPT), *args],
        capture_output=True,
        text=True,
    )


def scalar_record(cam: int, theta: float, floor: float = 0.0, stan_h: float = 0.0) -> dict:
    return {"p": 1, "cam": cam, "theta": theta, "floor": floor, "stan_h": stan_h}


def swirl_record(cam_delta_y: float = 0.0, cam_pos_x: float = 0.0) -> dict:
    return {
        "p": 1,
        "cam": 3,
        "cam_pos": [cam_pos_x, 0.0, 0.0],
        "cam_target": [0.0, 0.0, 0.0],
        "cam_up": [0.0, 1.0, 0.0],
        "cam_floor": [0.0, 0.0, 0.0],
        "cam_delta": [0.0, cam_delta_y, 0.0],
        "facing": [0.0, 0.0, 1.0],
    }


class FullEvaluationTest(unittest.TestCase):
    """1. No early-return: 30 divergent records, --max-divergences 5 must still
    report a total of 30, with printing capped at 5."""

    def test_full_window_reports_true_total_with_capped_printing(self):
        baseline = [scalar_record(cam=1, theta=0.0) for _ in range(30)]
        test = [scalar_record(cam=1, theta=10.0) for _ in range(30)]
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            baseline_path = tmp_path / "baseline.jsonl"
            test_path = tmp_path / "test.jsonl"
            json_out = tmp_path / "out.json"
            write_jsonl(baseline_path, baseline)
            write_jsonl(test_path, test)

            result = run_cli(
                [
                    "--align", "active-index",
                    "--profile", "scalar",
                    "--camera-modes", "intro",
                    "--max-divergences", "5",
                    "--json-out", str(json_out),
                    str(baseline_path), str(test_path),
                ]
            )

            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            printed_lines = [
                line for line in result.stdout.splitlines() if line.startswith("  key")
            ]
            self.assertEqual(len(printed_lines), 5, result.stdout)

            metrics = json.loads(json_out.read_text())
            self.assertEqual(metrics["divergence_count"], 30)
            self.assertEqual(metrics["verdict"], "fail")


class PerModeAlignmentTest(unittest.TestCase):
    """2. Per-mode alignment kills boundary ghosts: mode-1 lengths differ by 3
    records but per-mode content is identical -> 0 divergences under per-mode
    alignment, but active-index alignment (global index across the mode
    boundary) fabricates divergences."""

    def test_per_mode_alignment_eliminates_boundary_ghosts(self):
        modes = {1, 2, 3}
        baseline = (
            [{"p": 1, "cam": 1, "cam_pos": [1.0, 1.0, 1.0]} for _ in range(10)]
            + [{"p": 1, "cam": 2, "cam_pos": [2.0, 2.0, 2.0]} for _ in range(5)]
            + [{"p": 1, "cam": 3, "cam_pos": [3.0, 3.0, 3.0]} for _ in range(5)]
        )
        test = (
            [{"p": 1, "cam": 1, "cam_pos": [1.0, 1.0, 1.0]} for _ in range(13)]
            + [{"p": 1, "cam": 2, "cam_pos": [2.0, 2.0, 2.0]} for _ in range(5)]
            + [{"p": 1, "cam": 3, "cam_pos": [3.0, 3.0, 3.0]} for _ in range(5)]
        )
        specs = [("cam_pos", "vector")]

        per_mode_pairs = cit.align_per_mode(baseline, test, modes, None)
        per_mode_divergences, _ = cit.compare_pairs(per_mode_pairs, specs, 0.05, 0.005, 0.001, 0.02)
        self.assertEqual(per_mode_divergences, [], [d.message for d in per_mode_divergences])

        index_pairs = cit.align_by_index(baseline, test, 1, 1, None)
        index_divergences, _ = cit.compare_pairs(index_pairs, specs, 0.05, 0.005, 0.001, 0.02)
        self.assertGreater(len(index_divergences), 0)


class ModeDurationAssertionTest(unittest.TestCase):
    """3. Duration assertion: native (test) mode count outside expected+/-tol
    fails; within tolerance passes."""

    def test_duration_within_tolerance_passes(self):
        baseline = [scalar_record(cam=1, theta=0.0) for _ in range(5)]
        test = [scalar_record(cam=1, theta=0.0) for _ in range(5)]
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            baseline_path = tmp_path / "baseline.jsonl"
            test_path = tmp_path / "test.jsonl"
            write_jsonl(baseline_path, baseline)
            write_jsonl(test_path, test)

            result = run_cli(
                [
                    "--align", "active-index",
                    "--profile", "scalar",
                    "--camera-modes", "intro",
                    "--expect-mode-durations", "1:5:0",
                    str(baseline_path), str(test_path),
                ]
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_duration_outside_tolerance_fails(self):
        baseline = [scalar_record(cam=1, theta=0.0) for _ in range(5)]
        test = [scalar_record(cam=1, theta=0.0) for _ in range(5)]
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            baseline_path = tmp_path / "baseline.jsonl"
            test_path = tmp_path / "test.jsonl"
            json_out = tmp_path / "out.json"
            write_jsonl(baseline_path, baseline)
            write_jsonl(test_path, test)

            result = run_cli(
                [
                    "--align", "active-index",
                    "--profile", "scalar",
                    "--camera-modes", "intro",
                    "--expect-mode-durations", "1:10:0",
                    "--json-out", str(json_out),
                    str(baseline_path), str(test_path),
                ]
            )
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            metrics = json.loads(json_out.read_text())
            self.assertEqual(metrics["verdict"], "fail")
            self.assertTrue(any("mode 1" in d for d in metrics["divergences"]))


class WaiverTest(unittest.TestCase):
    """4. Waivers: a field divergence matching a field:<name>:mode<N> scope is
    reported WAIVED with its ledger ID and the run exits success; an unmatched
    divergence alongside it still fails."""

    def test_waiver_absorbs_matching_divergence(self):
        baseline = [swirl_record(cam_delta_y=0.0)]
        test = [swirl_record(cam_delta_y=100.0)]
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            baseline_path = tmp_path / "baseline.jsonl"
            test_path = tmp_path / "test.jsonl"
            json_out = tmp_path / "out.json"
            write_jsonl(baseline_path, baseline)
            write_jsonl(test_path, test)

            result = run_cli(
                [
                    "--align", "active-index",
                    "--profile", "path",
                    "--camera-modes", "swirl",
                    "--waivers", json.dumps({"field:cam_delta[1]:mode3": "D31"}),
                    "--json-out", str(json_out),
                    str(baseline_path), str(test_path),
                ]
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("WAIVED (D31)", result.stdout)

            metrics = json.loads(json_out.read_text())
            self.assertEqual(metrics["verdict"], "pass")
            self.assertEqual(len(metrics["waived"]), 1)
            self.assertEqual(metrics["waived"][0]["ledger_id"], "D31")
            self.assertGreaterEqual(metrics["waived"][0]["max_delta"], 99.9)

    def test_unwaived_divergence_alongside_a_waived_one_still_fails(self):
        baseline = [swirl_record(cam_delta_y=0.0, cam_pos_x=0.0)]
        test = [swirl_record(cam_delta_y=100.0, cam_pos_x=5.0)]
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            baseline_path = tmp_path / "baseline.jsonl"
            test_path = tmp_path / "test.jsonl"
            json_out = tmp_path / "out.json"
            write_jsonl(baseline_path, baseline)
            write_jsonl(test_path, test)

            result = run_cli(
                [
                    "--align", "active-index",
                    "--profile", "path",
                    "--camera-modes", "swirl",
                    "--waivers", json.dumps({"field:cam_delta[1]:mode3": "D31"}),
                    "--json-out", str(json_out),
                    str(baseline_path), str(test_path),
                ]
            )
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("WAIVED (D31)", result.stdout)

            metrics = json.loads(json_out.read_text())
            self.assertEqual(metrics["verdict"], "fail")
            self.assertEqual(len(metrics["waived"]), 1)
            self.assertEqual(metrics["unwaived_divergence_count"], 1)


class VerdictJsonTest(unittest.TestCase):
    """5. Verdict JSON: --json-out always contains verdict, per-mode aligned
    counts, and the waived list."""

    def test_json_out_contains_verdict_per_mode_counts_and_waived_list(self):
        baseline = (
            [scalar_record(cam=1, theta=0.0) for _ in range(4)]
            + [scalar_record(cam=3, theta=0.0) for _ in range(2)]
        )
        test = (
            [scalar_record(cam=1, theta=0.0) for _ in range(4)]
            + [scalar_record(cam=3, theta=0.0) for _ in range(2)]
        )
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            baseline_path = tmp_path / "baseline.jsonl"
            test_path = tmp_path / "test.jsonl"
            json_out = tmp_path / "out.json"
            write_jsonl(baseline_path, baseline)
            write_jsonl(test_path, test)

            result = run_cli(
                [
                    "--align", "per-mode",
                    "--profile", "scalar",
                    "--camera-modes", "intro,swirl",
                    "--json-out", str(json_out),
                    str(baseline_path), str(test_path),
                ]
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

            metrics = json.loads(json_out.read_text())
            self.assertEqual(metrics["verdict"], "pass")
            self.assertEqual(metrics["waived"], [])
            self.assertEqual(metrics["per_mode_aligned_counts"].get("1"), 4)
            self.assertEqual(metrics["per_mode_aligned_counts"].get("3"), 2)


if __name__ == "__main__":
    unittest.main()
