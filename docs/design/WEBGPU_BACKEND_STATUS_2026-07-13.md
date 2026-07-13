# WebGPU backend — status & owner flip runbook (2026-07-13)

The cross-platform **WebGPU (wgpu-native)** render backend that replaces the
GL + Metal fork with one backend everywhere. This is the status of the
transition and the exact steps for the owner to validate and flip the default.

See also: `docs/design/RENDERER_BACKEND_STRATEGY_2026-07-13.md` (why WebGPU),
`docs/superpowers/plans/2026-07-13-webgpu-backend.md` (task-by-task plan),
`docs/VISUAL_MODES.md` (`GE007_RENDERER=webgpu`).

## What it is

`src/platform/fast3d/gfx_webgpu.c` (+ `gfx_webgpu_shader.c`) implement the same
~23-function Fast3D `GfxRenderingAPI` seam as `gfx_opengl.c` / `gfx_metal.mm`,
against the standard `webgpu.h` C API. A WGSL emitter generates the N64
combiner shaders at runtime (the capability that ruled out bgfx). wgpu-native
dispatches to Metal / D3D12 / Vulkan under the hood. Everything is gated behind
the CMake option `MGB64_WEBGPU_BACKEND` (OFF by default) and the runtime
selector `GE007_RENDERER=webgpu`, so **the shipping `ge007` links no wgpu
symbols and stays byte-identical** until the deliberate flip.

## Done (committed, validated on macOS)

| Area | State |
|------|-------|
| Bring-up: instance/adapter/device/surface, offscreen scene target, present | ✅ |
| Textures + samplers (RGBA8, clamp/mirror/wrap, id recycling) | ✅ no leak |
| WGSL combiner emitter + draw path (pipelines, bind groups, vertex layout) | ✅ **renders Dam** |
| Depth buffer + viewport + scissor (correct 3D + letterbox) | ✅ |
| `read_framebuffer_rgb` — screenshot/parity/oracle tooling works on WebGPU | ✅ |
| Exact blend modes + shader-side UV clamp + N64 tile-mask | ✅ |
| Cross-platform surface (Win32/X11/Wayland) | ✅ code; **MinGW `ge007.exe` links** |
| Isolation: default binary byte-identical, 7/7 determinism tapes byte-exact | ✅ every task |

An A/B vs the GL reference (render-scale 1, Dam) shows **near-parity**:
geometry, textures, depth ordering, lighting, and HUD match.

## Remaining before pixel-parity (tracked)

1. **Minimap / radar overlay** — `minimap_overlay.c` draws it via a per-backend
   path (GL direct calls; Metal has its own pipeline). WebGPU needs an
   equivalent overlay draw path. Currently absent on WebGPU (the one clear A/B
   gap).
2. **N64 3-point texture filter** — surfaces with `SHADER_OPT_TEXELn_N64_FILTER`
   currently sample with the standard bilinear sampler; the exact 3-point filter
   (needs the `uN64FilterScale` uniform) is not yet ported. Subtle.
3. **Exact translucency edge cases** — the coverage / RDP-memory blend variants
   approximate to alpha; the framebuffer-sampling shader paths are not ported.
4. **`draw_modern_mesh`** (Task 6) — scene decor (`Video.SceneDecor`). Optional;
   the GL default doesn't render it either (AUDIT-0001), so not needed for
   GL-parity — it would be a "further" enhancement.

## Cross-platform verification done (2026-07-13, Docker + MinGW)

| Platform | Build | Runtime WebGPU path |
|----------|-------|---------------------|
| **macOS (arm64)** | ✅ full game | ✅ renders Dam + decor, all 3 backends, determinism byte-exact, default→WebGPU |
| **Windows (MinGW x86_64)** | ✅ `ge007.exe` links (windows-gnu prebuilt + d3d12/dxgi libs) | ⏳ owner run |
| **Linux x86_64** | ✅ spike (linux-x86_64 prebuilt) | ✅ **spike PASS under Vulkan/llvmpipe** (init + runtime WGSL + offscreen readback) |
| **Linux aarch64 (PortMaster arch)** | ✅ `ge007` compiles+links (linux-aarch64 prebuilt) | ✅ **spike PASS under Vulkan/llvmpipe**; Mali `panfrost_icd` present for real HW |

The Linux runs use a container with Mesa's software Vulkan (lavapipe/llvmpipe)
under Xvfb — no GPU needed to prove the code path. Real handhelds use the Mali
`panfrost` Vulkan driver (ICD present in the base image). The one thing software
Vulkan can't prove is real-hardware driver quirks + performance → the owner
device-run below.

## Owner validation checklist (per platform)

Build: `cmake -B build-webgpu -DMGB64_WEBGPU_BACKEND=ON . && cmake --build build-webgpu --target ge007`

1. **macOS** — `GE007_RENDERER=webgpu build-webgpu/ge007 --level dam` and play.
   Compare vs `build/ge007 --level dam` (GL). Check: geometry, textures,
   translucency (glass/water), the minimap, HUD, weapon, performance.
2. **Windows** — build with `cmake/mingw-w64-x86_64.cmake` +
   `-DMGB64_WEBGPU_BACKEND=ON` (or MSVC), run `ge007.exe` with
   `GE007_RENDERER=webgpu`. (Compile+link already proven; run is the unknown.)
3. **Linux + PortMaster handheld** — build with `-DMGB64_WEBGPU_BACKEND=ON`
   (linux-x86_64 / linux-aarch64 prebuilt auto-selected), run with
   `GE007_RENDERER=webgpu`. The handheld Vulkan/GLES path is the last real
   unknown.

For each: `tools/fidelity/tape_regression.sh` must stay byte-exact (the sim is
backend-agnostic — the backend must never affect gameplay), and the parity
capture harness should be within tolerance.

## The flip — DONE (owner-authorized 2026-07-13)

The default is now WebGPU, done as a small, reversible change:

1. `MGB64_WEBGPU_BACKEND` is **ON by default** in `CMakeLists.txt` (the shipping
   binary links wgpu). `-DMGB64_WEBGPU_BACKEND=OFF` builds a GL/Metal-only binary.
2. `gfx_backend.c` `gfx_backend_use_webgpu()` defaults to **true** unless
   `GE007_RENDERER` is `gl`/`opengl` (OpenGL fallback) or `metal` (native Metal,
   still used by `--remaster`).
3. **Validated on macOS:** default boot → WebGPU (M3 Max); `GE007_RENDERER=gl` →
   OpenGL; `GE007_RENDERER=metal` → Metal. **The determinism gate runs on the
   WebGPU-default binary and is byte-exact (7/7 tapes, identical hashes to the GL
   baseline) — the flip is sim-invariant.**

Remaining (owner cross-platform validation): Windows/Linux/PortMaster gameplay
runs. After a proving release, delete `gfx_opengl.c` + `gfx_metal.mm` + the
GLSL/MSL forks (~8k LOC), collapse the shader fork, and make
`MGB64_WEBGPU_BACKEND` non-optional. See `docs/design/adr/` for the decision
record.
