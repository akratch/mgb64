# W3 — Advanced Rendering & Presentation (Metal-first)

**Workstream of the MGB64 AAA Remaster program. Branch base: `feat/metal-backend`.**
**Constitution: [docs/REMASTER_ROADMAP.md](../REMASTER_ROADMAP.md) §1 rails R1/R2/R3 — every task below states its flag gate and rail compliance.**

---

## 1. Executive summary

The native Metal backend (`src/platform/fast3d/gfx_metal.mm`, selected at the single
seam `gfx_pc.c:22894`) removed the one hard blocker in the presentation stack: Apple's
GL-over-Metal translator op-hangs on per-sample view-position reconstruction math
(`docs/METAL_BACKEND_PLAN.md:39` — the hang is the *math*, not the depth-texture read),
which killed reference-quality SSAO on macOS. Native Metal already runs the planar SSAO
v1 and the full filter chain clean across all 18 solo levels. W3 cashes that in: a real
hemisphere SSAO v2 with view-space reconstruction, bilateral blur and half-res; a
modern-AA upgrade (SMAA, with an honest time-boxed TAA spike); HDR/EDR output on macOS;
AAA-viable HiDPI; SSAO-under-MSAA via Metal depth resolve; dynamic resolution scaling;
a 120 Hz-present + view-interpolation verdict; and the GPU profiling substrate that
gates all of it. Everything is flag-gated, default-identity, render-only (R1 by
construction — all work lives in the screen-space output pass or the presentation layer).

| # | Headline deliverable | Flag gate | Why it reads "AAA" |
|---|---|---|---|
| 1 | **SSAO v2** — hemisphere AO, view-pos reconstruction, bilateral blur, half-res | `Video.SsaoMode=hemisphere` (+ existing `Video.Ssao*` knobs) | Contact shadows that stick to geometry instead of a depth-delta wash — the single biggest modern cue the engine can show |
| 2 | **SMAA 1x** replacing FXAA (TAA spike verdict included) | `Video.Smaa` | Clean gradient edges without FXAA's texture smear at 1080p |
| 3 | **HDR/EDR output** on macOS (float chain + EDR drawable) | `Video.HdrOutput` | Bloom highlights that actually glow above SDR white on XDR/EDR panels |
| 4 | **HiDPI + SSAO-under-MSAA + Metal vsync control** | `Video.HiDPI` / `Video.MSAA` / `Video.VSync` | Retina-native crispness; the GL "SSAO xor MSAA" limitation dies |
| 5 | **Dynamic resolution + Metal GPU profiling lane** | `Video.DynamicResolution` / `GE007_METAL_GPU_TRACE` | Locked frame-rate under load; every other row gets an enforceable GPU-ms budget |

---

## 2. Current state (verified in code, 2026-07-02)

### 2.1 The Metal filter chain (where all W3 GPU work lands)

- **Offscreen targets** — `mtl_ensure_targets` (`gfx_metal.mm:797-865`): scene color
  `BGRA8Unorm` + depth `Depth32Float`, both `RenderTarget|ShaderRead`
  (`gfx_metal.mm:809,818`); XLU/RDP snapshot tex; `s_final_color` (presented + readback)
  and `s_filter_low` (8-bit intermediate of the 2-pass filter) (`gfx_metal.mm:725-731`).
  The drawable is forced to the render resolution (`s_layer.drawableSize = render size`,
  `gfx_metal.mm:864`) — the compositor does the SSAA downsample.
- **Filter program** — `mtl_ensure_filter_program` (`gfx_metal.mm:1205-1368`) builds one
  monolithic MSL fragment (FXAA, CAS, bloom, grade, tonemap, gamma, vignette, Bayer
  dither, RGB555) compiled with `MTLMathModeSafe` for quantize-boundary parity with GL
  (`gfx_metal.mm:1330-1335`). `mtl_run_output_filter` (`gfx_metal.mm:1433-1458`) runs it
  as GL's two passes: scene → `s_filter_low` (apply_post=0, gamma/scale/bias only) →
  `s_final_color` (apply_post=1, full FX), matching GL's double-gamma with an 8-bit
  round between. Uniforms: `MtlFilterUniforms`, 144 bytes (`gfx_metal.mm:1134-1143`).
- **SSAO v1 (planar)** — MSL at `gfx_metal.mm:1275-1286`: 8 fixed screen-space
  directions × 2 steps, linearized via `ssaoLinZ(d) = projB / (projA + 2d - 1)`
  (`gfx_metal.mm:1276`), occluder accepted in the `(1.5%, 12%)·cz` band — a byte-level
  port of the GL shader (`gfx_opengl.c:2990-3018`). Composite: `color.rgb *= 1 -
  intensity·AO` before FXAA (`gfx_metal.mm:1296`). Gate: final pass && `g_pcRemasterFX
  && g_pcSsao && g_pc_ssao_proj_b != 0` (`gfx_metal.mm:1403`); radius key → UV scale
  ×0.02 (`gfx_metal.mm:1404`). Depth sampler is NEAREST/clamp (`gfx_metal.mm:1059-1065`).
- **Per-frame lifecycle** — `mtl_start_frame` resets **only** `g_pc_ssao_proj_b`
  (`gfx_metal.mm:1083`); GL's start-frame also resets `proj_x/proj_y`
  (`gfx_opengl.c:3552-3554`) — Metal must too once v2 consumes them. Triple-buffered
  ring vertex arena + semaphore throttle (`gfx_metal.mm:760-767,1102`). `mtl_end_frame`
  runs the filter iff `mtl_output_filter_active()` (`gfx_metal.mm:1178-1201`), blits the
  presented texture into the drawable and presents (`gfx_metal.mm:1491-1507`). Readback
  returns GL-convention bottom-up RGB from BGRA8 (`gfx_metal.mm:1846-1918`) — it
  hard-assumes 4-byte texels (`gfx_metal.mm:1906-1915`).
- **PSOs are already sample-count-keyed** — `mtl_pso_for(ms, blend, samples,
  write_alpha)` (`gfx_metal.mm:901-905`) but every draw passes `samples=1`
  (`gfx_metal.mm:1763`); depth format is pinned `Depth32Float` (`gfx_metal.mm:921`).
  MSAA on Metal is scaffolded but not wired.

### 2.2 The projection plumbing (v2's raw material — already shipped)

`gfx_sp_matrix` captures, per scene frame, from the perspective projection with the
largest `|B|` (the main world view): `g_pc_ssao_proj_a = P[2][2]`, `proj_b = P[3][2]`,
**`proj_x = P[0][0]`, `proj_y = P[1][1]`** (`gfx_pc.c:15384-15387` definitions,
`15442-15452` capture; perspective test `|P[2][3]| > 0.5`). x/y are captured but
**consumed by nothing today** — they exist precisely for view-ray reconstruction.

### 2.3 SSAO v2 knobs: registered, plumbed, dead

`platform_sdl.c:1617-1655` registers `Video.SsaoBias` (0.03), `SsaoPower` (1.6),
`SsaoFarCutoff` (128), `SsaoNearCut` (0.02), `SsaoSkyCut` (0.9999), `SsaoHalfRes` (0),
`SsaoBlur` (0), `SsaoBlurDepthSharp` (8.0); definitions at `platform_sdl.c:205-212`.
They are extern'd in `gfx_opengl.c:53-60` but **no shader reads them** (leftover from
the GL-era P1a plan that the hang killed, `docs/REMASTER_PHASE1_PLAN.md`). W3 wires
them to real code instead of inventing new keys.

### 2.4 GL parity state

GL SSAO gates on `g_scene_depth_valid` and **disables under MSAA** with a one-time warn
(`gfx_opengl.c:2248-2262`); the depth texture is `gfx_opengl.c:650` (NEAREST, `:2328-2329`).
The uniform upload (`gfx_opengl.c:3205-3223`) is deliberately in lockstep with Metal's
gate. GL remains default everywhere and byte-identical (`gfx_backend.c:12` env-cached
`GE007_RENDERER`; `--remaster` sets it to `metal` on macOS, `main_pc.c:553-560`).

### 2.5 Presentation & timing

- **HiDPI** — `Video.HiDPI` default **0** (`platform_sdl.c:1515-1518`); when 1,
  `SDL_WINDOW_ALLOW_HIGHDPI` is added (`platform_sdl.c:2186-2188`). **Bug on Metal:**
  `gfx_sync_current_dimensions_from_window` sizes the render from
  `SDL_GL_GetDrawableSize` unconditionally (`gfx_pc.c:4417`) — on a `SDL_WINDOW_METAL`
  window (`platform_sdl.c:2181-2183`) that call has no GL context to consult; the
  screenshot path already branches to `SDL_Metal_GetDrawableSize`
  (`platform_sdl.c:605-609`) but the renderer sizing does not. HiDPI on Metal is
  therefore unplumbed.
- **VSync** — `Video.VSync` off/on/adaptive, default adaptive (`platform_sdl.c:252,
  1542-1546`), applied via `SDL_GL_SetSwapInterval` only (`platformApplyVSync`,
  `platform_sdl.c:1447-1468`; focus-lost fallback `:2313-2315`). **On Metal these are
  no-ops** — nothing touches `CAMetalLayer.displaySyncEnabled`, so Metal always
  presents vsynced (layer default) and `GE007_NO_VSYNC`/`Video.VSync=off` silently do
  nothing for benchmarking.
- **Frame cap** — `Video.FrameCap` ∈ {30, 60, display} (`platform_sdl.c:259-269,
  1549-1553`); "display" removes the `SDL_Delay` **only while vsync is active** —
  with `GE007_NO_VSYNC`/`VSync=off` it falls back to the 60 Hz delay
  (`platformFrameDelayMs`, `platform_sdl.c:1470-1484`). But the **sim loop itself is hard-capped at 60 Hz**:
  `waitForNextFrame` spins `osGetCount` until ≥ 1/60 s elapsed (`src/game/unk_0C0A70.c:
  199-212`, divisor 775875 NTSC) and feeds `speedgraphframes = whole 60 Hz frames
  elapsed` (`unk_0C0A70.c:151`), which becomes `g_ClockTimer` with the PC 1–4 clamp and
  the RAMROM bypass (`src/game/lvl.c:2056-2084`). **Uncapping presentation alone can
  never exceed 60 rendered sim states/sec.**
- **Perf tooling** — `tools/perf_census.sh` measures **CPU** `work_ms` from
  `GE007_PERF_TRACE` (`platform_sdl.c:2483-2607`), two configs (default/xluoff), GL
  only, no GPU timing, no renderer dimension. `docs/REMASTER_PHASE1_PLAN.md` already
  concluded `work_ms` is blind to GPU fill and demanded a GPU timer as the real gate.
  No `os_signpost`, no `MTLCaptureManager`, no GPU timestamps exist in the tree.

---

## 3. Target state — the AAA bar

A reviewer on an M-series MacBook Pro (ProMotion XDR panel) runs
`./build/ge007 --remaster --level jungle` and sees:

1. **Grounded lighting** — crates, pipes, foliage and wall bases show soft, stable
   contact occlusion that hugs corners and disappears on open flat ground; no boiling
   during a slow pan (per-pixel luma delta ≤ 2/255 on static patches), no dark halo
   around silhouettes, no far-field speckle on the receding valley floor.
2. **Clean edges** — geometric edges resolve without FXAA's crawling/texture blur;
   sub-pixel wire geometry (fences, girders on Depot) stays coherent.
3. **Retina-native output** — with `Video.HiDPI=1` the image is pixel-mapped to the
   panel (no compositor upscale softness); UI/HUD text is crisp at 254 ppi.
4. **Real highlights** — with `Video.HdrOutput=1` on an EDR display, muzzle flashes,
   explosion cores and the Dam floodlights exceed SDR white (verifiably: EDR headroom
   consumed > 1.0) with no clipping band; SDR displays are untouched.
5. **Locked smoothness** — with `Video.DynamicResolution=1`, worst-case scenes hold the
   16.6 ms present budget by scaling internal resolution (never below 0.5×), invisible
   at a glance; the HUD/filter chain stays full-res.
6. **A settled 120 Hz verdict** — presentation can run unsynced/ProMotion-paced today;
   the doc's spike gives a GO/NO-GO with evidence on true render-time view
   interpolation (sim stays bit-exact 60 Hz either way, RAMROM-verified).
7. **Every one of the above off ⇒ byte-identical faithful port** (R3), and the GL
   default path is untouched on all platforms.

---

## 4. Technical design

### 4.1 SSAO v2 — hemisphere AO with view-space reconstruction (Metal)

#### 4.1.1 Why v1 fails and what v2 changes

v1 compares linearized depths along 8 fixed *screen* directions: it cannot tell a
receding flat floor from a crease (no surface orientation), so thresholds were tuned to
a narrow band (1.5–12% of view distance, `gfx_metal.mm:1285`) and the effect reads as a
faint wash. v2 reconstructs the view-space position and normal per pixel, integrates a
cosine hemisphere above the surface, and blurs bilaterally — the standard
Crysis/HBAO-class formulation the GL translator could not run
(`docs/METAL_BACKEND_PLAN.md:39`: the reconstruction math itself hangs GL-over-Metal;
the color-encode dodge and `textureLod` were both tested and ruled out).

#### 4.1.2 Architecture — SSAO becomes a real pass chain

Today SSAO is fused inside the final filter pass. v2 restructures (Metal only):

```
scene depth (Depth32Float, exists)
   └─ PASS A  ssao_raw   : hemisphere AO  → s_ssao_raw   (R8Unorm, full or half res)
   └─ PASS B1 ssao_blurX : bilateral blur → s_ssao_tmp   (R8Unorm)   [Video.SsaoBlur=1]
   └─ PASS B2 ssao_blurY : bilateral blur → s_ssao_raw   (ping-pong back)
   └─ existing final filter pass samples s_ssao_raw as texture(2):
        color.rgb *= 1.0 - u.ssaoIntensity * (1.0 - aoTex)      [replaces inline ssaoAO()]
```

New statics in `gfx_metal.mm` beside `s_filter_low` (`:729`): `s_ssao_raw`,
`s_ssao_tmp` (`R8Unorm`, `RenderTarget|ShaderRead`, private, allocated in
`mtl_ensure_targets` at `w>>hr, h>>hr` where `hr = g_pcSsaoHalfRes ? 1 : 0`);
`s_ssao_pso`, `s_ssao_blur_pso`, one shared `MtlSsaoUniforms`.

```c
/* C-side uniform block (16-byte aligned, 64 bytes) — gfx_metal.mm */
struct MtlSsaoUniforms {
    float projA, projB, projX, projY;      /* g_pc_ssao_proj_* (gfx_pc.c:15384-15387) */
    float radius;        /* view-space units: g_pcSsaoRadius * kRadiusWorldScale       */
    float bias;          /* g_pcSsaoBias        (platform_sdl.c:205)                   */
    float power;         /* g_pcSsaoPower       (platform_sdl.c:206)                   */
    float farCutoff;     /* g_pcSsaoFarCutoff   (platform_sdl.c:207)                   */
    float nearCut, skyCut;                 /* platform_sdl.c:208-209                   */
    float blurDepthSharp;                  /* platform_sdl.c:212                       */
    float aoSizeW, aoSizeH;                /* AO target size (half-res aware)          */
    int   frameParity;   /* reserved 0 — no temporal accumulation in v2               */
    int   blurAxis;      /* 0=X pass, 1=Y pass                                        */
    int   _pad;
};
```

#### 4.1.3 PASS A — MSL sketch (the generator emits this into a new
`mtl_ensure_ssao_program()`, structured exactly like `mtl_ensure_filter_program`)

```metal
// View-space position from UV + window depth. The frontend's z=(z+w)/2 remap makes
// Metal window depth == GL window depth (proven for v1, gfx_metal.mm:1273-1274), so
// the GL linearization is reused verbatim. z is POSITIVE view distance.
static float3 viewPos(float2 uv, float d, constant SsaoUniforms& u) {
    float z = u.projB / (u.projA + 2.0 * d - 1.0);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);   // top-left UV -> GL NDC
    return float3(ndc.x * z / u.projX, ndc.y * z / u.projY, z); // THE op that hung GL
}
// Face normal from depth: least-slope neighbor pair kills the 1px edge ring that a
// naive dfdx/dfdy cross produces at depth discontinuities.
static float3 viewNormal(depth2d<float> dep, sampler s, float2 uv, float2 px,
                         float3 P, constant SsaoUniforms& u) {
    float3 Pr = viewPos(uv + float2(px.x, 0), dep.sample(s, uv + float2(px.x, 0)), u);
    float3 Pl = viewPos(uv - float2(px.x, 0), dep.sample(s, uv - float2(px.x, 0)), u);
    float3 Pd = viewPos(uv + float2(0, px.y), dep.sample(s, uv + float2(0, px.y)), u);
    float3 Pu = viewPos(uv - float2(0, px.y), dep.sample(s, uv - float2(0, px.y)), u);
    float3 dx = (abs(Pr.z - P.z) < abs(P.z - Pl.z)) ? Pr - P : P - Pl;
    float3 dy = (abs(Pd.z - P.z) < abs(P.z - Pu.z)) ? Pd - P : P - Pu;
    return normalize(cross(dy, dx));
}
// Interleaved gradient noise: per-pixel kernel rotation with NO noise texture (keeps
// the pass resource-free and deterministic — screenshot-gate friendly).
static float ign(float2 p) { return fract(52.9829189 * fract(0.06711056*p.x + 0.00583715*p.y)); }

constant float3 kKernel[12] = { /* 12 cosine-weighted hemisphere dirs, scale_i =
    mix(0.1, 1.0, (i/12)^2) — generated once offline, first-party math, Tier A1 */ };

fragment float4 ssaoFragment(FVO in [[stage_in]],
                             constant SsaoUniforms& u [[buffer(1)]],
                             depth2d<float> dep [[texture(0)]], sampler ds [[sampler(0)]]) {
    float2 uv = in.vTexCoord;
    float d  = dep.sample(ds, uv);
    if (d <= u.nearCut || d >= u.skyCut) return float4(1.0);        // viewmodel / sky
    float3 P = viewPos(uv, d, u);
    if (P.z >= u.farCutoff) return float4(1.0);                     // noisy-depth zone
    float2 px = 1.0 / float2(u.aoSizeW, u.aoSizeH);
    float3 N = viewNormal(dep, ds, uv, px, P, u);
    float a = ign(in.position.xy) * 6.2831853;                       // rotate kernel
    float3 T = normalize(float3(cos(a), sin(a), 0) - N * dot(float3(cos(a), sin(a), 0), N));
    float3x3 TBN = float3x3(T, cross(N, T), N);
    float fade = 1.0 - smoothstep(0.75 * u.farCutoff, u.farCutoff, P.z);
    float occ = 0.0;
    for (int i = 0; i < 12; ++i) {
        float3 sp = P + TBN * kKernel[i] * u.radius;                 // hemisphere sample
        // project back: sp -> NDC -> top-left UV (inverse of viewPos)
        float2 suv = float2( sp.x * u.projX / sp.z,  sp.y * u.projY / sp.z);
        suv = float2(suv.x * 0.5 + 0.5, 0.5 - suv.y * 0.5);
        float sz = u.projB / (u.projA + 2.0 * dep.sample(ds, suv) - 1.0);
        float rangeChk = smoothstep(0.0, 1.0, u.radius / max(abs(P.z - sz), 1e-4));
        occ += (sz < sp.z - u.bias * P.z) ? rangeChk : 0.0;          // depth-proportional bias
    }
    return float4(pow(saturate(1.0 - occ / 12.0), u.power) * fade + (1.0 - fade));
}
```

`u.radius` maps `Video.SsaoRadius` (preset 0.5, `platform_sdl.c:1976`) to view units via
`kRadiusWorldScale = 6.0` (tuned on Dam/Jungle so 0.5 ≈ crate-scale reach; the v1 UV
mapping `*0.02` at `gfx_metal.mm:1404` stays untouched for planar mode).

#### 4.1.4 PASS B — separable bilateral blur (ping-pong)

9-tap Gaussian per axis; weight `w_i = g_i * exp(-blurDepthSharp * |linZ_c - linZ_i| /
linZ_c)`. Samples `s_ssao_raw` + scene depth; writes `s_ssao_tmp` then back. Runs iff
`g_pcSsaoBlur` (default 0 → wire the `--remaster` preset to 1, §4.1.6). Half-res AO is
bilaterally *upsampled* implicitly by the composite's linear sampler + depth-weighted
rejection in the blur (blur runs at AO res; composite samples with the existing linear
`s_filter_smp`).

#### 4.1.5 Composite + mode switch

`MtlFilterUniforms` gains `int ssaoMode;` (0=off, 1=planar/v1, 2=hemisphere/v2 —
consumes one of the two trailing `_pad` ints (`gfx_metal.mm:1142`), so the struct
stays 144 bytes and the MSL twin needs the same field in the same slot; keep the C
mirror comment at `gfx_metal.mm:1131-1133` in sync). The final-pass fragment keeps the v1 inline path for mode 1 and samples
`s_ssao_raw` (`texture(2)`/`sampler(2)`) for mode 2. `mtl_end_frame` inserts passes
A/B between the scene encoder close and `mtl_run_output_filter` when
`ssaoMode==2 && proj_b != 0`.

**`mtl_start_frame` must also reset `g_pc_ssao_proj_x/y`** beside the `proj_b` reset
(`gfx_metal.mm:1083`), mirroring GL (`gfx_opengl.c:3552-3554`) — otherwise a
menu→gameplay transition can pair a stale x/y with a fresh a/b.

#### 4.1.6 Flags, presets, GL story (R3 + parity policy)

- New key: `Video.SsaoMode` enum `{planar=1, hemisphere=2}` default **planar** +
  `GE007_SSAO_MODE`; registered beside `Video.Ssao` in `platform_sdl.c`. The master
  gates are unchanged: `Video.RemasterFX && Video.Ssao`. All-off = byte-identical (the
  new passes don't run and the filter shader's mode-1 branch is byte-equal to today's).
- `--remaster` preset (`platform_sdl.c:1960-1977`) adds `Video.SsaoMode=hemisphere`,
  `Video.SsaoBlur=1`, `Video.SsaoHalfRes=1` — macOS-only effect since `--remaster`
  already selects Metal (`main_pc.c:553-560`).
- **GL keeps planar v1; v2 is Metal-only at launch.** Justification: (a) R3 is
  *default-identity*, not cross-backend feature parity — GL's byte-identical default is
  untouched because zero GL lines change; (b) the "land in both generators" policy
  exists to prevent *drift in shared shaders* — v2 is a new, additive shader that GL
  **cannot run on the flagship platform** (the reconstruction math is the documented
  op-hang, `docs/METAL_BACKEND_PLAN.md:39`), so a GLSL twin would ship dead code on
  macOS and untested code elsewhere; (c) a GLSL port for Linux/Windows is scoped as an
  explicit follow-up task (W3.E2.T7) hard-guarded `#ifndef __APPLE__` at the
  `output_ssao_active` seam (`gfx_opengl.c:2248`), so the generators re-converge once a
  non-Apple GL test box exists. Until then `Video.SsaoMode=hemisphere` on GL logs one
  warning and falls back to planar (explicit, not silent).

### 4.2 TAA vs SMAA — honest assessment

**Facts:** there are no motion vectors and no cheap way to make them — T&L is CPU-side
(`gfx_pc.c` transforms into clip space; the VBO carries clip-pos/uv/fog/colors only,
vertex shader is a passthrough `out.position = in.aVtxPos`, `gfx_metal.mm:439`).
Per-object velocity would need previous-frame matrices per DL draw — Track-2-scale
plumbing. Camera-only reprojection needs the frame's world→clip (P·V); we capture P
(`gfx_pc.c:15442-15452`) but **V is never isolated** — N64 DLs load combined
modelview matrices per room/object (`gfx_sp_matrix`, `gfx_pc.c:15389+`), and the
"room matrix" detection (`roomMatrixContainsAddress`, `gfx_pc.c:15398`) identifies a
camera·roomTransform product, not the camera.

**Design consequence:** a bounded TAA is *possible* only if a stable per-frame view
matrix can be extracted (e.g., capture the modelview loaded for room draws and factor
the known static room transform out). That is a research question, not a plan — so it
is a **3-day time-boxed spike (W3.E4.T4)** with a hard kill criterion, not a committed
feature. Even if it works, GoldenEye's content profile (low-poly, large flat surfaces,
already 2×SSAA + FXAA in the remaster preset, `platform_sdl.c:1964,1967`) means TAA's
marginal win over SMAA-on-2×SSAA is small, while its risks (ghosting on guards/doors
with no object velocity, softening the HD texture packs) are the classic complaints.

**Decision: ship SMAA 1x; TAA only if the spike's reprojection test passes AND a
side-by-side review prefers it.** SMAA fits the existing chain perfectly: three small
passes (edge-detect → blend-weights → neighborhood-blend), pure spatial, deterministic
(screenshot-gate friendly), two tiny LUT textures. Structure: new
`mtl_ensure_smaa_programs()` + two `RG8`/`R8` intermediates in `mtl_ensure_targets`;
runs scene→scene' *before* `mtl_run_output_filter`; the filter's FXAA branch is forced
off while `Video.Smaa=1` (mutually exclusive, one-time log). LUTs: the SMAA
area/search textures are generated **at init from the published algorithm's tables**
(Jimenez et al., MIT-style license) — **Tier A2**: per-asset review + `NOTICE` entry
before commit (R2); they are code-generated constants, never ROM-touched.

### 4.3 HDR/EDR output (macOS)

- **Flag:** `Video.HdrOutput` (default 0) + `GE007_HDR`; restart-scoped
  (`SETTING_SCOPE_RESTART` — layer pixel format is fixed at init). Metal-only; GL path
  logs-and-ignores.
- **Layer setup** (in `mtl_init`, `gfx_metal.mm:1023-1072`): `s_layer.pixelFormat =
  MTLPixelFormatRGBA16Float; s_layer.wantsExtendedDynamicRange = YES; s_layer.colorspace
  = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB);`. Per-frame headroom:
  `NSScreen.maximumExtendedDynamicRangeColorComponentValue` of the window's screen
  (bridge via a new `platformGetEdrHeadroom()` in `platform_sdl.c` beside
  `platformGetMetalLayer`, `platform_sdl.c:2291-2295`), clamped by a `Video.HdrPeak`
  knob (default 2.0, range 1.0–8.0).
- **Chain change:** the scene stays BGRA8 (the combiner is faithful 8-bit — R3). Only
  the **final** filter pass changes: it renders into a new `s_final_hdr`
  (`RGBA16Float`) with three shader edits gated on `u.hdrMode`: (1) the bloom add drops
  its `clamp(...,0,1)` (`gfx_metal.mm:1304`) so highlight energy survives; (2) after
  grading, `rgbLin = pow(rgb, 2.2); rgbOut = rgbLin * (1 + max(bloomLuma,0) *
  (u.hdrPeak - 1))` — SDR content maps 1:1, only bloom-classified energy enters the
  EDR headroom (no fake inverse-tonemap of LDR art); (3) dither/rgb555 branches are
  skipped (`u.dither/u.rgb555` forced 0 — they are 8-bit concepts). Present blits
  `s_final_hdr` → the float drawable (same-format blit, exact).
- **Readback/screenshots:** `mtl_read_framebuffer_rgb` assumes 4-byte BGRA8
  (`gfx_metal.mm:1906-1915`). In HDR mode, between-frame readback re-runs the final
  pass once into the existing SDR `s_final_color` **on demand** (only when a
  screenshot/probe is requested) with `hdrMode=0` — tooling keeps byte-stable SDR
  captures; all gates run with HDR off anyway (R3 identity is defined at flags-off).

### 4.4 HiDPI on Metal

1. **Fix the sizing seam** — `gfx_sync_current_dimensions_from_window`
   (`gfx_pc.c:4411-4477`): branch on `gfx_backend_use_metal()` and call
   `SDL_Metal_GetDrawableSize` (pattern already proven at `platform_sdl.c:605-609`).
   With `Video.HiDPI=1` this makes `gfx_current_dimensions` = backing pixels ×
   `RenderScale`; `mtl_ensure_targets` and the drawable follow automatically
   (`gfx_metal.mm:864`).
2. **Pixel-budget guard** — HiDPI(2×) × RenderScale(2×) = 4× a 1440p window ≈ 5.9K×3.3K
   render: legal (< 16384 cap, `gfx_metal.mm:1530-1532`) but slow. Add a one-time
   advisory when `drawable_px * renderScale² > 2.2×(3840×2160)` suggesting
   `Video.RenderScale=1` with HiDPI (they are redundant supersampling paths), and make
   `--remaster` leave HiDPI at the user's value (no preset pin).
3. **Input** — mouse is relative-delta (`platformGetMouseDelta`,
   `platform_sdl.c:1994`); no pixel-space coordinates cross the boundary. Verify only.

### 4.5 SSAO-under-MSAA on Metal (depth resolve — GL cannot)

GL disables SSAO whenever `Video.MSAA>0` (`gfx_opengl.c:2248-2262`). Metal removes the
limitation natively:

- `mtl_ensure_targets` gains MS twins when `g_pcMsaaSamples>0`: `s_scene_color_ms` /
  `s_scene_depth_ms` (`MTLTextureType2DMultisample`, private). Existing `s_scene_color`
  / `s_scene_depth` become the **resolve** targets.
- `mtl_open_scene_encoder` (`gfx_metal.mm:1007-1019`) attaches the MS textures with
  `storeAction = MTLStoreActionStoreAndMultisampleResolve` + `resolveTexture` on both
  color and depth; depth resolve filter `MTLMultisampleDepthResolveFilterMin` (nearest
  occluder — conservative for AO; supported on all Apple-silicon GPUs). Because every
  encoder close resolves, the mid-frame consumers — RDP/XLU snapshot blit
  (`gfx_metal.mm:1749-1758`) and mid-frame readback (`gfx_metal.mm:1857-1871`) — keep
  reading the single-sample resolve textures **unchanged**.
- Draws pass `samples = g_pcMsaaSamples>0 ? g_pcMsaaSamples : 1` into `mtl_pso_for`
  (replace the literal at `gfx_metal.mm:1763`) — the PSO cache is already keyed on it
  (`gfx_metal.mm:903`). Add `alphaToCoverageEnabled` to the PSO when MSAA>0 mirroring
  GL's A2C cutout behavior (`gfx_opengl.c:708-710`).
- SSAO (v1 and v2) then reads resolved depth with zero changes. Divergence from GL
  (which turns SSAO off) is **by design** and validated as its own case, exactly as
  `docs/METAL_BACKEND_PLAN.md:93` prescribes.

### 4.6 120 Hz+: present pacing now, interpolation as a gated spike

- **Phase A (this workstream, cheap):** wire `Video.VSync`/`GE007_NO_VSYNC` to
  `s_layer.displaySyncEnabled` (new `gfx_metal_set_display_sync(bool)` called from
  `platformApplyVSync`, `platform_sdl.c:1447`), so Metal benchmarking/capture actually
  runs unsynced and ProMotion panels self-pace. Honest framing: with sync off the loop
  still can't exceed 60 Hz (the `waitForNextFrame` spin, `unk_0C0A70.c:199-212`);
  the win is correct tooling, zero added present latency, and judder-free 60-on-120
  scanout (even-multiple pacing).
- **Phase B (5-day spike, W3.E6.T2 — sim-adjacent, R1 red zone):** render-time view
  interpolation per roadmap §5 needs (1) a pure-draw re-render path that rebuilds DLs
  without ticking sim, and (2) an isolatable camera pose (same unknown as TAA, §4.2 —
  the spikes share their matrix-capture work). Deliverable is a *verdict document* +
  prototype behind `GE007_INTERP_SPIKE`, validated by `tools/sim_invariance_gate.sh` +
  `tools/compare_state.py` (RAMROM replay hash must be bit-identical with the spike
  flag on). **Kill criteria** in §7. Full productization is explicitly deferred to its
  own workstream if GO — it is a month-scale item (roadmap §5: "week-plus,
  medium-high risk" was for a senior; junior-scale it is months).

### 4.7 Dynamic resolution scaling (Metal)

- **Flag:** `Video.DynamicResolution` (0 default) + `Video.DynResMin` (0.5) /
  `Video.DynResTargetMs` (14.0).
- **Sensor:** GPU frame time EMA from `MTLCommandBuffer.GPUEndTime - GPUStartTime` in
  the existing completed-handler (`gfx_metal.mm:1473-1479`) — no new sync points.
- **Actuator:** a quantized ladder `{1.0, 0.85, 0.7, 0.6, 0.5}` multiplying the value
  `gfx_scaled_dimension` returns (`gfx_pc.c:4404-4409`) — one new render-only global
  `g_pc_dynres_scale` written by the Metal backend, read in `gfx_pc.c`. Hysteresis:
  step down when EMA > target for 10 frames, up when < 0.8×target for 60 frames.
  Quantization bounds `mtl_ensure_targets` reallocation to ≤ 5 sizes; targets for
  previously-seen rungs are kept in a tiny cache (5 entries) to make rung changes
  alloc-free after first use.
- **Safety:** render-bookkeeping only (same class as RenderScale — `gfx_current_
  dimensions` already feeds culling, covered by the P0.2 stance that culling deltas
  are gameplay-neutral; still run the sim gate once with DRS forced to oscillate).
  All screenshot/parity harnesses pin `Video.DynamicResolution=0` (add to the identity
  flag list in `docs/REMASTER_ROADMAP.md` §7 step 2).

### 4.8 Metal GPU profiling workflow + census lane

- **Timers:** `GE007_METAL_GPU_TRACE=1` prints per-frame
  `[PERF-GPU] frame=N total_ms=X scene_ms=Y post_ms=Z` — total from
  `GPUStart/EndTime`; scene/post split via `MTLCounterSampleBuffer` timestamp sampling
  at encoder boundaries (`MTLCommonCounterSetTimestamp`, supported on Apple silicon;
  fall back to total-only if `counterSets` lacks it). Wrap passes in `os_signpost`
  intervals (`OS_SIGNPOST_INTERVAL_BEGIN/END`, subsystem `com.mgb64.metal`) so
  Instruments' Metal System Trace labels them.
- **Capture:** `GE007_METAL_CAPTURE=<frame>` triggers a programmatic
  `MTLCaptureManager` capture of that frame to `mgb64_frame<N>.gputrace` (open in
  Xcode). Document the interactive path too: Xcode → Debug → Capture GPU Workload on
  the running process.
- **Census lane:** `tools/perf_census.sh` gains `RENDERERS="gl metal"` (default `gl`;
  macOS adds `metal`): per renderer it sets `GE007_RENDERER`, adds `GE007_METAL_GPU_
  TRACE=1`, and emits extra CSV columns `metal_ms,metal_fps,metal_gpu_ms`. A new
  `tools/gpu_budget_gate.sh` asserts the W3 standing budget: **(scene+post) GPU delta
  of any W3 feature, on-minus-off, < 2.0 ms @1080p-class and < 4.0 ms @4K-class on
  jungle + dam** (numbers inherited from `docs/REMASTER_PHASE1_PLAN.md` G-PERF, which
  already established CPU `work_ms` is not a valid gate).

### 4.9 File-by-file change map

| File | Changes |
|---|---|
| `src/platform/fast3d/gfx_metal.mm` | SSAO v2 pass programs + targets + uniforms (§4.1); `proj_x/y` reset in `mtl_start_frame`; SMAA passes; MSAA targets/resolve + PSO sample wiring + A2C; HDR layer/final-pass/readback; DRS sensor/ladder; GPU timers/signposts/capture; `gfx_metal_set_display_sync` |
| `src/platform/fast3d/gfx_opengl.c` | `Video.SsaoMode=hemisphere` → warn-and-fallback (one `if` at `:2248`); **no other GL change** (T7 later adds the non-Apple GLSL v2) |
| `src/platform/fast3d/gfx_pc.c` | Metal branch in `gfx_sync_current_dimensions_from_window` (`:4417`); `g_pc_dynres_scale` factor in `gfx_scaled_dimension` (`:4404`) |
| `src/platform/platform_sdl.c` | Register `Video.SsaoMode/Smaa/HdrOutput/HdrPeak/DynamicResolution/DynResMin/DynResTargetMs`; wire `platformApplyVSync` to Metal; `platformGetEdrHeadroom`; `--remaster` preset rows (`:1960`) |
| `src/platform/main_pc.c` | `--remaster` help text notes SsaoMode/HDR (selection logic unchanged, `:553-560`) |
| `tools/perf_census.sh`, new `tools/gpu_budget_gate.sh`, `tools/ssao_gate.sh` | §4.8 census lane; GPU budget gate; SSAO wash/far/temporal gate runner (§8) |
| `docs/VISUAL_MODES.md` | New flag rows |

---

## 5. Work breakdown

Estimates are **junior-engineer-days** (include validation loops, which dominate).
"Identity gate" = the §8 byte-identical A/B; "GPU budget" = §4.8 gate. Rails column:
R1 = render-only unless noted; R2 = tier of any asset; R3 = the flag.

### E1 — Profiling substrate (do first: it gates everything)

| ID | Task | Files | Steps | Acceptance (runnable) | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W3.E1.T1 | GPU timers + signposts | gfx_metal.mm | Completed-handler `GPUStart/EndTime`; counter-sample encoder split w/ fallback; `os_signpost` per pass | `GE007_METAL_GPU_TRACE=1 GE007_RENDERER=metal build/ge007 --level jungle --deterministic` + auto-exit prints ≥100 `[PERF-GPU]` lines with `scene_ms+post_ms ≤ total_ms+0.2`; Instruments shows named intervals | 4 | — | R3: diag env, off by default; R1: read-only |
| W3.E1.T2 | Programmatic GPU capture | gfx_metal.mm | `MTLCaptureManager` capture at `GE007_METAL_CAPTURE=<frame>`; doc the Xcode flow in this file's §appendix | Env run emits `mgb64_frame120.gputrace` that opens in Xcode with labeled passes | 2 | E1.T1 | R3: diag env |
| W3.E1.T3 | Metal census lane + budget gate | perf_census.sh, gpu_budget_gate.sh | `RENDERERS` loop; CSV columns; gate script (on/off delta vs budget) | `RENDERERS="gl metal" tools/perf_census.sh jungle dam` writes both column sets; `tools/gpu_budget_gate.sh --feature Video.Ssao` exits 0 on v1 today | 3 | E1.T1 | R1: measurement only |

### E2 — SSAO v2 (the unblocked prize)

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W3.E2.T1 | Mode plumbing + resets | platform_sdl.c, gfx_metal.mm, gfx_opengl.c | Register `Video.SsaoMode`+`GE007_SSAO_MODE`; reset `proj_x/y` in `mtl_start_frame` (`:1083`); GL warn-fallback at `gfx_opengl.c:2248`; preset rows | Identity gate green (mode registered, default planar); GL run with `SsaoMode=hemisphere` logs exactly one warn and renders v1 | 3 | — | R3: `Video.SsaoMode` default planar |
| W3.E2.T2 | PASS A hemisphere AO | gfx_metal.mm | §4.1.3 program builder + `s_ssao_raw`; `GE007_SSAO_DEBUG=1` composites `vec3(ao)` | Debug capture on Dam pad: flat mid-ground AO ≥ 247/255 (≤8 occlusion), crease/under-prop ≤ 215/255 (≥40); far/grazing valley floor ≥ 247/255, no speckle band (gates lifted from `docs/REMASTER_PHASE1_PLAN.md:101-102`); run `tools/ssao_gate.sh dam jungle facility` | 8 | E2.T1 | R3: mode=hemisphere only; R1: output pass only |
| W3.E2.T3 | PASS B bilateral blur + half-res | gfx_metal.mm | `s_ssao_tmp` ping-pong; separable depth-weighted blur; `g_pcSsaoHalfRes` sizing; wire `SsaoBlur/BlurDepthSharp/HalfRes` (`platform_sdl.c:1642-1655`) | Temporal gate: slow-pan consecutive-frame luma delta ≤2/255 on static ground (`REMASTER_PHASE1_PLAN.md:122` gate); half-res on = no visible edge crawl vs full-res at 1080p at arm's length (reviewer A/B); GPU budget: half+blur ≤ full-res-no-blur | 5 | E2.T2 | R3: `Video.SsaoBlur/SsaoHalfRes` |
| W3.E2.T4 | Composite + knob wiring + tuning | gfx_metal.mm | `ssaoMode` in filter uniforms; sample `s_ssao_raw`; wire Bias/Power/Far/Near/SkyCut; tune on dam/jungle/facility/depot | All E2.T2 gates re-pass post-blur; `--remaster` boots hemisphere+blur+halfres; side-by-side v1/v2 captured for review | 6 | E2.T3 | R3 |
| W3.E2.T5 | Validation battery | tools/ssao_gate.sh | Script the §8 battery incl. 18-level `--remaster` boot loop, ASan, sim gate, budget | All §8 commands exit 0; `tools/gpu_budget_gate.sh --feature ssao_v2` <2ms@1080p / <4ms@4K on jungle+dam | 4 | E2.T4, E1.T3 | R1: sim gate proof |
| W3.E2.T6 | (follow-up, non-Apple) GLSL v2 port | gfx_opengl.c | Emit §4.1.3 in GLSL behind `#ifndef __APPLE__` runtime guard; reuse gates on a Linux box | Linux CI build green; parity capture GL-v2 vs Metal-v2 within `--max-changed-pct 4.0` | 4 | E2.T4; a Linux GPU box | R3; parity policy re-converges |

### E3 — Presentation foundations

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W3.E3.T1 | HiDPI sizing fix (Metal) | gfx_pc.c:4417 | Backend branch → `SDL_Metal_GetDrawableSize`; pixel-budget advisory | `Video.HiDPI=1 GE007_RENDERER=metal` on a retina display: `[metal] first frame: scene WxH` equals backing pixels × RenderScale; HiDPI=0 byte-identical | 3 | — | R3: `Video.HiDPI` default 0 |
| W3.E3.T2 | HiDPI perf validation | baselines/perf_census_latest.csv (results only, no code) | Census at HiDPI 0/1, RenderScale 1/2 | `RENDERERS=metal tools/perf_census.sh jungle dam` @HiDPI=1,RS=1 ≥ 60 fps-equivalent on M3 Max; results recorded in the census CSV | 2 | E3.T1, E1.T3 | — |
| W3.E3.T3 | Metal MSAA + depth resolve + A2C | gfx_metal.mm | §4.5: MS targets, StoreAndMultisampleResolve, resolve-filter Min, PSO samples + alphaToCoverage | `Video.MSAA=4 Video.Ssao=1 GE007_RENDERER=metal --level dam`: SSAO renders (no GL-style disable); MSAA=0 path byte-identical to pre-task; XLU snapshot scenes (train glass) visually unchanged at MSAA=4 (`--max-changed-pct 4.0` vs MSAA=0) | 8 | — | R3: `Video.MSAA` default 0; divergence-from-GL documented |
| W3.E3.T4 | Metal vsync/`displaySyncEnabled` | gfx_metal.mm, platform_sdl.c:1447 | `gfx_metal_set_display_sync`; call from `platformApplyVSync` + focus handlers (`:2311-2315`) | `GE007_NO_VSYNC=1 GE007_RENDERER=metal` census run shows present no longer paced to 60/display Hz (interval_ms < 16 sustained when work allows — note loop still ≤60 by `waitForNextFrame`, so assert via `[PERF-GPU]` total < interval instead); default run unchanged | 3 | E1.T1 | R3: existing `Video.VSync` |

### E4 — AA upgrade

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W3.E4.T1 | SMAA LUT generation + license review | gfx_metal.mm, NOTICE | Init-time area/search texture generation from published tables; NOTICE entry | `check_no_rom_data.sh` green (no tracked images — LUTs are runtime-generated); NOTICE lists SMAA license verbatim | 2 | — | **R2: Tier A2** (open-licensed, reviewed, NOTICE) |
| W3.E4.T2 | SMAA passes | gfx_metal.mm | 3-pass chain pre-filter; `Video.Smaa` key; FXAA mutual-exclusion log | `Video.Smaa=1` A/B vs FXAA on depot fences: reviewer picks SMAA; `Smaa=0` byte-identical; GPU budget ≤1.0ms@1080p | 8 | E4.T1, E1.T3 | R3: `Video.Smaa` default 0 |
| W3.E4.T3 | SMAA validation + preset decision | tools | 18-level boot, budget, identity; decide `--remaster` FXAA→SMAA swap with captures | §8 battery green; preset decision recorded here with A/B images (local, uncommitted) | 3 | E4.T2 | R2: captures stay local (Tier B contamination rule) |
| W3.E4.T4 | TAA spike (time-boxed) | scratch branch | Extract candidate view matrix (room-matrix factoring); reprojection error harness on a scripted pan | **Verdict doc.** GO requires: static-scene reprojection error <1px for >95% px over a 60-frame pan AND no sim-hash delta. Else record KILL and close | 3 (hard box) | E2.T1 (proj plumbing) | R1: spike flag `GE007_TAA_SPIKE`; RAMROM gate mandatory |

### E5 — HDR/EDR

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W3.E5.T1 | Flag + float layer + headroom query | platform_sdl.c, gfx_metal.mm | §4.3 layer config; `platformGetEdrHeadroom`; restart-scoped key | `Video.HdrOutput=1` boots with `[metal] EDR: headroom=X.X` log on XDR; =0 byte-identical | 3 | — | R3: `Video.HdrOutput` default 0 |
| W3.E5.T2 | Float final pass + EDR mapping | gfx_metal.mm | `s_final_hdr`; unclamped bloom; §4.3 transfer; skip dither/rgb555 | On XDR: Dam floodlight/muzzle-flash pixels read >1.0 in a GPU capture; SDR content unchanged side-by-side (external display A/B) | 4 | E5.T1 | R3; R1: output pass only |
| W3.E5.T3 | SDR readback path + validation | gfx_metal.mm | On-demand SDR re-render for screenshots; battery | `--screenshot-frame` under HDR produces the SDR-identical PNG (hash == HDR-off run at same frame); ASan clean; 18-level boot | 3 | E5.T2 | R3 gates stay SDR-defined |

### E6 — Frame rate & resolution

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W3.E6.T1 | Dynamic resolution scaling | gfx_metal.mm, gfx_pc.c | §4.7 sensor/ladder/hysteresis/target-cache; add a one-line `[metal] target alloc WxH` debug log under `GE007_METAL_GPU_TRACE` (none exists today — only the failure warn at `gfx_metal.mm:844`); harness pins DRS off | Forced-load test (`Video.RenderScale=4` + DRS on, jungle): EMA converges, rung changes alloc-free after warmup (the new alloc log stops after each rung's first visit), present holds target; DRS off byte-identical; sim gate green with DRS oscillating | 8 | E1.T1 | R3: `Video.DynamicResolution` default 0; R1: sim gate run |
| W3.E6.T2 | View-interpolation spike (verdict) | scratch | §4.6 Phase B; pure-draw re-render prototype; RAMROM invariance | **Verdict doc** + `GE007_INTERP_SPIKE` prototype; `tools/sim_invariance_gate.sh` + `compare_state.py` bit-identical with flag on; GO/NO-GO per §7 kill criteria | 5 (hard box) | E3.T4, E4.T4 (shared matrix work) | **R1 red zone: RAMROM gate is the acceptance** |

**Total: 94 junior-days ≈ 18.8 junior-weeks** (90 days ≈ 18 weeks excluding the
hardware-gated E2.T6; ≈ 7–9 senior-weeks). Sequencing: E1 →
E2 (flagship) with E3 in parallel by a second junior; E4/E5/E6 follow in any order
(E6.T2 last — it consumes E4.T4's findings).

---

## 6. Milestones & deliverables

| M | Deliverable (independently demoable) | Demo script (reviewer runs) | Est |
|---|---|---|---|
| M1 | **Instrumented Metal** — GPU timers, capture, census lane, budgets enforced on today's chain | `RENDERERS="gl metal" tools/perf_census.sh jungle dam && GE007_METAL_CAPTURE=120 GE007_RENDERER=metal build/ge007 --remaster --level dam` | 1.8 w |
| M2 | **SSAO v2 shipped** — hemisphere+blur+half-res in `--remaster`, all gates green | `build/ge007 --remaster --level jungle` then same with `GE007_SSAO_DEBUG=1` and `--config-override Video.SsaoMode=planar` for the A/B | 5.2 w |
| M3 | **Presentation correct** — HiDPI native, SSAO+MSAA coexist, Metal vsync real | `GE007_RENDERER=metal build/ge007 --remaster --level dam --config-override Video.HiDPI=1 --config-override Video.MSAA=4` | 3.2 w |
| M4 | **Modern AA** — SMAA live + TAA verdict on record | `GE007_RENDERER=metal build/ge007 --remaster --level depot --config-override Video.Smaa=1` (A/B vs `Video.Fxaa=1`) | 3.2 w |
| M5 | **HDR + DRS + 120 Hz verdict** | `GE007_RENDERER=metal build/ge007 --remaster --level dam --config-override Video.HdrOutput=1` on an XDR panel; DRS stress: add `--config-override Video.RenderScale=4 --config-override Video.DynamicResolution=1` | 4.6 w |

---

## 7. Risks & mitigations (ranked)

| # | Risk | Mitigation | Kill / de-scope criterion |
|---|---|---|---|
| 1 | **SSAO v2 quality misses the bar** on GoldenEye's compressed non-linear depth (the documented failure mode of the depth-only planar fallback: banding on distant grazing ground) | v2's normal-aware hemisphere + `FarCutoff` fade is designed for exactly this; the far/grazing gate (E2.T2) is evaluated *first*, before blur can hide it | If after 3 tuning days the far-gate (AO ≥247/255 on grazing ground) and the crease-gate (≤215/255) cannot hold simultaneously on dam+jungle → ship v2 with `FarCutoff` pulled in to 64 (near-field-only AO) and record the ceiling; do NOT ship a wash |
| 2 | **MSAA restructure breaks the XLU/RDP snapshot machinery** (encoder-break path, `gfx_metal.mm:1749-1758` — no upstream template, per METAL_BACKEND_PLAN §4 risk #1) | Resolve-on-every-encoder-close keeps all snapshot/readback consumers on single-sample textures; validate train/glass scenes explicitly | If `StoreAndMultisampleResolve` per encoder-break costs >2ms on jungle → de-scope to "MSAA disables the cvg-memory diag path" (mirroring GL's own SSAO-off compromise), one-time warn |
| 3 | **TAA/interpolation matrix extraction is infeasible** (no isolated view matrix — §4.2) | Both are hard-boxed spikes sharing the investigation; SMAA and 120Hz-present don't depend on them | Automatic: spike box expires → KILL recorded; SMAA (E4) and Phase-A pacing (E3.T4) are the shipped fallbacks |
| 4 | **HDR washes the faithful art** (LDR content naively expanded) | Design maps SDR 1:1 and admits only bloom energy into headroom (§4.3); side-by-side SDR review required | If reviewers can tell SDR content shifted with HdrPeak=1.0 → block until transfer is fixed; if EDR win is invisible in a blind test at HdrPeak=2.0 → de-scope to backlog |
| 5 | **DRS thrash/visibility** (target realloc or obvious res pumping) | Quantized ladder + rung cache + hysteresis (§4.7); HUD/filter always full-res | If rung transitions are visible in normal play on M3 Max → reduce ladder to {1.0, 0.7} and require 120-frame dwell; if still visible → default stays 0 forever (opt-in perf tool) |
| 6 | **Budget creep** — v2+SMAA+MSAA stack past the 4ms@4K post budget | Every epic lands behind the E1.T3 gate; `--remaster` preset only adopts combos that pass on jungle+dam | Preset never includes a combo that misses budget; users may opt into over-budget stacks manually |
| 7 | **Cross-backend drift** (Metal grows features GL lacks) | Each divergence is explicit, logged once at runtime, and listed in VISUAL_MODES.md; E2.T6 re-converges SSAO when a Linux box exists | If GL-only platforms become a support burden → freeze new Metal-only visual features until T6-class ports catch up |

---

## 8. Validation strategy (the standing battery — run per commit on touched features)

Pattern per `docs/REMASTER_ROADMAP.md` §7. All artifacts stay out of git (R2).

```bash
# 0) Build
cmake --build build -j

# 1) IDENTITY (R3): flags-off byte-identical, BOTH backends
for R in "" "GE007_RENDERER=metal"; do
  env $R SDL_AUDIODRIVER=dummy GE007_DETERMINISTIC_STABLE_COUNT=1 GE007_NO_VSYNC=1 \
    GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 build/ge007 --faithful --level dam \
    --deterministic --screenshot-frame 120 --screenshot-exit
done   # hash of each backend's capture must equal its recorded pre-W3 baseline

# 2) FEATURE A/B (per feature; example SSAO v2)
env GE007_RENDERER=metal ... build/ge007 --remaster --level jungle --deterministic \
  --config-override Video.SsaoMode=planar    --screenshot-frame 120 --screenshot-exit
env GE007_RENDERER=metal ... build/ge007 --remaster --level jungle --deterministic \
  --config-override Video.SsaoMode=hemisphere --screenshot-frame 120 --screenshot-exit
python3 tools/compare_screenshots.py a.png b.png --max-changed-pct 12.0   # v2 vs v1 budget
python3 tools/audit_screenshot_health.py b.png

# 3) SSAO wash/far/temporal gates (E2 only)
tools/ssao_gate.sh dam jungle facility      # encodes the ≤8/255, ≥40/255, ≤2/255 gates

# 4) SIM INVARIANCE (R1) — every W3 flag toggled on vs off
tools/sim_invariance_gate.sh                 # RAMROM dam1, identical end-state hash
python3 tools/compare_state.py baseline.jsonl feature.jsonl

# 5) GPU BUDGET
tools/gpu_budget_gate.sh --feature <flag> --levels jungle,dam   # <2ms@1080p, <4ms@4K delta

# 6) BREADTH + MEMORY SAFETY
tools/playability_smoke.sh --all
tools/asan_smoke.sh                          # ASan/UBSan on the draw path
# NB: --level takes level NAMES (or raw LEVELIDs like 33/41), never indices 1-18.
# This is the 18-solo-level list from tools/perf_census.sh ALL_LEVELS (minus aztec/egypt).
for L in dam facility runway surface1 bunker1 silo frigate surface2 bunker2 \
         statue archives streets depot train jungle control caverns cradle; do
  GE007_RENDERER=metal build/ge007 --remaster --level $L \
    --deterministic --screenshot-frame 90 --screenshot-exit || exit 1
done

# 7) CONTAMINATION GUARD (R2)
scripts/ci/check_no_rom_data.sh && scripts/ci/check_sim_render_separation.sh \
  && scripts/ci/check_timing_lock.sh
```

Notes: screenshots use `--level` boots (`--ramrom` renders black headless — roadmap
P1.1 note); the Metal readback contract (bottom-up RGB, `gfx_metal.mm:1846`) keeps all
existing compare tooling unchanged; HDR gates run SDR-only (§4.3); DRS pinned 0 in all
capture harnesses.

---

## 9. Open questions (genuinely undecidable without the user)

1. **`--remaster` preset adoption order** — once E2/E4 pass gates, does the preset flip
   `SsaoMode=hemisphere` and `Smaa=1` immediately, or after a play-session sign-off?
   (Plan assumes: flip after the M2/M4 demo reviews; the keys ship either way.)
2. **HDR default under `--remaster` on EDR panels** — auto-enable when headroom > 1.5
   is detectable, but silently changing output encoding on dock/undock may surprise;
   plan ships default-off pending a call.
3. **Hardware access** — is there a Linux/Windows GPU box for E2.T6 (GLSL v2 port) and
   a 120 Hz external display for E3.T4 verification beyond the built-in panel?
4. **120 Hz appetite** — if E6.T2 returns GO, view interpolation is a months-scale
   follow-on workstream touching the main loop (R1 red zone). Fund it, or accept 60 Hz
   as the engine's honest ceiling and spend the budget on lighting Track 1 instead?
