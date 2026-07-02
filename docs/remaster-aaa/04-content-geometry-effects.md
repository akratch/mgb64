# W4 — Content Completeness, Geometry & Effects ("zero known visual bugs")

**Workstream owner doc — MGB64 AAA Remaster program.**
Branch of record: `feat/metal-backend`. Constitution: [`docs/REMASTER_ROADMAP.md`](../REMASTER_ROADMAP.md) — rails R1 (gameplay-invariant), R2 (copyright tiers), R3 (opt-in/default-identity) govern every task below.

---

## 1. Executive summary

W4 closes every remaining content gap, geometry bug, and dated effect so the remaster ships with **zero known visual flaws**. The good news, verified against the live tree: the two "big" decomp gaps the survey flagged are *already coded* — shoot-out-the-lights is fully implemented default-off pending in-game verification (`src/game/lightfixture.c:110-498`, population pass `src/game/bg.c:301-377`), and the repo-wide stub guard passes (`tools/check_native_stub_surface.py` → PASS, zero `PORT_TODO`/`PORT_STUB` hits in `src/`). What remains is verification-and-flip work, one real population-gap bug we found (on-demand room loads never populate the fixture table, `bg.c:9889`), the glass-shard presentation endgame, resolving the train-sky-leak *fog fork* (the leak is N64-faithful geometry; the port's weaker distance-darkening exposes it), and a bounded effects-quality uplift (HD effect textures, density flags, soft particles — scene depth now exists in both backends).

| Headline deliverable | What a reviewer sees |
|---|---|
| Shoot-out-the-lights ON by default | Shoot a Bunker/Facility lamp → surface dims ~¼, shards fly, stays dark on re-entry |
| Train sky leak resolved | Pad-24 corridor: gap fades to black exactly like N64 (or opt-in occluders for HD draw distance) |
| Glass shards signed off | Dam pane shatter reads correct: bounded shards, aligned decal, no oversized/misprojected pieces |
| Effects uplift | HD decals/flares/smoke via texture pack; soft particles (no hard intersection lines) behind a flag |
| Zero-known-flaws certification | Full regression suite green; STATUS.md flaw table empty; every fix A/B-flagged |

Total estimate: **~78 junior-days (~16 junior-weeks)** across 5 epics.

---

## 2. Current state (verified in-tree)

### 2a. Shoot-out-the-lights — IMPLEMENTED, default OFF, unverified in-game

The decomp-survey claim ("3 PORT_TODO stubs at lightfixture.c:341/111/136") is **stale — the port landed** in commit `a873ec4` (contained in `feat/metal-backend`). Verified state:

| Piece | Where | Status |
|---|---|---|
| Flag helper `ge007_shoot_out_lights_enabled()` — `GE007_SHOOT_OUT_LIGHTS=1` opt-in, **default OFF** | `src/game/lightfixture.c:110-124` | ✅ coded |
| `return_ptr_vertex_of_entry_room` — raw big-endian backward G_VTX walk (8-byte stride), `uintptr_t` 0x0E-segment rebase, DL-start guard | `lightfixture.c:134-153` | ✅ coded |
| `extract_vertex_indices_from_triangle` — G_TRI1 (0xBF) bytes 5/6/7 ÷10; G_TRI4 (0xB1) raw 4-bit, authoritative bg.c decode | `lightfixture.c:173-193` | ✅ coded |
| Parent `sub_GAME_7F0BBE0C` — gated first-statement, darken + edge-particle spawn + neighbour scan at raw 8-byte stride | `lightfixture.c:393-498` | ✅ coded |
| Native population pass `lf_populate_one_dl` / `lf_populate_room_lightfixtures` — walks primary + secondary raw room DLs, records light runs via `bgFindTextureForTriangleCommand` | `src/game/bg.c:315-377` | ✅ coded |
| Population call at level-load room path | `src/game/lvl.c:274-281` (before `redarken_lights_in_room`) | ✅ wired |
| Population call at **on-demand gameplay load path** `sub_GAME_7F0B6368` | `src/game/bg.c:9752` (fn), `bg.c:9889` calls `redarken` only — **`lf_populate_room_lightfixtures` is MISSING** | ❌ **gap (bug)** |
| Persistence: `redarken_lights_in_room` reapplies `cn >>= 2` per re-entry | `lightfixture.c:253-272`; callers `bg.c:9889`, `lvl.c:281` | ✅ coded |
| Shot-path caller (material-gated by `check_if_imageID_is_light`, `lightfixture.c:87-107`) | `src/game/chrprop.c:3069` | ✅ live |

Residual per the header comment at `lightfixture.c:111-115` and plan `docs/SHOOT_OUT_LIGHTS_PLAN.md` §8: **in-game verification of correct-surface darkening** (risk #1: G_VTX-derived vertex base vs the hit-test's `vtx_base_offset`-biased base could diverge by a fixed stride → wrong surface dims), then a default-ON flip decision. The plan doc's §7 validation recipe is ready to run as written.

### 2b. Glass shards — projection parity DONE, presentation parity OPEN

`docs/GLASS_SHARDS_WIP.md` is authoritative and *more advanced* than the "too large/random" summary: shards render bounded and correctly projected. Un-stubbed at `src/game/unk_0A1DA0.c:1136` (default on, `GE007_GLASS_SHARDS=0` A/B, gate helper `:1122-1131`). The crash (`dynAllocateMatrix` missing prototype → pointer truncation) and the too-large/misprojected regressions are fixed with a full A/B flag ladder (`GE007_GLASS_SHARD_COMPRESS/_BASIS_SCALE/_NO_BASIS_SCALE/_SQRT_BASIS/_INV_VIS_SCALE`, `unk_0A1DA0.c:1279-1295`). Closed milestones (proofs in the WIP doc): active-shard projection parity `90 → 90` pieces, onscreen `82 → 82`, union area `65.2% → 65.0%`; impact world-quad exact parity; projected decal center `0.055px ≤ 1.0px`. **Open:** stock *presentation* pixel parity (`glass_burst` still ~86-99% changed), driven by pane/crack/decal phase + actor-composition dirt (native renders extra visible actor `45`; chr `12` `hidden_bits/action` drift). Guards: `tools/glass_material_regression.sh`, `glass_active_visual_isolation_regression.sh`, `glass_impact_visual_isolation_regression.sh`, `glass_pad10092_impact_visual_regression.sh`, `glass_actor_masked_visual_regression.sh`. Best next fixture (per the doc's own tie-off): **pad `10092` / yaw `315` / distance `650`** impact route.

### 2c. Train sky leak — root-caused: N64-FAITHFUL gap, exposed by weaker distance-darkening

Investigation state (2026-07-01, confirmed against code):
- Repro: `GE007_TINT_SKY=1` (`gfx_pc.c:4774`, `:13083`) + `GE007_AUTO_WARP_PAD` (`src/platform/stubs.c:1391`); leaks at pads **0/6/24/174 interior**, 84/102/78 exterior. Exact leak pose pinned via `GE007_AUTO_FORCE_PLAYER_SCRIPT="40-120:-6391.6:333.76:-319.58:270:-4:167.31:24"` (magenta=518).
- **Ruled out:** far plane (Train far=1500, faithful); room admission (`GE007_FORCE_RENDERED_ROOMS=0..57` → leak unchanged; `GE007_FORCE_ALL_ROOMS`, `bg.c:16349`, no effect). No polygon in ANY room covers the gap — the sky backdrop (`skyRender`, `src/game/player.c:280`, drawn first in `lvl.c:1673-1678`) legitimately shows.
- **User-confirmed on N64 footage: the gap EXISTS on real hardware but is hidden** — distant geometry fades to near-black. `GE007_FOG_DENSITY=2.0` reproduces the N64 look (gap blends away). Port fog: `gfx_fog_coord_for_vertex` (`gfx_pc.c:13463`) + application at `gfx_pc.c:16365-16373` (`fog_coord * density * rsp.fog_mul + rsp.fog_offset`; Train pad-24 fog_mul/offset = 32000/-31744, fog color black). **Open fork:** (i) port fog genuinely weaker than N64 at far depth = fidelity bug → fix curve; vs (ii) fog faithful, N64 conceals via CRT/low-res → deliberate opt-in darkening. Decided by measurement, not guessing (E3.T1).
- `tools/train_window_backdrop_regression.sh` is a **false alarm** (pad 74 has zero sky pixels; its "bright blue" metric miscalibrated for Train's white-cloud sky) — must be fixed or retired.

### 2d. Effects — shipped fixes verified; quality ceiling inventoried

| Effect | State (verified) |
|---|---|
| Explosion "confetti" (tile-size over-read) | **Fixed, default on**: decode footprint clamped to true loaded extent, `gfx_pc.c:11330-11344`; A/B `GE007_TILESIZE_CLAMP_SUBLOAD=0`. Manual close-up visual pass never recorded — do it once and log it. |
| Bullet sparks | Un-stubbed default-on (`unk_0A1DA0.c:4082-4094`, `GE007_BULLET_SPARKS=0` A/B); struct-misalignment UB fixed in `df2f1de` (in branch). |
| Prop bullet decals | Textured (N64 parity) default; flat path only behind `GE007_FLAT_PROP_BULLET_IMPACTS` (`src/game/explosions.c:104-150`); RDP state-reset epilogue hardened. |
| Muzzle flash | Fully ported billboard star/flare quads (`src/game/gun.c:3382+`, draw ~`:12868`); kill-switch `GE007_DISABLE_MUZZLE_FLASH` (`gun.c:60-67`). Reads dated: 4 tiny N64 textures, hard alpha. |
| Smoke | Faithful (`g_SmokeTypes`, `explosions.c:717/733`; `SMOKE_BUFFER_LEN 20`, `explosions.h:21`). Reads dated: low-res frames, hard edges where quads intersect geometry. |
| Explosions | Effect-slot bump already shipped compile-time `EXPLOSION_BUFFER_LEN 6→16` (`explosions.h:16-18`); deliberately not a runtime setting (`platform_sdl.c:1911-1913`), shake decoupled (`explosions.c:1085`). |
| Water splash | No dedicated splash system exists in the port sources (only sky/water backdrop images, `player.c:905/1349`); water impacts route through `g_ImpactTypes` (`explosions.c:826`). Inventory task, not an assumed gap. |
| **Soft-particle substrate** | Scene depth is now a sampleable texture in BOTH backends: GL `g_scene_depth_tex` (`gfx_opengl.c:650`, `GL_DEPTH_COMPONENT24` texture `:2338`), Metal `Depth32Float` (`gfx_metal.mm:816`). Effects currently z-test hard against it → visible intersection lines. |
| HD effect textures | Engine B hook is per-static-settex-token (`texture_pack_try_load`, `gfx_pc.c:21061`; loader `src/platform/texture_pack.c`). Effects drawn via `texSelect` (decals, flares, smoke, shards) go through this same decode → **pack-upgradeable today**. `DrawClass` tagging exists (`gfx_pc.c:451-560`) to find effect tokens. |

### 2e. Residual decomp gaps & aspect leftovers

- `grep -rn "PORT_TODO\|PORT_STUB" src/` → **0 hits**; `tools/check_native_stub_surface.py` → **PASS**. The survey's other top-4 (watch ammo, bgorder portal, room AABB, door OBB) landed via the merged `feat/dam-hd-remaster` work.
- One documented native workaround, not a stub-hole: `sub_GAME_7F03E27C` (room intersection during prop setup) is stubbed on native; guards get stan tiles via the `sub_GAME_7F0AF20C` fallback at `src/game/chr.c:1979-1999` (`[STAN_FIX]` log). Works; needs a triage entry, not necessarily code.
- Widescreen/aspect: all five fixes live and default-on — sky (`player.c:65-69`, `GE007_NO_SKY_ASPECT_FIX`), edge-cull (`bondview.c:2016`, `GE007_NO_CULL_ASPECT_FIX`), Bond body (`bondview.c:2829-2831`, `GE007_NO_BOND_BODY_FIX`), global kill-switch `GE007_DISABLE_ASPECT_CORRECTION` (`gfx_pc.c:15590`). The prior survey's HUD "oval/drift" findings were confirmed NOT bugs (shape-correct anamorphic cancellation). Leftover: no 21:9/ultrawide or split-screen-pane sweep has ever been run.

---

## 3. Target state — the AAA bar

A reviewer playing `--remaster` (and `--faithful`) sees, concretely:

1. **Interactive world**: shooting any lamp/neon/panel light visibly darkens that fixture (~¼ shade) with debris, persists across room re-entry, on every level with fixtures (Bunker, Facility, Archives…). Feeds W1's dynamic lights: a shot-out fixture also dims its light contribution.
2. **No sky bleed anywhere**: a full 20-level tint-sky sweep shows zero unintended backdrop pixels at faithful settings; Train reads exactly like N64 (gaps swallowed by darkness), and remaster draw-distance users get clean occlusion instead of holes.
3. **Glass that sells the fantasy**: pane shatter produces believable shard bursts, an aligned bullet-hole/crack decal, and no oversized/screen-spanning/misprojected artifacts — signed off by eye at 4K on the lit Dam tower pane, guarded by the existing gate stack.
4. **Effects that don't read 1997**: HD (pack-local, Tier B) decals/flares/smoke frames; particles fade softly through geometry instead of hard clip lines; effect density is tunable; muzzle flash blooms naturally through the existing bloom pass.
5. **A published flaw ledger that is empty**: STATUS.md "known visual issues" table has no open rows; every closed row cites its gate.

---

## 4. Technical design

### 4.1 E1 — Shoot-out-the-lights: verify, fix the load-path gap, flip ON

**4.1.1 The population gap (bug, fix first).** Rooms loaded on demand during gameplay by `sub_GAME_7F0B6368` (`bg.c:9752`) call `redarken_lights_in_room(room)` at `bg.c:9889` but never `lf_populate_room_lightfixtures(room)`. Consequence with the flag on: any room loaded after level start has an empty `light_fixture_table` → shooting its lights silently no-ops; worse, stale entries from an unloaded room keep dangling `[ptr_start,ptr_end)` DL pointers that a *new* allocation could alias (pointer-range match against freed memory → wrong-room darkening). Fix:

```c
/* bg.c, in sub_GAME_7F0B6368, immediately before line 9889 */
lf_populate_room_lightfixtures(room);   /* clears stale entries + repopulates; no-op when flag off */
redarken_lights_in_room(room);
```

Also add unload hygiene: in the room-free path (`delete_room_data` / `pc_room_allocs` teardown in `lvl.c`), call `clear_light_fixturetable_in_room(roomID)` (`lightfixture.c:649-660`) so no table entry ever outlives its DL buffer. Both calls are inside the existing flag gate semantics (`lf_populate_room_lightfixtures` early-returns when off, `bg.c:358`; the clear must be similarly gated to preserve byte-identity: wrap it in `if (ge007_shoot_out_lights_enabled())`).

**4.1.2 Correct-surface verification (plan §8 risk #1).** Add a temporary debug assertion behind `GE007_LF_VERIFY=1` in `darken_triangle_in_room` (`lightfixture.c:321-337`): recompute the triangle's absolute `Vtx*` via the hit-record route (`bg.c:11425-11450` parses the collision-DL header — `vtx_base_offset = coll_dl[1] & 0xF`, `vtx_pool_base = vtx_base + (word4 & 0xFFFFFF)` — and decodes indices as `raw/10 − vtx_base_offset`; the absolute `Vertex*` pointers `vtx_pool_base + idx*0x10` are stored into the hit record at `bg.c:11543-11545` via `bgPopulateHitRecord`, `bg.c:214-231`) and `fprintf` both pointers + the `>>4` index. Drive it headlessly with the scripted damage rig (same rig as glass): warp to a lamp, fire, and assert equality. If they diverge by `vtx_base_offset*0x10`, subtract `vtx_base_offset` in `extract_vertex_indices_from_triangle` — never bias both sides (plan `SHOOT_OUT_LIGHTS_PLAN.md` §8.1).

**4.1.3 Headless proof harness** `tools/shoot_out_lights_regression.sh` (new): Bunker/Facility route, `GE007_SHOOT_OUT_LIGHTS=1 GE007_AUTO_WARP_PAD=<lamp pad> GE007_AUTO_DAMAGE_TAG_SCRIPT=...` or forced-fire script, screenshots pre/post shot + post re-entry; assert (a) ROI mean-luma drop ≥ 30% over the fixture, (b) re-entry screenshot ≈ post-shot screenshot (`--max-changed-pct 1.0`), (c) OFF-run byte-identical to HEAD.

**4.1.4 Default flip.** After 4.1.1-4.1.3 are green plus one manual in-game session: change `lightfixture.c:121` polarity to default-ON (`e[0]=='0' ? 0 : 1`, the `ge007_glass_shards_enabled` idiom), update `VISUAL_MODES.md` and the `--faithful` note (this is a *faithfulness restoration*, class C3 — it belongs ON even in faithful mode; keep the env as an A/B escape hatch only). Rails: R1 — darkening writes only `Vtx.v.cn` shade bytes, never `ob[]` positions, so collision/LOS/AI are untouched by construction; still run the sim-invariance gate because the particle spawns allocate.

**4.1.5 W1 handoff (dynamic lights feed).** Export a read-only query for the lighting workstream:

```c
/* lightfixture.h */
typedef struct { s16 room; s16 count; float center[3]; } LfDarkenedCluster;
s32 lf_get_darkened_clusters(s32 room_index, LfDarkenedCluster *out, s32 max); /* aggregates darkened_light_table by proximity (get_room_data_float1()*100 radius, same metric as sub_GAME_7F0BBCCC, lightfixture.c:359-390) */
```

Renderer-side (W1) can then attenuate a fixture's dynamic light when its cluster is darkened. This is render→render data flow (table is written by sim, read by render — same direction as `Vtx.cn` today), R1-clean.

### 4.2 E2 — Glass shards completion

Follow the WIP doc's own tie-off instructions (`GLASS_SHARDS_WIP.md` "Tie-off / pause point"): **no more broad screenshot sweeps or global alpha tweaks** — both produced negative controls. The remaining delta is per-pixel *presentation* (pane break visual, crack/decal, draw phase, material state), pursued on the two clean fixtures:

1. **Pane/decal presentation** on `dam_regular_glass_shatter_pad10092_impact_visual_probe` (guard `tools/glass_pad10092_impact_visual_regression.sh`, already passing geometry: impact center Δ4.785 ≤ 5, decal Δ0.949px ≤ 1.0px). Work the recorded next step: one bounded recapture at the stock/aligned pixel (`188,170` stock ↔ `94,95` native) comparing raw RDP state (`other/combine/env`, blend/depth/combiner words — the ares probe already emits these) against the native `[SETTEX-PIXEL]` row. If a state word differs → fix that RDP-state translation in `gfx_pc.c`; if not, the doc's own stop-rule applies: **stop the pixel-parity thesis** and re-scope to "looks right" (step 2).
2. **Visual tuning harness** (the actual AAA deliverable — parity with a 240p N64 frame is *not* the bar): expose the shard look as tunables — count cap, size scale, initial velocity spread, gravity, lifetime — as `GE007_GLASS_SHARD_{COUNT,SIZE,VEL,LIFE}` env floats read next to the existing basis flags (`unk_0A1DA0.c:1279-1295`), defaults = current faithful values (identity). Author one remaster preset reviewed by eye at the lit tower pane (`GE007_AUTO_DAMAGE_TAG=19` Dam basement pane is the deterministic CI repro; the lit pane is the *judgment* fixture). Wire the chosen preset into `--remaster` (`platform_sdl.c` `s_remasterPreset`) only if it survives review.
3. **Gate promotion**: keep `impact_pixel_oracle`/`visual_oracle` report-only (they are actor-dirty by proven necessity); promote the geometry gates (projection parity, decal ≤ 1px, impact quad identity) into `tools/dam_visual_regression_suite.sh` as hard gates so shard geometry can never regress silently.

Rails: R3 — all tunables identity-default; R1 — shards are pure render (spawned from render-side particle pool); R2 — no assets involved.

### 4.3 E3 — Train sky leak: decide the fog fork, then fix

**E3.T1 Measure (decides everything).** Two independent measurements, either settles the fork:
- *Fog-equation comparison*: instrument `gfx_fog_coord_for_vertex` (`gfx_pc.c:13463`) to dump `(clip_z, clip_w, fog_coord, fog_z)` for the pad-24 corridor's far wall vertices; hand-compute the N64 RSP fog formula (`fog = clamp(fog_mul * z/w + fog_offset, 0, 255)` in s10.5 — check against `rsp.fog_mul/offset` load at `gfx_pc.c:19293-19294`) with the same fog params (32000/-31744). Any divergence at far depth (esp. the near-far normalization or the `2z-1` vs `z` NDC convention) = fidelity bug.
- *ares Train oracle* (definitive but heavier): build a Train route JSON per `docs/ROM_COMPARISON.md` — needs EEPROM seed (`ge007_eeprom.bin` exists in-repo), Train menu-nav events, and the matched pose hooks (`GE007_AUTO_FORCE_PLAYER_SCRIPT` ↔ `MGB64_ARES_FORCE_PLAYER_SCRIPT`), then compare far-corridor luma port vs stock.

**E3.T2a — if fog is a fidelity bug (Track A, preferred):** correct the curve in `gfx_pc.c:16365-16373` (single CPU-side site — backend-agnostic, no GLSL/MSL change needed since fog factor is computed per-vertex on CPU). This is *faithful-path-affecting*: it must be validated as a faithfulness **fix** (stock N64 comparison), gated `GE007_FOG_CURVE_FIX=0` escape hatch, default ON, and swept across all 20 levels (`tools/perf_census.sh` route list) since it fixes the whole "we see too far" class.

**E3.T2b — if fog is faithful (Track B, remaster-only):** the gap must be concealed only when the remaster brightens/extends visibility. Two options, both R3-gated, in preference order:
1. **Render-side interior backdrop** (`GE007_INTERIOR_BACKDROP=1`, Video.InteriorBackdrop): when the current room is interior (interior-room list for Train to be measured with `GE007_TINT_SKY` + the leak-pad table — the "rooms 14-26" span is a starting hypothesis, not verified; store it as a new hardcoded per-level table in `src/game/lvl.c`), `skyRender` substitutes a solid near-black quad (env bg color `fog.c:248`: Train bg RGB=(0,0,8)) for the animated cloud sky. Cheap, zero geometry, exactly what N64 darkness achieved. Tier A1 (a color, no assets).
2. **Occluder patches** (fallback if backdrop reads wrong at exterior/interior transitions): hand-authored quads (≤ 8 per leak pad, coordinates measured with `GE007_TINT_SKY` + the force-player script) appended to the room DL at load in `lvl.c`'s post-load fixup block (`lvl.c:271-283` — same place the lightfixture pass hooks). Hand-measured coordinates in our own C table = **Tier A1** (facts about geometry, not copied assets); gate `GE007_GAP_OCCLUDERS`.

**E3.T3 Harness repair:** rewrite `tools/train_window_backdrop_regression.sh`'s miscalibrated blue metric to a `GE007_TINT_SKY` magenta-pixel count (ground truth, immune to sky palette), and add `tools/sky_leak_sweep.sh`: for each of the 20 levels, warp the documented leak pads (Train 0/6/24/174/84/102/78 + any new finds) and assert magenta==0 (Track A default) or magenta==baseline (faithful preserved, remaster flag clears it).

### 4.4 E4 — Effects quality pass

**4.4.1 HD effect textures (dep: W2 pack pipeline).** Effects drawn through `texSelect` hit the same static-settex decode as world textures, so `texture_pack_try_load` (`gfx_pc.c:21061`) upgrades them with **zero new code**. Work = *content routing*: dump with `GE007_DUMP_SETTEX_TEXTURES='*'` during an effects-heavy scripted route (explosion + gunfire + glass on Dam), identify effect tokens by `draw_class==EFFECT`/`WEAPON` in the texmanifest (W2's P1.2 emit; interim: the hue-encoding throwaway-pack technique from `REMASTER_ROADMAP.md` §3), and add them to the pack build. Animated frame-indexed effect images that upload via `effect_image_from_handle` byte pointers (`unk_0A1DA0.c:4197+`) bypass the settex hook — inventory which effects those are and mark them out-of-scope for packs (they need the hash-key loader path that `texture_pack.c` deliberately lacks; do NOT build it in W4). All outputs Tier B, local-only, contamination guard applies.

**4.4.2 Soft particles (the one new shader feature — lands in BOTH generators).** Substrate exists: opaque scene depth as a texture (GL `gfx_opengl.c:650/2338`; Metal `gfx_metal.mm:816`). Design:
- **Depth copy**: sampling the bound depth attachment while depth-testing is a feedback hazard. At the first EFFECT-class XLU draw of a frame (detect via `g_current_draw_class`, `gfx_pc.c:451`, in `gfx_flush`), copy `g_scene_depth_tex` → new `g_scene_depth_copy` (GL: `glBlitFramebuffer` GL_DEPTH_BUFFER_BIT into a depth-texture FBO; Metal: `MTLBlitCommandEncoder copyFromTexture` — reuse the existing RDP/XLU snapshot plumbing pattern). One copy per frame (opaque world is already drawn when effects start; room-XLU deferral, `gfx_pc.c:13760+`, guarantees ordering).
- **Shader variant**: add a `soft_particle` bit next to the existing `CCFeatures` extras (GL generator `gfx_opengl_create_and_load_new_shader`, `gfx_opengl.c:819-834`; MSL mirror `mtl_create_and_load_new_shader`, `gfx_metal.mm:621`, emit at `:260+`). Set the bit at draw time when `draw_class==EFFECT && blend_active && Video.SoftParticles`. FS epilogue (GLSL; MSL is the mechanical mirror using the ssao linearize helpers already ported, `gfx_metal.mm:1271-1276`):

```glsl
// uniforms: uDepthCopy (sampler2D), uProjA, uProjB (same A=P[2][2], B=P[3][2] pair SSAO uses)
float linZ(float d){ float ndc = 2.0*d - 1.0; return uProjB / (ndc + uProjA); }
float sceneZ = linZ(texture(uDepthCopy, gl_FragCoord.xy / uFbSize).r);
float fragZ  = linZ(gl_FragCoord.z);
fragColor.a *= clamp((sceneZ - fragZ) / uSoftRange, 0.0, 1.0);   // uSoftRange ~ 12 world units
```

- Gate: `Video.SoftParticles` (+`GE007_SOFT_PARTICLES`), default 0; added to `s_remasterPreset`. Off = the variant bit never sets = identical shaders = byte-identical.
- Metal/GL parity is mandatory (project rule: every shader feature in both generators); GL is the Linux/Windows reference, Metal the macOS flagship.

**4.4.3 Density/scale flags (bounded).** Only render-only knobs; the sim-coupled buffers (`EXPLOSION_BUFFER_LEN` — explosions deal damage, `s_explosiontype.damage` + `dmg_range`, `explosions.h:84`; `SMOKE_BUFFER_LEN` — smoke slots are spawned by sim-side code, `explosionCreateSmoke` `explosions.c:2363` called from the explosion tick `:1679-1688`, so slot exhaustion is sim-visible) stay compile-time exactly as `platform_sdl.c:1911-1913` decided. Safe uplift: spark quad count/size (`unk_0A1DA0.c` spark render), shard count (E2's `GE007_GLASS_SHARD_COUNT`), muzzle-flash scale (`gun.c:3387` `muzzle_scale` path) — each an env-float multiplier, identity default.

**4.4.4 Verification of shipped fixes.** One recorded close-up A/B each for confetti (`GE007_TILESIZE_CLAMP_SUBLOAD=0` vs default, grenade on Dam) and prop decals (crate/barrel/monitor shots) — the memory notes both lack a logged manual visual pass. Screenshot pairs archived locally, results noted in STATUS.md.

### 4.5 E5 — Residual gap triage & aspect QA

- Re-grep suite as a CI-adjacent script `tools/decomp_gap_inventory.sh`: `PORT_TODO|PORT_STUB|stubbed|not implemented` across `src/`, plus `tools/check_native_stub_surface.py` (no `--gate` flag exists — it already exits non-zero on failure; only `--repo-root` is accepted); output a triage table (currently expected: 2 informational rows — `chr.c:1979` stan fallback, `textrelated.c:5041` note — 0 action rows).
- Ultrawide/split-screen aspect sweep: run the five aspect-fix repro scenes (sky bars, blood overlay, edge-cull, crosshair, split-screen rects) at 21:9 (2560×1080) and 32:9, 1P and 2P split; verify the `(4/3)/window_aspect` squeeze class (`player.c:48-69` comment documents the invariant) holds. File-and-fix any straggler with the same pattern + `GE007_NO_*_FIX` escape hatch.

---

## 5. Work breakdown

Estimates are **junior-engineer-days**. Cross-workstream: W1 = Lighting, W2 = Textures, W3 = Backend/Metal.

### Epic W4.E1 — Shoot-out-the-lights: verify → fix → flip (13 jd)

| ID | Task | Files | Steps | Acceptance (runnable) | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| **W4.E1.T1** | Fix on-demand-load population gap + unload hygiene | `src/game/bg.c` (~9889), `src/game/lvl.c` (room-free path), `src/game/lightfixture.c` | Insert `lf_populate_room_lightfixtures(room)` before `redarken` in `sub_GAME_7F0B6368`; gated `clear_light_fixturetable_in_room` on room unload | `cmake --build build --parallel`; a `-DLF_POPULATE_DEBUG` build (compile-time define, `bg.c:368`, not an env var) run with `GE007_SHOOT_OUT_LIGHTS=1` logs `[LF] room N` on a mid-level door-triggered room load (Bunker walk route); OFF-run screenshot vs HEAD: `python3 tools/compare_screenshots.py a.bmp b.bmp` → 0.000% | 2 | — | Gated `GE007_SHOOT_OUT_LIGHTS`; OFF byte-identical |
| **W4.E1.T2** | Vertex-base consistency verification (risk #1) | `lightfixture.c` (temp `GE007_LF_VERIFY` assert), `tools/shoot_out_lights_regression.sh` (new) | §4.1.2 assertion; scripted lamp shot; compare G_VTX-derived vs hit-record vertex pointers; resolve any `vtx_base_offset` bias one-sided | Assert log shows pointer equality on ≥5 distinct fixtures across 2 levels; if biased, fix + re-run; `tools/asan_smoke.sh --gate` clean | 3 | E1.T1 | Debug-only flag; no default change |
| **W4.E1.T3** | Headless darkening + persistence harness | `tools/shoot_out_lights_regression.sh` | §4.1.3: pre/post/re-entry screenshots, ROI luma gate, OFF-control | Script exits 0: luma drop ≥30% on fixture ROI, re-entry `--max-changed-pct 1.0`, OFF vs HEAD 0.000% | 3 | E1.T2 | R3 control lane built-in |
| **W4.E1.T4** | Manual in-game pass + default-ON flip + docs | `lightfixture.c:121`, `docs/VISUAL_MODES.md`, `docs/SHOOT_OUT_LIGHTS_PLAN.md` (status), `docs/STATUS.md` | Play Facility+Bunker, shoot ≥10 fixtures, check neighbours/persistence/particles; flip polarity; document C3 faithful-restoration rationale | `tools/sim_invariance_gate.sh` identical hashes ON vs OFF; `tools/playability_smoke.sh --all` green; `scripts/ci/check_sim_render_separation.sh` green; flag table updated | 2 | E1.T3 | Default-ON justified as faithfulness restoration (survey class C3); env stays as A/B |
| **W4.E1.T5** | W1 dynamic-lights feed API | `lightfixture.h/.c` (`lf_get_darkened_clusters`) | §4.1.5; unit-test clustering against a synthetic table | `ctest -R lightfixture` (new unit) passes; header reviewed by W1 owner | 3 | E1.T4; **gates W1's shot-out-light dimming** | Read-only export; R1-clean |

### Epic W4.E2 — Glass shards completion (17 jd)

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| **W4.E2.T1** | Bounded RDP-state recapture (the WIP doc's one sanctioned next probe) | ares probe configs, `tools/` glass harnesses | One recapture at stock `188,170` ↔ native `94,95` on the pad-10092 impact route; diff raw `other/combine/env` + blend/depth words vs `[SETTEX-PIXEL]` | Either a concrete RDP-state divergence is filed with the differing word, or the pixel-parity thesis is closed in `GLASS_SHARDS_WIP.md` per its stop-rule | 4 | — | Diagnostic only |
| **W4.E2.T2** | Fix any found state divergence in the DL translator | `src/platform/fast3d/gfx_pc.c` | Correct the identified render-mode/combine translation; A/B env for the change | `tools/glass_material_regression.sh`, `glass_pad10092_impact_visual_regression.sh`, `dam_visual_regression_suite.sh` all green; default-path 20-level screenshot sweep 0.000% off-route | 4 | E2.T1 (skip if T1 closes thesis) | `GE007_*` escape hatch; faithful-affecting → stock comparison required |
| **W4.E2.T3** | Shard look tunables + remaster preset | `src/game/unk_0A1DA0.c` (next to `:1279` flag ladder), `src/platform/platform_sdl.c` (`s_remasterPreset`) | Add `GE007_GLASS_SHARD_{COUNT,SIZE,VEL,LIFE}` identity-default floats; author preset by eye on the lit Dam tower pane (`GE007_AUTO_DAMAGE_TAG=19 GE007_AUTO_DAMAGE_TAG_FRAME=80 GE007_AUTO_DAMAGE_TAG_AMOUNT=80 ./build/ge007 --level dam --deterministic --screenshot-frame 110 --screenshot-exit` + warp pad 100 close-up) | Identity run byte-identical (compare_screenshots 0.000%); preset run reviewed + screenshot archived; ASan clean | 5 | — | R3 identity defaults; preset only via `--remaster` |
| **W4.E2.T4** | Promote geometry gates into the umbrella suite | `tools/dam_visual_regression_suite.sh` | Add projection-parity, decal ≤1px, impact-quad-identity as hard gates; keep pixel oracles report-only | Suite fails on a seeded shard-basis regression (`GE007_GLASS_SHARD_NO_BASIS_SCALE=1` as the negative control); passes on HEAD | 2 | E2.T2/T3 | — |
| **W4.E2.T5** | Sign-off + WIP doc closure | `docs/GLASS_SHARDS_WIP.md`, `docs/STATUS.md` | Record final state, retire stale `/tmp` proof pointers into the doc, mark milestone closed | Doc review; STATUS row closed with gate citation | 2 | E2.T4 | — |

### Epic W4.E3 — Train sky leak (14 jd)

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| **W4.E3.T1** | Fog-fork measurement | `gfx_pc.c:13463` (temp instrumentation), scratch analysis script | §4.3 T1: dump port fog terms at the pinned pose (`GE007_AUTO_FORCE_PLAYER_SCRIPT="40-120:-6391.6:333.76:-319.58:270:-4:167.31:24"`); recompute N64 RSP formula; optional ares Train route (EEPROM seed + menu-nav per `docs/ROM_COMPARISON.md`) | A written verdict with numbers: port fog_z vs N64 fog_z at ≥5 far depths; fork (i)/(ii) decided | 5 | — | Instrumentation only |
| **W4.E3.T2** | Implement the winning fix | Track A: `gfx_pc.c:16365-16373`; Track B: `src/game/player.c` (`skyRender`) or `src/game/lvl.c:271-283` occluder table | §4.3 T2a or T2b per verdict | Pad sweep 0/6/24/174/84/102/78: magenta==0 (A, default) or ==baseline-off / 0-on (B); Track A additionally: 20-level screenshot sweep + stock-N64 far-depth luma check; `tools/sim_invariance_gate.sh` green | 5 | E3.T1 | A: default-ON faithfulness fix + `GE007_FOG_CURVE_FIX=0` hatch; B: `Video.InteriorBackdrop`/`GE007_GAP_OCCLUDERS` default-off, occluder coords Tier A1 |
| **W4.E3.T3** | Harness repair + leak sweep gate | `tools/train_window_backdrop_regression.sh` (recalibrate), `tools/sky_leak_sweep.sh` (new) | Replace blue metric with TINT_SKY magenta count; per-level leak-pad table; wire into local preflight | Old false-alarm case (pad 74) passes; seeded regression fails (`GE007_FOG_CURVE_FIX=0` for Track A, or backdrop/occluder flag off for Track B — there is no `GE007_NO_SKY` env); sweep documented in INSTRUMENTATION.md | 4 | E3.T2 | — |

### Epic W4.E4 — Effects quality pass (26 jd)

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| **W4.E4.T1** | Effects token inventory + shipped-fix visual pass | none (dump/route work), `docs/STATUS.md` | Effects-heavy scripted Dam route with `GE007_DUMP_SETTEX_TEXTURES='*' GE007_DUMP_SETTEX_DIR=/tmp/fx`; classify tokens by DrawClass; record confetti + decal close-up A/Bs (§4.4.4); list `effect_image_from_handle` (non-settex) effects as pack-ineligible | Inventory table (token → effect → pack-eligible) committed as doc; 2 archived A/B screenshot pairs; confetti clamp A/B shows no over-read artifacts | 4 | — | Dumps local/gitignored (Tier B metadata) |
| **W4.E4.T2** | HD effect-texture pack routing | `tools/texpack/build_pack.py` route config (no engine code) | Feed effect tokens through Real-ESRGAN (detailed art per §3 decision tree); rebuild pack; in-game review of decals/flares/smoke | With `GE007_TEXTURE_PACK` set: bullet-hole decal and muzzle flare visibly sharper at 1440p (archived A/B); without pack: byte-identical; contamination guard green (`scripts/ci/check_no_rom_data.sh`) | 4 | E4.T1; **W2 pack pipeline (exists: `build_pack.py`)** | Tier B, local-only, never committed |
| **W4.E4.T3** | Soft particles — GL | `gfx_opengl.c` (depth copy FBO + `uDepthCopy/uSoftRange` uniforms + FS epilogue in generator `:819-1284` region), `gfx_pc.c` (variant bit at EFFECT-class flush; `Video.SoftParticles` in `platform_sdl.c`) | §4.4.2; one depth blit/frame at first effect draw | Smoke/explosion vs wall: no hard clip line (archived A/B); `Video.SoftParticles=0` byte-identical (compare_screenshots 0.000%); ASan clean; no measurable fps hit on jungle route (`tools/perf_census.sh` within ±3%) | 6 | Scene depth (shipped); E4.T1 | `Video.SoftParticles` + `GE007_SOFT_PARTICLES`, default 0 |
| **W4.E4.T4** | Soft particles — Metal parity | `gfx_metal.mm` (blit encoder depth copy, MSL epilogue in generator `:260+`, uniform block `:1140`) | Mirror T3 mechanically; reuse SSAO linearize helpers (`:1271-1276`) | Same A/B under `GE007_RENDERER=metal --remaster`; GL vs Metal screenshot delta within existing backend budget; no GPU hang (the whole point of native Metal) | 4 | E4.T3; **W3 Metal backend (shipped)** | Both-generators rule satisfied |
| **W4.E4.T5** | Render-only density/scale knobs | `unk_0A1DA0.c` (sparks), `gun.c` (muzzle scale), preset table | §4.4.3 env-float multipliers, identity defaults; explicitly do NOT touch EXPLOSION/SMOKE buffer lens | Identity run 0.000%; `tools/sim_invariance_gate.sh` green with knobs maxed (proves render-only); knobs documented in VISUAL_MODES.md | 4 | — | R1 proof via invariance gate mandatory; R3 identity defaults |
| **W4.E4.T6** | Water-impact inventory + verdict | `explosions.c` reading (`g_ImpactTypes:826`, `unk1==2` class `:3209`) | Trace which impact types fire on water surfaces (Dam/Frigate); decide fix-or-no-gap; if dated, route texture via E4.T2 | Written verdict with screenshots; any fix lands under an existing epic pattern | 4 | E4.T1 | — |

### Epic W4.E5 — Residual triage & aspect QA (8 jd)

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| **W4.E5.T1** | Decomp-gap inventory script + triage table | `tools/decomp_gap_inventory.sh` (new), `docs/STATUS.md` | §4.5; wrap greps + `check_native_stub_surface.py` (exits non-zero on failure); document `chr.c:1979` stan fallback as accepted-workaround | Script exits 0 on HEAD; seeded `PORT_TODO` fails it; triage table has 0 open action rows | 3 | — | — |
| **W4.E5.T2** | Ultrawide + split-screen aspect sweep | none (QA) → fixes in `player.c`/`bondview.c` pattern if found | Run 5 repro scenes at 21:9/32:9, 1P+2P; check squeeze-class invariant | Sweep log archived; each finding either closed as shape-correct (with math) or fixed with `GE007_NO_*_FIX` hatch + screenshot gate | 5 | — | Any fix default-on faithful-shape class, per prior 5 fixes |

**Total: 78 junior-days.**

---

## 6. Milestones & deliverables

| # | Milestone | Contents | Demo script (reviewer runs) | Est |
|---|---|---|---|---|
| **M1 — Lights out** | E1 complete: population gap fixed, verified, **default ON** | `./build/ge007 --level bunker1 --deterministic` (raw LEVELID 9; note `--level 25` is the Train) → shoot the first ceiling lamp in Bunker; watch it dim + debris; leave/re-enter the room — still dark | 2.6 jw |
| **M2 — No sky bleed** | E3 complete: fog fork decided + fixed, sweep gate green | `GE007_TINT_SKY=1 GE007_AUTO_FORCE_PLAYER_SCRIPT="40-120:-6391.6:333.76:-319.58:270:-4:167.31:24" ./build/ge007 --level train --deterministic --screenshot-frame 100 --screenshot-exit` → zero magenta pixels (or N64-matched darkness) | 2.8 jw |
| **M3 — Glass signed off** | E2 complete: presentation fixed or thesis closed, tunables + preset, gates promoted | `GE007_AUTO_DAMAGE_TAG=19 GE007_AUTO_DAMAGE_TAG_FRAME=80 GE007_AUTO_DAMAGE_TAG_AMOUNT=80 GE007_AUTO_WARP_PAD=100 GE007_AUTO_WARP_FRAME=50 ./build/ge007 --level dam --remaster --deterministic --screenshot-frame 110 --screenshot-exit` | 3.4 jw |
| **M4 — Effects uplift** | E4 complete: HD effect pack route, soft particles GL+Metal, density knobs | `GE007_TEXTURE_PACK=~/ge007_hd ./build/ge007 --level dam --remaster --config-override Video.SoftParticles=1` → grenade a wall: sharp decals, no particle clip lines | 5.2 jw |
| **M5 — Zero-flaw certification** | E5 + ledger: inventory script in preflight, aspect sweep archived, STATUS.md flaw table empty | `tools/decomp_gap_inventory.sh && tools/sky_leak_sweep.sh && tools/dam_visual_regression_suite.sh && tools/playability_smoke.sh --all` — all green | 1.6 jw |

---

## 7. Risks & mitigations (ranked)

| # | Risk | Mitigation | Kill / de-scope criterion |
|---|---|---|---|
| 1 | **Shoot-out-lights darkens the wrong surface** (vertex-base bias, plan §8.1) — worst case visually corrupts rooms | E1.T2 assertion BEFORE any default flip; one-sided bias fix; the hit-record already carries absolute `Vertex*` (`vtx0/vtx1/vtx2` filled by `bgPopulateHitRecord`, `bg.c:214-231`, from `bg.c:11543-11545`) as a fallback signature change for the initial triangle | If bases provably diverge inconsistently per-room after 3 jd of E1.T2, ship default-OFF as a documented experimental flag and re-scope E1.T4/T5 out |
| 2 | **Glass pixel parity is unreachable** (actor composition proven dirty; 744 checkpoint pairs, 0 strict matches) | The WIP doc's stop-rule is codified: E2.T1 is the LAST parity probe; E2.T3 redefines done as "looks right + geometry gates" | Automatic: T1 finds no state divergence → close thesis, proceed to T3 (this is the *expected* path) |
| 3 | **Fog fix regresses other levels** (Track A touches the faithful path on all 20 levels) | Fog verdict is numbers-first (E3.T1); full 20-level sweep + stock comparison + `GE007_FOG_CURVE_FIX=0` hatch; the RAMROM sim gate proves fog is render-only | If port fog proves faithful (fork ii), Track A is dead by measurement → Track B opt-in backdrop, faithful path untouched |
| 4 | **Soft-particle depth copy costs perf or breaks Metal** (extra blit + shader variant explosion) | One blit/frame only when an effect draws; variant bit doubles only EFFECT-class shader count (small); perf_census ±3% budget gate; W3's Metal snapshot plumbing is proven | >3% fps regression on jungle route or any Metal validation error → ship GL-only behind the flag, defer Metal to W3 follow-up; >2 jd of Metal debugging → same |
| 5 | **ares Train oracle is a time sink** (new territory: EEPROM seed, menu-nav, timeline) | It is *optional* — the fog-equation comparison alone can decide the fork; timebox 2 jd inside E3.T1 | ares route not working in 2 jd → decide on equation-comparison evidence only |
| 6 | **Occluder patches drift into asset territory** (Tier question) | Only hand-measured coordinate tables in first-party C (facts, not copied data); no ROM geometry bytes; reviewed against R2 in PR | Reviewer disagreement on tier → use the interior-backdrop option only (a solid color; unambiguously A1) |

---

## 8. Validation strategy

Every task follows roadmap §7. The exact lanes for this workstream:

```bash
# 1. Build
cmake -S . -B build && cmake --build build --parallel

# 2. Identity control (per feature flag F) — REQUIRED before any merge
SDL_AUDIODRIVER=dummy GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
  ./build/ge007 --level 33 --deterministic --screenshot-frame 80 --screenshot-exit   # default
<F=identity> ./build/ge007 --level 33 --deterministic --screenshot-frame 80 --screenshot-exit
python3 tools/compare_screenshots.py default.bmp identity.bmp        # expect 0.000% changed

# 3. Feature A/B on the exercising level (budget per task table; archive locally, never commit)
python3 tools/compare_screenshots.py off.bmp on.bmp --max-changed-pct <task budget>
python3 tools/audit_screenshot_health.py on.bmp && python3 tools/audit_render_trace.py trace.jsonl

# 4. R1 sim invariance — mandatory for E1 (table writes), E3 Track A (fog), E4.T5 (knobs maxed)
tools/sim_invariance_gate.sh                       # OFF vs ON end-state hash identical
python3 tools/compare_state.py <bootstrap jsonl A> <B>
scripts/ci/check_sim_render_separation.sh && scripts/ci/check_timing_lock.sh

# 5. Memory safety — mandatory for E1 (DL walks + cn writes), E2 (shard pool), E4 (new GPU paths)
tools/asan_smoke.sh --gate

# 6. Workstream gates
tools/shoot_out_lights_regression.sh               # new, E1.T3
tools/sky_leak_sweep.sh                            # new, E3.T3 (magenta==0 per leak pad)
tools/dam_visual_regression_suite.sh               # umbrella incl. promoted glass geometry gates
tools/playability_smoke.sh --all                   # broad behavioral gate

# 7. R2 contamination — every commit (no tracked images; packs/dumps stay local)
scripts/ci/check_no_rom_data.sh

# 8. Metal parity lane (E4.T4): repeat lane 3 under GE007_RENDERER=metal --remaster
```

Special rules: E3 Track A (fog, faithful-path) additionally requires the stock-N64 far-depth luma comparison from E3.T1 as its acceptance oracle, plus a 20-level screenshot sweep vs pre-change HEAD with per-level budgets (expected change concentrated at far depth). Screenshot references are always local artifacts (R2) — gates compare against freshly captured HEAD baselines, not committed images.

---

## 9. Open questions (need the user)

1. **Shoot-out-lights in `--faithful`:** we recommend default-ON *including* faithful mode (it restores original N64 behavior, class C3 like the portal fixes) — confirm you agree it is a bug-fix, not a remaster feature. If not, we pin it in `s_faithfulPreset` instead (1-line change either way).
2. **Train fork ii fallback taste call:** if measurement proves port fog faithful, do you prefer the interior-backdrop darkening (invisible-when-correct, changes sky rendering for interior rooms) or hand-authored occluder quads (surgical, more authoring)? Design supports both; T2b defaults to backdrop-first.
3. **Glass "done" definition:** after E2.T1's last sanctioned probe, are you comfortable ratifying "geometry gates + looks right at 4K" as done, even though 240p stock pixel parity stays formally open? (The WIP doc's evidence says strict parity is actor-composition-blocked.)
4. **Effect-density preset values:** the remaster preset numbers for shards/sparks/muzzle scale are aesthetic; we will propose defaults with archived A/Bs, but final taste sign-off is yours (30-minute review session per M3/M4).
