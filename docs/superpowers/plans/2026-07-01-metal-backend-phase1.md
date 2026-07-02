# Metal Backend — Phase 1 (Bring-up + De-risking Spikes) Implementation Plan

> **For agentic workers:** execute task-by-task; each ends with an independently verifiable deliverable. Parent design: `docs/METAL_BACKEND_PLAN.md`.

**Goal:** A macOS Metal window that clears and presents behind an opt-in flag, with the GL path byte-identical, and the two biggest non-shader unknowns (cross-backend CPU-pipeline invariance; RDP/XLU framebuffer-snapshot feasibility) converted into early yes/no answers — *before* any combiner→MSL work.

**Architecture:** A second `struct GfxRenderingAPI` (`gfx_metal_api`) in a new `gfx_metal.mm` (Objective-C++ against the system Metal/QuartzCore headers — no metal-cpp vendoring, offline-safe), selected at the single seam `gfx_pc.c:22879` behind a runtime flag, `#ifdef __APPLE__`-guarded. GL stays default everywhere.

**Tech Stack:** C11 (existing) + Objective-C++ (new `.mm` TU) + system `Metal`/`QuartzCore`/`Foundation` frameworks + SDL2 Metal view. (libultraship's metal-cpp backend is the structural oracle; syntax maps `dev->newX()` → `[dev newX]`.)

## Global Constraints (verbatim)
- **Gameplay-invariant**; opt-in / default-identity: **GL is the default; Metal only when explicitly selected.** GL default path must stay **byte-identical** (verified by `cmp` of a deterministic PPM, not tolerance).
- Copyright-clean: no ROM-derived assets; `scripts/ci/check_no_rom_data.sh` stays green (commit hook enforces).
- All Metal code `if(APPLE)`-guarded in CMake + runtime-opt-in → Linux/Windows/GL CI untouched.
- Zero warnings under `PORT_STRICT` (`-Werror=deprecated-declarations`, `CMakeLists.txt:480-482`); ASan clean.
- Attribution: `gfx_metal.mm` header credits the libultraship (Kenix3, MIT) backend as structural reference, mirroring the existing Emill attribution in `gfx_rendering_api.h:1-3`. (No metal-cpp → no Apache NOTICE needed.)
- Commit messages end with the `Co-Authored-By: Claude Opus 4.8 (1M context)` trailer.

---

## Task 1: CMake Objective-C++ build fork + stub backend

**Files:**
- Modify: `CMakeLists.txt` (`:2` project languages; APPLE block; fast3d source list `~:306-311`; frameworks)
- Create: `src/platform/fast3d/gfx_metal.mm` (all-stub `gfx_metal_api`)

**Interfaces — Produces:** `extern "C" struct GfxRenderingAPI gfx_metal_api;` — a fully-populated vtable of no-op/return-default stubs (so the linker + seam wiring in Task 2 resolve). Every fn present; bodies stubbed.

**Steps:**
- [ ] Add `enable_language(OBJCXX)` after `project(ge007 C)`, `if(APPLE)`-guarded; set `CMAKE_OBJCXX_STANDARD 17`.
- [ ] Add `src/platform/fast3d/gfx_metal.mm` to the fast3d source list **inside an `if(APPLE)` guard** (GL TU stays for all platforms).
- [ ] Link `"-framework Metal" "-framework QuartzCore" "-framework Foundation"` in the APPLE `target_link_libraries` block (Foundation likely already linked via SDL/Cocoa — additive is fine).
- [ ] Write `gfx_metal.mm`: LUS MIT reference-attribution header; `#import <Metal/Metal.h> <QuartzCore/QuartzCore.h>`; define every one of the 22 fns as a stub (`z_is_from_0_to_1`→`return true;`, `read_framebuffer_rgb`→`return false;`, `upload_texture`→`return false;`, `lookup_shader`/`create_and_load_new_shader`→`return nullptr;`, rest no-op); `extern "C" struct GfxRenderingAPI gfx_metal_api = { … };` positional init matching `gfx_rendering_api.h` order.
- [ ] **Verify build (GL default):** `cmake --build build --parallel 8` → `Built target ge007`, zero warnings. `gfx_metal.o` compiled but unreferenced.
- [ ] **Verify GL byte-identical:** capture a deterministic PPM before/after this task (`--level 33 --deterministic --screenshot-frame 3`) and `cmp` — must be identical (no runtime path changed yet).
- [ ] Commit.

## Task 2: Backend selector + seam wiring + hoist `g_depth_clamp_enabled`

**Files:**
- Modify: `src/platform/fast3d/gfx_pc.c` (`:22879` seam; `:2416` `g_depth_clamp_enabled`; add extern `gfx_metal_api`)
- Create: `src/platform/fast3d/gfx_backend.h` (tiny selector API)

**Interfaces — Consumes:** `gfx_metal_api` (Task 1). **Produces:** `bool gfx_backend_use_metal(void);` (reads `GE007_RENDERER=metal` / `Video.Backend`; `#ifndef __APPLE__` → always false).

**Steps:**
- [ ] `gfx_backend.h`: declare `bool gfx_backend_use_metal(void);` + a cached-getenv/config impl (define in gfx_pc.c or a small .c). Default **GL**.
- [ ] At `gfx_pc.c:22879`: `extern struct GfxRenderingAPI gfx_metal_api;` (APPLE-guarded) and branch: `gfx_rapi = gfx_backend_use_metal() ? &gfx_metal_api : &gfx_opengl_api; gfx_rapi->init();`.
- [ ] **Invariance hoist:** ensure `g_depth_clamp_enabled = true` is set for the Metal path too. Simplest safe form: after `gfx_rapi->init()`, `if (gfx_backend_use_metal()) g_depth_clamp_enabled = true;` (GL still sets it in its own init — unchanged). Document the coupling at `:219`.
- [ ] **Verify GL byte-identical:** selector defaults to GL; re-`cmp` the deterministic PPM vs Task 1 — identical.
- [ ] **Verify selector:** `GE007_RENDERER=metal` makes `gfx_backend_use_metal()` return true (log it in init); with the stub backend the app will render nothing yet (expected) — just confirm it takes the Metal branch and doesn't crash on init stub.
- [ ] Commit.

## Task 3: SDL Metal window + backend-aware present/drawable-size

**Files:** Modify `src/platform/platform_sdl.c` (`:2092-2101` GL attrs; `:2109` window flags; `:2138` context; `:600` drawable size; present site) + a `void* platformGetMetalLayer(void)` accessor.

**Steps:**
- [ ] Gate the `SDL_GL_SetAttribute` block (`:2092-2101`) and `SDL_GL_CreateContext` (`:2138`) behind `!gfx_backend_use_metal()`.
- [ ] Metal branch: window flags use **no** `SDL_WINDOW_OPENGL`; after `SDL_CreateWindow`, `SDL_Metal_CreateView(window)` → store view; `platformGetMetalLayer()` returns `SDL_Metal_GetLayer(view)` (`void*` → `CAMetalLayer*`).
- [ ] `SDL_GL_GetDrawableSize` (`:600`) → backend-aware: Metal uses `SDL_Metal_GetDrawableSize` (or the layer's `drawableSize`).
- [ ] Make the present site backend-aware (Metal present happens in `gfx_metal end_frame`; the `SDL_GL_SwapWindow` call is GL-only).
- [ ] **Verify:** GL path unchanged (byte-identical PPM); Metal path opens a window without a GL context and exposes a non-null `CAMetalLayer`.
- [ ] Commit.

## Task 4: `gfx_metal` bring-up — clear to color + present

**Files:** Modify `src/platform/fast3d/gfx_metal.cpp` (real `init`/`start_frame`/`end_frame`/`finish_render`/`on_resize`/`set_clear_color`/`max_offscreen_dim`).

**Steps:**
- [ ] `init`: create `MTL::Device` (`CreateSystemDefaultDevice`), `CommandQueue`; bind `CAMetalLayer` from `platformGetMetalLayer()` (set device, pixelFormat `BGRA8Unorm`); set `g_depth_clamp_enabled=true` (belt-and-suspenders with Task 2).
- [ ] `start_frame`: `NS::AutoreleasePool`; `layer->nextDrawable()`; build `MTL::RenderPassDescriptor` `loadAction=Clear` with the current clear color; open a `RenderCommandEncoder`, `endEncoding` (clear-only for now).
- [ ] `end_frame`: `presentDrawable`; `commit`; release pool.
- [ ] `set_clear_color`: store rgba. `max_offscreen_dim`: return a sane cap (e.g. `min(device.maxTextureDimension, 16384)` → match GL's value at `gfx_opengl_max_offscreen_dim`). Route the two non-vtable call sites (`gfx_pc.c:23165`, `:4434`) to a backend-aware dispatch (extern the Metal variants; branch on `gfx_backend_use_metal()`).
- [ ] `on_resize`: update `layer.drawableSize`.
- [ ] **Verify:** `GE007_RENDERER=metal ... --level 33 --deterministic --screenshot-frame 3 --screenshot-exit` → **exit 0**, window clears to the level's clear color and presents (screenshot shows the clear color, no geometry — expected). No hang (bring-up has zero reconstruction math). GL path still byte-identical.
- [ ] Commit.

## Task 5: Spike A — cross-backend CPU-pipeline invariance (GO/NO-GO signal)

**Rationale:** `draw_triangles` is a Metal no-op counter here, but the **CPU display-list interpreter + clipper run identically regardless of backend** — so `tris`/`rooms_drawn` bookkeeping and the `compare_state.py` gameplay trace must match GL, *provided* `g_depth_clamp_enabled` is true on both (Task 2/4). This is the invariance-critical check the parent plan flagged.

**Steps:**
- [ ] In `gfx_metal draw_triangles`, increment the same tri/rooms counters the harness reads (or confirm they're computed upstream in gfx_pc.c and backend-agnostic — inspect `:16644, :18839, :19016`).
- [ ] Run a short shared RAMROM replay on the **GL** build and the **Metal** build; capture `compare_state.py` gameplay-field traces + `tris`/`rooms_drawn`.
- [ ] **Verify (GO/NO-GO):** traces + tri counts **match**. RED → abort before shader work; the invariance coupling is deeper than `g_depth_clamp_enabled` and must be root-caused first.
- [ ] Record result in `docs/METAL_BACKEND_PLAN.md` (Spike A ✅/❌). Commit.

## Task 6: Spike B — RDP/XLU framebuffer-snapshot feasibility (GO/NO-GO signal)

**Rationale:** The FRESH, no-template risk #1: the per-batch XLU snapshot + RDP-memory blend needs to sample a color attachment mid-frame. In Metal that means a **blit-encoder copy → sampled texture** with a forced encoder break. Prove one round-trip works before relying on it in Phase 3.

**Steps:**
- [ ] In `gfx_metal.cpp`, behind a `GE007_METAL_SPIKE_B` env flag: create an offscreen `MTLTexture` (`BGRA8Unorm`, `RenderTarget|ShaderRead`), render a known color into it, `BlitCommandEncoder` copy → a second sampled texture, then a trivial fullscreen pass samples the copy and writes to the drawable.
- [ ] **Verify (GO/NO-GO):** the sampled-copy color reaches the screen (screenshot matches the known color). RED → the RDP/XLU path may be infeasible as designed; re-scope (e.g. programmable-blending `[[color(0)]]` inout) before Phase 3.
- [ ] Record result in `docs/METAL_BACKEND_PLAN.md` (Spike B ✅/❌). Commit.

---

## Phase 1 Exit Criteria
Metal window clears+presents behind the flag; GL default byte-identical (cmp); zero warnings under PORT_STRICT; ASan clean; Linux/Windows/GL CI green; **Spike A trace-match** and **Spike B round-trip** both green (or a red with a recorded re-scope decision). Only then proceed to Phase 2 (combiner→MSL — the make-or-break gate).
