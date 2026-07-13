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

## The flip (do NOT do without the validation above)

Per the release doctrine ([[mgb64-internal-external-doctrine]]: releases gated
on owner macOS+Windows gameplay checks), flipping the default is an owner
decision after playing it on each platform. The flip is intentionally a small,
reversible change:

1. Make `MGB64_WEBGPU_BACKEND` **ON by default** in `CMakeLists.txt` (so the
   shipping binary links wgpu). — This breaks the "byte-identical default"
   invariant on purpose; that invariant exists precisely to protect this moment.
2. In `gfx_backend.c`, default `gfx_backend_use_webgpu()` to **true** unless
   `GE007_RENDERER=gl` (keep `gl` as the one-release fallback).
3. Ship one release with WebGPU default + GL fallback. After it proves out,
   delete `gfx_opengl.c` + `gfx_metal.mm` + the GLSL/MSL forks (~8k LOC) and
   collapse the shader fork; make `MGB64_WEBGPU_BACKEND` non-optional.

Full regression (parity + determinism + all platforms) must be green at each
step.
