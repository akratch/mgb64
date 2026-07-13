# Launcher ↔ game WebGPU unification — design spec (2026-07-13)

Internal doc (`docs/superpowers/**` is export-ignored). No ROM-derived data here.

## Goal

Make the MGB64 launcher app render **end-to-end on WebGPU**, replacing the
`gfx_backend_force_opengl()` workaround (commit `ed9eab8`) that currently forces
the game to GL whenever it is launched through the launcher. After this, "one
backend everywhere" holds for the shipped app, not just standalone `--level`
boots. GL/Metal remain runtime-selectable fallbacks (`GE007_RENDERER=gl`/`metal`).

## Background: the coupling today

The launcher (`AppHost`, `src/app/app_host.cpp`) and the in-game F1 overlay
(`src/app/ui_overlay.cpp`) both render **ImGui via OpenGL** (`imgui_impl_sdl2` +
`imgui_impl_opengl3`) into a single `SDL_WINDOW_OPENGL` window with a GL context.
On Play, `main_app.cpp` hands that window to the game via `platformSetHostWindow`,
and the game adopts it. Two facts make the migration clean:

- **One ImGui context.** `AppHost::AppHost` calls `ImGui::CreateContext()` once;
  `Overlay_install` reuses it. So there is exactly **one** ImGui backend to
  initialize.
- **The overlay hook already exists at the right spot.** `gfx_end_frame` calls
  `platformOverlayRender()` immediately before `SDL_GL_SwapWindow` on the GL
  path. The WebGPU path needs the same call, before its present.

The launcher is **desktop-only** (`MGB64_APP=OFF` on the PortMaster/GLES path),
so a WebGPU launcher only targets Metal/D3D12/Vulkan — all already validated.

## Chosen architecture (approved: full unification)

```
AppHost (owns the WebGPU host objects, created once at startup)
 ├─ WGPUInstance / Adapter / Device / Queue / Surface   (owns)
 ├─ launcher UI     → gfx_webgpu_imgui → surface
 └─ platformSetHostWebGpu(instance, adapter, device, queue, surface, format)
          │  (Play)
          ▼
   gfx_webgpu.c  ADOPTS the host objects (no self-create)
          ├─ scene → offscreen → blit → surface
          └─ F1 overlay → gfx_webgpu_imgui → surface   (via platformOverlayRender)
          ▼  one wgpuSurfacePresent
```

### 1. WebGPU host ownership + a reusable surface helper
`AppHost` creates the WebGPU instance/adapter/device/queue and the surface at
startup (before drawing the launcher). The window is a Metal/native window
(macOS `SDL_WINDOW_METAL`, HWND/X11/Wayland elsewhere), NOT `SDL_WINDOW_OPENGL`;
no GL context is created. To keep the platform-specific surface code in one
place, `gfx_webgpu.c` exposes:
`WGPUSurface gfx_webgpu_create_surface(WGPUInstance, SDL_Window*)` — the same
Metal-layer / HWND / X11 / Wayland logic `wgpu_create_surface` uses today,
refactored to take an explicit instance + window. `AppHost` calls it.

### 2. The host handoff + adoption (mirrors the GL context handoff)
New platform seam (`src/platform/host_window.c` / a sibling `host_gfx.c`):
```
void platformSetHostWebGpu(void* instance, void* adapter, void* device,
                           void* queue, void* surface, int surface_format);
int  platformHasHostWebGpu(void);
// + typed getters used by gfx_webgpu.c
```
`gfx_webgpu.c` `wgpu_init`: **if** `platformHasHostWebGpu()`, adopt those objects
(set `s_instance/s_adapter/s_device/s_queue/s_surface/s_surface_format`, mark
them not-owned so teardown never releases the host's), **else** self-create as
today (standalone `--level` unchanged). This is exactly the adoption pattern the
GL path already uses (`platformHostWindow`/`platformHostGLContext`).

The `gfx_backend_force_opengl()` call in the platform_sdl.c adoption path is
**removed** once WebGPU adoption works (the window is now a Metal/native WebGPU
window, so WebGPU is correct). `gfx_backend_force_opengl()` itself stays as a
safety valve for a still-GL host window (e.g. `GE007_RENDERER=gl` with the app).

### 3. Frame lifecycle — one present per frame
- **Launcher mode** (no game): `AppHost::endFrame` acquires the surface texture,
  begins a clear render pass, calls `gfx_webgpu_imgui_render(draw_data, pass)`,
  ends the pass, submits, `wgpuSurfacePresent`.
- **Game mode**: `wgpu_end_frame` blits the scene into the surface texture, then
  calls `platformOverlayRender()` (the F1 overlay, rendered by
  `gfx_webgpu_imgui` into the same surface texture in a load-op pass), then one
  `wgpuSurfacePresent`. `gfx_webgpu` exposes the current frame's surface view to
  the overlay hook (a small `gfx_webgpu_overlay_pass_begin/end` or a direct
  callback with the view + encoder).
- **Surface size**: the game reconfigures the shared surface to its render
  resolution in `start_frame` (as standalone does today); `AppHost` reconfigures
  it back to the drawable size on return-to-launcher. Whoever draws owns the
  configured size; both call `wgpuSurfaceConfigure`.

### 4. ImGui on WebGPU — our own renderer (spike-driven)
`imgui_impl_wgpu` (ImGui 1.92.9 / master) does **not** compile against
wgpu-native v29: its `_WGPU` path targets the older wgpu API
(`WGPUProgrammableStageDescriptor`, positional struct initializers that predate
v29's `nextInChain`-first fields); its `_DAWN` path is Emscripten/Dawn-oriented.
Rather than carry a patched vendored copy that re-breaks on every wgpu-native
bump, we write **`src/platform/fast3d/gfx_webgpu_imgui.c` (+ `.h`)** — a minimal
ImGui renderer against v29, mirroring `imgui_impl_wgpu`'s logic with our proven
`gfx_webgpu.c` patterns:
- `gfx_webgpu_imgui_init(device, queue, surface_format)` — build the pipeline
  (WGSL vertex+fragment: MVP ortho, textured, alpha-blended), the font-atlas
  RGBA8 texture + sampler, a uniform buffer (the ortho matrix), a bind-group.
- `gfx_webgpu_imgui_render(ImDrawData*, WGPURenderPassEncoder)` — grow +
  `wgpuQueueWriteBuffer` the vertex/index buffers, set the pipeline/bind-group,
  and per `ImDrawCmd` set the scissor (clamped, reusing `wgpu_clamp_rect`'s rule)
  + texture bind-group and `DrawIndexed`.
- `gfx_webgpu_imgui_shutdown()`.

It is C (like `gfx_webgpu.c`), driven from the C++ app via the header. ImGui's
`ImDrawData`/`ImDrawVert`/`ImDrawCmd` are stable across 1.92.x.

`AppHost` and `ui_overlay.cpp` drop `imgui_impl_opengl3` and call the new
renderer; they keep `imgui_impl_sdl2` for event/input handling (it is
render-API-agnostic — `ImGui_ImplSDL2_InitForOther` instead of `...ForOpenGL`).

### 5. Removing GL from the app path
Once the above works: `AppHost` no longer creates a GL context or a
`SDL_WINDOW_OPENGL` window; `imgui_impl_opengl3` is dropped from the app lib's
sources (kept in-tree for the engine's standalone GL fallback). The engine's GL
and Metal backends are untouched (still selectable via `GE007_RENDERER`).

## Execution order (each step independently testable)

0. **Spike (DONE):** confirmed `imgui_impl_wgpu` is version-skewed from v29 →
   decision to write `gfx_webgpu_imgui`.
1. `gfx_webgpu_imgui.c/.h` — the ImGui WebGPU renderer, unit-smoke against a
   headless device (render a triangle-of-ImGui to an offscreen target, read back).
2. Refactor `wgpu_create_surface` → expose `gfx_webgpu_create_surface(instance,
   window)`; standalone still boots + renders (no behavior change).
3. `platformSetHostWebGpu`/`platformHasHostWebGpu` seam + `wgpu_init` adoption;
   standalone unchanged (host not set).
4. `AppHost`: create the WebGPU host + Metal/native window; render the launcher
   UI via `gfx_webgpu_imgui` (+ `imgui_impl_sdl2` for input). Launcher shows.
5. Hand the host objects to the game; remove `force_opengl()` from the adoption
   path; the game adopts + renders via WebGPU on the shared surface.
6. Overlay: `ui_overlay.cpp` renders via `gfx_webgpu_imgui`; wire
   `platformOverlayRender` into `wgpu_end_frame` before present.
7. Drop `imgui_impl_opengl3` from the app lib; delete the GL context from
   `AppHost`.

## Testing / validation

- `MGB64_APP_AUTOPLAY` (the exact adoption path): the adopted window must report
  **WebGPU** (not GL); the game + F1 overlay render (frame capture not black).
- `MGB64_APP_SMOKE_FRAMES` + `MGB64_APP_SMOKE_SHOT`: the **launcher UI** captures
  a non-blank WebGPU frame.
- `tools/fidelity/tape_regression.sh`: **7/7 byte-exact** on the app build
  (renderer never affects determinism).
- Full `ctest` green; MinGW `ge007.exe` links; Docker Linux (x86_64 + aarch64)
  builds.
- `GE007_RENDERER=gl` still gives a working GL app (the `force_opengl` safety
  valve); `--level` standalone still WebGPU.

## Risks + mitigations

- **ImGui `ImDrawData` API drift** — pinned to the vendored 1.92.9; our renderer
  uses only the stable `ImDrawVert`/`ImDrawCmd`/`ImTextureID` surface.
- **Font-atlas texture id plumbing** — ImGui 1.92 uses `ImTextureID` (a
  `void*`/`ImU64`); we bind our own `WGPUTextureView` handles through it, exactly
  as `imgui_impl_wgpu` does.
- **Shared-surface reconfig races** on the launcher↔game boundary — serialized
  by the blocking `play()` call (the launcher loop is paused while the game
  runs), so only one owner configures the surface at a time.
- **Cross-platform surface** already validated by the game; the launcher reuses
  the same `gfx_webgpu_create_surface`.
