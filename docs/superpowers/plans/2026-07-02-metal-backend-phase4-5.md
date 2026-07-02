# Metal Backend — Phase 4 (output-VI-filter post-FX) + Phase 5 (SSAO payoff) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans (inline). Steps use `- [ ]`. Parent design: `docs/METAL_BACKEND_PLAN.md` §2.5, §5 (Phases 4–5). Predecessors DONE + hardened: `docs/superpowers/plans/2026-07-01-metal-backend-phase2-3.md`. This plan was de-risked by a 4-agent analysis workflow; **one of its findings (a "depth-convention crux") was adversarially refuted — see the boxed correction below.**

**Goal:** Run the remaster output-VI-filter chain (FXAA, CAS sharpen, bloom, color-grade, filmic tonemap, gamma, vignette, Bayer dither, RGB555 quantize) natively on Metal (Phase 4), then enable SSAO sampling the native `Depth32Float` scene depth (Phase 5) — the whole reason the backend exists, since this math op-hangs Apple's GL-over-Metal translator.

**Architecture:** Replace `mtl_end_frame`'s straight `s_scene_color → drawable` blit with a **gated fullscreen-triangle filter chain**. The scene already renders into an offscreen sampleable `s_scene_color` + `s_scene_depth`, so GL's "copy default-FB → copy_tex" pass is **deleted** — `s_scene_color` *is* the filter source. Common remaster case = **one** pass (`s_scene_color`(+depth) → drawable, `apply_post=1`); VI-downscale case = **two** passes (→ `s_filter_low` mode-0 copy → drawable with post on the final pass only), mirroring GL's low-res+upscale split (`gfx_opengl.c:3397-3435`). SSAO is **folded into the filter fragment** (as GL does at `gfx_opengl.c:3020-3024`), not a separate pass. When all knobs are off, the existing blit is kept verbatim (preserves the byte-identical default).

**Tech Stack:** Objective-C++/ARC + MSL (a second `MTLLibrary` for the fullscreen filter, compiled once at init). C-buffer source builder (STL unusable — math.h shim). GL oracle: `gfx_opengl.c:2833-3091` (shader) + `:3163-3473` (multi-pass driver).

## Global Constraints (verbatim)
- **Gameplay-invariant / render-only**: the filter reads only `s_scene_color`, `s_scene_depth`, and read-only `g_pc_ssao_proj_*`; writes only `s_filter_low` + the drawable; never touches rsp/rdp/sim state. Cross-backend `compare_state.py` MATCH must hold; the R3 sim-invariance hash (`5c2983a3f0b7345f`) is unaffected.
- **GL default path byte-identical** (`cmp`); all Metal work `if(APPLE)`+runtime-opt-in; ASan clean; zero warnings (gfx_metal.mm is strict-clean today — keep it so); Linux/Windows CI untouched.
- Commit trailer: `Co-Authored-By: Claude Opus 4.8 (1M context)`.

---

> ### ⚠️ CORRECTION — the planning workflow's "depth-convention crux" is a FALSE POSITIVE (verified; do NOT apply it)
> The analysis claimed `out.position = in.aVtxPos` (gfx_metal.mm scene VS) passes raw GL clip-Z ([-w,w]) so `MTLDepthClipModeClamp` collapses the near-depth half to 0, and proposed adding `out.position.z = (z+w)*0.5` to the scene VS.
> **This is wrong.** The frontend already applies exactly that remap: `gfx_pc.c:18395` does `if (z_is_from_0_to_1) z = (z + w) / 2.0f;`, and `mtl_z_is_from_0_to_1()` returns `true`. So the clip-Z packed into `aVtxPos.z` is already `(z_gl+w)/2` → Metal NDC z = `(ndc_gl+1)/2` ∈ [0,1] → Metal window depth **equals** GL window depth. Adding the VS remap would **double-remap** (`(z+3w)/4`) and break depth entirely.
> **Consequences for this plan:** (a) Task 0/T8 is **DELETED**; (b) `ssaoLinZ` (which uses `2d-1`) works **verbatim** because Metal's stored `d` == GL's `d`; (c) no scene-VS change, no Spike-A re-run needed. This is independently confirmed by Phase-3 validation: depth-tested geometry occludes correctly across all 18 levels — impossible if the near half collapsed. Lesson: the analysts didn't see the frontend remap; always verify against `gfx_pc.c`.

---

> **PHASE 4 — ✅ COMPLETE (commits 2642bba + 982fa96).** T1–T7 done. The output-VI-filter runs as a fullscreen chain in `mtl_end_frame` (FXAA/CAS/bloom/grade/tonemap/gamma/vignette/dither/rgb555); scene→`s_final_color`→drawable, `s_final_color` also the readback source (screenshots match display); mid-frame readback reads the pre-filter scene. **Correction to this plan:** GL's filter is a **2-pass** pipeline (resolve pre-pass + final) and applies colorScale/bias/gamma OUTSIDE the apply_post guard → TWICE; a review caught the single-pass diverging at non-default gamma, so Metal now runs the faithful **2-pass** (pass1 apply_post=0→`s_filter_low` 8-bit intermediate, pass2 apply_post=1→`s_final_color`), matching GL. Validated: gamma=1 byte-exact (facility 2.8%, most levels 2.6–3.0%); gamma≠1 ~9% (intermediate-rounding tolerance, was ~60%); gate-off byte-identical (dam 4.0%); compare_state MATCH; ASan clean; RSS flat 60→600; strict-clean; GL byte-identical. Deferred (diag-only): the `GE007_DIAG_OUTPUT_VI_FILTER` downscale (filterMode 1/2/3 need a bottom-left remap — documented in code). **Next: Phase 5 (SSAO).**

## PHASE 4 — base output filter (no SSAO)

### T1. Scene-color ShaderRead
**File:** `gfx_metal.mm` `mtl_ensure_targets`. Add `MTLTextureUsageShaderRead` to `s_scene_color` usage (currently RenderTarget-only after hardening); keep `StorageModePrivate`. Depth stays as-is until T9.
- [ ] Build; default path (gate off) still blits; screenshot byte-identical to pre-change.

### T2. `FilterUniforms` struct (C + MSL)
**File:** `gfx_metal.mm`. New struct, 16-byte aligned largest-first (like `MtlUniforms`): `float3 colorTint`; `float2 srcSize, dstSize`; scalars (colorScale, colorBias, gamma, saturation, contrast, brightness, vignette, bloomThreshold, bloomIntensity, sharpen, levelSat, levelCon, ssaoRadius, ssaoIntensity, ssaoAspect, ssaoProjA, ssaoProjB); ints (applyPost, dither, bloom, ssao, filterMode, fxaa, tonemap, rgb555, fbH); pad to 16. **Do NOT reuse `MtlUniforms`.** Upload via `setFragmentBytes atIndex:1`.
- [ ] `sizeof` matches the MSL struct (offset assert or one-off print); zero warnings.

### T3. Fullscreen-triangle VS + filter fragment MSL (separate `MTLLibrary`, compiled once at init)
**File:** `gfx_metal.mm`.
- **VS:** `vertex FilterVSOut filterVS(uint vid [[vertex_id]])`; `const float2 kPos[3] = {{-1,-1},{3,-1},{-1,3}}`; `out.position = float4(kPos[vid],0,1)`; **V-flipped** `out.vTexCoord = float2(kPos[vid].x*0.5+0.5, 0.5 - kPos[vid].y*0.5)`. `vertexDescriptor=nil`, no vertex buffer.
- **FS:** port `gfx_opengl.c:2884-3091` preserving GLSL declaration order (fxLuma, sampleNearest/fitSrcToDst/fitLogical/cpuBilinear, sampleDst, fxaa, casSharpen, [SSAO in T11], `fragment float4 filterFragment(...)`). MSL free functions can't see globals — **thread `(texture2d<float> uTex, sampler colSmp, constant FilterUniforms& u)` into every color helper.**
- **Vocab:** `texelFetch(uTex,p,0)`→`uTex.read(uint2(p))` **keeping every existing clamp** (`clamp(p,int2(0),int2(u.srcSize)-1)`); `texture(uTex,uv)`→`uTex.sample(colSmp,uv)`; `ivec2`→`int2`; `const` arrays→`constant float kBayer4[16]` / `constant float2 kSsaoDir[8]`.
- **Orientation (HIGH-RISK — three distinct conventions in one shader):**
  - `sampleDst`/`fxaa`/`casSharpen`: use **`in.position.xy` UNFLIPPED** (Metal top-left). Do NOT reuse the scene shaders' `fbH - position.y` flip — scene color AND output are both top-left in Metal, so it's self-consistent (as GL's bottom-up self-consistency was).
  - bloom + SSAO-depth: use the **V-flipped `vTexCoord`** (color-uv == depth-uv spatially — load-bearing).
  - Bayer dither + RGB555: reconstruct the **GL bottom-left** pixel index for pixel-exact pattern parity: `dy = int(floor(float(u.fbH) - in.position.y)) & 3`, `dx = int(floor(in.position.x)) & 3`.
- Consider `MTLCompileOptions.fastMathEnabled = NO` on this library for bit-exact rgb555/dither vs GL goldens.
- [ ] Library compiles; zero warnings.

### T4. Dedicated filter PSO (cached; distinct from `mtl_pso_for`)
**File:** `gfx_metal.mm`. `vertexFunction=filterVS`, `fragmentFunction=filterFragment`, `vertexDescriptor=nil`, `depthAttachmentPixelFormat=Invalid`, no depth-stencil, `blendingEnabled=NO`, `rasterSampleCount=1`, `colorAttachments[0].pixelFormat=BGRA8Unorm`. Two samplers: linear/clampToEdge (color+bloom; matches GL `filter_copy_tex` LINEAR/CLAMP) and nearest/clampToEdge (depth, T11).
- [ ] PSO builds without assert.

### T5. Filter-target lifecycle
**File:** `gfx_metal.mm`. Static `s_filter_low` (+ cached w/h), lazy-alloc (BGRA8Unorm, `RenderTarget|ShaderRead`, Private), realloc on `(filter_w,filter_h)` change, invalidated with the `s_scene_color` resize. Defer `s_filter_logical` unless `GE007_DIAG_OUTPUT_VI_LOGICAL_SIZE`. Single-pass case allocates nothing.
- [ ] No per-frame allocation churn (alloc count stable across frames).

### T6. Gate helper + `mtl_end_frame` restructure
**File:** `gfx_metal.mm` `mtl_end_frame`. Gate mirrors `gfx_opengl.c:3287-3298`: `use_vi_filter | color_adjust | (bloom&&RemasterFX) | fxaa | tonemap | sharpen`. All false → **keep the existing blit**. ON → after scene `endEncoding` + acquire drawable, run the chain: final pass = render pass with `colorAttachments[0].texture = s_drawable.texture`, `loadAction=DontCare`, `storeAction=Store`, filter PSO, `drawPrimitives(triangle, 0, 3)`. Single pass when `filter_w==width`; two passes otherwise (post/SSAO on the FINAL pass only). **Preserve** the drawable-size guard and the `addCompletedHandler` semaphore-signal on the final committed cmdbuf in BOTH branches.
- [ ] Gate-off byte-identical (blit); gate-on renders filtered output; long-run stable (no deadlock; RSS flat).

### T7. Phase-4 parity gate
Freeze a full-post preset (bloom/fxaa/tonemap/grade/dither/sharpen ON, **SSAO OFF, MSAA=0, RenderScale=1**). GL vs Metal screenshot. **Acceptance:** GL byte-identical when all off; Metal filtered output within a defined per-golden `--max-changed-pct` (dither/rgb555 normalized or compared on the RGB555 grid); ASan clean; cross-backend `compare_state` MATCH; zero warnings.

---

## PHASE 5 — SSAO (the payoff)

*(Task 0/T8 depth-convention fix is DELETED — see the boxed correction. `ssaoLinZ` is reused verbatim.)*

### T9. Depth target ShaderRead
**File:** `gfx_metal.mm` `mtl_ensure_targets`. `dd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead`; keep Private. Depth `storeAction=Store` + `clearDepth=1.0` are already correct — update the stale "Phase 5" comment. No encoder change.
- [ ] Build; depth still tests correctly (parity unchanged); zero warnings.

### T10. SSAO externs + per-frame reset
**File:** `gfx_metal.mm`. Add externs: `int g_pcRemasterFX, g_pcSsao; float g_pcSsaoRadius, g_pcSsaoIntensity, g_pc_ssao_proj_a, g_pc_ssao_proj_b`. **Do NOT** add proj_x/proj_y/SsaoPower/SsaoBlur (dead P1a leftovers). In `mtl_start_frame` before draws, reset `g_pc_ssao_proj_b = g_pc_ssao_proj_x = g_pc_ssao_proj_y = 0.0f` (mirror `gfx_opengl_start_frame`) — the frontend `gfx_sp_matrix` only ever *raises* proj_b, so without the reset the `proj_b != 0` gate latches ON forever (stale coefficients on menu/HUD frames).
- [ ] Build; extern types match; zero warnings.

### T11. Port SSAO into the filter FS (extends T3)
**File:** `gfx_metal.mm`. Add `ssaoLinZ` (`gfx_opengl.c:2994`), `kSsaoDir[8]`, `ssaoAO` (`:2997-3018`), and the main-block application (`:3020-3024`). Declare `depth2d<float> uDepthTex [[texture(1)]]`; `texture(uDepthTex,uv).r`→`uDepthTex.sample(depSmp,uv)` (**drop `.r`**). Thread `(depth2d<float> uDepthTex, sampler depSmp, constant FilterUniforms& u)` into `ssaoAO`/`ssaoLinZ`. Sample depth with the **same V-flipped `vTexCoord`** as color. Keep `dir.x /= max(uSsaoAspect,0.001)` and the `>= 0.99999` sky early-out verbatim.
- [ ] Library compiles; zero warnings.

### T12. SSAO uniforms + gate
**File:** `gfx_metal.mm`. In the filter uniform upload (matching `gfx_opengl.c:3201-3219`): gate `uSsao = (g_pcRemasterFX && g_pcSsao != 0 && g_pc_ssao_proj_b != 0.0f)` — **no MSAA clause** (Metal is single-sample here; `g_scene_depth_valid` is GL-only). `uSsaoRadius = g_pcSsaoRadius * 0.02f` (the 0.02 UV-scale is load-bearing), `uSsaoIntensity = g_pcSsaoIntensity`, `uSsaoAspect = s_fb_w/(float)s_fb_h` (SCENE aspect — even in two-pass where color src is `s_filter_low`, depth is full-res), `uSsaoProjA/B = g_pc_ssao_proj_a/b`. Bind `s_scene_depth` at fragment `texture(1)` with the nearest/clamp sampler in the `apply_post=1` pass.
- [ ] Build; zero warnings.

### T13. Phase-5 SSAO-only parity gate
Frozen preset: `RemasterFX=1, Ssao=1` (fixed Radius/Intensity), **everything else off** (bloom/fxaa/tonemap/grade/dither/sharpen off, VI/logical off, rgb555 off, identity color-scale/bias/gamma/sat/con/bright, vignette 0), **`Video.MSAA=0`** — a single-pass SSAO-only chain on both backends so a pixel diff attributes cleanly to the reconstruction math. **Acceptance:** **no GPU op-hang** (the whole point — GL-over-Metal hangs here; native Metal must not); SSAO visually comparable to GL within budget; ASan clean; sim-hash unchanged; cross-backend `compare_state` MATCH; zero warnings. **⚠ Watch the stale `Video.MSAA=2` in the CWD `ge007.ini`** — it silently disables GL-side SSAO and fakes divergence; verify effective MSAA at runtime first (per [[mgb64-ssao-quality-blocker]]).

---

## Ranked risks (workflow-derived, corrected)
1. ~~[CRITICAL] Depth-convention/clip-Z~~ — **REFUTED** (frontend already remaps; see box). No action.
2. **[HIGH] Filter orientation — three conventions in one shader.** `sampleDst/fxaa/sharpen` UNFLIPPED `in.position`; `vTexCoord` V-flipped in the VS (bloom + SSAO-depth); Bayer/rgb555 use `fbH - position.y`. Reusing the scene flip on `sampleDst` mirrors the output. *Mitigation:* the T3 rules; validate with a top/bottom-asymmetric scene (HUD) golden diff.
3. **[HIGH] Multi-pass target lifecycle.** Final pass must *replace* the blit (else double-write/wasted filter); semaphore signal on the FINAL committed cmdbuf; no per-frame target churn. *Mitigation:* T5 lazy-alloc + T6 branch preserving present/semaphore ordering; single-pass allocates nothing.
4. **[MED] Dither/rgb555 bit-exactness.** MSL fast-math can reorder quantize/dither boundaries (±1 LSB vs GL). *Mitigation:* `fastMathEnabled=NO` on the filter library, or compare on the RGB555 grid.
5. **[MED] proj_b latch.** No per-frame reset → SSAO can't re-disable on menu frames / uses stale coefficients. *Mitigation:* T10 reset in `mtl_start_frame`.
6. **[MED] `FilterUniforms` alignment** (`float3` vs MSL). *Mitigation:* largest-first + `sizeof`/offset assert.
7. **[LOW/FUTURE] MSAA-depth-resolve win.** Metal *can* run SSAO under MSAA (native depth resolve) where GL cannot — but freeze `MSAA=0` for parity now. When MSAA is later added: a single-sample `resolveTexture` (ShaderRead), `storeAction=StoreAndMultisampleResolve`, `depthResolveFilter`. Removes the GL "SSAO off under MSAA" limitation (`gfx_opengl.c:2255-2265`) — a genuine Metal-only upgrade for a follow-up.

## Ordering & go/no-go
Phase 4 (T1→T7) first — base filter, SSAO off. Then Phase 5 (T9→T13). **Go/no-go = T13:** SSAO renders on Metal **without the op-hang** and is visually comparable to the intended look. That unblock is the entire justification for the backend; if it renders clean, the project's thesis is proven and the remaining work is polish/perf. Faithful-base (Phases 1–3) already shipped and is hardened, so Phases 4–5 are strictly additive behind the remaster gate.
