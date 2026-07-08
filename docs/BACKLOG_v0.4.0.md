# MGB64 v0.4.0 Backlog — "I'd rather play this than an emulator"

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Take the faithful mode from "cool project" to the preferred way to play — immaculate
image, complete gameplay, modern-feeling input and UI — while preserving the faithful sim.

**Architecture:** This is a *port*, not a 1:1 decomp. Sim behavior chases N64 parity
(oracle-validated where possible); presentation systems (text, HUD, settings, input) get
outright upgraded when that serves the player. Every behavior change ships with an A/B
escape hatch (`GE007_NO_*` for default-on fixes, opt-in flags for experiments) and a
validation route.

**Tech stack:** C (game/sim, Fast3D interpreter), Objective-C++ (Metal backend), C
(OpenGL backend), C++/ImGui (app shell), SDL2, CMake; tools in Python/shell under `tools/`.

## Global constraints

- Faithful mode with all fixes at defaults must stay deterministic: `sim_state_hash`
  invariance and the renderer parity lane must pass after every task.
- No ROM media enters the repo; release guards (`verify_asset_free.sh`,
  `check_sdk_inventory.py`, filename/history audits) must stay green.
- GL stays the default/byte-identical-everywhere renderer this cycle; Metal is the quality
  target on macOS and must reach GL parity, not diverge from it.
- Every default-behavior change lands with: (a) an env A/B hatch, (b) a
  `docs/ENV_FLAGS.md` regen, (c) a route or screenshot validation named in the task.
- Windows is the least-tested platform: any change touching `src/platform/` needs at
  least a MinGW cross-build check (`release.yml` dispatch or local `x86_64-w64-mingw32`).

## Execution standards — the anti-slop charter

Binding on every agent and human executing an item in this backlog. These are not
aspirations; a task that violates one of these is rejected in review regardless of
whether "it works." Each rule exists because we already paid for its absence once.

1. **Authority hierarchy.** For sim behavior: retail ASM / jump tables > ares oracle
   captures > the decomp C. The `#else` retail-reference bodies **lie** — never
   transcribe one into a native path (documented failure: the red-shard bug came from a
   skipped `#else`-only `totalsize` advance). Re-derive from the ASM/oracle and cite the
   anchor (e.g. `jpt_80054084`, oracle route name) in the commit message.
2. **Root cause before fix.** No symptom patches. A fix must name the mechanism in one
   sentence; if you can't, you're not done diagnosing. Heuristics are allowed only as
   explicitly-labeled bridges with a retirement condition written into the code comment
   (the pattern: `GE007_PORTAL_EDGE_RESCUE` exists *until* the portal BFS handles
   degenerate apertures — M3.4 then deletes it).
3. **Evidence before claims.** "Fixed" requires, pasted into the PR/commit: (a) the
   task's named validation route passing, and (b) the **negative control** — the A/B
   hatch or fault injection proving the validator catches the old behavior
   (`GE007_NO_BOND_BODY_FIX=1` must *fail* the intro validator). A validator that never
   failed anything validates nothing.
4. **Determinism is a gate, not a hope.** `sim_state_hash` invariance, the renderer
   parity lane, and (for opt-in features) identity-off byte-identity must pass on every
   task. Render-touching changes also run the perf census; >2% frame-time regression on
   the heavy routes (Jungle, Cradle) needs written justification.
5. **One item, one branch, one concern.** Commit messages cite the backlog ID (and
   audit ID where one exists). No drive-by refactors, no stacked unrelated fixes on one
   branch (the WORKFLOW.md anti-pattern — main once accumulated 8 unrelated fixes).
6. **Code reads like the codebase.** Follow `docs/CODING_STYLE.md`. Comments state
   constraints the code can't show — never narration ("now we check X"), never
   self-praise ("robust", "improved", "enhanced"), never review-chatter ("this fixes the
   bug by..."). Replaced code is deleted, not `#if 0`'d — the only sanctioned dead code
   is the `PORT_FIXME_STUBS && !NATIVE_PORT` retail-reference pattern.
7. **Flag discipline.** Every env flag is exactly one of: (a) a default-ON fix with a
   `GE007_NO_*` opt-out, (b) an opt-in experiment with a named owner and a
   promote-or-reap decision recorded in this backlog, or (c) a diagnostic. New flags
   register through the schema/env registry and regenerate `docs/ENV_FLAGS.md` in the
   same commit. Orphan flags are deleted on sight.
8. **Tool discipline.** A new `tools/` script is either wired into a suite
   (ctest/regression lane, listed in a doc) in the same PR, or it is a scout and gets
   **deleted when the investigation closes** — its conclusion goes into the relevant doc,
   not the filesystem. (The ~50-script `glass_*` sprawl is the cautionary example; MG.5
   pays this debt.)
9. **No placeholder work.** No TODO-later, no stubbed error paths, no "handle edge
   cases" comments, no silently narrowed scope. If an item can't be completed as
   written, stop and report what you found — a smaller true fix beats a larger fake one.
10. **Misdiagnosis is reportable, not embarrassing.** If implementation reveals the
    backlog/audit item mischaracterized the bug, update the document and say so
    (precedent: audit finding #1 misattributed the red shards; the correction was more
    valuable than the original finding). Never force the prescribed fix onto a
    different-shaped bug.
11. **Fresh-eyes review gate.** Every task gets a review (subagent or human) against
    this charter before merge. The reviewer independently re-runs the negative control —
    not just the happy path — and checks rules 6–8 mechanically.
12. **Docs are curated, not accreted.** Update the existing authoritative doc
    (STATUS.md, ENV_FLAGS.md, the audit, this backlog) instead of writing a new one.
    New standalone docs need a reason existing homes can't serve, and process artifacts
    (scratch analyses, one-off findings) never land in `docs/`.

## How this backlog was compiled (2026-07-08)

Synthesis of: `docs/RENDERER_SIM_AUDIT_2026-07-07.md` (R1–R19, with per-item status
re-verified against the working tree), the un-carried CONFIRMED findings from
`docs/RENDERER_SIM_AUDIT_2026-07-06.md` (each re-verified OPEN/FIXED in the current tree),
the intro/outro D-ledger, `docs/STATUS.md` / `docs/ROM_COMPARISON.md` validation-coverage
gaps, and six fresh code sweeps (gameplay/sim, renderer, HUD/text/menus, audio, platform/
app/CI, docs). Items verified fixed are in Appendix A so we stop re-auditing them.

**Priority:** P0 = broken/corrupt primary path · P1 = frequent player-visible defect ·
P2 = narrower visual/behavioral issue · P3 = hygiene/robustness/future-proofing.
**Effort:** S = hours · M = 1–3 days · L = 1–2 weeks.

---

## Execution status

**2026-07-08 — Week-1 batch LANDED** (`510e181..2a30542` + follow-ups, all on main,
whole-branch review verdict READY-WITH-MINOR-FOLLOWUPS, 0 Critical):
M0.1 ✅ M0.2 ✅ M0.3 ✅ (`9acba24`/`510e181`) M0.4 ✅ M8.1 ✅ (premise corrected:
gap was the prop pool, chr/player were pool-covered) M1.1 ✅ M2.1 ✅ M2.2 ✅ M2.4 ✅
M6.3 ✅. Open from review: MinGW cross-build validation (no local toolchain — needs a
staging push + CI dispatch), and the M8.3 manual-QA list grew (see M8.3). Details in
`.superpowers/sdd/progress.md`.

## Milestone map

| # | Milestone | Theme | Ships as |
|---|-----------|-------|----------|
| M0 | Land the in-flight work | Commit + gate what's already in the working tree | immediately |
| M1 | Bond intro & the corruption class | Flagship visual defect + allocator fail-closed | 0.4.0-alpha.1 |
| M2 | Gameplay feel & parity | Input feel, combat visibility, sim parity | 0.4.0-alpha.2 |
| M3 | Immaculate image | Renderer correctness on both backends | 0.4.0-beta.1 |
| MG | The glass sprint | Panes, cracks, shatter, shards — accurate and validated | 0.4.0-beta.1 |
| M4 | HUD, text & UI overhaul | Crisp text, real settings, crosshair, languages | 0.4.0-beta.2 |
| M5 | Audio polish | Resolve the last faithfulness questions + modern output | 0.4.0-beta.2 |
| M6 | Platform & Windows hardening | Crash logs, rebinding, CI, config safety | 0.4.0-rc.1 |
| M7 | Split-screen multiplayer | 3/4-player + scoreboard validation | 0.4.0-rc.1 |
| M8 | Validation rails & release | Pixel validators, sim-hash coverage, QA sweep | 0.4.0 |

Deferred past 0.4.0 (tracked, deliberately not in scope): 120 Hz render interpolation
(F5 project — sim stays 60 Hz; needs a full transform-snapshot layer), MSVC support
(MinGW is the Windows toolchain), gyro aim, full netplay, menu mouse navigation.

---

## M0 — Land the in-flight work (days, do first)

The working tree already contains four finished audit fixes and one experiment that
needs a gating decision. Nothing else in this backlog should start until M0 is committed,
because M1–M3 build on these files.

### M0.1 — Commit R4/R5/R10: texLoad bounds, SETTEX checked alloc, N64 DL recursion cap
**P1 · S · done in tree, needs commit**
**Files:** `src/game/image.c:3333` (texture-ID bound → blank texture),
`src/platform/fast3d/gfx_pc.c:21573-21609` (dimension reject + `size_t` checked multiply),
`src/platform/fast3d/gfx_pc.c:22750` (`entry_depth >= 32` cap, restores
`g_effect_pending_child_label`, logs `[N64_DL_DEPTH_CAP]`).
- [ ] Run `tools/playability_smoke.sh --all` and the renderer parity lane; confirm zero
      `[N64_DL_DEPTH_CAP]` / texture-health errors on normal routes.
- [ ] Commit as three commits (one per fix) with the audit IDs in the message
      (`fix(fast3d): R10 …` etc.), regenerate `docs/ENV_FLAGS.md` if flags changed.

### M0.2 — Commit R7: minimap overlay on Metal
**P1 · S · done in tree, needs validation + commit**
**Files:** `src/platform/fast3d/gfx_metal.mm` (`gfx_metal_draw_minimap_overlay`: color-only
PSO + scissor, drawn in `mtl_end_frame` onto `present_src` before present),
`src/platform/minimap_overlay.c/.h` (backend dispatch,
`minimap_overlay_draw_queued_frames_metal`), `src/platform/fast3d/gfx_pc.c:24182`
(Metal skip removed).
- [ ] Run `tools/minimap_smoke.sh` twice: `GE007_RENDERER=metal` and default GL. Compare
      overlay screenshots; assert draw summaries match and no GL symbols execute under Metal.
- [ ] Verify `Input.MinimapEnabled=0` parity checks still pass under Metal.
- [ ] Commit; update `docs/RENDERER_SIM_AUDIT_2026-07-07.md` R7 status line.

### M0.3 — Gate and land D43: intro phase-3 animation + root-motion experiment
**✅ LANDED 2026-07-08** as `9acba24` (phase-3 anim + root motion, oracle-hash-matched) and
`510e181` (D31 grounding — Bond settles through the whole swirl, Y delta 0.00), both
oracle-validated on Dam with a 20-level regression pass. Remaining from this item: re-test
the D35/D36 waivers (folded into M1.5) and keep `GE007_NO_INTRO_PHASE3`/`_ROOTMOTION` as
the A/B hatches. Original task text kept below for reference.

**P0 (it changes every 1P intro) · M**
**Files:** `src/game/bondview.c` working-tree diff — `bondviewMaybeDriveIntroPhase3`
(fires `ANIM_aim_one_handed_weapon_left_right` at swirl seg 4 / timer 40, oracle-measured
frame delta 0.00), `bondviewDumpAnimHashesOnce` diagnostic, and a `GE007_INTRO_ROOTMOTION`
change in `playerTickBeams` (~`bondview.c:24437`).
**Problem:** the code comments say phase-3 is default-ON with `GE007_NO_INTRO_PHASE3` as
the A/B hatch, while the root-motion block is labeled "experiment (opt-in
GE007_INTRO_ROOTMOTION)" — but both currently read as default-enabled. These overlap the
unresolved intro transform-authority question (M1.3/R2); shipping two default-on movers of
the intro body invites regressions the D-ledger just waived.
- [ ] Decide the defaults: phase-3 anim default-ON (it is oracle-validated: onset, anim
      index 99, hash `0x79F92FB0`, end 96, hold-last-frame) with `GE007_NO_INTRO_PHASE3`
      A/B; root-motion **opt-in only** (`GE007_INTRO_ROOTMOTION=1`) until M1.3 makes one
      system authoritative for the intro body transform.
- [ ] Re-run the Dam intro oracle route (`dam_intro_swirl_bond_anim`) + `--trace-state`
      Bond trace; confirm anim-phase counters and camera-anchor drift (D35/D36) improve.
- [ ] Strip or env-gate the `[ANIM-HASH]` dump (`GE007_DUMP_ANIM_HASHES` is fine to keep).
- [ ] Commit; update the D-ledger: D43 landed, then re-test whether the D35/D36 waivers
      can be removed (that is D43's exit gate).

### M0.4 — Restore the design docs the backlog depends on
**P2 · S**
`docs/design/AUDIO_QUALITY_PLAN.md`, `docs/design/PERFORMANCE_PLAN.md`,
`docs/design/COMBAT_DEFERRED_PLAN.md`, and the intro/outro defect ledger (D-numbers) exist
only in git history or `.superpowers/sdd/progress.md` — `docs/CINEMATICS.md:99` points at
a "plan doc's defect ledger" that is not in the tree.
- [ ] Recover `git show 8de6d8d:docs/design/AUDIO_QUALITY_PLAN.md` (and the perf plan)
      into `docs/design/`; copy the D-ledger table into `docs/CINEMATICS.md` or a
      `docs/design/INTRO_OUTRO_LEDGER.md` and fix the dangling reference.
- [ ] Correct `docs/FRAME_TIMING_ARCHITECTURE.md:109`: the perf work IS present in the
      public tree by content (e.g. `gfx_opengl_compute_batch_snapshot_rect`,
      `g_room_cmd_cache_generation`), but merge hash `1b29b7a` is private-lineage; cite
      content, not the hash.

---

## M1 — Bond intro & the corruption class

The red shards are fixed (`50f3f61`), the alias fallback is fail-closed (R1 done). What
remains is the same memory neighborhood plus the transform-authority question — this
milestone makes the intro *provably* immaculate and makes buffer exhaustion incapable of
producing shards anywhere in the game.

### M1.1 — R9: guard the intro weapon sub-buffer load (underflow still possible)
**P1 · S (F1)**
**Files:** `src/game/bondview.c:3362` (weapon `load_object_fill_header` into
`bodyBuffer + totalsize`, size `bodyBufSize - totalsize`).
**Bug:** `totalsize` is advanced three times after the head guard (`:3269`, `:3276`,
`:3346`) with no re-check. If `totalsize >= bodyBufSize`, the size argument underflows and
the weapon load writes past the body buffer — the exact class that produced the red shards.
The `GE007_TRACE_BOND_BUF` block at `:3353` already computes the overlap; it is a print,
not a guard.
- [ ] Immediately before `:3362` add:
      `if (totalsize < 0 || totalsize >= bodyBufSize) { log one "[BONDVIEW][RENDER-HEALTH] intro weapon skipped: buffer exhausted"; skip weapon attach; }`
      (Bond renders unarmed rather than corrupt; every caller already tolerates a missing
      hand item via the R1 fail-closed paths.)
- [ ] Promote the `GE007_TRACE_BOND_BUF` overlap computation into the guard condition.
- [ ] Validate: Dam intro still shows the silenced PP7 (`ITEM_WPPKSIL` route assertion);
      a forced-small body buffer (`GE007_BOND_BODY_ALLOC_FAIL` variant or a debug clamp)
      skips the weapon without corrupting the body.

### M1.2 — R3 (remaining half): dyn-allocator caller fail-closed contract
**P0 · L (F4) — the last systemic shard generator**
**Files:** `src/game/dyn.c:129-205` and high-risk callers.
**Bug:** `9b9c4e2` stopped the OOB *writes*, but on overflow the allocators still return
the **current pointer** (`dyn.c:144`, `:188`, `:205`) or a **single shared scratch matrix**
(`dyn.c:166`). Two overflowing users in one frame overwrite each other's vertices/
matrices → warped limbs, bad billboards, flickering overlays. This is why "rare shards"
can still appear anywhere under memory pressure.
- [ ] Add a fail-closed mode: on overflow return `NULL` (and for the matrix allocator a
      distinguishable sentinel, e.g. the scratch matrix's address exposed as
      `dynIsOverflowMatrix(m)`), plus checked arithmetic on `count * 0x10` and aligned adds.
- [ ] Update callers in priority order, each skipping its draw on failure: model
      render-position allocation (`chr.c:5350` area), effect/billboard quads
      (`dynAllocate7F0BD6C4` users), glass shards, HUD rectangles, matrix producers.
      Grep callers: `grep -rn "dynAllocate" src/game/ | grep -v dyn.c`.
- [ ] Add `g_dyn_overflow_count` per-frame render-health counter; fail visual test routes
      when nonzero.
- [ ] Add `GE007_DYN_STRESS_LIMIT=<bytes>` to force small arenas; under stress the game
      must drop effects cleanly, never draw screen-spanning triangles.
- [ ] Validate: `tools/playability_smoke.sh --all` reports zero overflows at defaults;
      stress route degrades cleanly; ASan lane clean.

### M1.3 — R2: one authority for the intro body transform
**P0 · L (F4)**
**Files:** `src/game/bondview.c:24437+` (post-`chrTickBeams` snap),
`src/game/bondview.c:15629/15647` (frozen-camera render origin), `src/game/chr.c:5115-5369`.
**Bug:** in frozen intro modes the current-player prop is corrected (snapped to
`field_488.collision_position`) *after* `chrTickBeams` has already advanced animation,
allocated render positions, and built matrices — so the visible body can be built from
stale or animation-shifted state while diagnostics see corrected state (floating/
ungrounded Bond).
- [ ] Add `bondviewAlignCurrentPlayerIntroPropBeforeChrTick(struct player *)`: copy
      `field_488.collision_position` → `prop->pos`, `field_488.current_tile_ptr` →
      `prop->stan`, call `setsuboffset` on `ptr_char_objectinstance`. Call it before
      `chrTickBeams` only when `playerHasFrozenIntroCamera(player)`.
- [ ] Demote the existing post-call snap to a consistency check: log if it moves any
      coordinate by more than epsilon (it should now be a no-op).
- [ ] Fold the M0.3 root-motion experiment into this single authority (this is where
      `GE007_INTRO_ROOTMOTION` either becomes the default or dies).
- [ ] Trace record before/after `chrTickBeams`: intro pad, prop pos, collision pos, model
      suboffset, anim frame, root joint pos, room, camera render origin.
- [ ] Validate: Dam intro root/pelvis height stable relative to the selected pad;
      M1.4's grounding check passes; first-person hand/body path unchanged in normal play.

### M1.4 — R8: pixel-level intro validator (prove it, keep it proved)
**P1 · M (F2)**
**Files:** `tools/audit_intro_trace.py`, new screenshot analyzer under `tools/`.
**Gap:** current checks are actor-state (`bond_rendered=1`, anim hash) — a shredded or
floating Bond still passes.
- [ ] Extend the intro trace with projected root/pelvis/feet coordinates and render-pos
      count per frame.
- [ ] Add a Dam-intro screenshot analyzer with three checks: (1) body present (silhouette
      coverage in the projected joint bbox), (2) body grounded (root/feet Y vs pad floor,
      fail on persistent offset), (3) shard score = red/warm outlier pixels far outside
      the body bbox → must be 0.
- [ ] Negative controls: `GE007_NO_BOND_BODY_FIX=1` must fail the silhouette check;
      a deliberately offset prop must fail grounding. Wire into the regression suite.

### M1.5 — Close out the remaining intro D-ledger items
**P2 · M each — schedule after M1.3/M1.4 give measurement**
- [ ] **D37/D39:** static establishing-shot `cam_floor`/`cam_delta` divergence (Statue
      ~34u, Cradle ~9083u) — instrument the static-shot camera seed vs stock captures.
- [ ] **D38:** stock static-shot duration varies +3..+14 ticks while native is fixed —
      find stock's duration source (likely per-setup or load-driven), replicate.
- [ ] **D41:** ~3-tick anim-phase shift between menu-boot and direct-boot — trace where
      the tick offset enters (menu exit timing) and align.
- [ ] **D42 residual:** distant reservoir mountains not portal-reachable from Dam room 53
      absent in far establishing shots — needs stock pixel comparison; consider a
      camera-seed room supplement for establishing shots only.
- [ ] Re-test D31/D32/D35/D36 waivers after M0.3+M1.3; delete every waiver the fixes
      obsolete (that is the ledger's exit criterion).

---

## M2 — Gameplay feel & parity

The highest player-facing wins per hour in the whole backlog. Everything here is felt in
the hands within seconds of playing.

### M2.1 — Radial deadzone for the movement stick (slow-walk is currently dead)
**P1 · S**
**Files:** `src/platform/stubs.c:6322-6337` (P1), `stubs.c:5887-5900` (P2–4),
`GAMEPAD_DEADZONE` at `stubs.c:634`; reference implementation: the aim stick's radial
deadzone + rescale-from-edge at `src/game/lvl.c:5882-5895`.
**Bug:** the movement stick uses a per-axis square deadzone with a linear map — input
below the threshold is zeroed per-axis, killing slow-walk and making diagonal creep feel
notchy. The aim stick already got the correct radial treatment.
- [ ] Factor the aim stick's radial magnitude deadzone + rescale into a shared helper;
      apply it to the left stick in both pad paths before the ±80 N64-range map.
- [ ] Respect `Input.GamepadDeadzone` / `Input.GamepadRadialDeadzone` for both sticks.
- [ ] Validate: analog walk from stick center is smooth 0→80 (log the mapped magnitude);
      `tools/mp_smoke.sh` still green (multi-pad path touched).

### M2.2 — Mouse-wheel weapon cycling drops notches
**P2 · S**
**Files:** `src/platform/stubs.c:6267-6269`.
**Bug:** `if (wheel > 0) g_pcWeaponCycleForward = 1;` — N accumulated notches in one frame
collapse into one switch; fast scrolls feel unresponsive.
- [ ] Make the cycle request a signed count (`g_pcWeaponCycleSteps += wheel`); consumer
      steps `|count|` times in the cycle direction, then zeroes it.
- [ ] Validate: scripted 3-notch wheel event switches three weapons.

### M2.3 — Guard visibility/targetability: retire the render-visibility coupling
**P1 · L (F4) — the biggest remaining sim-parity item**
**Files:** `src/game/chr.c:5220-5238` (`GE007_CHRBEAMS_FRUSTUM` default OFF; room-rendered
bypass), `src/game/lvl.c:1737-1743` (zero-rooms fallback marks EVERY loaded room rendered),
`docs/design/COMBAT_DEFERRED_PLAN.md` (M1b per-player frustum union).
**Bugs (three coupled):** (1) `chrTickBeams` visibility defaults to "any of the guard's
rooms is rendered" instead of the retail frustum test `sub_GAME_7F054D6C`; (2) guard
auto-aim targetability and movement-AI mode key off that render-derived visibility, so
render fallbacks change *combat behavior*; (3) when the portal walk yields zero rooms, the
fallback marks all loaded rooms rendered — for that frame every guard is targetable and
"visible" through walls.
- [ ] Implement the per-player frustum **union** (evaluate `sub_GAME_7F054D6C` against all
      active viewports) so the accurate test is MP-safe; flip `GE007_CHRBEAMS_FRUSTUM`
      default ON after A/B.
- [ ] Narrow the `lvl.c:1737` fallback: mark only the player's current room + direct
      portal neighbors, and increment a render-health counter when it fires.
- [ ] Decouple auto-aim/AI-mode from `getROOMID_isRendered`: they should consume the sim
      frustum result, never the render set. Keep the viewer-body intro bypass explicit.
- [ ] Then restore the retail visibility-gated actiontype dispatch (off-screen guards
      shouldn't full-tick every frame) and delete the bypass.
- [ ] Validate: `GE007_TRACE_VISIBILITY` disagreement records = 0 on
      `tools/playability_smoke.sh --all`; combat gates (guard fire, hidden-guard
      no-phantom-fire) pass; split-screen `mp_smoke.sh` green; auto-aim no longer acquires
      guards behind walls on the Dam guard-pressure route.

### M2.4 — HUD/watch timers jump on frame spikes (`speedgraphframes` unclamped)
**P2 · S**
**Files:** `src/game/unk_0C0A70.c:151` (raw assignment from `deltaFrames`), consumers
`src/game/bondview.c:9484,9492,13542,15758`, `src/game/watch.c:2472,9158,9202`; reference:
the `g_ClockTimer` cap at `src/game/lvl.c:2113-2126`.
**Bug:** alt-tab or a load spike feeds a huge `speedgraphframes` into watch/HUD timers →
visible timer jumps.
- [ ] Clamp `speedgraphframes` at assignment with the same 4-tick cap the clock uses (or
      route consumers through the capped tick).
- [ ] Validate: induce a 2 s stall (SIGSTOP/CONT); watch timer advances ≤ cap.

### M2.5 — Patrol force-loop workaround → fix the root cause
**P2 · M**
**Files:** `src/game/chrlv.c:11216-11218` (every patrol tick force-sets anim looping on
any guard), root cause chain: fog zeroing alpha → `PROPFLAG_ONSCREEN` never set →
`WAYMODE_MAGIC` path stalls.
- [ ] Fix the ONSCREEN determination for fogged-but-visible guards (ties into M2.3's
      frustum result — a guard in-frustum should be ONSCREEN regardless of fog alpha).
- [ ] Remove the per-tick `modelSetAnimLooping` enforcement; keep it one release behind a
      `GE007_PATROL_FORCELOOP` compat flag in case a level regresses.
- [ ] Validate: guard patrol routes on Dam/Surface (walk cycles don't freeze); death/hit
      reaction anims (non-looping) still play exactly once.

### M2.6 — R6: ammo HUD fallback icon + image validation
**P1 · M (F2)**
**Files:** `src/game/gun.c:31752-31830` (`portGetAmmoImage`, `portDrawHandAmmo`),
`src/game/gun.c:31142-31165` (dimension trust), `src/game/image_bank.c:270-290`,
`src/game/bondwalk2.c:34-136` (`draw_textured_rectangle` clamping).
- [ ] Add `portValidateImageEntry(const struct sImageTableEntry *img, const char *label)`:
      non-null, positive w/h, sane max, texture index < NUM_TEXTURES.
- [ ] On invalid/missing icon draw a visible fallback (simple bordered rect glyph) instead
      of silently drawing digits only; count HUD-health errors.
- [ ] Clamp texture-rect right/bottom against the screen in `draw_textured_rectangle` (or
      document why backend clipping suffices).
- [ ] Validate: a scripted inventory cycle across every ammo type, screenshots under GL
      and Metal, assert icon pixels + digit pixels for each.

### M2.7 — Parity audits that need an oracle pass (schedule, don't rush)
**P2 · M each**
- [ ] **Shoot-out-lights coverage:** `src/game/bg.c:316-357` reconstructs the
      `light_fixture_table` by texture-ID heuristic (`check_if_imageID_is_light`) instead
      of retail's `texLoadFromGdl` population. Run the `GE007_LF_VERIFY` sweep (`bg.c:311`)
      per level against the ROM's fixture set; fix misclassified textures; key spans on
      tile-load boundaries where runs are mis-split.
- [ ] **stan.c seam parity:** `GE007_STAN_ONEDGE` (`src/game/stan.c:731`) family is
      validated but not frame-exact. Run the stan oracle comparator vs ares on
      multi-floor levels (Facility/Bunker stairwells); diff tile selection per frame.
- [ ] **SFX pan fidelity:** `src/platform/audio_pc.c:740` recomputes pan from
      `g_pcCamYaw` instead of the N64 game-computed pan (`snd.c:1226/1822`). A/B a few
      moving-source scenes vs ares; keep whichever matches.

### M2.8 — Surface the finished ADS feature
**P3 · S**
`Input.AdsEnabled` + 14 sub-flags are implemented (per-weapon `AdsProfile` pose authoring,
runtime gates — `src/game/gun.c:189-234`, `src/game/ads_profiles.h`) but ship buried in
the generic settings list.
- [ ] Add an explicit "Aim-down-sights (modern)" toggle row in `src/app/ui_modes.cpp`
      (default OFF, faithful-first); decide whether 0.4.0 documents it as a headline
      modern-mode feature.

---

## M3 — Immaculate image

Renderer correctness on both backends. Ordered: user-visible Metal gaps first, then
correctness sharp-edges, then the instrumentation-first audits.

### M3.1 — Metal: implement MSAA (the setting is silently ignored)
**P1 · M-L**
**Files:** `src/platform/fast3d/gfx_metal.mm` — `rasterSampleCount` hardcoded 1 at
`:1209, :2131, :2430, :2444, :2760`; draw-site PSO request `samples=1` at `:3206`; no
`alphaToCoverageEnabled` anywhere.
**Bug:** `Video.MSAA` works on GL but is a silent no-op on Metal, and A2C cutout
smoothing (foliage/fences) is unavailable — a direct image-quality gap on the flagship
backend.
- [ ] Allocate MSAA scene color/depth targets (`textureType:MTLTextureType2DMultisample`,
      sampleCount from `Video.MSAA`) with a resolve to the existing single-sample
      `present_src` chain (`storeAction:MTLStoreActionMultisampleResolve`).
- [ ] Thread sample count into the PSO cache key + descriptor; set
      `alphaToCoverageEnabled=YES` for the coverage-blend modes GL uses A2C for.
- [ ] Mind the snapshot path: `s_snapshot_tex` blits must read the resolved texture.
- [ ] The R7 minimap overlay PSO hardcodes BGRA8 + sample-count 1 — thread the same
      sample count through it (final-review note, 2026-07-08).
- [ ] Validate: GL-vs-Metal screenshot compare at MSAA 4x on Dam fences/foliage; perf
      check on the jungle route (MSAA is the most expensive knob — keep default as-is).

### M3.2 — Metal shadow & SSAO correctness trio
**P2 · S each**
- [ ] **Shadow depth clamp:** `mtl_shadow_encode` (`gfx_metal.mm:1631-1649`) never calls
      `setDepthClipMode:MTLDepthClipModeClamp` (the scene encoder does at `:1354`) —
      boundary casters get clipped where GL clamps. Add the one call.
- [ ] **Read-while-write hazard:** `gfx_metal.mm:3308-3311` binds `s_scene_depth` as the
      shadow-map fallback sampler while it is the live depth attachment. Bind a dummy
      cleared depth texture (or skip the shadow term) when `s_shadow_depth == nil`.
- [ ] **Autoreleasepool:** wrap every MSL compile body (`:696/718`, plus setup compiles at
      `:1179, :1577, :2116, :2417, :2738`) in `@autoreleasepool { … }` — compiler
      transients currently live until thread teardown.

### M3.3 — Texture-cache and interpreter sharp edges
**P2-P3 · S each**
- [ ] **Footprint-reject eviction** (`gfx_pc.c:15276-15296`): on implausible-footprint
      reject the code discards the *currently bound* cache node for the slot, evicting an
      unrelated healthy texture. Change to unbind/no-draw for the current tile without
      `gfx_texture_cache_discard_node`.
- [ ] **Base-GBI light moveMem bound** (`gfx_pc.c:20065`): `G_MV_L0..L7` indexes
      `rsp.current_lights[(index-G_MV_L0)/2]` with array size `MAX_LIGHTS+1 = 3` — L7 is
      an OOB write. Mirror the F3DEX2 bound (`:20046-20048`). GoldenEye emits F3DEX2 so
      risk is low, but it's a 2-line guard.
- [ ] **Matrix-stack push at depth 11** (`gfx_pc.c:16005`): silently skipped push, then
      `G_MTX_LOAD` clobbers the top. Add a render-health counter + one-shot log so a
      malformed DL is diagnosable.
- [ ] **Eye-intro prim-color bake** (`gfx_pc.c:15161-15162` vs cache key at `:21526`,
      struct `:944-955`): prim-modulated texels are cached without prim_color in the key —
      first prim wins on reuse. Add `prim_color.r` to the settex cache key for this path.
- [ ] **Texgen s16 overflow** (`gfx_pc.c:16901/16996-17004`): texgen U/V reassignment
      truncates to `short` for texture scale > 0x7FFF. Widen the intermediate and clamp to
      the tile's addressable range; log once if clamped.
- [ ] **G_SETBLENDCOLOR no-op** (`gfx_pc.c:22684, :23721`): store blend color as RDP
      state and add a diagnostic when a combiner references it, so we learn whether any
      live material needs it before implementing.
- [ ] **`texSelect` table bounds** (`src/game/othermodemicrocode.c:94/104/119/596`):
      the `< NUM_TEXTURES` (3001) checks share the 2698-entry compiled-table mismatch
      the R4 follow-up (`340d892`) fixed in texLoad — bound by the actual table.

### M3.4 — Train windows: fix the portal BFS, retire the heuristics
**P1 · M-L**
**Files:** `src/game/bg.c:2094` (`bgFindNearestRenderedRoomByAabb`), `bg.c:2160+`
(`bgApplyVisibilitySupplement`), `bg.c:2200` (neighbor_snapshot gate);
regression lane `tools/train_window_backdrop_regression.sh`.
**Bug:** retail's portal BFS reaches rooms behind grazing-angle windows/slats; the native
BFS drops them when the projected aperture collapses to an empty 2D bbox. Two default-on
heuristics (`GE007_PORTAL_EDGE_RESCUE`, the AABB-gap supplement) approximate it — Train
room-51 windows still show sky, and the over-broad variant can admit far terrain shards.
- [ ] Diff the native aperture projection (`bgProjectRoomAabbToScreenBbox` / portal clip)
      against retail for the degenerate-aperture case; make real portal edges survive with
      a clamped-to-pixel minimum aperture instead of empty-bbox rejection.
- [ ] Once the BFS reaches the rooms, retire `bgApplyVisibilitySupplement` (distance
      heuristic ≠ connectivity) behind a compat flag for one release.
- [ ] Validate: Train regression lane; `GE007_PORTAL_EDGE_RESCUE=0` as negative control;
      Dam room-14 far-terrain guard stays green.

### M3.5 — Promotion decisions for parked default-off fixes
**P2 · S-M each — each is "validate, then flip or close"**
- [ ] **`GE007_DIAG_SETTEX_CC_COLOR_SCALE`** — frontend/briefing ~5-6% brightness residual
      fix, pending visual A/B sign-off. Do the A/B (menu + briefing screenshots vs stock
      capture), promote or document why not.
- [ ] **`GE007_TEXTURED_PROP_BULLET_IMPACTS`** — textured prop decals are opt-in because
      the textured path corrupted world texture state historically. Re-test on the current
      renderer (the texture-state fixes in RENDERING_REGRESSION_NOTES may have obsoleted
      the corruption); promote to default if the Dam decal probe passes.
- [ ] **`Video.EnvSmoothNormals`** — merged, default-off, "under review". Finish the blend
      tuning, decide default-off-but-in-`--remaster` vs stay experimental.
- [ ] **`GE007_EXACT_ROOM_SCISSOR`** — exact per-room N64 scissor boxes disabled by
      default (can under-cover interior seams). Route-test on Facility/Bunker interiors;
      promote or document.
- [ ] **`Video.HiDPI` default** (`platform_sdl.c:1595`) — off by default double-resamples
      on Retina; the macOS app shell already forces it on (`app_host.cpp:53`), so the SDL
      path is the odd one out. Benchmark fog/alpha-heavy stages at native backing scale;
      default ON where the GPU budget allows, keep the setting as the escape hatch.
- [ ] **HD-pack tooling gap:** `GE007_DUMP_LOADED_TEXTURES` emits hash-keyed dumps the
      runtime loader cannot consume (`texture_pack.c` loads `tok####.png` only). Make the
      dump emit token-named files (or teach the loader the hash key) so the dump→upscale→
      pack loop works end-to-end for world textures, matching the M4.2 font path.

### M3.6 — Audit-driven instrumentation items (R11, R15, R16, R17, R18, R19, R12)
**P2-P3 · instrumentation-first; no default behavior change without screenshots**
- [ ] **R11** RDP-memory snapshot census: **executed as MG.1/MG.2** (the glass sprint
      owns the instrumentation and the overlap-splitting fix); water/scope/fade
      materials found by that census that aren't glass get filed back here.
- [ ] **R15** depth audit: log every draw with `Z_UPD` without `Z_CMP` (class, material,
      room); classify and add targeted overrides only for proven cases.
- [ ] **R16** Metal degraded-target mode: classify targets required (scene color/depth)
      vs optional (snapshot/final/low-filter); optional failure disables the effect, not
      the frame (`gfx_metal.mm:1005`).
- [ ] **R17** mid-frame readback counters + warning outside diagnostic modes
      (`gfx_metal.mm:3148+`).
- [ ] **R18** private-storage texture upload path via staging blit for static textures;
      keep shared as fallback; upload/eviction counters first to prove it matters.
- [ ] **R19** `G_ZS_PRIM` false contract: add a diagnostic if it appears in live DLs;
      fix the lying comment at `gfx_opengl.c:1759`; implement only if a real material uses it.
- [ ] **R12** `gfx_ptr.h` registry: probe all four slots (or tombstones), add
      eviction/collision counters; bump associativity only if counters say so.

---

## MG — The glass sprint

Glass is the hardest remaining visual system and gets its own sprint. State of play:
transparent panes no longer vanish, crack decals are surface-aligned and lifted
(`explosions.c:183-254`), shards render by default (`GE007_GLASS_SHARDS=0` A/B) with the
shatter-crash and byte-swap hazards fixed (`alloc_window_pieces.c:29` registers the shard
buffer as native vertex data). What remains is *accuracy*: the pane compositing math, the
shard geometry scale, and trigger parity are all either measurably wrong or signed off by
nothing. Note: the "red shards" around intro Bond were a different bug (fixed `50f3f61`) —
this sprint is window glass.

### MG.1 — Center-glass framebuffer-memory blend: close the pad10092 pixel gap
**P1 · L — the core open glass bug**
**Files:** `src/platform/fast3d/gfx_pc.c` room-XLU coverage/RDP-memory classification
(`gfx_room_xlu_cvg_memory_gate_reason` and the `G_SETTEX` room-glass class,
`ROOM_GLASS_CC=0x00738e4f020a2d12`); context `docs/RENDERING_REGRESSION_NOTES.md` #13;
oracle route `tools/rom_oracle_routes/dam_regular_glass_shatter_pad10092_impact_visual_probe.json`.
**Bug:** pad 10092 is the Dam guard-tower window. The footprint-LOD fix made the pane's
*source fragment* correct (no longer black), but per the regression notes' own words it
was "a source-sampling fix only" — the RDP framebuffer-memory/coverage blend that
composites the tinted pane over the geometry behind it is still not stock-pixel-accurate.
This is the residual the entire 15-script `glass_pad10092_*` probe family was chasing.
- [ ] Instrument every draw using `diagRdpMemory`/`diagRdpCvgMemory` (this is audit R11's
      glass instance — M3.6's R11 bullet is satisfied by this task): material, draw
      class, screen rect, and same-batch overlap.
- [ ] Complete the coverage-memory blend for the room-XLU `G_SETTEX` glass class against
      the oracle capture: match blend inputs (memory color/coverage) per the diag CC
      signatures (`GE007_DIAG_XLU_RDP_CVG_MEMORY_BLEND_CC` / `_XLU_RDP_MEMORY_BLEND_CC`
      are the exact-match hypotheses to confirm or kill).
- [ ] Validate: `glass_pad10092_impact_visual_regression.sh` (or its MG.5 distilled
      successor) passes ROI pixel comparison vs the ares oracle on GL **and** Metal;
      `port_room_glass_source_reconstruction_guard` stays green; Frigate room-57 and the
      Surface/Jungle XLU coverage regressions stay green.
- [ ] Close out the diag-flag hypotheses this resolves: promote the winning behavior to
      default, delete the refuted `GE007_DIAG_XLU_*` alternates (charter rule 7), keep
      `GE007_DISABLE_ROOM_XLU_CVG_MEMORY` as the one A/B hatch.

### MG.2 — Overlapping coplanar panes read stale framebuffer memory
**P2 · M**
**Files:** GL batch path `gfx_opengl.c:2140-2279` (`gfx_opengl_compute_batch_snapshot_rect`),
Metal port `gfx_metal.mm:3099` (per-batch snapshot only; `GE007_XLU_SNAPSHOT_MODE`
not honored on Metal).
**Bug:** the per-batch snapshot (the Jungle perf win) is exact only for non-overlapping
coplanar panes — two glass panes in one same-material batch each composite against a
snapshot that lacks the other's contribution. This is the documented correctness trade
and it lands squarely on glass.
- [ ] Using MG.1's overlap instrumentation, detect same-batch overlapping
      framebuffer-read triangles; on overlap, split the batch at the overlap boundary
      (or fall back per-triangle for that batch only). Keep the fast path for the
      common non-overlapping case — do not regress Jungle/Cradle (charter rule 4).
- [ ] Honor `GE007_XLU_SNAPSHOT_MODE=pertri` on Metal so both backends have the exactness
      A/B, then compare GL-vs-Metal screenshots on Dam/Frigate glass scenes.

### MG.3 — Shard geometry scale: six flags in, one answer out
**P2 · M**
**Files:** `src/game/unk_0A1DA0.c:1266-1336` (shard model-matrix build; default correction
`1/bgGetLevelVisibilityScale()` at `:1311-1313`), coverage diag
`gfx_pc.c:5776` (`GE007_TRACE_GLASS_SHARD_COVERAGE`), scorer
`tools/compare_glass_shard_pixel_oracle.py`.
**Bug:** the native projection compresses shard triangles differently from N64, so the
tree carries **six** competing A/B scale hypotheses (`GE007_GLASS_SHARD_COMPRESS`,
`_BASIS_SCALE`, `_NO_BASIS_SCALE`, `_SQRT_BASIS`, `_INV_VIS_SCALE`, `_FIXED_MTX`) with an
empirical default that has never been signed off against stock. Shard on-screen
size/coverage is unproven.
- [ ] Run the shatter route under each hypothesis with the coverage trace + pixel oracle;
      pick the one matching stock coverage, make it the unconditional code path.
- [ ] **Delete the other five flags** (charter rule 7 — this block is the poster child).
- [ ] Wire a shard-coverage regression (first-sample shard parity + NDC coverage budget)
      into the MG.5 glass lane so the answer stays locked.
- [ ] While in the file: give the shard float-mtx path a fail-closed overflow (skip shard
      on `dynAllocate` failure) — this is M1.2's contract applied here; today pool
      exhaustion snaps every overflowed shard onto one matrix (07-06 #63).

### MG.4 — Parity questions that need an oracle verdict, then a default
**P2-P3 · S-M each**
- [ ] **Shatter-trigger parity:** nothing automated guards that native shatters a pane at
      the same hit/damage count as stock — `glass_route_parity_regression.sh` asserts
      same pane hit, same active shard count, same prop removal, but is manual. Wire it
      ROM-gated (MG.5); add a crack-count→shatter assertion to it
      (damage model: `objGetDamage`/`maxdamage`, `chrobjhandler.c:2560-2600`).
- [ ] **Tinted-glass minimum opacity:** the 16/255 near-clear floor
      (`chrobjhandler.c:572-628`) is a deliberate, *unvalidated* deviation — A/B
      `GE007_TINTED_GLASS_MIN_OPACITY=0` against the oracle, then promote one behavior
      and reap the flag.
- [ ] **Crack accumulation/fade:** cracks currently persist unbounded per pane; confirm
      stock behavior (fade? cap?) against the oracle before adding anything — if stock
      does neither, ours doesn't either (faithful default), and we only add a cap if a
      degenerate accumulation case shows up in soak.
- [ ] **Classifier fallback hardening:** `gfx_room_xlu_cvg_memory_gate_reason`'s
      material-signature-only fallback (`dl_room<0 && dl_which==NULL && !room_matrix &&
      prim.a==0`) has no spatial key — harmless today (both branches converge), fragile
      under partial attribution. Add a spatial key or a converging assertion.

### MG.5 — Pay the glass tooling debt (charter rule 8 made concrete)
**P2 · S-M**
~49 `glass_*` scripts exist; only ~11 are wired (the ctest guards +
Surface/Jungle XLU smokes). The rest are spent scouts whose conclusion is already
recorded in `RENDERING_REGRESSION_NOTES.md` #13.
- [ ] Build **one** ROM-gated glass lane (`tools/glass_regression_lane.sh`) from the four
      load-bearing unwired scripts: `glass_route_parity_regression.sh`,
      `glass_material_regression.sh`, `glass_visual_oracle_regression.sh`,
      `glass_pad10092_impact_visual_regression.sh`; document it in INSTRUMENTATION.md and
      run it in the M8.3 release sweep.
- [ ] Delete the ~34 one-off scouts (the other `glass_pad10092_*` probes, `glass_actor_*`,
      the isolation/scout variants, and `analyze_*`/`score_*`/`compare_glass_projection_*`
      helpers not referenced by a wired guard). Their findings live in the regression
      notes; the scripts are spent. One commit, listed by name, revertable.
- [ ] Keep the six ctest glass/XLU guards untouched.

**Definition of done for the sprint:** the glass lane + shard-coverage regression pass on
GL and Metal; pad10092 ROI matches the oracle; exactly one shard-scale code path and one
opacity default remain; the `tools/` glass surface is ≤ 15 files, every one wired or
documented.

---

## M4 — HUD, text & UI overhaul

The "port, so make it better" milestone. The text stack already supersamples glyphs 3×
(`GE007_FONT_UPSCALE`) and widescreen 2D is solved — what's missing is player control,
true crispness, and languages.

### M4.1 — Register the env-only presentation knobs as real settings
**P1 · S — highest value-per-hour in the milestone**
The F1 overlay auto-renders every schema key (`src/app/ui_settings.cpp:63`), so each of
these is one `settingsRegister*` call plus replacing the `getenv` read:
- [ ] `Video.FontUpscale` (1–8, default 3) ← `GE007_FONT_UPSCALE` (`gfx_pc.c:14959`);
      also `FontAlphaCutoff`/`FontWhitePoint`/`FontPoint` (`:14991/:15008/:14980`) if
      they prove player-meaningful.
- [ ] Crosshair/reticle size-color-thickness ← `GE007_ADS_RETICLE_THK/GAP/LEN/DOT/OUTLINE`
      (`src/game/gun.c:33788`), plus a classic-crosshair scale.
- [ ] `Game.Language` enum (see M4.4), HUD-scale/opacity if trivially wireable.
- [ ] Verify the `Audio.Master/Music/SfxVolume` sliders actually appear and live-apply in
      the F1 Audio tab (buses are wired — `audi_port.c:201`, `music.c:1335`,
      `audio_compat.c:4950`); add to the native watch options menu only if we want
      in-fiction parity.
- [ ] Regen `docs/ENV_FLAGS.md`; keep env vars working as overrides.

### M4.2 — HD glyph path: truly crisp text everywhere
**P1 · L — the headline "prefer this over an emulator" upgrade**
**Files:** `src/platform/fast3d/gfx_pc.c:15081` (`gfx_upload_font_texture_i8`),
`gfx_pc.c:3767` (`gfx_is_font_texture_addr`), `src/game/textrelated.c:320`
(`text_dump_font_glyphs`), `src/platform/texture_pack.c:64` (loader pattern).
**Gap:** glyphs are authored at ~8-13 px; the 3× bilinear supersample makes them smooth
but soft at 4K. The HD texture pack loader is keyed on settex tokens and never sees font
uploads, so HD text is currently impossible.
- [ ] Extend `gfx_is_font_texture_addr` to return `(font_bank, glyph_index)` for a glyph
      upload (both banks are fixed 94-glyph tables loaded at known addresses in
      `load_font_tables`, `textrelated.c:468`).
- [ ] In `gfx_upload_font_texture_i8`, before the supersample, try
      `<pack>/fonts/<bank>_<index>.png` (mirror `texture_pack_try_load`'s stat + miss-cache
      design). On hit, upload the HD RGBA at N× native dims — UVs unchanged because tile
      dims stay native, exactly like the settex HD path.
- [ ] Author the atlas: use `text_dump_font_glyphs` to enumerate the 94 glyphs per bank;
      re-render at 4× from faithful-looking typefaces; ship as an optional pack (or bundle
      — glyphs re-rendered from fonts are not ROM media, but confirm provenance policy).
- [ ] Validate: side-by-side menu/briefing/HUD screenshots at 4K; faithful mode with pack
      absent is pixel-identical to today.

### M4.3 — Crosshair quality
**P2 · S-M**
`drawHitMarker`/`drawModernAdsReticle` (`gun.c:33711/33766`) are already resolution-scaled
vector drawing; only the default classic crosshair is still the low-res `crosshairimage`
texture (`gunDrawSight`, `gun.c:33836`).
- [ ] Either route `crosshairimage` through an HD-pack token or add a faithful-styled
      vector variant reusing the reticle drawer; expose size/color via M4.1 settings.

### M4.4 — Language support (PAL/JP banks)
**P2 · M**
**Files:** `src/game/lvl_text.c:78` (`j_text_trigger`, build-time), `:87` (lookup tables),
`:376` (JP multibyte glyph path, already implemented), `:458` (`langLoadToAddr`);
`assets/obseg/text/` currently ships only `*E` English banks.
- [ ] Extract/convert the PAL `*P` banks (Fr/De/It/Es) — and J if feasible — into the same
      header/asset format (they come from the user's ROM at runtime for the port paths;
      confirm which side of the asset-free boundary the text banks sit on and follow it).
- [ ] Register `Game.Language`; flip the lookup column + `langLoadToAddr` reload on change.
- [ ] Verify accented glyph coverage (é ü ñ) in the Gothic/Zurich banks; supplement via
      the M4.2 HD atlas if glyphs are missing.

### M4.5 — Intro caption polish
**P3 · S**
Mission-intro location/time captions already render (`bondview.c:6954/6965` via
`hudmsgBottomShow`). Make duration/styling consistent at all aspect ratios and optionally
toggleable. (A full subtitle system is N/A — GoldenEye has no spoken dialogue.)

### M4.6 — 21:9 menu spot-check
**P3 · S**
The aspect system is solved in code (`gfx_pc.c:16137/21087` pane-centered squeeze); do a
manual pass of every menu (watch tabs, folders, briefing, MP setup) at 21:9 and file
anything misplaced. `watch.c:538` has a leftover HACK comment (font-char pointer
arithmetic) — read it while there.

---

## M5 — Audio polish

Audio is in strong shape (no crackle/underrun bugs, queue-mode output, authentic
polyphase resampler, volume buses wired, RAW16 byte-swap fixed). What remains:

### M5.1 — Resolve H2: envelope/pole sample-ordering (last open DSP question)
**P2 · M**
**Files:** `src/platform/mixer.c:39-40` (`MIXER_ENV_SAMPLE_XOR_DEFAULT`/`POLE` = 0),
used at `:588` (env) and `:732/740/757` (pole).
- [ ] Run the existing capture + `compare_audio_reference.py` A/B against ares for both
      xor settings on a music-heavy route; lock the winning default and delete the loser
      or keep it as a documented A/B hatch. Record the verdict in the restored
      `docs/design/AUDIO_QUALITY_PLAN.md`.

### M5.2 — H3: per-instrument bank loop/tuning audit
**P2 · M**
The "cheap instruments, Surface 2 worst" report was never converted to measured deltas.
- [ ] Run the Phase-0 reference capture for gameplay levels (Surface 2 + a control level);
      diff per-instrument. Only touch the bank parsers
      (`audio_compat.c:99-341`, `bank_make_adpcm_loop` ~`:139`) if the capture confirms a
      defect. Also covers STATUS.md's "final reverb balance" follow-up.

### M5.3 — Optional 48 kHz output stage
**P2 · M — biggest cheap fidelity win on modern hardware**
**Files:** `src/platform/audi_port.c` (final mix path around `:483`),
`src/platform/audio_pc.c:26` (`PORT_AUDIO_RATE` 22050).
- [ ] Add an opt-in final polyphase upsample 22050→48000 before `osAiSetNextBuffer`
      submission and open SDL at 48 kHz when enabled (`Audio.OutputRate` enum:
      22050 native / 48000). The synth stays at 22050 — pitch math depends on it.
      Default OFF to preserve byte-identity.
- [ ] Validate: null test at 22050 (flag off = identical bytes); listening check + spectral
      compare at 48 kHz.

### M5.4 — Audio hygiene
**P3 · S each**
- [ ] Delete the dead simple-synth in `audio_pc.c` (`musicNoteOn`, `calcNotePitch`,
      `findInstSound`, `musicMixSamples`, `portMusicPlaySequence`, legacy MIDI parser at
      `:2361` — zero external callers; keep device open, volume buses, spatial-mix).
- [ ] Remove/annotate the dead distance-attenuation branch in `portAudioComputeSpatial`
      (`audio_pc.c:751` — real path passes `attenOut=NULL`, `snd.c:1199`).

---

## M6 — Platform & Windows hardening

### M6.1 — Windows diagnostics are a no-op (players can't report bugs)
**P1 · M**
**Files:** `src/app/diag_log.cpp:34-38` — the entire `_WIN32` branch is stubbed
(`DiagLog_install() {}`, empty snapshot, empty path); POSIX (`:64-101`) tees to
`mgb64.log` with rotation and feeds the F1 diagnostics view.
- [ ] Implement the Windows tee: `freopen`-to-file + `SetStdHandle` (or CreatePipe +
      reader thread) into `SDL_GetPrefPath("MGB64","MGB64")\mgb64.log`, same
      prev-rotation, same ring buffer for `DiagLog_snapshot`.
- [ ] Validate on a real Windows machine or Wine: log file appears, F1 diagnostics
      populate, crash diagnostics from M6.2 land in it.

### M6.2 — Windows crash handler: remove the SEH-UB `longjmp` path
**P2 · S-M (latent — only reachable with `GE007_ENABLE_RECOVERY`)**
**Files:** `src/platform/main_pc.c:659-660` (plain `signal()` on `_WIN32`), `:387`
(`siglongjmp` from handler), `src/platform/platform.h:39-42` (`siglongjmp`→`longjmp` on
Windows); POSIX correctly uses `sigaction`+`SA_ONSTACK` (`:447-455`).
- [ ] On Windows, replace with `SetUnhandledExceptionFilter` (or a vectored handler) that
      writes the same `[CRASH]` diagnostics and terminates; compile out the recovery
      `longjmp` and force `recovery_enabled=0` on `_WIN32`.

### M6.3 — Config-parsing safety
**P3 · S**
- [ ] `src/platform/config_pc.c:227-229`: unmatched enum token falls back to raw
      `strtol` with no range clamp — clamp to `[0, enum_count-1]` or reject.
- [ ] `src/platform/port_env.c:41-44`: `get_or_create` ignores the `kind` of an existing
      entry — a reused name type-puns the value union. Kind-check, log, ignore on mismatch.

### M6.4 — Gamepad button rebinding
**P1 · M — most-requested missing input feature**
**Files:** `src/app/ui_bindings.cpp` (keyboard-only capture; footer at `:72-73` admits
"Gamepad (fixed)"), `src/platform/input_bindings.c`.
- [ ] Add a gamepad binding table parallel to the scancode one; capture
      `SDL_CONTROLLERBUTTONDOWN` in the existing rebind flow; persist via the schema.
- [ ] Optional: `Input.LookCurveExponent` response-curve slider while in the area.

### M6.5 — CI: protect the platforms we ship
**P2 · S-M — weigh against the deliberate no-hosted-auto-trigger provenance posture**
**Files:** `.github/workflows/ci.yml:9` (dispatch-only), `release.yml:57-82` (Windows job
has no smoke-launch and no asset-free verify; Linux does both at `:45-51`).
- [ ] Add a lightweight `pull_request` lane: hygiene checks + Linux build + ROM-free ctest.
- [ ] Add a Windows MinGW build job to `ci.yml`; mirror smoke-launch +
      `verify_asset_free` into the Windows release job.

### M6.6 — Launcher/startup niceties
**P3 · S each**
- [ ] ROM SHA-1 advisory: `mgb_validate_rom` checks size + title signature but not
      `ge007.u.sha1` — on explicit selection, warn (don't block) for non-matching hashes
      (ROM hacks/PAL) since behavior is unvalidated there.
- [ ] Linux: document `SDL_VIDEODRIVER=x11` Wayland fallback in README troubleshooting;
      consider a flatpak manifest (M, optional).
- [ ] Decide the shipping default for the FPS/frame-time overlay (currently default-ON by
      choice; fine for beta, revisit for 0.4.0 final).

---

## M7 — Split-screen multiplayer

2-player is green (distinct viewports, render-health clean). GoldenEye's defining feature
deserves the full matrix.

### M7.1 — 3/4-player validation + scoreboard
**P1 · M-L**
**Files:** `src/game/lvl.c:1713/1722` (`viSetupScreensForNumPlayers`), `src/game/mp_watch.c`
(scoreboard/results), `tools/mp_smoke.sh`.
- [ ] Extend `mp_smoke.sh` to 3-player (asymmetric split — the risky viewport math) and
      4-player with per-quadrant dissimilarity assertions.
- [ ] Drive a full round to completion; assert the scoreboard/results transition renders
      (this is the open "scoreboard/results transition proof" in STATUS.md).
- [ ] Sustained-load frame budget on the heaviest MP stage with
      `room_render_fallback_records == 0` (interacts with M2.3's fallback narrowing).
- [ ] Label 3/4-player "beta" in the launcher until this passes.

---

## M8 — Validation rails & release

### M8.1 — sim-hash must cover the prop/chr BSS
**P2 · S-M**
**Files:** `src/platform/sim_state_hash_registry.c:24-38` — only `s_pcPool`,
`g_ClockTimer`, `g_GlobalTimer` are hashed; movement/collision drift in BSS-resident
chr/prop/stan state is invisible to the invariance gate that everything else in this
backlog leans on.
- [ ] Register the guard/prop record arrays and stan/collision scratch as additional
      `SimHashRegion`s (BSS base/size); re-baseline the invariance lane.
- [ ] Do this EARLY in the cycle (ideally alongside M1) so M2.3/M2.5 sim changes are
      guarded by a hash that can actually see them.

### M8.2 — Route/validation expansion (the honesty gap)
**P2 · ongoing**
The stock-ROM oracle lane is Dam-only; ~12 stages have placeholder intro routes with empty
waivers; objective/report/exit contracts are scripted, not organic.
- [ ] Capture stock traces for the 8 classified stages' swirl/anim routes; then the 12
      uncaptured stages (ledger T25) in priority order: Facility, Bunker 1, Surface 1/2,
      Frigate (the campaign spine).
- [ ] One organic mission-completion route (menu → objectives → report → save) for Dam as
      the template, then Facility. The scout non-promotions (Surface 1 key ~3,652u short,
      Bunker key pad 8, Surface II bridge ~25,120u short) stay documented as open until an
      organic route or better scout closes them.
- [ ] Set a numeric soak budget: `tools/soak_stability.sh` N-hour per-stage run with zero
      crashes/render-health failures as a release gate (currently "no budget claimed").

### M8.3 — Release QA sweep + ship
**P1 · M**
- [ ] Week-1 manual-QA carryover (2026-07-08 review): controller-in-hand feel check of
      the radial movement deadzone (M2.1); real-mouse multi-notch wheel scroll (M2.2,
      no runtime validator exists — 3 notches must give 3 switches); full 20-level
      Metal minimap sweep (M0.2 validated 3 levels); MinGW/Windows CI build of the
      week-1 batch if not already dispatched.
- [ ] Run the manual priority list from `RENDERING_REGRESSION_NOTES.md` (Dam glass/decals/
      truck, Surface snow + sky-fog boundary, Cradle bridge/fog/faces, Silo/Depot/Train/
      Caverns corridors) on GL and Metal, plus the M4 UI screens at 16:9/21:9/4:3.
- [ ] Full gate stack: `native_playability_regression_suite.sh`, campaign routes 34/34,
      MP lanes, save persistence, ASan lane, release guards.
- [ ] Update STATUS.md (it currently undersells the port), README known-issues, ENV_FLAGS
      regen; `scripts/release.sh` for all three platforms per RELEASE_CHECKLIST.md.

---

## Appendix A — Verified already fixed (stop re-auditing these)

| Item | Where fixed |
|---|---|
| Bond intro red shards (weapon overwrote head in shared load buffer) | `50f3f61` + R1 fail-closed (`bondview.c:2905/2988`) |
| Weapon fire-mode jump tables: detonator, M16/RC-P90 full-auto, tank cannon, magnum floor | `03becb6`, `854315e`, `abf6f3e`, `13a4d70`; both switches carry per-item retail-ASM citations (`gun.c:18062-18360`) |
| Weapon equip/reload sound cues vs jpt_80054194/80054294 | verified faithful (`gun.c:18824/18967` vs retained ASM `gun.c:20987-21014`) |
| Magazine pickup granted remote mines | `78787dc` |
| Multi-ammo crate collect read wrong lane | `97e7898` (PR #23) |
| Multi-ammo crate DESTROY scatter loop missing | present: `chrobjhandler.c:37841-37886` (`ammocrateAllocate`) |
| chr.c "magic travel" | gate narrowed to intro window (`chrShouldSuppressIntroMagicTravelVisibility`, `chr.c:4890`) |
| `FrameCap=display` ran sim at panel refresh | `e5b09eb` |
| G_MW_NUMLIGHT OOB | `a122288`; F3DEX2 light index + segment bounds also in |
| Vtx/Light bump-allocator OOB writes | `9b9c4e2` (aliasing half still open → M1.2) |
| Metal semaphore permit leak on nil command buffer | `e0a1b99` |
| SSAO hemisphere perspective-divide guard | `c5dd67d` |
| Metal shadow-VBO race | ring-buffered `s_shadow_vbuf[MTL_VBUF_SLOTS]` (`gfx_metal.mm:911`) |
| CC-pool wrap @256 / fixed texture pool | `2576a34`, `7aae535` (growable) |
| Jungle/Cradle perf (per-tri XLU copy, room-attribution) | content on main: `gfx_opengl_compute_batch_snapshot_rect` (`gfx_opengl.c:2140`, Metal port `gfx_metal.mm:3099`), `g_room_cmd_cache_generation` |
| Windows `-mno-ms-bitfields` crash | `d629355` lineage (`CMakeLists.txt:634/891`) |
| Watch menu inventory/controls/options stubs | real renderers live (`watch.c:3848/6995/10801`) |
| Volume buses stale `MasterVolume` | all three buses live (`audi_port.c:201`, `music.c:1335`, `audio_compat.c:4950`) + ini migration (`config_pc.c:314`) |
| RAW16 byte-swap deep-bass buzz | default-on `mixerLoadBufferSwap16` (`mixer.c:288`) |
| H1 output low-pass | resolved REJECT on ares evidence; plumbing kept default-off |
| Glass disappearing / edge-on crack decals | landed (STATUS.md; secondary-room alpha + aligned decals) |

Refuted/policy (don't reopen without new evidence): G_FOG re-assert on world geometry is a
deliberate, guarded port workaround (`gfx_pc.c:19953-19961`); N64 3-point filtering IS
emulated on Metal (`n64TextureFilter`, `gfx_metal.mm:338`); doppler absence is faithful;
MSVC support is deliberately out of scope (`CMakeLists.txt:32`).

## Appendix B — Suggested sequencing & sizing

Rough serial order inside the parallel tracks (sim, renderer, UI/platform can proceed
concurrently once M0 lands):

1. **Week 1:** M0 (all), M8.1 (sim-hash BSS — early so it guards the rest), M1.1, M2.1,
   M2.2, M2.4, M6.3.
2. **Weeks 2-3:** M1.2, M1.3+M1.4 (pair them), M2.6, M3.2, M3.3, M4.1.
3. **Weeks 3-5:** M2.3 (+M2.5 rides it), M3.1, M3.4, **MG.5 then MG.3/MG.4** (tooling
   lane first so the sprint has its measurement rail), M6.1, M6.2, M6.4, M5.1-M5.3.
4. **Weeks 5-7:** **MG.1 then MG.2** (the hard glass work, after the lane exists),
   M4.2-M4.4, M3.5, M3.6, M6.5, M7.1, M2.7.
5. **Week 8:** M8.2 minimum bar, M8.3, ship v0.4.0.

The single highest-leverage items if forced to choose six: **M1.2** (kills the corruption
class), **M2.3** (combat correctness), **M2.1** (game feel in 2 hours of work), **MG.1**
(the hardest visual problem — glass compositing), **M3.1** (Metal stops ignoring MSAA),
**M4.2** (crisp text — the thing every screenshot shows).
