<!-- Provenance: authored 2026-07-01 from a live profiling investigation of the
Fast3D native port. Method: macOS `sample` call-graph on the Jungle render loop +
the built-in GE007_PERF_TRACE per-frame `work_ms` tracer, run headless and
deterministically across all 20 direct-boot levels with controlled A/B env knobs
(GE007_DISABLE_ROOM_XLU_CVG_MEMORY, --faithful, Video.RenderScale). Numbers are
mean per-frame CPU+GPU work at the deterministic spawn view on Apple M3 Max,
-O3 Release, macOS OpenGL-over-Metal (AppleMetalOpenGLRenderer). Root-cause
supported by two independent subsystem audits (fast3d hot path; bg.c culling). -->

# MGB64 Native Port — Performance Roadmap ("Make It Rip")

**Status:** M0–M2 **SHIPPED** (branch `perf/make-it-rip`). Goal met: all 20 levels
now **101–189 fps** (was: 5 below 60, Jungle at 18). M3–M5 assessed and deferred
as scalability/hardening (rationale in §5); M6 docs/governance in progress.
**Owner:** rendering/port.
**Companion docs:** `docs/RENDER_PORT_SURVEY.md` (rendering correctness), `docs/REMASTER_ROADMAP.md` (visual features), `docs/INSTRUMENTATION.md` (harness).

## 0. Results (as executed, 2026-07-01)

Two behavior-safe changes moved every level above 100 fps on the reference
machine (M3 Max, -O3). Measured mean per-frame `work_ms`, deterministic spawn:

| Level | Before | After M1+M2 | | Level | Before | After M1+M2 |
|---|---:|---:|---|---|---:|---:|
| jungle | 18 fps | **131 fps** | | dam | 50 fps | **137 fps** |
| cradle | 30 fps | **165 fps** | | control | 71 fps | **127 fps** |
| surface2 | 42 fps | **180 fps** | | egypt | 71 fps | **116 fps** |
| surface1 | 54 fps | **183 fps** | | frigate | 76 fps | **107 fps** |
| statue | 83 fps | **158 fps** | | *(all others)* | 106–270 | **155–189** |

- **M1** (per-batch XLU snapshot) fixed the five Tier-A levels — e.g. Jungle
  55.2→8.4 ms (7.3×). Validated pixel-identical on Dam glass (0/0/0 diff),
  imperceptible on Jungle foliage (0.18/0.26/0.09 avg/255), both XLU visual
  regression lanes pass (MSAA 0 and 4), ASan/UBSan clean, adversarial review
  found 0 confirmed defects.
- **M2** (glass-label hoist + room-cmd-offset memoization) fixed the Tier-B floor
  — e.g. Dam 18.9→8.3 ms (60→137 fps), Control/Egypt/Frigate similarly. Proven
  **byte-identical** (Dam+Jungle 0/0/0 diff vs the M1 binary); interleaved A/B
  vs the true baseline binary shows no regression on any level (fast levels
  slightly *faster*).
- **What we did NOT need to touch:** the HD remaster (SSAA/post-FX ≈ 0.5–1 ms),
  compiler flags, or draw-call batching (GL submission is only ~7% of frame
  post-fix — see §5 M3).

---

## 1. Executive summary

The native port is CPU/GPU-render bound in a way that has nothing to do with the
game being from 1997. On modern hardware the game *should* run at hundreds of FPS
everywhere; instead five levels drop below 60 and one (Jungle) sits at **18 fps**.
The cause is **not** the N64 workload, **not** the HD remaster, and **not** a
missing compiler flag. It is two specific, fixable architectural defects in the
Fast3D translation layer, both of which are *symptoms of one systemic problem*:

> **Development instrumentation and single-level visual hacks were shipped
> default-on, globally, inside the per-primitive render hot path.**

A full performance census (§3) shows two independent cost centers:

1. **A per-triangle GPU pipeline stall** (§4.1) — the XLU "RDP coverage-memory"
   blend path copies the framebuffer into a texture *once per translucent
   triangle*. It was built for Dam's glass and never scoped. It degrades every
   open / foliage / translucency-heavy level. Disabling it is a measured
   **7.3× on Jungle, 5.5× on Cradle, 5.1× on Surface 2, 3.9× on Surface 1**.
2. **Unconditional per-primitive CPU overhead** (§4.2) — the triangle/vertex hot
   path runs dev-only bookkeeping on *every* primitive even when no trace is
   enabled: `O(rooms)` room-attribution scans twice per triangle, redundant NDC
   recomputes, ~40 diagnostic predicates per triangle, ~30 debug-field stores per
   vertex. This is the residual floor that keeps Dam/Frigate/Control at 65–90 fps
   even after everything else is disabled.

**What is NOT the problem (measured, so we stop chasing it):** 2× SSAA
(`RenderScale=2`), FXAA, 49-tap bloom, filmic tonemap, grade, vignette, 16×
anisotropy — the entire remaster pipeline costs **~0.5–1 ms/frame on every
level**. `--faithful` barely moves the needle (Jungle 55→53 ms). The GPU is not
fragment-bound; it is stall-bound and the CPU is bookkeeping-bound.

**The goal ("rip"):** every level ≥ 120 fps (≤ 8.3 ms) at default settings on the
reference machine, with a hard floor of 60 fps on min-spec, enforced by a standing
regression gate so it never silently comes back. After M1+M2 the census projects
**all 20 levels under 8 ms** (see §6). This plan sequences that work and, more
importantly, establishes the discipline that keeps it fast.

---

## 2. Design principles (the generalized, S-tier pathway)

The point of this plan is not five spot-fixes; it is a durable performance
architecture. Every milestone below serves one of these principles.

- **P1 — The hot path is sacred.** Nothing executes per-triangle or per-vertex
  unless it draws that triangle. No diagnostics, no room attribution, no effect
  labelling, no `strcmp`/`strstr`, no debug provenance in the primitive loop.
  Dev instrumentation lives behind a compile-time flag or a single per-frame /
  per-DL check — never a per-primitive branch. *(This is the root fix; §4.2.)*

- **P2 — Features are scoped, budgeted, and A/B-gated.** A visual feature that
  helps one level must not tax the other nineteen. Every perf-sensitive feature
  ships with (a) a scope (which levels/materials it applies to), (b) a frame-time
  budget, and (c) a `GE007_*` escape hatch — exactly the "default-off
  byte-identical" discipline the remaster already uses, extended to performance.

- **P3 — Performance is a measured contract, not a hope.** Per-level frame-time
  budgets, a deterministic headless benchmark, and a CI/local regression gate.
  If a change regresses any level past its budget, the gate fails. We already
  have the primitives (`GE007_PERF_TRACE`); M0 turns them into a standing lane.

- **P4 — Correct-by-construction, then fast.** Every optimization preserves
  visual and gameplay parity, proven by the existing screenshot/oracle suites,
  and keeps an escape hatch so any regression is one env var away from A/B.
  Performance work must never silently change what the player sees or how the AI
  behaves (note the `room_rendered → auto-aim` coupling, `chr.c:5205`).

- **P5 — Amortize at the batch, not the primitive.** GPU submission and
  framebuffer readback are batch-level operations. Snapshot once per pass, upload
  once per batch, minimize state transitions. The Fast3D "one draw call per
  material run" model is the ceiling to raise for scalability (§4.3).

---

## 3. The performance census (evidence)

Mean per-frame `work_ms` at the deterministic spawn view, M3 Max / -O3 / default
settings. `XLU-off` = same run with `GE007_DISABLE_ROOM_XLU_CVG_MEMORY=1`.
Speedup = default ÷ XLU-off. Higher speedup ⇒ more dominated by the §4.1 defect.

| Level | Default ms | Default FPS | XLU-off ms | Speedup | Tier |
|---|---:|---:|---:|---:|:--|
| **Jungle** | 55.2 | **18** | 7.6 | **7.26×** | A |
| **Cradle** | 32.6 | **30** | 6.1 | **5.48×** | A |
| **Surface 2** | 23.7 | **42** | 4.7 | **5.07×** | A |
| **Surface 1** | 18.4 | **54** | 4.7 | **3.88×** | A |
| Statue | 12.0 | 83 | 4.5 | 2.64× | A |
| Runway | 9.4 | 106 | 6.8 | 1.38× | A/– |
| Egypt | 13.8 | 71 | 10.2 | 1.32× | B |
| Streets | 8.3 | 121 | 6.5 | 1.28× | – |
| Depot | 7.3 | 137 | 5.6 | 1.30× | – |
| Bunker 2 | 5.9 | 171 | 4.8 | 1.23× | – |
| **Dam** | 19.9 | **50** | 16.8 | 1.19× | B |
| Archives | 7.5 | 133 | 6.6 | 1.14× | – |
| **Control** | 14.1 | **71** | 12.8 | 1.10× | B |
| Facility | 5.8 | 172 | 5.3 | 1.09× | – |
| **Frigate** | 13.2 | **76** | 13.2 | 1.00× | B |
| Bunker 1 | 6.8 | 148 | 6.7 | 1.01× | – |
| Silo | 4.6 | 216 | 4.6 | 1.01× | – |
| Caverns | 6.1 | 163 | 6.0 | 1.02× | – |
| Aztec | 4.7 | 211 | 5.5 | 0.86× | – |
| Train | 3.7 | 270 | 3.7 | 0.99× | – |

**Tier A** (jungle, cradle, surface1/2, statue): dominated by the §4.1
per-triangle framebuffer copy. Fix → all reach 130–220 fps.
**Tier B** (dam, frigate, control, egypt): *not* helped by the XLU fix
(speedup ≈ 1.0). Their cost survives `--faithful` too (Frigate 12.4→12.5 ms) — it
is raw CPU hot-path + draw-call cost (§4.2, §4.3).

Attribution pass (default → `--faithful` → `--faithful` + XLU-off), Tier B:

| Level | default | faithful | faithful+XLU-off | ⇒ remaster cost | ⇒ residual CPU floor |
|---|---:|---:|---:|---:|---:|
| Dam | 19.4 | 18.8 | 15.3 | ~0.6 ms | **~15 ms** |
| Frigate | 12.4 | 12.5 | 12.2 | ~0 ms | **~12 ms** |
| Control | 13.2 | 12.1 | 11.2 | ~1.1 ms | **~11 ms** |
| Egypt | 13.8 | 14.1 | 10.2 | ~0 ms | **~10 ms** |

The remaster is nearly free; the floor is CPU render overhead.

---

## 4. Root-cause taxonomy

### 4.1 The per-triangle framebuffer stall (Tier A — the headline)

`gfx_opengl_draw_triangles` (`src/platform/fast3d/gfx_opengl.c:1958`) contains a
**per-triangle loop** (`:1966`):

```c
for (size_t tri = 0; tri < buf_vbo_num_tris; tri++) {
    ...
    gfx_opengl_copy_framebuffer_snapshot(viewport, requested_rect);  // :1976
    glDrawArrays(GL_TRIANGLES, tri * 3, 3);                          // one triangle
}
```

For every translucent triangle, `gfx_opengl_copy_framebuffer_snapshot`
(`:1740`) issues ~6 `glGetIntegerv` state queries, rebinds read/draw
framebuffers, runs `glCopyTexSubImage2D` (`:1801`) to copy the framebuffer into
a texture, then restores state — after which one triangle is drawn. In the
`sample` profile this single chain (`gfx_flush → gfx_opengl_draw_triangles →
glCopyTexSubImage2D → Metal submitCommandBuffer`) is **~47 ms of a 55 ms Jungle
frame (~85%)**. The cost is not the copy *size* (a per-triangle bbox rect is
tiny); it is **serialization** — each copy waits for all prior draws to finish
and each subsequent draw waits for the copy, thousands of times per frame.

- **Trigger:** materials using the N64 AA-XLU "wrap-color-on-coverage / read
  framebuffer memory" mode (`gfx_pc.c:5016`) are routed to
  `GFX_BLEND_ALPHA_RDP_CVG_MEMORY`. GoldenEye foliage, waterfalls, and layered
  translucency use this mode pervasively.
- **Why default-on globally:** the gate `gfx_opengl_room_xlu_cvg_memory_enabled()`
  (`:295`) **returns 1 unless `GE007_DISABLE_ROOM_XLU_CVG_MEMORY` is set**. The
  feature exists to make Dam's glass blend against the framebuffer correctly; it
  was never scoped to Dam.
- **Why it's a P1/P2 violation:** a Dam-glass visual feature runs its most
  expensive path on every translucent triangle of every level.

### 4.2 Unconditional per-primitive CPU overhead (Tier B floor)

Independent audit of the Fast3D hot path found dev-only work that executes on
every primitive regardless of whether any `GE007_*` trace is enabled:

- **`O(triangles × rooms)` room attribution**, run **twice per triangle**:
  `gfx_find_room_for_dl_addr` (`gfx_sp_tri1:18732`) and again in
  `gfx_emit_loaded_triangle:16329`, each scanning up to `g_MaxNumRooms` × 2 DL
  ranges. Clipped triangles scan once per fan tri.
- **Redundant NDC-metrics recompute** per triangle (`gfx_sp_tri1:18730` +
  `gfx_emit_loaded_triangle:16325`).
- **~40 diagnostic predicate evaluations per triangle** (`16333–16457`), several
  using `strcmp`/`strstr`.
- **~30 `dbg_*` debug-field stores per vertex** (`gfx_sp_vertex:16144–16170`) +
  a per-vertex effect-label linear scan over ≤256 ranges + `strstr(label,
  "glass")` (`:16108`).
- **Per-command draw-class linear scan** over ≤512 ranges (`gfx_run_dl_pc:21167`).

None is gated by a runtime flag; all of it is the machinery that lets developers
attribute triangles to rooms/effects during debugging. In a shipping frame it is
pure waste, and it scales with triangle/vertex/command count — i.e. it is worst
on exactly the heavy levels.

### 4.3 Draw-call churn (Tier B ceiling, scalability)

Fast3D batches per material run: each shader/combine/texture/blend/depth/scissor
change flushes a batch as a fresh `glBufferData(GL_STREAM_DRAW)` full re-upload +
`glDrawArrays`, with no index buffer and no vertex reuse (`gfx_opengl.c:1962`,
`gfx_pc.c` flush triggers `17698–17900`). Heavy scenes with many small material
runs produce many small draw calls. This is inherent to Fast3D and is the
long-run scalability ceiling once §4.1/§4.2 are resolved — it sets the ~10–15 ms
Tier-B floor on Dam/Frigate/Control.

### 4.4 Open-level room over-admission (secondary, gameplay-coupled)

The port added default-on visibility heuristics that admit *more* rooms than the
stock portal walk — `bgApplyVisibilitySupplement` (+12 rooms/frame, `bg.c:1982`),
`bgApplyPortalProjectFrustumFallback` (`:1621`), `bgPromotePortalEdgeRescueCandidates`
(`:1501`). On open levels many terrain cells are simultaneously frustum-visible,
so these pile on extra geometry, feeding more triangles into §4.1/§4.2. Net cost
is small once §4.1 is fixed (~1 ms), and disabling them naively can *hurt*
(measured Jungle 55→79 ms) because it changes the visible set, and it is coupled
to gameplay (`getROOMID_isRendered → chr.c:5205 → auto-aim`). Treat as a careful,
late, correctness-first tuning item — **not** a perf quick-win.

---

## 5. Milestones

Each milestone: **Goal · Work · Acceptance · Expected · Risk · A/B knob.**
Sequence: **M0 → M1 → M2 → M3**, with M4/M5/M6 in parallel/after. M1 alone makes
the game playable everywhere; M0 is the guardrail that must land first.

### M0 — Standing performance harness & budgets ✅ SHIPPED *(foundation; P3)*
> **Done** (commits `perf(M0)`): `tools/perf_census.sh`, `tools/perf_budget_check.py`,
> `tools/perf_budget_smoke.sh`, opt-in `port_perf_budget_smoke` CTest lane,
> `baselines/perf_census_baseline.csv` (now the post-fix good-state), docs.

- **Goal:** performance becomes measurable and non-regressable before we optimize.
- **Work:**
  - Promote the ad-hoc census into a committed script (`tools/perf_census.sh`)
    that boots each level headless/deterministic, records mean/median/p95
    `work_ms` via `GE007_PERF_TRACE`, and emits a CSV + a human table.
  - Define per-level budgets (§6) and a `tools/perf_budget_check.py` that fails
    if any level exceeds budget by a margin.
  - Add a **`port_perf_budget` CTest lane** (opt-in like the other smoke lanes;
    reference-hardware-gated) and document it in `docs/INSTRUMENTATION.md`.
  - Capture a committed **baseline CSV** (`baselines/perf_census_baseline.csv`)
    = the §3 table, so every future change is diffed against it.
- **Acceptance:** `tools/perf_census.sh` reproduces the §3 table ±10%; the budget
  check runs green on the *post-M1/M2* build and red on today's build for Jungle.
- **Expected:** no runtime change; a durable guardrail.
- **Risk:** low. Headless GL needs a GUI session on macOS (documented caveat).
- **A/B knob:** n/a (tooling).

### M1 — Kill the per-triangle framebuffer stall ✅ SHIPPED *(Tier A; P1/P2/P5)*
> **Done** (commit `perf(M1)`, `gfx_opengl.c`): per-batch snapshot (option 1 below),
> default `perbatch` with `GE007_XLU_SNAPSHOT_MODE=pertri` escape hatch. Result:
> Jungle 18→131 fps and the other four Tier-A levels to 158–183 fps. Validated as
> in §0; adversarial review = 0 confirmed defects. Option 2 (per-pass) and option 3
> (material/level scoping) were unnecessary — per-batch already reaches the floor.

- **Goal:** remove the per-triangle serialization; keep Dam-glass visual parity.
- **Work (in priority order):**
  1. **Batch the snapshot.** In `gfx_opengl_draw_triangles`, take the framebuffer
     snapshot **once before the triangle loop** (union of the batch's screen
     bbox, or full viewport), then draw all triangles in the batch with a single
     `glDrawArrays`. Reduces copies from *N triangles* to *1 per batch* (up to
     ~1024×). Hoist the state save/restore out of the loop.
  2. **Prefer once-per-pass.** Where correctness allows, snapshot once per
     translucent pass per frame (dirty-flag re-copy only after intervening
     draws), sampled by all memory-blend batches. This approaches the measured
     7.6 ms Jungle floor.
  3. **Scope the feature (P2).** Restrict `RDP_CVG_MEMORY` routing to the
     materials/levels that visually need framebuffer-memory blending (Dam glass,
     water); let ordinary foliage use standard alpha blending, which is visually
     equivalent for those surfaces. Keep `GE007_DISABLE_ROOM_XLU_CVG_MEMORY` and
     add a per-scope override.
- **Acceptance:** Dam glass visual-regression suite (`dam_visual_regression_suite.sh`,
  glass oracle/isolation lanes) passes unchanged; Jungle/Cradle/Surface1/2/Statue
  hit their §6 budgets; no new ASan/UBSan findings; identity A/B
  (`GE007_DISABLE_ROOM_XLU_CVG_MEMORY=1`) still available.
- **Expected:** Jungle 18→~130 fps, Cradle 30→~160, Surface 2 42→~210,
  Surface 1 54→~210, Statue 83→~220. Small wins on Dam/Egypt.
- **Risk:** medium — correctness of translucent-over-translucent blending within a
  batch. Mitigation: the existing per-triangle path already only re-snapshots
  *between* triangles, and GoldenEye's XLU material runs are largely
  non-self-overlapping; validate against the glass oracle before/after.
- **A/B knob:** `GE007_DISABLE_ROOM_XLU_CVG_MEMORY` (existing);
  add `GE007_XLU_SNAPSHOT_MODE=pertri|perbatch|perpass`.

### M2 — De-pollute the CPU hot path ✅ SHIPPED (behavior-identical variant) *(Tier B floor; P1)*
> **Done** (commit `perf(M2)`, `gfx_pc.c`): rather than a broad compile-time `GFX_DIAG`
> gate (which would require enclosing every trace *reader* too, in a 1 MB shared file),
> we took the two **behavior-identical** wins that eliminate the Tier-B floor:
> (a) hoist the per-vertex glass effect-label lookup out of `gfx_sp_vertex`'s loop
> (it is constant per DL command); (b) **memoize** `gfx_diag_room_cmd_offset` — the
> O(rooms) room-attribution scan that ran per-triangle — into a 1-entry cache keyed on
> the command address, invalidated per frame. Proven **byte-identical** (0/0/0 diff).
> Result: Dam 60→137 fps, Control/Egypt/Frigate 71–76→107–139 fps.
>
> **Remaining (optional, low value now):** compile-time gating of the ~27 per-vertex
> `dbg_*` stores and the ~40 per-triangle diagnostic predicates. Post-fix profiling
> shows these are a small residual (the room-scan was the dominant cost), and gating
> the writes safely requires latching every trace consumer — deferred as hardening,
> not a perf need. See §5-notes.

- **Goal:** the per-primitive path does only rendering; dev bookkeeping is gated.
- **Work:**
  - Move all per-triangle/per-vertex diagnostics (room attribution, NDC dup,
    ~40 predicates, ~30 `dbg_*` stores, effect-label scan, `strstr`/`strcmp`,
    draw-class scan) behind a **compile-time `GFX_DIAG` build flag** *and/or* a
    single per-frame boolean latched from the `GE007_*` trace env vars, checked
    once — not per primitive.
  - Compute room attribution **once per DL segment** (segments already carry
    room identity) instead of scanning all rooms per triangle.
  - Compute NDC metrics once; pass down. Remove duplicate work in
    `gfx_sp_tri1` / `gfx_emit_loaded_triangle`.
- **Acceptance:** with diagnostics compiled out, render output is byte-identical
  to today (screenshot oracle); Tier-B residual floor drops materially; the trace
  env vars still produce identical trace output when enabled.
- **Expected:** Dam ~15→~8–10 ms, Frigate ~12→~7–8 ms, Control ~11→~7 ms, and a
  proportional drop on every level (this overhead is universal). Combined with M1,
  all 20 levels project under ~8 ms.
- **Risk:** medium — the diagnostics feed real validation lanes (room/effect
  attribution, oracle traces). Keep them fully functional under `GFX_DIAG`; the
  default build simply omits them. Verify each trace-consuming tool still works
  in a `GFX_DIAG` build.
- **A/B knob:** build-time `-DGFX_DIAG=ON` restores full instrumentation.

### M3 — Draw-call & vertex efficiency ⏸️ DEFERRED (no current-HW benefit) *(Tier B ceiling; P5, scalability)*
> **Assessed & deferred.** Post-M1/M2 profiling of the slowest level (Frigate,
> ~107 fps) shows `gfx_flush` → GL draw submission is only **~7 % of frame time**;
> the residual is the CPU DL interpreter + legitimate software T&L, not draw-call
> overhead. A persistent-VBO / indexed-drawing refactor is medium-risk (Fast3D
> depth/XLU ordering) for **≈0 benefit on current hardware** where every level
> already exceeds 100 fps. Kept as a *scalability* item for 4K / high-refresh /
> weak-GPU targets, to be revisited only if a profile shows submission-bound frames.
> The plan below stands as the implementation spec when that day comes.

- **Goal:** raise the throughput ceiling for all levels and higher resolutions.
- **Work (incremental, measure each):**
  - Replace per-flush `glBufferData` orphaning with a persistent /
    ring-buffered VBO (or `glBufferSubData` into a large streaming buffer).
  - Add indexed drawing (dedupe the 3-verts-per-tri expansion) where the N64
    vertex cache already implies reuse.
  - Coalesce redundant state changes (sort/merge batches by material within a
    pass where draw order allows; skip no-op combine/texture rebinds).
- **Acceptance:** draw-call count and CPU submit time drop on Frigate/Dam/Control
  with no visual diff; no regression on the fast levels.
- **Expected:** Tier-B floor toward ~5 ms; large headroom for 4K / high-refresh /
  min-spec GPUs.
- **Risk:** medium — Fast3D draw ordering is subtle (depth/XLU). Land behind an
  A/B and validate the full screenshot suite.
- **A/B knob:** `GE007_FAST3D_BATCHING=0` restores per-material-run draws.

### M4 — Open-level culling correctness ⏸️ DEFERRED (gameplay risk > reward) *(§4.4; correctness-first, gameplay-safe)*
> **Assessed & deferred.** Worth ~1 ms of geometry and coupled to gameplay
> (`room_rendered → auto-aim`, `chr.c:5205`); measured earlier that naive disabling
> can *hurt* (Jungle 55→79 ms by changing the visible set). With every level now
> ≥100 fps, the gameplay-invariance risk outweighs a sub-millisecond gain. Left as a
> future correctness item behind the existing `GE007_VIS_*` knobs, gated on the
> playability/route/auto-aim smoke lanes.

- **Goal:** submit only what's needed on open levels, without changing gameplay.
- **Work:** revisit the over-admission heuristics for open geometry; tune
  `bgApplyVisibilitySupplement` budget / frustum-fallback thresholds so open
  levels don't flood, *guarded by* the `room_rendered → auto-aim` invariant
  (`chr.c:5205`) — any change must pass the native-playability + campaign-route
  smoke lanes.
- **Acceptance:** Jungle/Surface room-draw count reduced; playability + route
  contract + auto-aim smoke lanes unchanged; A/B parity preserved.
- **Expected:** ~1 ms geometry saving + fewer triangles into M1/M2 paths.
- **Risk:** high (gameplay coupling). Ship last, behind existing `GE007_VIS_*`
  knobs, with the caveat that naive disabling can hurt.
- **A/B knob:** `GE007_VIS_SUPPLEMENT`, `GE007_PORTAL_*` (existing).

### M5 — Remaster scoping & dynamic resolution ⏸️ DEFERRED (remaster ≈ free) *(future-proofing; P2)*
> **Assessed & deferred.** Direct measurement refuted the earlier assumption that
> SSAA/post-FX were expensive: the whole remaster pipeline costs **~0.5–1 ms/frame**
> on the reference GPU, and `--faithful` barely moves any level. No perf action
> needed today. Dynamic resolution remains a worthwhile *min-spec feature* (not a
> fix) for a future hardware-scaling pass; spec below.

- **Goal:** keep the (currently ~free) remaster free on weaker GPUs and at 4K.
- **Work:** scope Dam-specific visual features to Dam; add optional **dynamic
  resolution scaling** that trades `RenderScale` to hold a target frame time on
  min-spec; keep `--faithful` as the zero-cost path. Low priority — measured
  remaster cost is ~0.5–1 ms on M3 Max today.
- **Acceptance:** target-FPS held on a min-spec profile; no change on reference HW.
- **Risk:** low. **A/B knob:** `Video.RenderScale`, `Video.RemasterFX`, `--faithful`.

### M6 — Documentation & governance ✅ SHIPPED *(P1–P3; "well-built for future work")*
> **Done:** `docs/RENDERING_ARCHITECTURE.md` (frame pipeline, hot-path boundary rule,
> the two cost-center case studies, the snapshot model); a **Performance discipline**
> section added to `docs/CODING_STYLE.md`; this plan's §0 results + living §6 budget
> table + the post-fix `baselines/perf_census_baseline.csv`.
- **Goal:** the discipline outlives this plan.
- **Work:**
  - A `docs/RENDERING_ARCHITECTURE.md`: the frame pipeline, the opaque/XLU
    passes, the batching model, where the hot-path boundary is, and the rule
    "nothing per-primitive that isn't rendering."
  - A short **contributor performance checklist** (in `CONTRIBUTING.md` /
    `docs/CODING_STYLE.md`): every perf-sensitive feature ships scoped +
    budgeted + A/B-gated; run `tools/perf_census.sh` before/after; the
    `port_perf_budget` lane must stay green.
  - Keep this file's §6 budget table as the living contract; update the baseline
    CSV whenever budgets change intentionally.
- **Acceptance:** docs merged; CI references the budget lane; a new contributor
  can reproduce the census from the docs alone.

---

## 6. Frame-time budgets (the contract)

Reference: Apple M3 Max, -O3 Release, default settings, deterministic spawn.
"Rip" target = 120 fps (8.3 ms). Hard floor = 60 fps (16.6 ms) on min-spec.

| Class | Budget | Rationale |
|---|---|---|
| **Target (all levels)** | ≤ 8.3 ms (120 fps) | headroom over 60 for combat/effects/min-spec |
| **Hard fail (any level)** | > 16.6 ms (60 fps) | below this is a user-visible regression |
| **Post-M1 Tier A** | ≤ 8 ms | census floor with XLU fixed |
| **Post-M2 Tier B** | ≤ 8 ms | census floor with CPU overhead gated |

Today **5 levels fail** the hard floor (jungle 18, cradle 30, surface2 42,
dam 50→ok-ish, surface1 54). M1 fixes jungle/cradle/surface1/2/statue; M2 fixes
dam/frigate/control/egypt. Projected post-M1+M2: **all 20 levels ≤ ~8 ms**.

Budgets assume the spawn view; combat/effects add load, which is why the target
is 120 not 60. A worst-case lane (walk + fire + explosions on the heavy levels)
should be added to the census in M0.

---

## 7. Risk register

| Risk | Milestone | Mitigation |
|---|---|---|
| Batched/per-pass snapshot changes translucent blending | M1 | Glass oracle + Dam visual suite before/after; keep per-tri A/B; scope to Dam-glass materials |
| Gating diagnostics breaks a validation lane | M2 | Keep full instrumentation under `-DGFX_DIAG=ON`; verify every trace-consuming tool in that build |
| Draw-call reordering changes depth/XLU order | M3 | Land behind `GE007_FAST3D_BATCHING`; full screenshot suite |
| Culling change alters auto-aim / AI visibility | M4 | Gate on `room_rendered→auto-aim` (`chr.c:5205`); playability + route + auto-aim smoke lanes; ship last |
| "It's fast on M3 Max" ≠ fast on min-spec | M5/M6 | Budgets defined per reference HW; dynamic resolution; min-spec profile in census |
| Optimization silently regresses later | all | M0 budget gate is the backstop; baseline CSV diffed every change |

---

## 8. Appendix — reproduction

Prereqs: built `build/ge007`, ROM at `baserom.u.z64`, GUI session (headless GL on
macOS still needs a window), `python3`.

**Profile the Jungle render loop (call graph):**
```sh
SDL_AUDIODRIVER=dummy GE007_MUTE=1 ./build/ge007 --level jungle --deterministic --no-input-grab &
sleep 9; sample $! 12 1 -f /tmp/jungle.sample.txt; kill $!
```

**Per-level frame time (built-in tracer):**
```sh
env SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
    GE007_NO_VSYNC=1 GE007_DETERMINISTIC_STABLE_COUNT=1 \
    GE007_AUTO_EXIT_FRAME=180 GE007_PERF_TRACE=1 GE007_PERF_TRACE_AFTER_FRAME=80 \
    ./build/ge007 --level jungle --deterministic --background --no-input-grab 2>&1 \
  | grep '^\[PERF\]'   # work_ms is the real cost; FPS = 1000/work_ms
```

**A/B the root causes:**
```sh
# §4.1 per-triangle framebuffer stall (Tier A):
GE007_DISABLE_ROOM_XLU_CVG_MEMORY=1  ...   # Jungle 55→8 ms
# Remaster (proves it is NOT the cost):
--faithful   or   --config-set Video.RenderScale=1
```

**Full census:** `tools/perf_census.sh` (to be committed in M0); iterates the 20
`--level` slugs (`dam facility runway surface1 bunker1 silo frigate surface2
bunker2 statue archives streets depot train jungle control caverns cradle aztec
egypt`) at default and `XLU-off`, emitting the §3 table.

**Relevant source anchors:**
- `src/platform/fast3d/gfx_opengl.c:1958` (`gfx_opengl_draw_triangles`, per-tri loop `:1966`), `:1740` (snapshot copy), `:1801` (`glCopyTexSubImage2D`), `:295` (default-on gate).
- `src/platform/fast3d/gfx_pc.c:5016` (RDP-memory mode classify), `:5024` (`gfx_rdp_memory_blend_class_for_draw`); hot-path overhead `gfx_sp_tri1:18732`, `gfx_emit_loaded_triangle:16329`, `gfx_sp_vertex:16144`.
- `src/game/bg.c:1982/1621/1501` (over-admission heuristics), `:16091` (visibility driver); `chr.c:5205` (auto-aim coupling).
- `src/game/fog.c:204` (Jungle fog far=2500), `src/boss.c:123` (Jungle heap budget).
