"""ROM-free unit tests for tools/fidelity/ledger.py.

Auto-discovered by the ctest `intro_tools_unittests` lane
(`python3 -m unittest discover -s tools/tests -p 'test_*.py'`) and by the
dedicated `fidelity_ledger_valid` ctest indirectly. Exercises the ledger in an
isolated tmpdir via --ledger-dir so it never touches the real ledger.

Fixture evidence paths must be real repo-relative files (C5, 2026-07-10 review:
`validate` now checks evidence paths resolve to something real -- see
`docs/fidelity/CHARTER.md` used below, and the dedicated edge-set/evidence
coverage in `tools/fidelity/tests/test_ledger.py`).
"""
import io
import json
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

TOOLS_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "fidelity"))
sys.path.insert(0, TOOLS_DIR)
import ledger  # noqa: E402


def run(args):
    """Invoke ledger.main capturing (exit_code, stdout, stderr)."""
    out, err = io.StringIO(), io.StringIO()
    try:
        with redirect_stdout(out), redirect_stderr(err):
            code = ledger.main(args)
    except SystemExit as e:  # argparse errors
        code = e.code if isinstance(e.code, int) else 1
    return code, out.getvalue(), err.getvalue()


class LedgerTest(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.dir = os.path.join(self.root, "ledger")  # LEDGER.md lands in self.root
        self.base = ["--ledger-dir", self.dir]

    def _load(self, fid):
        with open(os.path.join(self.dir, f"{fid}.json")) as f:
            return json.load(f)

    def _new(self, title="A defect", cls="port-defect", surface="sim", pri="P1",
             suspect="foo.c:10", status=None, fid=None):
        args = self.base + ["new", "--title", title, "--class", cls,
                            "--surface", surface, "--priority", pri,
                            "--evidence", "docs/fidelity/CHARTER.md#A", "--evidence-kind", "doc-anchor",
                            "--repro", "run it", "--suspect", suspect]
        if status:
            args += ["--status", status]
        if fid:
            args += ["--id", fid]
        code, out, err = run(args)
        self.assertEqual(code, 0, err)
        return out.strip()

    def test_create_and_validate(self):
        fid = self._new()
        self.assertEqual(fid, "FID-0001")
        path = os.path.join(self.dir, "FID-0001.json")
        self.assertTrue(os.path.isfile(path))
        obj = json.load(open(path))
        self.assertEqual(obj["status"], "discovered")
        self.assertEqual(obj["history"][0]["from"], "")
        code, out, err = run(self.base + ["validate"])
        self.assertEqual(code, 0, err)

    def test_transition_without_evidence_fails(self):
        self._new()
        code, out, err = run(self.base + ["transition", "FID-0001", "--to", "triaged"])
        self.assertEqual(code, 1)
        self.assertIn("evidence", err.lower())
        # still discovered
        obj = self._load("FID-0001")
        self.assertEqual(obj["status"], "discovered")

    def test_transition_with_evidence_records_history(self):
        self._new()
        code, out, err = run(self.base + ["transition", "FID-0001", "--to", "triaged",
                                          "--evidence", "docs/fidelity/CHARTER.md#A", "--evidence-kind",
                                          "doc-anchor"])
        self.assertEqual(code, 0, err)
        obj = self._load("FID-0001")
        self.assertEqual(obj["status"], "triaged")
        self.assertEqual(len(obj["history"]), 2)
        self.assertEqual(obj["history"][-1]["from"], "discovered")
        self.assertEqual(obj["history"][-1]["to"], "triaged")
        self.assertEqual(run(self.base + ["validate"])[0], 0)

    def test_waived_requires_note_and_retest(self):
        self._new()
        # note but no retest
        code, _, err = run(self.base + ["transition", "FID-0001", "--to", "waived",
                                        "--note", "not now"])
        self.assertEqual(code, 1)
        self.assertIn("retest", err.lower())
        code, _, err = run(self.base + ["transition", "FID-0001", "--to", "waived",
                                        "--note", "not now", "--retest", "when FID-0002 verified"])
        self.assertEqual(code, 0, err)
        obj = self._load("FID-0001")
        self.assertEqual(obj["waiver"]["retest"], "when FID-0002 verified")

    def test_documented_only_for_parity_divergence(self):
        self._new(cls="port-defect")
        code, _, err = run(self.base + ["transition", "FID-0001", "--to", "documented",
                                        "--evidence", "docs/fidelity/CHARTER.md"])
        self.assertEqual(code, 1)
        self.assertIn("parity-divergence", err)

    def test_dedupe_check(self):
        self._new(title="Center-glass blend not accurate", suspect="gfx_pc.c:100")
        code, out, err = run(self.base + ["dedupe-check", "--title",
                                          "Center glass blend not accurate",
                                          "--suspect", "gfx_pc.c:200"])
        self.assertEqual(code, 0, err)
        self.assertIn("FID-0001", out)  # matched by title ratio and/or same suspect file

    def test_actionable_respects_blocked_on(self):
        # blocker in triaged (not verified) -> dependent not actionable
        self._new(title="Blocker", status="triaged", fid="FID-0032")
        code, out, err = run(self.base + ["new", "--title", "Dependent", "--class",
                                          "parity-divergence", "--surface", "sim",
                                          "--priority", "P1", "--evidence", "docs/fidelity/CHARTER.md",
                                          "--status", "triaged", "--blocked-on", "FID-0032"])
        self.assertEqual(code, 0, err)
        dep = out.strip()
        code, out, _ = run(self.base + ["list", "--actionable"])
        self.assertIn("FID-0032", out)     # blocker itself is actionable
        self.assertNotIn(dep, out)         # dependent is blocked
        # verify the blocker -> dependent becomes actionable
        for to in ("root-caused", "fix-in-progress", "landed", "verified"):
            # M6 (2026-07-10 review): transition now validates --evidence the same way
            # `validate` does, so it must be a real path, not a placeholder like "e".
            code, out, err = run(self.base + ["transition", "FID-0032", "--to", to,
                                              "--evidence", "docs/fidelity/CHARTER.md"])
            self.assertEqual(code, 0, err)
        code, out, _ = run(self.base + ["list", "--actionable"])
        self.assertIn(dep, out)

    def test_render_roundtrip(self):
        self._new()
        code, out, err = run(self.base + ["render"])
        self.assertEqual(code, 0, err)
        md = os.path.join(self.root, "LEDGER.md")
        self.assertTrue(os.path.isfile(md))
        self.assertIn("FID-0001", open(md).read())

    def test_stats_assert_closed(self):
        # M3 (2026-07-10 review): `new --status` no longer accepts terminal/closed
        # statuses (it minted findings directly into e.g. "verified" with no chain,
        # the cheapest way to fake closure) -- walk the real edge-set instead.
        fid = self._new()  # discovered
        for to in ("triaged", "root-caused", "fix-in-progress", "landed", "verified"):
            code, out, err = run(self.base + ["transition", fid, "--to", to,
                                              "--evidence", "docs/fidelity/CHARTER.md"])
            self.assertEqual(code, 0, err)
        code, out, err = run(self.base + ["stats", "--assert-closed"])
        self.assertEqual(code, 0, err)
        # add an open finding -> assert fails
        self._new(title="open one", status="triaged")
        code, out, err = run(self.base + ["stats", "--assert-closed"])
        self.assertEqual(code, 1)


if __name__ == "__main__":
    unittest.main()
