# Escalations Log

The loop's stop-instead-of-guess record (charter rule 10 and Appendix B).
An entry is appended here when:

- the same finding fails verify twice (it drops out of the actionable list),
- evidence is contradictory and acting would mean guessing, or
- the required action is irreversible (deleting baselines, force-push,
  remote push) and therefore owner-only.

Escalated findings stay in the ledger with their last honest status; the
entry here names the finding, the contradiction or blocker, and what an
owner/controller must decide to unblock it.

Format, one entry per escalation:

```
## YYYY-MM-DD FID-NNNN — short title
- Lane/phase:
- What was attempted (evidence paths):
- The contradiction or irreversible action:
- Decision needed from owner/controller:
- Resolution (filled when closed):
```

## 2026-07-10 C1/D1 — fidelity program + personal email already on the public remote
- Lane/phase: Phase A (C1 leak fix), FIDELITY_REVIEW_AND_PLAN_2026-07-10 P0.
- What was attempted: C1 forward-fix branch (export-ignore for the fidelity
  trees + path-gap docs, email scrub, archive-reachability guard) — lands as
  forward protection. During verification the controller confirmed against the
  live remote: `origin/main` (the public repo) ALREADY contains
  `docs/fidelity/**`, `tools/fidelity/**`, `baselines/tapes/**`, the personal
  email address in both public-launch scripts, and ~86 commits carrying
  AI-authorship/session trailers — the ongoing publish flow is a plain
  `git push origin main`, which never consults `export-ignore`.
- The contradiction / irreversible action: Decision D1 declared the program
  internal-only, but it is already published. Removing it retroactively
  requires rewriting or replacing public-repo history — irreversible and
  outward-facing, therefore owner-only (charter rule 10; the loop never
  touches remotes).
- Decision needed from owner: (a) rewrite/replace the public repo history to
  expunge the fidelity trees + email + trailers, (b) accept the program as
  public retroactively and revise D1 to forward-only (new content stays
  internal via the landed guards), or (c) accept as-is including future
  pushes (revoking D1). Until decided, NO further public push should include
  new `docs/fidelity/**` / `tools/fidelity/**` / `baselines/tapes/**` content
  — note that a plain `git push origin main` of current local main WOULD do
  so; the landed guard blocks the archive/bundle paths only. The email scrub
  is on local HEAD either way and shrinks the exposure on the next push.
- Resolution: OPEN.
