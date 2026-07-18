# PERF-006 — Material-derivation split (biggest single CPU win)

**Target:** `gfx_emit_loaded_triangle`, `src/platform/fast3d/gfx_pc.c` (~lines **17519–20009**,
~2,490 lines). Runs once PER EMITTED TRIANGLE and re-derives the complete material state each
time, though it is loop-invariant between RDP state changes — a 200-tri same-material batch pays
it 200×. Callers: `gfx_sp_tri1` (`:20291` clipped fan, `:20316` unclipped). Verified at `c69c01d`.

**The split must be a PURE REFACTOR — byte-identical rendering** (validated by
`port_renderer_parity_smoke` + screenshot/oracle suite + tape gate, all byte-exact). Do it as
**two separately-validated commits**: Step A (extraction, no cache) then Step B (dirty-flag cache).

## Verdict from recon
Achievable byte-identically, BUT the material is **not** a pure function of DL-command state.
The naive "any G_SETCOMBINE/othermode/tile/settex/geometry sets dirty" (the backlog's proposal)
is **necessary but NOT sufficient** — see §3 Class 2. The correct key is:
**handler-dirty-flag (§3.1) + a per-triangle equality check on ~7 non-command inputs (§3.2) +
cache-OFF when `g_any_diag_active()`**, with prim/env/fog COLORS deliberately left LIVE (uniforms,
consumed per-vertex — caching them is a stale-color bug; leaving them live is correct for free).

## 1. Boundary (per-material vs per-triangle)
- **Phase A** `17528–17963` — per-triangle reject/cull (NDC, diag gate [already `g_any_diag_active`],
  room attribution `dl_room`/`dl_which` at `:17548`, clip/backface cull). **Culling already precedes
  material derivation** — a culled tri never reaches phase C and leaves RDP state untouched.
- **Phase B** `17965–18009` — depth/viewport/scissor sync (cheap, existing redundancy guards). Leave
  OUTSIDE the cache; compute the depth booleans here and pass into derive.
- **Phase C** `18105–19285` (+ `19563–19571`) — **THE MATERIAL DERIVATION (extract this).** blend-mode
  classifier switch (`18131–18323`), `cc_options` assembly (`18446–18477`),
  `gfx_cc_id_raw_features` **CALL #1** `:18480`, per-tile UV clamp/mask + tile-state fetches
  (`18484–18604`), `effective_cc_id` (`:18609`), **CALL #2** `:18694`, room-water/cvg gates
  (`18696–18722`), `api_blend_mode` (`:18790`), sort predicate (`:18805`), **combiner lookup + shader/
  blend bind** (`18968–18989`), texture bind + sampler + `tex_width/height` (`18995–19244`),
  `use_texture` (`:19285`). (Raw features actually decoded **3×** — CALL #3 indirectly at `:19256`.)
- **Phases D/E/F** `19290–20008` — per-triangle traces/probes, **per-vertex writes** (`for vi<3` at
  `:19590`; combiner input-color loop `19694–19774` reads **live** prim/env/fog), sort/room
  bookkeeping, flush.
- **Boundary:** last material line = `use_texture = ...` at **`:19285`**; first per-vertex line =
  `for (int vi=0; vi<3; vi++)` at **`:19590`**.
- **Per-triangle work INTERLEAVED in phase C — keep it in the slim emit, reading cached MaterialState:**
  `lod_fraction = gfx_lod_fraction_for_triangle(...)` **`:19274`** (vertex-derived, consumes material
  outputs), `room_secondary_xlu_sort_key = gfx_room_xlu_tri_sort_key(...)` **`:18818`** (predicate is
  cached, key is per-tri), all phase-D traces.

## 2. MaterialState — fields to cache
Resolved: `comb` (`:18968`), `num_inputs`/`used_textures[2]` (`:18993`), `blend_mode`/`api_blend_mode`;
cc keys `cc_id`/`effective_cc_id`/`settex_material_cc_id`/`cc_options`; texture geometry `tex_tile_base`,
`allow_lod_redirect`, `tex_width/height[2]`, `tex_clamp_width/height[2]`, `settex_mirror_tex1`,
`mirror_tex1_from_tex0`, `allow_footprint_lod`, `settex_authored_lod_endpoint`; booleans `use_alpha`/
`use_fog`/`fog_use_fixed_alpha`/`texture_edge`/`use_noise`/`use_texture`; depth `depth_test/update/
compare/source_prim`, `zmode`, `depth_mode`, `sky_backdrop_depth`; room `room_matrix`,
`room_secondary_xlu_sort`, `scale_room_alpha_env`, `room_alpha_env_scale`, `diag_rdp_cvg_memory`
(**drives VBO stride** `:19602` — read from `m` in phase E or the layout desyncs); diag `tint_match`,
`diag_tint_color` (only when diag active — cache disabled then). **NOT cached:** `rendering_state`
(global, authoritative) and prim/env/fog/blend/fill colors + `prim_lod_fraction` (live per-vertex).

## 3. THE CORRECTNESS CRUX — dirty-flag triggers

### 3.1 Class 1 — DL command handlers → set `s_material_dirty = true` in the handler
| State | Handler (verify current line) | Dirty |
|---|---|---|
| `rdp.combine_mode` | `gfx_dp_set_combine_mode` (G_SETCOMBINE) | YES |
| `other_mode_l_raw`/`l`/`h`, tex_lod/detail, palette_fmt | `gfx_sp_set_other_mode` + `gfx_sync_other_mode_l_effective`; raw G_SETOTHERMODE_L/H, G_RDPSETOTHERMODE | YES |
| `rsp.geometry_mode` (+forced-fog) | `gfx_sp_geometry_mode` | YES |
| `first_tile_index`, `texture_scaling_factor`, `textures_changed` | `gfx_sp_texture` (G_TEXTURE) | YES |
| `texture_to_load`, `textures_changed` | `gfx_dp_set_texture_image` (G_SETTIMG) | YES |
| `texture_tile[]` descriptor | `gfx_dp_set_tile` (G_SETTILE) | YES |
| `texture_tile[]` uls/ult/lrs/lrt/w/h | `gfx_dp_set_tile_size` (G_SETTILESIZE) | YES |
| TLUT/palette | `gfx_dp_load_tlut` (G_LOADTLUT) | YES |
| `loaded_texture[]`, settex clear | `gfx_dp_load_block` (G_LOADBLOCK) | YES |
| `loaded_texture[]`, settex clear | `gfx_dp_load_tile` (G_LOADTILE) | YES |
| settex_active/gl_tex_id/tex_w/h/texturenum/tile_state | `gfx_handle_settex` + `gfx_settex_configure_tiles` (G_SETTEX) | YES |
| settex eviction (`~:4550`) | cache evict → settex_active=false | YES |

### 3.1b Colors — writers that MUST NOT dirty (no cached value depends on them; consumed live per-vertex)
`prim_color` (G_SETPRIMCOLOR), `env_color` (G_SETENVCOLOR), `fog_color` (G_SETFOGCOLOR),
`prim_lod_fraction` (moveword), `fill_color`. **Cache them and you get a stale-color bug.**

### 3.2 Class 2 — mid-batch mutations OUTSIDE the DL handlers (the trap — per-triangle EQUALITY CHECK)
All are already computed as locals at the top of the emit, so validate by equality every triangle:
1. **`dl_room` / `dl_which`** — `g_diag_current_cmd_addr` changes per DL command; feed water-suppress,
   cvg-memory, memory-blend-class→`api_blend_mode`, secondary-xlu-sort.
2. **`g_current_draw_class`** — feeds `frontend_settex_material`, WORLD_POS, fog-suppress, settex LOD.
3. **`g_sky_tri_mode`** — feeds `sky_backdrop_depth`, sort predicate, tint-sky.
4. **`g_texrect_uv_mode` / `g_texrect_tile_override`** — feed `tex_tile_base`, `allow_lod_redirect`, dims.
5. **`g_fillrect_draw_active`** — feeds `texture_edge` override.
6. `rsp.modelview_is_room_matrix[top]` / `modelview_room_id[top]` — feed `room_matrix`. Simplest: dirty
   on any matrix push/pop (`gfx_sp_matrix`/`gfx_sp_pop_matrix`).
> The naive flag misses #1–#5: e.g. a same-combiner room→secondary-DL transition changes `dl_which`,
> flipping `room_secondary_xlu_sort`/`cvg_memory`/`api_blend_mode` with NO G_SET* in between.

### 3.3 Class 3 — process/frame-constant
Diag latches (`g_diag_*`) never change mid-batch → **disable the cache entirely while
`g_any_diag_active()`** (zero-risks every diag interaction, no cost in production). Frame globals
(`g_pcSunShadow`, `g_pcPerPixelLight`, shadow/view-inv valid flags, `g_FogSkyIsEnabled`,
`z_is_from_0_to_1`) → **reset `s_material_dirty=true` at frame start** (`gfx_run_dl`/`gfx_sp_reset`).

## 4. Incremental plan
- **Step A — pure extraction, NO cache, byte-identical.** Lift phase C into
  `static bool derive_material(const inputs, struct MaterialState *m)` reading current RDP/RSP/settex
  state; the emit calls it EVERY triangle and consumes `m`. Move `lod_fraction`/sort-key/traces to
  read `m`. Phase C has no early `return`/`goto` except two graceful-degrade returns (combiner OOM
  `:18973`, `GE007_SKIP_ALPHA_TRIANGLES` `:18374` [diag-only]) → make derive return `bool success`,
  emit returns on false. ~1,180-line mechanical cut with local→field renames, no logic change.
  Validate parity byte-exact (de-risks the extraction independent of caching).
- **Step B — dirty flag + cache.** `static struct MaterialState s_material; static bool
  s_material_dirty=true;` Cache-hit key = `valid && !dirty && !g_any_diag_active() &&` the §3.2
  equality checks. On miss: `derive_material` (skips its guarded flush+binds on a hit — correct,
  because `rendering_state` is unchanged; see §5 flush invariant). Set dirty from §3.1 handlers +
  frame start. Validate parity byte-exact + 20-level census (the win) + sim-hash + tape.
- **Fold-ins AFTER the split lands (separate commits):** PERF-004 pt2/3, PERF-018 NDC reuse, PERF-001
  (redundant-flush skip becomes free — on a cache hit the guarded flush sites aren't reached).

## 5. Risks
1. **Missed dirty trigger (highest).** Mitigated by §3.2 equality checks + cache-off-under-diag. Add a
   DEV self-check (CHECK counter, since ctest is `-DNDEBUG`): re-derive into a scratch struct every Nth
   triangle and `memcmp` vs the cache, flag on mismatch.
2. **Material reading a per-triangle input.** Only 3 (`lod_fraction`, sort key, `diag_tri_ndc`) — stay
   per-triangle.
3. **`gfx_flush()` mid-batch.** The cached material MUST survive a flush (do NOT re-derive): `gfx_flush`
   only drains buf_vbo; the deferred-XLU replay saves+restores `rendering_state` fully
   (`gfx_apply_rendering_state_snapshot`), so the cache's "rendering_state still reflects my binds"
   holds. **Load-bearing invariant — guard with a comment.**
4. **Colors = uniforms** (§3.1b) — not cached, not dirtying; per-vertex loop reads live.
5. **`diag_rdp_cvg_memory` controls VBO stride** — phase E must read it from `m`.

## Validation commands (byte-exact at each step)
`ctest -R "port_renderer_parity_smoke|screenshot_series|port_synth_frame_screenshot|sim_state_hash"`;
the oracle/route captures (`tools/fidelity/*`); `tools/perf_census.sh` (20-level before/after);
tape gate; `GE007_TINT_SKY=1`/`GE007_TRACE_ROOM_ALPHA=1` diag spot-checks (cache bypassed under diag).
Re-record baselines only AFTER byte-exact confirmed.
