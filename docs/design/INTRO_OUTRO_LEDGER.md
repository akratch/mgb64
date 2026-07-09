# Intro/Outro Defect Ledger (D-numbers)

**Status (2026-07-08):** curated table extracted from the working ledger in
`.superpowers/sdd/progress.md` (feat/intro-outro-faithfulness program, T1-T29).
This is the reference `docs/CINEMATICS.md` points at as "the plan doc's defect
ledger." **D43 and D31 landed 2026-07-08** (commits `9acba24` and `510e181`):
Bond now plays his scripted phase-3 animation with root motion and settles
onto the floor through the whole intro swirl, instead of standing frozen and
hovering above the ground. The D35/D36 waivers were written against the
pre-fix behavior and need to be re-tested against these two landings to see
whether they can be removed — that re-test is **not done yet** and is tracked
as the first bullet of `docs/BACKLOG_v0.4.0.md` milestone **M1.5** ("Close out
the remaining intro D-ledger items"), which is the plan of record for every
D-number still open below.

| D# | Status | Description |
|----|--------|--------------|
| D2 | Fixed | Native intro-skip path didn't match retail's no-op behavior; fixed with the `GE007_NO_CINEMA_INTRO_FIX` hatch and routed through `bondviewNativeSkipIntroToFp`; triple-digest match proved the retail path is a no-op. |
| D4 | Fixed | Chr-load timing diverged from stock's ~7-VI-frame ROM-DMA latency; native now defers chr load one tick to match; waivers removed and the fix proven against the oracle. |
| D5 | Refuted | Suspected `cameraBufferToggle` breaks outros — empirically disproven; the mechanism works correctly on PC (verified via `dam_mission_flow_smoke`). |
| D6 | Fixed | Cinematics rendered at gameplay FovY 50 instead of N64 60; fixed via `Video.CutsceneFovY` (default 60), applied render-only at `viSetFovY`. |
| D7 | Closed (pre-existing) | Sky-blue shard signature at intro timer marks; already fixed before this program started, confirmed clean during verification-only Task 1. |
| D8 | Fixed | Cinematic-mode predicate (intro/swirl detection) fixed as part of the intro/swirl work; POSEND/DEATH inheritance separately verified. |
| D11 | Documented | Legacy animation-seed fudge value made explicit and value-identical, behind `GE007_INTRO_ANIM_LEGACY_SEED`. Not a behavior change. |
| D12 | Fixed | Viewer-body allocation could fail silently and the head buffer could overrun; made fail-loud with a bounds guard (defensive hardening, byte-identical render verified). |
| D13 | Verified no defect | HUD-during-cinematics concern checked and found absent in every cinematic capture, present only during gameplay — no fix needed. |
| D14 | Fixed | Camera-path render defect fixed as a side effect of the D42 admission fix (POSEND/death cameras now render full scene with Bond body instead of a floating fragment). |
| D15 | Fixed | Music-mapping recursion risk on death; fixed fail-loud with a recursion guard, keeping the faithful death-sting fallback. |
| D17 | Verified no-op | Suspected sound-disable leak on the VIEWER prop; stock's `disable_sounds` guard already rejects it correctly — no leak observed, no fix needed. |
| D20 | Verified faithful | Suspected menu/unlock gating regression; confirmed unlocks only fire on `objectiveIsAllComplete` per the shipped `VERSION_US`/`BUGFIX_R0` build — faithful as-is. |
| D28 | Fixed | Deterministic camera-index pick used a non-stock RNG shape; fixed so the pick is stock-shaped while keeping deterministic-index-0 priority intact. |
| D29 | Verified clean | Census of pre-pick RNG draws across 3 stages found all draws stock-equivalent with no port-added draws — nothing to fix. |
| D30 | Closed | Menu-boot camera path is now bit-exact vs. direct-boot (1076 records, max abs diff 0.0); residual divergences are the same D31/D32/D35 classes as direct-boot, not menu-specific. |
| D31 | **Landed 2026-07-08** (`510e181`) | Intro Bond hovered ~57u above the floor during the swirl, then snapped down when phase 3 took over. Root motion is now unpinned across all three intro animation phases (not just phase 3), so Bond settles onto the floor across the whole swirl, matching stock. Re-test of the associated waiver is M1.5's first bullet. |
| D32 | Open (re-test pending) | Grouped alongside D31/D35/D36 in the original intro-route waiver set; re-test against the D31/D43 landings is scheduled under M1.5. |
| D33 | Resolved as no-fix | `Input.SteadyView` was suspected to shift the swirl look-target; investigation showed the default (neutral head, no tilt) is itself oracle-validated, so forcing an anim-driven head tilt would move *away* from the validated behavior. Left as-is by evidence, not skipped. |
| D35 | Open (re-test pending) | Camera-anchor drift at swirl segment boundaries. Root-caused: fully attributed to D36 (stock's phase-3 scripted animation drives an anchor bob that native didn't reproduce before D43). Re-test whether the waiver can be removed now that D43 has landed — tracked under M1.5. |
| D36 | Open (re-test pending) | Stock's intro Bond plays a 3-phase animation; native reproduced phases 1-2 faithfully but never fired stock's phase-3 scripted `PlayAnimation` (a Bond-cinema AI-list call that native's frozen-camera mode never executed). Fix (D43) has landed 2026-07-08; re-test whether the D35/D36 waivers can now be removed is M1.5's exit criterion for this item. |
| D37 | Open | Static establishing-shot `cam_floor`/`cam_delta` divergence from stock (Statue ~34u, Cradle ~9083u); needs instrumentation of the static-shot camera seed vs. stock captures. Assigned to M1.5. |
| D38 | Open | Stock's static-shot duration varies +3..+14 ticks across captures while native's is fixed; needs stock's duration source identified (likely per-setup or load-driven) and replicated. Assigned to M1.5. |
| D39 | Open | Statue stage shows a `cam_delta` anchor divergence across all 3 capture modes; grouped with D37 for the static-shot camera-seed investigation. Assigned to M1.5. |
| D40 | Deferred | Small residual per-stage divergence classes noted during the 13-stage capture sweep; not scheduled under the current M1.5 bullet list (original plan scoped it to a later capture-completion task). Revisit when the 13 stages with placeholder-only routes get full anim-route captures. |
| D41 | Open | ~3-tick anim-phase shift between menu-boot and direct-boot intro timing; needs the tick-offset entry point traced (likely menu-exit timing) and aligned. Assigned to M1.5. |
| D42 | **Fixed, DEFAULT ON (T13b, 2026-07-09)** | Establishing shots rendered near-blank (only the spawn room was admitted; detached-camera supplemental room seeding was skipped). Seed half fixed: seed the room BFS from the camera's own resolved room (`GE007_NO_CAMERA_SEED_FIX` hatch). **T13** root-caused the residual (headline Silo defect: large sky-blue areas / blue behind Bond): the seed step admits the camera room but never *walks* its portals, so the BFS stops at the camera room + its one-hop supplement neighbors and everything one hop further down the sightline falls through to the clear color. Walking the BFS from the camera room fills it (Silo intro blue-clear pixels 3681→132, Dam ~4074→54), but the original walk fed `room_rendered`, which is coupled to the sim (`room_rendered`→`getROOMID_isRendered`→`PROPFLAG_ONSCREEN` gates actor ticks / the deterministic `pcRandom` stream), so it diverged `intro_oracle_dam_route` from stock — the reason T13 shipped opt-in. **T13b decouples it and makes it DEFAULT ON:** the walk runs as the final admission step (snapshot the settled default `room_rendered`+`room_neighbor_to_rendered`, run the stock camera-room walk + the same edge-rescue/frustum-fallback/visibility-supplement passes so it reaches exactly the T13 rooms, mark the rooms added beyond the snapshot **draw-only**, then restore the two sim-visible fields to the snapshot). Draw-only rooms are in the draw list (geometry+props emit DLs) but invisible to every `room_rendered` consumer, so the sim is byte-identical (verified: 0 sim-critical trace diffs vs walk-off across 599 frames on Silo+Dam; `intro_oracle_dam_route` unchanged; negative control `GE007_NO_CAMERA_SEED_WALK=1` byte-identical to pre-T13b). The draw-only set is provably empty in gameplay (`camera_seed_room=-1` off-intro; `GE007_TRACE_DRAW_ONLY` probe). |
| D43 | **Landed 2026-07-08** (`9acba24`) | Intro Bond stood completely static during the swirl instead of playing stock's scripted phase-3 animation (drawing his weapon) with root motion. Fixed by transitioning the intro chr to the measured phase-3 animation at swirl segment 4 and letting its root motion drive the prop position instead of pinning it to the static spawn anchor. This is the root fix that D35/D36 were waived pending. |

## Plan of record

Every open row above is scoped under `docs/BACKLOG_v0.4.0.md` milestone
**M1.5 — Close out the remaining intro D-ledger items**, except D40 which
remains deferred to a future capture-completion pass (see the D40 row). Do not
add new investigation docs for these items — update this table and the
backlog when a D-number's status changes.
