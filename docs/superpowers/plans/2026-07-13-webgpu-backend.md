# WebGPU Renderer Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Graphics/build work: the "test" for most tasks is **build + the renderer-parity harness + tape byte-identity + ASan + the `webgpu_spike` ctest**, not unit tests. Each task ends with a concrete validation gate and a commit. Steps use `- [ ]`.

**Goal:** Add a single cross-platform render backend on **WebGPU (wgpu-native)** behind the existing Fast3D `GfxRenderingAPI` vtable, validate it to visual parity with the OpenGL reference, ship it opt-in (`GE007_RENDERER=webgpu`), then make it the default and retire the GL + Metal backends — one backend everywhere.

**Architecture:** `gfx_webgpu.c` implements the ~27-fn `GfxRenderingAPI` (the same seam `gfx_opengl.c` / `gfx_metal.mm` fill) against the standard `webgpu.h` C API. A WGSL emitter generates vertex+fragment shaders **at runtime** from the N64 combiner state (`shader_id0/shader_id1`) — the shape `gfx_opengl.c` already emits as GLSL — and `wgpuDeviceCreateShaderModule` compiles them live (validated in the spike). wgpu-native dispatches to Metal (macOS), D3D12/Vulkan (Windows), Vulkan (Linux), Vulkan/GL (handheld). The shared 25k-LOC `gfx_pc.c` GBI interpreter is untouched.

**Tech Stack:** C11 (backend), WGSL (runtime shaders), wgpu-native v29.0.1.1 prebuilt (pinned, SHA-256-verified via `cmake/webgpu.cmake`), SDL2 (window handle → `WGPUSurface`), CMake + CTest, `tools/renderer_parity_capture.sh` / `startup_visual_parity_capture.sh`.

## Global Constraints

- **Default build byte-identical until the deliberate flip (Task 8).** All WebGPU code behind `MGB64_WEBGPU_BACKEND` + runtime `GE007_RENDERER=webgpu`. Determinism gate `tools/fidelity/tape_regression.sh --no-build` byte-exact at EVERY task (the sim is backend-agnostic).
- **Warnings budget 0** for shipped targets; **ASan-clean** WebGPU boots.
- **Dependency stays pinned + verified** (`cmake/webgpu.cmake`, v29.0.1.1 + SHA-256). Archive-safe (fetched, not committed).
- **Parity harness is the source of truth**, not eyeballing.
- **Branch:** `feat/webgpu-backend` (off `checkpoint/pre-bgfx-14c7b2d`).

## Status

- [x] **Task 0 — Dependency + spike (DONE, `8cf3aaa`/`80f2924`).** wgpu-native integrated + gated; `test_webgpu_spike` validates init (Metal), runtime WGSL compile, and an offscreen triangle readback. Handheld covered by the linux-aarch64 prebuilt.

---

## Task 1: Backend skeleton + surface + clear (renders into the MGB64 window) — DONE

**Files:** Create `src/platform/fast3d/gfx_webgpu.c`; modify `gfx_backend.c`/`.h` (a `GE007_RENDERER=webgpu` selector `gfx_backend_use_webgpu()`), `gfx_pc.c` (select `gfx_webgpu_api`), `CMakeLists.txt` (compile + link `webgpu` into `ge007` only when `MGB64_WEBGPU_BACKEND`).

**Interfaces:** Produces `struct GfxRenderingAPI gfx_webgpu_api;` selected when `GE007_RENDERER=webgpu`. Consumes the SDL window native handle for `WGPUSurface` (metal layer via `SDL_Metal_CreateView`/`SDL_SysWMinfo` on macOS, HWND on Windows, X11/Wayland on Linux — the `wgpu.h` `wgpuInstanceCreateSurface` + platform `WGPUSurfaceSourceMetalLayer`/`...WindowsHWND`/`...XlibWindow`).

- [x] Init: instance/adapter/device (spike code), `WGPUSurface` from the SDL window, configure the swapchain (`wgpuSurfaceConfigure`, BGRA8/preferred format, size = `gfx_current_dimensions`, lazy-(re)configure in `start_frame`).
- [x] `start_frame`/`end_frame`: acquire the surface texture, a render pass that clears to the charcoal color, submit, `wgpuSurfacePresent`. `on_resize` forces reconfigure. Rest stubbed (safe: shader cache returns stable non-NULL programs, textures return ids, state/draw are no-ops).
- [x] **Cross-backend guard fix (`gfx_backend_use_opengl()`):** GL-context-only paths in the shared interpreter were gated on `!gfx_backend_use_metal()`, so a WebGPU session (also no GL context) fell into `glGetIntegerv`/`SDL_GL_SwapWindow`/`minimap_overlay_draw_queued_frames`/`glReadPixels` and crashed. Added `gfx_backend_use_opengl()` (= `!metal && !webgpu`) in `gfx_backend.c/.h` and routed those sites through it. **Also fixed a C fall-through:** a bare `extern` declaration placed directly after an `else` (from the `#ifdef __APPLE__` block) is not a valid `else` body — it silently made the following metal/opengl branches unconditional. Hoisted all `extern`s above the `if/else` chain in the `max_offscreen_dim` selector.
- [x] **Validate:** `GE007_RENDERER=webgpu build-webgpu/ge007 --level dam --no-ui` boots to a cleared frame + stable game loop + clean exit (no crash, no wgpu validation errors); default `build/ge007` has **zero** real-WebGPU symbols (only the always-false `gfx_backend_use_webgpu` stub); `tape_regression.sh --no-build` byte-exact (7/7); `webgpu_spike` green. (ASan-clean boot deferred — wgpu-native is a non-instrumented prebuilt; will run a dedicated ASan pass at a later task.)
- [x] Commit.

## Task 2: Textures + samplers

**Files:** `gfx_webgpu.c`.
**Interfaces:** `new_texture`/`delete_texture`/`select_texture`/`upload_texture`/`set_sampler_parameters` → `WGPUTexture`+`WGPUSampler` (RGBA8, `wgpuQueueWriteTexture`, clamp/mirror/wrap + linear/point from cms/cmt).

- [ ] Id→texture/view/sampler map; upload via `wgpuQueueWriteTexture`; sampler cache keyed by (filter,cms,cmt).
- [ ] **Validate:** builds; texture count stable across a boot (no leak). Commit.

## Task 3: WGSL combiner emitter + `draw_triangles` + shader create/lookup

**Files:** Create `src/platform/fast3d/gfx_webgpu_shader.c` (emit vs+fs WGSL from `shader_id0/shader_id1`, mirroring `gfx_opengl.c:~880-1030`); modify `gfx_webgpu.c` (`create_and_load_new_shader`/`lookup_shader`/`load_shader`/`shader_get_info`/`draw_triangles`).
**Interfaces:** Runtime `wgpuDeviceCreateShaderModule` per combiner id (spike-proven), a `WGPURenderPipeline` cache keyed by (shader_id, blend, depth) since WebGPU bakes state into pipelines, a `WGPUVertexLayout` matching the `buf_vbo` interleave, bind group(s) for texture(s)+uniforms; `draw_triangles` uploads a transient VB + submits.

- [ ] Port the combiner→shader builder to WGSL (same mux logic as GL). Map the fixed `buf_vbo` layout to a `WGPUVertexBufferLayout`. Uniforms (fog, noise, matrices) via a uniform buffer + bind group.
- [ ] **Validate:** Dam renders recognizable textured geometry on `GE007_RENDERER=webgpu`; tapes byte-exact; commit.

## Task 4: Depth / viewport / scissor / blend → visual parity

**Files:** `gfx_webgpu.c`.
**Interfaces:** `set_depth_mode` (depth-stencil state + N64 zmode/decal), `set_viewport`/`set_scissor` (`wgpuRenderPassEncoderSetViewport/SetScissorRect`), `set_blend_mode` (map the 7 `GfxBlendMode` → `WGPUBlendState`), `z_is_from_0_to_1` → true (WebGPU clip 0..1, like Metal). Note: WebGPU bakes blend+depth into the pipeline, so these feed the pipeline cache key (Task 3).

- [ ] Implement; feed into the pipeline key + render-pass state.
- [ ] **Validate:** `renderer_parity_capture.sh` + `startup_visual_parity_capture.sh` (webgpu vs GL) within tolerance on Dam, Surface1, and a translucency-heavy scene. ASan clean; tapes byte-exact; commit.

## Task 5: `read_framebuffer_rgb` + `finish_render`

**Files:** `gfx_webgpu.c`.
**Interfaces:** texture→buffer copy + map readback (spike-proven), GL-convention bottom-left RGB, matching the Metal path so `platformSaveScreenshot` + the parity/oracle tooling work on WebGPU.

- [ ] Implement; wire into the slot.
- [ ] **Validate:** a `--screenshot-frame N --screenshot-exit` webgpu boot writes a correct BMP; the parity harness runs end-to-end on webgpu. Commit.

## Task 6: `draw_modern_mesh` (scene decor — closes AUDIT-0001)

**Files:** `gfx_webgpu.c` (+ WGSL for the modern-mesh path).
**Interfaces:** the optional `draw_modern_mesh` matching the Metal contract (cache keyed by `mesh->mesh_id`, MVP + N64 fog curve, alpha-cutout two-sided). Renders scene decor — which is a no-op on the GL default today (AUDIT-0001).

- [ ] Implement + release on level change.
- [ ] **Validate:** `Video.SceneDecor=1` renders decor on Surface1 (webgpu); parity vs Metal decor. Commit.

## Task 7: Cross-platform bring-up (Windows MinGW + Linux + handheld)

**Files:** `CMakeLists.txt`/`cmake/webgpu.cmake` (surface source per platform), `tools/mingw_cross_check.sh` (webgpu lane).

- [ ] MinGW: link the windows-x64-gnu prebuilt; `WGPUSurfaceSourceWindowsHWND`. Validate `ge007.exe` links + the spike cross-builds.
- [ ] Linux + handheld (linux-aarch64 prebuilt): confirm the backend builds; owner device-run of the spike + a boot on the actual PortMaster target (the one remaining unknown).
- [ ] Commit per platform as green.

## Task 8: Owner gameplay validation → flip default → retire GL/Metal

**Files:** docs (`VISUAL_MODES.md`, ENV_FLAGS regen); then `gfx_backend.c` default; then delete `gfx_opengl.c` + `gfx_metal.mm` + the GLSL/MSL forks (~8k LOC) after a proving release.

- [ ] Ship `GE007_RENDERER=webgpu` documented; owner plays each platform webgpu vs GL/Metal, logs parity + perf.
- [ ] On sign-off: flip the default to webgpu (keep `GE007_RENDERER=gl` one release as fallback); after a release, delete GL+Metal + collapse the shader fork; make `MGB64_WEBGPU_BACKEND` non-optional.
- [ ] Full regression green (parity + determinism + all platforms). Commit.

---

## Self-Review

**Spec coverage:** dependency+spike (T0 done), skeleton/surface (T1), textures (T2), WGSL emitter+draw (T3), state/parity (T4), readback (T5), modern-mesh/AUDIT-0001 (T6), cross-platform+handheld (T7), owner-validate+flip+retire (T8) — covers the strategy doc §6.
**Placeholder scan:** each task names exact files + a concrete validation gate; the surface-creation platform structs (WGPUSurfaceSource*) are read from `wgpu.h` at execution time.
**Type consistency:** `gfx_webgpu_api` / `gfx_backend_use_webgpu()` / `GE007_RENDERER=webgpu` used consistently T1→T8; the vtable is the fixed `GfxRenderingAPI` contract throughout.
**Risk ordering:** T1–T6 additive + gated (default byte-identical, tapes byte-exact every task); T7 widens platforms; only T8 flips the default + deletes code, gated on owner sign-off.
