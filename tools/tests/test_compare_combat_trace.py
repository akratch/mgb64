"""FID-0062 regression lane -- pins the sim-tick alignment of the combat
comparator (``tools/compare_combat_trace.py --align tick``).

The bug: ``--align move`` pairs records by INDEX, but the two combat emitters run
at different record cadences -- native emits exactly 1 record per game-frame while
the ares/stock emitter emits ~2 records per advancing g_GlobalTimer tick (intra-
frame AI substeps) AND interleaves EMPTY-roster sampling records. Index pairing
therefore skews the timelines ~2x in sim-tick space, inflating and misattributing
combat-oracle guard divergences. ``--align tick`` pairs by the move.global sim-tick
stamp (relative to shared motion onset) and collapses the ares interleave to one
canonical roster-bearing record per tick.

All fixtures are synthetic JSONL built in-test -- nothing here is ROM-derived.
Tests exercise both the internal alignment helpers and the CLI end-to-end.

The fail-on-revert guard: ``test_tick_align_has_no_phantom_divergences`` asserts
tick-alignment finds ZERO divergences on a construction where the two sides agree
per sim-tick. If ``--align tick`` is reverted to index pairing, the ~2x skew and
the empty-roster interleave both re-appear as divergences and this test reddens.

Run: python3 -m unittest tools.tests.test_compare_combat_trace
 or: python3 tools/tests/test_compare_combat_trace.py
"""
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools import compare_combat_trace as cct  # noqa: E402

SCRIPT = Path(__file__).resolve().parent.parent / "compare_combat_trace.py"

# Two guards; their actiontype is a deterministic function of the SIM-TICK, so any
# alignment that pairs the same sim-tick on both sides sees identical actiontypes
# (zero divergences), while any alignment that skews the tick mapping sees them
# differ.
GUARD_CHRNUMS = (6, 7)
SPEEDFRAMES = 3  # g_ClockTimer per game-frame on the Dam combat route


def guard(chrnum: int, rel_tick: int) -> dict:
    """A schema-complete guard whose actiontype/room encode the sim-tick."""
    phase = rel_tick // SPEEDFRAMES
    return {
        "chrnum": chrnum,
        "pos": [float(chrnum), 1.0, float(rel_tick)],
        "actiontype": phase % 16,
        "aimode": 1,
        "health": 4.0,
        "shotbondsum": 0.0,
        "flags_onscreen": 1,
        "target_visible": 1 if phase >= 10 else 0,
        "anim_hash": "0x0000000000000abc",
        "room": 135,
    }


def combat_oracle(rel_tick: int, *, empty_roster: bool) -> dict:
    guards = [] if empty_roster else [guard(cn, rel_tick) for cn in GUARD_CHRNUMS]
    return {
        "guards": guards,
        "guards_overflow": 0,
        "floor": {"stan_id": 777, "stan_room": 4, "stan_flags": 0, "height": -107.0},
        "combat": {
            "player_health": 700.0,
            "player_armor": 0.0,
            "shots_fired_total": 0,
            "hits_landed_total": 0,
            "rng_seed": "0x00000000",
        },
        "projectiles": [],
        "projectiles_overflow": 0,
    }


def record(global_tick: int, rel_tick: int, *, empty_roster: bool = False) -> dict:
    # Non-zero move.speed from the first record so first_moving_index == 0 on both
    # sides (motion onset at index 0); the alignment difference under test is the
    # tick-vs-index pairing, not the onset search.
    return {
        "f": global_tick,
        "p": 1,
        "move": {"global": global_tick, "clock": SPEEDFRAMES, "speed": [1.0, 0.0, 1.0]},
        "combat_oracle": combat_oracle(rel_tick, empty_roster=empty_roster),
    }


def native_stream(onset_global: int, n_ticks: int) -> list[dict]:
    """Native cadence: exactly 1 full-roster record per advancing sim-tick."""
    out = []
    for k in range(n_ticks):
        rel = k * SPEEDFRAMES
        out.append(record(onset_global + rel, rel))
    return out


def ares_stream(onset_global: int, n_ticks: int) -> list[dict]:
    """ares/stock cadence: for each advancing sim-tick, an EMPTY-roster sample
    followed by (sometimes two) full-roster substep records -- ~2 records/tick
    with the full/empty interleave that broke the naive tick align."""
    out = []
    for k in range(n_ticks):
        rel = k * SPEEDFRAMES
        g = onset_global + rel
        out.append(record(g, rel, empty_roster=True))       # empty sampling record
        out.append(record(g, rel))                          # full-roster substep
        if k % 2 == 0:
            out.append(record(g, rel))                      # extra intra-frame substep
    return out


class TickAlignmentHelpers(unittest.TestCase):
    def test_canonical_by_tick_drops_empty_roster(self) -> None:
        recs = ares_stream(1387, 5)
        canon = cct.canonical_by_tick(recs, 0, 1387)
        # One entry per advancing sim-tick (0,3,6,9,12), never an empty roster.
        self.assertEqual(sorted(canon), [0, 3, 6, 9, 12])
        for rel, rec in canon.items():
            self.assertEqual(cct.roster_size(rec), len(GUARD_CHRNUMS),
                             f"tick {rel} canonicalised to an empty/partial roster")

    def test_tick_align_pairs_by_simtick_not_index(self) -> None:
        base = ares_stream(1387, 20)     # ~2.5 rec/tick, different onset
        test = native_stream(241, 20)    # 1 rec/tick
        aligned = cct.align_records(base, test, "tick")
        # Paired keys are relative sim-ticks; both sides carry the same rel_tick.
        self.assertTrue(aligned)
        for rel, brec, trec in aligned:
            self.assertEqual(brec["move"]["global"] - 1387, rel)
            self.assertEqual(trec["move"]["global"] - 241, rel)
            self.assertEqual(cct.roster_size(brec), len(GUARD_CHRNUMS))
            self.assertEqual(cct.roster_size(trec), len(GUARD_CHRNUMS))


class TickVsMoveDivergence(unittest.TestCase):
    """The core FID-0062 assertion + fail-on-revert guard."""

    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        d = Path(self.tmp.name)
        self.baseline = d / "stock.jsonl"   # ares cadence
        self.test = d / "native.jsonl"      # native cadence
        write_jsonl(self.baseline, ares_stream(1387, 40))
        write_jsonl(self.test, native_stream(241, 40))

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def _run(self, align: str) -> dict:
        with tempfile.NamedTemporaryFile("r", suffix=".json") as out:
            subprocess.run(
                [sys.executable, str(SCRIPT),
                 "--baseline", str(self.baseline),
                 "--test", str(self.test),
                 "--align", align,
                 "--json-out", out.name],
                capture_output=True, text=True, check=True,
            )
            return json.loads(Path(out.name).read_text())

    def test_tick_align_has_no_phantom_divergences(self) -> None:
        """Under sim-tick alignment the two sides agree per tick => 0 divergences.

        FAIL-ON-REVERT: revert --align tick to index pairing and both the ~2x
        skew and the empty-roster interleave reappear as divergences => this
        assertion reddens.
        """
        metrics = self._run("tick")
        self.assertEqual(metrics["divergences_total"], 0,
                         f"tick-align invented divergences: "
                         f"{metrics['divergences_by_field']}")
        self.assertGreaterEqual(metrics["aligned_frames"], 40)

    def test_move_align_shows_skew_and_phantom_present(self) -> None:
        """--align move (index pairing) MUST inflate divergences on the same
        cadence-mismatched pair -- the bug this fix removes."""
        metrics = self._run("move")
        self.assertGreater(metrics["divergences_total"], 0,
                           "move-align should surface the index-pairing skew")
        # The empty-roster interleave shows up as phantom guard 'present'
        # divergences under index pairing; tick-align must not.
        self.assertIn("guards.present", metrics["divergences_by_field"])

    def test_tick_strictly_fewer_divergences_than_move(self) -> None:
        tick = self._run("tick")["divergences_total"]
        move = self._run("move")["divergences_total"]
        self.assertLess(tick, move)


def write_jsonl(path: Path, records: list[dict]) -> None:
    with path.open("w", encoding="utf-8") as handle:
        for r in records:
            handle.write(json.dumps(r) + "\n")


if __name__ == "__main__":
    unittest.main()
