# W9 — Surface 1 Showcase (the AAA level demo testbed)

> Workstream 9 of the MGB64 AAA Remaster program, added 2026-07-10. Constitution:
> [`REMASTER_ROADMAP.md`](../REMASTER_ROADMAP.md) §1 rails — R1 gameplay
> invariance, R2 copyright tiers, R3 opt-in/default-identity — govern everything
> here. Program goal (user direction): one level, Surface 1, pushed to a truly
> impressive AAA demo through **distributable** assets wherever possible, spot
> -checked in-game milestone by milestone, with zero faithful-mode regressions.

## 1. Why Surface 1

Surface 1 was already W2's exit-demo level (outdoor snow, the `snow` synth
preset exercise, `tools/texpack/overrides/surface1.json`). It has dedicated
regression guards (`surface_projection_regression.sh`,
`surface_xlu_cvg_memory_regression.sh`), a snowfield traversal lane
(pads 80/79/77, STATUS.md), and its BG (`bg_sevx_all_p.seg`) is shared with
Surface 2 — every texture win lands on two levels.

## 2. Milestone ledger

| # | Landed | What | Evidence |
|---|---|---|---|
| M1 | `937773c` | CC0 snow heroes (ground `tok1267`, bank `tok1263`) via new `cc0_import.py`; beat the W2.E5 synth in A/B | pack_qa PASS (identity byte-identical, tone 5.6/25, perf noise-cleared by 8-sample median 104.7%) |
| M2 | (curation) | Full token inventory: multi-vantage dumps (spawn + pads 1/10/20/30/45/60/79) grew the manifest 40 → **145 tokens**; contact sheets + hue-ID coverage ranking | `overrides/surface1.json` `_comment` |
| M3 | this ws | Environment CC0 sweep: **34 heroes** re-curated `cc0_import` — snow plateau `tok1264` (35.7% coverage at pad 60, was stock checkerboard), frozen creek `tok1265` (Ice004, desat 0.35), cabin woods (Planks003/021, Wood026/051, WoodSiding001), rust family (Rust004), plates (MetalPlates006), hut metal (Metal029), concrete (Concrete034) | VALIDATE_PACK PASS 273 textures / 82 MB; spot-checks at 6 vantages |
| M4 | this ws | Weapons: KF7 walnut stock `tok2674` (Wood026); receiver plates `tok0027/0028` + barrel sheen `tok0026` deliberately stay AI (painted mechanical detail must survive) | KF7 viewmodel spot-check |
| M5 | this ws | Treeline bands `tok1198-1201`: first-party `gen_treeline.py` spruce art (Tier A1), V-flipped for the hillside mapping, vertically wrap-faded (the faces tile the strip), `tok1201` keeps its bare-snow left transition, `tok1198/1199` emit **alpha-cutout** (stock strips are translucent XLU overlays) | validate_pack alpha-parity PASS; in-game A/B at pads 79/60/spawn |

## 3. Curation principles (what keeps this from being AI slop)

1. **Distributable first.** CC0 (ambientCG) or first-party generated art for
   every hero; the AI-upscale tree is the *floor*, not the ceiling. All CC0
   tone-locking uses only committed `avgRGB` metadata — never ROM pixel stats —
   so every hero asset in `overrides/surface1.json` is Tier A1.
2. **Look before you leap.** Every token got eyeballed on a contact sheet and
   located in-game via `hue_pack.py` before routing. This is what caught
   `tok1793-1798` being Bond's *hands* (not wood — an early plan would have
   wrapped the fingers in walnut), and characters (skin `tok1609-1622`, ushanka
   fur `tok1820-1822`, jackets `tok1785/1786`) staying untouched on the AI tree.
3. **The gates decide, not vibes.** `validate_pack.py` alpha-parity caught three
   would-be regressions: the see-through grates `tok1193/1195` sealed solid by a
   plate import (reverted to alpha-preserving AI), and the XLU treeline strips
   `tok1198/1199` losing their transparency (fixed with `--alpha-cutout`).
4. **Painted detail beats material swaps.** Where the stock texture carries
   *art* (weapon receivers, signage, dish mesh), a flat CC0 material would erase
   it — those ride the AI tree until hand-crafted replacements exist.
5. **Shared global tokens are harmonized, never clobbered.** `tok0022/tok0291`
   copy dam.json's routes verbatim; `tok0265` copies train.json's; route_pack's
   cross-plan conflict WARN is the enforcement (now also keyed on cc0 args).
   This bit for real: the first full-manifest build ran BEFORE harmonization and
   silently overwrote Dam's curated gravel/concrete synths with AI upscales —
   caught by the Dam regression gate, restored from dam.json. Harmonize the
   overrides BEFORE the first `build_pack.py` run, not after.
6. **Characters are pinned `stock` — art AND perf.** Guard skin/hands/ushanka
   textures (draw_class mislabeled `room` in the manifest) re-upload
   continuously while characters animate, so an HD hit is a per-frame decode
   tax: with them built, Dam pack-on measured ~120% of pack-off; without,
   85-103% (median-of-5 interleaved `perf_census.sh`). The 15 tokens are pinned
   `stock` in `overrides/surface1.json` with the measurement in the note.

## 4. Tooling added by this workstream

- `tools/texpack/cc0_import.py` — open-licensed import: LANCZOS resize →
  wrap-padded tile-uniform high-pass → chroma control (`--desat`) → scalar-mean
  tone-lock → I-format intensity-alpha (`--i-alpha`) → `tok####.png` +
  `tok####.provenance.json`. The §6 P2.4 manual half.
- `tools/texpack/gen_treeline.py` — first-party snowy-conifer band art
  (layered spruce silhouettes, snow-dusted crowns, fog wrap-fade,
  `--snow-left` slope transition, `--alpha-cutout` for XLU strips).
- Both deterministic (same args ⇒ byte-identical), both covered in
  `tools/texpack/tests/` (136 tests green).

## 5. Reproducing the pack

```bash
# dump every vantage (spawn + the pads in §2 M2), merge manifests, then:
python3 tools/texpack/route_pack.py --manifest <merged.csv> \
    --overrides tools/texpack/overrides/surface1.json --level surface1 --out plan.json
python3 tools/texpack/build_pack.py --dump <merged-dump> --plan plan.json --out ~/ge007_hd
# then run the cc0_import / gen_treeline commands recorded per-token in
# overrides/surface1.json (source: cc0_import / original entries, args verbatim)
tools/texpack/pack_qa.sh --level surface1 --pack ~/ge007_hd \
    --manifest <merged.csv> --dump <merged-dump>
```

Play: `GE007_TEXTURE_PACK=~/ge007_hd ./build/ge007 --level surface1`
(default visual mode is the full remaster; `--faithful` pins the pack off, R3).

## 6.5 M7 — the scene-decoration layer (real 3D models over the sim)

The texture ceiling is broken: `Video.SceneDecor` (default **0**) draws imported
glTF models on top of the untouched simulation. Ten first-party snowy spruces
(crossed-quad needle boughs, CC0 Poly Haven bark, ~300 tris each) now line the
Surface 1 spawn valley.

**Architecture** (`src/platform/decor_assets.c` + `src/game/decor_native.c` +
`include/decor.h`):
- Per-level manifest `<Video.SceneDecorDir>/<slug>.decor.txt` (committable
  text: `model`/`place` lines, world units); models load via vendored cgltf
  (`lib/cgltf`, MIT), textures via stb_image -> big-endian RGBA16.
- Loader bakes interpreter-ready data: <=16-vertex native `Vtx` batches in
  persistent blocks (`gfx_register_pc_vertex_region`), per-model s16
  quantization (`30000/extent`; the quant cancels in the instance matrix).
- Emitter appends native-endian DL commands at the `lvlRender` seam right
  after `bgLevelRender`: decor Z-tests against rooms and composites under
  characters/effects/viewmodel/HUD. Per instance: a float `G_MTX_FLOAT_PORT`
  MODELVIEW (room convention: `world*room_data_float2 - current_model_pos`),
  opaque pass (`G_RM_FOG_SHADE_A`/`OPA_SURF2`) then cutout pass
  (`TEX_EDGE2`, double-sided), `fogSetRenderFogColor` inherited per level,
  weapon matrices restored at the tail (mirroring bgLevelRender's own tail).
- Fail-closed: budget-checks `dynGetFreeGfx` and skips rather than overflow
  the 256 KB frame gfx pool; OFF emits zero commands.

**Hard-won engine facts** (each cost a debugging round; do not re-derive):
1. The master DL is **NATIVE-endian**, built with the port's GBI macros
   (`platform_gbi.h`); big-endian encoding is only for ROM sub-DLs.
2. **F3D, not F3DEX**: `gSPVertex` packs `(n-1)<<4|v0` into ONE byte — the
   vertex cache ceiling is **16 verts per load**. 30 silently wraps to 14 and
   triangles referencing stale cache slots draw kilometer-long beams.
3. **LOADBLOCK moves at most 4096 texels** (12-bit lrs): decor textures cap
   at 64x64 RGBA16 through the standard path.
4. Rooms draw under `get_BONDdata_field_10E0()` — the combined
   **world-to-clip** matrix (float when `bondviewField10E0IsFloat()`).
   `currentPlayerGetProjectionMatrix[F]()` is only the bare perspective; using
   it leaves the camera rotation out and geometry lands ~4 screens off-axis.
5. `osVirtualToPhysical` returns **u32** — it truncates 64-bit malloc'd
   pointers. Pass raw pointers to the port's `gSPVertex` (it stores the full
   `uintptr_t`); the dyn-pool statics only survive by accident of layout.
6. Placement data comes from traces: `GE007_TRACE_CAMERA` room positions,
   `--trace-state` pos/floor at warp pads (`GE007_AUTO_WARP_PAD` **plus**
   `GE007_AUTO_WARP_FRAME`). ~1 world unit = 1 cm on Surface.

**Diagnostics**: `GE007_TRACE_DECOR=1` (load + emit + batch dumps),
`GE007_DECOR_ONLY_PRIM=N` (bisect a model's primitives), plus the generic
`GE007_TRACE_FRAME=N` full-DL trace.

**Assets**: `assets/decor/surface1.decor.txt` (committed) +
`assets/decor/build_models.sh` (regenerates the .glb deterministically;
models/ and .cache/ are gitignored to keep the tree binary-free).
`tools/decor/gen_tree3d.py` is the tree builder (tests in
`tools/decor/tests/`).

**Validation**: decor-off byte-identical (2-run identity + on-with-no-manifest
identity); dam/facility/runway boot smokes with decor on; 139 tool tests; perf
median-of-5 marginal cost recorded in the commit message.

## 6.6 M8 — the modern render path (2026 fidelity through our own renderer)

M7's ceiling (s16 verts, 16-vert loads, 64x64 RGBA16) is gone: **`G_MODERNMESH`
(0xC1)**, a port DL opcode, flushes the pending N64 batch and hands a
full-fidelity mesh to the backend at the exact frame position — float32
vertices, u32 indices, **mipmapped RGBA8 textures of arbitrary size**, drawn
by a dedicated MSL pipeline into the same color/depth attachments as the N64
scene (same encoder, correct interleaving), GPU-transformed by the
interpreter's MP matrix, with the N64 per-vertex fog curve replicated in the
vertex shader and Metal's 0..1 depth remap applied. PSOs cache per
(cutout, MSAA sample count); mesh GPU resources upload once (one-off blit
command buffer for mip generation) keyed by a never-reused mesh id.
GL slot is NULL (warn-once skip) — the optional-member vtable pattern.

Manifest models tagged `modern` take this path (`decor_assets.c` keeps float
data, no quantization). Surface 1 now stands **a decimated CC0 Poly Haven
photoscan fir** (`tools/decor/decimate_gltf.py`: uniform-grid vertex
clustering, 685k -> 52k tris, 512px textures, sun-lambert vertex bake with an
`--ambient` dial) at eight placements plus high-detail generated spruces
(`gen_tree3d.py --detail high`: clustered crossed-quad boughs, supersampled
needle cards).

Validated: decor-off byte-identical; dam boot smoke; 139 tool tests; scaled
to **45 placements / ~3.0M decor triangles per frame at ~4.6 ms** (M9/M9.1:
hero pine + full fir + boulders + deadwood, snow baked into COLOR_0.alpha).
`assets/decor/build_models.sh` reproduces every manifest model from a clean
checkout (fetch-on-demand Poly Haven cache, ~1.5 GB one-time; args mirror the
per-model provenance.json — a review catch: the script originally built only
the generated spruces).

## 6. Known remaining work (next milestones)

- **Radome/dish whites** (`tok0820` sphere, `tok0668` mesh, `tok1984-1994`
  cluster): baked shading makes flat CC0 swaps risky — needs hand-crafted art.
- **Signage accents**: fire alarm `tok0030`, hazard roundel `tok1259`, Cyrillic
  strip `tok0424`, switch panel `tok0617` — crisp first-party redraws would pop.
- **Near cutouts**: radio-mast truss `tok1373`, icicle/antenna row `tok0806`
  (fmt3/IA) — need an IA-aware import path.
- **Material sidecars**: ambientCG sources ship normal/roughness maps, and
  `make_sidecars.py` writes the files, but no runtime reader exists (W1 §4.7).
- **Sky**: the sunset is a `skyRender` backdrop, not a token — engine-side work.
- **Decor next steps**: rocks/snowdrift models; per-instance tint; distance
  fade; Surface 2 manifest (same models); richer generated-spruce cards (the
  photoscan currently outclasses them); normal-mapped + per-pixel-lit modern
  materials (W1 sun integration); **prop-DL substitution through
  G_MODERNMESH** — the path to replacing crates/mast/weapon models with HD
  assets driven by the game's own matrices — now fully specified (hook points,
  sim-coupling proof, task plan) in
  [10-asset-replacement-architecture.md](10-asset-replacement-architecture.md).
- **Character/dynamic texture uploads**: the settex hook pays the HD decode on
  every upload; static room geometry uploads once, animated characters do not.
  A future loader-side decoded-PNG cache would make character HD viable.
- **Gotchas for the next operator**: `GE007_AUTO_WARP_PAD` requires
  `GE007_AUTO_WARP_FRAME` (pack_qa's seam leg omits it — latent); the pack_qa
  perf leg is noise-bound on surface1 (±20% single samples — judge by ≥8-sample
  interleaved median); zsh does NOT word-split unquoted vars (env assignments
  built in shell variables silently merge).
