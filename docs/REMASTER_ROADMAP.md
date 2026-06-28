# MGB64 Remaster — Master Plan & Golden Path

**The single authoritative roadmap** for taking MGB64 from a faithful port to a
modern, AAA‑leaning remaster — modern textures, modern lighting, modern feel —
**without changing the original game's engine, dynamics, or feel, and without ever
shipping a copyrighted asset.**

> This rewrite replaces the earlier survey-shaped roadmap and **folds in the
> now-removed `PHASE2_PLAN.md` (texture-pipeline plan) and `MODERN_LIGHTING_ROADMAP.md`
> (lighting assessment)** — this is the one plan to sprint against. Operational "how do
> I turn these on" detail lives in its companion [VISUAL_MODES.md](VISUAL_MODES.md); the
> offline texture tools live in [tools/texpack/](../tools/texpack/README.md).
>
> **On the anchors:** `file:line` references are function‑level and may drift by a few
> lines as the tree changes — they name the right construct; re‑verify the exact line
> before relying on it.

---

## 0. North star

A player should be able to launch MGB64 and pick anywhere on a slider from
**pixel‑faithful 1997** to **"is this really GoldenEye?" 2026** — and the *game*
underneath (timing, AI, collision, accuracy, speedrun routes) is **bit‑for‑bit the
original** at every setting. We get as close to modern AAA rendering as a forward,
combiner‑driven N64 renderer allows, and we are honest about the ceiling.

Three rails govern **every** item in this plan. They are non‑negotiable:

1. **Gameplay‑invariant by construction** — the simulation never reads render state.
2. **Copyright‑bulletproof** — the repo ships only first‑party code, public‑domain
   libs, PD/CC0/original art, vetted open‑licensed art, and *pipelines*.
   ROM‑derived assets are generated locally and never committed.
3. **Opt‑in / default‑identity** — every feature is flagged; with everything off the
   build is byte‑identical to the faithful port.

---

## 1. The rails (read this before building anything)

### 1a. Gameplay invariance — *why we can do all of this safely*

The port's defining structural asset: **render and simulation are physically
separate.**

- Collision, AI line‑of‑sight, and floor navigation read the **baked source mesh on
  the CPU** — `bgTestLineIntersectionInRoom` (`bg.c:9750`) walks the room collision
  DL and its `Vtx` pool; `stan.c` StandTiles drive navigation. **None of them ever
  touch GL, the framebuffer, or any material/lighting state.**
- The renderer consumes that same `const Vtx *` strictly **read‑only**, transforming
  into a private `rsp.loaded_vertices[]` (`gfx_pc.c:9692`, in `gfx_sp_vertex`) — render transforms never
  write back to the collision source.
- The **only** render→sim coupling is frame timing: `g_ClockTimer` = wall‑clock
  delta frames (`lvl.c:2042`), already clamped to 1–4 ticks on PC (`lvl.c:2053-2056`) and
  **bypassed for RAMROM replay** (`lvl.c:2050`) so recorded runs stay bit‑identical.

**Consequence:** any texture/lighting/post‑FX change is gameplay‑neutral *as long as
the sim tick never reads GL / material / FBO state and never stretches a tick.* That
is cheap to **enforce**, but the enforcement is **not built yet**; building it is
step 1 of the sprint (§6, P0):

- **CI invariance gate (planned)** — a fixed RAMROM replay must produce the same
  end‑state sim hash with rendering features on vs off.
- **Static enforcement (planned)** — sim translation units may not reference
  render/material/GL symbols (build error, not a heisenbug).
- **Timing lock** — keep the `g_ClockTimer` clamp; document the RAMROM bypass as the
  determinism anchor.

### 1b. Copyright — *the bulletproof model*

GoldenEye's art is Nintendo/Rare's. We never redistribute it, in any form, including
upscaled, recolored, histogram‑matched, or metadata‑reconstructable derivatives.
The model has three buckets:

**Asset tiers**

| Tier | What | Distributable? | How it ships |
|---|---|---|---|
| **A1 — First‑party / PD / CC0 / original** | Engine code; `stb_*` (public domain); the texture **pipelines & generators**; public‑domain / CC0 imports with provenance; **original** hand‑authored art; procedural textures generated with **generic tone presets**. | ✅ Yes | Committed to the repo / shipped in releases. |
| **A2 — Open‑licensed third‑party art** | CC‑BY / CC‑BY‑SA / similar assets from sources such as OpenGameArt, after per‑asset provenance review. | ✅ Only after review | Committed only with `NOTICE` attribution and a share‑alike / downstream‑compatibility check. GPL art is not assumed compatible with a texture pack. |
| **B — ROM‑derived** | Raw texture dumps; **AI‑upscaled** originals; procedural textures **tone/histogram‑matched to a ROM original** (`--match`). | ⛔ Never | Generated **locally by the user from their own ROM**; gitignored; never committed. |

> **The procedural nuance, made airtight:** `synth_texture.py` output is 100%
> first‑party math, but its *inputs* control its tier. Run with generic tone
> (`--mean/--sd`) it is **Tier A1**. Run with `--match <rom dump>` it inherits the
> original's tonal distribution and is treated as **Tier B**, local‑only, exactly
> like an upscale. This tier split is an advisory/project convention, not a
> cryptographic gate; the tool now forces `.png` output, but it cannot prove whether
> a chosen tone came from a ROM dump. **Default release stance: ship the generator +
> generic presets; ROM‑matched packs are user‑built.**

**Enforcement machines**:

1. **gitignore** — `*.png`/`*.bmp`/`*.ppm`/`*.pgm`/`*.pnm`, dump/pack dir patterns,
   `*.texmanifest.csv` (`.gitignore`).
2. **Contamination guard** — `scripts/ci/check_no_rom_data.sh` (pre‑commit and
   release CI) **hard‑fails on tracked image/capture formats** and opaque asset blobs.
   Its >3 MB exemption is intentionally narrow: large `*.csv`/`*.json` files are not
   globally allowed, and the planned `*.texmanifest.csv` carries ROM‑derived
   `avgRGB` and stays ignored/local even when small.
3. **Runtime is opt‑in & local** — the loader reads `Video.TexturePack` (default
   empty = no filesystem touch); dumps write out‑of‑tree.

Code provenance (clean‑room SDK/libultra surfaces) is tracked separately in
[PROVENANCE_AUDIT.md](PROVENANCE_AUDIT.md) and `THIRD_PARTY.md` — out of scope here,
but the same "ship code, not proprietary content" principle.

### 1c. Opt‑in / default‑identity

Every item lands behind a `GE007_*` env and/or `Video.*`/`Input.*` settings key, and
with all flags at their identity value the frame is byte‑identical to the faithful
port. The three user‑facing **modes** (faithful · faithful‑upscale · full remaster)
and the full flag table are in [VISUAL_MODES.md](VISUAL_MODES.md).

---

## 2. Where we are today

### The two carrier "engines" (build once, unlock dozens of features)

- **Engine A — the fullscreen output pass.** One post pass already runs each frame
  (`gfx_opengl_apply_output_vi_filter`). It carries **grade, tonemap, bloom,
  vignette, sharpen, dither, FXAA**, gated by `Video.RemasterFX` +
  `gfx_opengl_output_color_adjust_active()`. *This is where all screen‑space FX live.*
- **Engine B — the texture replacement hook.** The static **G_SETTEX** decode path
  calls `texture_pack_try_load(token, …)` (`gfx_pc.c:13782`; loader in
  `texture_pack.c:35`) *after* the N64 texels decode: if `<pack>/textures/tok####.png`
  exists it uploads that at higher res instead. Native tile dims are preserved
  (`gfx_pc.c:13772-13778`, cached at `:13856`) so **UVs are unchanged — substitution
  is gameplay‑neutral.** Bad/oversize asset → falls back to stock (`gfx_pc.c:13789`).
  The loader only probes zero‑padded static settex tokens (`texture_pack.c:45`);
  it has **no hash‑key path** for `GE007_DUMP_LOADED_TEXTURES` dumps.

### Status

| Status | Area | Notes |
|---|---|---|
| **Shipped** | Phase 0/1 visual and feel flags | Grade, dither, vignette, bloom, SSAA cap 4×, FXAA, adaptive sharpen, per‑level mood grade, FovY range, viewmodel‑FOV decouple, alpha‑to‑coverage, reticle feedback, effects caps, gamepad aim curve/deadzone, modern crosshair, hit markers; identity‑off validated in the implementation branch. |
| **Shipped** | HD texture loader (Engine B) | Static settex replacement only; `textures/tok####.png` is the runtime contract (`texture_pack.c:45`). Default empty `Video.TexturePack` is stock. |
| **Shipped** | `build_pack.py` Real‑ESRGAN pipeline | Static settex dumps now emit zero‑padded loader keys; seam‑safe tiling and tiny‑texture Lanczos are in place. Hash‑key loaded‑texture dumps are skipped because the loader cannot consume them yet. |
| **Shipped tool, prototype content** | `synth_texture.py` gravel + rock generators | The script and two presets exist; the tone‑match/look validation is **Dam‑only** so far (`tok0022` ground win, `tok0949` rock rejected as a geometry/UV seam problem). |
| **Not built** | `texmanifest` C emit | `build_pack.py` can read `*.texmanifest.csv`, but no C dump path emits it yet. |
| **Not built** | Router / `--cc0-library` / broader synth library | The per‑surface Router, manifest‑driven open‑licensed substitution, and synth presets beyond gravel/rock are target architecture, not current tooling. |
| **Not built** | CI sim invariance + static sim/render separation | No `scripts/ci/check_sim_render_separation.sh` exists, and `.github/workflows/ci.yml` has no invariance job yet. This is §6 P0. |

**Honest ceiling (established):** this is a beautiful **remaster**, not native PBR.
Env geometry clears `G_LIGHTING` (`bg.c:5440`) → **no normals reach the shader** →
real normal‑mapping/specular needs the renderer work in §4 Track 2. Everything in the
screen‑space / texture / AA / grade space is reachable now.

---

## 3. Modern textures — the blended strategy

There is no single best texture source. The win is **routing each surface to the
right one.** The table below is the **target production architecture**; §2 is the
current tooling truth.

### The four sources

| Source | Best for | License tier | Tool |
|---|---|---|---|
| **AI upscale** of your own static settex dump (Real‑ESRGAN) | *Detailed* art: signs, crates, props, character skins, weapon skins, HUD icons — anything with real structure to recover | **B** (ROM‑derived, local) | `build_pack.py`; static `tok####` only |
| **Procedural synthesis** (first‑party noise) | *Tiled* surfaces with no recoverable detail: ground/gravel, large floors, sand, concrete | **A1** generic / **B** if `--match` | `synth_texture.py`; gravel/rock exist today |
| **Open‑licensed import** (PD/CC0 sources such as ambientCG / Poly Haven; mixed-license sources such as OpenGameArt only after per‑asset review) | Generic surfaces with a good independent match: metal, wood, brick, fabric | **A1** if PD/CC0; **A2** if attribution/share‑alike applies | `build_pack.py --cc0-library` *(not built, §6 P2.4)* |
| **Original hand‑authored** | Hero surfaces that need specific art and have no good match | **A1** (distributable) | any editor → pack |

### The decision tree (per surface, keyed off the `texmanifest`)

```
Is the surface tiled across large geometry (ROOM floor/ground/wall)?
├─ YES → does the original have recoverable detail at 4×?
│        ├─ NO  (tiny, e.g. 64×32 gravel) → PROCEDURAL synth (tone-matched, tile-uniform)
│        └─ YES, and a good PD/CC0/open-licensed match exists → open import (seam-safe)
│        └─ YES, no good match → AI upscale (seam-safe tile-3×3 → crop)
│   ⚠ but if the surface uses INDEPENDENT PER-QUAD UVs (e.g. Dam rock walls),
│      high-detail replacement EXPOSES the per-quad seams → LEAVE STOCK,
│      fix via lighting (§4 T1.3 smooth env normals), not texture.
└─ NO (detailed art: signs/props/faces/weapons/HUD) → AI upscale (whole-image)
```

### What we learned building the prototype (Dam)

Validated in‑game, frame‑accurate:

- **Identified the hero surfaces** by hue‑encoding every token into a throwaway pack
  and reading the on‑screen color back (vertex shading is a grayscale multiply, so
  hue survives exactly): **ground = `tok0022`** (64×32), **rock wall = `tok0949`**
  (64×64). *Reusable technique for any level.*
- **The AI upscaler degrades tiled hero surfaces** because the source is tiny: it
  smoothed the gravel into a flat smear and melted the rock speckle into oily blobs.
  Detailed art upscales fine — only big tiled tiles suffer.
- **Procedural synth fixes the ground decisively in the Dam prototype.**
  `synth_texture.py` builds
  surfaces from **band‑pass + FFT spectral noise** (periodic ⇒ seamless & isotropic,
  no grid, no banding), then can **histogram‑match** the original (exact tone +
  bright flecks ⇒ reads identically under the baked lighting, but therefore Tier B)
  and **high‑passes** to
  tile‑uniform (no cloudy macro‑blotches across the plane). The Dam ground is a clear
  win over both the washed AI version and the blocky stock.
- **The rock walls are a geometry problem, not a texture problem.** Dam's cliffs use
  independent per‑quad UVs; *any* high‑detail/high‑contrast texture exposes the seams
  (reads as "tiger stripes"). Stock's low‑res rock hides them. **Correct fix is
  smooth‑shaded env normals (§4 T1.3), not a texture swap.** Rock left stock.

### The pipeline that ties it together

1. **`texmanifest` emit** *(planned, §6 P1)* — at dump time, write one CSV row per
   token: `token,w,h,fmt,siz,avgRGB,tileable,draw_class`. The `DrawClass`
   (ROOM/CHRPROP/HUD/WEAPON/EFFECT) already exists in‑engine (`gfx_pc.c:436`);
   `build_pack.py` already *reads* this CSV (`build_pack.py:129`) but **no C code
   emits it yet.** This is the substrate the router needs.
2. **Router** *(not built, §6 P2.3)* — per token, the decision tree above picks a
   source; high‑confidence ROOM tiles auto‑route, hero surfaces get a hand‑curated
   override JSON.
3. **Per‑source tool** — AI upscale / procedural / open import / original.
4. **Pack** — `<pack>/textures/tok####.png`; the loader is unchanged.

### The upgrade path: material maps

The pack is **HD diffuse only** today. Per‑token `_n`/`_r`/`_ao` sidecar maps are a
clean extension of the loader *but render nothing without §4 Track 2* (no lighting to
feed them, no free sampler — uTex1 is taken by the N64 combiner). So material maps are
**gated on the lighting track** and scheduled there.

---

## 4. Modern lighting & rendering

**The obstacle:** env geometry clears `G_LIGHTING` (`bg.c:5440`) and draws with baked
vertex colors — the N64 `Vtx` is a union where normals and color share 4 bytes
(`include/PR/gbi.h:871`, members `cn[4]` vs `n[3]`), so for env surfaces those bytes *are* color
and **there are genuinely no normals in the data.** The VBO carries only clip‑space
position + texcoords + fog + combiner colors (`gfx_pc.c:11445`); the vertex shader is
a near‑passthrough (`gl_Position = aVtxPos`, `gfx_opengl.c:570`); the fragment shader just samples
`uTex0/uTex1` and runs the combiner. **Normal maps have nothing to plug into yet.**

This splits cleanly into two tracks.

### Track 1 — modern look **without normals** (the 80/20: high value, bounded risk)

| Step | What | Effort | Risk | Payoff |
|---|---|---|---|---|
| **T1.1 Depth texture** | Scene FBO depth is a `GL_DEPTH_COMPONENT24` *renderbuffer* (`gfx_opengl.c:1267`), unreadable. Make it a sampleable texture; feed `rsp.P_matrix` (`gfx_pc.c:2624`) + z‑convention to the post pass for linearization. | days | low | *Unlocks everything below* |
| **T1.2 SSAO** | Depth‑only horizon/hemisphere AO + blur in the output pass; reconstruct a coarse normal from depth derivatives. No G‑buffer, no authored normals. | weeks | medium | **High** — the single biggest "modern" cue per unit effort (contact darkening in corners/under props) |
| **T1.3 Smooth env normals** | Derive per‑face/averaged geometric normals at draw time from reconstructed positions; light them with the existing global directional light (`GlobalLight`, `bg.c:1221`). **No mesh/collision change.** | weeks | medium | **High** — directly fixes the ground banding *and* the rock‑facet seams that defeat texture replacement |
| **T1.4 Sun shadow map** | One light‑view depth FBO for `GlobalLight`; replace the flat blob shadow (`model.c:10850`) with a real cast shadow for player + characters. | weeks | medium | **High** — first real cast shadows |

### Track 2 — per‑pixel lit materials (transformative, **gated**, months)

Must land in order; each step is dead weight without the next.

| Step | What | Effort | Risk |
|---|---|---|---|
| **T2.1 VBO plumbing** | Extend `LoadedVertex` (already keeps object‑space `ob[3]` + `room_id`, `gfx_pc.c:1891`) and the VBO to carry **world position + a tangent basis**; pass the live modelview captured in `gfx_sp_matrix` (`gfx_pc.c:9099-9107`). **Enabling prerequisite for all per‑pixel lighting.** | weeks | medium |
| **T2.2 Geometric‑normal lighting** | With world‑pos in the FS, compute a face normal via `dFdx/dFdy` and apply `GlobalLight` — real per‑pixel directional shading, **no authored normals.** Cheapest real lighting once T2.1 lands. | days | low |
| **T2.3 Normal/roughness maps** | Load `tok####_n/_r.png` sidecars to **new** GL units (uTex1 is taken), inject sampler + lighting into the generated FS for `draw_class==ROOM` only. *This is where §3's material maps finally render.* | months | medium |
| **T2.4 Tangent hardening** | Mirrored UVs, wrap seams, per‑vertex vs per‑face bases. | weeks | medium |
| **T2.5 Character normals** | Recover true authored normals on the *character* path (G_LIGHTING already set there, `gfx_pc.c:9861`) — isolated win, doesn't need T2.1. | weeks | low |

### Phase 4 territory — deferred / PBR / dynamic lights (assess‑and‑defer)

The backend has no MRT/G‑buffer concept (one `out vec4 fragColor`,
`gfx_opengl.c:584`); there are **no authored point/spot lights** (`g_CurrentEnvironment`
holds only fog + sky colors, `fog.c:160`); `lightfixture.c` is a "shoot‑the‑lamp"
gameplay effect (and stubbed PORT_TODO on native), not a light system. A true deferred
PBR pipeline (MRT G‑buffer, authored lights, their shadow maps) is **multi‑month,
major‑rewrite**. **Recommendation: defer.** If ever pursued, the bounded version is
**hybrid** — keep the forward main pass, deferred *only* for tagged hero materials.

---

## 5. Modern feel — gameplay‑faithful polish (already largely shipped)

The "feels modern" levers that do **not** touch the sim (all flagged, identity‑off):
modern crosshair, hit markers, reticle target feedback, gamepad aim curve + radial
deadzone, fps‑independent look, decoupled viewmodel FOV, viewmodel sway, FovY range,
effects‑density caps. Shipped in Phase 0/1 (§2). The one **big bet** left here:

- **Render‑time view interpolation between 60 Hz sim ticks** — true 120/144 Hz visual
  smoothness while logic stays 60 Hz. Flagship modern win, but the sim is
  frame‑coupled (`g_ClockTimer`), so it needs a pure‑draw / mutating‑state split *and*
  RAMROM‑invariant golden validation. **Week‑plus, medium‑high risk — not a quick
  win.** Belongs after Track 1.

---

## 6. THE GOLDEN PATH (sprint‑ordered backlog)

The order I'd actually build. Each step is independently landable, flagged, and
A/B‑testable. **Stop‑and‑evaluate** checkpoints are marked ◆. Through P4 *nothing
touches the simulation.*

**P0 — Rails (days, do first).** Build the enforcement that §1a describes.

| Item | First steps | Acceptance |
|---|---|---|
| **P0.1 Static sim/render separation** | Add `scripts/ci/check_sim_render_separation.sh`; build native objects; use `nm -u` (`otool -Iv` fallback on macOS only if needed) to reject OpenGL `gl[A-Z]*`/`GL_*`, `gfx_*`, `gfx_opengl_*`, `texture_pack_*`, `g_pcTexturePack`, material/FBO symbols from simulation owners. Allowlist renderer/platform TUs, diagnostics, and explicit one‑way bridge declarations. Wire it into `.github/workflows/ci.yml`, which currently has no such step. | A seeded violation in a throwaway local branch fails the script; the real tree passes; CI runs the script before native build. |
| **P0.2 RAMROM sim hash gate** | Add a native `--sim-state-hash-out <json>` or equivalent script. Hash the native logical equivalent of N64 mutable game RAM: `.data`/`.bss` game symbols corresponding to `__dataSegmentVaddrStart.._dataSegmentVaddrEnd` and `_bssSegmentStart.._bssSegmentEnd` (`ge007.ld:196-223`), excluding platform/render caches, framebuffers/stacks, allocator bookkeeping, trace counters, and logs. Use the symbol map on native because those regions are not one contiguous host address range. | `ramrom_Dam_1` (or alias `dam1`) run to deterministic frame **3600** hashes identical with all remaster flags at identity vs stress‑ON (`Video.RemasterFX=1`, `Video.RenderScale=4`, max post‑FX, local texture pack if present). The JSON records the include/exclude symbol sets, frame, ROM region, replay name, and hash. `tools/compare_state.py`/render‑health stays green as a bootstrap, but the P0 gate is not accepted until the hash exists. |
| **P0.3 Timing lock** | Keep the PC clamp and RAMROM bypass in `lvl.c` (`g_ClockTimer` assignment and replay bypass at `lvl.c:2042-2056`). Add a small source check or unit assertion so future remaster code cannot write `g_ClockTimer`. | RAMROM trace reports replay block `speedframes` unchanged through `pc_ramrom_trace_state` (`ramromreplay.h:42-80`, `port_trace.c:5169-5182`); no remaster TU defines or writes `g_ClockTimer`; P0.2 hash is unchanged by render load. |

**P1 — Foundation (days).**

| Item | First steps | Acceptance |
|---|---|---|
| **P1.1 Sampleable depth texture** | Replace the scene depth renderbuffer (`gfx_opengl.c:1267-1277`) with a `GL_DEPTH_COMPONENT24` texture attached to the scene FBO; preserve resize and fallback behavior. Feed `rsp.P_matrix` (`gfx_pc.c:2623-2624`) and the backend z convention into the output pass for linearization. | With the feature flag off, canonical Dam frame screenshots are byte‑identical and P0 hash matches. With it on, a debug depth capture at Dam frame **180** is nonblank/nonflat, has near/far ordering correct on foreground guardrail vs sky, and render‑health stays zero. |
| **P1.2 `texmanifest` C emit** | Add an opt‑in dump beside `GE007_DUMP_SETTEX_TEXTURES`: one CSV row per static token, `token,w,h,fmt,siz,avgRGB,tileable,draw_class`. `DrawClass` tagging already exists (`gfx_pc.c:435-437`); `build_pack.py --route` already consumes the manifest. | On a Dam dump, row count equals the unique static settex token count observed in the trace, `tok0022` is `ROOM`, and `build_pack.py --route` chooses the manifest model class. Manifest files remain gitignored because `avgRGB` is ROM‑derived metadata. |

**P2 — Texture remaster, productionized (separate deliverables).**

| Item | Effort | Acceptance |
|---|---|---|
| **P2.1 AI upscale hardening for static settex** | days | Synthetic dump fixture `ge007_settex_0022.rgba.ppm` produces `textures/tok0022.png` and never `tok22.png`; hash‑key `loaded_tex_*` dumps are skipped or fail with a clear message because the loader has no hash path (`texture_pack.c:45`). Alpha sidecars still round‑trip. |
| **P2.2 Procedural ground/gravel production path** | days-week | `synth_texture.py` generic preset output (`--mean/--sd`, fixed seed) has a stable SHA256 and writes only `.png`. Dam ground capture `dam_ground_tok0022_generic` compares to the accepted local reference with `tools/compare_screenshots.py --max-changed-pct 0.50`; render‑health passes. `--match` remains documented as Tier B/local‑only. ◆ |
| **P2.3 Router** | week | Given a manifest + hand override JSON, the Router emits a deterministic plan (`token → source,tier,tool,args`) and refuses any distributable pack plan containing Tier B assets. The plan is reviewable text; generated texture assets stay local. |
| **P2.4 Open‑licensed substitution mode** | week | `--cc0-library` or successor only ingests per‑asset records with URL, license, author, file hash, and transform. PD/CC0 assets need provenance; CC‑BY/CC‑BY‑SA assets require `NOTICE`; share‑alike/GPL compatibility is checked before a pack can be marked distributable. |
| **P2.5 First level production pass** | week+ | Dam pack uses AI only for detailed art, generic/procedural for ground where accepted, stock for rock walls until T1.3. Screenshot refs are local artifacts, never committed; only route JSON, manifests, hashes, and notices are reviewable. |

**P3 — Modern lighting, no‑normals (weeks each).**
 1. **SSAO** (§4 T1.2) — biggest modern cue per effort. ◆
 2. **Smooth env normals** (§4 T1.3) — fixes ground banding + rock seams (the surfaces
    texture work *couldn't* fix). ◆
 3. **Sun shadow map** (§4 T1.4) — first real cast shadows.

**P4 — Per‑pixel materials, open the door (months).** **T2.1 → T2.2** (§4 Track 2).
◆ **Evaluate here:** does the look still demand normal maps? If yes → T2.3 (and §3
material maps light up). If the remaster is already strong enough, **stop**.

**P5 — Big bets (only with budget).** Render‑time view interpolation (§5);
character normals (T2.5); hybrid deferred for hero materials (§4 Phase 4 territory).
SSR and full deferred PBR remain **out of scope** (§8).

---

## 7. Validation & safety conventions (every commit)

**Canonical local harness** (artifacts stay out of git):

1. Build `build/ge007`; run with `SDL_AUDIODRIVER=dummy`,
   `GE007_DETERMINISTIC_STABLE_COUNT=1`, `GE007_NO_VSYNC=1`, `GE007_BACKGROUND=1`,
   `GE007_NO_INPUT_GRAB=1`, `--deterministic`, `--trace-state`, and
   `--screenshot-frame N` (the existing smokes use this pattern in
   `tools/playability_smoke.sh:318-339` and `tools/renderer_parity_capture.sh:101-118`).
2. Capture a baseline with identity flags:
   `Video.RemasterFX=0 Video.TexturePack= Video.RenderScale=1 Video.MSAA=0
   Input.ModernCrosshair=0 Input.HitMarkers=0`.
3. Capture the feature variant with only the target flag(s) changed.
4. Run `tools/audit_screenshot_health.py`, `tools/audit_render_trace.py`, and
   `tools/compare_screenshots.py --max-changed-pct <feature threshold>`; use
   `tools/compare_state.py` for the existing JSONL bootstrap until P0.2 adds the
   end‑state sim hash.
5. For P0 and any sim‑adjacent change, also run the RAMROM hash lane once P0.2
   exists: same replay, same final frame, identical sim hash over the documented
   mutable game state region, flags‑off vs flags‑on.

**Per‑commit rules**:

1. **Default‑off / identity** — verify byte‑identical frame with flags off (screenshot
   hash + render‑health counters).
2. **`GE007_*` A/B escape hatch** + (user‑facing) a `Video.*`/`Input.*` key.
3. **Screenshot A/B** on the level that best exercises it (Dam/Surface for
   ground/fog/SSAA; a firefight for decals/hit‑markers).
4. **Sim‑adjacent changes** (anything timing/pacing) need **RAMROM replay‑invariant
   golden validation** — presentation is *not* automatically decoupled there.
5. **`tools/playability_smoke.sh --all`** broad gate + **ASan/UBSan** on hot‑draw‑path
   code. **Contamination guard stays green** (no tracked images).

---

## 8. Out of scope / not worth the cost

Honest down‑rankings — spend budget on §3 textures + §4 Track 1 (T1.1–T1.4), not these:

- **Per‑pixel character lighting without Track 2** — no light vector/normal reaches
  the shader; very‑hard, low payoff until T2.1.
- **SSR (screen‑space reflections)** — mostly matte content; months, high risk.
- **Full deferred G‑buffer PBR** — major rewrite, no authored lights to justify it.
- **Geometry re‑topology / mesh replacement** — multi‑month, rig‑constrained; the
  facet problem is *lighting* (§4 T1.3 smooth normals), not geometry.
- **SDF/vector fonts** — no RDP distance‑field path; needs a bespoke PC rasterizer.
- **Viewmodel normal‑smoothing** — the PP7 is unlit baked‑color; smoothing corrupts it.

---

## Appendix — provenance of this plan

Consolidated 2026‑06‑25 from: the original 12‑lane renderer + 9‑lane asset remaster
surveys (Phases 0–7, ~128 verified subagents); the 5‑area HD‑texture‑pipeline plan
(areas T‑A..T‑E, loader now shipped); the in‑game procedural‑texture prototype (Dam
ground/rock, hue‑token identification, histogram/high‑pass synthesis); and the 6‑lane
modern‑lighting/textures code audit (31 items: texture‑substitution,
normal‑mapped‑lighting, dynamic‑lights‑shadows, deferred‑PBR, screen‑space‑SSAO,
geometry‑invariance). Behavioral claims were verified against the live
`feat/dam-hd-remaster` tree; `file:line` anchors are function‑level and may drift a few
lines (re‑verify before relying on exact numbers). Supersedes `PHASE2_PLAN.md` and
`MODERN_LIGHTING_ROADMAP.md` (removed; folded here).
