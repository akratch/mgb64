#!/usr/bin/env python3
"""Unit tests for tools/fidelity/ledger.py (C5, 2026-07-10 review).

Covers the two ratchet teeth added by C5:
  1. the legal transition edge-set enforced by `cmd_transition` -- no
     skip-ahead (ever), no backward move or reopening a terminal status
     without --reopen (+ --note), and --reopen may only land on a
     non-terminal status;
  2. the evidence-path existence check added to `validate` (a real
     repo-relative file, a `path#section` anchor whose file part exists, a
     `ctest:<name>` shorthand for a name registered in CMakeLists.txt, a
     bare filename that resolves unambiguously, or a path under
     docs/fidelity/reports/ that does NOT escape it via `..` traversal).

ROM-free, pure Python; every test operates inside a throwaway temp
directory via `--ledger-dir` / a synthetic repo root and never touches the
real docs/fidelity/ledger/. Registered with ctest as fidelity_ledger_unittest.
"""
import contextlib
import io
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
FID_DIR = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, FID_DIR)

import ledger  # noqa: E402


def run_cli(argv):
    """Invoke the ledger CLI in-process; return (rc, stdout, stderr)."""
    args = ledger.build_parser().parse_args(argv)
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        rc = args.func(args)
    return rc, out.getvalue(), err.getvalue()


class LedgerTransitionTestCase(unittest.TestCase):
    """Base fixture: a single fresh finding (class=port-defect) in a temp ledger dir."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.ld = self._tmp.name
        rc, out, err = run_cli([
            "--ledger-dir", self.ld, "new",
            "--title", "synthetic test finding", "--class", "port-defect",
            "--surface", "sim", "--priority", "P2",
            "--evidence", "docs/fidelity/CHARTER.md", "--evidence-kind", "doc-anchor",
        ])
        self.assertEqual(rc, 0, err)
        self.fid = out.strip()

    def tearDown(self):
        self._tmp.cleanup()

    def transition(self, to, evidence=None, note=None, retest=None, reopen=False):
        argv = ["--ledger-dir", self.ld, "transition", self.fid, "--to", to]
        if evidence is not None:
            argv += ["--evidence", evidence]
        if note is not None:
            argv += ["--note", note]
        if retest is not None:
            argv += ["--retest", retest]
        if reopen:
            argv.append("--reopen")
        return run_cli(argv)

    def status(self):
        return ledger.load_entry(self.ld, self.fid)["status"]

    def history(self):
        return ledger.load_entry(self.ld, self.fid)["history"]


class TestLegalEdgesHelper(unittest.TestCase):
    """Direct unit tests on legal_edges_from(), independent of the CLI plumbing."""

    def test_open_state_reaches_next_forward_and_all_branches(self):
        self.assertEqual(
            ledger.legal_edges_from("triaged"),
            {"root-caused", "documented", "refuted", "waived"})

    def test_landed_reaches_verified_and_branches(self):
        self.assertEqual(
            ledger.legal_edges_from("landed"),
            {"verified", "documented", "refuted", "waived"})

    def test_closed_states_have_no_edges(self):
        for closed in ("verified", "documented", "refuted", "waived"):
            self.assertEqual(ledger.legal_edges_from(closed), set(),
                              f"{closed} should have no outgoing edges")


class TestForwardChain(LedgerTransitionTestCase):
    def test_single_step_forward_succeeds(self):
        rc, _, err = self.transition("triaged", evidence="docs/fidelity/CHARTER.md")
        self.assertEqual(rc, 0, err)
        self.assertEqual(self.status(), "triaged")

    def test_full_chain_to_verified(self):
        for dst in ("triaged", "root-caused", "fix-in-progress", "landed", "verified"):
            rc, _, err = self.transition(dst, evidence="docs/fidelity/CHARTER.md")
            self.assertEqual(rc, 0, f"{dst}: {err}")
        self.assertEqual(self.status(), "verified")

    def test_promotion_without_evidence_still_rejected(self):
        # C5 edge-set enforcement must not bypass the pre-existing evidence-
        # monopoly check (charter rule 3).
        rc, _, err = self.transition("triaged")
        self.assertNotEqual(rc, 0)
        self.assertIn("--evidence", err)
        self.assertEqual(self.status(), "discovered")


class TestSkipAhead(LedgerTransitionTestCase):
    def test_skip_ahead_rejected(self):
        rc, _, err = self.transition("landed", evidence="docs/fidelity/CHARTER.md")
        self.assertNotEqual(rc, 0)
        self.assertIn("not a legal edge", err)
        self.assertEqual(self.status(), "discovered")

    def test_skip_ahead_not_rescued_by_reopen(self):
        # --reopen authorizes walking a finding BACK, never fast-forwarding it.
        rc, _, err = self.transition("landed", evidence="docs/fidelity/CHARTER.md",
                                      reopen=True, note="not a legitimate use")
        self.assertNotEqual(rc, 0)
        self.assertEqual(self.status(), "discovered")


class TestBackwardAmongOpenStates(LedgerTransitionTestCase):
    def setUp(self):
        super().setUp()
        for dst in ("triaged", "root-caused", "fix-in-progress"):
            rc, _, err = self.transition(dst, evidence="docs/fidelity/CHARTER.md")
            self.assertEqual(rc, 0, err)

    def test_backward_without_reopen_rejected(self):
        rc, _, err = self.transition("triaged", evidence="docs/fidelity/CHARTER.md")
        self.assertNotEqual(rc, 0)
        self.assertIn("--reopen", err)
        self.assertEqual(self.status(), "fix-in-progress")

    def test_backward_with_reopen_but_no_note_rejected(self):
        rc, _, err = self.transition("triaged", evidence="docs/fidelity/CHARTER.md",
                                      reopen=True)
        self.assertNotEqual(rc, 0)
        self.assertIn("--note", err)
        self.assertEqual(self.status(), "fix-in-progress")

    def test_backward_with_reopen_and_note_succeeds_and_is_recorded(self):
        rc, _, err = self.transition("triaged", evidence="docs/fidelity/CHARTER.md",
                                      reopen=True, note="evidence contradicted root-cause")
        self.assertEqual(rc, 0, err)
        self.assertEqual(self.status(), "triaged")
        self.assertTrue(self.history()[-1].get("reopen"))


class TestTerminalStates(LedgerTransitionTestCase):
    def _reach_verified(self):
        for dst in ("triaged", "root-caused", "fix-in-progress", "landed", "verified"):
            rc, _, err = self.transition(dst, evidence="docs/fidelity/CHARTER.md")
            self.assertEqual(rc, 0, err)

    def test_verified_has_no_outgoing_edge_without_reopen(self):
        self._reach_verified()
        rc, _, err = self.transition("landed", evidence="docs/fidelity/CHARTER.md")
        self.assertNotEqual(rc, 0)
        self.assertIn("--reopen", err)
        self.assertEqual(self.status(), "verified")

    def test_verified_reopen_to_open_state_succeeds(self):
        self._reach_verified()
        rc, _, err = self.transition("landed", evidence="docs/fidelity/CHARTER.md",
                                      reopen=True, note="regression found post-verify")
        self.assertEqual(rc, 0, err)
        self.assertEqual(self.status(), "landed")

    def test_reopen_cannot_land_on_another_terminal(self):
        self._reach_verified()
        rc, _, err = self.transition("refuted", reopen=True, note="reopen misuse")
        self.assertNotEqual(rc, 0)
        # M4 (2026-07-10 review): the message used to print the full FORWARD list
        # (which includes "verified") as if it were the legal --reopen target set;
        # it must name only the actual open lifecycle states and must not claim
        # "verified" is a valid landing spot.
        self.assertIn("open lifecycle state", err)
        self.assertNotIn("'verified']", err)
        self.assertEqual(self.status(), "verified")


class TestBranchTerminals(LedgerTransitionTestCase):
    def test_waived_reachable_directly_from_discovered(self):
        rc, _, err = self.transition("waived", note="known n64-quirk",
                                      retest="re-test if FID-9999 verified")
        self.assertEqual(rc, 0, err)
        self.assertEqual(self.status(), "waived")

    def test_refuted_reachable_directly_from_discovered(self):
        rc, _, err = self.transition("refuted", note="not reproducible")
        self.assertEqual(rc, 0, err)
        self.assertEqual(self.status(), "refuted")

    def test_documented_still_gated_on_parity_divergence_class(self):
        # class=port-defect (see setUp) -- edge-set allows the transition, but the
        # pre-existing class gate must still fire.
        rc, _, err = self.transition("documented", evidence="docs/fidelity/CHARTER.md")
        self.assertNotEqual(rc, 0)
        self.assertIn("parity-divergence", err)


class TestSelfTransition(LedgerTransitionTestCase):
    def test_self_transition_rejected(self):
        rc, _, err = self.transition("discovered", evidence="docs/fidelity/CHARTER.md")
        self.assertNotEqual(rc, 0)
        self.assertIn("self-transition", err)


class TestEvidenceTargetOk(unittest.TestCase):
    """evidence_target_ok() against a synthetic repo root (never the real repo)."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = self._tmp.name
        os.makedirs(os.path.join(self.root, "docs", "fidelity", "reports"))
        os.makedirs(os.path.join(self.root, "src", "game"))
        with open(os.path.join(self.root, "src", "game", "gun.c"), "w", encoding="utf-8") as f:
            f.write("// stub\n")
        with open(os.path.join(self.root, "docs", "fidelity", "reports", "sample.log"),
                  "w", encoding="utf-8") as f:
            f.write("log\n")
        with open(os.path.join(self.root, "CMakeLists.txt"), "w", encoding="utf-8") as f:
            f.write("add_test(NAME my_cool_gate COMMAND foo)\n"
                     "add_port_validation_smoke(port_my_smoke script.sh 30)\n")
        self.ctest_names = ledger._known_ctest_names(self.root)
        self.basenames = ledger._basename_index(self.root)

    def tearDown(self):
        self._tmp.cleanup()

    def check(self, raw, basenames=None):
        return ledger.evidence_target_ok(
            raw, self.root, self.ctest_names, basenames or self.basenames)

    def test_real_file_ok(self):
        ok, detail = self.check("src/game/gun.c")
        self.assertTrue(ok, detail)

    def test_missing_file_rejected(self):
        ok, _ = self.check("src/game/does_not_exist.c")
        self.assertFalse(ok)

    def test_anchor_only_file_part_checked(self):
        ok, detail = self.check("src/game/gun.c#some-heading-that-need-not-exist")
        self.assertTrue(ok, detail)

    def test_line_citation_and_trailing_prose(self):
        ok, detail = self.check("src/game/gun.c:1234 (some prose about a jump table)")
        self.assertTrue(ok, detail)

    def test_known_ctest_shorthand_ok(self):
        ok, detail = self.check("ctest:my_cool_gate")
        self.assertTrue(ok, detail)

    def test_known_port_validation_smoke_shorthand_ok(self):
        ok, detail = self.check("ctest:port_my_smoke")
        self.assertTrue(ok, detail)

    def test_unknown_ctest_shorthand_rejected(self):
        ok, _ = self.check("ctest:no_such_gate")
        self.assertFalse(ok)

    def test_reports_dir_real_file_exempt(self):
        ok, detail = self.check("docs/fidelity/reports/sample.log")
        self.assertTrue(ok, detail)

    def test_reports_dir_nonexistent_file_still_exempt(self):
        # The whole tree is .gitignore'd by design (ephemeral per-run captures) --
        # a reference to a file that ran and was captured in some other session
        # must not be flagged just because THIS checkout never generated it.
        ok, detail = self.check("docs/fidelity/reports/some_other_run.log")
        self.assertTrue(ok, detail)

    def test_reports_dir_path_traversal_rejected(self):
        # This is the exact shape of the original FID-0046 bug: a '..'-laden path
        # that starts with the reports/ prefix as a string but actually escapes it.
        ok, detail = self.check("docs/fidelity/reports/../../../etc/passwd")
        self.assertFalse(ok, detail)

    def test_empty_path_rejected(self):
        ok, _ = self.check("")
        self.assertFalse(ok)

    def test_bare_filename_unique_basename_ok(self):
        ok, detail = self.check("gun.c")
        self.assertTrue(ok, detail)

    def test_bare_filename_ambiguous_rejected(self):
        os.makedirs(os.path.join(self.root, "src", "other"))
        with open(os.path.join(self.root, "src", "other", "gun.c"), "w", encoding="utf-8") as f:
            f.write("dup\n")
        basenames = ledger._basename_index(self.root)
        ok, _ = self.check("gun.c", basenames=basenames)
        self.assertFalse(ok)


class TestValidateCatchesBadEvidence(unittest.TestCase):
    """End-to-end: cmd_validate must surface an invalid evidence path as a violation,
    and accept a corrected one -- this is the FID-0046 regression shape."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.ld = self._tmp.name

    def tearDown(self):
        self._tmp.cleanup()

    def _new(self, evidence):
        rc, out, err = run_cli([
            "--ledger-dir", self.ld, "new",
            "--title", "validate-evidence probe", "--class", "instrumentation-gap",
            "--surface", "sim", "--priority", "P2",
            "--evidence", evidence, "--evidence-kind", "gate-log",
        ])
        self.assertEqual(rc, 0, err)
        return out.strip()

    def test_malformed_traversal_evidence_fails_validate(self):
        self._new("docs/fidelity/reports/../../../private/tmp scratchpad x.md")
        rc, _, err = run_cli(["--ledger-dir", self.ld, "validate"])
        self.assertNotEqual(rc, 0)
        self.assertIn("evidence path invalid", err)

    def test_real_gate_script_evidence_passes_validate(self):
        self._new("tools/uncap_purity_gate.sh")
        rc, out, _ = run_cli(["--ledger-dir", self.ld, "validate"])
        self.assertEqual(rc, 0)
        self.assertIn("ledger OK", out)


if __name__ == "__main__":
    unittest.main()
