# Phase 2 — HD Texture-Replacement Pipeline (implementation plan)

The keystone HD-asset unlock from [REMASTER_ROADMAP.md](REMASTER_ROADMAP.md). End
goal: **an artist runs a level, gets a folder of correctly-named PNGs, upscales
them, drops them in a pack directory, and sees HD textures in-game** — with zero
geometry/UV changes, and with the repository staying provably asset-free.

Produced from a 5-area adversarially-verified plan workflow (every anchor checked
against the live `gfx_pc.c` texture code; all areas `goAhead`). This is the **plan**;
nothing here is implemented yet.

## Why this works (Engine B)

Every static game texture — world walls/floors, props, characters, weapon skins,
**and HUD icons** — flows through one funnel, `import_texture` → the 1024-bucket
texture cache (`struct TextureHashmapNode`, `gfx_pc.c:1899`), and carries a
content-stable token via `GE007_STATIC_TEXTURE_CACHE_KEY(token)` (`gfx_pc.c:1019-1024`)
drawn from the shared ~3001-entry IMAGESEG namespace. UVs normalize by *logical* tile
dims (`gfx_pc.c:3276`) and `upload_texture` accepts up to 4096×4096 (`gfx_opengl.c:888`),
so **a 2×/4× replacement Just Works with no geometry change.** One loader hooked at the
cache-miss covers every art class at once.

## Asset-free by construction (provenance)

Dumped textures and HD packs are ROM-derived and are blocked from the repo by **three
independent mechanisms**, so the pipeline cannot contaminate the tree:

1. **Already gitignored** — `*.png`, `*.bmp`, `*.ppm`, `screenshot_*` (`.gitignore:70-92`).
   Phase 2 adds dump/pack dir patterns (`ge007_loaded_tex_*`, `ge007_tex_*`, `texdump/`,
   `texture_pack*/`, `*.texmanifest.csv`).
2. **Contamination guard** — `scripts/ci/check_no_rom_data.sh` (run by `.githooks/pre-commit`,
   `pre-push`, and CI) **hard-fails on any tracked `*.png`/`*.bmp`/`*.ppm`** and any blob > 3 MB.
3. **Runtime is opt-in & local** — the loader reads `Video.TexturePack`/`GE007_TEXTURE_PACK`
   (default empty → no filesystem touch); dumps write to `GE007_DUMP_LOADED_TEXTURE_DIR`
   (default `/tmp`, out-of-tree); tooling only *processes* local dumps.

> ⚠️ **The CSV gap:** `*.csv` is exempt from both guard checks. The dump manifest is
> ROM-derived metadata, so it must use the gitignored `*.texmanifest.csv` suffix and write
> to the dump dir — never a bare `.csv` in-tree.

Only **first-party code** + **public-domain single-header libs** (`stb_image.h` /
`stb_image_write.h`, Unlicense/MIT, plain ASCII, < 3 MB) are tracked, vendored under
`lib/stb/` (mirroring `lib/glad/`) and noted in `THIRD_PARTY.md`.

---

## Build order (8 steps)

### STEP 0 — Vendor `stb` *(prereq, no deps)*
`lib/stb/{stb_image.h, stb_image_write.h, LICENSE, README}`; one implementation TU
`src/platform/texpack_stb.c` (`#define STB_IMAGE_IMPLEMENTATION` etc.), auto-globbed by
`CMakeLists.txt:202`. Document in `THIRD_PARTY.md`. *Hours · very-low risk.*

### STEP 1 — Dump foundation (keystone start) — area T-A
In `gfx_pc.c`:
- Add a file-static **FNV-1a-64** `gfx_diag_texture_content_hash(rgba, w, h, fmt, siz)`
  (~15 lines, no dep — grep-confirmed none exists) near `:6340`, hashing the **decoded
  RGBA + fmt+siz+w+h** (so CI variants with different palettes hash apart).
- Filename stem = **static token** `(uint32_t)(source_cache_key & ~FLAG)` when the static
  flag is set (`gfx_loaded_texture_is_static_game_texture`, `:2733-2736`), else the content
  hash. **Lock the stem contract now** (see below) — the loader globs it.
- Re-key the dedup table (`:6485-6496`) on the content hash and **drop the frame term** so
  identical textures at different DRAM addresses collapse to one file.
- Raise caps (default 64 → env-unbounded; `MAX_DUMPED_LOADED_TEXTURES` 512 → 8192; settex
  `dumped[4096]`).
- Emit a CSV manifest line per unique texture: `token/hash,fmt,siz,w,h,first_frame,draw_class`
  (`g_current_draw_class` at `:435`, `gfx_draw_class_name` at `:484`).
- Cover the **settex/static bypass** path too (`gfx_handle_settex` `:13360`,
  `gfx_diag_dump_settex_texture` `:13614` — `texturenum` already *is* the token).

> **Verifier corrections folded in:** resolve `dir` before the manifest write (the naive
> `~:6506` insert is too early); avoid the legacy `/tmp/ge007_tex_%d.ppm` debug dump
> collision (texturenums 1988/1787/1633 at `:13619-13643`); keep w/h/fmt/siz in the filename
> suffix *and* dedup on the hash/token. *Hours · very-low risk.*

### STEP 2 — Cache-node hash (keystone middle) — area T-B
Add `uint64_t content_hash` to `struct TextureHashmapNode` (`:1923`); compute it with the
**same** FNV helper in the **miss path only** (cache hit short-circuits before decode), at
the decoder tails where the decoded RGBA is in scope (the same points the dump runs).
Unused by rendering until the loader reads it → **byte-identical when dormant.** Prove via a
trace flag that `node.content_hash` == the dump filenames. *Hours · low risk.*

### STEP 3 — Disk replacement loader (keystone end) — area T-C
New `src/platform/texture_pack.c/.h`: `texture_pack_enabled()`, `texture_pack_try_load(token, …)`.
Register `Video.TexturePack` (string) + `GE007_TEXTURE_PACK`. Hook the **cache-miss branch**
of `import_texture` (the verified single hook at `~:8805`, before the format dispatch): if a
pack is set and the texture is static, glob `<pack>/textures/<token>.png`, `stbi_load`, and
`gfx_rapi->upload_texture(png_rgba, w, h)` at the higher resolution instead of the N64 decode;
on miss, fall through to the normal decode (parity preserved). Default empty pack → byte-identical.

> **Three mandatory corrections (load-bearing):**
> 1. **Dump filename ↔ loader key MUST match exactly** — both derive the stem from the *same*
>    pristine `source_cache_key` token. This is the round-trip; if they diverge, nothing loads.
> 2. **Negative cache is required in v1** — `import_texture` runs on *every* `textures_changed`
>    miss (`gfx_run_dl` `:11055-11058`); without caching the *misses*, every un-replaced static
>    texture re-`stat()`s disk every frame. The LRU must remember "no PNG for this token."
> 3. **I/IA luminance** — I/IA decoders write luminance to RGB and it's `prim_color`-tinted at
>    draw; a colored replacement double-tints. v1 restricts to RGBA-class (below). *Days · medium risk.*

### STEP 4 — PNG dump swap *(parallel with STEP 3, needs STEP 0)* — area T-E
Swap `gfx_diag_write_rgba_texture_dump` (`:6381-6422`) PPM/PGM body → `stbi_write_png(path, w, h, 4, rgba, w*4)`;
flip `.rgba.ppm`→`.png` at both call sites. Now: run a level → a folder of named PNGs. *Hours · very-low.*

### STEP 5 — Format-correctness guard (hardening) — area T-D
Add a `replace_class`/`replace_allowed` bit + `gfx_tex_replace_class`/`_allowed` classifier
(`~:1018`), computed after fmt/siz recovery (`~:8760`), gating the loader. **v1 = RGBA16/32 +
`maxlod==0` only.** CI behind `GE007_TEXTURE_PACK_CI` (palette-baked RGBA, breaks palette
animation — default off); I/IA behind `GE007_TEXTURE_PACK_LUMA` (luminance-preserving — default
off); LOD/dynamic/font refused. *Days · medium risk.*

### STEP 6 — Artist tooling *(needs STEP 4 PNGs + STEP 1 manifest)* — area T-E
`tools/texpack/`: `manifest.py` (parse), `repack.py` (1:1 identity proof — dump→repack→in-game
identical, *no* upscaler), `upscale.py` (draw-class-routed: Real-ESRGAN for
room/weapon/chrprop/effect, xBRZ for hud), `requirements.txt`. First-party MIT, processes local
dumps only. *Days · low risk.*

### STEP 7 — Provenance backstop — area T-E
`.gitignore` additions (above) + confirm the guard already refuses tracked PNGs. *Hours · very-low.*

---

## First milestone — "drop one PNG, see it HD"

The smallest end-to-end slice (STEP 0 + 4 + the STEP-1 token-stem fix + a minimal STEP 3 hook
restricted to `fmt==RGBA && maxlod==0`):

1. Run Dam with `GE007_DUMP_LOADED_TEXTURES=* GE007_DUMP_LOADED_TEXTURE_DIR=/tmp/td`, stand at a
   **clamp-tiled wall**, grab its `ge007_tex_tok####.png`.
2. Upscale that one file 4× in any editor.
3. Drop it in `<pack>/textures/tok####.png`, run with `GE007_TEXTURE_PACK=<pack>`.
4. See the wall render crisp at 4× — same UVs, same geometry. Remove the PNG → back to N64. ✅

Scope the demo to a **clamped** surface, not a tiled floor, to avoid shipping a REPEAT-seam bug
as the headline.

## Recommended decisions (resolve before a pack is authored)

- **v1 class scope:** RGBA16/32 static + `maxlod==0` only; CI/I-IA/LOD/settex deferred to v2 behind
  default-off flags. *(Covers world/prop/HUD — the demo surface.)*
- **Filename stem contract (lock now):** static `ge007_tex_tok%04u_%dx%d_fmt%u_siz%u.png`, dynamic
  `ge007_tex_h%016llx_….png`; loader globs the leading `tok####` / `h<hash>`.
- **Primary key:** token for static (artist-friendly, stable), content-hash for dynamic.
- **Format:** PNG (RGBA8, artist-editable) for v1; DDS/BCn (VRAM/load wins for a full 3001-token
  pack) is a v2 call.
- **Pack layout / config:** `<pack>/textures/<token>.png`; `Video.TexturePack` string, **STARTUP**
  scope (honest — mid-run change won't re-decode the cache).
- **stb layout:** `lib/stb/` (co-locates notices, mirrors `lib/glad/`).
- **settex + multi-LOD:** v2 (one level dump-survey first to confirm no demo-critical settex-only surface).

## Validation plan

- **Parity-when-dormant:** empty pack → `playability_smoke --all` reproduces the current 14/6
  contract and screenshots byte-identical (no pack = no behavior change).
- **Round-trip identity:** `repack.py` (dump→repack→load, no upscaler) renders byte-identical
  in-game — proves the dump key == loader key == decode.
- **One-wall end-to-end:** the first-milestone demo.
- **Negative-cache perf:** confirm no per-frame disk `stat` storm with a partial pack (instrument
  the miss path).
- **ASan** on the loader (stbi_load buffers, LRU eviction) + **contamination guard** stays green.

## Provenance

Compiled 2026-06-24 from a 5-area adversarially-verified plan (areas T-A..T-E), every anchor
checked against live `gfx_pc.c`; provenance independently confirmed against
`scripts/ci/check_no_rom_data.sh` and `.gitignore`.
