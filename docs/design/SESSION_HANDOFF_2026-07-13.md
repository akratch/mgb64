# Session handoff — 2026-07-13 (WebGPU backend changeover → back to the Codex audit)

Internal doc (`docs/design/**` is export-ignored). No ROM-derived data here.

This session did one large thing — **converged the render backend on WebGPU and
flipped it to the default** — and then stopped. This document records that work
and hands the next session **back to the work that was in flight before it: the
Codex native-port audit (known bugs).**

---

## Part 1 — What this session did: the WebGPU backend changeover

**Goal:** replace the drifting GL + Metal render-backend fork with one
cross-platform backend, reach parity, and make it the default.

**Outcome: DONE and validated.** WebGPU (wgpu-native) is now the default render
backend. Branch `feat/webgpu-backend`, ~17 commits. GL and Metal are retained as
runtime fallbacks.

### State of the changeover
- **Selection:** default (unset) → WebGPU. `GE007_RENDERER=gl` → OpenGL,
  `=metal` → native Metal (still used by `--remaster` for post-FX/SSAO).
- **Build:** `MGB64_WEBGPU_BACKEND` defaults **ON**; `-DMGB64_WEBGPU_BACKEND=OFF`
  builds a GL/Metal-only binary with no wgpu dependency.
- **Sim-invariant:** `tools/fidelity/tape_regression.sh` runs on the
  WebGPU-default binary and is byte-exact (same 7 hashes as GL). The renderer
  never touches gameplay determinism.
- **Perf parity:** Dam 300-frame headless run: GL 5.38 s vs WebGPU 5.24 s.
- **Tests:** full ctest suite **84/84** green.
- **Cross-platform (Docker + MinGW):** macOS full; Windows MinGW `ge007.exe`
  links; Linux x86_64 **and** aarch64 (PortMaster arch) — `ge007` builds and the
  `webgpu_spike` (init → runtime WGSL → offscreen render → readback) **passes**
  under Mesa software Vulkan (llvmpipe); the Mali `panfrost` Vulkan ICD real
  handhelds use is present in the image.
- **Code-reviewed:** an adversarial review found 10 issues; the 7 that matter are
  fixed (pipeline-cache-overflow leak, unclamped viewport → potential black
  frame, scissor negative-extent order, per-draw bind-group cache, sampler leak,
  WGSL-emitter snprintf guard, attribute bounds guard).

### Where it lives
- Code: `src/platform/fast3d/gfx_webgpu.c` + `gfx_webgpu_shader.c/.h`;
  `cmake/webgpu.cmake`; the surface accessor `platformWebGpuWindowInfo` in
  `platform_sdl.c`; the selector in `gfx_backend.c/.h` (`gfx_backend_use_opengl/
  _webgpu`).
- Decision record: **`docs/design/adr/0001-webgpu-render-backend.md`**.
- Status + flip runbook + platform matrix: `docs/design/WEBGPU_BACKEND_STATUS_2026-07-13.md`.
- Plan: `docs/superpowers/plans/2026-07-13-webgpu-backend.md`.
- Memory: `[[mgb64-webgpu-backend-transition]]`.
- Debug: `GE007_WEBGPU_DUMP_FRAME=<n>` → `/tmp/webgpu_frame_<n>.ppm`.
- Docker images kept for re-verification: `mgb64-webgpu-linux` (arm64),
  `mgb64-webgpu-linux-x64` (amd64).

### Late fix (2026-07-13, commit `ed9eab8`) — launcher→Play black screen
The default flip first broke the **launcher→Play** flow: the MGB64_APP launcher
owned a GL window and handed it to the game, but WebGPU can't render into a GL
window → "backend inert" → black frame (audio/sim ran). The stopgap was
`gfx_backend_force_opengl()` in the app-shell adoption path (the launcher ran
**GL**). **This is now superseded** by the launcher WebGPU unification below —
`force_opengl` remains only as the `GE007_RENDERER=gl` fallback.

### Launcher WebGPU unification — DONE (2026-07-13, commits `0b59264`..`acb8c80`)
The launcher app now renders **end to end on WebGPU** by default: the launcher
UI, the game, and the F1 overlay all run on one shared WebGPU device/surface.
`AppHost` creates a Metal/native window + its own wgpu device/surface (via the
shared `gfx_webgpu_bringup`), renders the launcher UI through an in-house ImGui
renderer (`gfx_webgpu_imgui`, ImGui 1.92 dynamic-texture model), and hands the
device/surface to the game (`platformSetHostWebGpu` → `gfx_webgpu.c` adopts it,
no `force_opengl`). The F1 overlay draws into a surface render pass opened by
`wgpu_end_frame`. `GE007_RENDERER=gl` still gives a fully working GL app
(runtime UI-renderer selection); `MGB64_WEBGPU_BACKEND=OFF` builds a GL-only app.
Validated: launcher + Dam + F1 overlay captured on WebGPU; GL fallback; ctest
84/84; tapes 7/7 byte-exact; MinGW `ge007.exe` links; Docker Linux x64 builds.
Plan: `docs/superpowers/plans/2026-07-13-launcher-webgpu-unification.md` (all
7 tasks complete).

### What is NOT done (owner-gated, do not do unilaterally)
1. **Owner gameplay validation** on a real Windows box and the actual PortMaster
   device. Software Vulkan proves the code path, not real-driver quirks or
   on-device framerate. This is the release-doctrine gate ([[mgb64-internal-external-doctrine]]).
2. **Launcher WebGPU unification — DONE** (see the section above). The launcher
   app renders end to end on WebGPU; `force_opengl` is now only the
   `GE007_RENDERER=gl` fallback.
3. **After the launcher unification (now done) proves out on real hardware:**
   delete `gfx_opengl.c` + `gfx_metal.mm` + the
   GLSL/MSL forks (~8k LOC), collapse the shader fork, make
   `MGB64_WEBGPU_BACKEND` non-optional. (Sequenced in the ADR.)
4. **Minor parity polish** (tracked in the status doc): mipmap generation for
   decor textures; the residual ~3.7% Dam A/B is GL's anisotropic filtering,
   which WebGPU can't do without mipmaps (an API nuance, not a bug); the rare
   sun-shadow/dfdx/diag shader-option effects.

---

## Part 2 — Return here: the Codex native-port audit (the prior work)

Before the backend work, we were **triaging and fixing the Codex audit** — a
73-finding native-port defect ledger, `docs/audit/issues/AUDIT-0001..0073`,
ranked in **`docs/audit/PRIORITIZATION.md`** (the authoritative queue).

### Progress so far
- **AUDIT-0001** (scene-decor GL no-op) — **closed by the WebGPU backend** this
  session (WebGPU renders decor GL cannot; `draw_modern_mesh`).
- **Do-now batch** (commit `6c1a23d`, 12 findings): AUDIT-0003, 0004, 0010, 0016,
  0018, 0021, 0023, 0032, 0044, 0045, 0048, 0073.
- **AUDIT-0013** (ledger index omits records) — the fidelity ledger index was
  regenerated this session (`ledger.py render`, 129 findings); **verify whether
  that fully closes AUDIT-0013** or only the index-currency aspect.
- Everything else in the ledger is **open**.

### The recommended fix sequence (from PRIORITIZATION.md — resume here)
1. **Trust the gates first** — the CI / exit-status swallowers: **AUDIT-0012,
   0035, 0043, 0046, 0015**. Until these are honest you can't tell whether any
   other pipeline fix took. *(0043 auto-screenshot-write-failure-still-exits-0 is
   partially adjacent to work already done — check its current state first.)*
2. **Release integrity** — the two S1 ship-blockers **AUDIT-0037** (unpinned
   mutable download in Linux packaging) and **AUDIT-0052** (release assets not
   bound to the verified commit), then 0053/0058/0059/0070.
3. **Save durability** — AUDIT-0033/0034/0054/0055/0041 (progress loss > most
   gameplay bugs).
4. **Malformed-input crashes** — AUDIT-0005/0002/0006/0027/0042/0071/0072
   (harden the ROM/asset trust boundary; several are one guarded branch each).
5. **The rest of P1**, then the P2 correctness backlog by theme, then P3 polish.

### How to work the audit
- Each finding is a self-contained report under `docs/audit/issues/AUDIT-XXXX`
  with severity/priority, evidence, and source anchors — those reports are
  authoritative; PRIORITIZATION.md is the reading/order.
- `docs/audit/README.md` describes the ledger + workflow.
- Prefer the same rigor used for the do-now batch: fix, keep the default build
  byte-identical where possible, run `tape_regression.sh` (determinism) + the
  relevant ctest, and note the AUDIT-id in the commit.

### Gotchas carried forward
- The **default backend is now WebGPU** — any audit work touching the renderer,
  screenshots, or window/GL assumptions must remember `GE007_RENDERER=gl` is now
  opt-in, not the default. The `gfx_backend_use_opengl()` predicate is the guard
  for GL-context-only code.
- The `ci_local` "Ignored-artifact hygiene" gate FAILS by design on any dev tree
  (local ROM/decor/baselines are gitignored) — benign, a fresh clone passes
  ([[mgb64-prerelease-hardening-2026-07-12]]).
- Mute audio + headless for test boots: `SDL_AUDIODRIVER=dummy GE007_MUTE=1`,
  `--no-ui`, `GE007_BACKGROUND=1` ([[mgb64-test-run-etiquette]]).
