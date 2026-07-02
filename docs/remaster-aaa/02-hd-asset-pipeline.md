# W2 — HD Asset Pipeline at Scale (AI + Procedural + Open-Source, All 20 Levels)

> Workstream 2 of the MGB64 AAA Remaster program. Constitution:
> [`docs/REMASTER_ROADMAP.md`](../REMASTER_ROADMAP.md) §1 — the three rails govern
> every task below. **Doc-local shorthand**: R1 = §1a gameplay invariance,
> R2 = §1b copyright tiers A1/A2/B, R3 = §1c opt-in/default-identity.
> ⚠ Do not confuse with the roadmap §6 **P0 gate** numbering (there R1 = the
> sim/render nm-check, R2 = the timing lock, R3 = the sim-invariance hash gate —
> those are the *enforcement machines* for rail §1a).
> Branch baseline: `feat/metal-backend`. All `file:line` anchors verified 2026-07-02.

---

## 1. Executive summary

Today MGB64 has a **prototype** HD texture path: a shipped in-engine loader
(`src/platform/texture_pack.c`), a Real-ESRGAN upscale script
(`tools/texpack/build_pack.py`), a two-preset procedural generator
(`tools/texpack/synth_texture.py`), and exactly **one production level (Dam)**.
This workstream turns that prototype into a **production asset factory for all 20
solo levels plus weapons, characters and HUD**: a dump-side manifest emitted by the
engine (P1.2), a deterministic **Router** that assigns every texture token to the
right source per the roadmap §3 decision tree (P2.3), an open-licensed CC0/CC-BY
ingestion path with provenance and NOTICE generation (P2.4), an **8-preset procedural
library** (6 new presets + the 2 shipped), hardened per-class AI upscaling, AI-derived **material sidecars**
(normal/roughness) ready for W1's per-pixel lighting, and a **per-level QA harness**
that makes "level N is done" a runnable command, not an opinion. Everything stays
inside the copyright tiers: Tier-B (ROM-derived) output never leaves the user's
machine; the repo ships only generators, routers, manifest-free plans, and vetted
A1/A2 art.

| # | Headline deliverable | Roadmap item |
|---|---|---|
| 1 | `texmanifest` C emit — the engine writes `token,w,h,fmt,siz,avgRGB,tileable,draw_class` per static settex token | §6 P1.2 |
| 2 | Router (`route_pack.py`) — deterministic per-token source plan; hard-refuses Tier B in distributable packs | §6 P2.3 |
| 3 | Open-licensed ingestion (`cc0_library.py` + `build_pack.py --cc0-library`) with provenance records + NOTICE | §6 P2.4 |
| 4 | Synth preset library ×8 total (6 new: concrete, metal panel, wood, sand, snow, brick — plus existing gravel, rock) + AI material sidecars | §3 / feeds W1.E5 (roadmap T2.3) |
| 5 | 20-level production process + QA harness (`pack_qa.sh`, `validate_pack.py`) — every level demoable & gated | §6 P2.5 scaled |

---

## 2. Current state (verified in code)

### 2.1 Runtime loader (Engine B) — shipped, contract frozen

- Hook point: the static **G_SETTEX** decode path. `gfx_handle_settex()`
  (`src/platform/fast3d/gfx_pc.c:20671`) decodes N64 texels to RGBA32
  (`gfx_pc.c:20744-20758`), then — **after** decode, before GPU upload — calls
  `texture_pack_try_load(texturenum, &hd_rgba, &hd_w, &hd_h)`
  (`gfx_pc.c:21061`). On success it uploads the HD pixels instead
  (`gfx_pc.c:21062-21067`); on backend rejection it **falls back to native texels**
  (`gfx_pc.c:21068-21073`), so a bad asset degrades to stock, never breaks.
- **UV invariance** (why substitution is gameplay- and layout-neutral): only the
  uploaded pixels/dims change; the logical tile dims stay native
  (`gfx_pc.c:21051-21057` comment + `settex_cache[slot].tex_w/h = w/h` at
  `gfx_pc.c:21137-21138`), so the GPU samples the HD image over the same [0,1] UVs.
- Loader contract (`src/platform/texture_pack.c`): key =
  `<pack>/textures/tok%04d.png` (`texture_pack.c:75`), token range 0..4095
  (`TEXPACK_MAX_TOKENS`, `texture_pack.c:24`; token = `w1 & 0xFFF`,
  `gfx_pc.c:20672`), per-token miss cache (`texture_pack.c:29`), one-time
  pack-dir validation warning (`texture_pack.c:40-62`). Enabled solely by non-empty
  `Video.TexturePack` / `GE007_TEXTURE_PACK` (`platform_sdl.c:220`, registered at
  `platform_sdl.c:1709`); the `--faithful` preset forces it empty
  (`platform_sdl.c:1921`) — R3 identity is preserved by construction.
- **Size cap 4096×4096 on BOTH backends**: GL rejects larger
  (`gfx_opengl.c:1483-1484`), Metal mirrors the guard exactly
  (`gfx_metal.mm:1555-1560`). Pack assets above 4096 silently render stock — the
  validator (E7) must catch this offline.
- The loader has **no hash-key path**: `GE007_DUMP_LOADED_TEXTURES` dumps
  (`gfx_pc.c:11948-11969`) are not loadable. Static settex only.

### 2.2 Dump side — images yes, manifest no

- `GE007_DUMP_SETTEX_TEXTURES=<list|*>` + `GE007_DUMP_SETTEX_DIR` dump
  `ge007_settex_%04d.rgba.ppm` + `.alpha.pgm` + `.info.txt` per token
  (`gfx_pc.c:12237` fresh-decode, `gfx_pc.c:12446` cache-hit variant; dir env read
  at `gfx_pc.c:12287`). The dump block already computes alpha-weighted avg RGB
  (`gfx_pc.c:21000-21035` pattern).
- `DrawClass` tagging exists: enum at `src/platform/gfx_pc.h:68-77`
  (`ROOM/WEAPON/CHRPROP/EFFECT/HUD/FRONTEND`), live value `g_current_draw_class`
  (`gfx_pc.c:451`) set by top-level renderers via `gfx_set_draw_class()`
  (`gfx_pc.c:524`) with a DL-address-range fallback (`gfx_pc.c:552`).
- **The CSV manifest emit does not exist** — `build_pack.py` already *reads*
  `*.texmanifest.csv` (`load_manifest`, `build_pack.py:129-158`) but nothing writes it.
  This is the single blocking gap for everything downstream (Router, per-class models).

### 2.3 Offline tools — working but Dam-scale

- `build_pack.py`: token normalization to `tok%04d` (`build_pack.py:48-63`), alpha
  re-attach from `.alpha.pgm` (`:74-89`), seam-safe 3×3-tile-then-crop for
  edge-matching textures (`is_tileable` `:92-115`, `tile_3x3` `:118-126`, crop
  `:230-237`), anti-hallucination Lanczos for ≤16 px sources (`TINY`, `:186-202`),
  per-class model routing table (`CLASS_MODEL`, `:37-43`) keyed off the (not yet
  emitted) manifest.
- `synth_texture.py`: seamless spectral primitives — `_fft_noise` (1/f^β fractal,
  `:104-118`), `_bandpass` (blob fields, `:143-156`), `_worley` (toroidal cell
  noise, `:65-101`), `_highpass` (tile-uniformity, `:128-140`), `_hist_match`
  (`:50-62`). Presets: `gravel` (`:170`), `rock` (`:191`) only (`GEN`, `:211`).
  Enforces `.png` output (`:227-228`). **Tier logic**: `--mean/--sd` = A1
  distributable; `--match <dump>` = Tier B local-only (roadmap §1b nuance).
- Dam prototype learnings (roadmap §3, validated in-game): hue-token identification
  works; AI degrades tiny tiled surfaces; procedural won the ground (`tok0022`);
  rock walls (`tok0949`) are a **lighting** problem (per-quad UV seams) — left stock
  pending W1 smooth normals.

### 2.4 Enforcement machines (R2)

- `.gitignore:71` blocks `*.png`; `.gitignore:106` blocks `*.texmanifest.csv`
  (avgRGB is ROM-derived metadata).
- `scripts/ci/check_no_rom_data.sh:53-60` hard-fails **any tracked image format**
  (`png/bmp/ppm/pgm/...`), `:91-101` flags >3 MB blobs, `:107` blocks base64
  payloads. Consequence: **all pack content is local**; only code, route plans
  (token IDs + choices, no pixel data), provenance JSON, and NOTICE text are
  committable.

---

## 3. Target state — the AAA bar

What a player/reviewer observes when this workstream is done:

1. **Every one of the 20 solo levels** (`dam facility runway surface1 bunker1 silo
   frigate surface2 bunker2 statue archives streets depot train jungle control
   caverns cradle aztec egypt` — the canonical list, `tools/perf_census.sh:30-31`)
   renders with an HD pack built by **one command per level**, with zero flat-smear
   ground, zero broken tile seams, zero alpha-cutout regressions (grates stay
   grates), on both GL and Metal.
2. **Weapons, character skins, and HUD** covered by whole-image upscale where they
   are static-settex reachable, with a measured coverage report where they are not.
3. A **distributable pack** (`--distributable`) exists containing only A1/A2 assets
   (procedural generic + CC0/CC-BY + original), with a machine-generated `NOTICE`,
   that a user can download legally; the **full-fidelity pack** builds locally from
   the user's ROM in under 15 minutes for all 20 levels on an M-series GPU.
4. Per-token `_n`/`_r` **material sidecars** exist for hero surfaces, ready to light
   up the moment W1 lands W1.E5 (roadmap T2.3 — sidecar samplers in the generated
   shaders; doc 01 §4.7).
5. Reviewer demo: `tools/texpack/pack_qa.sh --level <name>` prints PASS with
   pixel-diff budgets, seam metrics, VRAM budget, and render-health — for any level.

---

## 4. Technical design

### 4.1 (a) P1.2 — `texmanifest` C emit

**New env**: `GE007_DUMP_TEXMANIFEST=1` (independent of the image dump so a manifest
run needs no pixel writes; both use `GE007_DUMP_SETTEX_DIR`, default `/tmp` like
`gfx_pc.c:12288-12290`).

**Hook point**: `gfx_handle_settex()` fresh-decode path, immediately after the RGBA
decode completes and before upload (insert around `gfx_pc.c:21045`, where `rgba`,
`w`, `h`, `fmt`, `sz`, `texturenum`, and `g_current_draw_class` are all in scope).
The settex cache means every token passes through fresh-decode exactly once per
residency; a `static uint8_t emitted[4096]` guard (same pattern as `dumped[4096]`,
`gfx_pc.c:12249`) dedupes rows per process. ⚠ The guard is per-*process* and the
file opens append: always dump into a **fresh directory** per run (as the §4.7
recipe does), or a second run duplicates rows.

**File**: `<dir>/ge007.texmanifest.csv`, opened append, header written iff the file
is freshly created. One row per unique token:

```
token,w,h,fmt,siz,avgRGB,tileable,draw_class
tok0022,64,32,0,1,4a4a44,1,room
```

**New static functions** (all in `gfx_pc.c`, ~120 lines total):

```c
/* Alpha-weighted average color, hex rrggbb. Reuses the exact loop already
 * used by the settex diag dump (gfx_pc.c:21000-21035). */
static void gfx_texmanifest_avg_rgb(const uint8_t *rgba, int texels, char out[7]);

/* C port of build_pack.py:92 is_tileable(): mean |L1| of opposite-edge RGB
 * pairs; tileable iff both edge means < 20.0 and min(w,h) >= 8. The Python
 * and C implementations MUST agree (shared fixture test, T2 below). */
static int gfx_texmanifest_is_tileable(const uint8_t *rgba, int w, int h);

static void gfx_texmanifest_emit(int texturenum, const uint8_t *rgba,
                                 int w, int h, uint32_t fmt, uint32_t siz);
```

`draw_class` column = `gfx_draw_class_name(g_current_draw_class)` verbatim — it
already returns lowercase (`"room"/"hud"/…`, `gfx_pc.c:511-522`), matching the keys
`build_pack.py:37-43` already expects
(`hud/room/weapon/chrprop/effect`; `unknown`/`frontend` pass through and fall back
to `--model`).

**Rails**: R2 — `avgRGB` is ROM-derived, so the CSV stays local; `.gitignore:106`
already covers it. R3 — env-gated diagnostic, zero behavior change when unset
(identical code path: one `getenv`-cached branch, same as every `g_diag_*` toggle).
R1 — render-side only; sim untouched.

### 4.2 (b) P2.3 — the Router (`tools/texpack/route_pack.py`)

Deterministic function: `(manifest.csv, overrides.json, library index) → plan.json`.

**Decision tree** (roadmap §3, mechanized; evaluated top-down per token):

```
1. token in overrides.json            → use override verbatim (source/tool/args)
2. draw_class ∉ {room}                → source=ai_upscale, mode=whole_image
3. draw_class == room:
   a. max(w,h) <= 16                  → source=lanczos            (tiny; AI hallucinates)
   b. tileable && max(w,h) <= 64      → source=procedural         (no recoverable detail)
        preset chosen by override only; default preset=gravel is WRONG —
        unrouted small ROOM tiles get source=stock + a WARN line, because a
        mis-preset floor is worse than a blocky one. Curation (E8) fills these.
   c. tileable && library match        → source=cc0_import (match = curated mapping
        in the library index, never automatic — visual matching is a human call)
   d. tileable, no match               → source=ai_upscale, mode=seam_safe
   e. not tileable                     → source=ai_upscale, mode=whole_image
4. overrides may also set source=stock (e.g. Dam rock tok0949: per-quad-UV
   seam surfaces stay stock until W1 smooth normals — roadmap §3).
```

**Plan format** (`plan.json`, sorted keys, stable float-free output → byte-stable
across runs; committable — contains no ROM data, only token IDs and decisions):

```json
{
  "pack_kind": "full|distributable",
  "level": "dam",
  "entries": {
    "tok0022": {"source": "procedural", "preset": "gravel", "size": "1024x512",
                 "tone": {"mode": "match"}, "tier": "B"},
    "tok0107": {"source": "ai_upscale", "mode": "whole_image",
                 "model": "realesrgan-x4plus", "tier": "B"},
    "tok0311": {"source": "cc0_import", "asset": "ambientcg/Metal032", "tier": "A1"},
    "tok0949": {"source": "stock", "reason": "per-quad UV seams; see W1.E1 (T1.3)", "tier": "-"}
  }
}
```

**Tier assignment rules** (hard-coded, not configurable): `ai_upscale`/`lanczos` = B
always; `procedural` = B if `tone.mode == "match"` else A1; `cc0_import` = A1 (CC0/PD)
or A2 (CC-BY/CC-BY-SA) from the provenance record; `original` = A1.

**The refusal invariant** (the load-bearing R2 feature):
`route_pack.py --distributable` exits non-zero if any entry resolves to Tier B, and
`build_pack.py --plan plan.json --distributable` re-checks at build time (defense in
depth — a hand-edited plan must not bypass it). Error message names every offending
token and its cheapest A-tier alternative (procedural-generic or stock).

**Signatures**:

```python
def route(manifest: dict[str, Row], overrides: dict, library: LibraryIndex | None,
          distributable: bool) -> Plan: ...   # library=None => branch 3c never matches
def main():  # --manifest --overrides --out --distributable --level [--library]
             # --library is OPTIONAL (M1 demo predates the E4 cc0 library)
```

### 4.3 (c) P2.4 — open-licensed ingestion (`--cc0-library`)

**Library layout** (local dir, e.g. `~/mgb64_assets/`; images gitignored as always;
the *index* is committable):

```
library/
  index.json                     # id -> record (no pixels) — the operational index
  assets/<id>/source.png         # local only (even CC0 — keep repo image-free;
                                 #   check_no_rom_data.sh:57 fails ANY tracked image)
  assets/<id>/record.json
```

The library dir itself lives **outside the repo** (e.g. `~/mgb64_assets`), so
"the index is committable" means: E4.T4 copies `index.json` into the repo as
`tools/texpack/cc0_index.json` (`cp ~/mgb64_assets/index.json
tools/texpack/cc0_index.json && git add …`) — it is pure text (ids, URLs,
licenses, hashes; no pixels), so `check_no_rom_data.sh` passes. `route_pack.py
--library <dir>` reads `<dir>/index.json`; the committed snapshot is the
review/provenance record.

**Provenance record** (one per asset, required fields — ingestion refuses partial
records):

```json
{
  "id": "ambientcg/Metal032",
  "url": "https://ambientcg.com/view?id=Metal032",
  "license": "CC0-1.0",
  "author": "ambientCG",
  "retrieved": "2026-07-02",
  "sha256": "<hash of source.png as downloaded>",
  "transforms": ["crop 1024x1024", "downscale 2x", "desaturate 0.1"]
}
```

**Tool**: `tools/texpack/cc0_library.py`
- `ingest --url U --license L --author A --file F` → verifies the license string is
  in the allowlist (`CC0-1.0`, `PD`, `CC-BY-4.0`, `CC-BY-SA-4.0`; **GPL/unknown
  refused** per roadmap §1b — "GPL art is not assumed compatible"), hashes, writes
  the record. CC-BY/SA records get `"notice_required": true`.
- `notice --plan plan.json --out NOTICE.pack.txt` → walks all `cc0_import` entries,
  emits attribution lines for every `notice_required` asset. `build_pack.py`
  refuses to mark a pack distributable if a required NOTICE line is missing.
- `check` → re-hash all assets vs records (drift detection).

**`build_pack.py --cc0-library <dir>`**: for plan entries with `source=cc0_import`,
load `assets/<id>/source.png`, apply the recorded transforms (deterministic PIL ops),
resample to the routed output size, write `textures/tok####.png`, and copy
`NOTICE.pack.txt` into the pack root.

Sizing rule: cc0 imports are resampled to `native_dims × 16` capped at 4096
(the synth-preset sizing convention — e.g. Dam ground 64×32 → 1024×512; NOTE the
AI path maxes at ×4, `build_pack.py:165` `choices=[2,3,4]`, so cc0/synth sources
deliberately out-resolve upscales — and the backend cap is 4096,
`gfx_opengl.c:1484` / `gfx_metal.mm:1560`), and **tileability is verified after
transform** with the same edge metric — a non-wrapping import routed onto a tiled
surface is a build error, not a runtime surprise.

### 4.4 (d) Procedural preset library (8 presets total — 6 new, Tier A1 generic-tone)

All presets compose the existing seamless primitives (`_fft_noise`, `_bandpass`,
`_worley`, `_highpass` — `synth_texture.py:65-156`); every one is registered in
`GEN` (`synth_texture.py:211`) and inherits determinism (fixed `--seed`), the
`.png`-only guard (`:227`), and the tone pipeline (`--match`/`--mean/--sd`,
`:230-248`). Spectral recipes (w = output width; all layers zero-mean unit-std
before mixing, `up(x) = clip(0.5+0.5x, 0, 1)` as in `gen_gravel` `:180`):

| Preset | Recipe (sketch) | Default tone (mean/sd) |
|---|---|---|
| `concrete` | `0.55*up(_fbm(persist=0.35))` fine aggregate + `0.25*up(_bandpass(cells=w/40, bw=0.7))` pores + `0.20*up(_bandpass(cells=w/6, bw=0.6))` grit; `_highpass(min_cells=8)`; mild S-curve | 110 / 14 |
| `metal_panel` | base `0.85 + 0.15*_fbm(persist=0.2)` brushed grain: multiply x-axis frequencies by 6 in `_fft_noise` (anisotropic, still periodic ⇒ seamless); panel lines = periodic square grid `1 - 0.35*lines(nx=2, ny=2, width=2px)` (integer panel counts ⇒ wraps); sparse `_worley` F1 dots as rivets at panel corners | 96 / 10 |
| `wood` | rings: `r = frac(ny*y + 0.35*_fft_noise(beta=2.4))`, `rings = 0.5+0.5*cos(2π*k*r)` with integer `ny,k` (periodic ⇒ seamless); overlay `0.2*up(aniso grain)` (y-stretched `_fft_noise`); NO `_highpass` (rings are the feature) | 92 / 18 |
| `sand` | `0.7*up(_bandpass(cells=w/3, bw=0.8))` grain + `0.3*up(_bandpass(cells=w/24, bw=0.45))` ripples (anisotropic: scale fy by 3); `_highpass(min_cells=12)` | 150 / 12 |
| `snow` | `0.8*up(_fbm(persist=0.25))` + `0.2*up(_bandpass(cells=w/2, bw=0.9))` sparkle (clip top 2% to 255); `_highpass(min_cells=10)`; high mean, tiny sd | 215 / 8 |
| `brick` | running-bond grid: integer `bx,by` courses, odd rows offset 0.5 period; mortar = smoothstep bands (periodic); per-brick tone via `_worley`-cell-indexed random offsets (toroidal ⇒ seamless); brick-face `_fbm(persist=0.3)` roughness | 105 / 20 |
| (exists) `gravel`, `rock` | `synth_texture.py:170,191` | 64 / 22 |

Implementation notes for juniors:
- Seamlessness rule: **every** spatial function must be periodic in both axes —
  integer cycle counts for grids/rings, DFT-domain noise, toroidal Worley. Never
  use non-wrapped `np.random` fields or absolute-coordinate gradients.
- Grid seams are the brick/metal trap: build lines with
  `0.5+0.5*cos(2π*n*x)` raised to a power, never `np.linspace` + modulo edges.
- Each preset gets a **default generic tone** (table above) so `--mean/--sd`-less
  invocations are still Tier A1; `--match` stays the local-only Tier-B route.
- Color: current output is grayscale RGB (`synth_texture.py:249`). Add optional
  `--tint r,g,b` (multiply, then re-normalize luma) so wood/brick aren't gray;
  a tint chosen by eye is generic (A1), a tint computed from a dump is B — the
  flag docs must say so, mirroring the `--match` warning.

### 4.5 (e) AI upscale hardening + per-class model selection

Current: `CLASS_MODEL` (`build_pack.py:37-43`) routes `hud → x4plus-anime`, all
else `x4plus`; Dam experience says the **anime model is often cleaner on GoldenEye's
flat low-color art** (`tools/texpack/README.md:71-72`; the Dam showcase shipped on
the anime model per `docs/` history). Work:

1. **Per-class bake-off** (data, not vibes): for each DrawClass, upscale a 12-token
   sample with `x4plus`, `x4plus-anime`, `realesr-animevideov3`; deterministic
   in-game A/B screenshots; pick per-class winners; update `CLASS_MODEL`. Expected
   outcome (to be confirmed): `hud/weapon → anime` (flat cel art), `chrprop → x4plus`
   (faces/organic), `room` split by tileability (already handled by routing).
2. **Alpha integrity**: dumps carry alpha separately (`.alpha.pgm`,
   `build_pack.py:74-89`). Verify `realesrgan-ncnn-vulkan` preserves the alpha
   channel through the 3×3-tile path; if it drops or halos it, upscale alpha
   independently (Lanczos on the `.pgm`) and re-attach post-crop. Acceptance uses an
   alpha-cutout fixture (grate) — "round wheels become solid squares" is the
   documented failure (`build_pack.py:78-80`).
3. **Plan-driven build**: `build_pack.py --plan plan.json` becomes the primary mode
   (per-token source dispatch); the legacy manifest-only `--route` stays for quick
   one-offs.
4. **Failure hygiene**: today a mid-batch `subprocess.run(check=True)`
   (`build_pack.py:226-228`) aborts the whole pack; wrap per-model batches, report
   failed tokens, continue, exit non-zero at end with a summary.

### 4.6 (f) AI-generated material sidecars (normal/roughness)

**File contract** (agreed with W1, who owns the loader/shader side —
`texture_pack_try_load()` stays untouched until W1.E5; their loader entry point is
`texture_pack_try_load_sidecars`, doc 01 §4.7):

```
<pack>/textures/tok####_n.png   # tangent-space normal, RGB8, +Z out (128,128,255 flat)
<pack>/textures/tok####_r.png   # roughness, grayscale, 255 = fully rough
```

**Tool**: `tools/texpack/make_sidecars.py <diffuse.png> --out-dir <textures/>
[--plan plan.json] [--distributable] [--height <h.png>] [--strength 2.0]`
— token inferred from the diffuse filename (`tok####.png`); `--plan` supplies the
routed source for tier refusal; `--height` uses a true height field (E5.T3) instead
of the luma prior. Pure first-party math (numpy — already in
`tools/texpack/requirements.txt`), no AI weights needed for v1:

```python
def height_from_luma(lum, blur_sigma=1.5):     # luma ≈ height prior for rough
    return gaussian_blur(lum, blur_sigma)      # surfaces (gravel/rock/concrete)
def normal_from_height(h, strength=2.0):       # Sobel gradients, WRAPPED
    dx = sobel_x(h, mode="wrap"); dy = sobel_y(h, mode="wrap")   # keep seamless!
    n = normalize(stack(-strength*dx, -strength*dy, ones))
    return (n * 0.5 + 0.5) * 255
def roughness_from_variance(lum, win=8):       # local contrast -> rough; flat -> smooth
    return clip(255 * sqrt(local_var(lum, win, mode="wrap")) / 40.0, 40, 255)
```

`sobel(mode="wrap")` is mandatory — non-wrapped gradients put a lighting seam on
every tile edge of an otherwise seamless texture.

**Tier propagation** (R2, the subtle one): the sidecar math is first-party, but the
tier follows the **input**: sidecars of procedural-generic or CC0 diffuse = **A1**
(committable to a distributable pack); sidecars of an upscaled dump = **B**
(local-only). `make_sidecars.py` reads the plan entry for the token and refuses to
emit into a `--distributable` build from a Tier-B diffuse. Procedural presets also
get a `--emit-height` option so synth surfaces can derive normals from their *true*
height field (better than luma-guessing).

**Validation before W1 lands**: sidecars render nothing today (no sampler — roadmap
§3 "material maps" gate). Offline QA = a small `preview_material.py <diffuse.png>`
(finds `tok####_n.png`/`tok####_r.png` beside the diffuse) that fake-lights
diffuse+normal under a swinging directional light and writes a GIF for eyeballing;
in-game QA arrives with W1.E5.T3.

**Single implementation, two entry points** (coordination with doc 01): W1.E5.T5
adds `build_pack.py --emit-material-maps` (that flag does **not** exist today — it
is created by W1, not by any W2 task) as the batch driver that emits sidecars
beside each diffuse during a pack build. It MUST import the normal/roughness
functions from `make_sidecars.py` (this epic), not reimplement them — the E6.T3
handshake records that agreement so the two docs don't ship divergent Sobel math.

### 4.7 (g) The 20-level production process

Per-level flow (one engineer-day per ordinary level once tooling exists):

```bash
L=facility  # any of the 20 names (perf_census.sh:30-31)
# 1. DUMP (images + manifest), deterministic headless:
SDL_AUDIODRIVER=dummy GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
GE007_DUMP_SETTEX_TEXTURES='*' GE007_DUMP_TEXMANIFEST=1 \
GE007_DUMP_SETTEX_DIR=/tmp/td_$L \
  ./build/ge007 --level $L --deterministic --screenshot-frame 300 --screenshot-exit
# 2. HERO ID (hue technique, roadmap §3, now a tool):
python3 tools/texpack/hue_pack.py --manifest /tmp/td_$L/ge007.texmanifest.csv \
    --out /tmp/huepack_$L        # paints every ROOM token a unique hue + prints the map
GE007_TEXTURE_PACK=/tmp/huepack_$L ./build/ge007 --level $L --deterministic \
    --screenshot-frame 300 --screenshot-label hue_$L --screenshot-exit
#   screenshots are written to the CWD as screenshot_<label>.bmp (platform_sdl.c:673)
python3 tools/texpack/hue_pack.py --identify screenshot_hue_$L.bmp \
    --map /tmp/huepack_$L/hue_map.json
# 3. CURATE: write tools/texpack/overrides/$L.json (hero surfaces: preset/stock/cc0)
# 4. ROUTE:
python3 tools/texpack/route_pack.py --manifest /tmp/td_$L/ge007.texmanifest.csv \
    --overrides tools/texpack/overrides/$L.json --library ~/mgb64_assets \
    --level $L --out /tmp/plan_$L.json
# 5. BUILD:
python3 tools/texpack/build_pack.py --dump /tmp/td_$L --plan /tmp/plan_$L.json \
    --out ~/ge007_hd     # one shared pack dir; tokens are GLOBAL ids (Rare's
                         # texture-by-number system), so the same token in two
                         # levels is the same texture
# 6. QA GATE:
tools/texpack/pack_qa.sh --level $L --pack ~/ge007_hd
```

⚠ **Shared-token conflicts**: because the pack dir is shared, two levels' overrides
routing the *same* token differently would silently overwrite each other (last
build wins). `route_pack.py` must WARN when a plan entry contradicts an entry in a
previously written plan for the same token (compare against
`tools/texpack/overrides/*.json` + the other `/tmp/plan_*.json` present); resolving
the conflict (pick one route, note it in both overrides files) is a curation step.

**`hue_pack.py`** mechanics (mechanizing the validated manual technique): vertex
shading is a grayscale multiply, so hue survives to screen exactly (roadmap §3
"Dam learnings"). Assign each candidate token a maximally-spaced HSV hue, emit flat
1-color `<out>/textures/tok####.png` files (the loader probes exactly
`<pack>/textures/tok%04d.png` and warns if `textures/` is absent,
`texture_pack.c:51-58,75`) + `<out>/hue_map.json`; `--identify` clusters screenshot hues
(tolerant of luma variation, keyed on hue angle) and prints on-screen tokens ranked
by pixel coverage — the top ~5 ROOM tokens per level are the hero-surface
candidates for curation.

**Curation policy** (what a junior writes in `overrides/$L.json`): route the top-5
coverage ROOM tokens explicitly (preset, cc0 asset, or `stock` with a reason —
per-quad-UV surfaces like Dam's `tok0949` stay stock, roadmap §3); everything else
rides the automatic tree. Overrides JSONs are **committable** (token IDs + choices,
no ROM data) and reviewable — they are the per-level art direction record.

**Level order** (risk-first): dam (redo via new pipeline — regression anchor),
facility (indoor/CI4-heavy), surface1 (snow preset), depot (brick/concrete/metal),
train (interior + known sky-leak level, QA must not mask it), then the remaining 15
in `perf_census.sh` order.

### 4.8 (h) The QA harness

**`tools/texpack/validate_pack.py --pack <dir> --manifest <csv> [--dump <dir>]
[--budget-mb 256]`** (offline, seconds — structural gate):
- filename shape `tok\d{4}\.png` (+ `_n`/`_r` sidecars); token < 4096
  (`texture_pack.c:24`); decodable RGBA (stbi will force RGBA at runtime,
  `texture_pack.c:79`); dims ≤ 4096 (**both backends reject above**,
  `gfx_opengl.c:1484`, `gfx_metal.mm:1560`); warn on non-power-of-2 or aspect
  mismatch vs manifest; alpha presence must match the original's — NOTE the
  manifest CSV has **no alpha column** (schema is frozen to the 8 roadmap-P1.2
  columns), so alpha truth comes from the dump's `.alpha.pgm` files via `--dump`
  (the §4.7 recipe sets `GE007_DUMP_SETTEX_TEXTURES='*'` alongside the manifest
  env, so they land in the same dir) — catches dropped cutouts;
  seam self-check: for tokens the manifest marks tileable, run the `is_tileable`
  edge metric on the HD output — a produced tile that no longer wraps is a FAIL;
- **upload budget**: sum of decoded RGBA bytes vs a per-level budget (default
  256 MB/level — 4096² RGBA = 64 MB each, so this also catches "everything at 4096"
  packs); prints the top-10 offenders.

**`tools/texpack/pack_qa.sh --level L --pack P`** (in-game, ~2 min/level; runs the
roadmap §7 canonical harness):

```bash
# NOTE: screenshots are BMP in the CWD — screenshot_<label>.bmp (platform_sdl.c:673).
# identity leg (R3): pack OFF, run TWICE (labels base/base2) — the two BMPs must be
# byte-identical (deterministic baseline; also proves "pack unset == stock"):
env SDL_AUDIODRIVER=dummy GE007_DETERMINISTIC_STABLE_COUNT=1 GE007_NO_VSYNC=1 \
    GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
  ./build/ge007 --level $L --deterministic --screenshot-frame 300 \
    --screenshot-label base --screenshot-exit          # Video.TexturePack empty
cmp screenshot_base.bmp screenshot_base2.bmp           # (after the 2nd run)
# feature leg: same command + GE007_TEXTURE_PACK=$P GE007_TRACE_SETTEX=1, label hd,
#   plus --trace-state trace_hd.jsonl (that flag is what produces the JSONL that
#   audit_render_trace.py consumes). Repeat once with GE007_RENDERER=metal —
#   macOS-only, gfx_backend.h:10; pack_qa.sh SKIPs, not fails, the metal leg elsewhere.
#   Metal draws scenes today: gfx_metal.mm header "Implemented (Phases 1-3)" incl.
#   texture upload + draw flush + CPU readback; parity harness pattern =
#   tools/renderer_parity_capture.sh)
python3 tools/audit_screenshot_health.py screenshot_hd.bmp   # non-black, non-garbage
python3 tools/compare_screenshots.py screenshot_base.bmp screenshot_hd.bmp \
    --max-changed-pct 60 --json-out qa.json
# The ceiling (>60% changed => FAIL) is enforced by the tool itself (exit 1).
# The floor + tone budgets are computed BY pack_qa.sh from qa.json fields:
#   .changed_pct < 5            => "pack didn't load" (fail loudly)
#   per-channel |mean_rgb.test - mean_rgb.baseline| > 25 on any channel => tone drift
# (compare_screenshots.py has no floor/tone flags; mean_rgb is in the JSON payload.)
python3 tools/audit_render_trace.py trace_hd.jsonl     # render health (bad cmds/NaN)
# settex-event check: [SETTEX-UPLOAD-FAIL]/[SETTEX-MISS] are STDERR lines gated on
# GE007_TRACE_SETTEX=1 (gfx_log_settex_event, gfx_pc.c:11431) — pack_qa.sh greps the
# feature leg's stderr log for them; audit_render_trace.py does NOT cover them
# (its gates are bad-cmds/crashes/NaN/DL-counter only).
```

Output contract: `pack_qa.sh` prints one `PACK_QA PASS level=<L>` line and exits 0;
any failed check prints `PACK_QA FAIL level=<L> check=<name>` and exits 1.

Plus the seam A/B: a scripted close-up warp at each hero surface
(`GE007_AUTO_WARP_PAD` pattern already used by the train investigation) with a
9-region `--region` grid — tile-repeat borders must not show step deltas.
Perf check: frame-time via the `perf_census.sh` single-level mode, run twice —
`CENSUS_OUT=/tmp/qa_perf_off.csv tools/perf_census.sh $L` (pack unset) and
`CENSUS_OUT=/tmp/qa_perf_on.csv GE007_TEXTURE_PACK=$P tools/perf_census.sh $L`
(exported env passes through the script's `env` launcher; `CENSUS_OUT` overrides
the default `baselines/perf_census_latest.csv` so QA never dirties the repo).
The pack-on mean work_ms must stay ≤ 111% of pack-off (≥ 90% fps); texture
residency is cached per token — `texture_pack.c:26-29` miss cache + settex GL
cache — so steady-state cost is VRAM/bandwidth, not I/O.

**Sim invariance** (R1): texture substitution never touches the sim by construction
(pixels only, dims preserved — §2.1), but the pack flag rides the standard gate once
per milestone. NOTE the gate's two internal legs are hard-coded RemasterFX/SSAO
config-overrides (`tools/sim_invariance_gate.sh:52-57`); an exported
`GE007_TEXTURE_PACK` passes through to BOTH legs. So the pack check is **two
invocations**, comparing printed hashes across them:
`tools/sim_invariance_gate.sh dam1 600 2` once with `GE007_TEXTURE_PACK` unset and
once exported to the pack — all four hashes must be identical (script header:
`tools/sim_invariance_gate.sh:1-24`).

### 4.9 (i) Weapons / viewmodel, character skins, HUD

- All three are **whole-image** upscales — never tile-3×3 (they don't wrap; the
  `is_tileable` heuristic already returns false for detailed art, and the Router's
  class branch (§4.2 step 2) forces `whole_image` regardless).
- **HUD** (`draw_class == hud`): alpha is the product — reticles, ammo digits,
  watch UI are cutouts. Anime model (already routed, `build_pack.py:38`), plus the
  alpha-integrity path from §4.5. Upscale factor 4 is enough (HUD renders at
  logical-resolution scale; bigger buys nothing).
- **Weapons** (`weapon`): viewmodel textures fill large screen area — the highest
  ROI per token in the game. Route to the per-class bake-off winner; hero weapons
  (PP7, KF7) get hand-curated override review.
- **Characters** (`chrprop`): faces are photo-sourced in GoldenEye — the photo model
  (`x4plus`) is the expected winner; hallucination risk is highest here, so the
  bake-off (§4.5.1) must include face tokens and a human sign-off.
- **Coverage truth**: only **static settex** textures are HD-replaceable today
  (`texture_pack.c` has no hash-key path; hash-key dumps are explicitly skipped,
  `build_pack.py:214-216`). Task E9.T1 runs a per-class census (settex trace vs
  `GE007_DUMP_LOADED_TEXTURES` trace) to measure what fraction of
  weapon/chrprop/HUD pixels are reachable. If coverage is materially incomplete
  (>20% of viewmodel screen-pixels unreachable), E9.T2 extends the loader with a
  content-hash key (`textures/hash_%016x.png`, keyed on a stable hash of the
  *source texels* so it is ROM-content-stable, not address-based) — mirroring the
  settex hook contract (decode-replace, native dims preserved).

### 4.10 File-by-file change map

| File | Change | Epic |
|---|---|---|
| `src/platform/fast3d/gfx_pc.c` | +`gfx_texmanifest_*` (3 static fns + hook at ~21045) | E1 |
| `tools/texpack/route_pack.py` | NEW — Router | E3 |
| `tools/texpack/cc0_library.py` | NEW — ingestion/provenance/NOTICE | E4 |
| `tools/texpack/synth_texture.py` | +6 presets, `--tint`, `--emit-height` | E5 |
| `tools/texpack/build_pack.py` | `--plan`, `--distributable`, `--cc0-library`, alpha hardening, per-batch failure isolation | E2/E3/E4 |
| `tools/texpack/make_sidecars.py` | NEW — normal/roughness | E6 |
| `tools/texpack/hue_pack.py` | NEW — hue-token ID | E8 |
| `tools/texpack/validate_pack.py`, `pack_qa.sh` | NEW — QA harness | E7 |
| `tools/texpack/overrides/<level>.json` ×20 | NEW — committable curation | E8 |
| `tools/texpack/README.md`, `docs/VISUAL_MODES.md` | doc updates per epic | all |
| `src/platform/texture_pack.c` | E9.T2 only (hash-key path, if census demands) | E9 |

No shader work in this workstream ⇒ no GLSL/MSL dual-generator changes (that is
W1's contract; our sidecar *files* feed their samplers). Likewise
`build_pack.py --emit-material-maps` is **W1's** change (W1.E5.T5, doc 01 §4.7 —
it wraps `make_sidecars.py`, see §4.6); it is deliberately absent from this table.

---

## 5. Work breakdown

Estimates are **junior-engineer-days**. Every task is flagged/local per R2/R3; rails
notes call out the gate. Build command for all C tasks:
`cmake --build build -j && ctest --test-dir build -R sim_state_hash`.

### E1 — texmanifest C emit (P1.2)

| ID | Task | Files | Steps | Acceptance (runnable) | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W2.E1.T1 | Emit CSV from settex decode | `gfx_pc.c` | Add 3 static fns (§4.1); hook after decode ~`:21045`; `emitted[4096]` dedupe; append-with-header-once | Fresh dir: `rm -rf /tmp/td && mkdir /tmp/td`, then `GE007_DUMP_TEXMANIFEST=1 GE007_DUMP_SETTEX_TEXTURES='*' GE007_DUMP_SETTEX_DIR=/tmp/td ./build/ge007 --level dam --deterministic --screenshot-frame 300 --screenshot-exit` → (a) `/tmp/td/ge007.texmanifest.csv` exists, first line exactly `token,w,h,fmt,siz,avgRGB,tileable,draw_class`; (b) no duplicate tokens: `tail -n+2 …csv \| cut -d, -f1 \| sort \| uniq -d` empty; (c) every fresh-decode image dump is manifested: each `/tmp/td/ge007_settex_NNNN.rgba.ppm` (exclude `*_cache*` — those are the cache-hit variant, `gfx_pc.c:12484`) has a `tokNNNN` CSV row (both run off the same fresh-decode path, so set-equality is exact; `SETTEX-MISS`/`OOM` tokens never reach decode and appear in neither — `gfx_pc.c:20731,20754,20769`); (d) `grep '^tok0022,64,32,' …csv` hits, with `tileable=1`, `draw_class=room` | 3 | — | R3: env-gated diag, unset = byte-identical (screenshot sha vs base). R2: CSV local (`.gitignore:106`) |
| W2.E1.T2 | C/Python tileable parity | `gfx_pc.c`, `build_pack.py`, new pytest | Shared fixture: 6 synthetic PNGs (wrapping, non-wrapping, 4px-tiny…) run through both implementations | `python3 -m pytest tools/texpack/tests/test_tileable_parity.py` green; both agree on all fixtures | 2 | E1.T1 | A1 fixtures (generated in-test, never tracked) |
| W2.E1.T3 | Identity + guards sweep | — | Run identity screenshot A/B; run contamination guard | `scripts/ci/check_no_rom_data.sh` OK; run the §8.1 identity command (envs unset) on the pre-E1.T1 build (`--screenshot-label base`) and the post-E1.T1 build (`--screenshot-label after`), then `tools/compare_screenshots.py screenshot_base.bmp screenshot_after.bmp --max-changed-pct 0` exits 0 | 1 | E1.T1 | R3 proof |

### E2 — AI upscale hardening (P2.1 + model selection)

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W2.E2.T1 | Alpha integrity through ESRGAN | `build_pack.py` | Fixture: PIL-generate a 64×64 grate (opaque bars, alpha-0 holes) as `.rgba.ppm` + `.alpha.pgm` pair (the dump format, `build_pack.py:74-89`); verify ncnn alpha survives 3×3-tile+crop; else split-channel path (Lanczos alpha, re-attach post-crop) | Pipeline on fixture: output alpha histogram matches source ±2% (fraction of alpha<128 pixels); in-game grate screenshot shows holes — check via `compare_screenshots.py --region grate:X,Y,W,H` vs stock until E7's `pack_qa.sh` lands, then via its region check | 3 | — | Tier B pack, local |
| W2.E2.T2 | Per-class model bake-off | `build_pack.py` (`CLASS_MODEL`) | 12-token sample/class from a Dam+facility manifest dump; 3 models each; deterministic A/B screenshots; human pick; update table + README | Decision doc lines in README; `CLASS_MODEL` updated; A/B shots archived locally | 4 | E1.T1 | Tier B, local; results (words) committable |
| W2.E2.T3 | Batch failure isolation + `--plan` input mode | `build_pack.py` | Wrap per-model `subprocess.run`; per-token dispatch off plan JSON | Kill ESRGAN mid-batch → build finishes others, exits 1, names failures; `--plan` builds Dam pack identical (sha per file) to legacy path for ai-routed tokens | 3 | E3.T1 | — |

### E3 — Router (P2.3)

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W2.E3.T1 | `route_pack.py` core tree | new: `tools/texpack/route_pack.py`, `tools/texpack/overrides/dam.json` (first curation file — M1 demo needs it), `tools/texpack/tests/test_route_pack.py` | Implement §4.2; stable JSON emit; WARN-not-guess for unrouted small ROOM tiles; cross-plan shared-token conflict WARN (§4.7) | Unit tests (`python3 -m pytest tools/texpack/tests/test_route_pack.py`): 10 synthetic manifests hit every branch; running twice → byte-identical plan; Dam manifest + `overrides/dam.json` reproduces the shipped Dam decisions (`tok0022` procedural, `tok0949` stock) | 4 | E1.T1 | Plan JSON committable (no ROM data) |
| W2.E3.T2 | Tier-B refusal (distributable) | `route_pack.py`, `build_pack.py` | `--distributable` refusal in BOTH tools (§4.2); actionable error | `route_pack.py --distributable` on a plan w/ one ai_upscale → exit 1 naming token; hand-edit plan to smuggle tier:"A1" on an ai entry → `build_pack.py --distributable` still refuses (source-based, not label-based) | 2 | E3.T1 | **R2 core enforcement** |

### E4 — Open-licensed ingestion (P2.4)

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W2.E4.T1 | `cc0_library.py` ingest/check | new | Record schema §4.3; license allowlist; sha256; refuse partial/GPL/unknown | `ingest` a real ambientCG CC0 asset → record complete; `ingest --license GPL-2.0` → refused; `check` detects a bit-flipped asset | 4 | — | A1/A2 with provenance; images stay local (`check_no_rom_data.sh:57`) |
| W2.E4.T2 | NOTICE generation + build gate | `cc0_library.py`, `build_pack.py` | `notice` cmd; distributable build fails on missing attribution | Plan w/ one CC-BY asset → NOTICE contains author/URL/license line; delete record → distributable build exits 1 | 2 | E4.T1, E3.T1 | R2 A2 discipline |
| W2.E4.T3 | `--cc0-library` build path | `build_pack.py` | Load, transform (recorded ops), resample ×16≤4096, post-transform tileability check, place | Plan-routed cc0 token lands as `tok####.png`; non-wrapping import on tileable token → build error; in-game screenshot shows it | 3 | E4.T1, E3.T1 | A1/A2 |
| W2.E4.T4 | Seed library: 12 assets | `~/mgb64_assets` (local) + committed snapshot `tools/texpack/cc0_index.json` (§4.3) | Curate CC0 metal/concrete/wood/brick/sand/snow from ambientCG/PolyHaven (both CC0); ingest each; `cp ~/mgb64_assets/index.json tools/texpack/cc0_index.json` and commit | 12 complete records; `check` green; ≥1 asset visually accepted on depot (manual screenshot A/B vs stock; re-gate via `pack_qa.sh` once E7 lands); `check_no_rom_data.sh` green with the index committed | 4 | E4.T1 | A1 (CC0) |

### E5 — Synth preset library

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W2.E5.T1 | concrete + sand + snow | `synth_texture.py` | §4.4 recipes; register in `GEN`; default tones | Each: fixed seed → stable sha256 twice; self-tileable (edge metric <20, the `is_tileable` math); `_highpass` verified: in-test FFT of output luma, sum of `\|F\|` over radial frequencies < 8 cycles/width (DC excluded) must be < 5% of total spectral energy (macro blotches would repeat across a tiled plane) | 4 | — | A1 generic |
| W2.E5.T2 | metal_panel + wood + brick (structured) | `synth_texture.py` | Periodic grids/rings per §4.4 notes; `--tint` | Same determinism/seam tests + a wrap-shift test (`np.roll` by w/2 then edge metric — catches non-integer periods); visual accept on depot/train hero surface | 6 | E5.T1 | A1 generic (`--tint` doc note per §4.4) |
| W2.E5.T3 | `--emit-height` for all presets | `synth_texture.py` | Return pre-tone height field; write `tok####_h.png` (local intermediate) | Height PNG exists, seamless, feeds E6 pipeline | 2 | E5.T1, E5.T2 | follows diffuse tier |

### E6 — Material sidecars (feeds W1.E5, roadmap T2.3)

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W2.E6.T1 | `make_sidecars.py` normal+rough | new | §4.6 math; **wrapped** Sobel/variance; plan-aware tier refusal | Flat input → exact (128,128,255); seamless input → seamless normal (edge metric on n map); unit-length ±1%; Tier-B diffuse + `--distributable` → refused | 4 | E3.T1 | tier follows input (§4.6) |
| W2.E6.T2 | `preview_material.py` offline QA | new: `tools/texpack/preview_material.py <diffuse.png>` (finds `_n`/`_r` siblings, §4.6) | Lambert+Blinn fake-light GIF over swinging light | GIF renders; gravel normal shows moving shading, flat map doesn't | 2 | E6.T1 | local artifact |
| W2.E6.T3 | Sidecar contract handshake w/ W1 | doc §4.6 | Freeze filename/encoding contract in this doc + W1's doc §4.7 (incl. roughness polarity 255=rough); record that W1.E5.T5's `build_pack.py --emit-material-maps` wraps `make_sidecars.py` (§4.6 — one implementation); hero-surface sidecars for Dam+facility generated | W1 sign-off recorded; sidecars validate in `validate_pack.py` | 1 | E6.T1; **W1.E5 (T2.3) for in-game render** | — |

### E7 — QA harness

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W2.E7.T1 | `validate_pack.py` | new | §4.8 structural checks incl. 4096 cap, alpha parity, seam self-check, upload budget | Seeded bad packs (5000px asset, `tok22.png`, opaque grate, non-wrapping tile) each produce a named FAIL; Dam pack passes | 4 | E1.T1 | local |
| W2.E7.T2 | `pack_qa.sh` per-level gate | new | §4.8 in-game legs: identity, feature (GL **and** `GE007_RENDERER=metal`), floor+ceiling diff budgets, render-trace audit, hero-surface region grid, perf leg | `pack_qa.sh --level dam --pack ~/ge007_hd` → PASS; empty-pack run → FAIL "pack didn't load" (floor check); identity leg byte-identical | 5 | E7.T1 | R3 identity leg is the gate |
| W2.E7.T3 | Sim-invariance + ASan legs | — | Wire the two-invocation pack-on/off gate recipe (§4.8 — the script's own legs toggle RemasterFX/SSAO, not the pack) into milestone checklist; ASan run of loader path | `tools/sim_invariance_gate.sh dam1 600 2` run twice (`GE007_TEXTURE_PACK` unset vs exported): all four printed hashes identical; `tools/asan_smoke.sh` clean with pack | 2 | E7.T2 | **R1 proof** |

### E8 — 20-level production

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W2.E8.T1 | `hue_pack.py` | new: `tools/texpack/hue_pack.py` | §4.7: hue assign, flat pack emit, `--identify` clustering by hue angle + coverage rank (input = the `screenshot_hue_<L>.bmp` from the §4.7 step-2 run) | On Dam: identifies `tok0022` as top ground token (matches roadmap §3 ground truth) | 3 | E1.T1 | throwaway pack local |
| W2.E8.T2 | Pipeline levels 1–5 (dam, facility, surface1, depot, train) | `overrides/*.json` | §4.7 flow per level; curate top-5 heroes each; QA gate | `pack_qa.sh` PASS ×5; Dam redo matches/exceeds shipped Dam pack in A/B review; train QA run does NOT regress the known sky-leak metrics | 10 | E2,E3,E5,E7,E8.T1 | overrides committable; packs local |
| W2.E8.T3 | Levels 6–20 | `overrides/*.json`, new `tools/texpack/all_levels.sh` (loops the §4.7 recipe over `perf_census.sh:30-31`'s list) | Same flow ×15 | `for L in …; do tools/texpack/pack_qa.sh --level $L --pack ~/ge007_hd; done` all PASS; full 20-level build wall-time < 15 min | 15 | E8.T2 | same |
| W2.E8.T4 | Distributable pack v1 | plans + NOTICE | Route all 20 levels `--distributable` (procedural+cc0+stock only); build; NOTICE | `route_pack.py --distributable` ×20 green; `check_no_rom_data.sh` green on everything committed; pack installs & renders on a machine with no dumps | 4 | E8.T3, E4.* | **R2 flagship artifact** |

### E9 — Coverage census + (conditional) hash-key loader

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W2.E9.T1 | Weapon/chrprop/HUD coverage census | `gfx_pc.c` (tiny env-gated counter) + new `tools/texpack/coverage_census.py` | Add `GE007_TEXTURE_COVERAGE_CENSUS=1` per-flush tri counters keyed `draw_class × (settex_active ? settex : rdram_hash)` (same pattern as `g_drawclass_tri_counts`, `gfx_pc.c:452`), dumped at exit as `[COVERAGE] class=<c> settex_tris=<n> other_tris=<n>` stderr lines; python reducer runs 5 levels (dam facility surface1 depot train) headless to frame 300 and aggregates | CSV report `level,class,settex_tri_pct` (5 levels × 6 classes); go/no-go number for T2 (threshold: weapon-class mean `settex_tri_pct` < 80 ⇒ go); diag env unset ⇒ screenshot byte-identical (R3 leg) | 3 | E1.T1 | local traces; R3: env-gated diag |
| W2.E9.T2 | *(conditional)* hash-key loader path | `texture_pack.c`, `gfx_pc.c`, `build_pack.py` | Stable content-hash of source texels; `textures/hash_%016x.png` probe beside token probe; dump emits same key; miss-cache by hash | Fixture round-trip: dump → pack → in-game replacement of one hash-key texture; identity leg byte-identical with pack off; two-invocation sim-invariance gate green (§4.8 recipe — the hash probe touches the hot `gfx_pc.c` path); ASan clean | 8 | E9.T1 says go | R3: same `Video.TexturePack` gate; R2: Tier B local; R1: RAMROM gate in acceptance |

**Total: 100 junior-days base ≈ 20 junior-weeks; 108 (≈ 22 wks) if the census
triggers E9.T2** (≈ 7-8 senior-weeks). Per-epic: E1 6 · E2 10 · E3 6 · E4 13 ·
E5 12 · E6 7 · E7 11 · E8 32 · E9 3 (+8 conditional).

---

## 6. Milestones & deliverables

| M | Deliverable | Contents | Demo script (reviewer runs) | Est (jr-wks) |
|---|---|---|---|---|
| M1 | **Manifest + Router live** | E1, E3, E2.T3 | `GE007_DUMP_TEXMANIFEST=1 GE007_DUMP_SETTEX_DIR=/tmp/td ./build/ge007 --level dam --deterministic --screenshot-frame 300 --screenshot-exit && python3 tools/texpack/route_pack.py --manifest /tmp/td/ge007.texmanifest.csv --overrides tools/texpack/overrides/dam.json --level dam --out /tmp/plan.json && cat /tmp/plan.json` (`overrides/dam.json` ships with E3.T1) | 4 |
| M2 | **All-sources build** (AI hardened + synth ×8 + cc0 + NOTICE) | E2, E4, E5 | `python3 tools/texpack/build_pack.py --dump /tmp/td --plan /tmp/plan.json --cc0-library ~/mgb64_assets --out /tmp/pack && GE007_TEXTURE_PACK=/tmp/pack ./build/ge007 --level dam` | 6 |
| M3 | **QA harness + 5 flagship levels** | E7, E8.T1-T2, E6 | `tools/texpack/pack_qa.sh --level depot --pack ~/ge007_hd` (prints PASS w/ budgets); `python3 tools/texpack/preview_material.py ~/ge007_hd/textures/tok0022.png` (finds the `_n`/`_r` siblings) | 6 |
| M4 | **All 20 levels, full pack** | E8.T3, E9.T1 | `for L in dam facility runway surface1 bunker1 silo frigate surface2 bunker2 statue archives streets depot train jungle control caverns cradle aztec egypt; do tools/texpack/pack_qa.sh --level $L --pack ~/ge007_hd; done` (the canonical list, `perf_census.sh:30-31`; `ALL_LEVELS` spans two lines, don't sed it) → 20× PASS | 4 |
| M5 | **Distributable pack v1 + release gates** | E8.T4 (+E9.T2 if go) | `python3 tools/texpack/route_pack.py --distributable …` (green) `&& scripts/ci/check_no_rom_data.sh && tools/sim_invariance_gate.sh dam1 600 2` | 2 (+1.6 cond.) |

*Note: the Est column sums to 22 jr-wks vs §5's 100 jd ≈ 20 jw — milestone weeks
include ~2 weeks of integration/review slack and deliberately do not sum to the
task-day total. Schedule from the §5 task estimates; use this column for milestone
pacing only.*

---

## 7. Risks & mitigations (ranked)

| # | Risk | Mitigation | Kill / de-scope criterion |
|---|---|---|---|
| 1 | **Tier-B leakage into a distributable artifact** (program-ending legal risk) | Two independent refusal points (Router + builder, E3.T2); source-based not label-based tier; `check_no_rom_data.sh` in pre-commit + release CI; images never tracked even when A1 | None — this rail is absolute. Any near-miss ⇒ stop, add a third gate before resuming |
| 2 | **Curation doesn't scale** — 20 levels × hero surfaces swamps the schedule | `hue_pack.py` mechanizes identification; only top-5 coverage tokens curated per level; the automatic tree handles the long tail; WARN-not-guess keeps un-curated tiles stock (safe default) | If a level exceeds 2 junior-days of curation: cut hero curation to top-3 and ship; polish later |
| 3 | **Structured synth presets (brick/wood/metal) look procedural/fake** in-game | Periodicity rules (§4.4); per-preset in-game accept on a real hero surface before library sign-off (E5.T2); cc0 import is the fallback source for exactly these materials | If a preset fails visual accept twice: drop it, route those surfaces to cc0/stock (decision tree already supports it) |
| 4 | **Alpha/hallucination regressions from ESRGAN at scale** (fine on Dam, fails on level 14's odd texture) | Fixture-driven alpha gate (E2.T1); TINY-Lanczos guard already in (`build_pack.py:186`); `validate_pack.py` alpha-parity check per level; face tokens human-reviewed (E2.T2) | Per-token: `override → stock` is always available; no systemic kill needed |
| 5 | **Weapon/character coverage is settex-incomplete** and the hash-key loader (E9.T2) balloons | Census first (E9.T1) — decide on data; hash-key design mirrors the proven settex contract; strictly additive probe | If census shows ≥80% coverage: skip E9.T2 entirely. If E9.T2 exceeds 12 days: de-scope to weapons-only keys |
| 6 | **VRAM/perf regression from 20 levels of 1-4k textures** | Upload budget in `validate_pack.py` (256 MB/level default); perf leg in `pack_qa.sh` (≥90% fps); settex cache eviction already bounded (`SETTEX_CACHE_SIZE`, `gfx_pc.c:21122-21133`) | If a level can't meet budget at ×4: route its long tail to ×2; DDS/BCn (README `:132`) stays future work, not scope creep |
| 7 | **C/Python `tileable` heuristic drift** silently mis-routes | Shared-fixture parity test (E1.T2) pinned in CI (pytest is ROM-free) | — (cheap, permanent) |

---

## 8. Validation strategy

Per roadmap §7, every commit in this workstream runs:

1. **Identity (R3)** — flags unset ⇒ byte-identical:
   `env SDL_AUDIODRIVER=dummy GE007_DETERMINISTIC_STABLE_COUNT=1 GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 ./build/ge007 --level dam --deterministic --trace-state t.jsonl --screenshot-frame 300 --screenshot-label base --screenshot-exit`
   on the pre-change build, the same with `--screenshot-label after` on the
   post-change build, then
   `tools/compare_screenshots.py screenshot_base.bmp screenshot_after.bmp --max-changed-pct 0`
   (screenshots are `screenshot_<label>.bmp` in the CWD, `platform_sdl.c:673`;
   launch pattern per `tools/playability_smoke.sh:318-339`).
2. **Feature A/B** — same command + `GE007_TEXTURE_PACK=<pack>`, once per backend
   (`GE007_RENDERER=metal` leg mandatory — the 4096 caps are mirrored,
   `gfx_metal.mm:1557-1560`, and must stay behaviorally identical); budgets are
   floor+ceiling (§4.8): `--max-changed-pct 60` ceiling enforced by the tool;
   changed-pct ≥ 5 floor and per-channel mean delta ≤ 25 computed from the
   `--json-out` payload (`changed_pct`, `mean_rgb` fields) — the tool has no
   floor/tone flags.
3. **Health** — `tools/audit_screenshot_health.py` + `tools/audit_render_trace.py`;
   plus grep the feature-leg stderr (run with `GE007_TRACE_SETTEX=1`) for new
   `[SETTEX-UPLOAD-FAIL]`/`[SETTEX-MISS]` events (§4.8 note — these are stderr
   lines, not trace records).
4. **Sim invariance (R1)** — `tools/sim_invariance_gate.sh dam1 600 2` run twice,
   `GE007_TEXTURE_PACK` unset vs exported (two invocations — see §4.8; the script's
   internal legs toggle RemasterFX/SSAO, not the pack): all hashes identical;
   `tools/compare_state.py off.jsonl on.jsonl` to localize any (unexpected)
   divergence.
5. **Memory safety** — `tools/asan_smoke.sh` with a pack active (loader mallocs via
   stbi and the settex path frees, `texture_pack.c:86` / `gfx_pc.c:21074` — the
   exact seam ASan watches).
6. **Contamination (R2)** — `scripts/ci/check_no_rom_data.sh` locally per commit and
   in CI; plus `git status --porcelain | grep -E '\.(png|ppm|pgm|csv)$'` must be
   empty before any commit in this workstream.
7. **ROM-free CI legs** — `ctest -R sim_state_hash`; pytest for tileable-parity,
   Router branches, provenance schema (synthetic fixtures only, generated in-test).

Milestone gates add: `tools/playability_smoke.sh --all` (broad), full 20-level
`pack_qa.sh` sweep (M4), and the distributable-refusal red-team test (deliberately
smuggled Tier-B plan must fail both tools, E3.T2 acceptance).

---

## 9. Open questions

1. **Distributable-pack hosting**: the v1 A1/A2 pack is legal to distribute — but
   *where* (GitHub Releases on this repo vs separate repo)? Affects whether
   `check_no_rom_data.sh` needs a release-artifact exemption path (it currently has
   none, by design). Needs user decision at M5.
2. **CC-BY-SA acceptance**: roadmap §1b allows CC-BY-SA "after downstream-
   compatibility check". Does the user want SA assets at all, given the pack itself
   would then carry SA obligations? Default in this plan: allowlist includes
   `CC-BY-SA-4.0` but E4.T4 seeds **CC0-only**; flip needs user sign-off.
3. **Remaster preset default**: should `--remaster` auto-set `Video.TexturePack`
   when a pack is present at a well-known path (e.g. `~/.mgb64/hd_pack`)? Touches
   the open remaster-default policy question (VISUAL_MODES §3, flagged in the Dam
   memory as an unresolved non-technical confirm). Not needed for any W2 task.
4. **Character likenesses**: GoldenEye faces are real-person likenesses (actors).
   Even in a *local* Tier-B pack this is fine (user's own ROM), but any *original*
   A1 replacement art for faces raises likeness questions beyond copyright. Default:
   faces are AI-upscale (Tier B, local) only; no A1 substitution attempted.
