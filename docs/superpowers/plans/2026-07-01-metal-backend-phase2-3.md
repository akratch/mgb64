# Metal Backend — Phases 2 & 3 (Combiner→MSL + Game-at-Parity) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (inline, this session) to implement task-by-task. Steps use checkbox (`- [ ]`) syntax. Parent design: `docs/METAL_BACKEND_PLAN.md` §2.2–§2.4, §3 (Phases 2–3). Phase 1 plan (done): `docs/superpowers/plans/2026-07-01-metal-backend-phase1.md`.

**Goal:** Every live N64 combiner variant compiles to a Metal render-pipeline and the full game renders on the native Metal backend at tolerance-parity with OpenGL — geometry, textures, depth/blend, the RDP-memory/XLU framebuffer-snapshot path, and synchronous readback — with the OpenGL default path byte-identical throughout.

**Architecture:** Retarget this port's own GLSL string-builder (`gfx_opengl.c:722-1449`) to emit **MSL** inside `gfx_metal.mm`, compiling each combiner to an `MTLLibrary` + lazily-built `MTLRenderPipelineState` cache keyed on the dynamic state Metal bakes into the PSO (blend, sample-count, color format, colorWriteMask). GL's independent immediate-mode setters (`load_shader`/`set_blend_mode`/`set_depth_mode`/`set_sampler_parameters`) become **deferred state recorded by the setters and resolved at `draw_triangles`**, which writes the frontend's interleaved `buf_vbo[]` (identical layout to GL) into a ring buffer and flushes one encoder draw. The FRESH RDP-memory blend re-authors GL's `glCopyTexSubImage2D` snapshot as a Metal blit-encoder color-copy into a sampled texture (forced encoder break), with bit-exact `>>3`/`>>5` integer math in MSL.

**Tech Stack:** Objective-C++ (`.mm`, ARC) + system `Metal`/`QuartzCore`/`Foundation`; MSL 2.x compiled at runtime via `[device newLibraryWithSource:options:error:]`. C11 frontend unchanged. libultraship `gfx_metal_shader.cpp` is the correctness oracle for the item→string table (NOT lifted).

## Global Constraints (verbatim)

- **Gameplay-invariant**; opt-in / default-identity: **GL is the default; Metal only when `GE007_RENDERER=metal`.** GL default path must stay **byte-identical** (verified by `cmp` of a deterministic PPM/BMP, not tolerance).
- Copyright-clean: no ROM-derived assets; `scripts/ci/check_no_rom_data.sh` stays green.
- All Metal code `if(APPLE)`-guarded in CMake + runtime-opt-in → Linux/Windows/GL CI untouched and byte-identical.
- Zero warnings under `PORT_STRICT` (`-Werror=deprecated-declarations`, `CMakeLists.txt:480-482`); ASan clean.
- Attribution: `gfx_metal.mm` header already credits libultraship (Kenix3, MIT) as structural reference — keep it.
- Commit messages end with the `Co-Authored-By: Claude Opus 4.8 (1M context)` trailer.
- **Cross-backend gameplay invariance** proven with `tools/compare_state.py` on a shared run (NOT `sim_state_hash`, which folds render bookkeeping). **Screenshot parity** via `tools/compare_screenshots.py`/`renderer_parity_capture.sh` with per-golden `--max-changed-pct` budgets, dither/quantize normalized.

---

## File Structure

- **Modify only:** `src/platform/fast3d/gfx_metal.mm` — all Phase 2–3 work lands here (mirrors `gfx_opengl.c`'s single-TU model; GL backend is 3652 lines, so a ~2000-line Metal TU is in-pattern). No new files; no frontend changes beyond what Phase 1 already wired (the vtable + the 3 non-vtable couplings are done).
- **Reference (read, do not modify):** `gfx_opengl.c` (the oracle for every behavior), `gfx_cc.c`/`gfx_cc.h` (feature decode — backend-agnostic, reused as-is), `gfx_pc.c:18390-18700` (VBO packing order), `gfx_rendering_api.h` (vtable contract).

### The canonical vertex layout (locked — `gfx_pc.c:18390-18540` == generator attrib-push order)

Per vertex, floats appended in this exact order (stride = `num_floats`):
1. `aVtxPos` — x, y, z, w (4)  *(z already remapped to [0,1] by the frontend when `z_is_from_0_to_1()` → true)*
2. if `opt_alpha && diag_rdp_cvg_memory_blend`: `aDiagTri01`(4) + `aDiagTri2`(2) = 6
3. per texture ti∈{0,1} if `used_textures[ti]`: `aTexCoord`(2) + `aTexClampS`(1?) + `aTexClampT`(1?) + `aTexMaskS`(1?) + `aTexMaskT`(1?)
4. if `opt_fog`: `aFog` (4)
5. `num_inputs` × `aInput`(3 if !opt_alpha else 4)

The Metal `MTLVertexDescriptor` assigns `[[attribute(i)]]` at increasing byte offsets in this same order; the MSL `VertexIn` struct mirrors it. Both are generated from the same `CCFeatures`, so they cannot drift.

### GLSL→MSL vocabulary map (the mechanical core of Phase 2)

| GLSL | MSL | Notes |
|---|---|---|
| `vec2/3/4` | `float2/3/4` | |
| `.rgb` / `.a` / `.xy` / `.s` / `.t` | same | MSL supports rgba+xyzw+st swizzles |
| `texture(t, uv)` | `t.sample(smp, uv)` | sampler passed alongside texture |
| `textureLod(t, uv, 0.0)` | `t.sample(smp, uv, level(0.0))` | |
| `textureSize(t, 0)` | `float2(t.get_width(), t.get_height())` | |
| `gl_FragCoord.xy` | `float2(in.pos.x, uWinH - in.pos.y)` | **Y-FLIP**: GL bottom-left vs Metal top-left origin. `uWinH` = attachment height (uniform). Load-bearing for noise dither + RDP-memory sampling. |
| `dFdx/dFdy` | `dfdx/dfdy` | |
| `noperspective X` (varying) | `X [[center_no_perspective]]` | on the `VertexOut` member |
| `discard` | `discard_fragment()` | |
| `mod(a,b)` | `fmod(a,b)` | (note: GLSL `mod` and MSL `fmod` differ for negative operands; here operands are non-negative — verify per site) |
| `mix/clamp/floor/fract/abs/sign/step/max/min/dot/sin` | same | all in `metal_stdlib` |
| `out vec4 fragColor;` | fragment returns `float4` | |
| `in`/`out` varyings | `VertexOut` struct members | |
| `uniform sampler2D uTexN` | `texture2d<float> uTexN [[texture(N)]]`, `sampler smpN [[sampler(N)]]` | |
| `uniform int/float/vec2/vec4` | `constant Uniforms& u [[buffer(1)]]` fields | one packed struct |

### Metal state model (replaces GL immediate mode)

```
MetalShader (≈ struct ShaderProgram):
    uint64 id0; uint32 id1;
    CCFeatures cc;                       // decoded once
    id<MTLLibrary> library;              // vertex+fragment funcs
    id<MTLFunction> vtxFn, fragFn;
    MTLVertexDescriptor* vtxDesc;        // from the float layout
    uint8 numFloats, numInputs;
    bool usedTextures[2], usedNoise, usedN64Filter;
    bool diagRdpMemory, diagRdpCvgMemory;
    NSMutableDictionary* psoCache;       // key(blend,samples,fmt,writeMask) -> id<MTLRenderPipelineState>

Deferred draw state (globals, set by setters, resolved in draw_triangles):
    MetalShader* curShader;
    GfxBlendMode curBlend;
    depth: {test,update,compare,srcPrim,zmode}
    per-tile: {textureId, samplerLinear, cms, cmt}
    viewport{x,y,w,h}, scissor{x,y,w,h}
    Caches: MTLDepthStencilState by (test,update,cmpFunc,zmode); MTLSamplerState by (linear,cms,cmt)
```

---

## PHASE 2 — Combiner → MSL (the GO/NO-GO gate)

### Task 2.1: Scaffolding — MetalShader type, source builder skeleton, MSL compile

**Files:** Modify `src/platform/fast3d/gfx_metal.mm`.

**Interfaces — Produces:** `struct ShaderProgram` (opaque to frontend) backed by an ObjC++ `MetalShader`; `mtl_create_and_load_new_shader`/`mtl_lookup_shader`/`mtl_shader_get_info`/`mtl_load_shader`/`mtl_unload_shader` real implementations. Consumes: `gfx_cc_get_features` (from `gfx_cc.c`, extern "C").

- [ ] Add `#include "gfx_cc.h"` and `extern bool g_depth_clamp_enabled;`. Define the `MetalShader` object (Objective-C++ class or a C++ struct with ObjC ivars via `__strong`), a static pool/`NSMutableArray`, `s_cur_shader`.
- [ ] Port `shader_item_to_str` + `append_formula` verbatim to file-static functions emitting **MSL vocabulary** (apply the map above: `vec4`→`float4`, noise's `gl_FragCoord`→flipped `fragCoord`, `window_height`→`u.winH`, `frame_count`→`u.frameCount`). Keep identical case structure to `gfx_opengl.c:731-817` — LUS `p_shader_item_to_str` is the 1:1 oracle.
- [ ] Write `mtl_build_shader_source(CCFeatures, out std::string)` that mirrors `gfx_opengl_create_and_load_new_shader:823-1284` block-for-block, emitting one MSL TU with `vertexMain` + `fragmentMain`. Emit the `VertexIn`/`VertexOut` structs from the same per-feature attribute walk. Compute `num_floats` identically.
- [ ] `mtl_create_and_load_new_shader`: decode CCFeatures, build source, `newLibraryWithSource:` (log + `abort()` on compile error, mirroring GL's abort at `:1319`), fetch `vertexMain`/`fragmentMain`, build the `MTLVertexDescriptor`, store MetalShader in pool, set `s_cur_shader`, return it.
- [ ] `mtl_lookup_shader` linear-search pool by (id0,id1); `mtl_shader_get_info` return numInputs+usedTextures; `mtl_load_shader` record `s_cur_shader`; `mtl_unload_shader` clear if match.
- [ ] **Verify build:** `cmake --build build --parallel 8` → zero warnings, `Built target ge007`.
- [ ] **Verify GL byte-identical:** capture GL BMP before/after (`GE007_RENDERER` unset), `cmp` — identical (no GL path touched).
- [ ] **Verify Metal compiles shaders:** `GE007_RENDERER=metal … --level 33 --deterministic --screenshot-frame 3 --screenshot-exit` → exit 0; stderr shows N libraries compiled, no MSL compile-abort. (Still no geometry — draw is stubbed until 2.2/3.1.) Commit.

### Task 2.2: PSO + depth/sampler caches + vertex descriptor build

**Files:** Modify `gfx_metal.mm`.

**Interfaces — Produces:** `mtl_pso_for(MetalShader*, blend, samples, fmt, writeMask)` → cached `id<MTLRenderPipelineState>`; `mtl_depth_state_for(...)`, `mtl_sampler_for(...)`. Consumes: MetalShader (2.1).

- [ ] Implement `mtl_pso_for`: build `MTLRenderPipelineDescriptor` (vtx/frag fns, `vtxDesc`, `colorAttachments[0].pixelFormat`, `rasterSampleCount`, `writeMask`, blend factors per `GfxBlendMode`→ the map from `gfx_opengl_set_blend_mode:1619-1647`), `newRenderPipelineStateWithDescriptor:`. Cache by packed 64-bit key. **colorWriteMask is part of the key** (§2.2 risk #4).
- [ ] Implement `mtl_depth_state_for(test,update,compare,zmode)` mirroring `gfx_opengl_set_depth_mode:1556-1607` (LEQUAL/LESS/ALWAYS map to `MTLCompareFunction`; `depthWriteEnabled=update`). Cache by key. DEC polygon-offset handled per-draw via `setDepthBias:slopeScale:clamp:`.
- [ ] Implement `mtl_sampler_for(linear,cms,cmt)` mirroring `gfx_opengl_set_sampler_parameters:1529-1550` (`G_TX_CLAMP`→clampToEdge, `G_TX_MIRROR`→mirrorRepeat, else repeat; min/mag linear/nearest; no mipmaps). Cache by key.
- [ ] Implement `mtl_build_vertex_descriptor(MetalShader*)` — walk the same attribute order as the layout above, assigning `attributes[i].format` (float4/float2/float per size), `.offset`, `.bufferIndex=0`; `layouts[0].stride = numFloats*4`, `.stepFunction=perVertex`.
- [ ] **Verify build** zero-warnings; **GL byte-identical** unchanged. Commit. (No visible change yet; caches exercised in 3.1.)

### Task 2.3: Curated combiner smoke — compile every live variant

**Files:** none (validation task); optionally a throwaway `GE007_METAL_DUMP_SHADERS` env to dump MSL like GL's `[SHADER_n]` dump (`gfx_opengl.c:1290-1302`).

- [ ] Run a broad scene set on Metal (`--level` sweep over ≥6 levels incl. Dam/jungle/facility + split-screen + an HD-pack level) with a shader-compile counter; confirm **zero MSL compile-aborts** across all combiner variants the game actually instantiates. This exercises: tile-mask, N64 filter, shader clamp, noperspective, fog, 2-cycle, noise, texture-edge, and the RDP diag combiners.
- [ ] Record the compiled-variant count + any variant that needed a vocabulary fix in `docs/METAL_BACKEND_PLAN.md` (Phase 2 note). Commit.

**Phase 2 GO/NO-GO:** every instantiated combiner compiles to a PSO with no abort. Pixel-parity of those combiners is judged at the end of Phase 3 (needs geometry). GO if compiles are clean and the shader structure matches GL block-for-block; NO-GO (re-scope) if a feature can't be expressed in MSL.

---

## PHASE 3 — Render the game at parity with GL

### Task 3.1: draw_triangles flush + frame render targets (depth) — first geometry

**Files:** Modify `gfx_metal.mm`.

**Interfaces — Produces:** real `mtl_draw_triangles`, `mtl_set_viewport` (Y-flip), `mtl_set_scissor` (clamp), `mtl_set_depth_mode`/`set_blend_mode`/`set_sampler_parameters` (record), `mtl_select_texture`. A per-frame depth `MTLTexture` (`Depth32Float`) alongside the drawable color; `start_frame` opens ONE encoder spanning all draws; `end_frame` present.

- [ ] Rework `start_frame`/`end_frame`: create a per-frame depth texture sized to the drawable (recreate on size change), one `MTLRenderPassDescriptor` (color=drawable `loadAction=Clear` clear-color; depth `loadAction=Clear` clearDepth=1.0), open ONE `id<MTLRenderCommandEncoder>` held for the frame. `end_frame` ends encoding, presents, commits.
- [ ] Ring vertex-buffer pool (triple-buffered `id<MTLBuffer>`), `mtl_draw_triangles`: `memcpy` `buf_vbo` into ring buffer; resolve PSO (`mtl_pso_for(curShader, curBlend, sampleCount, drawableFmt, writeMask)`), depth-state, per-tile samplers; set on encoder: pipeline, vertex buffer(0), uniforms(1), textures+samplers, depth-stencil, viewport, scissor, depth-bias if ZMODE_DEC; `drawPrimitives:triangle vertexStart:0 vertexCount:3*numTris`.
- [ ] `mtl_set_viewport(x,y,w,h)`: store; convert to Metal `MTLViewport` with **Y-flip** relative to attachment height. `mtl_set_scissor`: store; **clamp to attachment bounds** (Metal asserts on OOB — split-screen risk §3). `set_depth_mode`/`set_blend_mode`/`set_sampler_parameters`/`select_texture`: record into deferred state (+ note `writeMask` from the coverage-alpha-preserve logic at `gfx_opengl.c:1692-1703`).
- [ ] Uniforms buffer: pack `frameCount`, `winH` (attachment height for the fragcoord flip + noise 240-scale), `n64FilterScale` (from `gfx_opengl_set_uniforms:617-634` logic — needs `viGetX/Y`, `gfx_current_dimensions`), diag origin/viewport (3.3). Bind at buffer(1) for both stages.
- [ ] **Verify:** Metal renders level geometry (`--level 33`) — screenshot shows the scene, not just clear. **GL byte-identical.** ASan clean. Commit.
- [ ] **Cross-backend invariance:** `compare_state.py` GL-vs-Metal on a shared run stays green (draw counts unchanged). Commit.

### Task 3.2: Texture upload + select (replaceRegion)

**Files:** Modify `gfx_metal.mm`.

**Interfaces — Produces:** real `mtl_new_texture`/`mtl_delete_texture`/`mtl_select_texture`/`mtl_upload_texture`.

- [ ] Texture registry: id→`id<MTLTexture>` (monotonic ids like GL names). `new_texture` reserves an id; `upload_texture(rgba32,w,h)` creates an `MTLTexture` (`RGBA8Unorm`, `width×height`, `usage=ShaderRead`) and `replaceRegion:mipmapLevel:withBytes:bytesPerRow:` (bytesPerRow=w*4); bounds-check like `gfx_opengl_upload_texture:1484` (reject >4096, drop GL's zero-texture NPOT substitution — Metal handles NPOT natively). `select_texture(tile,id)` records the bound texture for that tile; `delete_texture` releases.
- [ ] Bind recorded textures + samplers at draw time (3.1 flush): `[enc setFragmentTexture:tex atIndex:tile]`, `[enc setFragmentSamplerState:smp atIndex:tile]`.
- [ ] **Verify:** textured scene renders (walls/props textured, not flat) on Metal; **GL byte-identical**; HD-pack level (`feat/dam-hd-remaster` pack if available, else stock) uploads through `replaceRegion` without error. Commit.

### Task 3.3: RDP-memory / coverage blend + per-batch XLU framebuffer snapshot (FRESH, #1 risk)

**Files:** Modify `gfx_metal.mm`.

**Interfaces — Produces:** the snapshot texture + encoder-break flush for `GFX_BLEND_ALPHA_RDP_MEMORY`/`_RDP_CVG_MEMORY`; bit-exact `>>3`/`>>5` MSL (already emitted by 2.1's port of `gfx_opengl.c:1238-1277` — this task wires the framebuffer-sampling side).

- [ ] In `draw_triangles`, when `curBlend ∈ {RDP_MEMORY, RDP_CVG_MEMORY}`: compute the batch snapshot rect (port `gfx_opengl_compute_batch_snapshot_rect:1936`), **end the current encoder**, blit-copy the scene color attachment region → a persistent `snapshotTex` (`RGBA8Unorm`, ShaderRead), **reopen the encoder with `loadAction=Load`** (preserve prior contents), bind `snapshotTex` at `[[texture(2)]]`, set the diag origin/viewport uniforms, then draw. (Per-batch default; `GE007_XLU_SNAPSHOT_MODE=pertri` A/B parity path optional.)
- [ ] Because the frame color target must be *readable* for the blit, the scene renders into an **offscreen color `MTLTexture`** (`RGBA8Unorm`, `RenderTarget|ShaderRead`) instead of the drawable directly; `end_frame` blits offscreen→drawable. (This is also the Phase-4 structural prep — do it now since the snapshot needs it.)
- [ ] Preserve the coverage-alpha write-mask semantics (`gfx_opengl.c:1692-1703`) via the PSO `writeMask` key.
- [ ] **Verify:** glass (Dam/facility) + jungle foliage render correctly on Metal — the materials this path exists for. A/B vs GL within budget. **GL byte-identical.** Commit.

### Task 3.4: read_framebuffer_rgb synchronous readback + probes

**Files:** Modify `gfx_metal.mm`.

**Interfaces — Produces:** real `mtl_read_framebuffer_rgb` (blit→shared `MTLBuffer`→`getBytes` with `waitUntilCompleted`, Y-flip); `mtl_finish_render` blocks appropriately.

- [ ] `read_framebuffer_rgb(x,y,w,h,rgb_out)`: blit the offscreen color region → a shared `MTLBuffer`, `waitUntilCompleted`, copy out as RGB with **Y-flip** (GL bottom-up vs Metal top-left; mirror the screenshot flip at `gfx_pc.c:23195`). Handle the readback for both the screenshot path and the 14 diag probes (`gfx_pc.c:6528-8036`).
- [ ] **Verify:** `--screenshot-frame N --screenshot-exit` on Metal writes a correct (non-flipped, correct-color) BMP; the readback probes log values that agree with GL **within the ±1-LSB / bit-exact-RDP rule** (§2.4). Commit.

### Task 3.5: Full-level + split-screen parity sweep (Phase 3 acceptance)

**Files:** none (validation); doc update.

- [ ] `renderer_parity_capture.sh` + `compare_screenshots.py` GL-vs-Metal across ≥8 representative levels + a split-screen frame; establish per-golden `--max-changed-pct` budgets (dither/quantize normalized). Presentation/`active_bbox` for gross parity; masked-ROI pixel-diff for detail.
- [ ] `compare_state.py` GL-vs-Metal green (gameplay-invariant). ASan clean. Zero warnings. Linux/Windows/GL CI unaffected (Metal is APPLE+opt-in).
- [ ] Record results + residual deltas in `docs/METAL_BACKEND_PLAN.md` (Phase 3 complete). Commit.

**Phase 3 acceptance:** all swept levels + split-screen within budget; readback probes agree per the tolerance rule; GL byte-identical; ASan/warnings/CI clean.

---

## Self-Review notes
- **Spec coverage:** §2.3 combiner→MSL → Tasks 2.1–2.3; §2.2 caches/PSO/vertex-desc → 2.2, 3.1–3.2; §2.4 readback+probe calibration → 3.4; FRESH RDP/XLU snapshot → 3.3; z-hack drop + `z_is_from_0_to_1` → already true in Phase 1 stub, VS drops `z*=0.3` since `g_depth_clamp_enabled=true` on Metal. Phase 4 (remaster post-FX) and Phase 5 (SSAO) are explicitly OUT of this plan (faithful-base parity only), but 3.3 front-loads the offscreen-target plumbing they need.
- **No placeholders:** MSL bodies are *mechanically derived* from cited GL line ranges via the vocabulary map — the "how" is the map + the oracle, not invented per step.
- **Type consistency:** `MetalShader`, `mtl_pso_for`, `mtl_depth_state_for`, `mtl_sampler_for`, `mtl_build_vertex_descriptor` names used consistently across 2.1→3.4.
