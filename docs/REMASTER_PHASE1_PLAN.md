# MGB64 Remaster — Track‑1 Lighting: Phased Execution Plan

> ## ⚠ EXECUTION STATUS (2026‑07‑01)
> - **P0 foundation — ✅ SHIPPED** (`64f9574`): proj view‑ray capture (`P[0][0]`/`P[1][1]`),
>   SSAO v2 settings, one‑time MSAA warn + hard‑disable. No visual change; A2 SSAO intact.
> - **P1a‑quality — ⛔ BLOCKED on this host**, reverted; tree stable at P0. **Two compounding
>   walls on macOS arm64 / Apple GL 4.1‑over‑Metal:**
>   1. **Per‑sample view‑position reconstruction HANGS the GPU** — `vec3(ndc.x/uProjX,
>      ndc.y/uProjY,-1)*linZ` + `cross`/`normalize`. Wedges the Metal driver at runtime (0% CPU,
>      resists `timeout`; ~4 min recovery, wedges other GL processes meanwhile). Reproduced both
>      as a separate FBO pass and inline; **not** a shader‑size issue (fewer samples still hang).
>      A2's depth‑delta AO and the planar approach below do **not** hang — the hang is exactly the
>      per‑sample proj‑divide + cross/normalize.
>   2. **Depth‑only fallback bands.** Planar‑prediction AO (local `linZ` gradient; no
>      reconstruction) is hang‑safe but shows **horizontal precision banding on receding ground**:
>      GoldenEye's huge far plane compresses world depth to ~0.999x, so 24‑bit non‑linear depth
>      quantization noise ≈ the crease signal. No occlusion floor separates creases from banding.
> - **DEEP‑RESEARCH VERDICT (102‑agent adversarial research + empirical test):** the top cheap fix
>   `textureLod(depth,uv,0.0)` was **tested and did NOT clear the hang** → not implicit‑derivative UB,
>   not pixel‑count. Equal‑fetch‑count planar AO runs fine → the trigger is the reconstruction OPS
>   (`ndc/projX`,`ndc/projY` divides + `cross`/`normalize`): an **op‑level bug in Apple's deprecated,
>   unmaintained GL‑4.1‑over‑Metal translator** (frozen since 2018), not fixable in‑shader. **Durable
>   fix = a NATIVE METAL backend** — our exact lineage, **Ship of Harkinian / libultraship (Fast3D),
>   already ships one** (metal‑cpp); base fast3d has only GL/D3D. That is the proven template (weeks of
>   work; also unblocks all future screen‑space FX). Untested long‑shot before committing to it: a
>   half‑res separate RGBA8 AO pass. `textureLod` + NaN‑guard remain correctness wins to fold in.
> - **(superseded) LINEAR‑DEPTH PREPASS** — write
>   per‑draw view‑space Z (R32F) during scene render (each draw's own `P_matrix`). Gives clean
>   linear depth (kills banding) **and** lets AO read `linZ` from a texture instead of the
>   hang‑prone per‑sample proj‑divide. Touches the scene‑render path (MRT or a re‑submit prepass) →
>   larger change + own dual‑case identity re‑validation; **should be validated on non‑Apple‑GL
>   hardware too** (the hang may be Apple‑specific). Until then, **A2 SSAO stays the default‑off
>   shipped SSAO.** (Ops note: a stale `Video.MSAA=2` in the run‑CWD `ge007.ini` silently disables
>   SSAO via `g_scene_depth_valid=!multisampled` — `rm ge007.ini` / force `MSAA=0`.)

**Grounding (verified against tree):** the shipped SSAO is the depth‑delta wash at `gfx_opengl.c:2963‑2997` (raw depth‑delta, 1.5% floor, no AO texture, no normal, one global far‑plane linearization). Single‑projection capture keeps largest‑|B| (`gfx_pc.c:15433‑15436`). `g_scene_depth_valid = !multisampled` (`:3588`). The scene resolve blit is a **separate** function, `gfx_opengl_resolve_scene_target` (`:3552‑3582`), called at `end_frame:3593` **before** `gfx_opengl_apply_output_vi_filter` (`:3219`). `Video.Ssao` defaults off but `Video.RemasterFX` defaults **on** (`platform_sdl.c:207`), and SSAO gates on `g_pcRemasterFX && g_pcSsao` (`:2239`). `GlobalLight` is a single hard‑coded constant — WSW `(77,77,46)` at `bg.c:2740`, uploaded via `gSPSetLights1` right after `G_LIGHTING` is cleared (`bg.c:6991→7000`).

---

## Strategy framing

The shipped SSAO looks bad for a **structural** reason, not a tuning one: a single fused horizon loop inside the monolithic output shader (`gfx_opengl.c:2981‑2987`) with **no AO texture** (blur impossible), **no reconstructed normal** (flat receding ground self‑occludes), and **one global far‑plane linearization**. The winning move is to make a depth‑reconstructed **view‑space normal** the shared primitive — it fixes SSAO *and* is the exact input roadmap T1.3 needs — and to re‑architect SSAO into a real half‑res → bilateral‑blur → composite pipeline. Everything stays in the read‑only screen‑space output pass (gameplay‑invariant by construction), behind `Video.*` / `GE007_*` flags, default‑identity. We descend by visible‑quality‑per‑effort and stop at an honest, cheap Track‑2 go/no‑go rather than committing months blind.

**Honest engine ceiling (state up front):** env geometry clears `G_LIGHTING` (`bg.c:6991`), so the VBO carries baked vertex colour and **no authored normals**. Every Track‑1 normal is *reconstructed from depth* — degenerate on low‑gradient ground, speckled at silhouettes/foliage, wrong on the viewmodel (near projection) and on XLU/sky. Screen‑space directional relight risks **double‑lighting** the already‑baked colour (baked with the *same* fixed WSW `GlobalLight`). Real per‑pixel geometric normals and material maps require VBO/TBN plumbing = **Track 2 (T2.1→T2.2 = door; T2.3+ = months, deferred)**. Track 1 buys contact AO, a subtle directional cue, and cast shadows — not PBR.

**Scope‑honesty correction (do not oversell):** the single global linearization *does* mis‑scale props on their own intermediate projection, but **P1a does NOT fix that.** P1a only adds a near‑**cut** that *excludes* the viewmodel; intermediate‑projection props remain mis‑linearized (neither cut nor corrected). The real fix is the **linear‑depth prepass**, which is **deferred** (see below). Therefore "fixes prop mis‑scale" is explicitly **removed** from P1a's value proposition — P1a's claim is exactly: *flat ground stops self‑occluding; creases/props darken cleanly; the viewmodel is excluded.*

---

## Standing acceptance gates (every phase must pass all; per‑phase "Acceptance" only adds specifics)

- **G‑IDENTITY (byte‑identical off — TWO cases, both mandatory):**
  1. `Video.RemasterFX=0` (whole chain off) → `--level 33` direct‑boot screenshots pixel‑exact vs pre‑change baseline.
  2. **`Video.RemasterFX=1` + phase‑flag=0** (the at‑risk case) → pixel‑exact vs pre‑change baseline. This catches an AO/blur/shadow pass that rebinds FBO/viewport/textures (P1a wiring `:3332‑3344`) and fails to restore state, silently regressing the *existing* bloom/FXAA/grade chain for remaster‑on / effect‑off users. New shader code sits behind a `uFlag==0` branch mirroring `uSsao==1` at `:2994`; the off path must take the identical route and hash identically (note `output_ssao_active()` forces the scene FBO at `:2246` — re‑verify OFF still routes direct).
- **G‑SIM (invariance):** `tools/sim_invariance_gate.sh dam1 600 2` PASS with feature ON vs OFF (whole‑arena RAMROM hash identical). Structurally guaranteed (pass reads `g_scene_depth_tex` + colour copy + per‑frame‑captured backend globals; writes only scratch FBOs/backbuffer) but must be **proven**. Backend light/proj captures are read‑once‑per‑frame **uniforms only, never written back into anything a sim tick reads.**
- **G‑PERF (budget, GPU‑timed, bracket the RIGHT span):** `perf_census.sh work_ms` is a 1 ms **CPU** interval, blind to GPU fill — **do not trust it** as the gate. Land a `GL_TIME_ELAPSED` query that brackets **`gfx_opengl_resolve_scene_target` + `gfx_opengl_apply_output_vi_filter` together** (the resolve blit at `:3552‑3582` is the cost SSAO *forces* by turning on the scene FBO, and at 2160p it is not free — bracketing only the post‑chain under‑measures the true tax). Enforce **combined resolve+post‑chain delta (on minus off) < 2 ms @1080p, < 4 ms @2160p** on **jungle + dam**. Run `perf_census.sh` too, but the GPU timer is the gate.
- **G‑ASAN:** `-fsanitize=address,undefined` boots `--level 33` + runs the invariance replay clean; **zero new `-Wall` warnings.**
- **G‑VISUALS (multi‑level — not Dam‑only):** all visual A/B on **direct boot** (renders gameplay); `--ramrom` is black‑headless, usable only for the deterministic hash gate. Every visual gate below must be evaluated on **all three**: **Dam exterior** (`--level 33`, long depth range + sky), **Jungle** (foliage/silhouette speckle — the plan's own top failure mode), and **one interior level** (flat floors, short depth range, no sky — exercises near‑cut/sky‑cut differently). A gate that passes only on Dam is **not** passed.
- **G‑MSAA (no silent no‑op):** `g_scene_depth_valid = !multisampled` (`:3588`; resolve blits `GL_COLOR_BUFFER_BIT` only, `:3576`) → any `Video.MSAA>0` silently disables the effect. Ship a **one‑time warn** + settings‑UI reflection; document SSAO/lighting ⇔ non‑MSAA mutual exclusion (default stack = `2×SSAA + FXAA`).
- **G‑SPLIT (pane safety):** output pass runs **once over the whole backbuffer** (`:3592`). On `feat/split-screen`, two panes share one depth tex + one captured projection + full‑backbuffer aspect → seam garbage + wrong per‑pane scale. **Gate every effect OFF when viewport/player count > 1** until pane‑aware plumbing exists.
- **Flag discipline:** every knob under `Video.RemasterFX` master, default‑off, with a `GE007_*` env A/B hatch and a `*_DEBUG` visualization.

---

## P0 — Foundation & guardrails *(no visual change; unblocks everything)*

**Goal:** land the shared capture, AO render targets, and acceptance instruments so P1a is pure shader work behind a proven‑safe scaffold.

**File‑level deliverables**
- `gfx_pc.c:15374‑15375, 15433‑15437` — beside `g_pc_ssao_proj_a/_b`, capture `g_pc_ssao_proj_x=P[0][0]`, `g_pc_ssao_proj_y=P[1][1]` **inside the same `fabsf(P[2][3])>0.5 && largest‑|B|` guard** (one matrix → aspect/FOV‑correct view‑ray; ortho guard prevents divide‑by‑zero).
- **Proj‑capture stability — gate on depth‑present, NOT blind last‑good reuse.** Today `g_pc_ssao_proj_b` resets to 0 (`:3521`) and `ssao_on` gates on `proj_b!=0` (`:3174`), so SSAO skips no‑perspective frames. Do **not** blindly persist last‑good across frames — that runs SSAO on load/scope‑toggle/menu‑overlay frames using a projection that may not match that frame's depth buffer → wrong‑linearization AO flash. Instead: persist last‑good **only when a valid perspective depth buffer is actually present this frame**; otherwise leave SSAO skipped as today. This kills full‑off flicker on legitimate frames without introducing stale‑projection flashes.
- `gfx_opengl.c` — new `g_ssao_fbo` + `g_ssao_ao_tex` + `g_ssao_blur_tex` (**RG16F**, `GL_LINEAR`, `CLAMP_TO_EDGE`), sized full‑res in P1a‑quality; half‑res `(g_scene_w>>1, g_scene_h>>1)` becomes selectable in P1a‑perf via `SsaoHalfRes`. Recreate‑on‑resize via a new `ensure_ssao_target` modeled on `ensure_scene_target` (`:2253`) / `ensure_filter_texture` (`:3339`). Auto‑tracks RenderScale/SSAA since it derives from `g_scene_w/h`.
- `gfx_opengl.c` — `GL_TIME_ELAPSED` query bracketing **resolve + post‑chain** (`:3552‑3582` + `:3219‑3409`), logged under `GE007_PERF_TRACE`.
- `gfx_opengl.c:2238‑2250` — extend `gfx_opengl_output_ssao_active()` to emit the MSAA warn and return false when player‑count > 1.
- `platform_sdl.c:198‑207, 1568‑1583` — register `Video.SsaoHalfRes` (default **0** — see coupling note), `Video.SsaoBlur` (default 0 until P1a‑perf), `Video.SsaoBias`, `Video.SsaoPower`, `Video.SsaoBlurDepthSharp`, `Video.SsaoFarCutoff` (world‑units, see P1a), each with a `GE007_*`; keep master `Video.Ssao=0`.

**Acceptance (+ standing gates):** old inline AO still renders unchanged when `Video.Ssao=1` (no regression yet); combined resolve+post GPU timer prints; MSAA+SSAO emits exactly one warn; **both** G‑IDENTITY cases byte‑identical.
**Effort:** ~1–1.5 days. **Risk:** low. **Deps:** none.

---

## P1a‑quality — SSAO v2 wash‑kill *(IMMEDIATE PRIORITY; full‑res, no blur)*

**Goal:** prove the **normal‑oriented horizon** kills the flat‑ground wash — in isolation, at full res, with no blur — so the load‑bearing quality claim is verified before any perf optimization can confound it.

**File‑level deliverables**
- **New AO program** (standalone, *not* the mega shader), near `ensure_scene_program` (~`:2803`): fullscreen VS; FS implements
  - `linZ(d)=uSsaoProjB/(uSsaoProjA+2d-1)`; `viewPos(uv)=vec3(ndc.x/uSsaoProjX, ndc.y/uSsaoProjY, 1)*linZ` (drops the `uSsaoAspect` divide at `:2977`);
  - **edge‑aware normal** — 5‑tap best‑of‑neighbour (`dx = |pr.z−P.z|<|P.z−pl.z| ? pr−P : P−pl`, same for `dy`), `N=normalize(cross(dx,dy)); if(N.z>0)N=-N;` (best‑of avoids silhouette bleed);
  - **normal‑oriented horizon** (the ground fix) — 6 dirs × 4 steps, `elev=dot(v,N)/dist`, `horizon=max(horizon,(elev−uSsaoBias)*falloff)`, `ao=pow(ao/DIRS,uSsaoPower)` → flat surface samples lie in its own tangent plane → elevation≈0 → AO≈0;
  - **far/grazing depth‑range falloff (new, load‑bearing):** multiply AO by `smoothstep(uSsaoFarCutoff, uSsaoFarCutoff*0.7, linZ)` so AO fades to 0 beyond a world‑Z cutoff (default ~128 world‑units, tunable). This is where non‑linear depth precision collapses and the reconstructed normal/elevation go noisy; contact AO at ~200 units is meaningless anyway. Kills the *far‑speckle‑becomes‑fainter‑wash* tail **and** saves fill.
  - `ign(gl_FragCoord.xy)` rotation for cheap dither;
  - **guards:** `d0>=uSsaoSkyCut` (sky) and `d0<=uSsaoNearCut` (viewmodel/near) → `outAO=vec2(0.0,linZ(d0))`;
  - output **RG16F = (AO, linZ)** (the G channel serves P1a‑perf's blur without a depth re‑read).
- **Driver wiring** `gfx_opengl.c:3332‑3344`: when `use_ssao`, run AO pass → `g_ssao_ao_tex` (full‑res, **no blur** in this phase), restoring FBO/viewport. In `draw_output_filter_texture`'s `ssao_on` block (`:3173‑3193`) bind `g_ssao_ao_tex` to **`GL_TEXTURE2` / `uSsaoTex`** instead of raw depth.
- **Compose rewire** `gfx_opengl.c:2963‑2997`: **delete** `kSsaoDir`/`ssaoLinZ`/`ssaoAO` (they move into the AO program); replace the `uSsao` branch with `color.rgb *= clamp(1.0 - uSsaoIntensity*texture(uSsaoTex,vTexCoord).r, 0.0, 1.0)`. AO composes **before** FXAA/bloom/grade.
- **Debug:** `GE007_SSAO_DEBUG=1` → `color.rgb=vec3(1.0-ao)` (raw AO field).
- **Golden reference:** capture the full‑res AO field on all three G‑VISUALS levels — this is the reference P1a‑perf's half‑res/blur output is diffed against.

**Acceptance — phase‑specific GATES (numbered, on top of standing gates)**
- **WASH GATE (the whole point):** in `GE007_SSAO_DEBUG`, near/mid flat gravel AO ≤ **8/255**; corners/under‑props visibly darker (AO ≥ **40/255** at contact creases). Evaluated on all three levels.
- **FAR/GRAZING GATE (new):** on **distant grazing ground** (camera near‑parallel to a receding plane — Dam and Jungle both have this), the `GE007_SSAO_DEBUG` AO field must stay ≤ **8/255** and show **no speckle band** at range. Confirm `uSsaoFarCutoff` fades AO to 0 before the noisy‑depth zone. This gate exists specifically because near/flat evaluation *hides* the far tail.
- **MULTI‑PROJECTION GATE (scoped honestly):** with the gun **raised**, heat‑map the reconstructed normal/AO — the viewmodel must show **no self‑occlusion** (the `uSsaoNearCut` band works). **Note the limit:** props on intermediate projections are *not* corrected here (see prepass deferral); the gate only asserts they are not *grossly* mis‑occluded (no black halo), not that their AO is geometrically correct.
- **Visual A/B:** flat gravel ≈ neutral, corners/under‑props darkened, no gun self‑occlusion, no sky darkening — on Dam, Jungle (watch foliage/silhouette), and one interior.
- **G‑PERF:** full‑res AO + compose meets the combined‑bracket budget. (If full‑res AO over a RenderScale=2 depth buffer blows the budget, that is *expected* and is precisely what P1a‑perf fixes — record the number, do not optimize here.)

**Effort:** 2–3 days. **Risk:** medium (depth‑normal fragility at silhouettes/foliage, mitigated by best‑of‑neighbour + far cutoff). **Deps:** P0.

---

## P1a‑perf — half‑res + separable bilateral *(perf play, isolated from the quality verdict)*

**Goal:** bring the proven‑correct AO into budget via half‑res + bilateral blur, diffed against P1a‑quality's golden reference so upsample halos can't masquerade as (or hide) a quality regression.

**File‑level deliverables**
- Enable half‑res `g_ssao_*` sizing (`SsaoHalfRes`).
- **New separable bilateral blur program:** one program run twice (`uBlurDir=(1,0)` then `(0,1)`), 9 taps, depth‑weighted via the RG16F G channel (`exp(-|Δz|*uSsaoBlurDepthSharp)*gauss`). Wiring: AO → `g_ssao_ao_tex`, blur‑X → `g_ssao_blur_tex`, blur‑Y → `g_ssao_ao_tex`.
- **RenderScale⇄half‑res coupling (documented + enforced):** `g_ssao_*` derives from `g_scene_w/h` = RenderScale × window. At the default `2×SSAA` stack, half‑of‑scene ≈ window res (fine). At **RenderScale=1** (what FPS‑constrained users pick), `SsaoHalfRes=1` yields **quarter‑window** AO (540p at 1080p — visibly coarse). Therefore: default `SsaoHalfRes=0`; when `SsaoHalfRes=1` **and** RenderScale≤1, **auto‑force full‑res (or emit a one‑time warn)**. Document this inversion in `VISUAL_MODES.md`.

**Acceptance — phase‑specific GATES**
- **HALF‑RES HALO GATE (numbered):** diff the half‑res+blur AO field against P1a‑quality's full‑res golden reference at high‑contrast depth edges — bilinear‑upsample halo band luma delta ≤ **3/255**. If it exceeds, enable a bilateral **upsample** in compose or fall back to `SsaoHalfRes=0` (the escape hatch must exist and work). Evaluated on all three levels.
- **TEMPORAL GATE (numbered):** slow‑pan; diff consecutive frames over a **static ground patch** — max per‑pixel luma delta ≤ **2/255** with SSAO on (bilateral + IGN + depth‑present‑gated proj capture must kill boiling). No full‑off flicker on legitimate frames.
- **G‑PERF:** AO + 2× blur + compose meets the combined‑bracket budget (this is the payoff: half‑res ≈ 0.25× fill vs the ~4× trap of full‑res AO over a RenderScale=2 depth buffer). Report the on‑minus‑off delta on jungle + dam.
- **WASH/FAR GATES from P1a‑quality still hold** (blur must not resurrect a blurry gray wash).

**Effort:** 1.5–2 days. **Risk:** medium (upsample bleed — bounded by the halo diff + escape hatch). **Deps:** P1a‑quality (golden reference).

> **Deferred robustness upgrade (flagged; the *real* prop/viewmodel fix):** a **linear‑depth prepass** — write per‑draw view‑space Z (R32F) using each draw's own `P_matrix` during scene render — structurally fixes viewmodel/prop/character mis‑linearization (the thing P1a deliberately does **not** fix) *and* lets AO own its resolution (killing the MSAA no‑op). Scope as a follow‑up only if the multi‑projection limit becomes a real defect. Touches the scene‑render path → carries its own dual‑case G‑IDENTITY re‑validation.

---

## ◆ Default‑on decision — **VISUAL_MODES policy call, NOT an engineering toggle**

After P1a‑perf: whether to flip `Video.Ssao` default‑on is an **explicit policy decision recorded in `VISUAL_MODES.md`**, made with these caveats stated in the doc, **not** an "if net‑positive, flip it" inside a phase:
- `RemasterFX` already defaults on, so flipping `Ssao` on makes the **shipped default no longer byte‑identical to stock** (`platform_sdl.c:207`).
- It forces the **scene‑FBO + resolve + AO + 2× blur + compose (5 passes)** on *every* user.
- It **silently no‑ops under MSAA** (`g_scene_depth_valid`) — a settings‑reflected‑but‑inactive feature.
This is the same "remaster‑default policy" decision flagged in memory; route it there. Until that call is made, `Video.Ssao` stays default‑off.

---

## P1b — Directional relight PROBE (T1.3) + Track‑2 go/no‑go *(expected outcome: WASH)*

**Goal:** cheaply produce the **Track‑2 decision** — spend a *days*‑scale probe instead of committing *months* blind. Its primary output is a **verdict**, not shipped pixels.

**State the expected result honestly:** `GlobalLight` (`bg.c:2740`) is the **same fixed WSW direction the world's vertex colours were already baked with** (`bg.c:6991→7000`). A screen‑space directional term therefore **re‑applies light already baked in** — low intensity → imperceptible, high intensity → double‑lights. And a *single global* direction multiplies near‑coplanar ground faces by ~the same factor, so it **structurally cannot** smooth per‑face baked "rock‑facet seams" (those are baked colour discontinuities, not a missing directional term). **The expected verdict is WASH.** P1b is scoped to *produce that verdict cleanly*, **not** to fix seams. The four‑perspective disagreement is resolved this way: ship it as a low‑weight, default‑off probe with double‑lighting as a hard gate; its deliverable is the go/no‑go.

**File‑level deliverables**
- `gfx_pc.c` (near the proj capture, `:15418‑15437`) — capture the **eye‑space** sun dir: read `rsp.current_lights[0].dir` (uploaded every frame via `gSPSetLights1` even though env clears `G_LIGHTING`), forward‑rotate by the first room modelview's upper‑left 3×3 (**non‑transposed** — opposite of `calculate_normal_dir`'s object‑space transpose at `:15241`), normalize → `g_pc_sun_dir_view[3]`; copy `current_lights[0].col` (diffuse) + `current_lights[num-1].col` (ambient); reset in `start_frame` beside the proj reset.
- `gfx_opengl.c` main() (`:2992‑3019`) — after AO, before grade: `if(uEnvLight==1 && depth<uSkyCut && linZ>uNearCut){ N=reconNormal(); ndl=max(dot(N,uSunDir),0.0); shade=1.0+uSunIntensity*(ndl-uSunBias); color.rgb*=clamp(shade,0.0,2.0);}` with `uSunBias≈0.5` (or `150/255≈0.59` to match grey ambient), optional positive‑delta tint by diffuse / negative toward ambient. At `uSunIntensity=0`, `shade==1.0` exactly → identity. Reuses P1a's `reconNormal`.
- `gfx_opengl.c` — **depth‑discontinuity guard:** if any normal neighbour `|Δdepth| > k*z`, snap N to view axis and `shade→neutral` (kills silhouette/thin‑geo/foliage rim halos). Reapply near/sky guards.
- `platform_sdl.c` — `Video.EnvLight` (default 0), `Video.EnvLightIntensity` (default 0.15, clamp 0–0.5), `Video.EnvLightBias`, each `GE007_*`; `GE007_ENVLIGHT_DEBUG` outputs N as RGB / raw N·L, plus an env flag to **flip/rotate** the light dir to empirically validate the matrix‑convention sign before trusting it.

**Acceptance — phase‑specific GATES**
- **DOUBLE‑LIGHTING GATE (hard, numbered):** capture a **flat baked‑lit wall** with EnvLight on vs off; **mean |Δluma| < 3/255 AND no new histogram mode** (no bimodal split). If the term visibly re‑shades already‑baked surfaces (histogram shifts/splits above threshold), the probe **fails default‑on** — its value is the verdict, not the pixels.
- **LIGHT‑SIGN GATE:** `GE007_ENVLIGHT_DEBUG` confirms the ground normal is uniformly up, N·L smooth/constant across flat gravel, and the lit gradient matches `GlobalLight` (brighter on WSW‑facing slopes). Sign validated empirically.
- **◆ THE pivotal Track‑2 checkpoint:**
  - **WASH (expected):** derivative normal too blocky on low‑gradient ground / term double‑lights → **this is the GO signal** for bounded Track‑2 entry. Ship the probe default‑off as an A/B artifact and **route budget to P2 (T2.1→T2.2) BEFORE P1c** (see reorder).
  - **CONVINCING (unlikely):** a subtle screen‑space directional cue that helps *without* double‑lighting → keep it (flag stays; default‑on only via the VISUAL_MODES policy call), **DEFER Track 2**, proceed to P1c.

**Effort:** 2–3 days. **Risk:** medium (derivative normals near‑degenerate on ground — the probe is *expected* to be a legitimate wash; matrix‑convention sign; faithfulness → subtle + default‑off). **Deps:** P1a (normal + guards).

---

## P2 — Track‑2 entry, **conditional** (T2.1 → T2.2), then STOP *(preempts P1c on a WASH)*

**Goal:** if P1b = **WASH**, open the bounded Track‑2 door for *real* per‑vertex geometric normals — the only correct fix for ground banding — and stop before the months of material maps.

**Sequencing (corrected):** because P1b's entire purpose is to decide Track‑2, a **WASH means per‑vertex normals (T2.1→T2.2) are the real ground fix — so P2 PREEMPTS the P1c shadow epic.** The committed budget stops at the P1b ◆ regardless; but if work continues after a WASH, **do P2 before P1c**, not the printed‑order reverse.

**Gate to start:** BOTH (a) P1b proved screen‑space directional lighting can't fix the ground, AND (b) the target bar demands per‑pixel/material‑mapped lighting (§3 material maps wait on this). Otherwise **DEFER** (roadmap §8).

**File‑level deliverables (if GO)**
- **T2.1** `gfx_pc.c` — extend `LoadedVertex` (`:1891`) + the VBO (`~:11445`) to carry **world‑pos + tangent basis**; pass the live modelview captured in `gfx_sp_matrix` (`:9099‑9107`). Enabling prerequisite for all per‑pixel lighting. **Weeks.**
- **T2.2** `gfx_opengl.c` (VS ~`:570`, FS ~`:584`) — world‑pos in FS, `dFdx/dFdy` world‑pos face normal, apply `GlobalLight` — real per‑pixel directional shading, no authored normals. **Days once T2.1 lands.**

**Acceptance:** produces a **decision** + (if GO) T2.2 fixes the ground with real geometry normals; **both** G‑IDENTITY cases identical; **G‑SIM green (VBO format change touches the hot draw path → ASan/UBSan + dual‑case identity mandatory).** **◆ After T2.2, re‑evaluate T2.3** — default answer **STOP** (roadmap §8).

**Effort:** T2.1 weeks, T2.2 days. **Risk:** medium (hot‑path VBO change; still render‑only/invariant). **Deps:** P1b = WASH.

---

## P1c — Sun shadow map (T1.4) *(deferrable epic; NOT a phase‑peer; last, and deprioritized after a WASH)*

**Goal:** replace the flat blob shadow with a real cast shadow for player + characters under `GlobalLight`.

**Status — read before sequencing:** this is a **separate epic (1.5–2.5 wks, geometry re‑submission)**, not a days‑scale peer of P1a/P1b. It is **explicitly conditional and deferrable**: it must **not** sit in the committed‑budget conversation as comparable to P1a/P1b, and after a P1b **WASH** it is **preempted by P2**. Reach it only when (i) P1b was CONVINCING or the ground fix is otherwise settled, and (ii) the blob‑shadow upgrade clears the bar over §3 texture work.

**Classification (honest):** **not** pure screen‑space. The **receive** is screen‑space (reconstruct world‑pos from camera depth via a captured inverse‑view‑proj, sample light‑view depth in the output pass), but the **caster pass is a new geometry re‑submission from the light POV** — the invasiveness cliff of Track 1. Still gameplay‑invariant (caster reads the same const meshes read‑only, writes only its own depth FBO, lives in the fast3d backend → passes G‑SIM). The existing blob already gives a contact cue, so this is the lowest quality‑per‑effort of the three.

**File‑level deliverables**
- `gfx_opengl.c` — new light‑view depth FBO; caster pass bounded to **dynamic casters** (player + characters, optionally tagged props) to stay cheap; shadow **receive** in the output FS (PCF sample after world‑pos reconstruction).
- `gfx_pc.c` (near `:15418`) — capture inverse‑view‑proj for receive. **(Do NOT assume T2.1 world‑pos is available — T2.1 is conditional on a P1b WASH, which routes to P2 instead of P1c, so the "reuse T2.1 world‑pos" path is unavailable when P1c actually runs. Capture the inverse‑view‑proj directly.)**
- `src/game/model.c:~10850` — gate/replace the blob shadow only when `Video.SunShadow` is on.
- `platform_sdl.c` — `Video.SunShadow` (default 0) + `GE007_SUNSHADOW`.

**Acceptance — phase‑specific GATES**
- Player/characters cast a **stable** shadow (Dam exterior): no acne, no peter‑panning, no swim during slow pan (numbered TEMPORAL threshold ≤ 2/255 applies to the shadow edge too).
- Blob replaced **only** when the flag is on; blob unchanged otherwise (both G‑IDENTITY cases).
- **G‑PERF:** caster + PCF meets the combined‑bracket budget with the caster set bounded to dynamic geometry.
- **◆ Evaluate:** is the look strong enough to STOP Track 1 here.

**Effort:** 1.5–2.5 weeks. **Risk:** medium‑high (classic shadow‑map artifacts; caster re‑submission is the largest Track‑1 surface). **Deps:** P1a (depth/proj foundation). No T2.1 dependency.

---

## Deferred / out of scope (honest ceiling)

- **Linear‑depth prepass** — the *real* prop/viewmodel/character linearization fix and the MSAA‑no‑op killer; deferred from P1a (see P1a‑perf note). Touches scene‑render path → dual‑case identity re‑validation.
- **T2.3 normal/roughness material maps** — new sampler units + TBN, `draw_class==ROOM` only; where §3 material maps finally render. **Months.** Behind the P2 ◆; default answer STOP.
- **T2.4 tangent hardening**, **T2.5 character normals** (isolated — chars keep `G_LIGHTING`; cheap standalone side‑quest, low priority since chars are already lit).
- **Out of scope entirely (§8):** SSR, full deferred PBR, per‑pixel character lighting without Track 2, dynamic point/spot lights. Spend budget on §3 textures + Track‑1 T1.1–T1.4, not these.

---

## Priority ranking (visible‑quality‑per‑effort, descending)

`P0 (foundation, ~1 day)` → **`P1a‑quality (2–3 days, IMMEDIATE — prove the wash dies, full‑res)`** → `P1a‑perf (1.5–2 days — half‑res + bilateral into budget)` → `◆ VISUAL_MODES default‑on policy call` → `P1b directional probe → Track‑2 verdict (2–3 days, expected WASH)` → **branch on verdict:**
- **WASH →** `P2 T2.1→T2.2 (weeks — the real ground fix)` → `STOP / T2.3 months (deferred)`. *(P1c deprioritized/deferred.)*
- **CONVINCING →** `P1c sun shadow (1.5–2.5 wks, deferrable epic)` → evaluate STOP; Track 2 deferred.

**Recommended committed budget now:** ~1.5–2 weeks for **P0 + P1a‑quality + P1a‑perf + P1b through the pivotal ◆ checkpoint** — this resolves the biggest visible defect *and* returns the Track‑2 decision. **Re‑plan from the probe verdict before spending on P1c or P2.** P1c's 2‑week epic is deliberately **excluded** from this budget line; it competes for budget only *after* the verdict, and loses to P2 on a WASH.
