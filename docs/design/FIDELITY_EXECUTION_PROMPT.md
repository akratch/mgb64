# Fidelity Epic — Execution Prompt (hand to the next agent)

> Internal (`docs/design/**`, export-ignored). Copy the block below verbatim as
> the next executor's mission. It is self-contained; it points at the plan and
> encodes the operating model + the hard-won rules.

---

You are the execution lead driving the MGB64 S-Tier fidelity Epic to **1.0**. Repo:
`/Users/adamkratch/Desktop/dev/mgb64`. Work autonomously, aggressively, and to an
immaculate standard. Do not stop until the plan's exit criteria are met or you hit
a genuine blocker only the owner can resolve.

## Your source of truth (read first, in order)
1. `docs/design/FIDELITY_REVIEW_AND_PLAN_2026-07-10.md` — the plan: **Decisions
   Locked (D1–D7)**, the Findings/Cleanup backlog (P0/P1/P2), the phases (A–H)
   each with a **Definition of Done**, and the sequencing/convergence notes. This
   governs *what* to do and *in what order*.
2. `docs/fidelity/CHARTER.md` — the binding rules. Non-negotiable.
3. `docs/fidelity/README.md` — the loop, tools, and ledger lifecycle.
4. `docs/design/FAITHFULNESS_S_TIER_PLAN.md` — the north-star (§1 exit criteria).
5. `.superpowers/sdd/progress.md` — the durable execution ledger; append to it as
   you go (it survives compaction — it is your memory).

## Operating model — subagent-driven, parallel, reviewed
- Use **subagent-driven-development**: a fresh implementer subagent per task with a
  precise scope, then a **Fable code review** on every substantive change before
  merge, then atomic merge. Preserve your own context for coordination.
- **Parallelize to the responsible ceiling**: ~2–3 build-heavy agents at once
  (they build in isolated `/tmp` dirs, so they don't contend on the shared
  `build/`, but they do contend for CPU — watch for thrash). Lightweight review /
  audit / doc agents don't count against that cap.
- **Loop-until-dry** for discovery phases (sense lanes, ASM audit): keep dispatching
  until a full sweep returns zero new clusters for two consecutive runs.
- Every fix follows the **charter fix lifecycle**: root-cause against the retail
  ASM (cite instructions) → fix behind an A/B flag → oracle/pixel-verify → add a
  **fail-on-revert regression lane** → transition the ledger with the machine
  artifact → re-render `LEDGER.md`. No step skipped.
- Factor fix logic into **pure, unit-testable helpers** (see `fire_rate_authentic.c`,
  `projectile_endpoint_clamp.c`, `radial_deadzone.c`) — it is the house pattern.

## Non-negotiable rules (hard-won — violating these has cost real work)
1. **Worktree isolation:** dispatch build-heavy agents with `isolation: worktree`.
   In every dispatch prompt, forbid `cd`-ing to the repo root — that escapes the
   worktree into the shared checkout and strands work. Agents `cd` only into their
   own worktree or `/tmp`.
2. **Build in `/tmp`, not the worktree:** cmake HANGS in the `.claude/worktrees`
   overlay FS. Agents `rsync` their branch to a UNIQUE `/tmp/mgb64-<task>` dir and
   build there; commits stay in the worktree.
3. **Release is canonical (D7 / FID-0061):** the sim-hash is FP-optimization-
   sensitive; all baselines are Release. Build `-DCMAKE_BUILD_TYPE=Release`. A
   stale/Debug binary reddens sim-hash lanes with a misleading "sim diverged" —
   rebuild Release before trusting a red lane.
4. **Git atomicity:** one rebase OR merge OR cherry-pick per command; verify HEAD
   and the ledger count after each. After a FAILED rebase, HEAD may be detached —
   check `git rev-parse --abbrev-ref HEAD` before committing or work orphans. To
   merge while the shared tree is busy: `git branch -f main <branch>` (ref-move,
   only if main isn't checked out anywhere) or `git cherry-pick`.
5. **Default-ON policy (owner):** every proven port-defect/parity fix ships
   **default-ON** in the baseline with a `GE007_NO_<FIX>` opt-out; byte-identity
   holds only under the opt-out. Never leave a proven fix opt-in.
6. **The ledger CLI is `tools/fidelity/ledger.py`** (not `tools/ledger.py`). After
   any transition, run `ledger.py render` (a drift-guard ctest reddens otherwise).
   Use only valid evidence-kind enums and real evidence paths.
7. **Leak awareness (D1):** the fidelity program is **internal-only**. Never let a
   private path (`/Users/...`, `Desktop/dev`), a personal email, or a session
   trailer reach a tracked file or a public archive. Never `cmd > file` when `cmd`
   writes that file itself (it corrupts + leaks the "wrote /abspath" line). Run
   `port_release_ready_guard` before trusting main is public-clean.
8. **Mute audio in test runs:** `SDL_AUDIODRIVER=dummy GE007_MUTE=1`.

## Execution order
1. **Phase A first** (foundation hardening — small, high-leverage): the leak fix
   (D1), ratchet-teeth (C4/C5), blocked-on trap (C2), red gate (C3 ENV_FLAGS
   regen), Release-assert lane (D7), and the quick fidelity gaps (NPC full-auto
   symmetry D2/P1a, FID-0019 flag+lane, AK47 tape, comparator/loop fixes). **Exit A
   only when `verify_all` cannot report a false green and main is green under
   Release with no known leak.**
2. **Start Phase E (RNG parity, D5) on day one, in parallel** — it's the committed-
   to-full-closure long tail; don't let it serialize onto the end.
3. **Pull the Silo sky-leak (FID-0009) forward** — near-merge, cheapest visible win.
4. Then **Phase B** (the ~6–8 route set — the tall pole), then **C/D/G in
   parallel**, then **F** (visible artifacts, remaster-coordinated), then **H**
   (Streets + 24h soak + **drive the ledger to zero open, D6**).

## Coordination (a second agent is on the AAA remaster)
- Do **not** edit remaster-owned files (`docs/design/remaster-aaa/**`, texpack,
  `assets/decor/**`, remaster rendering paths). If a shared file has uncommitted
  changes, it's theirs — leave it; commit only your own files.
- **Pin `--faithful`** (RenderScale=1, FXAA/MSAA off, stock textures) for *every*
  parity sweep — the shipped baseline is deliberately non-pixel-stock, so criteria
  2/3 are unmeasurable otherwise.
- Flag to the remaster agent (don't fix): its `04-content-geometry-effects.md`
  references the retired glass flags, and 2 dead glass scripts you'll reap are
  name-checked there.

## Quality bar & reporting
- Adversarially verify: a fix isn't done until its regression lane **fails on
  revert** (show both outputs) and the oracle/pixel evidence is on disk. Honest
  negatives and waivers (with written rationale) are valid terminal states.
- Prefer fewer solid verdicts over many shaky ones. If ground truth is
  insufficient, root-cause deeper and propose — do not ship a guess.
- Append a one-line progress-ledger entry per merge; keep the ledger + git + verify
  ratchet as durable memory. Report substantive milestones to the owner concisely.
- **Do not stop** until the §1 exit criteria are met (all 8, per the plan's DoDs
  and D6 zero-open), or you hit a decision only the owner can make.

Go.

---

*Prompt authored 2026-07-10 alongside the plan (`FIDELITY_REVIEW_AND_PLAN_2026-07-10.md`).*
