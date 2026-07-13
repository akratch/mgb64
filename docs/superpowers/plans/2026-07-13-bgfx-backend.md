# bgfx Renderer Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. This is graphics/build-integration work: the "test" for most tasks is **build success + the renderer-parity harness + tape byte-identity + ASan**, not unit tests. Each task ends with a concrete validation gate and a commit.

**Goal:** Add a third Fast3D render backend built on **bgfx**, validate it to visual parity with the OpenGL reference behind the existing `GfxRenderingAPI` vtable, ship it as an opt-in (`GE007_RENDERER=bgfx`), then flip it to the default and retire the GL + Metal backends — so MGB64 ships ONE cross-platform backend everywhere.

**Architecture:** The renderer already isolates the backend behind a ~27-function vtable (`src/platform/fast3d/gfx_rendering_api.h`) with two working implementations (`gfx_opengl.c`, `gfx_metal.mm`). We add `gfx_bgfx.cpp` implementing that same vtable against bgfx, plus a shader-generator that emits bgfx `shaderc` sources from the N64 combiner state (the shape `gfx_opengl.c` already emits as GLSL). The shared 25k-LOC `gfx_pc.c` GBI interpreter is untouched. bgfx dispatches to Metal (macOS), D3D12/Vulkan (Windows), Vulkan (Linux), and GLES (handhelds). Every step is gated so the existing GL/Metal default build stays byte-identical until we deliberately flip.

**Tech Stack:** C++17, bgfx + bx + bimg (via the `bgfx.cmake` wrapper, pinned), bgfx `shaderc` for shader cross-compilation, SDL2 (window/context handoff via `platformSetHostWindow`), CMake + CTest, the existing `tools/renderer_parity_capture.sh` / `startup_visual_parity_capture.sh` harness.

## Global Constraints

- **Default build must stay byte-identical until the deliberate flip.** All bgfx code is behind `option(MGB64_BGFX_BACKEND OFF)` and the runtime `GE007_RENDERER=bgfx` selector until Task 9. Determinism gate: `tools/fidelity/tape_regression.sh --no-build` byte-exact (the sim is backend-agnostic, so this must hold at EVERY task).
- **Warnings budget 0:** `tools/summarize_build_warnings.py --max-total 0` stays green for the shipped targets (bgfx's own warnings are fenced to its target).
- **Dependency is pinned + archive-safe:** bgfx is fetched via CMake FetchContent pinned to an exact commit + no submodule (git-archive drops submodules; the public build must contain it). Matches the pinned-dependency doctrine (MinGW SDL2, appimagetool).
- **No new default runtime deps for the GL/Metal build:** bgfx links only into the bgfx-enabled target.
- **License:** bgfx/bx/bimg are BSD-2-Clause — compatible; record provenance in `src/platform/fast3d/PROVENANCE.md`.
- **Branch:** all work on `feat/bgfx-backend` (off checkpoint `checkpoint/pre-bgfx-14c7b2d`).
- **Validation harness is the source of truth for parity**, not eyeballing: a backend is "done" for a scene when its capture matches the GL reference within the harness tolerance.

---

## Task 0: Dependency integration (bgfx builds, gated, default build untouched)

**Files:**
- Modify: `CMakeLists.txt` (add `option(MGB64_BGFX_BACKEND)` + a `FetchContent` block + a `mgb64_bgfx` interface)
- Create: `cmake/bgfx.cmake` (the FetchContent + pin, kept out of the main file for clarity)
- Modify: `src/platform/fast3d/PROVENANCE.md` (bgfx provenance + pinned commit)

**Interfaces:**
- Produces: CMake target `bgfx` (+ `bx`, `bimg`), available only when `-DMGB64_BGFX_BACKEND=ON`.

- [ ] **Step 1: Pin the dependency.** In `cmake/bgfx.cmake`, `FetchContent_Declare(bgfx.cmake GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake GIT_TAG <exact-commit>)` with `BGFX_BUILD_EXAMPLES OFF`, `BGFX_BUILD_TOOLS ON` (we need `shaderc`). Gate the whole file behind `if(MGB64_BGFX_BACKEND)`.
- [ ] **Step 2: Wire the option.** In `CMakeLists.txt`, `option(MGB64_BGFX_BACKEND "Build the bgfx render backend" OFF)` and `include(cmake/bgfx.cmake)`.
- [ ] **Step 3: Validate the DEFAULT build is unaffected.** `cmake -S . -B build && cmake --build build --target ge007 -j` — expected: builds, 0 warnings, no bgfx fetched (option OFF).
- [ ] **Step 4: Validate bgfx FETCHES + BUILDS.** `cmake -S . -B build-bgfx -DMGB64_BGFX_BACKEND=ON && cmake --build build-bgfx --target bgfx shaderc -j` — expected: bgfx + shaderc build to completion on macOS (Metal + GL backends compiled in).
- [ ] **Step 5: Commit.** `git add CMakeLists.txt cmake/bgfx.cmake src/platform/fast3d/PROVENANCE.md`

**Validation gate:** default build 0-warning green; `build-bgfx` produces `libbgfx` + `shaderc`; `tape_regression.sh --no-build` byte-exact (nothing changed in the shipped binary).

---

## Task 1: Backend skeleton + init spike (bgfx clears the MGB64 window)

**Files:**
- Create: `src/platform/fast3d/gfx_bgfx.cpp` (the vtable implementation; stubs + init + clear)
- Modify: `src/platform/fast3d/gfx_backend.c` + `.h` (add a `GFX_BACKEND_BGFX` selector value)
- Modify: `src/platform/fast3d/gfx_pc.c` (where the active `GfxRenderingAPI *` is chosen)
- Modify: `CMakeLists.txt` (compile `gfx_bgfx.cpp` + link bgfx into `ge007` only when `MGB64_BGFX_BACKEND`)

**Interfaces:**
- Produces: `struct GfxRenderingAPI gfx_bgfx_api;` (C linkage) selected when `GE007_RENDERER=bgfx` and `MGB64_BGFX_BACKEND`.
- Consumes: `platformGetHostWindow()` / the SDL window + native handle for `bgfx::PlatformData` (NSWindow layer on macOS, HWND on Windows, X11/Wayland on Linux).

- [ ] **Step 1:** Implement `gfx_bgfx_init()`: build `bgfx::Init` with `PlatformData` from the SDL window (SDL_SysWMinfo), `type = Metal` on macOS / `Count` (auto) elsewhere, `resolution` = drawable size. Call `bgfx::init`.
- [ ] **Step 2:** Implement `start_frame`/`end_frame`/`finish_render`/`on_resize`: `bgfx::setViewClear(0, CLEAR_COLOR|CLEAR_DEPTH, 0x101012ff)`, `bgfx::touch(0)`, `bgfx::frame()`, `bgfx::reset` on resize. Stub the rest of the vtable (return safe defaults; `draw_triangles` no-op).
- [ ] **Step 3:** Wire selection: `gfx_backend_use_bgfx()` (env `GE007_RENDERER=bgfx`) and hand `gfx_bgfx_api` to `gfx_pc.c` when set.
- [ ] **Step 4: Build + spike.** `cmake --build build-bgfx --target ge007 -j`; boot `GE007_RENDERER=bgfx build-bgfx/ge007 --level dam --screenshot-frame 30 --screenshot-exit` — expected: window opens, bgfx clears to the charcoal color, clean exit (no geometry yet).
- [ ] **Step 5: Determinism.** `tape_regression.sh --no-build` byte-exact (bgfx path is off in the default binary).
- [ ] **Step 6: Commit.**

**Validation gate:** bgfx-selected boot renders a cleared frame + exits 0; default build unaffected; tapes byte-exact.

---

## Task 2: Textures + samplers

**Files:** Modify `src/platform/fast3d/gfx_bgfx.cpp`.

**Interfaces:** Produces working `new_texture`/`delete_texture`/`select_texture`/`upload_texture`/`set_sampler_parameters` backed by `bgfx::TextureHandle` + a small id→handle map (RGBA8, mip 0, `SAMPLER_*` flags from cms/cmt clamp/wrap/mirror + linear/point).

- [ ] **Step 1:** id allocator + `bgfx::createTexture2D` on `upload_texture` (RGBA8, `bgfx::copy`), stored by our uint32 id.
- [ ] **Step 2:** `select_texture(tile, id)` records the handle for tile 0/1; `set_sampler_parameters` maps `cms/cmt` (G_TX_CLAMP/MIRROR/WRAP) + `linear_filter` to bgfx sampler flags.
- [ ] **Step 3: Build.** Compiles; no render change yet (shaders in Task 3 consume these).
- [ ] **Step 4: Commit.**

**Validation gate:** builds clean; texture handles created/freed without leak (bgfx debug stats show stable texture count across a boot).

---

## Task 3: Shader generator (combiner → bgfx-sc) + `draw_triangles` + `create_and_load_new_shader`

**Files:**
- Create: `src/platform/fast3d/gfx_bgfx_shader.cpp` (emit vertex+fragment bgfx-sc from `shader_id0/shader_id1`, mirroring the GLSL emitter in `gfx_opengl.c:~880-1030`)
- Create: `src/platform/fast3d/shaders/` (bgfx-sc varying.def.sc + templates, compiled by `shaderc` at build time)
- Modify: `CMakeLists.txt` (a `shaderc` build step producing embedded shader blobs)
- Modify: `src/platform/fast3d/gfx_bgfx.cpp` (`create_and_load_new_shader`/`lookup_shader`/`load_shader`/`shader_get_info`/`draw_triangles`)

**Interfaces:** Produces the full color-combiner shader path — the `buf_vbo` layout (`gfx_pc.c` interleaves pos/uv/color per `used_textures`/`num_inputs`) mapped to a bgfx `VertexLayout`; `draw_triangles` submits a transient VB with the current program + uniforms (texture(s), fog, noise).

- [ ] **Step 1:** Port the combiner→shader string builder to bgfx-sc (same inputs/mux logic as `gfx_opengl.c`), producing a vs/fs pair per shader id. Compile via `shaderc` (or runtime via `bgfx::createShader` on embedded blobs).
- [ ] **Step 2:** Build the `bgfx::VertexLayout` matching the `buf_vbo` interleave for each (num_inputs, used_textures) permutation; `draw_triangles` allocs a transient VB + submits.
- [ ] **Step 3: Build + first geometry.** `GE007_RENDERER=bgfx ... --level dam --screenshot-frame 60 --screenshot-exit` — expected: Dam renders recognizable geometry + textures.
- [ ] **Step 4: Determinism.** `tape_regression.sh` byte-exact.
- [ ] **Step 5: Commit.**

**Validation gate:** Dam renders textured geometry on bgfx; not yet pixel-parity (blend/depth follow).

---

## Task 4: Depth, viewport, scissor, blend modes → visual parity

**Files:** Modify `src/platform/fast3d/gfx_bgfx.cpp`.

**Interfaces:** Produces `set_depth_mode` (bgfx `STATE_DEPTH_TEST_*` + `WRITE_Z` + the N64 zmode/decal handling), `set_viewport`/`set_scissor` (bgfx view rect/scissor), `set_blend_mode` (map the 7 `GfxBlendMode` values to bgfx `STATE_BLEND_*`), `z_is_from_0_to_1` (true — bgfx clip is 0..1; the projection already handles this like Metal).

- [ ] **Step 1:** Implement each state setter; carry the state into the `bgfx::setState` bitfield submitted in `draw_triangles`.
- [ ] **Step 2: Parity capture.** `bash tools/renderer_parity_capture.sh` + `startup_visual_parity_capture.sh` comparing `GE007_RENDERER=bgfx` vs the GL reference on Dam/Surface1 — expected: within tolerance (translucency, decals, sky, HUD).
- [ ] **Step 3: Determinism + ASan.** tapes byte-exact; an ASan bgfx boot clean.
- [ ] **Step 4: Commit.**

**Validation gate:** parity harness green on the core scenes (Dam, Surface1, a translucency-heavy scene).

---

## Task 5: `read_framebuffer_rgb` (screenshots/oracle) + `finish_render`

**Files:** Modify `src/platform/fast3d/gfx_bgfx.cpp`.

**Interfaces:** Produces a bgfx blit-readback (`bgfx::blit` to a CPU-readable texture + `bgfx::readTexture`, 1-frame latency handled) returning GL-convention bottom-left RGB, matching the Metal path so `platformSaveScreenshot` + the parity/oracle tooling works on bgfx.

- [ ] **Step 1:** Implement the readback; wire into the existing `read_framebuffer_rgb` slot.
- [ ] **Step 2: Validate.** A `--screenshot-frame N --screenshot-exit` bgfx boot writes a correct BMP; the parity harness (which uses this) runs end-to-end on bgfx.
- [ ] **Step 3: Commit.**

**Validation gate:** bgfx screenshots are correct + drive the parity harness.

---

## Task 6: `draw_modern_mesh` (scene decor — closes AUDIT-0001 as a bonus)

**Files:** Modify `src/platform/fast3d/gfx_bgfx.cpp` (+ shader for the modern-mesh path).

**Interfaces:** Produces the optional `draw_modern_mesh` matching the Metal contract (`gfx_metal.mm mtl_draw_modern_mesh`): a resource cache keyed by `mesh->mesh_id`, MVP + the N64 fog curve, alpha-cutout two-sided pass. Fixes AUDIT-0001 (scene decor is a GL no-op) for free on the new backend.

- [ ] **Step 1:** Implement the mesh cache + draw; released on level change.
- [ ] **Step 2: Validate.** `Video.SceneDecor=1` on bgfx renders decor on Surface1; parity vs Metal decor capture.
- [ ] **Step 3: Commit.**

**Validation gate:** scene decor renders on bgfx (it renders NOWHERE on the GL default today).

---

## Task 7: Cross-platform bring-up (Windows MinGW + Linux + handheld GLES)

**Files:** Modify `CMakeLists.txt` / `cmake/bgfx.cmake` (platform data + toolchain wiring), `tools/mingw_cross_check.sh` (add a bgfx cross-compile lane).

- [ ] **Step 1:** MinGW cross-build of the bgfx backend (`bgfx.cmake` supports MinGW; wire `PlatformData` HWND). Validate `ge007.exe` links with bgfx (D3D12/Vulkan).
- [ ] **Step 2:** Linux + the handheld GLES target: confirm bgfx's GL/GLES/Vulkan backend builds for each (the handheld is the tightest constraint — see the strategy doc's open question).
- [ ] **Step 3: Commit.** Per platform as they come green.

**Validation gate:** the bgfx backend builds on all four platform toolchains; owner does the on-device render check per platform.

---

## Task 8: Owner gameplay validation (opt-in shipping)

- [ ] Ship `GE007_RENDERER=bgfx` documented (`docs/VISUAL_MODES.md`, ENV_FLAGS regen). Owner plays each platform on bgfx vs GL/Metal; log parity + perf. This is the gate before the default flip. **No code beyond docs.**

**Validation gate:** owner sign-off on macOS + Windows + handheld that bgfx matches or beats GL/Metal.

---

## Task 9: Flip default + retire GL/Metal

**Files:** Modify `gfx_backend.c` (default → bgfx), delete `gfx_opengl.c` + `gfx_metal.mm` + the GLSL/MSL forks, collapse `CMakeLists.txt`, update `docs/RENDERING_ARCHITECTURE.md` + the strategy doc.

- [ ] **Step 1:** Default `gfx_backend_use_*` to bgfx; keep GL behind `GE007_RENDERER=gl` for one release as a fallback.
- [ ] **Step 2:** After a release proving out, delete GL + Metal (~8k LOC) + the shader fork; make `MGB64_BGFX_BACKEND` non-optional.
- [ ] **Step 3: Full regression + commit.**

**Validation gate:** all parity + determinism + platform builds green with bgfx as the sole backend.

---

## Self-Review

**Spec coverage:** dependency (T0), skeleton/spike (T1), textures (T2), shaders+draw (T3), state/parity (T4), readback (T5), modern-mesh/AUDIT-0001 (T6), cross-platform (T7), owner validation (T8), flip+retire (T9) — covers the strategy doc's phased plan §6.
**Placeholder scan:** each task names exact files + a concrete validation gate; the "<exact-commit>" in T0 is filled at execution time from the pinned bgfx.cmake ref (recorded in PROVENANCE.md).
**Type consistency:** `gfx_bgfx_api` / `gfx_backend_use_bgfx()` / `GE007_RENDERER=bgfx` used consistently T1→T9; the vtable is the fixed `GfxRenderingAPI` contract throughout.
**Risk ordering:** T0–T6 are additive + gated (default build untouched, tapes byte-exact every task); T7 widens platforms; T8 is owner-gated; only T9 changes the default and deletes code.
