# Renderer Backend Strategy — one modern backend everywhere

**Date:** 2026-07-13
**Question:** which cross-platform graphics framework should MGB64 converge on so we
ship *one* backend on every platform (no GL+Metal fork), how heavy is the lift, and
what does it unlock for the AAA remaster?

## TL;DR

- We are **not** on Metal-everywhere. Today OpenGL is the default/primary backend on
  **every** platform (Windows, Linux, macOS, handhelds); Metal is a **macOS-only,
  opt-in** path (`GE007_RENDERER=metal`, auto-selected by `--remaster`) that exists
  only to route around Apple's *deprecated* GL-over-Metal translator, which GPU-hangs
  on SSAO.
- The renderer is **structurally ready** for a clean backend swap: a thin ~27-entry
  vtable (`GfxRenderingAPI`), two proven backends (~4k LOC each), **vertex+fragment
  shaders only** (no compute/geometry to reimplement), and an existing
  **renderer-parity regression harness** to validate a new backend against the GL
  reference byte-for-byte.
- **Recommendation: bgfx** as the pragmatic "one backend everywhere" (proven on all
  our platforms incl. ARM handhelds, one shader source cross-compiled to every API,
  solo-maintainable), with **WebGPU (Dawn)** as the most-modern alternative if we want
  a native-Metal/D3D12/Vulkan mapping and a compute-first future and can absorb the
  heavier dependency. Raw **Vulkan+MoltenVK** is ruled out as too heavy for the reward;
  **ANGLE** is ruled out because it doesn't modernize.
- **Lift:** implement ONE new backend behind the existing vtable (~3–5k LOC + a shader
  emitter port + build integration). Phased and de-risked by the parity harness:
  add-alongside → validate → flip default → retire GL+Metal. Weeks of focused work,
  not a multi-month rewrite — because the N64→draw-call layer (25k LOC in `gfx_pc.c`)
  is untouched.

---

## 1. Where we actually are

`src/platform/fast3d/gfx_backend.c` is the entire decision:

```c
bool gfx_backend_use_metal(void) {
#ifdef __APPLE__
    return getenv("GE007_RENDERER") == "metal";   // opt-in only
#else
    return false;                                  // everyone else: OpenGL
#endif
}
```

| Platform | Default | Notes |
| --- | --- | --- |
| Windows / Linux | OpenGL (native GL 3.3) | Runs the full effect stack incl. SSAO directly. |
| macOS | OpenGL (Apple GL 4.1, *deprecated*, runs on an internal GL-over-Metal translator) | SSAO/heavy post-FX **hang** the translator. |
| macOS `--remaster` | native Metal | Built *specifically* to run SSAO without the hang. |
| Handheld (PortMaster) | OpenGL ES 3.2 | Same `gfx_opengl.c`, GLES shader dialect. |

So Metal was **added** as a macOS specialist, not a migration. We maintain two backends
(`gfx_opengl.c` 4177 LOC, `gfx_metal.mm` 3977 LOC) that must be kept at feature parity —
and they already drift (scene decor renders on Metal but is a no-op on GL: AUDIT-0001;
combiner diagnostics diverge: AUDIT-0031). That drift is the tax we want to stop paying.

**Why we can't just "go Metal everywhere":** Metal is Apple-only. Windows, Linux, and
ARM handhelds have no Metal. Converging requires a framework that is genuinely
cross-platform.

## 2. Why the swap is bounded (the good news)

The renderer is Emill's **Fast3D** (n64-fast3d-engine, modified BSD-2-Clause) — the same
lineage as libultraship/Ship-of-Harkinian, which already run GL/D3D11/Metal behind the
same interface. That interface is doing its job here:

- **Thin backend contract.** `GfxRenderingAPI` (`gfx_rendering_api.h`) is ~27 function
  pointers: shader create/load/lookup, texture upload/select/sampler, depth/viewport/
  scissor/blend, `draw_triangles`, `read_framebuffer_rgb`, frame start/end, and the
  optional `draw_modern_mesh`. A backend is "implement these."
- **Shared work stays shared.** The 25k-LOC `gfx_pc.c` (N64 GBI display-list interpreter
  → backend-neutral draw calls + shader IDs) is **untouched** by a backend swap.
- **Clean feature surface.** The backends use **only vertex + fragment shaders** —
  no compute, geometry, or tessellation. So a new backend reimplements a small,
  well-understood shape. (And compute becomes pure *upside*, see §5.)
- **Shaders are generated, per backend.** `gfx_opengl.c` string-builds GLSL (already in
  three dialects: `150` for macOS core, `320 es` for handhelds, `330 core` for desktop)
  from the combiner state; `gfx_metal.mm` builds MSL. A new backend needs its own
  emitter for the *same* combiner logic — mechanical, not novel.
- **A parity safety net already exists.** `tools/renderer_parity_capture.sh`,
  `startup_visual_parity_capture.sh`, `glass_route_parity_regression.sh`,
  `metal_shadow_clamp_regression.sh`, plus `sim_invariance_gate.sh`. A new backend is
  validated against the GL reference exactly the way Metal was ("GL byte-identical").

This is the difference between "swap the backend" and "rewrite the renderer." It's the
former.

## 3. Candidate frameworks

### bgfx  — *recommended (pragmatic)*
A mature cross-platform rendering library that itself abstracts D3D11/12, GL/GLES,
Vulkan, and Metal, and selects the best native backend per platform.
- **Covers every platform we ship** including ARM/GLES handhelds, with one integration.
- **One shader source**: bgfx `shaderc` compiles a GLSL-ish source to every backend via
  SPIRV-Cross — so the combiner emitter is written *once*.
- Proven in shipping games; C/C++ friendly; single well-understood dependency;
  solo-maintainable.
- Trade-off: you adopt bgfx's resource/model conventions (a clean mapping from our
  vtable, but more than a literal drop-in), and you're at bgfx's abstraction ceiling
  (fine for our needs).

### WebGPU via Dawn  — *most-modern alternative*
Write one WebGPU backend; Dawn (Google's production C++ impl, ships in Chrome) maps it to
**Metal on macOS/iOS (native — no Apple-GL hang), D3D12 on Windows, Vulkan on Linux/
Android**, with a GL fallback.
- The genuine "write once, run native everywhere" modern standard (Apple/Google/MS/Mozilla
  backed). Compute-first. Best long-term future (incl. web/mobile).
- WGSL (or SPIR-V) shaders — a new emitter target.
- Trade-off: Dawn is a **heavy dependency** (GN/CMake build integration), and native
  desktop WebGPU is younger than GL/Vulkan. More integration risk for a solo maintainer.
  (wgpu-native, Mozilla's Rust impl with a C ABI, is the alternative WebGPU provider but
  adds a Rust toolchain.)

### Vulkan (+ MoltenVK on Apple)  — *ruled out*
One low-level API: native on Windows/Linux/Android, translated to Metal on Apple by
MoltenVK (Khronos-official and well-maintained — unlike Apple's dead GL).
- Best control/ceiling and the industry AAA standard.
- But Vulkan is by far the **highest-effort** API (swapchains, descriptor sets, pipeline
  objects, explicit sync/memory — a Vulkan backend is typically 2–3× a GL/Metal one), and
  we still ship a translation layer on Apple. For an N64 remaster's needs that's a poor
  effort/reward vs. bgfx/WebGPU (both of which get us Metal-native or Vulkan under the hood
  anyway).

### ANGLE (GLES → native)  — *ruled out*
Run the existing GLES code on ANGLE, which translates to Metal/D3D/Vulkan.
- Near-zero new code; escapes Apple's dead GL on Mac.
- But it keeps us on **GLES (old API, no modern compute/features)**, adds *another*
  translation layer, and doesn't unify (still native GL elsewhere). It fixes the Apple
  symptom without delivering the modern, single-backend goal.

## 4. Recommendation & the lift

**Pick bgfx** unless we specifically want to bet on WebGPU's modern-standard future and can
take on Dawn. Both satisfy "same backend everywhere"; bgfx is the lower-risk,
faster-to-ship, handheld-proven choice for a solo maintainer marching to a remaster,
while WebGPU/Dawn is the more future-proof, compute-native, heavier bet.

**Effort (either):** medium-large, **weeks not months**, because it's one backend against
a proven vtable with a parity harness — not a renderer rewrite.
1. Stand up the new backend implementing `GfxRenderingAPI` (~3–5k LOC), + build
   integration (bgfx amalgamated build, or Dawn).
2. Port the combiner shader generator to the new shader source (the bounded, novel part).
3. Bring up frame/texture/blend/depth to pass `renderer_parity_capture.sh` +
   `startup_visual_parity_capture.sh` against the GL reference.
4. Reach + prove parity on the visual-regression suite (glass, shadows, intro, decor).

## 5. What it unlocks for the AAA remaster

This is the strategic payoff — it's not just deduplication:

- **Compute shaders on every platform.** We're stuck at GL 4.1 on Mac with *no* compute,
  which is why SSAO hangs the translator. A modern backend gives compute everywhere →
  proper **GTAO/HBAO+**, bloom, **SSR**, **TAA**, motion blur, DOF, and correct HDR
  tonemapping — the actual AAA post-FX bar — done once, running everywhere.
- **Every remaster effect is written once and works everywhere.** This permanently kills
  the "feature only on one backend" class (AUDIT-0001 scene decor, AUDIT-0031 combiner
  diagnostics) and halves the per-effect authoring cost.
- **One shader codebase** (no GLSL+MSL divergence to keep in sync).
- **Modern render targets / MRT / storage buffers** → the door to deferred/clustered
  lighting, better shadow maps, and screen-space effects the current fixed-ish GL path
  can't reach cleanly.
- **No deprecated-API risk.** Apple GL 4.1 is on borrowed time; Metal-native (via bgfx or
  WebGPU) removes that overhang, and the handheld/GLES path is served by the same source.
- **Future platforms for free-ish** (web via WebGPU, Steam Deck native Vulkan, mobile).

## 6. Phased plan (de-risked)

1. **Spike (small):** stand up the chosen backend rendering one triangle + one textured
   quad in the MGB64 window via `platformSetHostWindow`; confirm build integration on all
   three desktop OSes + the handheld toolchain.
2. **Backend bring-up:** implement the full vtable; port the combiner emitter; pass the
   parity captures on Dam/Surface.
3. **Add-alongside:** ship it behind `GE007_RENDERER=<new>` next to GL+Metal (same pattern
   Metal uses today) so it's validated in the real app with zero default-risk.
4. **Flip default** once the visual-regression suite is green on all platforms + owner
   gameplay sign-off on each (macOS, Windows, handheld).
5. **Retire GL + Metal** after a release of the new default proving out, deleting ~8k LOC
   of forked backend + collapsing the shader fork.
6. **Then build the AAA post-FX on compute** — the reason we did this — once, everywhere.

## 7b. CRITICAL FINDING (2026-07-13, during bgfx Task 0) — bgfx is the WRONG fit

Fast3D **generates and compiles shaders at RUNTIME**, one per N64 color-combiner ID:
`gfx_opengl_create_and_load_new_shader` string-builds GLSL and `glCompileShader`s it
live; `mtl_create_and_load_new_shader` builds MSL and `newLibraryWithSource`s it live.
The shader pool is **dynamic and unbounded** (`gfx_opengl.c` reallocs it; "EXCEEDS old
fixed pool of 256").

**bgfx has no runtime shader compilation** — it loads only *offline* `shaderc`-compiled
binaries. Making bgfx work would require pre-compiling the entire (dynamic, 256+)
combiner-permutation set offline and embedding it — fragile (a missed permutation = a
missing draw; the remaster's modern meshes add more) and it bolts a capture/codegen step
onto the build. That is a **heavy, awkward swap** — which fails the owner's "only if bgfx
isn't a heavy swap" condition.

**Revised recommendation: WebGPU (via Dawn or wgpu-native).** WebGPU supports **runtime**
shader-module creation (`wgpuDeviceCreateShaderModule` from WGSL/SPIR-V at draw-time),
which matches Fast3D's dynamic combiner model exactly — the same way GL/Metal compile
live today — while still giving the single cross-platform backend (native Metal on macOS,
D3D12 on Windows, Vulkan on Linux, GL/Vulkan on handhelds) and compute for the AAA post-FX.
This makes WebGPU both the *most modern* and the *correct-fit* choice; bgfx's only edge
(maturity/simplicity) is outweighed by the shader-model mismatch. The `2026-07-13-bgfx-backend`
plan is superseded; the WebGPU plan is `2026-07-13-webgpu-backend.md`.

### 7c. SPIKE VALIDATED (2026-07-13) — WebGPU proven viable end-to-end

The de-risking spike (`tests/test_webgpu_spike.c`, ctest `webgpu_spike`, gated behind
`MGB64_WEBGPU_BACKEND`) **passes headlessly**, converting the two big unknowns into facts:

- **Dependency + platform:** the pinned, SHA-256-verified **wgpu-native v29.0.1.1** prebuilt
  links and initializes; on this host WebGPU brings up **backend = Metal** (Apple M3 Max) —
  native, no Apple-GL translation-layer hang, no deprecated API.
- **Runtime shaders (the decisive capability):** a WGSL shader **compiles at runtime**
  (`wgpuDeviceCreateShaderModule`) — exactly what Fast3D's dynamic combiner path needs and
  what bgfx could not do.
- **Full pipeline correctness:** a triangle renders to an offscreen RGBA8 target; a
  texture→buffer copy + map reads the pixels back; the center is the green triangle
  `(38,217,89)` and the corner is the clear color `(13,13,18)` — both asserted.
- **Handheld unknown closed:** wgpu-native ships prebuilts for macOS (arm64/x64), Windows
  (x64-MinGW), Linux x64 **and linux-aarch64** — the PortMaster/ARM target is covered by an
  existing, pinned binary (device-run confirmation still owner-side, but the lib exists).

The default `ge007` build is byte-identical (no wgpu symbols; 7 tapes byte-exact). **Verdict:
proceed with the full migration.** Effort/risk are now measured, not guessed.

## 7. Open questions for the owner

- **bgfx vs WebGPU/Dawn:** ship-it-pragmatic (bgfx) vs most-modern-future (WebGPU). This
  doc recommends bgfx; the decision is a taste/risk call on dependency weight and how much
  the "modern standard" future matters.
- **Handheld reality check:** confirm the chosen framework's backend on the specific
  PortMaster targets (bgfx GLES/Vulkan; Dawn Vulkan/GL) before committing — the handheld is
  the tightest constraint.
- **Sequence vs. AUDIT-0001:** the GL scene-decor fix is still worth doing now (it makes the
  showcase content visible on today's default backend for whoever's on GL when the
  migration lands); it is not wasted if we later converge, because the migration replaces
  the backend wholesale.
