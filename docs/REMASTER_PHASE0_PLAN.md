# MGB64 Remaster — Phase 0 "First Light" (design spec)

**Date:** 2026-07-01 · **Branch:** `robustness/remaster-hardening` · **Status:** ✅ **SHIPPED**
(R1 `ceb71a5`, R2 `6f0b405`, R3 `9b47469`/`20e4686`/`6cae93f`, A1 `3fc3ace`, A2 `6c93c46`).
Implementation plan: `docs/superpowers/plans/2026-07-01-remaster-phase0-first-light.md`.

> **Two harness gotchas learned in build‑out (both cost real time):**
> 1. **`--ramrom <demo>` renders black headless** — use it for the deterministic sim‑hash
>    *gate*, but capture visual screenshots with **`--level <id>`** (direct boot renders
>    gameplay). Every early SSAO screenshot showed 0% change because it was a black frame.
> 2. **R3's ASLR fix is a pointer *value* window, not `-no-pie`/`MAP_FIXED`** (arm64 rejects
>    non‑PIE; malloc/mmap bases still slide). See R3 below.

Phase 0 of the remaster golden path ([REMASTER_ROADMAP.md](REMASTER_ROADMAP.md) §6).
It builds the **gameplay-invariance enforcement machine first**, then lands the first
*new* visible artifact — **SSAO** — as the first feature to flow through that machine.
SSAO is the proof that the whole **rails → foundation → feature** pipeline works
end-to-end and stays byte-identical to the faithful port when flagged off.

> **On anchors:** every `file:line` below was verified against the current
> `robustness/remaster-hardening` tree (2026-07-01). They are function-level and may
> drift a few lines; re-verify before relying on an exact number.

---

## 0. Decisions locked in brainstorming

| Decision | Choice | Why |
|---|---|---|
| **Phase 0 goal** | Shortest path to the **first visibly-different frame**, carried on a complete rails foundation | Product-first; the rails are the durable asset, SSAO is the payoff that validates them |
| **First artifact** | **SSAO** (screen-space ambient occlusion) | Roadmap's "biggest modern cue per unit effort"; screen-space ⇒ gameplay-invariant *by construction* |
| **Rails depth** | **Full P0 rails first**, then SSAO | Build the invariance machine correctly once; SSAO is its first customer |
| **R3 gate strategy** | **Raw memory hash with in-hash pointer normalization (primary)** + extended `--trace-state` as divergence localizer | Maximal coverage (catches any byte divergence, not just enumerated fields); ASLR-invariant without `-no-pie` (arm64-infeasible) |
| **Branch** | Continue on `robustness/remaster-hardening` | Inherits perf fixes (M1/M2), HD loader, `--faithful`, full validation harness |

The three governing rails from the roadmap remain non-negotiable: gameplay-invariant by
construction, copyright-bulletproof, opt-in / default-identity.

---

## 1. What the code validation changed in the roadmap

The roadmap's §6 P0 specs were written against `feat/dam-hd-remaster` and three of them
are **wrong against current code**. Corrected designs (verified) below; these corrections
must be back-ported into `REMASTER_ROADMAP.md §6` as part of Phase 0.

- **P0.3 anchors drifted.** `g_ClockTimer` clamp + RAMROM bypass now live at
  `src/game/lvl.c:2060–2083` (bypass via `get_is_ramrom_flag()` at `:2076`), not
  2042–2056. Only **two** TUs ever write `g_ClockTimer` — `lvl.c` and `src/game/front.c`
  (`:279`). The "no new TU writes it" guard is therefore trivial and low-risk.

- **P0.1 spec was too broad.** Ten `src/game/*.c` files legitimately
  `#include "gfx_pc.h"` and call the display-list *submission* API (`gfx_run_dl`, etc.):
  `bg.c`, `player.c`, `lvl.c`, `chrprop.c`, `bondview.c`, `front.c`, `explosions.c`,
  `image_bank.c`, `initexplosioncasing.c`, `chrobjhandler.c`. A blanket `gfx_*` ban would
  fail immediately. Reframe: a **denylist of render-state-*read* symbols**, submission API
  explicitly allowed. `src/game` is already clean of `GL_*`/`gl[A-Z]*`.

- **P0.2 method was N64-only.** The segment symbols (`_bssSegmentStart` = `0x8005d2e0`,
  `_bssSegmentEnd` = `0x8008e360`, `src/platform/segment_stubs.c`) are **ROM-offset
  metadata on native**, not a live buffer — you cannot hash a region between them.
  **Native game-state memory model (verified verdict B):** game state lives in a single
  contiguous **8 MB `malloc`'d arena `s_pcPool`** (`src/boss.c:238`, `PC_POOL_SIZE`,
  memset-zeroed at `:243`) plus native `.bss`/`.data` globals; structs use real 64-bit
  host pointers; `osVirtualToPhysical`/`K0_TO_PHYS` are identity no-ops
  (`src/platform/platform_os.h:507`, `include/PR/R4300.h:62–82`). No emulated guest-RAM
  buffer exists. That arena is the native analog of RDRAM and is what makes a real
  memory-hash gate feasible.

---

## 2. ACT 1 — The rails (built first)

Each rail is independently landable, flagged where runtime, and CI-wired where ROM-free.

### R1 · Static sim/render separation check

- **Deliverable:** `scripts/ci/check_sim_render_separation.sh` (new).
- **Design:** `nm` the built `src/game/*.o` objects; **fail** if a simulation TU
  references render-state-*read* symbols — `gl[A-Z]*` / `GL_*`, `gfx_opengl_*`,
  `texture_pack_*`, `g_pcTexturePack`, FBO/material-query symbols. **Allow** the
  display-list submission surface (`gfx_run_dl`, `gfx_set_draw_class`, the `gfx_pc.h` API
  the ten files already use) via an explicit allowlist. Allowlist renderer/platform TUs
  and any explicit one-way bridge declarations.
- **Build wiring:** TU groups already separated in `CMakeLists.txt`
  (`GLOB src/game/*.c` vs `src/platform/*.c` + `FAST3D_SOURCES`).
- **CI:** ROM-free → runs in `.github/workflows/ci.yml` (add a job after `linux-build`).
- **Accept:** a seeded violation on a throwaway branch fails the script; the real tree
  passes; CI runs it before/after the native build.

### R2 · Timing lock

- **Deliverable:** keep the `lvl.c` clamp (1–4 ticks) + RAMROM bypass; add a guard
  (grep- or `nm`-based, folded into R1's script or a tiny checker) asserting only
  `lvl.c` + `front.c` write `g_ClockTimer`.
- **CI:** ROM-free → CI.
- **Accept:** RAMROM replay `speedframes` unchanged (`pc_ramrom_trace_state`,
  `src/game/ramromreplay.h:42–76`); a seeded third writer of `g_ClockTimer` fails; R3
  hash unchanged by render load.

### R3 · RAMROM sim-invariance hash gate — *critical path*

- **Deliverable:** native `--sim-state-hash-out <json>` producing a deterministic hash of
  all mutable simulation state, plus an extended `--trace-state` final-state record.
- **What to hash:** the `s_pcPool` arena (`boss.c:238`) **+ a curated set of `.bss`/`.data`
  game globals** (player/chr/level/RNG/timers). **Exclude** the verified non-deterministic
  denylist: frame counters (`s_traceFrame`, `port_trace.c`), the `gfx_ptr` GBI translation
  cache (`src/platform/gfx_ptr.h`), audio-queue state, render caches, allocator bookkeeping.
- **Pointer / ASLR determinism (the core problem):** raw bytes embed host pointers that
  ASLR varies between separate process invocations, so a naive byte hash of `s_pcPool`
  differs run-to-run even when the logical state is identical. The roadmap's spec'd fix
  (`mmap(MAP_FIXED)` + `-no-pie`) is **not viable on macOS arm64** — Apple Silicon requires
  PIE (`-no-pie` is rejected/ignored), so global addresses always slide. **Primary fix
  (host-agnostic): in-hash pointer normalization.** When hashing, every pointer-aligned word
  that points into a registered state region is canonicalized to a base-independent token
  `(region_id, offset)` before it enters the hash; non-pointer words hash literally. Because
  the region set and its layout are identical across the flags-off/flags-on runs, all
  intra-state pointers normalize identically and the hash becomes ASLR-invariant *by
  construction* — no fixed load address required. (Optional hardening: a best-effort
  `mmap(MAP_FIXED)` pool pin under `GE007_SIM_HASH` shrinks the residual "data word that
  coincidentally aliases a region range" false-positive window; the memset-zeroed pool keeps
  padding deterministic.)
- **The gate:** run `ramrom_Dam_1` (alias `dam1`, `src/game/ramromreplay.c:209`)
  `--deterministic` to a fixed frame, **flags-identity vs flags-stress-ON**
  (`Video.RemasterFX=1 Video.RenderScale=4 Video.Ssao=1` + local pack if present) →
  assert **identical** hash. The JSON records include/exclude symbol sets, frame, ROM
  region, replay name, and hash.
- **Companion localizer:** extend `--trace-state` to a comprehensive final-state record so
  that when the hash trips, `tools/compare_state.py` shows *where* it diverged. Hash =
  tripwire; trace = debugger.
- **CI honesty:** the full replay needs a ROM and CI is ROM-free, so **R3's full gate is a
  documented local-preflight** in the canonical harness (§4). CI runs only a ROM-free
  hash-tool fixture test (determinism self-check on a synthetic buffer + `-no-pie`/
  `MAP_FIXED` layout self-test). This matches the existing "smokes run local, PR is the
  shared gate" posture.
- **Accept:** flags-identity vs stress-ON produce the same hash at the chosen frame; a
  seeded sim perturbation (e.g. a deliberate off-by-one in an AI tick) makes it diverge and
  `compare_state.py` localizes it.

**Open risks for the implementation plan** (call them out, don't hand-wave): macOS
`MAP_FIXED` reliability on the M3 host; confirming *all* game allocations route through
`s_pcPool` (no game state escaping to raw `malloc`); curating the global include-set
without omission (cross-checked by the trace localizer); choosing the canonical frame
(roadmap suggested 3600 — validate it's reached deterministically under `dam1`).

---

## 3. ACT 2 — The first artifact (flows through the rails)

### A1 · Sampleable depth texture (roadmap P1.1)

- **Design:** convert the scene depth **renderbuffer → `GL_DEPTH_COMPONENT24` texture** in
  `gfx_opengl_ensure_scene_target` (`src/platform/fast3d/gfx_opengl.c:2237`): globals at
  `:633–638`, non-MSAA + MSAA + resize + attachment paths. Blit MSAA→non-MSAA depth before
  the post pass. **Force the scene FBO on when SSAO is active** — at pure identity
  (`RenderScale=1`, `MSAA=0`) the scene FBO is skipped and depth would be unavailable. Feed
  `rsp.P_matrix` (`gfx_pc.c:3391`, updated `:15413`) + the backend z-convention
  (`gfx_opengl_z_is_from_0_to_1`, `:564`) to the output pass for linearization.
- **Accept:** with SSAO off, the color frame is **byte-identical** (screenshot hash +
  render-health zero); a debug depth capture at Dam frame 180 is non-blank, correctly
  near/far-ordered (foreground guardrail vs sky); R3 hash matches.

### A2 · SSAO in the output pass (roadmap P3.1 / §4 T1.2)

- **Design:** add `Video.Ssao` (default `0` for the identity-first landing) + `GE007_SSAO`
  + `Video.SsaoRadius`/`Video.SsaoIntensity`, mirroring the `Video.Bloom` plumbing
  (declare/default in `src/platform/platform_sdl.c`, extern + `settingsRegister*`, read in
  `gfx_opengl.c`). Gate via a new `gfx_opengl_output_ssao_active()` (mirror
  `gfx_opengl_output_color_adjust_active()`, `:2726`; requires `g_pcRemasterFX`).
  Implement depth-linearized hemisphere/horizon AO + blur + multiply-composite in the
  inline post-FX GLSL of `gfx_opengl_apply_output_vi_filter` (`:3126–3351`), inserted in
  the effect chain like FXAA/bloom. Bind depth to a free texture unit; pass `uProjMatrix`.
  Render-scale-normalized sampling (sample offsets scale with `gfx_current_dimensions`);
  sRGB-safe (depth is non-sRGB).
- **Accept:** identity-off byte-identical; **on** = visible contact darkening in
  corners/under props (Dam frame 180 + one interior A/B); render-health zero; **R3 hash
  identical on-vs-off** (the invariance payoff); ASan/UBSan clean on the hot draw path;
  perf within `tools/perf_census.sh` budget (M1/M2 fixes provide headroom — SSAO adds fill
  cost).

### ◆ Exit checkpoint

Evaluate the look → decide SSAO default-on-under-`RemasterFX` vs opt-in → choose the next
Phase-1 artifact (smooth env normals §4 T1.3, or sun shadow map T1.4), both of which reuse
the A1 depth foundation.

---

## 4. Sequencing, validation, and conventions

**Order:** `R1 → R2 → R3` (rails first) → `A1 → A2` → ◆. R1/R2 are cheap and land in CI
early; R3 is the meaty build and its gate then guards A1/A2.

**Canonical harness (per commit, artifacts stay out of git)** — per roadmap §7:
`SDL_AUDIODRIVER=dummy GE007_DETERMINISTIC_STABLE_COUNT=1 GE007_NO_VSYNC=1
GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 --deterministic --trace-state --screenshot-frame N`.
Baseline with identity flags, capture the variant with only the target flag changed, then
`tools/audit_screenshot_health.py` + `tools/audit_render_trace.py` +
`tools/compare_screenshots.py`. For every step, add the **R3 hash lane** (flags-off vs
flags-on, same replay, same final frame, identical hash) once R3 exists.

**Per-commit rules:** default-off byte-identical (screenshot hash + render-health); a
`GE007_*` A/B hatch **and** a `Video.*` key; screenshot A/B on Dam; ASan/UBSan on hot-draw
code; contamination guard stays green (no tracked images).

---

## 5. Doc & scope hygiene

- Back-port the §1 corrections into `REMASTER_ROADMAP.md §6` (P0.1 denylist reframe, P0.2
  native `s_pcPool`+globals design, P0.3 drifted anchors).
- The word "Phase 0" is overloaded in the docs (§2's *shipped* visual Phase 0/1 vs §6's
  *unbuilt* P0 rails). This plan is the **§6 P0 rails + the first P1/P3 artifact**; note the
  distinction where it appears.
- Out of scope for Phase 0: normal maps, per-pixel materials, shadows, texture-pack
  productionization, the heavyweight deferred/PBR territory (roadmap §8).
