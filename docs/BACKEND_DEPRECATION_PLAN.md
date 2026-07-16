# Renderer backend deprecation plan — retiring the GL + Metal fork

**Status:** planning (no deletions have landed). WebGPU is the default backend on
every platform at HEAD; GL and Metal remain as runtime fallbacks. This document
is the durable plan for retiring them in two phases once the owner-gated
preconditions are met.

**Companion docs:** `docs/design/RENDERER_BACKEND_STRATEGY_2026-07-13.md` (why
WebGPU), `docs/design/WEBGPU_BACKEND_STATUS_2026-07-13.md` (parity status + flip
runbook), `docs/design/adr/0001-webgpu-render-backend.md` (decision record),
`docs/RELEASING.md` (owner release checklist). File:line references below are
against HEAD `23bca90`; re-check them before executing a phase.

**Web build note:** the browser build (`docs/WEB.md`) depends only on the
WebGPU backend (`emdawnwebgpu` under Emscripten) — it has no GL or Metal code
path at all. Neither Phase M nor Phase G below affects it.

---

## 1. Current state

- **WebGPU is the default backend everywhere.** `MGB64_WEBGPU_BACKEND` is `ON` by
  default (`CMakeLists.txt:97`); `gfx_backend_use_webgpu()`
  (`src/platform/fast3d/gfx_backend.c:22-48`) returns true unless
  `GE007_RENDERER` selects a fallback. wgpu-native dispatches to Metal (macOS),
  D3D12 (Windows), and Vulkan (Linux/handheld) under the hood.
- **GL and Metal are runtime fallbacks, not the default:**
  - `GE007_RENDERER=gl` (or `opengl`) → OpenGL via `gfx_opengl.c`.
  - `GE007_RENDERER=metal` → native Metal via `gfx_metal.mm`
    (`gfx_backend_use_metal()`, `gfx_backend.c:55-74`, macOS-only; **still
    pinned by `--remaster`** on Apple today (`src/platform/main_pc.c:572`,
    `setenv("GE007_RENDERER", "metal", 1)`) — a holdover from when SSAO/post-FX
    were Metal-exclusive. That capability gap is now closed (WebGPU SSAO +
    post-FX landed 2026-07-16), so the pin is no longer load-bearing, but the
    code hasn't been retargeted yet; retargeting `main_pc.c:572` to
    `"webgpu"` is enumerated below as a required Phase-M deletion-scope edit,
    not a nice-to-have).
  - `-DMGB64_WEBGPU_BACKEND=OFF` builds a GL/Metal-only binary with no wgpu
    dependency (`CMakeLists.txt:94`).
- **The GLES / desktop-GL coupling (the single most important constraint).**
  `gfx_opengl.c` is **shared** between the desktop GL fallback and the
  PortMaster/handheld GLES3 build. GLES is not a separate translation unit — it
  is compiled from the *same* `gfx_opengl.c` via in-file `#ifdef
  MGB64_PORTMASTER_GLES` branches (`gfx_opengl.c:21, 893, 1013, 3067, 3084, 3836,
  3907, 3937, 4090`; `CMakeLists.txt:88` option, `:1839` GLES link branch).
  **Consequence:** deleting the desktop GL fallback does NOT free `gfx_opengl.c`
  for deletion while PortMaster ships GLES. `gfx_metal.mm` (3977 LOC, macOS-only,
  `CMakeLists.txt:1566-1570`) IS independently deletable. So the "~8k LOC" figure
  is two separable things: **Metal (~3977 LOC) deletable first; GL (~4177 LOC)
  blocked on the handheld GLES path.**

---

## 2. Phase M — Metal removal

Metal is macOS-only and structurally independent of the shared GLES file, so it
is the first and cleanest deletion.

### Preconditions

- **Owner macOS gameplay attestation on WebGPU.** The strategy doc §6 step 4-5
  (`RENDERER_BACKEND_STRATEGY_2026-07-13.md:178-181`) requires the default flip
  to be proven by "a release of the new default proving out" plus owner gameplay
  sign-off. On macOS specifically that means: WebGPU default boot played through,
  `tools/fidelity/tape_regression.sh` byte-exact, parity capture within tolerance
  (`WEBGPU_BACKEND_STATUS_2026-07-13.md:94-111`). macOS is the developed platform
  and is already marked "full game" there — the gate is a signed attestation on a
  shipped release, not new engineering.
- **`--remaster` post-FX delivered on WebGPU first. — MET (2026-07-16).** The
  remaster output post-FX (FXAA/CAS/tonemap/grade/vignette/bloom/dither) landed on
  WebGPU (commit `487a4b3`), and **SSAO — the one effect that would otherwise be
  *lost* on Metal deletion — now runs on WebGPU too** (planar v1, `gfx_webgpu_postfx_wgsl`;
  see `WEBGPU_BACKEND_STATUS_2026-07-13.md` "SSAO — CLOSED 2026-07-16"). GL's SSAO
  hangs on macOS GL-over-Metal, so before this Metal was the *only* backend with
  working SSAO; that dependency is now discharged. **No capability GL had is lost
  when `gfx_metal.mm` is deleted** — hemisphere-v2 SSAO (`Video.SsaoMode=hemisphere`)
  was always Metal-exclusive and non-default, so WebGPU's planar-v1 SSAO reaches
  full GL parity, and GL never had v2 to begin with. This was the one Phase-M
  engineering dependency; what remains is the owner attestation, not new engineering.

### Deletion scope (enumerated)

> **Rehearsal correction (2026-07-16).** A full worktree rehearsal (commit
> `8373ac6`, DO-NOT-MERGE) found the original scope below drastically
> under-counted the deletion: it named only `gfx_metal.mm`, the selector, and the
> CMake refs, but the real deletion edits **five `src/` files with ~20 call
> sites** that the grep checklist (which excludes `src/`) never surfaces. Total
> deletion is **4861 lines removed / 132 inserted** (gfx_metal.mm is 3977 of the
> removals). Line anchors below are current as of HEAD `23bca90` (verified: no
> further drift since `487a4b3`, where the rehearsal above was measured) — the
> corrected anchors are noted inline; re-check them before executing a phase.
> The scope is:

- `src/platform/fast3d/gfx_metal.mm` (3977 LOC) — delete.
- `gfx_backend.c:55-74` (`gfx_backend_use_metal()`) — delete; simplify the
  selector to WebGPU-or-GL. **Also delete its declaration in `gfx_backend.h`**
  and update the `use_opengl()`/`force_opengl()` doc comments (they reference
  `use_metal`).
- **`src/` call sites the original scope omitted (all MUST be edited or the build
  fails to link):**
  - `platform_sdl.c` (~8 sites): the `extern gfx_backend_use_metal` decl; the
    `use_metal() || use_webgpu()` screenshot/drawable-size/window-flag/metal-view
    conditions (drop the `use_metal()` disjunct → `use_webgpu()`); the
    `gfx_metal_set_vsync` block; the `Video.Smaa` advanced-gate; the
    "native Metal" window-created print. **MUST-STAY:** `g_metalView`,
    `SDL_Metal_CreateView`, `platformGetMetalLayer()`, `SDL_WINDOW_METAL` — these
    are **co-load-bearing for WebGPU on macOS** (wgpu-native renders into the same
    CAMetalLayer). Deleting them because they say "Metal" breaks WebGPU.
  - `gfx_pc.c` (~7 sites): `gfx_metal_api`/`use_metal` externs; the `gfx_init`
    rapi ternary; the depth-clamp block; the clear-color block; the
    offscreen-dim block; the GPU-verify `!use_metal()` guard.
  - `minimap_overlay.c` + `minimap_overlay.h`: the whole
    `minimap_overlay_draw_queued_frames_metal` function, the
    `gfx_metal_draw_minimap_overlay` extern, the `s_minimap_overlay_metal_backend`
    state + all its uses (its caller in the deleted `gfx_metal.mm` means the
    extern is an **undefined-symbol link error** if the function is left behind).
  - `main_pc.c` — `--remaster` does `setenv("GE007_RENDERER","metal")`
    (`main_pc.c:572` at HEAD). This **must be retargeted to `"webgpu"`**, not just
    left: as `"metal"` it only works by falling through to the WebGPU default by
    accident. This is the concrete form of the Phase-M "--remaster post-FX on
    WebGPU" precondition. Also `src/app/launch_intent.cpp` has a comment quoting
    the old `metal` pin.
- `CMakeLists.txt` — anchors drifted; at HEAD: `:19-25` (Metal comment **+
  `enable_language(OBJCXX)` / `CMAKE_OBJCXX_STANDARD` — gfx_metal.mm is the ONLY
  `.mm`/OBJCXX TU, so remove the whole block**), `:91-95` (fallback comment),
  `:1651-1657` (Metal TU + ARC flags), `:1885` (`-framework Metal` — safe to drop:
  `cmake/webgpu.cmake:85` already links Metal for wgpu-native; keep QuartzCore +
  Foundation), `:1405` `port_metal_shadow_clamp_regression`.
- `CMakeLists.txt:1024` `port_metal_msaa_sample_count` (FID-0018): **verified
  Metal-only** — `gfx_msaa_util.c/.h` is referenced ONLY by the (now-deleted)
  `gfx_metal.mm` and its test. It is now **dead code**, but it is pure/self-
  contained and **its unit test still builds and passes**, so the rehearsal
  **kept it** (removing it is optional cleanup, not required for the build). The
  original "it survives" was correct for the build; note it is now dead.
- Env-flag rows for Metal-only flags in `docs/ENV_FLAGS.md`: these are **auto-
  generated** — do NOT hand-edit; run `python3 tools/gen_env_reference.py --out
  docs/ENV_FLAGS.md`. The `GE007_METAL*`/`GE007_NO_METAL*` rows vanish with
  gfx_metal.mm; `GE007_SMAA` + `GE007_SSAO_MODE` stay (still registered in
  platform_sdl.c) but their now-inert help text must be updated at the source
  (platform_sdl.c) before regenerating.

### Audit items that close

Four of the five deferred fallback-only items are **Metal-specific** and close on
Metal deletion (each carries an explicit "MOOT on the default WebGPU path"
rationale in its issue file):

| ID | Title | Closes on Phase M |
|---|---|---|
| 0029 | Metal half-res SSAO upsample | Yes |
| 0030 | Metal no private-storage upload | Yes |
| 0031 | Metal combiner diagnostics ignore GL controls | Yes |
| 0057 | Metal shadow bias copies GL depth units | Yes |

AUDIT-0001 (GL skips scene-decor meshes) is **GL-specific**, closes in Phase G —
though it is already independently closed on the WebGPU path (`56406b6`,
`draw_modern_mesh`). AUDIT-0040 (GLES stale back-buffer) **survives both phases**
(handheld GLES).

### Test / doc fallout

- Metal-only tools become fully obsolete: `tools/ssao_gate.sh` (4×
  `GE007_RENDERER=metal`), `tools/metal_shadow_clamp_regression.sh` (2×),
  `tools/metal/README.md`.
- Metal-pinned lanes: `tools/w1_interaction_matrix.sh:28`,
  `tools/texpack/pack_qa.sh`, `tools/ammo_hud_smoke.sh` run `gl`+`metal` compare
  legs that can become `gl`+`webgpu` (a judgement call — it changes what they
  compare). **But `tools/perf_census.sh:71,80` and `tools/gpu_budget_gate.sh:58`
  CANNOT be mechanically retargeted** — their lane is `GE007_METAL_GPU_TRACE`,
  which has no WebGPU equivalent (rehearsal finding; see the deletion-day
  checklist's "NOT mechanical" note).
- Docs to correct: `docs/VISUAL_MODES.md` (SSAO-on-Metal rows),
  `docs/INSTRUMENTATION.md`, `RELEASE_NOTES.md:262`, the `docs/design/remaster-aaa/**`
  plan set (heavy Metal assumptions), and `FID-0019.json`.

### Rollback

Every deletion lands as one revertable commit (or a small stacked series).
Rollback = `git revert` of that range; the WebGPU path is unchanged by the
deletion, so a revert restores the Metal fallback without touching the default.

---

## 3. Phase G — desktop-GL fallback retirement

GL is harder: the desktop GL fallback and the handheld GLES build compile the
same `gfx_opengl.c`. This phase has **two branches** depending on the handheld
decision.

### Preconditions — two branches

- **Branch G-1 (WebGPU replaces GLES on handheld).** Requires **owner
  handheld/PortMaster WebGPU validation on real Mali/panfrost hardware** — the
  "last real unknown" (`WEBGPU_BACKEND_STATUS_2026-07-13.md:104-107`; software
  Vulkan/lavapipe passes today but does not prove driver quirks + performance).
  If WebGPU is proven on-device, PortMaster switches to WebGPU and `gfx_opengl.c`
  can be deleted **entirely** — desktop GL and GLES both go.
- **Branch G-2 (keep GLES on GL for handheld).** If the owner decides to keep the
  handheld on the GLES path (driver/perf/footprint reasons), then `gfx_opengl.c`
  **MUST stay** — only the *desktop* GL fallback selection and its desktop-only
  `#else` branches are removed. The file, the `MGB64_PORTMASTER_GLES` branches,
  and the GLES link path remain load-bearing.

The gating precondition for either branch is a shipped release with WebGPU
default proving out (§4), same as Phase M.

### What can be removed vs. what MUST stay

**Removable in both branches (desktop GL selection):**
- The `GE007_RENDERER=gl`/`opengl` desktop selection in `gfx_backend_use_webgpu()`
  (`gfx_backend.c:22-48`) and the legacy `gfx_backend_force_opengl()`
  (`gfx_backend.c:20`).
- `-DMGB64_WEBGPU_BACKEND=OFF` GL/Metal-only build path (`CMakeLists.txt:94`);
  eventually make `MGB64_WEBGPU_BACKEND` non-optional
  (`WEBGPU_BACKEND_STATUS_2026-07-13.md:129-131`).
- Desktop-only (non-`MGB64_PORTMASTER_GLES`) `#else` branches inside
  `gfx_opengl.c` — only in Branch G-1, where the whole file goes anyway; in
  Branch G-2 the desktop `#else` bodies stay because the file must still compile
  for both configs' shared code.

**MUST stay in Branch G-2 (shared-TU constraint):**
- `gfx_opengl.c` in its entirety, all `MGB64_PORTMASTER_GLES` branches
  (`gfx_opengl.c:21, 893, 1013, 3067, 3084, 3836, 3907, 3937, 4090`), the GLES
  link branch (`CMakeLists.txt:1839`), and `tools/portmaster_build_check.sh` /
  the PortMaster CI lane.
- Parity tooling pins: the parity/screenshot harness reference is GL; if GL is
  retired from desktop the parity baseline reference must be re-pinned to WebGPU
  first (do not delete the GL reference capture path until the WebGPU baseline is
  authoritative).

### Audit items that close

- **AUDIT-0001** (GL scene-decor skip) closes in Branch G-1 (GL gone) and is
  already moot on the default WebGPU path.
- **AUDIT-0040 survives** in Branch G-2 and is only closable in Branch G-1 (it is
  a genuine handheld GLES `glReadBuffer`/`GL_FRONT`-unavailable screenshot bug;
  keeping GLES keeps the bug). It needs a real ARM/GLES device screenshot harness
  regardless — see HARNESS_STRATEGY §8 E5.

### Test / doc fallout

- `docs/VISUAL_MODES.md`, `docs/ENV_FLAGS.md` (`GE007_RENDERER` row), and any
  script defaulting to `RENDERER=gl` update to WebGPU-only or handheld-only
  framing.
- README / RELEASE_NOTES renderer wording moves from "OpenGL default, WebGPU opt"
  (already stale) to "WebGPU only; GLES retained for handheld" (G-2) or "WebGPU
  everywhere" (G-1).

---

## 4. Phase timeline

The order is a hard invariant, from the strategy doc §6
(`RENDERER_BACKEND_STRATEGY_2026-07-13.md:169-182`):

1. **A proving release ships FIRST with GL + Metal fallbacks intact.** WebGPU is
   the default in that release, but the fallbacks stay compiled in and selectable
   (`GE007_RENDERER=gl|metal`, `MGB64_WEBGPU_BACKEND=OFF`) so any field regression
   has an escape hatch. WebGPU is not in ANY release yet (still on unpushed
   `feat/webgpu-backend`), so no deletion can precede it.
2. **Phase M deletions land in the release *after* the proving release** — once
   macOS gameplay is attested and `--remaster` post-FX is on WebGPU.
3. **Phase G deletions land later still**, gated on the handheld decision (G-1
   real-hardware validation, or G-2 explicit keep-GLES ruling).

Never delete a fallback in the same release that first ships the new default.

---

## 5. Deletion-day checklists

Run these greps at deletion time; the reference set drifts, so re-grep rather
than trusting this list.

### Phase M (Metal) deletion-day

```sh
# 1. LINK-CRITICAL: src/ call sites + symbols. The original checklist OMITTED
#    src/ entirely (it only grepped tools/scripts/docs/.github). This is the set
#    that must ALL be edited or the build fails to link — grep it FIRST:
grep -rn 'gfx_backend_use_metal\|gfx_metal_api\|gfx_metal_\|GE007_RENDERER.*metal\|metal_backend' src/
#    Expected live-symbol hits to clear: gfx_backend.c/.h, gfx_pc.c,
#    platform_sdl.c, minimap_overlay.c/.h, main_pc.c. After the edit, the ONLY
#    residual 'gfx_metal' hits in src/ are documentary comments (gfx_webgpu.c /
#    gfx_opengl.c port-attribution, PROVENANCE.md, gfx_msaa_util.h) — those are
#    accurate history and STAY. Confirm zero live refs with:
grep -rn 'gfx_backend_use_metal\|gfx_metal_api' src/    # must be empty post-edit
#    DO NOT delete the SDL Metal-view infra (g_metalView / SDL_Metal_CreateView /
#    platformGetMetalLayer / SDL_WINDOW_METAL) — WebGPU on macOS needs it.

# 2. Tools/docs selections + Metal-only flags:
grep -rn 'GE007_RENDERER=metal\|RENDERER=metal' tools/ scripts/ docs/ .github/
grep -rn 'GE007_METAL\|GE007_NO_METAL\|GE007_SMAA\|Metal-only' docs/ENV_FLAGS.md docs/VISUAL_MODES.md docs/INSTRUMENTATION.md
grep -rn 'gfx_metal\|-framework Metal\|MGB64_.*METAL\|port_metal_shadow_clamp\|enable_language(OBJCXX' CMakeLists.txt
grep -rln 'metal' tools/ssao_gate.sh tools/metal_shadow_clamp_regression.sh tools/metal/
```
Delete `ssao_gate.sh`, `metal_shadow_clamp_regression.sh`, `tools/metal/`; drop
`port_metal_shadow_clamp_regression` + `enable_language(OBJCXX)` from
`CMakeLists.txt`; correct `RELEASE_NOTES.md`, `docs/VISUAL_MODES.md`,
`docs/RELEASING.md`, `docs/INSTRUMENTATION.md`; move AUDIT-0029/0030/0031/0057
to Closed.

**GATING unit tests (these go RED mid-deletion — the original checklist did not
flag them; all three are in the ROM-free `ctest -E port_` set):**
- `env_reference_current` → run `python3 tools/gen_env_reference.py --out
  docs/ENV_FLAGS.md` after updating the inert Metal/SMAA/SSAO help text in
  `platform_sdl.c`.
- `fidelity_ledger_valid` → `FID-0019.json` has evidence
  `ctest:port_metal_shadow_clamp_regression` (now an invalid path). Transition it
  to **`waived`** — and a waived entry REQUIRES a `waiver` object with both
  `reason` and `retest` (validator: "waived without waiver.retest"), plus a
  history entry `from:"fix-in-progress" to:"waived"`.
- `fidelity_ledger_index_current` → after the FID-0019 status change, run
  `python3 tools/fidelity/ledger.py render` to regenerate `docs/fidelity/LEDGER.md`.

**NOT mechanical — keep OUT of the deletion commit (rehearsal findings):**
- `perf_census.sh` / `gpu_budget_gate.sh` cannot be `s/metal/webgpu/`-retargeted:
  their lane is `GE007_METAL_GPU_TRACE`, a Metal-backend-only instrumentation flag
  with **no WebGPU equivalent**. A naive swap yields a silently-inert lane; a real
  WebGPU GPU-timing hook is separate work. (`w1_interaction_matrix.sh`,
  `ammo_hud_smoke.sh`, `texpack/pack_qa.sh` run `gl`+`metal` compare lanes — those
  CAN become `gl`+`webgpu`, but that changes what they compare and is a judgement
  call, not a rename.)
- `docs/design/remaster-aaa/**`, `docs/design/*.md`, `RENDERER_SIM_AUDIT_*` carry
  ~30+ structural Metal references across archival planning/design docs. These are
  a large editorial revision, not a mechanical edit, and should be a **separate
  doc-hygiene pass** — do not let them bloat or gate the one-commit deletion.

### Phase G (desktop-GL) deletion-day

```sh
# GL fallback selection + the OFF build path + the shared-TU guards to preserve:
grep -rn 'GE007_RENDERER=gl\|=opengl\|force_opengl\|use_opengl' src/ tools/ scripts/ docs/
grep -rn 'MGB64_WEBGPU_BACKEND=OFF\|MGB64_WEBGPU_BACKEND OFF' docs/ tools/ tests/ scripts/
grep -rn 'MGB64_PORTMASTER_GLES' CMakeLists.txt src/platform/fast3d/gfx_opengl.c   # MUST-STAY in Branch G-2
```
`MGB64_WEBGPU_BACKEND=OFF` currently appears in `docs/VISUAL_MODES.md`,
`docs/design/SESSION_HANDOFF_2026-07-13.md`,
`docs/design/WEBGPU_BACKEND_STATUS_2026-07-13.md`, and
`docs/design/adr/0001-webgpu-render-backend.md` — update each. In Branch G-2, do
NOT touch the `MGB64_PORTMASTER_GLES` branches or delete `gfx_opengl.c`; only the
desktop GL selection and the OFF build go. Re-pin the parity baseline to WebGPU
before removing the GL reference capture path. Move AUDIT-0001 (and, in G-1 only,
AUDIT-0040) to Closed; AUDIT-0040 stays Open in G-2.
