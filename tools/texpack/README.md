# `tools/texpack` — HD texture-pack pipeline

Offline tooling to build an **HD texture pack** from your own ROM's textures using
GPU-accelerated AI super-resolution (Real-ESRGAN, Vulkan → Metal/MoltenVK on macOS).

This is the *content-authoring* half of the Phase 2 plan
([`docs/PHASE2_PLAN.md`](../../docs/PHASE2_PLAN.md)). The *runtime* half — the
in-engine loader that consumes the pack — is the Phase 2 code work and is tracked
separately.

> ⚠️ **Everything this produces is ROM-derived and personal-use only.** Texture dumps
> and HD packs are derivative works of the original game art; they are gitignored and
> must never be committed or redistributed. You run this on textures decoded from *your
> own* legally-owned copy. The repo ships only first-party code; the upscaler binary is
> fetched on demand into a gitignored cache.

## One-time setup

```bash
tools/texpack/fetch_realesrgan.sh          # pinned, checksum-verified, into .bin/ (gitignored)
pip install -r tools/texpack/requirements.txt   # Pillow, only to decode the engine's .ppm dumps
```

## Workflow

```bash
# 1. Dump a level's textures from the game (static/world/HUD art, token-keyed):
GE007_DUMP_SETTEX_TEXTURES='*' GE007_DUMP_SETTEX_DIR=/tmp/td \
  ./build/ge007 --level 33 --deterministic        # ... your usual launch flags

# 2. Upscale the dump into a pack (GPU; ~seconds per level):
python3 tools/texpack/build_pack.py --dump /tmp/td --out ~/ge007_hd

# 3. (Once the Phase 2 loader lands) play with the pack:
GE007_TEXTURE_PACK=~/ge007_hd ./build/ge007 --level 33
```

Output matches the planned loader key: `<out>/textures/<token>.png`.

## Options

| Flag | Default | Notes |
|------|---------|-------|
| `--scale` | `4` | 2 / 3 / 4 |
| `--model` | `realesrgan-x4plus` | also `realesrgan-x4plus-anime`, `realesrnet-x4plus`, `realesr-animevideov3` |
| `--route` | off | route model by `draw_class` via a `*.texmanifest.csv` (Phase 2 STEP 1): photo model for world/prop/character, anime model for HUD |
| `--realesrgan` / `--models` | `.bin/...` | override the fetched binary/model paths |

## How it works

`build_pack.py` decodes each engine dump (`.ppm` today; `.png` after Phase 2 STEP 4),
parses the texture token from the filename, batches by model, and runs
`realesrgan-ncnn-vulkan` in directory mode on the GPU. Real-ESRGAN super-resolution is
deterministic (feed-forward — same input always yields the same output), so a pack is
reproducible.

Two quality safeguards run automatically:
- **Seam-safe tiling** — textures whose edges wrap (detected by edge-matching) are
  tiled 3×3, upscaled, then center-cropped, so the result stays seamless across mapped
  quads instead of showing broken-wrap seams. Disable with `--no-seamless`.
- **Anti-hallucination** — tiny source textures (≤16 px) use deterministic Lanczos
  instead of the AI model, which otherwise invents junk on small ambiguous tiles
  (e.g. a "smiley face" on a hubcap).

**Model tip:** for GoldenEye's flat, low-color textures, `--model realesrgan-x4plus-anime`
is usually cleaner (less speckle) than the default `realesrgan-x4plus`.

See [docs/VISUAL_MODES.md](../../docs/VISUAL_MODES.md) for how the pack plugs into the
faithful / remaster visual modes.

## Alternatives / future

- **Algorithmic** (deterministic, no AI): swap in `xbrz`/`scale2x` for pixel-art/UI tiles.
- **CC0 substitution** (the only *distributable* route): replace generic surfaces with
  open-licensed textures instead of upscaling the originals — a separate `--mode substitute`
  track (see `docs/PHASE2_PLAN.md` discussion).
- **DDS/BCn** output for VRAM/load wins on a full ~3001-token pack (Phase 2 v2).
