# MGB64 Remaster Roadmap

A ranked, phased plan for layering **modern visual enhancements** and a **content
remaster** on top of the now-playable port — without sacrificing original-hardware
parity.

This roadmap was produced from two adversarially-verified multi-agent surveys of
the live code (a 12-lane renderer-enhancement survey + a 9-lane asset-remaster
survey, ~128 subagents, every claim checked against `file:line`). It supersedes
nothing in [RENDER_PORT_SURVEY.md](RENDER_PORT_SURVEY.md) (that doc is parity/bug
work); this one is forward-looking *enhancement* work.

---

## ✅ Phase 0 — SHIPPED (2026-06-24)

All of Phase 0 is implemented as 13 opt-in, default-off commits on
`feat/split-screen-multiplayer` (local; unpushed). Each carries a
`GE007_*` / `Video.*` / `Input.*` A/B gate and defaults to identity/off.

| Commit | Feature | Toggle (default) |
|--------|---------|------------------|
| `27a8ae0` | Output-pass post-FX plumbing (`uApplyPost`) + **color grade** | `Video.Saturation/Contrast/Brightness` (1/1/0) |
| `c2a1d06` | Output **ordered dither** | `Video.OutputDither` (0) |
| `fc26a0f` | Output **vignette** | `Video.Vignette` (0) |
| `2a15fe1` | Output **bloom v1** | `Video.Bloom` (0) |
| `e15cc87` | **RenderScale cap 2×→4×** + driver-level GL-limit clamp | `Video.RenderScale` (1.0) |
| `bc01b1e` | **Radial deadzone + gamepad aim curve + opt-in fps-independent look** | `Input.Gamepad{LookCurve,Deadzone,RadialDeadzone,FpsScale}` (linear) |
| `06c85f0` | **Cosmetic fog-density dial** (AI-neutral) | `Video.FogDensity` (1.0) |
| `3025470` | **Per-level sky/cloud RGB grade** | `#ifdef GE007_ATMOS_GRADE` (off) |
| `4a8bfca` | **Bullet-decal cap 100→400** | `#ifdef NATIVE_PORT` (N64 keeps 100) |
| `014b557` | **Additive viewmodel sway** | `Input.ViewmodelSway` (0) |
| `90a0be0` | **Per-gun viewmodel proportion** | `GE007_VIEWMODEL_TWEAK` (off) |
| `f1277cd` | **Always-on modern crosshair** | `Input.ModernCrosshair` (0) |
| `31eebd2` | **Hit markers on damage** | `Input.HitMarkers` (0) |

**Validation (all driven post-hoc, independent of the implementing agents):**

- **Regression — default path byte-identical.** Clean rebuild (`--clean-first`)
  green. `playability_smoke --all` (20 levels, all flags off) reproduced the
  baseline contract *exactly* (14 pass / same 6 pre-existing fails), and **all 39
  default-state screenshots are byte-identical to the pre-Phase-0 baseline**. The
  opt-in/identity model held perfectly — zero default-path change.
- **Per-feature A/B (deterministic, level 33).** 8/9 visually-testable effects
  confirmed active by pixel-diff vs a byte-identical reference: color-grade (≈20%),
  vignette (≈42%), bloom, RenderScale-3×, fog-density ×4 (≈81%; thinning already-thin
  fog shows nothing, thickening is dramatic), dither (≈1.8% *atop* a graded frame —
  correct ±0.5-LSB anti-banding, a no-op on flat 8-bit by design), viewmodel sway,
  per-gun scale.
- **ASan/UBSan.** Output-FX+RenderScale, fog+HUD+viewmodel, and input feature paths
  all run clean; the only finding is a *pre-existing* misaligned-struct UB
  (`unk_0A1DA0.c:4061`) also present with all flags off. **No findings in Phase 0 code.**

**Needs in-game (not headless-confirmable) confirmation — code-correct, boots clean:**
- **Modern crosshair** routes correctly in `gunDrawSight` but only draws in *aim*
  states; the headless walk never aims. (If "always-on in hip-fire" is wanted, the
  crosshair must be force-drawn in hip-fire — a small follow-up.)
- **Hit markers** trigger from the correct hit-detect site, but need a registered
  hit (combat) to render.
- **Gamepad input feel** has no pad in the headless harness; defaults reproduce the
  vanilla linear map exactly.

---

## Guiding principles

1. **Everything is opt-in and default-off.** With all flags off, the build is
   byte-identical to today. Each item ships behind a `GE007_*` env var and/or a
   `Video.*`/`Input.*` settings key.
2. **One shippable step at a time.** Every phase is a small, independently
   landable, A/B-testable commit. No item requires a big-bang rewrite.
3. **Ranked by impact vs. difficulty/risk.** We spend effort where the visible
   payoff is highest and the regression surface is smallest.
4. **Honest about the engine.** GoldenEye's N64 model/animation/geometry formats
   are baked; the sim is quantized to integer 60 Hz ticks. We lean into what the
   renderer and asset pipeline can cheaply do, and we do **not** fight the baked
   formats (see *Out of scope*).

## The two engines that unlock almost everything

Two pieces of existing infrastructure are **force multipliers** — build the
carrier once, and a dozen features become a few lines each.

### Engine A — the fullscreen output pass (post-processing carrier)

A once-per-frame fullscreen pass already runs for gamma + N64 VI-filter emulation
(`gfx_opengl_apply_output_vi_filter`, `gfx_opengl.c:1744`, called in `end_frame`).
It is the ready-made carrier for **color-grade, dither, vignette, sharpen, FXAA,
and bloom** — all sharing one uniform-upload site (`gfx_opengl.c:1732-1737`) and
one activation gate.

> ⚠️ **Shared gotcha:** the pass early-returns when neither the VI-filter nor the
> color-adjust is active (`gfx_opengl.c:1783-1785`), and color-adjust is *false at
> default gamma 1.0*. **Every** new effect must add its term to
> `gfx_opengl_output_color_adjust_active()` (`gfx_opengl.c:1537`) or it silently
> no-ops. Order in the chain: grade/contrast **before** the gamma `pow`; bloom and
> sharpen on the final draw; `clamp(0,1)` stays last.

### Engine B — the texture import hook + IMAGESEG token namespace (HD-asset carrier)

Every static game texture (world walls/floors, props, character skins, weapon
skins, **and HUD icons**) flows through one funnel: `import_texture` →
`gfx_texture_cache_lookup` (`gfx_pc.c:7932`) keyed by a stable
`GE007_STATIC_TEXTURE_CACHE_KEY(token)` drawn from the shared **3001-entry
IMAGESEG token namespace** (`gfx_pc.c:1041,1088`). **One replacement loader hooked
at the cache-miss branch covers every art class at once.** (Fonts are the one
exception — they carry a raw `fc->pixeldata` pointer instead of a token and need
bespoke keying; see Phase 6.)

---

## Top picks (highest impact-per-effort, across both surveys)

| # | Item | Impact | Effort | Risk | Phase |
|---|------|--------|--------|------|-------|
| 1 | **Color-grade knobs** (saturation/contrast/brightness on output pass) | High | Hours | Very-low | 0 |
| 2 | **Bloom** (light bleed on lamps/sky/muzzle, standalone in output pass) | High | Days | Low | 0 |
| 3 | **Raise RenderScale cap 2×→4×** (true SSAA) + `GL_MAX_TEXTURE_SIZE` clamp | High | Hours | Low | 0 |
| 4 | **Always-on modern crosshair** (decouple crisp reticle from ADS) | High | Hours | Low | 0 |
| 5 | **Per-level color/fog grade table** (sky/cloud RGB columns only) | High | Hours | Very-low | 0 |
| 6 | **Hit markers** on bullet impact | High | Day | Low | 0 |
| 7 | **Gamepad aim curve + radial deadzone** (the "feels modern" lever) | High | Day | Low | 0 |
| 8 | **Texture-replacement spine** (dump → hash → disk loader) | Transformative* | Days–week | Low | 1–2 |
| 9 | **Class-aware HD texture pack** (world → props → characters → guns) | Transformative | Weeks (content) | Low | 3 |
| 10 | **Material-class helper + emissive boost** (lamps/screens pop) | Medium-high | Days | Low | 4 |

\*The spine is medium impact standalone but **transformative as the keystone** the
entire HD-asset lane depends on.

---

# The phased plan

Each item: **Impact · Effort · Risk · Type**, the payoff, the smallest first step
with `file:line` anchors, and the A/B gate.

## Phase 0 — Start today (instant gratification, all disjoint files)

These touch separate files (output FS, fog, `gun.c` HUD, input, viewmodel) so they
can all land concurrently with no conflicts. Each is hours-to-a-day and opt-in.

### Post-processing on the output pass (Engine A)

- **Color grade (saturation / contrast / brightness)** — High · Hours · Very-low · code-only.
  GoldenEye's palette is flat/washed; a global grade on final pixels richens every
  level, asset-independent (dodges the "invisible at N64 fidelity" trap).
  *First step:* add `uSaturation/uContrast/uBrightness` after scale/bias/gamma in
  the output FS (`gfx_opengl.c:1645-1647`), upload at `:1732-1737`, register three
  `SETTING_SCOPE_LIVE` floats next to `Video.Gamma` (`platform_sdl.c:1434`), and
  extend `output_color_adjust_active()` (`:1537`). Rec.601 luma; clamp last.
  *A/B:* defaults 1.0/1.0/0.0 = identity (byte-identical); `GE007_SATURATION/CONTRAST/BRIGHTNESS`.

- **Bloom** — High · Days · Low · code-only · **standalone**.
  Lamps, skies, explosions and muzzle flashes bleed light — the single most
  "remastered"-reading effect, entirely in the present pass.
  *First step:* in the output FS add a `uBloom` branch sampling
  `g_output_filter_copy_tex` (the FBO-0 copy, `gfx_opengl.c:1856` — **not**
  `g_scene_color_tex`, which only exists at RenderScale>1/MSAA): luma>threshold
  bright-pass + multi-tap blur, add back. v2 = separable blur on the already-declared
  half-res ping-pong (`g_output_filter_low_tex`, `:1050`).
  *A/B:* `GE007_BLOOM`, **OR it into the early-return gate** (`:1783-1785`). Caveat:
  LDR RGBA8 has no HDR headroom — tune threshold so bright white walls don't bloom.

- **Output Bayer dither** — Medium · Hours · Low · code-only.
  Skies/fog/fades band visibly (5551→RGBA8 with no VI dither); a 4×4 ordered-Bayer
  ±0.5 LSB dither restores smooth gradients near-free. Use a static LUT, **not**
  `fract(sin(dot))` (it crawls). *A/B:* `Video.OutputDither` default off + `GE007_OUTPUT_DITHER`.

- **Vignette** — Medium · Hours · Very-low · code-only.
  Soft 8–12% edge falloff adds depth/focus, esp. on the widened widescreen FOV.
  *A/B:* `Video.Vignette` 0.0 default = no-op.

### Resolution

- **Raise RenderScale cap 2.0×→4.0×** (true SSAA) — High · Hours · Low · code-only.
  The entire supersampling pipeline already works; three numeric clamps cap it at
  2.0. 4× internal kills edge-crawl/shimmer on low-poly geometry — the single most
  visible near-free IQ win. Verifier proved `gfx_current_dimensions` is never read
  by `src/game/`, so it **cannot** corrupt logic/aspect/alpha-sort.
  *First step:* change `>2.0f`→`>4.0f` in `gfx_opengl_effective_render_scale()`
  (`gfx_opengl.c:1082`) and `gfx_clamped_render_scale()` (`gfx_pc.c:3337`), and the
  `settingsRegisterFloat` max (`platform_sdl.c:1439`). **Mandatory companion:**
  query `GL_MAX_TEXTURE_SIZE`/`GL_MAX_RENDERBUFFER_SIZE`/`GL_MAX_SAMPLES` once
  (reuse the anisotropy static-query idiom `:946-954`) and clamp the scene-color
  tex + depth RB (`:1175-1179`) **and** MSAA renderbuffers (`:1219-1225`); log once
  if clamped. *A/B:* already gated by `Video.RenderScale`; default 1.0 no-op.
  Ship with per-tier guidance (4× = 16× fragments × both split panes → fill-rate cliff).

### HUD & combat feedback (fill-rect carrier — `gun.c` HUD chain, `bondview.c:19931`)

- **Always-on modern crosshair** — High · Hours · Low · code-only.
  The crisp fill-rect `drawModernAdsReticle` (`gun.c:33191`) is locked behind ADS;
  exposing it for hip-fire gives a razor-sharp, resolution-independent,
  split-screen-pane-aware reticle where the 32×32 N64 texture blurs at 2×.
  *First step:* add `Input.ModernCrosshair` (default 0); in `gunDrawSight`
  (`gun.c:33257`) route to the reticle when enabled. **Mandatory correction:**
  parameterize `drawModernAdsReticle(gdl, cx, cy)` and drive cx/cy + all outline
  offsets from `crosshair_angle.f[0]/f[1]` (`gun.c:33312`) in normal play, pane
  center only under `insightaimmode` — else the reticle desyncs from the bullet ray.
  *A/B:* `GE007_MODERN_CROSSHAIR` default off = byte-identical; `GE007_ADS_RETICLE_*` tune live.

- **Hit markers on bullet impact** — High · Day · Low · code-only.
  The highest-value modern combat cue: instant feedback that a shot connected, with
  head/kill variants. *First step:* model `triggerHitMarker`/`drawHitMarker` on the
  damage-overlay pair (`gun.c:33138-33181`); trigger **only** from the
  `handles_shot_actors` switch arms (`chrlv.c:3353-3376`, gated `isPlayer && hitpart`):
  `SHOT_REGISTER_HEAD`→headshot, death at `chrlv.c:3382` → kill. **Do not** use the
  `chrprop.c:5132/5149` weapon-*fire* path (fires on misses). Draw after
  `gunDrawSight`; decrement the timer in the per-frame draw (not the tick) for
  frame-rate independence. *A/B:* `Input.HitMarkers` default off.

### Input feel — the "feels modern" lever (game-side block, `lvl.c:5784`)

- **Radial deadzone + rescale-from-edge** — High · Hours · Low · code-only.
- **Gamepad aim response curve + tunable deadzone** — High · Day · Low · code-only.
  Right-stick aim is a flat 24% square deadzone + linear ramp; an exponent curve
  (slow fine-aim near center, fast at full deflection) is the biggest pad-player
  "feels good" jump. *First step:* add `Input.GamepadLookCurve` (default 1.0 =
  linear) + `Input.GamepadDeadzone` (default 0.244); apply at `lvl.c:5784-5789`
  (the live consumer is `platformGetPadRightStick`), also wire the tank-turret
  branch (`lvl.c:5834`). Defaults reproduce current behavior exactly. *A/B:*
  `GE007_GAMEPAD_LOOK_CURVE/_DEADZONE`. No effect on mouse.
- **Frame-rate-independent gamepad look** — Medium · Hours · Low · code-only.
  Scale **only** the gamepad term by `g_GlobalTimerDelta` (mouse already accumulates
  raw deltas — do **not** scale it).

### Atmosphere & world (cheap, AI-safe)

- **Per-level color/fog grade table** — High · Hours · Very-low · code-only.
  Per-level atmosphere lives in editable C tables `fog_tables[]` (`fog.c:178-237`),
  each row carrying sky `Red/Green/Blue`, `CloudRed/Green/Blue`, `FarFog`,
  `FarIntensity`, `SkyImageId`. The same `g_CurrentEnvironment` RGB drives both the
  sky tint *and* the in-world fog color (`fog.c:571`), so grading the RGB
  harmonizes haze + sky in one shot. *First step:* nudge only the sky/cloud **RGB**
  columns (e.g. richer Surface dusk) behind `#ifdef GE007_ATMOS_GRADE`, leaving
  `FarFog`/`FarIntensity` byte-identical. **Never touch the distance columns** —
  `FarIntensity` feeds enemy sight (`chrlv.c:5515`). Cover both `#if VERSION_EU` arms.
- **Cosmetic fog-density dial** — Medium · Hours · Very-low · code-only.
  Thins/thickens distance haze on big outdoor levels for crisper sightlines,
  provably AI-neutral (GL-shader fog `fog_mul` is separate from the AI-sight fog
  path). *First step:* at `gfx_pc.c:9902` multiply `fog_z` by a cached
  `g_fogDensityScale` (`GE007_FOG_DENSITY` default 1.0); keep the 0..255 clamp. Do
  **not** touch `fog.c`/`viSetZRange`/`g_ScaledFarFogIntensity`.

### Viewmodel feel

- **Additive viewmodel sway** — Medium · Trivial · code-only. A small `sin(t)`
  idle/move sway on the weapon (`Input.ViewmodelSway` default 0) — instant "remaster feel".
- **Per-gun proportion/scale correction** — Low · Trivial · code-only. Per-item
  root-matrix tweak on top of the existing family rig (`GE007_VIEWMODEL_TWEAK`).

### Effects density (self-contained loop/alloc bounds in `explosions.c`)

- **Raise bullet-decal persistence cap** — High · Very-low · Hours · code-only.
  `BULLET_IMPACT_BUFFER_LEN` 100 → ~400 so bullet holes accumulate for richer
  firefight aftermath. No sim/damage coupling. (Mind the shared `dynAllocateMatrix`
  VTX bump pool — set sane ceilings.)

## Phase 1 — Near-term renderer & feel polish

- **FXAA post-pass** — Medium · Day · Low. Cheap edge AA for the sprites/HUD/alpha
  cutouts MSAA misses; reuses Engine A.
- **Adaptive sharpen (CAS-style)** — Medium · Day · Low. On the output pass; pairs
  with the SSAA cap raise (no-op at ≤2.0×).
- **Extend `FovY` range to ~100–105** + **decouple viewmodel FOV from world FOV**
  (fix gun warp) — Medium · Days · Low. Ship the pair; extending FOV alone warps the
  weapon at the high end.
- **Default MSAA to 4×** and **default VSync to on** — Low/Medium · Hours · per-panel
  validation needed (VSync interacts with the wall-clock-derived sim substep count).
- **Per-level / mood color-grade presets** — Medium · Days. Fast-follow once the
  grade uniforms exist; can be the runtime per-`LEVELID` selection of Engine A's
  `uColorScale`/`uColorBias` (default = identity).
- **Reticle target-acquired color feedback** (dynamic crosshair) + **alpha-to-coverage**
  for cutouts under MSAA — Medium · Hours/Day.
- **Effects caps batch:** flying-debris cap, persistent scorch in split-screen MP,
  explosion-buffer cap (fix the screen-shake coupling) — Medium · Hours each.

## Phase 2 — Texture dump foundation (Engine B, part 1)

The keystone chain. Three commits, no visible change until Phase 3 — but everything
HD downstream depends on this.

1. **Content-hash + token on the existing dump** — High(keystone) · Trivial–Moderate · tooling+code.
   `gfx_diag_dump_loaded_texture` (`gfx_pc.c:6386`) already receives
   `(rgba, fmt, siz, w, h, cache_key)` and writes PPM. Add an FNV-1a/CRC over
   `(decoded rgba + fmt+siz+w+h)` as the filename stem; emit the static token via
   `cache_key & ~GE007_STATIC_TEXTURE_CACHE_KEY_FLAG`. Re-key the dedup table
   (`:6447`) on content hash (drop the frame term so identical textures collapse);
   raise the 512 cap (`:6396`) and the default-64 limit (`:6332`). Keep PPM first to
   prove hash stability across runs. Gate `GE007_DUMP_LOADED_TEXTURES`. Append a CSV
   manifest line per unique hash: `hash,token,fmt,siz,w,h,first_frame,drawclass`.
   *Note:* the settex/static path (`gfx_pc.c:13685`) bypasses `TextureHashmapNode`
   and needs its own dump hook for full coverage.
2. **Vendor `lib/stb_image_write.h`, swap PPM→PNG** (mirror the `lib/glad`
   single-header pattern; add to CMake). Now: run a level → a folder of correctly
   named GoldenEye PNGs ready to upscale.
3. **Plumb `content_hash` onto `TextureHashmapNode`** (`gfx_pc.c:1899`), populated in
   the **miss path only** (cache hit short-circuits at `:7978`) at the decoder tails
   where the dump already runs (`:8128`/`:8170`/…). Unused by rendering until the
   loader reads it → byte-identical when dormant. Proven via trace = dump filenames.

## Phase 3 — The replacement payoff (Engine B, part 2 — the HD moment)

- **Disk-backed token/hash replacement loader** — Transformative(capability) · Hard · code+content.
  On a cache miss, look up `<pack>/<token>.png`; if present, upload **that** at its
  native higher resolution instead of the N64 RGBA. `upload_texture` already accepts
  up to 4096×4096 (`gfx_opengl.c:888`) and UVs normalize by logical tile dims
  (`gfx_pc.c:3276`), so a 2×/4× replacement just works with no geometry change.
  *First step:* new `src/platform/texture_pack.c` (auto-globbed) + vendored
  `lib/stb_image.h`; hook the cache-miss branch **before** the fmt dispatch
  (`gfx_pc.c:8769`): if `GE007_TEXTURE_PACK` set and the texture is static, derive
  the token, try `textures/<token>.png`, `stbi_load`, `upload_texture`, return.
  Prove with **one** hand-upscaled clamped Dam wall end-to-end. Lazy-load + small LRU.
  *Honest caveats the loader must respect:* (a) CI4/CI8 decode against the live
  palette (`gfx_pc.c:8575/8613`) → replacements must be pre-baked full RGBA;
  (b) I/IA textures are prim-color-tinted at draw (`:8531`) → preserve luminance,
  not baked color; (c) skip/special-case `maxlod!=0` LOD textures (`:2742`);
  (d) REPEAT-tiled world surfaces need seam-safe upscaling — scope the v1 demo to a
  clamped wall, not a tiled floor, so we don't ship a seam bug as the headline.
  *A/B:* keyed by on-disk PNG presence — absent pack = identical N64 decode path.
- **alpha/CI correctness guard** (`keep_point`/`is_mask` bit on the node) — Moderate.
  Ships alongside the loader to avoid alpha/palette regressions.

## Phase 4 — Make the HD pack real (content + tooling)

- **`tools/texpack/` repack pipeline** — round-trip dump→repack→in-game **identical**
  first (no upscaler), to prove correctness.
- **Upscaler backends** — Real-ESRGAN for world surfaces, xBRZ for pixel/UI art,
  driven by the manifest `drawclass`.
- **Class-aware HD pack, batch order** — Transformative · content-gated:
  1. **World floors/walls/skies** (clamp/tileable first; tile-before-SR +
     center-crop for REPEAT seams) — max perceived sharpness for least authoring.
  2. **Props** ("for free" — same token path; curate hero props).
  3. **Characters / Bond faces**, then **weapon skins** (just a `drawclass` tag + content).
  Sky/cloud + reflective-water planes (`player.c:1365/913`) come along here too.
- **Replacement-only mipmaps + trilinear/aniso** — Medium · Hard · code-only.
  POT-pad replacements, thread `has_mips` through the sampler API; kills floor
  shimmer on tiling surfaces. (Mipmaps gate the cleanest HD minification.)

> **Honest framing:** this is an HD **diffuse** pack — no normal maps, no geometry.
> CI/palette + REPEAT-seam handling shrink the trivially-safe first batch. Still the
> headline end-user payoff: crisp floors, walls, and faces.

## Phase 5 — Material & shading lane (independent — can start after Phase 0)

Pure code, no art, no texture-pipeline dependency. Runs in the GLSL generator.

- **Material-class detection helper** — High(enabler) · Moderate · code-only.
  A `gfx_material_class_for_draw(cc_id, other_mode_l)` → `{EMISSIVE,METAL,GLASS,SKIN,DEFAULT}`
  classifier (clone the `gfx_diag_settex_cc_color_scale` allowlist machinery,
  `gfx_pc.c:1257-1315`; seed from the per-draw combine census `:10395-10410`).
  Allocate a distinct `SHADER_OPT` bit (`1<<19`; `gfx_cc.h:65`). Lets every shading
  feature target the right pixels safely. Start EMISSIVE-only.
- **Emissive boost for self-lit materials** — Medium · Moderate · code-only.
  Makes lamps/screens/muzzle glow; pairs with Phase-0 bloom. `GE007_EMISSIVE_BOOST`.
- **Fake gloss/spec glint** on gun metal & glass, **rim light** on characters,
  **per-material color-grade** — Low–Medium · Moderate/Hard · code-only. Polish, each opt-in.

## Phase 6 — UI / font deep remaster

- **HD HUD icons "for free"** — High · Moderate · tooling+code then content.
  HUD icons carry the `tconfig->index` enum → static token → they ride Engine B's
  loader directly. RGBA32 icons (crosshair/ammo, `GlobalImageTable.c:523-581`) are
  the easiest. *First step:* once the loader lands, replace one icon with a 4–8× PNG
  and confirm it's sharp at RenderScale=2.
- **HD font glyphs** — Transformative(text is the most-read element) · Hard · code+content.
  Fonts are the exception to Engine B: per-glyph G_SETTIMG uses a raw
  `fc->pixeldata` heap pointer (`textrelated.c:1002,1135`), so glyphs get a *dynamic*
  cache key and need **bespoke keying** (per-glyph-index override at font-load,
  `textrelated.c:438`). Glyphs are prim-tinted and per-glyph-blitted (94 ASCII
  entries, no atlas) → HD glyphs must be I4/IA luminance-only so tinting +
  `textRenderGlow` still work. SDF/vector fonts are infeasible in-pipeline (no RDP
  distance-field path) without a bespoke PC-side rasterizer.
- **Point-filter the UI layer (cheap interim)** — Medium · Moderate · code-only.
  Before HD art exists, force nearest sampling on texrect/UI draws (the path already
  flags `g_texrect_uv_mode`, `gfx_pc.c:875`) so low-res icons stay crisp-pixel
  instead of bilinear-mushy at RenderScale>1. Gate like the existing
  `GE007_FORCE_*_FILTER` knobs.

## Phase 7 — Big bets & research (only with budget to spare)

- **Render-time view interpolation between 60 Hz sim ticks** — Transformative · Week-plus · **Medium-high risk**.
  True high-refresh (120/144 Hz) *visual* smoothness while logic stays 60 Hz. The
  flagship modern win, but the sim is frame-coupled: `osGetCount` is wall-clock
  (`stubs.c:255`), `waitForNextFrame` derives the substep count → `g_ClockTimer`
  (clamped 1..4, `lvl.c:2033-2048`), iterated by ~12 sim loops. Needs an internal
  `lvlRender` pure-draw / mutating-state split **and** RAMROM replay-invariant golden
  validation before it can ship. Do **not** attempt as a quick win.
- **Higher-poly weapon models** — Medium · Very-hard · code+content.
  Phased: (1) model-binary **export** tooling to open the PP7 in Blender (low risk,
  documents the rig/attachment contract via `model_convert.c`); (2) a
  per-gun-model-file-override hook + a re-import compiler (very-hard, content-heavy).
  Wants the texture loader for hires skins too.

---

## Already done — do not re-implement

The verification pass confirmed these are **already live** (proposing them again
would waste effort or cause regressions):

- **Anisotropic filtering** — wired at 8× on bilinear textures (`gfx_opengl.c:944-953`).
- **60 fps** — default `FrameCap=60` (a 2–3× smoothness win over N64's 20–30 fps).
- **Supersampling pipeline** — works today (only the 2.0× cap limits it; Phase 0).
- **Ultrawide / arbitrary aspect** — engine renders pillarbox-free at 21:9/32:9 via
  the live anamorphic squeeze (`gfx_pc.c:9131-9168`); HUD/sky/cull/crosshair already
  registered at arbitrary aspect. No separate internal-aspect FBO is needed.
- **Damage / low-health screen flash** — live via `currentPlayerSetFadeColour`/
  `currentPlayerDrawFade` (`bondview.c:12322`, `:9670`), alpha-blended,
  split-screen-pane-correct. (Only the flash color being white vs red is a tiny RGB
  table tweak, `bondview.c:879`.)
- **HUD text outline/backing** — `textRenderGlow` 8-direction outline + black box
  already ship on ammo/objective text (`gun.c:31330-31333`).
- **Decal age-fade** — `explosionSetBulletImpactAlpha` runs a trailing 10-slot fade
  every spawn (`explosions.c:3179`); oldest decals fade, they don't pop.
- **Back-to-front transparency sort** — the secondary/transparent pass is already
  sorted back-to-front across rooms (`bg.c:2595`).
- **Raw mouse input** — SDL2 relative mode already delivers unaccelerated 1:1 deltas
  by default.

## Out of scope / not worth the cost

Honest down-rankings from the verification pass — spend budget on textures +
post-process, not on fighting baked formats:

- **Per-pixel (Phong) character lighting** — env geometry clears `G_LIGHTING`
  (`bg.c:5428`); no light vector / world-pos / normal reaches the shader. Very-hard,
  low payoff.
- **Viewmodel normal-smoothing** — the PP7 flagship is *unlit, baked-color*
  (`gun.c:14677`); smoothing corrupts it (mechanism mismatch).
- **True character geometry/silhouette remaster** — multi-month rig-constrained
  re-topology of the baked N64 binaries. Keep as a marker only.
- **Anim-rate decoupling spike** — self-declared dead end; translation already lerps
  (`model.c:2792`).

---

## Validation & safety conventions

Every remaster commit must:

1. **Default off / identity** — verify with all flags off the frame is byte-identical
   (screenshot hash, render-health counters).
2. **Carry a `GE007_*` A/B escape hatch** and (where user-facing) a `Video.*`/`Input.*`
   settings key.
3. **Screenshot A/B** — `tools/compare_screenshots.py` on/off, on the level that best
   exercises it (e.g. Dam/Surface for fog/atmosphere/SSAA; firefight scene for
   decals/hit-markers).
4. **Sim-touching changes** (anything in the framerate/pacing lane, VSync default)
   need **RAMROM replay-invariant golden validation** — pure-presentation is *not*
   automatically decoupled here.
5. **Run `tools/playability_smoke.sh --all`** (broad native gate + contact sheet) and
   `tools/soak_stability.sh` for anything in the hot draw path.

## Provenance

Compiled 2026-06-24 from two adversarially-verified multi-agent surveys against the
live `feat/split-screen-multiplayer` tree: a 12-lane renderer-enhancement survey (64
opportunities) and a 9-lane asset-remaster survey (41 opportunities), plus targeted
follow-ups for the skybox-atmosphere and HUD/UI/font lanes. Every impact/risk/
difficulty rating was challenged by an independent verifier reading the actual code;
"already done" and "infeasible" items were filtered out before ranking.
