# Fidelity Program Charter

> This document is the binding charter for the S-Tier Faithfulness Program's
> Fidelity Flywheel loop. It is assembled **verbatim** from
> `docs/design/FAITHFULNESS_S_TIER_PLAN.md`: the Global Constraints, the §2
> scheduling policy, the §4 fix lifecycle, the Task 2.4 Step 3 ASM-review
> contract, and Appendix B's iteration prompt. When the plan and this file
> disagree, the plan is authoritative — regenerate this file to match.

---

## Global Constraints (Program Charter)

These bind every task and every loop iteration. They extend `docs/BACKLOG_v0.4.0.md` charter rules 1–10.

1. **Authority hierarchy (sim behavior):** retail ASM / jump tables (`GLOBAL_ASM` bodies, `glabel jpt_*`) > ares oracle captures > decomp C. The `#else` C reference bodies (`PORT_FIXME_STUBS && !NATIVE_PORT`) **lie** — never transcribe one into a native path. Re-derive from ASM/oracle and cite the anchor (e.g. `jpt_80054084`, oracle route name). Documented failure: the red-shard bug came from a skipped `#else`-only `totalsize` advance (`src/game/bondview.c:3307-3318`).
2. **Authority hierarchy (renderer behavior):** the renderer has no retail ASM — its ground truth is (a) ares RDP/pixel captures and (b) N64 RDP documentation. Renderer findings must cite an ares-side artifact (screenshot, RDP command trace) or a documented RDP semantic, never "looks wrong".
3. **Evidence monopoly:** no ledger finding is created, promoted, waived, or closed without a machine-checkable artifact (comparator JSON, trace path, gate log) referenced in the ledger entry. Agent assertion alone is never evidence.
4. **Ratchet invariant:** every fix lands with (a) a `GE007_*` negative-control A/B flag (fix default-ON for port-defects; default-OFF opt-in for n64-quirk mitigations), (b) a regression lane added to a gate suite, (c) **all** pre-existing gates green. No gate is ever weakened or deleted to make a change pass; waivers go through the waiver protocol (§4.6).
5. **Byte-identity contract:** the deterministic faithful path stays byte-identical under every default-off flag; `tools/sim_invariance_gate.sh` and the sim-hash lanes are hard gates. `Video.FovY` is sim state — never perturb it in gates.
6. **Determinism envelope for all automated runs:** `env -u GE007_DEBUG SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1 GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1` + `--deterministic` (fixed RNG seed 0x12345678, frozen input, synthetic clock). Never run game tests with audio audible. Leave `build/` on clean `main` after test sessions.
7. **Small, evidenced commits:** one finding (or one task) per commit; commit message cites the ledger ID and evidence path. ROM-derived artifacts never committed (`scripts/ci/check_no_rom_data.sh` is a pre-commit hook); goldens live in `baselines/`, per-run captures in `/tmp/mgb64_*_$$`.
8. **Classification taxonomy (from the 2026-07-06 audit):** every behavioral finding is exactly one of `port-defect` (port diverges from retail — fix, default-on), `parity-divergence` (intentional port behavior differing from retail — document + A/B flag + faithful-mode default decision), `n64-quirk` (retail bug faithfully reproduced — preserve; player-helping mitigation only behind opt-in flag), `instrumentation-gap`, or `coverage-gap`.
9. **No silent caps:** any lane that bounds coverage (top-N routes, sampled frames, skipped stages) must print what it skipped into its report artifact.
10. **Loop safety:** the loop never force-pushes, never touches `origin`/`public` remotes, never edits `.github/workflows/`, never deletes baselines, and stops (writes `docs/fidelity/ESCALATIONS.md` and halts the lane) rather than guessing when evidence is contradictory.

---

## §policy — Loop scheduling policy (one iteration = one unit of work)

```
if verify_all is red on HEAD:            phase = REPAIR   (fix the regression first; nothing else)
elif ledger has fix-in-progress:         phase = ACT      (finish it; one finding at a time)
elif ledger has actionable P0/P1:        phase = ACT      (highest priority, oldest first)
elif any sense lane stale (> N iters):   phase = SENSE    (run stalest lane, harvest)
elif coverage critic has open items:     phase = EXPAND   (add routes/fields/lanes)
else:                                    phase = DRY-CHECK (full sense sweep; 2 consecutive
                                                            empty sweeps ⇒ program exit criteria audit)
```

Blocked findings (`blocked(oracle)`) become actionable automatically when the blocking instrumentation task's ledger entry reaches `verified` — the ledger stores the dependency edge (`blocked_on: FID-00NN`).

---

## §act — The Act Pipeline: fix lifecycle (normative for all Phase 2+ work)

Every finding the loop acts on moves through these stages; each stage's exit produces evidence attached to the ledger transition.

**4.1 Root-cause.** Locate the authoritative reference: for sim — the `GLOBAL_ASM` body + `jpt_*` tables for the function(s) involved (grep the VRAM address from the symbol name, e.g. `sub_GAME_7F0B0914`); for renderer — the ares capture (pixel/RDP-stream) demonstrating stock behavior; for converters — the raw ROM bytes at the documented offset. Write the divergence statement: *retail does X (anchor), port does Y (file:line), player-visible effect Z, repro command R*. Ledger → `root-caused`.

**4.2 Fix behind a flag.** Implement per charter rules 4–5. `port-defect` ⇒ fix default-ON with `GE007_NO_<FIX>`/`GE007_LEGACY_<X>` negative control; `n64-quirk` mitigation ⇒ opt-in default-OFF. Sim fixes must cite the ASM anchor in a comment at the change site (existing house style — see `gun.c:18327-18331`).

**4.3 Oracle-verify.** Run the narrowest oracle lane that demonstrates the fix: trace comparator on the relevant route (fix-ON matches stock where it previously diverged; fix-OFF reproduces the divergence — both artifacts saved). For visual fixes, pixel/ROI comparator before/after. Ledger → `landed` on commit.

**4.4 Regression lane.** Add a permanent guard: either a new scene/route in an existing suite (preferred — e.g. add a route JSON + comparator thresholds to the trace sweep gate set) or a new `tools/` smoke registered in ctest. Append to `verify_manifest.txt` if it's a new command.

**4.5 Full verify.** `tools/fidelity/verify_all.sh` (tier per change class: renderer-only ⇒ tiers 1–2 + parity captures; sim-touching ⇒ all tiers). Green ⇒ commit with ledger ID in message; ledger → `verified` with the verify report as evidence. Red ⇒ REPAIR phase (fix or revert; never commit red).

**4.6 Waivers, quirks, and the approximation registry.** Three documents make "explained divergence" auditable:
- `docs/fidelity/APPROXIMATIONS.md` — renderer approximation classes accepted as non-goals for pixel-exactness (VI output filter, 3-point-filter screen-derivative approximation, RDP dither, coverage AA), each with: why, bound (max expected pixel delta / affected region class), and the comparator masks/thresholds that encode it.
- `docs/fidelity/QUIRKS.md` — n64-quirk registry (FID-0045): retail bugs faithfully preserved (`chraidata.c:715` guard shuffle, etc.), each with ASM anchor and the opt-in mitigation flag if one exists.
- Ledger `waived` status — requires `waiver.reason` + `waiver.retest` (a concrete condition, e.g. "re-test when FID-0032 verified"). The coverage critic (S6) re-opens waivers whose retest condition has become true.

---

## §audit — ASM-vs-C review contract (Task 2.4 Step 3)

This is the review contract the loop's audit agent executes for each `GLOBAL_ASM` audit-queue entry (stored here so the future audit agent has it):

> Given entry E: read the `GLOBAL_ASM` body + jump tables; reconstruct control flow (branches, switch groupings, early-outs, arithmetic including fixed-point shifts); diff against the live C body; for any semantic difference, write the divergence statement (§act 4.1) and file a candidate finding with the instruction-level citation; else mark verified-equivalent with a 2-line justification. **Never consult the `#else` C reference body as authority.**

Verdict values recorded per entry in `docs/fidelity/asm_audit_queue.json`:
`unreviewed | verified-equivalent | finding-filed:FID-NNNN | waived:<reason>`.

Risk-rank order for the queue: (a) files with prior confirmed defects first (gun.c, bondview.c, chrobjhandler.c, stan.c, prop.c converters), (b) then by instruction count × has-jump-table (dispatch semantics are the proven failure mode), (c) bg.c/model.c bulk last.

Pilot calibration: the review contract wording must be able to re-derive the known-fixed weapon-switch case (`jpt_80054084` groupings) from the ASM alone.

---

## Appendix B — Loop iteration prompt (the unattended-run charter)

```
You are running one iteration of the mgb64 Fidelity Flywheel. Read docs/fidelity/CHARTER.md
(binding rules) and run: tools/fidelity/loop_iteration.sh status

Pick exactly one phase per the scheduling policy in CHARTER.md §policy:
  REPAIR   — verify is red on HEAD: root-cause and fix the regression or revert the offending
             commit. Nothing else until green.
  ACT      — take the top actionable finding. Follow the fix lifecycle (charter §act):
             root-cause with an ASM/oracle anchor → fix behind the correct flag polarity →
             oracle-verify both flag sides → add a regression lane → run
             tools/fidelity/verify_all.sh at the tier for your change class →
             loop_iteration.sh checkpoint "fix(<area>): <title> [FID-NNNN]" →
             ledger.py transition. If evidence contradicts the ledger's root-cause,
             transition it back with a note instead of forcing the fix.
  SENSE    — run the stalest lane via loop_iteration.sh sense <lane>; triage its candidates
             (dedupe, classify per charter taxonomy, set priority); do not fix anything.
  EXPAND   — take the top coverage-critic item (usually: author one oracle route per
             Task 1.2 method, or add a comparator field).
  DRY-CHECK— full sense sweep; if zero candidates twice consecutively, run
             ledger.py stats --assert-closed and produce the S-tier candidate report.

Hard rules: one finding or one lane per iteration. Never commit with verify red. Never
weaken a gate, threshold, or baseline without a ledger entry created FIRST. If the same
finding fails verify twice, append an escalation entry naming it to
docs/fidelity/ESCALATIONS.md (escalated findings drop out of the actionable list) and
move on. If evidence is contradictory or an action is
irreversible (deleting baselines, force-push, remote push), escalate instead of acting.
All game executions use the determinism envelope from CHARTER.md rule 6. End the iteration
at a checkpoint commit and report: phase chosen, work done, evidence paths, ledger deltas.
```
