# Fidelity Effort — Code Review & Epic Completion Plan (2026-07-10)

> Internal design doc (`docs/design/**`, export-ignored). Consolidates a 6-lane
> read-only review of the S-Tier fidelity effort and lays out the plan to
> complete the Epic toward 1.0. Source reports: `scratchpad/review-lane-{A..F}-*.md`.

**Review method.** Six parallel read-only lanes: A sim-fix correctness (Fable),
B renderer-fix correctness (Fable), C fidelity tooling (Opus), D docs & repo
hygiene (Sonnet), E ledger integrity (Sonnet), F systemic + Epic map (Opus).

**Headline verdict.** The fixes are sound — **0 Critical, 0 High in the code
itself**; every opt-out path was verified byte-identical, both renderer folds
are correct, sim-purity holds, and the glass algebra is an exact identity. The
real weaknesses are in the **connective tissue**: the ratchet has inert teeth in
some paths, the ledger's machine-readable fields lag its own prose, a red gate is
live on main, one fix created an unledgered balance skew, and **the internal
program would leak into the public archive**. All fixable and well-scoped.

---

## Decisions Locked (owner, 2026-07-10)

These bind the plan below. Owner chose maximal fidelity on the strategic forks.

| # | Decision | Choice | Consequence for the plan |
|---|----------|--------|--------------------------|
| D1 | Fidelity program visibility | **Internal-only** | Phase A: add `export-ignore` for `docs/fidelity/**`, `tools/fidelity/**`, `baselines/tapes/**` + the two path-gap docs (`BACKLOG_v0.4.0.md`, dated `RECOMP_LANDSCAPE_SURVEY`); scrub the personal email from tracked scripts; strip Claude/session trailers on the public-publish path. **Gate before any public push.** |
| D2 | Enemy full-auto cadence | **Fix symmetrically** | P1a: tick-scale the guards' fire counter (`chrlv.c:8297`) behind the same `Input.FireRateAuthentic`, so both player and enemies fire at true N64 cadence. Faithful; enemies fire slower too. |
| D3 | MSAA default level | **Keep SSAA+FXAA, MSAA off** | No default change. `Video.MSAA=0` stays (baseline already AAs via 2× SSAA + FXAA); MSAA remains a working opt-in knob. Update the stale "OFF by design" comments to read as a settled decision, not a TODO. |
| D4 | Route-coverage scope (crit. 1) | **Representative subset ~6-8** | Phase B: Dam + a spread across archetypes (indoor / outdoor / vehicle / boss / stealth), each with a `gate:true` ares-oracle route. Expandable post-1.0. |
| D5 | FID-0063 RNG parity | **Commit to full closure** | Phase E is now a HARD requirement, not a waiver option: pursue call-for-call `randomGetNext()` parity until native matches retail exactly. Biggest single risk/effort in the Epic; start its investigation early (parallel with B/C). No "faithful-within-tolerance" exit. |
| D6 | Ledger-closure bar (crit. 8) | **Zero open (all terminal)** | Phase H: EVERY finding (64+ and growing) must reach verified / documented / refuted / waived. No P2/P3 backlog carried past 1.0 — the purest bar. Every open item is either fixed or has a written terminal rationale. |
| D7 | Cross-config determinism | **Release-canonical + assert lane** | Phase A: add a cheap lane asserting the running binary is Release (a Debug build gives a clear message, not a false RED). strict-FP deferred as unnecessary for 1.0. |

**Auto-handled (no decision needed; scheduled in Phase A):** ENV_FLAGS regen (clears the red gate C3); FID-0019 opt-out flag + real shadow lane retrofit (P1b); `verify_all` SKIP → amber locally / hard-fail in the release gate (C4); blocked-on trap fix — add FID-0062 to FID-0011/12/13/54 (C2); `ledger validate` verifies evidence paths exist + scrub FID-0046's malformed private-path evidence (C5).

---

## Part 1 — Findings & Cleanup Backlog

Severity is the review's own; ★ = confirmed live this synthesis.

### P0 — Integrity, correctness, leak (do before any public push or unattended loop)

| # | Sev | Finding | Fix |
|---|-----|---------|-----|
| C1 | HIGH ★ | **Public-leak surface** (Lane D). `docs/fidelity/**`, `tools/fidelity/**`, `baselines/tapes/**` have no `export-ignore`; `BACKLOG_v0.4.0.md` + dated `RECOMP_LANDSCAPE_SURVEY` defeat the globs; personal Gmail in 2 tracked `scripts/*.sh`; 86 commits carry Claude/session trailers; the WORKFLOW "public=source" model bypasses `export-ignore` entirely. | **Decided D1 = internal-only.** (1) Add `export-ignore` to `.gitattributes` for the three trees + the two path-gap docs; (2) `git archive HEAD` test confirms they no longer appear; (3) scrub the Gmail from the 2 scripts; (4) determine the actual publish flow — if `export-ignore` is not load-bearing (direct-to-public), add the guard to the push/publish path AND a trailer-strip step. Then extend `port_release_ready_guard` to assert none of the internal trees are archive-reachable, so this can't regress. **Gate before next public release.** |
| C2 | HIGH ★ | **Blocked-on integrity trap** (Lane E). FID-0011/0012/0013 are `blocked_on: FID-0032` (now `verified`) → `is_actionable()` marks them ready, but their prose says they need FID-0062 re-validation; FID-0054 has the same issue with no `blocked_on`. An unattended loop would act on stale/inverted combat findings. | Add `FID-0062` (or a new "combat-realign" gate) to `blocked_on` of FID-0011/0012/0013/0054 until FID-0062 is `verified`. |
| C3 | IMP ★ | **`env_reference_current` RED on main** (Lane A+D). ENV_FLAGS header says 904, table has 905 rows; `--check` exits 1 → fails tier-1 verify. | One-command regen: `python3 tools/gen_env_reference.py --repo-root . --out docs/ENV_FLAGS.md`. (Coordinate — remaster agent has an uncommitted CMakeLists.txt and may be adding texpack flags.) |
| C4 | MED | **`verify_all.sh` fail-open** (Lane C). Verdict green when every gate SKIPs → in ROM-less/ares-less CI the ratchet silently degrades to tier-1 static checks yet reports green. | Make a SKIP of a required tier count as non-pass (or emit an explicit "degraded, N gates skipped" verdict that CI treats as amber, not green). |
| C5 | MED | **Ledger gating is bypassable** (Lane C+E). `transition` allows any status→any status (backward, skip, resurrect terminal); evidence is a non-empty-string check, path never verified. FID-0046's evidence is a malformed private-`/tmp` path that resolves to nothing. | Enforce a legal transition edge-set in `cmd_transition`; verify evidence paths exist (or are a recognized gate name) in `validate`; fix/scrub FID-0046's evidence. |

### P1 — Fidelity correctness & missing ratchets

| # | Sev | Finding | Fix |
|---|-----|---------|-----|
| P1a | IMP ★ | **NPC full-auto unfixed** (Lane A, `chrlv.c:8297-8308`). Guards use the same per-frame `firecount % rate` gate FID-0056 fixed for the player, so with the fix default-ON the player fires at N64 cadence while **guards fire ~3× it** — a player-vs-guard balance skew that didn't exist pre-fix. Unledgered. | File a FID. Fix symmetrically (tick-scale the guard counter behind the same `Input.FireRateAuthentic`) — the faithful choice, since retail gated both on the same frame counter. Add to the fire-rate regression evidence. |
| P1b | HIGH | **FID-0019 has no opt-out flag + no real lane** (Lane E). The Metal shadow fix landed with no `GE007_NO_*` (policy violation) and its cited lane doesn't exercise shadow scenes. | Add the opt-out flag; add a shadow-casting regression scene/assertion or downgrade its status honestly. |
| P1c | HIGH | **FID-0006 lane doesn't guard the fail path** (Lane E, self-admitted). | Strengthen the lane or re-open the finding. |
| P1d | IMP | **FID-0056 has no end-to-end gate** (Lane A+E). The unit test re-implements the gate; a gun.c call-site revert stays green. Canonical tape is PP7 (never hits the machinegun case). | Record the open AK47 sustained-fire tape as a `combat_route_capture` baseline so a call-site revert reddens. |
| P1e | IMP | **`step==1` assumptions beyond the noted one** (Lane A). Burst catch-up divide (`gun.c:18372`) and `field_88C<2` state timers (`gun.c:19058/19067`) mis-behave in authentic mode under `g_ClockTimer>1`. | Blocks F5/variable-clock: land the `>=`-accumulator refactor before uncapped-FPS work; widen the warning comment now. |
| P1f | MED | **`compare_combat_trace --align tick` silently drops data** (Lane C). One-sided empty-roster ticks are dropped pre-intersection, hiding whole-roster `guards[].present` divergences — the alignment FID-0062 leaned on. | Treat a one-sided full/empty-roster tick as a divergence, not a drop. Re-validate FID-0062's numbers after. |
| P1g | MED | **Loop sense lanes are inert** (Lane C). S5 (asm) runs `asm_audit.py` with no subcommand → argparse exit 2 swallowed → no-op; S3 (rdp) maps to a missing `sense_rdp_sweep.sh`; loop dedup swallows a 2nd distinct bug in an already-ledgered file (OR not AND on suspect file). | Fix the S5 invocation, build/land S3, tighten dedup to (file AND signature). |
| P1h | MED | **`sense_trace_sweep` uses wrong alignment** (Lane C, `:294`) — default `global`, not `--align tick`, so combat candidates use the mode the comparator itself calls untrustworthy. | Pass `--align tick`. |

### P2 — Hygiene, docs, minor code

- **Raw-getenv → registered-accessor batch** (all lanes): ~898/905 flags read via raw `getenv` → blank ENV_FLAGS type/default. The **fix-flags** to migrate first: `GE007_NO_AUTOSHOT_BULLETTYPE_FIX`, `GE007_NO_PROJECTILE_ENDPOINT_CLAMP_FIX`, `GE007_NO_CULL_ASPECT_FIX`, `GE007_NO_SKY_ASPECT_FIX`, `GE007_NO_CAMERA_SEED_WALK`, and the intro cluster. Inverse gap: the settings-registered `GE007_FIRE_RATE_AUTHENTIC`/`_N64_FRAME_COST` are **absent** from ENV_FLAGS (generator doesn't scan `settingsRegister` mirrors) — teach it to.
- **35 dead glass scripts** (Lane B/D): of 50 `tools/glass_*`, 12 wired, 3 semi-wired, 35 dead. Reap — but 2 are name-checked in the remaster AAA doc; **coordinate before deleting**.
- **Stale docs**: FAITHFULNESS_S_TIER_PLAN.md 97 checkboxes all unchecked (Phase 0 done); BACKLOG MG.3 "RESOLVED" note over a still-open checklist; 2 dead CHARTER links (QUIRKS.md, ESCALATIONS.md); stale "OFF by default" comments (`gun.c:42`, `fire_rate_authentic.h:51/63`) missed in the default-flip; the remaster AAA doc (`04-content-geometry-effects.md`) references the retired six glass flags (remaster-owned — **coordinate**).
- **FID-0050 missing** (Lane C/D/E): an unexplained ID gap — document it as intentionally skipped or reallocate.
- **Minor code**: `weapon_cycle_queue` add-before-clamp overflow; `lvl.c` cap duplicates `frame_clamp` rather than sharing it; `s_msaa_failed_req` doesn't retry after MSAA off→on at the same size; stale CW/CCW winding comment near the FID-0019 shadow code; `gfxMsaaPipelineCountsConsistent` has no production caller (test-only guard).
- **sim-hash cross-config (FID-0061)**: mitigated (default Release) not solved — no lane asserts the binary is Release (a Debug build gives a false RED); strict-FP flags are the full fix.

**Confirmed clean (no action):** no tracked build artifacts (`__pycache__` false alarm retired), LEDGER.md current, commit convention 98.8% consistent, all referenced tool paths exist, no duplicate findings, no FP-reassociation in any extracted helper.

---

## Part 2 — Epic Completion Plan (toward 1.0)

**Where we are.** Phase 0 substrate is 100% complete and the loop is trustworthy.
The **content pass has barely begun** — the mileage, not the machine, is what
remains. Exit-criteria status (from Lane F against plan §1):

| # | Criterion | Status | Evidence |
|---|-----------|--------|----------|
| 1 | Sim parity across routes | **NOT STARTED** | 0 routes `gate:true`, Dam-only |
| 2 | Renderer parity | PARTIAL | pixel infra built, not run to closure |
| 3 | RDP parity | **NOT STARTED** | S3 lane unbuilt (missing script) |
| 4 | ASM audit | 14 / 388 | ~1 defect per few reviews so far |
| 5 | State-hash coverage | MOSTLY MET | 19/20 (Streets FID-0046 open) |
| 6 | Flag hygiene | PARTIAL | 24 unreferenced gates; raw-getenv batch |
| 7 | Endurance (24h soak) | **NOT STARTED** | — |
| 8 | Ledger closure | **NOT MET** | 40 findings open |

**Critical path** (Lane F): route coverage → RDP lane → run sense lanes to
closure → ASM drain → RNG-phase parity → visible artifacts → Streets → ledger
drain → endurance.

### Phases

**Phase A — Foundation hardening (P0/P1 cleanup). Do first; small, high-leverage.**
Make the machine trustworthy before scaling mileage:
- **Leak (C1/D1):** `export-ignore` the three fidelity trees + the two path-gap docs; scrub the personal email from tracked scripts; add a trailer-strip to the public-publish path.
- **Ratchet teeth (C4/C5):** `verify_all` SKIP → amber-local / hard-fail-release; enforce legal ledger transition edges; `validate` verifies evidence paths exist.
- **Blocked-on trap (C2):** add FID-0062 to FID-0011/12/13/54 `blocked_on`.
- **Red gate (C3):** ENV_FLAGS regen.
- **Determinism (D7):** add the Release-binary assert lane (kills the false-RED footgun).
- **Quick fidelity gaps:** P1a NPC full-auto symmetry (D2), P1b FID-0019 flag+lane, P1c/d missing lanes + AK47 tape, P1f/g/h comparator/loop fixes.
- **Docs (D3):** update the MSAA "OFF by design" comments to settled-decision wording.

Exit: `verify_all` can't report a false green, the ledger's fields match its prose, main is green under a Release build, and there is no known public leak.

**Phase B — Coverage build-out (the tall pole).** Build **FID-0031 route
coverage** (P0, XL — gates criteria 1/2/3): per **D4**, author a **~6–8 route
representative subset** — Dam + one route per archetype (indoor / outdoor /
vehicle / boss / stealth), each a deterministic input tape with `gate:true` and a
paired ares oracle capture. Build **FID-0043 RDP sense lane** (S3, currently a
missing `sense_rdp_sweep.sh`). Add the AK47 combat tape (P1d). **Remaster-
coordination constraint:** every pixel/RDP sweep must pin `--faithful`
(RenderScale=1, FXAA/MSAA off, stock textures) or criteria 2/3 are unmeasurable —
the shipped baseline is deliberately non-pixel-stock.
**DoD:** ≥6 routes with `gate:true`, each 3-run deterministic (Release) + an ares
capture on disk; S3 RDP lane exists and self-tests; AK47 tape reddens a fire-rate
call-site revert.

**Phase C — Sense-lane closure.** Run S1 (trace), S2 (pixel), S3 (RDP) to
closure across the route set (pinned `--faithful`). Each divergence → ledger →
fix cycle. This is where criteria 1/2/3 actually get satisfied and where most new
findings surface. "Closure" = a sense lane runs a full route sweep and returns
**zero un-triaged clusters** for two consecutive runs (the loop-until-dry
pattern).
**DoD:** every route passes S1/S2/S3 with no un-triaged divergence; each real
divergence has a filed FID + a fix or a written waiver; criteria 1/2/3 flip to MET.

**Phase D — ASM audit drain.** 14 → 388 bodies via risk-ranked parallel batches
(the pilot found ~1 defect per few reviews — expect a steady stream of real
port-defects). Batches of ~8–12 bodies per agent, each verdict citing the ASM;
`finding-filed` verdicts open a FID. Closes criterion 4 / FID-0042.
**DoD:** `asm_audit.py stats` shows 388/388 reviewed (verified-equivalent or
finding-filed, none `unreviewed`); every filed FID reaches a terminal state.

**Phase E — Combat parity.** **FID-0063 RNG call-count phase parity** — the XL
linchpin and deepest unknown (native vs retail differ in how many
`randomGetNext()` calls per tick, so probabilistic perception fires a few ticks
off). **Per D5, this is committed to full closure — no waiver exit.** Method:
instrument a per-tick `randomGetNext()` call-count differential between native
and the ares oracle on a combat route, bisect to the first tick where the counts
diverge, and trace the extra/missing consumer (an ASM-authored call the reimpl
added, dropped, or reordered). Because it's open-ended, **start the investigation
early — in parallel with Phases B/C** — so a long tail doesn't serialize onto the
end. Closing it unblocks FID-0011/0012/0013/0054, the whole combat-parity cluster.
**DoD:** the per-tick RNG call-count diff is zero across every combat route;
FID-0063 `verified`; FID-0011/0012/0013/0054 re-captured under `--align tick` and
either `verified` or fixed.

**Phase F — Visible artifacts.** Land the near-done **FID-0009 Silo sky-leak**
FIRST (already on `fix/fid-0009-silo-apertures` — the cheapest visible win;
rebase + Fable-review the T13b sim-purity invariant + merge), then **FID-0008/
0010** sky-leaks, then the hardest single visual — **FID-0001 glass compositing**
(RDP framebuffer-memory blend not stock-accurate) — plus FID-0002 overlapping
panes and FID-0004 shatter/tint parity. Resume **FID-0064** (MP ammo HUD,
early-stage branch). Coordinate every rendering touch with the remaster agent.
**DoD:** each artifact evidenced fixed via the pixel oracle (pinned `--faithful`),
sim-hash unchanged (render-only), each with a regression lane; FIDs `verified`.

**Phase G — Audio & converter.** Audio FID-0025 (env/pole XOR), 0026 (bank
loop/tuning), 0027 (voice starvation), 0028 (dead-synth hygiene); converter
FID-0036 (PROPDEF union cases), 0037 (padnames byte-swap), 0039 (OP11). Audio
needs an oracle/measurement lane it currently lacks — building that is part of G.
**DoD:** each audio/converter FID reaches a terminal state with cited evidence.

**Phase H — Endurance & closure.** Resolve **Streets FID-0046** non-determinism
(purity fuzz 19/20), run the **24h soak** (criterion 7), and **per D6 drive the
ledger to ZERO open** (criterion 8) — every finding reaches verified / documented
/ refuted / waived, each open item either fixed or carrying a written terminal
rationale (no P2/P3 backlog past 1.0). Confirm the verify ratchet green
end-to-end at its tightest. Then 1.0.

### Sequencing notes
- Phases A→B are gating; **C, D, E, G can run in parallel** once B lands the
  routes (C/E need the routes; D and G are independent).
- **E (RNG parity) is the biggest risk and is committed to full closure (D5) with
  no waiver escape — start it on day one, in parallel**, so its open-ended tail
  doesn't serialize onto the end.
- **F's Silo fix is the cheapest visible win — pull it forward** to run alongside
  Phase A (its branch is near-merge).
- **Convergence:** Phases C, D, and E each *generate* new findings that expand the
  ledger. Phase H (D6 zero-open) can only complete after that inflow settles — so
  H is a *closing* phase, not a parallel one, and the ledger count will rise before
  it falls. Track the open-count trend; H starts when C/D/E stop producing new FIDs.
- **P2 hygiene is a rolling track**, not a phase: the raw-getenv→registered-accessor
  batch (needed for **criterion 6** — do the fix-flags first, then teach the
  generator to scan `settingsRegister` mirrors), the 35 dead-glass-script reap
  (coordinate the 2 the remaster doc name-checks), stale-doc fixes, FID-0050
  documentation, and the minor-code items. Fold opportunistically; **all P2 must be
  clear before H** (criterion 6 gates 1.0). P1e (`step==1` under variable clock) is
  **deferred to the separate F5/uncapped-FPS epic**, not required for 1.0 — but
  widen its warning comment now.
- Coordinate all rendering-adjacent work (F glass, pixel sweeps) with the
  concurrent AAA-remaster effort; pin `--faithful` for all parity measurement.

### Immediate next actions (Phase A — when the tree is clear of the remaster agent)
1. C3 ENV_FLAGS regen (unblocks the red gate).
2. C2 blocked-on trap — add FID-0062 to FID-0011/12/13/54 (protects the loop).
3. P1a/D2 file + fix NPC full-auto symmetry (the balance bug the fire-rate fix introduced).
4. C1/D1 leak fix — `export-ignore` the three fidelity trees + 2 path-gap docs, scrub the email, add trailer-strip (before any public push).
5. D7 Release-assert lane; C4/C5 ratchet-teeth hardening.
6. Kick off Phase E (FID-0063 RNG parity) investigation in parallel — per D5 it's committed to full closure and is the long tail; start day one.
7. Begin Phase B route coverage (~6-8 archetype routes, D4).
8. Pull forward the near-done **FID-0009 Silo sky-leak** merge (rebase `fix/fid-0009-silo-apertures`, Fable-review the T13b sim-purity invariant, merge) — cheapest visible win, and resume the **FID-0064** MP-ammo-HUD branch.

**Coordination:** all rendering-adjacent work and every parity sweep pins
`--faithful`; flag the remaster agent that its `04-content-geometry-effects.md`
references the retired glass flags, and coordinate before reaping the 2 dead
glass scripts it name-checks.
