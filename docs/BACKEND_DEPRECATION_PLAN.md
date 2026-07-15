# Renderer backend deprecation plan — retiring the GL + Metal fork

**Status:** planning (no deletions have landed). WebGPU is the default backend on
every platform at HEAD; GL and Metal remain as runtime fallbacks. This document
is the durable plan for retiring them in two phases once the owner-gated
preconditions are met.

**Companion docs:** `docs/design/RENDERER_BACKEND_STRATEGY_2026-07-13.md` (why
WebGPU), `docs/design/WEBGPU_BACKEND_STATUS_2026-07-13.md` (parity status + flip
runbook), `docs/design/adr/0001-webgpu-render-backend.md` (decision record),
`docs/RELEASING.md` (owner release checklist). File:line references below are
against HEAD `822d1c9`; re-check them before executing a phase.

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
    (`gfx_backend_use_metal()`, `gfx_backend.c:55-74`, macOS-only; still selected
    by `--remaster` for SSAO/post-FX).
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
- **`--remaster` post-FX delivered on WebGPU first.** `--remaster` currently
  routes SSAO/post-FX to Metal. Metal cannot be deleted until the remaster
  post-FX path runs on WebGPU, or `--remaster` regresses. This is the one Phase-M
  engineering dependency, not just an attestation.

### Deletion scope (enumerated)

- `src/platform/fast3d/gfx_metal.mm` (3977 LOC) — delete.
- `gfx_backend.c:55-74` (`gfx_backend_use_metal()`) — delete; simplify the
  selector to WebGPU-or-GL.
- `CMakeLists.txt:19-21` (Metal comment block), `:93-95` (fallback comment),
  `:1566-1570` (Metal TU + ARC flags), `:1800` (`-framework Metal`) — remove.
- `CMakeLists.txt:1321` `port_metal_shadow_clamp_regression` (FID-0019) — remove.
  Note `CMakeLists.txt:935` `port_metal_msaa_sample_count` (FID-0018) is a pure
  helper test — **it survives** unless `gfx_msaa_util.c/.h` is Metal-only (verify
  WebGPU does not reuse it before removing).
- Env-flag rows for Metal-only flags in `docs/ENV_FLAGS.md`:
  `GE007_METAL_CAPTURE`, `GE007_METAL_DEBUG_VP`, `GE007_METAL_DUMP_SHADERS`,
  `GE007_METAL_GPU_TRACE`, `GE007_NO_METAL_MSAA`,
  `GE007_NO_METAL_SHADOW_DEPTH_CLAMP`, `GE007_NO_METAL_SHADOW_DUMMY_DEPTH`,
  `GE007_SMAA` (Metal-only), and the Metal branch of `GE007_SSAO_MODE`.

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
- Metal-pinned lanes must be retargeted to `webgpu`: `tools/perf_census.sh:71,80`,
  `tools/gpu_budget_gate.sh:58`, `tools/w1_interaction_matrix.sh:28`,
  `tools/texpack/pack_qa.sh:15,101,156`, `tools/ammo_hud_smoke.sh:175`.
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
# Metal renderer selections + Metal-only flags that must be removed/retargeted:
grep -rn 'GE007_RENDERER=metal\|RENDERER=metal' tools/ scripts/ docs/ .github/
grep -rn 'GE007_METAL\|GE007_NO_METAL\|GE007_SMAA\|Metal-only' docs/ENV_FLAGS.md docs/VISUAL_MODES.md docs/INSTRUMENTATION.md
grep -rn 'gfx_metal\|-framework Metal\|MGB64_.*METAL\|port_metal_shadow_clamp' CMakeLists.txt
grep -rln 'metal' tools/ssao_gate.sh tools/metal_shadow_clamp_regression.sh tools/metal/
```
Update: retarget `perf_census.sh`, `gpu_budget_gate.sh`, `w1_interaction_matrix.sh`,
`texpack/pack_qa.sh`, `ammo_hud_smoke.sh` to `GE007_RENDERER=webgpu`; delete
`ssao_gate.sh`, `metal_shadow_clamp_regression.sh`, `tools/metal/`; drop
`port_metal_shadow_clamp_regression` from `CMakeLists.txt`; correct
`RELEASE_NOTES.md:262`, `docs/VISUAL_MODES.md`, `docs/INSTRUMENTATION.md`,
`docs/design/remaster-aaa/**`, `FID-0019.json`, and the Metal `docs/ENV_FLAGS.md`
rows; move AUDIT-0029/0030/0031/0057 to Closed.

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
