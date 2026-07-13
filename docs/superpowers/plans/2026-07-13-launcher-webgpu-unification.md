# Launcher ↔ game WebGPU unification — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Graphics work: the "test" for most tasks is **build + the MGB64_APP_AUTOPLAY / MGB64_APP_SMOKE frame-capture + tape determinism + ctest**, not unit tests. Each task ends with a concrete validation gate + a commit. Steps use `- [ ]`.

**Goal:** Make the MGB64 launcher app render end-to-end on WebGPU (launcher UI + game + F1 overlay on one shared WebGPU device/surface), retiring the `force_opengl()` workaround so "one backend everywhere" holds for the shipped app.

**Architecture:** `AppHost` owns the WebGPU instance/device/queue/surface, renders the launcher UI via a new in-house `gfx_webgpu_imgui` renderer, and hands the device+surface to the game (`platformSetHostWebGpu`); `gfx_webgpu.c` adopts them; the F1 overlay renders through the same renderer into the shared surface before one present.

**Tech Stack:** C11 (`gfx_webgpu_imgui.c`), C++17 (AppHost/overlay), WGSL, wgpu-native v29.0.1.1, ImGui 1.92.9 (vendored; `imgui_impl_sdl2` kept for input), SDL2, CMake.

## Global Constraints

- **Spec:** `docs/superpowers/specs/2026-07-13-launcher-webgpu-unification-design.md`.
- **Determinism untouched:** `tools/fidelity/tape_regression.sh --no-build` byte-exact (7/7) after every task — the renderer never affects gameplay.
- **Standalone `--level` boots unchanged** (WebGPU, engine-owned window) until/after each task.
- **`GE007_RENDERER=gl` must still yield a working GL app** (the `force_opengl` safety valve stays for a GL host window).
- **Our own renderer, not upstream imgui_impl_wgpu** (spike: it is version-skewed from v29). C, matching `gfx_webgpu.c` patterns + v29 API exactly.
- **Warnings budget 0** for the app + engine targets; **ASan-clean** app boots.

---

## Task 1: `gfx_webgpu_imgui` — ImGui renderer on WebGPU v29

**Files:**
- Create: `src/platform/fast3d/gfx_webgpu_imgui.h` (C-callable interface)
- Create: `src/platform/fast3d/gfx_webgpu_imgui.c`
- Modify: `CMakeLists.txt` (add to `FAST3D_SOURCES` under `MGB64_WEBGPU_BACKEND`)

**Interfaces (Produces):**
```c
/* gfx_webgpu_imgui.h — C interface, callable from the C++ app + C overlay. */
#include <stdbool.h>
#include <stdint.h>
struct ImDrawData;                 /* opaque here; the .c includes cimgui/imgui via C++ or uses the C ABI */
bool gfx_webgpu_imgui_init(void *device, void *queue, int surface_format);
void gfx_webgpu_imgui_new_frame(void);              /* rebuilds font texture if dirty */
/* Render draw_data into an already-open render pass encoder. */
void gfx_webgpu_imgui_render(struct ImDrawData *draw_data, void *render_pass_encoder,
                             int fb_width, int fb_height);
void gfx_webgpu_imgui_shutdown(void);
```

**Implementation notes (mirror `imgui_impl_wgpu` logic, v29 API + `gfx_webgpu.c` patterns):**
- One WGSL module: vertex applies an ortho MVP (uniform) to `ImDrawVert` (pos vec2 @0, uv vec2 @8, col unorm8x4 @16, stride 20); fragment samples the bound texture × vertex color, alpha-blended.
- Font atlas: `ImGui::GetIO().Fonts->GetTexDataAsRGBA32` → a `WGPUTexture` (RGBA8) + view + linear sampler; store the view as the ImGui `ImTextureID`.
- Uniform buffer (16 floats ortho) + bind group (uniform + font sampler); per-texture bind groups cached by `ImTextureID` (view pointer) — reuse the single-entry cache idea from `gfx_webgpu.c`.
- `render`: grow + `wgpuQueueWriteBuffer` the vertex/index buffers from `draw_data`; per `ImDrawCmd`: clamp its clip rect with the `wgpu_clamp_rect` rule, `SetScissorRect`, bind the cmd's texture, `DrawIndexed(elem_count, 1, idx_offset, vtx_offset, 0)`.

**Note on the ImGui C/C++ boundary:** `gfx_webgpu_imgui.c` needs `ImDrawData`. Compile it as **C++** (rename to `.cpp` OR keep `.c` and pass the already-extracted arrays). Decision: compile as **`gfx_webgpu_imgui.cpp`** (it uses ImGui's C++ `ImDrawData`), exposing `extern "C"` functions — so C (`gfx_webgpu.c`/overlay) and C++ (AppHost) both call it. Add `${IMGUI_DIR}` to its include path.

- [x] **Step 1:** Wrote `gfx_webgpu_imgui.cpp/.h` (commit `fab11d2`).
- [x] **Step 2:** Wired into CMake — **into `mgb64_app`** (not `FAST3D_SOURCES`): the renderer needs ImGui headers, which only the app lib has; the engine only reaches it via `platformOverlayRender`. Added `src/platform/fast3d` to the app include path (safe — fast3d seam headers only) + linked `webgpu` to `mgb64_app`.
- [x] **Step 3 (validate):** compiles clean against wgpu-native v29 + ImGui 1.92.9 **standalone AND in `mgb64_app`**. (The offscreen render-readback ctest was folded into the compile-gate + the Task 4 launcher-capture validation, which is a stronger real-pixels test.)
- [x] **Step 4:** Committed `fab11d2`.

> **RESUME NOTE (Task 2 onward — the sequencing crux).** The remaining tasks are
> a sequencing-sensitive integration. The key ordering problem: on macOS the
> surface needs a `CAMetalLayer` from an `SDL_Metal_CreateView`, and **AppHost
> creates the window first** (for the launcher) while the game adopts it later.
> So `platformSetHostWindow` + the metal-view must be set up **before** AppHost
> creates its surface, so both AppHost and the adopting game resolve the *same*
> layer. Recommended concrete order for Task 4: (a) AppHost creates the
> `SDL_WINDOW_METAL` window + `SDL_Metal_CreateView`; (b) AppHost calls
> `platformSetHostWindow(window, /*gl*/NULL)` immediately so `platformGetMetalLayer`
> resolves AppHost's view; (c) AppHost builds instance/adapter/device/queue +
> `gfx_webgpu_create_surface`; (d) `platformSetHostWebGpu(...)`; (e) launcher
> renders; (f) at Play the game adopts both. Validate each step with
> `MGB64_APP_SMOKE_SHOT` (launcher) / `MGB64_APP_AUTOPLAY` (game) frame captures.

## Task 2: Expose `gfx_webgpu_create_surface(instance, window)`

**Files:** Modify `src/platform/fast3d/gfx_webgpu.c` (refactor `wgpu_create_surface` to take explicit `WGPUInstance` + `SDL_Window*`; keep the internal caller); add the prototype to a small `gfx_webgpu.h` (create it) or `gfx_backend.h`.

**Interfaces (Produces):** `WGPUSurface gfx_webgpu_create_surface(WGPUInstance instance, void *sdl_window);` — the current Metal-layer / Win32 / X11 / Wayland logic, parameterized. `wgpu_init` calls it with `s_instance` + the platform window (via a new `platformGetSdlWindow()` or the existing `g_sdlWindow` extern).

- [ ] **Step 1:** Refactor: move the body of `wgpu_create_surface` into `gfx_webgpu_create_surface(instance, window)`; `wgpu_create_surface()` becomes a thin caller using `s_instance` + the SDL window. On non-macOS it uses `platformWebGpuWindowInfo`; on macOS it uses `platformGetMetalLayer` (needs the window's metal view — for the app path AppHost creates the metal view, see Task 4).
- [ ] **Step 2 (validate):** `GE007_RENDERER=webgpu build-webgpu/ge007 --level dam --no-ui` still boots + renders (no behavior change); tapes byte-exact.
- [ ] **Step 3:** Commit `refactor(webgpu): parameterize surface creation (instance, window)`.

## Task 3: Host WebGPU handoff seam + `wgpu_init` adoption

**Files:**
- Modify: `src/platform/host_window.c` (+ its header `src/app/engine_entry.h`) — add the WebGPU host slots.
- Modify: `src/platform/fast3d/gfx_webgpu.c` (`wgpu_init` adoption + an `s_owns_device`/`s_owns_surface` flag so teardown never frees the host's).

**Interfaces (Produces):**
```c
void platformSetHostWebGpu(void *instance, void *adapter, void *device,
                           void *queue, void *surface, int surface_format);
int  platformHasHostWebGpu(void);
void *platformHostWgpuInstance(void); /* + Adapter/Device/Queue/Surface getters */
int   platformHostWgpuSurfaceFormat(void);
```

- [ ] **Step 1:** Add the host slots to `host_window.c` + getters; declare in `engine_entry.h`.
- [ ] **Step 2:** `wgpu_init`: if `platformHasHostWebGpu()`, adopt (`s_instance = host instance`, … `s_surface_format = host format`), set `s_owns_device=false`; else self-create (set `s_owns_device=true`). Guard the (future) teardown to only release owned objects.
- [ ] **Step 3 (validate):** standalone boot unchanged (host not set → self-create); tapes byte-exact; ctest green.
- [ ] **Step 4:** Commit `feat(webgpu): host device/surface handoff + adoption in gfx_webgpu`.

## Task 4: AppHost on WebGPU + WebGPU launcher UI

**Files:**
- Modify: `src/app/app_host.cpp` / `app_host.h` — create a Metal/native window (not GL), create the WebGPU host objects (via `gfx_webgpu_create_surface` + adapter/device request), init `gfx_webgpu_imgui`, drive `beginFrame`/`endFrame` through it. Keep `imgui_impl_sdl2` via `ImGui_ImplSDL2_InitForOther`.
- Modify: `CMakeLists.txt` (link `webgpu` into `mgb64_app`; the app already gets `MGB64_WEBGPU_BACKEND`).

**Interfaces (Consumes):** `gfx_webgpu_imgui_*` (Task 1), `gfx_webgpu_create_surface` (Task 2).

- [ ] **Step 1:** Window: `SDL_WINDOW_METAL` on macOS (+ `SDL_Metal_CreateView` so `platformGetMetalLayer` returns a layer), native elsewhere; drop `SDL_GL_CreateContext`.
- [ ] **Step 2:** Create WebGPU instance/adapter/device/queue + surface (reuse the spike's request sequence; surface via `gfx_webgpu_create_surface(instance, window_)`); configure to drawable size.
- [ ] **Step 3:** ImGui: `ImGui::CreateContext()` (unchanged) → `ImGui_ImplSDL2_InitForOther(window_)` + `gfx_webgpu_imgui_init(device, queue, format)`. `beginFrame`: `gfx_webgpu_imgui_new_frame` + `ImGui_ImplSDL2_NewFrame` + `ImGui::NewFrame`. `endFrame`: `ImGui::Render` → acquire surface texture → clear render pass → `gfx_webgpu_imgui_render(GetDrawData(), pass, w, h)` → end/submit/present (+ the `captureBmpPath` readback path via `gfx_webgpu`'s readback).
- [ ] **Step 4 (validate):** `MGB64_APP_SMOKE_FRAMES=30 MGB64_APP_SMOKE_SHOT=/tmp/launcher.bmp build/ge007` renders the **launcher UI** on WebGPU (BMP non-blank, log shows a WebGPU/Metal window, no GL context). Convert + eyeball.
- [ ] **Step 5:** Commit `feat(app): AppHost + launcher UI on WebGPU`.

## Task 5: Game adopts the host WebGPU; remove `force_opengl` from adoption

**Files:**
- Modify: `src/app/main_app.cpp` — after `platformSetHostWindow`, also `platformSetHostWebGpu(instance, adapter, device, queue, surface, format)` before `play()`.
- Modify: `src/platform/platform_sdl.c` — in the adoption path, DON'T `gfx_backend_force_opengl()` when a host WebGPU is present (only force GL if the host is GL / `GE007_RENDERER=gl`). Use the adopted metal view for `platformGetMetalLayer`.

**Interfaces (Consumes):** `platformSetHostWebGpu`/`platformHasHostWebGpu` (Task 3).

- [ ] **Step 1:** `main_app.cpp`: hand the WebGPU host objects to the engine alongside the window.
- [ ] **Step 2:** `platform_sdl.c` adoption path: `if (host is GL) force_opengl(); else adopt WebGPU` (the game's `wgpu_init` adopts via Task 3). Ensure the adopted window's metal view feeds `platformGetMetalLayer`.
- [ ] **Step 3 (validate):** `MGB64_APP_AUTOPLAY=1 MGB64_APP_AUTOPLAY_LEVEL=dam MGB64_BOOT_SCREENSHOT_FRAME=90 MGB64_BOOT_SCREENSHOT_EXIT=1 build/ge007` → the adopted window reports **WebGPU** (not GL), the game renders (BMP is Dam, not black). `GE007_RENDERER=gl` variant still renders via GL. Tapes byte-exact.
- [ ] **Step 4:** Commit `feat(app): game adopts the launcher's WebGPU device+surface`.

## Task 6: F1 overlay on WebGPU + `wgpu_end_frame` hook

**Files:**
- Modify: `src/app/ui_overlay.cpp` — render via `gfx_webgpu_imgui` (drop `imgui_impl_opengl3` there); `ImGui_ImplSDL2` stays.
- Modify: `src/platform/fast3d/gfx_webgpu.c` — in `wgpu_end_frame`, after the scene→surface blit and before present, call `platformOverlayRender()` (open a load-op render pass on the surface texture, hand its encoder to the overlay via a small `gfx_webgpu_overlay_render(cb)` or expose the current surface view + let the overlay open the pass).

**Interfaces (Consumes):** `gfx_webgpu_imgui_render` (Task 1), `platformOverlayRender` (existing hook).

- [ ] **Step 1:** `gfx_webgpu.c`: expose the current frame's surface view for the overlay; call `platformOverlayRender()` in `wgpu_end_frame` before present (mirrors GL `gfx_end_frame`).
- [ ] **Step 2:** `ui_overlay.cpp` render: `gfx_webgpu_imgui_new_frame` + `ImGui_ImplSDL2_NewFrame` + `ImGui::NewFrame` … `ImGui::Render` → `gfx_webgpu_imgui_render(GetDrawData(), <surface pass>, w, h)`.
- [ ] **Step 3 (validate):** autoplay boot + press-F1 path (or force the overlay on): the F1 overlay renders over the WebGPU game (capture shows the overlay). Tapes byte-exact.
- [ ] **Step 4:** Commit `feat(app): F1 overlay on WebGPU`.

## Task 7: Remove GL from the app path

**Files:**
- Modify: `src/app/app_host.cpp` (no GL context — done in Task 4; here delete any dead GL code), `ui_overlay.cpp` (no `imgui_impl_opengl3`), `CMakeLists.txt` (drop `imgui_impl_opengl3.cpp` from `mgb64_app` sources iff nothing else in the app needs it — the engine's standalone GL backend does NOT use ImGui, so the app lib no longer needs the GL ImGui backend).

- [ ] **Step 1:** Remove `imgui_impl_opengl3` from `mgb64_app`; delete dead GL paths in `app_host.cpp`/`ui_overlay.cpp`. Keep `gfx_backend_force_opengl` (the `GE007_RENDERER=gl` app path still needs a GL window + a GL ImGui — so KEEP `imgui_impl_opengl3` if the GL-app fallback must keep working; otherwise the GL app path loses its UI). **Decision:** keep `imgui_impl_opengl3` compiled but only used when `GE007_RENDERER=gl`; AppHost picks the ImGui renderer (GL vs WebGPU) at runtime by the selected backend. (This preserves the GL fallback app fully.)
- [ ] **Step 2 (validate):** default app → WebGPU UI + game; `GE007_RENDERER=gl` app → GL UI + game (both render). Full ctest green; MinGW `ge007.exe` links; Docker Linux builds; tapes byte-exact.
- [ ] **Step 3:** Commit `refactor(app): runtime GL/WebGPU UI selection; WebGPU default end to end`.

---

## Self-Review

**Spec coverage:** gfx_webgpu_imgui (T1), surface helper (T2), handoff+adoption (T3), AppHost WebGPU + launcher UI (T4), game adoption + remove force_opengl (T5), overlay (T6), GL-removal/runtime-selection (T7) — covers the spec's execution order 1–7 and the testing matrix.
**Placeholder scan:** each task names exact files + a concrete validation gate; the ImGui renderer's detailed per-command logic follows `imgui_impl_wgpu` + the `gfx_webgpu.c` patterns already in-tree.
**Type consistency:** `gfx_webgpu_imgui_init/new_frame/render/shutdown`, `platformSetHostWebGpu`/`platformHasHostWebGpu`, `gfx_webgpu_create_surface` used consistently T1→T7.
**Refinement to the spec:** T7 keeps `imgui_impl_opengl3` compiled and selects the UI renderer at runtime by backend, so the `GE007_RENDERER=gl` app keeps a working UI (the spec's "drop imgui_impl_opengl3" is softened to "runtime-select" to preserve the GL fallback app — a strict improvement).
