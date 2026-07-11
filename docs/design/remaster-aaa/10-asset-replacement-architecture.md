# W10 — Asset-Replacement Architecture (1:1 swaps over the untouched sim)

**The game-wide architecture for replacing what the player *sees* — textures, set
dressing, props, characters, and eventually rooms — with true HD assets, while the
1997 simulation underneath runs byte-identical.** This is the program doc that turns
the Surface 1 showcase ([09](09-surface-showcase.md)) from a one-level experiment into
a coherent, repeatable system a junior engineer can execute level by level.

> **Verification date: 2026-07-11.** Every `file:line` anchor in §2 and §4 was
> re-verified against `main` on that date (two dedicated code-recon passes + spot
> checks). Anchors drift — re-verify before use, per house rules.
> **Authority chain:** [REMASTER_ROADMAP.md](../REMASTER_ROADMAP.md) §1 rails are
> non-negotiable. [02-hd-asset-pipeline.md](02-hd-asset-pipeline.md) owns the texture
> factory; this doc owns everything with *geometry*. External-precedent and
> license claims in §3/§6 come from a sourced research pass (Halo CEA/KEX/RT64/
> Ship of Harkinian/RTX Remix; Poly Haven/ambientCG/NASA et al.), summarized inline.

---

## 0. The verdict — read this first

**No feedback loop into the simulation is required. 1:1 visual swaps are the
architecture, for every replacement class.** Two independent recon passes confirmed
the load-bearing fact: everything the sim "knows" about geometry, it reads either
from the **original model/room bytes left untouched in memory** (bullet hit-scan
parses the retail display-list opcodes directly) or from **data the animation/portal
systems compute before rendering runs** (joint matrices, room visibility). A
render-time substitution that draws a different mesh under the same matrices is
therefore *invisible* to the sim — provided three invariants hold:

1. **Never repoint sim-shared data.** Room `ptr_point_index` / DL pointers feed
   three subsystems (render, bullet-wall collision, shoot-out-lights). Prop model
   bytes feed hit-scan. Replacements are **additive draw-substitutions** at the
   emit site; the retail data stays loaded and untouched (§2.1, §2.3).
2. **Never derive sim-visible state from new meshes.** `room_rendered` is a
   portal-BFS decision the sim consumes for auto-aim, hit eligibility, AI branches,
   and guard locomotion — computed *before* any draw. New culling/LOD systems for
   replacement meshes must be render-local (§2.2).
3. **Respect the player-perception contract.** The one "feedback loop" that is
   real is the player: silhouettes must match the untouched collision, retail
   darkness must stay dark, and added detail must not occlude gameplay sightlines.
   Halo CE Anniversary shipped the best dual-render remaster ever made and its
   *only* enduring criticisms are exactly these three failures (§2.5).

Everything else in this doc is machinery for doing that beautifully, fast, and
repeatably across 20 levels.

---

## 1. The five replacement classes

| Class | Replaces | Keyed by | Status | Sim contact |
|---|---|---|---|---|
| **R-T** texture swap | Static settex textures | settex token (`tok####`) | ✅ SHIPPED (loader + texpack + `cc0_import.py`) | Zero — loader keeps native tile dims, UVs unchanged |
| **R-A** additive decor | Nothing (adds set dressing) | per-level manifest placement | ✅ SHIPPED (`Video.SceneDecor`, M7/M8) | Zero — sim never learns decor exists; placement doctrine keeps it out of play (§2.5) |
| **R-P** prop visual swap | Prop/object/pickup meshes | `PROP_*` enum / model `debugName` | 📐 **SPEC'D HERE — build next** | Zero by construction (§2.1) |
| **R-C** character + viewmodel swap | Guard/Bond limb meshes | model `debugName` + limb node index | 📐 spec'd, gated on R-P landing | Zero by construction — limbs ride existing `render_pos[]` (§2.1, §4.2) |
| **R-W** room visual swap | Room opaque/XLU geometry | room id per level | 🔬 spike-verdict only (breaches roadmap §8 "no mesh re-topology" — needs written spike + sign-off) | Zero **iff** invariants 1–2 hold; lights-out needs a parallel path (§2.3) |

**R-T** and **R-A** are production systems with their own docs (02, 09 §6.5–6.6).
This doc specifies **R-P** fully (§4, §7), **R-C** as its direct extension, and
**R-W** as a bounded spike.

---

## 2. The sim-coupling ledger (verified 2026-07-11)

This section is the answer to "do we need a feedback loop, or are swaps 1:1?" —
enumerated, cited, and closed. Treat it as the safety datasheet for all five classes.

### 2.1 What the sim reads from prop model data — and why render-only swaps are invisible

| Sim system | What it actually reads | Anchor | Affected by a render-only mesh swap? |
|---|---|---|---|
| Shot/hit detection | The **retail model DL bytes + vertex buffer, parsed directly** (`G_VTX`/`G_TRI1` opcodes → `intersectLineTriangle`) | `bgTestHitOnObj` `src/game/chrobjhandler.c:35497` (opcode parse ~35588–35700); node walk `sub_GAME_7F04D9B0` :36647 | **No** — those bytes stay in memory untouched |
| Prop movement collision | `ModelRoData_BoundingBoxRecord` — author-placed bbox metadata, a *separate node type* (`MODELNODE_OPCODE_BBOX`) | `chrobjGetBboxFromObjFile` `chrobjhandler.c:2605`; consumed :2917 | **No** — separate record, untouched |
| Player/guard movement + AI LOS | STAN navmesh tiles — no relationship to model files at all | `stanTestLineUnobstructed` `src/game/stan.c:3852`; `chrCanSeeBond` → `src/game/chrlv.c:5430` | **No** — unrelated system |
| Auto-aim magnet point | Translation components of `model->render_pos[0]`/`[1]` — **animation output**, computed at tick time before rendering | `chr.c:10776-10784` via `chrpropUpdateAutoaimTarget` `chrprop.c:6607` | **No** — produced pre-render, independent of drawn mesh |

The same holds for characters and the Bond viewmodel: models are **segmented, not
skinned** (rigid geometry per node, one matrix per node from `model->render_pos[]`,
filled by the tick-time animation pass `modelUpdateMatrices`, `model.c:5933`).
Substituting a rigid HD mesh per limb under the same matrices leaves animation,
hit-scan, and aim untouched.

### 2.2 The one true render→sim loop: `room_rendered` (FID-0012)

`g_BgRoomInfo[room].room_rendered` is written by the **portal-visibility BFS**
(`bgRoomVisibilityRelated`, `src/game/bg.c:6461`) and read back by the sim — this is
the project's known, ledgered render→sim coupling, and it gates far more than aim:

- **Auto-aim eligibility** — `chr.c:5291` (`getROOMID_isRendered`) → `PROPFLAG_ONSCREEN`
  → `g_OnScreenPropList` → `chrpropUpdateAutoaimTarget` (`chrprop.c:6628`).
- **Player gunfire hit-scan scope** — `chrprop.c:2818`: a guard in a non-rendered
  room *cannot be hit by player bullets*. Melee same (`chrprop.c:4816`).
- **AI script branches** — `AI_IFImOnScreen`/`AI_IFMyRoomIsOnScreen` (`chrai.c:2735-2775`).
- **Guard locomotion mode** — `chrlv.c:11348`: non-rendered room → patrol teleport
  ("magic travel") instead of walked animation.

**Why swaps can't break it:** the BFS runs and the sim consumes the result *before*
`bgLevelRender` emits a single draw (`lvl.c:1718-1792`). Visibility is a function of
**portal geometry**, not of what mesh gets rasterized.
**The law it implies:** replacement meshes must never write `room_rendered`/
`room_neighbor_to_rendered`, and any culling/LOD for HD meshes must be a
render-local decision — never "derive room visibility from the new mesh's occlusion."
**The oracle:** `g_BgRoomInfo` is hashed by the determinism gate precisely because
this read-back is non-waivable (`src/platform/sim_state_hash_registry.c:89-94`,
`bg.c:20481-20497`). Any swap that perturbs visibility timing fails the sim-hash gate
even if pixels look right. Run it on every R-P/R-W change.

### 2.3 The three-consumer invariant on room data (why R-W is "additive draw-substitution or nothing")

Three unrelated subsystems key off the **same** per-room pointers
(`ptr_point_index` Vtx buffer + `ptr_expanded_mapping_info` DL, `src/game/bg.h:18-48`):

1. **Rendering** — `sub_GAME_7F0B677C` sets the BG vertex segment and emits the DL
   (`bg.c:10755-10756`).
2. **Bullet-vs-wall collision** — `bgTestBulletHitBackground` uses per-room collision
   quads (`ptr_unique_collision_points`, `bg.h:41`) built **lazily once from those
   same pointers** at room load (`sub_GAME_7F0B6994`, `bg.c:10875-10995`). This is
   *not* STAN — an easy wrong assumption.
3. **Shoot-out-the-lights** — the fixture scanner records byte spans *into the
   original DL*, and a hit darkens vertices **in place** (`vertex->v.cn[i] >>= 2`,
   `src/game/lightfixture.c:307-330`) with a pointer-indexed redarken table.

**The law:** never repoint these fields. A room visual swap draws its HD mesh *in
addition to the room's slot in the draw walk* (suppressing only the retail
`gSPDisplayList` emission), leaving all three consumers reading the untouched
original data. Consequences a junior must plan for: bullet-wall hits and decal
placement still resolve against retail geometry (keep the HD mesh within the retail
surface, §2.5), and lights-out needs a **parallel implementation** on the HD mesh
(map fixture → HD-mesh region at build time) or shot lights won't visibly darken.

### 2.4 Legitimate sim→render flows a swap must *consume* (one-directional, rail-R1-clean)

A replacement is a strict read-only consumer of: per-node matrices
(`model->render_pos[]` — **can be NULL on any frame** under dyn-pool pressure;
fail-closed skip, `model.c:5939-5941`), door/destruction/pickup state, lights-out
state (§2.3), per-level fog (`fogSetRenderFogColor`, inherited by decor already),
and draw-order position (§4.2). This mirrors the proven precedents: Halo CEA's
Saber renderer consumed proxy transforms from the untouched Blam sim; RTX Remix
states the invariant outright ("replacements alter rendering only; collisions remain
based on the original asset"). Render state never flows back.

### 2.5 The soft loop: the player (readability contracts)

The documented failure modes of the genre's best remaster (Halo CEA) are our
contracts, plus one of our own:

- **C1 — Silhouette ⊆ collision.** Replacement visuals must fit *inside* the retail
  silhouette/collision so cover and shots behave as they look (Halo shipped "shots
  blocked by invisible geometry"; Turok's KEX remaster and Quake's Authentic Models
  both codify "maintain the original silhouette"). Enforce with a per-swap
  silhouette A/B capture (§5.3) and a stated tolerance (±5% screen-space at
  gameplay distances; hero landmarks like the Surface dish may exceed it only
  *outside* playable space).
- **C2 — Retail darkness stays dark.** No remaster grade, added emissive, or bright
  albedo may reveal what retail lighting hid (Halo's bright swamp made the
  flashlight pointless). Our tone-locked import (`cc0_import.py` scalar-mean lock)
  enforces this for textures; R-P/R-W meshes bake to the room's retail brightness.
- **C3 — No new occlusion of gameplay sightlines.** Added decor/forests stay outside
  playable bounds or behind the fog band; the sim can see and shoot through decor it
  doesn't know exists — the player must never be blinded by it.
- **C4 — Legibility of threats and pickups.** Guards, pickups, and the reticle
  target must keep or gain contrast against replaced surfaces (Halo's busier art
  garbled enemy read). Spot-check every level pass at 640×480-equivalent in motion.

---

## 3. Identity & keying doctrine — stable IDs, never content hashes

The research pass across RT64, RTX Remix, Ship of Harkinian, Render96/DynOS, and
N64Recomp mods yields one clear split: projects that **don't own the code** key
replacements on content hashes (RT64: XXH3 over TMEM; Remix: geometry hashes) and
fight endless instability — culling-dependent hashes, palette-variant explosion,
TMEM aliasing, and CPU-skinned meshes whose hash changes every frame (Remix simply
cannot replace them). Projects that **own the code** key on stable symbols
(SoH `ResourceMgr_*ByName`, DynOS `mario_geo`, recomp `RECOMP_PATCH`) and none of
those failure classes exist.

**We own the decomp. Keys, per class:**

| Class | Primary key | Notes |
|---|---|---|
| R-T | settex token `tok####` | Already shipped; the model for everything else |
| R-A | manifest `model`/`place` names | Per-level `.decor.txt` |
| R-P | `PROP_*` enum (`ObjectRecord.obj`, `src/bondconstants.h:2738+`) for per-type swaps; model `debugName` (`ModelFileHeader`, set from asset filename) for per-asset swaps | Both stable across runs/levels |
| R-C | model `debugName` + limb node index (`modelFindNodeMtxIndex`) | Author replacements to the retail skeleton — no bone remap layer, ever (Remix's remap tooling is the cautionary tale) |
| R-W | level slug + room id | Same keys the manifest system already uses |

**Hard rule: never key on raw pointers.** `Model*`/buffer identity is transient —
the Bond viewmodel body historically *aliased the GUNRIGHT weapon buffer*
(`bondview.c:2871-3040`, the fixed intro red-shard bug), and those buffers are still
dynamically reused. A pointer-keyed cache would apply the wrong substitute exactly
where that bug used to live. Content-hash lookup is permitted only as a documented
*fallback* for a shared-buffer case an enum can't disambiguate — the exception,
never the spine.

---

## 4. The render seam — `G_MODERNMESH` contract and the universal hook points

### 4.1 The contract (recap; full spec in 09 §6.6)

`G_MODERNMESH` (0xC1) is the port's one modern-render opcode: flush the pending N64
batch, hand the backend a float32/u32 mesh with mipmapped any-size RGBA8 textures,
drawn into the same color/depth attachments at the exact DL position, transformed by
the interpreter's current MP matrix, with the N64 fog curve replicated. **It is only
legal in PC-native `Gfx` buffers** — the N64-format interpreter ignores it by design
(`gfx_pc.c:23785-23788`, host pointers can't live in ROM-format bytes). Therefore
**all substitution happens at C call sites**, never by patching ROM DL data.

### 4.2 The two universal hook points (this is the whole R-P/R-C design)

Every prop, door, pickup, guard limb, and the Bond viewmodel funnels through exactly
two leaf emitters in `src/game/model.c`:

- `modelRenderNodeDl` (`model.c:10015`) — objects/doors/pickups (DLCOLLISION nodes)
- `modelRenderNodeGundl` (`model.c:9814`) — weapon/gun models (DL nodes)

Both already hold everything a substitution needs: the `model` (→ `debugName` key),
the `node` (→ `modelFindNodeMtxIndex` → which `render_pos[]` camera-space matrix),
and the write cursor `renderdata->gdl` (a PC-native buffer). The swap is:

```
look up (key) in the substitution table
  → miss: emit retail gSPDisplayList as today (zero-cost path)
  → hit:  NULL-check model->render_pos (fail-closed skip — dyn-pool pressure)
          budget-check dynGetFreeGfx (decor pattern, decor_native.c:136-142)
          gSPMatrix(gdl++, render_pos[idx], G_MTX_MODELVIEW|LOAD|NOPUSH|FLOAT_PORT)
          emit G_MODERNMESH word pair → substitute mesh
          skip the retail gSPDisplayList   ← the mesh bytes stay in memory (§2.1)
```

Because `drawjointlist` walks character slots (body/head/held weapons/hat) through
the same two leaves, **R-C and the viewmodel come free architecturally** — per-limb
rigid meshes on retail matrices — and are gated only by art and readability, not
engineering. The per-prop context channel `gfx_set_prop_context`
(`src/platform/gfx_pc.h:80`) already threads prop identity into the gfx layer for
diagnostics; the substitution decision stays in `model.c` (game side) where the key
and matrix are local, per the decor precedent.

The Bond intro shared-buffer history (§3) applies here: table lookups per frame by
stable key, no caching of resolved `Model*`.

### 4.3 Rendering doctrine — beautiful *and* fast (binds all modern-mesh drawing)

Lessons already paid for (Surface trees at "performance is ass") plus the sourced
foliage/TBDR research. These bind every G_MODERNMESH producer:

1. **Measure at the player's real resolution.** Headless perf gates at RenderScale=1
   said "fine" while Retina RenderScale=2 was unplayable. Every perf acceptance in
   §7 runs at the user-representative config (RenderScale=2, MSAA as shipped,
   ≥8-sample interleaved medians) with the F1 overlay for spot truth.
2. **TBDR draw ordering:** within the modern layer draw opaque → alpha-test → blend.
   `discard` defeats Apple's hidden-surface removal — prefer **alpha-to-coverage
   under MSAA** for cutout foliage (we ship MSAA + A2C already, FID-0018), keep
   `[[early_fragment_tests]]` where legal, and alpha-*blend* only the farthest cards.
3. **Coverage-preserving alpha mipmaps + `fwidth` alpha sharpen** — kills the
   "triangular dissolve" of needle cards at distance. Build-time mip rescale per
   level; shader-side sharpen is two lines of MSL.
4. **Hardware instancing:** one draw per (model, LOD) with a per-instance transform
   buffer — 45 tree instances must not be 135 draw calls. Extend `GfxModernMesh`
   with an instance array; PSO unchanged.
5. **CPU frustum + distance culling, render-local** (§2.2 law). At our scale
   (≤ a few hundred instances/level) GPU-driven culling is overkill.
6. **LOD:** full mesh near → crossed-card far; fog does the last 20%. Octahedral
   imposters were evaluated and killed — cross-cards suffice through our fog.
7. **Lighting:** use `g_pc_sun_dir_world` (`gfx_uniforms.h:59`) — never a hardcoded
   sun (a known bug in the current generators). Foliage: two-sided wrap diffuse
   `saturate(dot(-N,L)*0.6+0.4)`, backlit translucency, hemisphere ambient
   (GPU Gems 3 ch.16 model). Hard surface: lambert + hemisphere ambient. AO: baked
   into vertex color; the snow channel stays `COLOR_0.a`. **No shadow-map casting
   from decor** (blob + SSAO ground it — SSAO samples scene depth, and decor writes
   depth, so grounding is free); **no sRGB/HDR/PBR pipeline change** — LDR cohesion
   with the retail frame *is* the vibe.
8. **Textures:** RGBA8 + full mip chains now; a BCn/compressed pack container is a
   later iteration (precedent: RT64 ships DDS/BC7 ≈ 4× VRAM savings — don't ship
   loose PNGs at scale).

---

## 5. Scaffolding — the machine that keeps 20 levels coherent

### 5.1 Per-level remaster content spec (one directory, three manifests, one loader family)

```
tools/texpack/overrides/<slug>.json     R-T   curated texture routes (shipped)
assets/decor/<slug>.decor.txt           R-A   model/place set-dressing (shipped)
assets/decor/<slug>.props.txt           R-P   substitution table (new, §7 P1):
    swap  PROP_SATDISH        satdish_hd.glb        # per-type
    swap  model:ammo_crate1   crate_hd.glb  cutout  # per-debugName
```

Rules that keep it from becoming a jumbled mess: manifests are committable text
(diffable, reviewable); **exactly one manifest may claim a key** — the loader
hard-fails on duplicate claims (the recomp-mod conflict-detection lesson);
deterministic load order; every class behind its own default-off flag
(`Video.SceneDecor` today; `Video.PropSwap` next) with all-off byte-identical.

### 5.2 Asset registry & provenance (already proven; now the norm)

Every imported asset carries a `*.provenance.json` (source URL, license, transform
args, verbatim rebuild command); `build_models.sh` regenerates every binary from a
clean checkout (fetch-on-demand cache; repo stays binary-free); license tiers
**A1** (CC0/PD/first-party — distributable), **A2** (CC-BY — NOTICE required),
**B** (ROM-derived — local only, never committed); the contamination guard enforces
on every commit. New in this program: a **NOTICE generator** that walks provenance
files → single credits file per release (no precedent project does this well; for
us it's both hygiene and legal necessity).

### 5.3 The validation ladder (per class; run before any merge)

1. **Identity:** all flags off → byte-identical capture, both backends.
2. **Sim purity:** `tools/sim_invariance_gate.sh` + the sim-state hash (which
   already covers `g_BgRoomInfo` — the §2.2 oracle) with the feature ON.
3. **Boot smokes:** the touched level + 3 spot levels, `--no-ui`, muted audio.
4. **Perf:** ≥8-sample interleaved medians at RenderScale=2 vs a pre-change control
   build; per-level frame budget stated in the PR.
5. **Silhouette A/B (R-P/R-C/R-W only):** paired screenshots retail-vs-swap from 3
   gameplay vantages; C1 tolerance stated; taste checkpoint (◆) for hero props.
6. **Manifest lint:** schema + duplicate-key + missing-file + license-tier check
   (new tool, §7 P1.T4).
7. **Tool tests:** `tools/decor`/`tools/texpack` suites stay green.

### 5.4 The A/B toggle is the regression oracle

Halo CEA's classic/remastered switch began as a dev tool and shipped as its most
loved feature. Ours already exists piecewise (per-class flags + `GE007_*` A/B
knobs); keep every new system A/B-able at runtime granularity, and treat "flip it
off mid-mission and compare" as the standing review gesture for C1–C4.

---

## 6. Asset doctrine — what we actually use

License-verified 2026-07-11 (every page opened; aggregator labels distrusted).
Full per-category tables live in the research record; this is the operative core.

### 6.1 Source reliability ladder

| Trust | Sources | Rule |
|---|---|---|
| Bulk-pull safe (first-party CC0) | Poly Haven, ambientCG, Kenney, Quaternius | Site-wide CC0 statements verified |
| High | NASA 3D Resources (PD), cgbookcase (CC0, ≤4K res) | Fine per-asset |
| Verify per asset | poly.pizza, OpenGameArt, itch.io | Read the license box, not the title |
| Never trust labels | Sketchfab, Free3D, TurboSquid-free | A "CC0"-titled Sketchfab antenna is actually CC-BY — title lies |
| Prohibited | textures.com, sharetextures.com (fake "CC0" w/ no-redistribution clause), threedscans | — |

### 6.2 The buy list (fetch first — max impact on Surface + Dam; all tier A1)

1. **NASA DSN 34-m antenna** (glTF, public domain) — Surface's hero dish, a real
   parabolic DSN antenna; grounded late-90s installation exactly. (Don't imply NASA
   endorsement.)
2. **ambientCG Snow006** (stomped footpath) + **Snow002** (clean) — the entire
   Surface ground read.
3. **Poly Haven wood_planks / weathered_planks / wood_planks_grey** — cabin walls,
   floors, roofs (the "only thing that looked really good" — lean into it).
4. **Poly Haven Ammo Box** — olive stenciled ammo can; the signature pickup prop.
5. **Poly Haven Barrel 01 (+03)** — rusty steel drums, 5 texture variants.
6. **Poly Haven concrete_wall_009 + cracked_concrete_wall** — Dam faces, Bunker.
7. **Poly Haven Modular Industrial Pipes 01** — Dam/Facility industrial language.
8. **Poly Haven Modular Chainlink Fence** — Surface/Runway/Dam perimeters.
9. **Poly Haven Boulder 01 + Rock Face 02** — snowy mountainside containment.
10. **Belfast Sunset Pure-Sky + Snowy Hillside HDRIs** — repaint source for the
    arctic-dusk LDR skybox (engine-side sky work is separate, 09 §6).

Vegetation stays **generate-led** (friggog/tree-gen headless Blender + ambientCG
PineNeedles001 needle atlas + Poly Haven bark; heroes = re-decimated photoscans
after the decimator's UV-seam fix, §7 P2.T3). Dead logs/stumps: Poly Haven
tree-stump/dead-trunk set. **Named gaps needing in-house or generated art:**
realistic server rack/console, pole floodlight, watchtower, pallet, jerry
can/cylinders, bare standing winter trees (tree-gen leafless config).

### 6.3 Weapons — the verdict

Legal research (not legal advice): *AM General v. Activision* (2020) protects
"military verisimilitude" under *Rogers*; *Jack Daniel's v. VIP* (2023) narrowed
*Rogers* only for source-identifying uses — an in-world AK-shaped gun named "KF7"
is neither. Rare fictionalized the names in 1997 for this exact reason; GoldenEye:
Source rebuilt all gun art from scratch under fictional names. **The only real red
line is redistributing ROM-derived meshes — which tier B already forbids.**
Plan: CC0 hobbyist models as blockouts now (quality is placeholder-grade at best);
**from-scratch hero viewmodel weapons as a later art milestone**; fictional names
everywhere including filenames.

### 6.4 Vibe doctrine (what keeps it GoldenEye)

Muted, low-frequency albedo (the tone-lock pipeline enforces); silhouette-first
composition — jagged near-black treeline, pale blue snow, pink-purple sky; detail
lives in texture and lighting response, **not** in silhouette-inflating geometry
(the Turok discipline — which is also the poly-budget discipline); every asset
auditioned at 640×480-equivalent in sunset fog before curation accepts it. LDR
cohesion with the retail frame is the aesthetic, not a limitation to engineer away.

---

## 7. Milestones & tasks (junior-executable)

Estimates in junior-days. Every task lands as its own PR against §5.3.
◆ = taste checkpoint (human review of captures).

### Track F — forest/R-A quality ("Eureka"; fixes the shipped trees)

| ID | Task | Files | Acceptance | Est |
|---|---|---|---|---|
| F0.T1 | Coverage-preserving alpha mips + `fwidth` sharpen for modern-mesh cutout textures | `gfx_metal.mm`, `tools/decor/*` | Needle cards hold density at 40 m in capture; no triangular dissolve ◆ | 2 |
| F0.T2 | CPU frustum + distance cull for decor instances (render-local, §2.2 law) | `decor_native.c` | Instances behind camera emit 0 commands (`GE007_TRACE_DECOR`); sim-hash green | 2 |
| F0.T3 | Hardware instancing: per-instance transform buffer in `GfxModernMesh`; one draw per (model, LOD) | `gfx_rendering_api.h`, `gfx_metal.mm`, `decor_native.c` | Surface decor ≤ 20 draws (Metal capture); perf gate §5.3.4 improves ≥ 30% | 4 |
| F0.T4 | A2C cutout path (replace discard when MSAA ≥ 2) + opaque→cutout→blend ordering | `gfx_metal.mm` | GPU frame time at RenderScale=2 recorded before/after; no halo artifacts ◆ | 3 |
| F0.T5 | Real sun: generators + shaders read `g_pc_sun_dir_world`, drop hardcoded vectors | `gen_tree3d.py`, `decimate_gltf.py`, `gfx_metal.mm` | Bake direction matches level sun in A/B capture | 1 |
| F1 | Foliage lighting: wrap diffuse + backlit translucency + hemisphere ambient in the modern-mesh shader | `gfx_metal.mm` | Trees respond to view/sun direction ◆; perf budget held | 3 |
| F2 | 4-frequency triangle-wave wind (vertex shader; phase from world pos) | `gfx_metal.mm` | Motion visible, no root sliding ◆; identity-off unchanged | 2 |
| F3 | Grounding: snow-skirt ring + AO band per tree; SSAO-on validation pass | `gen_tree3d.py`, manifest | No floating silhouettes at spawn vantage ◆ | 2 |
| F4 | Density: tree-gen pipeline (needle atlas + bark), scatter zones in manifest, cross-card far LOD; retire painted treeline where forest stands | `tools/decor/*`, `assets/decor/surface1.decor.txt` | Boundary forest reads dense at all playable vantages ◆; perf gate at RenderScale=2 ≥ 60 fps | 8 |

### Track P — prop visual swaps (R-P)

| ID | Task | Files | Acceptance | Est |
|---|---|---|---|---|
| P0 | Recon | — | **DONE — §2/§4 of this doc** | — |
| P1.T1 | Substitution table + loader: `assets/decor/<slug>.props.txt`, keys per §3, `Video.PropSwap` flag (default 0) | `decor_assets.c`, `platform_sdl.c` | Flag off byte-identical; duplicate key hard-fails with message | 3 |
| P1.T2 | Hook `modelRenderNodeDl`/`modelRenderNodeGundl` per §4.2 (lookup → matrix → `G_MODERNMESH` → skip retail DL; NULL/budget fail-closed) | `model.c`, `decor_assets.c` | Sim-hash green with swap ON; hit-scan A/B: shoot swapped prop, identical damage/decals to retail | 4 |
| P1.T3 | **Proof prop: the Surface satellite dish** (`PROP_SATDISH` → decimated NASA DSN 34-m, snow channel baked) | `build_models.sh`, manifest | Silhouette A/B within C1 tolerance ◆; 20-level boot smoke | 3 |
| P1.T4 | Manifest linter (schema/dup/missing/license-tier) + ctest | `tools/decor/lint_manifests.py` | Fails on each seeded error class; wired into §5.3.6 | 2 |
| P2.T1 | Crate/barrel/ammo-box set on Surface + Dam (buy list #4–5, silhouette-fit decimation) | assets + manifests | C1 A/B per prop ◆; perf budget held | 4 |
| P2.T2 | Destructible/state audit: verify swapped props honor destruction/pickup state transitions (glass, explosions) | test evidence | Shoot/explode/collect parity capture vs retail | 2 |
| P2.T3 | Decimator UV-seam fix (cluster key = position **+ UV bucket**, `decimate_gltf.py:58`) + rebuild scans | `tools/decor/decimate_gltf.py` | Seam-smear gone on fir/boulder captures ◆; tool tests | 2 |
| P3 | Surface full prop pass + NOTICE generator + doc 09 ledger update | — | All §5.3 green; ◆ level review | 5 |
| P4 | Viewmodel spike (R-C): one held-weapon limb swapped via the same hook; readability verdict | `model.c` evidence | Written verdict: ship path or defer; no pointer-keyed cache (§3) | 3 |

### Track W — room visual swap (R-W): spike only

One time-boxed spike (5 jd): substitute ONE Surface cabin's opaque DL at the
`sub_GAME_7F0B677C` seam per §2.3 laws; measure decal alignment, lights-out
behavior, XLU-pass interaction. **Exit = a written verdict** (ship-path / defer)
reviewed against roadmap §8 — not code on main.

**Order of execution: F0 → P1 → F1–F3 → P2 → F4 → P3 → P4 → W-spike.** F0 first
because it repairs the shipped experience; P1 next because it unlocks the hero dish
and every level's props with one mechanism.

---

## 8. Risks & kill criteria

| # | Risk | Mitigation / kill |
|---|---|---|
| 1 | Hook in `model.c` hot path regresses faithful perf | Miss path = one table lookup behind a flag-check; measure retail perf pre/post; kill = any measurable faithful-mode cost |
| 2 | Silhouette drift accumulates prop by prop (Halo failure) | C1 A/B is per-swap acceptance, not a final audit; hero exceptions documented in-manifest |
| 3 | Instancing/A2C churn destabilizes Metal backend | Each F0 task lands separately behind flags; GL parity capture per PR; kill = 2 failed fix attempts → revert that task, keep the rest |
| 4 | Asset licensing regression (a "CC0" that isn't) | Only §6.1 bulk-safe sources without per-asset review; everything else verified on-page in provenance.json; linter checks tier field |
| 5 | Scope bleed into sim "to make collision match visuals" | Forbidden by §0; the fix direction is always visuals→fit-collision, never collision→fit-visuals |
| 6 | Per-frame table lookups misapply on aliased viewmodel buffers | §3 hard rule (stable keys, no pointer caching); P4 spike validates before any R-C ships |

## 9. Validation doctrine

Every PR in this workstream runs §5.3 in full. Additionally: any change touching
`model.c`, `bg.c`, or `chr*.c` paths runs the sim-state hash across a 20-level
deterministic sweep (the FID-0012 oracle); any change touching `gfx_metal.mm` runs
the GL/Metal parity capture; any manifest change runs the linter + boot smoke of
the touched level. Perf claims are only valid at RenderScale=2 with ≥8-sample
interleaved medians — single-sample numbers are noise and have already misled us
once.

---

*Assembled 2026-07-11 from two dedicated code-recon passes (prop pipeline; room/sim
separation — every §2/§4 anchor spot-verified on `main`), a sourced external-precedent
study (Halo CEA, Nightdive KEX, RT64, Ship of Harkinian, Render96/DynOS, N64Recomp,
RTX Remix), and a license-verified asset survey. The Surface showcase (doc 09) is the
proving ground; this doc is the machine that scales it.*
