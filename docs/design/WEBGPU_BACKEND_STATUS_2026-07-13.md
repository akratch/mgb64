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
| Output post-FX (FXAA / CAS sharpen / filmic tonemap / grade / vignette / bloom / dither) | ✅ **CLOSED 2026-07-16** |

An A/B vs the GL reference (render-scale 1, Dam) shows **near-parity**:
geometry, textures, depth ordering, lighting, and HUD match.

## Output post-FX parity gap — DISCOVERED 2026-07-16, CLOSED same day

The 2026-07-16 parity investigation (`scratchpad/sdd/task-parity-report.md`) found
a **Category-B backend defect** this doc's earlier "at or above GL parity" summary
had missed and wrongly implied closed: `wgpu_end_frame` presented the raw scene
via a bare `CopyTextureToTexture`, running **none** of the output-VI-filter chain
that GL (`gfx_opengl.c:3196+`, 39 refs) and Metal (`gfx_metal.mm:2128+` "Phase 4",
34 refs) apply. All five remaster-default post-FX — FXAA, CAS sharpen, filmic
tonemap, per-level color grade, vignette (plus default-on bloom/dither) — were
silently dropped on WebGPU.

**CLOSED** by the output post-FX pass (commit — see below): a fullscreen-triangle
uber-shader (`gfx_webgpu_postfx_wgsl`) that resolves `s_scene_tex → s_post_tex`
between scene render and present, a faithful line-for-line WGSL port of GL's
output-filter fragment shader (same FXAA kernel, CAS weights, tonemap curve, grade
math, vignette falloff, Bayer/RGB555). Gating mirrors GL exactly (`uApplyPost ==
g_pcRemasterFX`, each effect on its own `g_pc*`), so **faithful mode
(`Video.RemasterFX=0`) keeps the plain copy, byte-identical to before**. The
minimap is composited **after** the filter (not tonemapped), matching GL/Metal
ordering; readback/screenshot capture the post-FX'd frame (AUDIT-0003 contract).
SSAO is the one deliberately-omitted stage (default-off; needs a sampleable depth
target — a future enhancement, not a default-look gap).

Parity evidence (Dam frame 90, render-scale 1, GL vs WebGPU; wgpu-native runs on
Metal here so this is an API-precision floor comparable to the documented GL↔Metal
~3% tolerance):
- **Post-FX magnitude matches GL:** the faithful→remaster per-pixel delta is
  mean **15.22** on WebGPU vs **15.03** on GL (p50 17 vs 16, p99 37 vs 38) — the
  filter lands at the same strength.
- **GL-vs-WebGPU sits at the cross-API LSB precision floor:** faithful mean
  **1.14** (p50 0, 95% ≤ 8); remaster mean **1.76**, >5-diff 6.8% — equal to the
  faithful floor (6.9%), i.e. the post-FX pass adds zero divergence of its own.
  (The initial port measured 2.59 — a review fix (`81344c4`) switched FXAA's
  fractional directional taps to bilinear, matching GL's filterMode-0
  `sampleCpuBilinear`.)
- Native: full build both configs, 7/7 tapes byte-exact, aperture+screenshot
  ctests pass. Browser: `ge007_web` rebuilt, headless boot clean — Dawn (strict
  WGSL validator) accepts the pipeline, zero shader/validation errors. Perf: jungle
  holds 60 fps, per-frame work 4.4 ms (unchanged vs the plain-copy path).

## Pixel-parity gaps — CLOSED at HEAD (update 2026-07-15)

All four gaps this section originally tracked are closed on the branch tip
(verified against HEAD `822d1c9`). With the output post-FX pass above also closed
(2026-07-16), WebGPU is now genuinely at GL parity for the default remaster look
(SSAO the sole default-off residual).

1. **Minimap / radar overlay — CLOSED (`b389f0d`).** WebGPU now has a full
   overlay draw path: `gfx_webgpu_draw_minimap_overlay`
   (`src/platform/minimap_overlay.c:54,371,1952`).
2. **N64 3-point texture filter — CLOSED.** The RDP triangular filter is emitted
   for tiles requesting it: `n64Filter3(...)` in
   `src/platform/fast3d/gfx_webgpu_shader.c` (~:305,:343), replacing the plain
   bilinear fallback.
3. **Exact translucency edge cases — CLOSED (no outstanding gap).** No TODO/FIXME
   translucency markers remain in `gfx_webgpu.c` (the only residual diagnostic is
   an unsupported-windowing-system message at `:234`); the blend paths are at
   GL-parity.
4. **`draw_modern_mesh` / scene decor — CLOSED (`56406b6`, closes AUDIT-0001).**
   WebGPU renders `Video.SceneDecor`; this is a "further" enhancement beyond
   GL-parity (the GL default never rendered it), delivered on WebGPU.

## Launcher WebGPU unification — DONE (2026-07-13)

Originally the MGB64_APP launcher owned a **GL** window and the game **forced GL**
on the adopted window (`gfx_backend_force_opengl()`, `ed9eab8`), so the shipped
`.app` ran WebGPU only for direct/standalone boots. That limitation is **retired**:
the launcher app now renders **end to end on WebGPU** — launcher UI, game, and F1
overlay on one shared WebGPU device/surface.

- `AppHost` creates a Metal/native window + its own wgpu device/surface via the
  shared `gfx_webgpu_bringup()` and renders the launcher UI through `gfx_webgpu_imgui`
  (our ImGui renderer on wgpu-native v29, ImGui 1.92 dynamic-texture model).
- The game adopts the launcher's device/surface (`platformSetHostWebGpu` →
  `wgpu_init` host handoff); the adoption path no longer forces GL.
- The F1 overlay draws into a surface render pass opened by `wgpu_end_frame`.
- `GE007_RENDERER=gl` still yields a fully working GL app (runtime UI-renderer
  selection); `force_opengl` remains only as that GL fallback.
  `MGB64_WEBGPU_BACKEND=OFF` builds a GL-only app with no wgpu dependency.

Plan + validation: `docs/superpowers/plans/2026-07-13-launcher-webgpu-unification.md`
(7 tasks, all complete; launcher + Dam + overlay captured on WebGPU, GL fallback,
ctest 84/84, tapes 7/7 byte-exact, MinGW links, Docker Linux x64 builds).

**Follow-up polish (non-blocking):** live `UI.Scale` changes re-bake the ImGui
font atlas — the dynamic-texture path handles the re-upload, so this is covered;
no known launcher-path visual gaps remain.

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
runs. After a proving release, the fallbacks are retired in two phases — see
**`docs/BACKEND_DEPRECATION_PLAN.md`** for the durable plan. Key correction to the
"~8k LOC" figure: `gfx_metal.mm` (~3977 LOC) is independently deletable in
**Phase M**, but `gfx_opengl.c` (~4177 LOC) is **shared with the PortMaster GLES
build** (`#ifdef MGB64_PORTMASTER_GLES` branches) and cannot be deleted in
**Phase G** unless WebGPU is proven on real handheld hardware and PortMaster
moves off GLES. See `docs/design/adr/` for the decision record.
