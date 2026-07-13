# ADR-0001: Converge the render backend on WebGPU (wgpu-native)

- **Status:** Accepted — implemented; default flipped to WebGPU (2026-07-13, owner-authorized). GL/Metal retained one release as fallback.
- **Date:** 2026-07-13
- **Deciders:** owner (akratch) + engineering
- **Supersedes / relates to:** `docs/design/RENDERER_BACKEND_STRATEGY_2026-07-13.md` (analysis), `docs/design/WEBGPU_BACKEND_STATUS_2026-07-13.md` (status + runbook), `docs/superpowers/plans/2026-07-13-webgpu-backend.md` (execution plan).

## Context

MGB64 shipped **two** render backends behind Fast3D's thin ~23-function
`GfxRenderingAPI` vtable:

- `gfx_opengl.c` (~4.2k LOC) — the default on every platform (Win/Linux/macOS/handheld).
- `gfx_metal.mm` (~4.0k LOC) — macOS-only opt-in (`GE007_RENDERER=metal`, auto
  under `--remaster`), added solely to dodge Apple's deprecated GL-over-Metal
  translator hanging on SSAO's view-position reconstruction.

Two hand-maintained backends **drift**. A concrete symptom: scene decor
(`G_MODERNMESH`) renders on Metal but is a silent no-op on the GL default
(AUDIT-0001). Every rendering feature must be written, debugged, and kept in
sync twice; the shader logic exists as parallel GLSL and MSL forks. The owner's
goal — *"ship the same backend everywhere, no fragmentation"* — is impossible
with a forked GL+Metal design, and adding a third hand-written backend
(D3D12/Vulkan for Windows/Linux) would make it worse.

**The hard constraint that shaped the decision:** Fast3D generates and compiles
its shaders **at runtime**, one per N64 color-combiner id, in `gfx_pc.c`'s GBI
interpreter (`glCompileShader` on GL, `newLibraryWithSource` on Metal). The
shader set is dynamic and unbounded; it is not known ahead of time. Any
replacement backend **must compile shaders at runtime**.

## Decision

Replace the GL+Metal fork with a **single cross-platform backend built on
WebGPU via wgpu-native**, implemented as `gfx_webgpu.c` behind the existing
`GfxRenderingAPI` seam, with a WGSL shader emitter that generates combiner
shaders at runtime (`wgpuDeviceCreateShaderModule`). wgpu-native dispatches to
the platform's native API under the hood: **Metal** (macOS), **D3D12/Vulkan**
(Windows), **Vulkan** (Linux and the ARM handheld). One backend, one shader
language (WGSL), native performance everywhere.

The default backend is **WebGPU** (`GE007_RENDERER` unset → WebGPU), with
OpenGL (`GE007_RENDERER=gl`) and Metal (`GE007_RENDERER=metal`) retained as
runtime-selectable fallbacks for one release. The build option
`MGB64_WEBGPU_BACKEND` defaults ON (set OFF for a GL/Metal-only binary with no
wgpu dependency).

## Options considered

| Option | Runtime shaders? | Cross-platform | Verdict |
|--------|------------------|----------------|---------|
| **Keep GL+Metal fork** | yes | no (2 backends, drift) | rejected — the status quo we are fixing |
| **bgfx** | **NO** (offline `shaderc` only) | yes | **rejected — fatal mismatch** |
| **Vulkan (hand-written)** | yes | partial (no macOS without MoltenVK; verbose) | rejected — huge surface, still needs a Metal path |
| **ANGLE (GL→native)** | yes | yes | rejected — GL-flavored, doesn't unlock modern GPU features; still GL semantics |
| **WebGPU / Dawn** | yes | yes | viable; heavier C++ build, Google-centric, no prebuilt ARM-handheld binaries |
| **WebGPU / wgpu-native** | **yes** (`wgpuDeviceCreateShaderModule`) | **yes** (incl. `linux-aarch64` prebuilt) | **chosen** |

The decisive finding was that **bgfx is architecturally incompatible**: bgfx
consumes only offline-precompiled shader blobs (`shaderc`); it has no runtime
shader-compilation path. Fast3D's whole model is runtime per-combiner shaders,
so bgfx was ruled out after a spike, not on preference. (The owner had
tentatively preferred bgfx "if not a super-heavy swap"; the incompatibility
made it not merely heavy but impossible.)

wgpu-native was chosen over Dawn because it ships **pinned, SHA-verifiable
prebuilt binaries for every target including `linux-aarch64`** (the PortMaster
handheld — the one platform with no easy fallback), links as a single static
lib with a stable C ABI, and has a lighter integration than building Dawn's C++
tree.

## Consequences

**Positive**
- One backend, one shader fork (WGSL), everywhere — no more GL/Metal drift.
- Modern GPU substrate for the remaster roadmap (compute, proper offscreen
  passes) without a GL-over-Metal translator.
- WebGPU renders scene decor (AUDIT-0001) that the GL default cannot.
- Performance at parity with GL on macOS (300-frame Dam run: GL 5.38 s vs
  WebGPU 5.24 s, both 60 Hz-locked).

**Costs / risks accepted**
- The shipping binary now links wgpu-native (~ per-platform prebuilt). This
  **ends the "byte-identical default build" invariant** — that invariant existed
  precisely to keep the migration reversible up to this flip.
- WebGPU cannot replicate GL's anisotropic-without-mipmaps filtering (WebGPU
  requires mipmaps for anisotropy; the N64 is single-level, so mipmaps would
  themselves diverge). Residual A/B on Dam is ~3.7 %/channel, an API nuance not
  a structural gap.
- One more pinned third-party dependency to track (mitigated: fetched-not-vendored,
  SHA-256-verified in `cmake/webgpu.cmake`, MIT/Apache-2.0).

**Migration safety (how we de-risked the flip)**
- All WebGPU code was gated behind `MGB64_WEBGPU_BACKEND` + `GE007_RENDERER`
  during development; the default build stayed byte-identical and all 7
  determinism tapes byte-exact at **every** task.
- The backend is **sim-invariant**: after the flip, `tape_regression.sh` runs on
  the WebGPU-default binary and reproduces the *same* sim-state hashes as the GL
  baseline (the renderer never touches gameplay determinism).
- Cross-platform proven: macOS (full render + all backends), Windows (MinGW
  `ge007.exe` links), Linux x86_64 + aarch64 (the WebGPU spike — init, runtime
  WGSL compile, offscreen render, readback — passes under Vulkan/llvmpipe; the
  Mali/panfrost Vulkan ICD real handhelds use is present).

**Reversibility**
- `GE007_RENDERER=gl` restores OpenGL; `=metal` restores Metal. Building with
  `-DMGB64_WEBGPU_BACKEND=OFF` produces the pre-flip GL/Metal binary.

## Follow-ups

1. Owner gameplay validation on Windows + the real PortMaster device (per the
   release doctrine) before a public release ships WebGPU-default.
2. ~~Launcher runs GL, only direct boots use WebGPU~~ **DONE (2026-07-13):**
   the launcher app now renders end to end on WebGPU (launcher UI + game + F1
   overlay on one shared device/surface); `force_opengl` is only the
   `GE007_RENDERER=gl` fallback. `AppHost` builds its own device/surface via
   `gfx_webgpu_bringup`, the game adopts it (`platformSetHostWebGpu`), and the
   overlay renders through `gfx_webgpu_imgui` (our ImGui-on-wgpu-native-v29
   renderer, ImGui 1.92 dynamic-texture model). Plan + validation:
   `docs/superpowers/plans/2026-07-13-launcher-webgpu-unification.md`.
3. After a proving release: delete `gfx_opengl.c` + `gfx_metal.mm` + the
   GLSL/MSL forks (~8k LOC), collapse the shader fork, make
   `MGB64_WEBGPU_BACKEND` non-optional. (Now unblocked — no GL-only app path
   remains except the deliberate `GE007_RENDERER=gl` fallback.)
4. Minor parity polish (tracked in the status doc): mipmap generation for decor
   textures; the rare sun-shadow/dfdx/diag shader-option effects.
