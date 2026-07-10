# W1 — Modern Lighting & Materials (Track 1 completion + Track 2 per-pixel)

**Workstream lead doc — MGB64 AAA Remaster program.**
Parent plan: [REMASTER_ROADMAP.md](../REMASTER_ROADMAP.md) §4 (this workstream implements
T1.3, T1.4, and Track 2 T2.1–T2.5, plus a bounded dynamic-light assessment).
Branch of record: `main` (merge train complete, tag v0.2.0-rc1; W1.E1 merged 2932033,
W1.E2 merged 987d48b). Where anchors differ from the roadmap's older anchors,
**this doc's numbers win** (the roadmap says its anchors drift; they have).
Anchors in §2, §4.3–§4.9, and §5 (E3–E7) re-verified against `main` @ 096249c on
2026-07-03 (post-W1.E1/E2 + W2.E1 insertions).
**Warning:** anchors in §4.0–§4.2, the §5 E1/E2 task tables, and §6–§9 were NOT
re-verified in that pass and may still carry pre-E1/E2 drift — re-verify before use.

---

## 1. Executive summary

This workstream takes MGB64's lighting from "baked 1997 vertex colors + screen-space AO"
to real light: smooth-shaded environment normals (fixing the Dam rock-facet seams and
ground banding that texture replacement provably cannot fix — roadmap §3), a real sun
shadow map replacing the flat blob shadow, per-pixel directional shading, and
normal/roughness material maps that finally give the HD texture pipeline (§3) a lighting
model to plug into. Everything is Metal-first but dual-backend (every shader change lands
in **both** structurally-parallel generators: `gfx_opengl.c` GLSL + `gfx_metal.mm` MSL),
everything is flagged and default-off (rail R3), nothing touches the sim (rail R1), and
no asset beyond Tier-A1/A2 is ever committed (rail R2).

| # | Headline deliverable | Player-visible payoff |
|---|---|---|
| 1 | **Smooth env normals** (T1.3, CPU relight, no VBO change) — **DONE on main** (E1, default-off; ◆ T3 blend-look review still pending) | Dam cliffs stop reading as "tiger-striped facets"; ground banding gone |
| 2 | **World-space VBO plumbing** (T2.1) — **DONE on main** (E2, merged 987d48b, T1–T4; identity-off byte-identical both backends, R1 sim-invariant, 8-agent review clean) | Invisible alone — the gate that unlocks 3–5 |
| 3 | **Sun shadow map** (T1.4) | Bond and guards cast real shadows; blob quad retired |
| 4 | **Per-pixel lighting + normal/roughness sidecars** (T2.2/T2.3/T2.4) | Surfaces respond to light direction; HD packs gain depth |
| 5 | **Character normals + bounded dynamic lights** (T2.5 + N≤4 forward) | Muzzle flashes light the room; shot-out lamps die visibly |

Total honest estimate: **~65 junior-days remaining** (was ~90 jd across 7 epics;
E1 13 + E2 12 done on main).

---

## 2. Current state (verified, file:line)

### 2.1 Why environment surfaces have no normals

- `bgLevelRender` explicitly clears lighting for all room geometry:
  `gSPClearGeometryMode(arg0++, G_TEXTURE_GEN | G_LIGHTING)` — `src/game/bg.c:7018`
  (comment at `bg.c:7013`: "GE rooms use baked vertex colors, NOT hardware lighting").
  A second clear site exists at `src/game/lvl.c:1432`.
- The N64 `Vtx` is a union — `include/PR/gbi.h:871-875`: `Vtx_t.cn[4]` (color) occupies
  the same bytes as `Vtx_tn.n[3]+a` (normal). For env surfaces those bytes **are** color;
  there are genuinely no normals in the room data.
- The global sun **does** exist: `Lights1 GlobalLight` — `src/game/bg.c:2745` — ambient
  (150,150,150), diffuse (255,255,255), direction `(77,77,46)` ("42° up, from the
  west-south-west"). It is bound every frame (`gSPSetLights1(arg0++, GlobalLight)`,
  `bg.c:7027`) but has no effect on rooms because G_LIGHTING is cleared.

### 2.2 The render pipeline the design must fit (docs/RENDERING_ARCHITECTURE.md §2)

- Software T&L: `gfx_sp_vertex` (`gfx_pc.c:16340`) transforms object-space `ob[3]`
  straight to **clip space** by `rsp.MP_matrix` (`gfx_pc.c:16438-16441`). The lit path
  (G_LIGHTING, used by characters/weapons, e.g. `bondview.c:18704`, `title.c:941`)
  consumes `Vtx_tn.n[]` on the CPU into a vertex **color** at `gfx_pc.c:16623-16687`.
- `struct LoadedVertex` (`gfx_pc.c:2435-2468`) carries clip pos, uv, color, fog,
  `int16 ob[3]` + `room_id` (populated only when tracing — `gfx_pc.c:16583-16593`), and
  debug fields. **It now also carries world position** (`wx/wy/wz`), populated when
  `SHADER_OPT_WORLD_POS` is latched — landed W1.E2.T2. **Still no normal.**
- Matrix state: modelview stack `rsp.modelview_matrix_stack[11][4][4]` (`gfx_pc.c:3387`),
  `P_matrix` (`:3398`), loaded in `gfx_sp_matrix` (`gfx_pc.c:15635`); MP recomputed at
  `gfx_pc.c:15529-15537`. Projection coefficients for SSAO are already captured per frame
  at `gfx_pc.c:15691-15697` into `g_pc_ssao_proj_{a,b,x,y}` (`:15566-15569`) — the
  pattern to copy for any new per-frame matrix capture.
- Triangles are packed into one interleaved CPU array `buf_vbo`
  (`gfx_pc.c:4487`, packing loop `gfx_pc.c:18839-19010`): pos(4) → [diag(6)] →
  [world(3), landed W1.E2] → per-texture uv(2)+clamp/mask(0-4) → [fog(4)] →
  combiner inputs (3 or 4 each).
  `gfx_flush()` (`gfx_pc.c:14194`) hands it to `gfx_rapi->draw_triangles`
  (`gfx_rendering_api.h:42`) — the vtable has **no matrix or lighting hooks**; the
  backend sees only pre-transformed floats.
- Shader identity = `(cc_id, cc_options)`; options assembled per draw at
  `gfx_pc.c:17722-18048`, combiner cached via `gfx_lookup_or_create_color_combiner`
  (`gfx_pc.c:14456`). **Free `SHADER_OPT` bits: 6, 7, 29, 30, 31** (bit 5 is now
  `SHADER_OPT_WORLD_POS`, claimed by W1.E2; `gfx_cc.h:50-76`) — this workstream claims
  the rest of its four (§4.1).
- GLSL generator: `gfx_opengl_create_and_load_new_shader` (`gfx_opengl.c:839-1494`).
  VS is a near-passthrough (`gl_Position = aVtxPos`, `gfx_opengl.c:956`; z×0.3 spread at
  `:963` when no depth clamp). Attribute walk order `gfx_opengl.c:866-918` == VBO pack
  order (incl. the landed `aWorldPos`, `:874-879`); attrib binding `:1385-1443`;
  samplers `uTex0`→unit 0, `uTex1`→unit 1
  (`:1481-1489`), diag framebuffer→unit 2 (`:1490-1495`). **Units 3+ are free.**
- MSL generator: `mtl_generate_msl` (`gfx_metal.mm:272`), a block-for-block mirror.
  Attribute walk via `add_attr` (`gfx_metal.mm:392-450`) fills `MetalShader.attrs[]`
  consumed by `mtl_build_vertex_descriptor` (`:913-930`). Fragment binds
  `uTex0 [[texture(0)]]`, `uTex1 [[texture(1)]]`, diag snapshot `[[texture(2)]]`
  (`gfx_metal.mm:472-476`); uniforms are one 48-byte `Uniforms` at `[[buffer(1)]]`
  (`:299-308`) with a C mirror (`MtlUniforms`, `:733`) — any new uniform must extend
  **both** structs in lockstep.
  Scene targets: BGRA8 color + **sampleable Depth32Float** (`mtl_ensure_targets`,
  `gfx_metal.mm:830-898`); scene encoder open/close `mtl_open_scene_encoder`
  (`:1040-1052`); frame lifecycle `mtl_start_frame` (`:1109-1158`) / `mtl_end_frame`
  (`:1496`); PSO cache keyed on blend|samples|write_alpha (`mtl_pso_for`, `:934-993`).
- GL scene target: `g_scene_fbo` + sampleable `g_scene_depth_tex`
  (`gfx_opengl.c:657-675`, created in `gfx_opengl_ensure_scene_target`, `:2326`). SSAO
  v1 lives in the output filter (GLSL `gfx_opengl.c:3040-3073`; MSL port
  `gfx_metal.mm:1307-1333`), controlled by `Video.Ssao` et al.
  (`platform_sdl.c:205-215`, registered `:1614-1668`).

### 2.3 The pieces this workstream replaces or feeds

- **Blob shadow**: `doshadow` (`src/game/model.c:10958`) draws a flat textured quad at
  ground height, alpha-faded by height, gated by `GE007_DROP_SHADOWS` (`model.c:10977`,
  early-return gate `if (!s_shadows_enabled)` at `model.c:10980`).
- **Shoot-out-the-lights**: real hit-detection/darkening exists behind
  `GE007_SHOOT_OUT_LIGHTS` (default off, `lightfixture.c:110-124`;
  docs/design/SHOOT_OUT_LIGHTS_PLAN.md) — it darkens fixture *surfaces*; there is no light
  entity. Muzzle-flash world position is already computed in
  `portBuildFirstPersonFlashMatrix` (`gun.c:3379`, `flash_world_pos` math `:3397-3410`;
  `portDisableMuzzleFlash` escape hatch at `gun.c:60`).
- **HD texture loader**: `texture_pack_try_load` (`texture_pack.c:64-90`) probes
  `<pack>/textures/tok%04d.png` only; hook at `gfx_pc.c:21525`. No sidecar concept yet.
- **Backend selection**: `GE007_RENDERER=metal` (`gfx_backend.c:16`); GL stays default
  and byte-identical (rail R3); GL is the cross-platform reference for Linux/Windows.

---

## 3. Target state — the AAA bar

What a reviewer sees with `--remaster` (Metal on macOS) after this workstream:

1. **Dam cliff walls read as continuous rock.** No per-quad lighting seams, no
   flat-facet banding on the ground plane. Verified by the exact A/B that killed the
   texture-swap approach (roadmap §3 "tiger stripes").
2. **Bond, guards, and props cast a single coherent sun shadow** that agrees with
   `GlobalLight`'s direction, with soft PCF edges and no acne/peter-panning at
   screenshot distance. The blob quad is gone in remaster mode (still present, untouched,
   in faithful mode).
3. **Surfaces respond to the sun per-pixel**: grazing light shows form on walls and
   floors; with an HD pack that ships `_n`/`_r` sidecars, gravel and concrete show
   texture-scale relief and believable specular restraint (roughness-attenuated).
4. **Firing a gun visibly lights the immediate environment** for the flash duration;
   shooting a lamp (flag on) kills its glow with a spark flicker. Max 4 dynamic lights,
   forward, no shadows — bounded by design.
5. **Faithful mode is untouched**: all-off frame remains byte-identical
   (screenshot-hash gate), sim hash identical ON vs OFF (R1 gate), 60fps+ maintained on
   all 20 levels (perf census budget: remaster ≥ 90 fps where today it is 101–189,
   docs/design/PERFORMANCE_PLAN.md).

Honest ceiling (unchanged from roadmap §2): this is a remaster, not PBR — no authored
light entities, no GI, no deferred pipeline. §8 deferrals stand (see §7 risk R7).

---

## 4. Technical design

### 4.0 Design invariants (apply to every epic)

- **Identity-off**: every new shader feature is a new `SHADER_OPT_*` bit. Bit unset ⇒
  shader key unchanged ⇒ generated source byte-identical ⇒ frame byte-identical. Bits are
  set at the single assembly site (`gfx_pc.c:17282+`) only when (runtime flag) AND
  (draw-class predicate) hold, mirroring how `SHADER_OPT_TEXEL0_CLAMP_S` is gated today.
- **Dual generator**: each feature lands as one GLSL block and one MSL block in the same
  structural position (the generators mirror block-for-block; see the mirroring contract
  comment at `gfx_metal.mm:133-136` and `:259-261`). PR checklist: a diff that touches
  one generator without the other is rejected.
- **Space convention**: all new lighting math is **world space**, recovered per path
  (see the §4.4 CORRECTION — the blanket `(ob·MV)·V⁻¹` rule is wrong for rooms, whose MV
  lacks the filepos offset). Rooms use the game's **exact identity**
  `world = (filepos + ob) * room_data_float2` (`gfx_pc.c:16133-16135`). The
  view-inverse path (`g_pc_view_inv`, §4.2) is kept for the camera position and for
  character/prop verts, whose modelviews *are* clean object→view.
- **Class gating**: env features apply to `DRAWCLASS_ROOM` only (`g_current_draw_class`,
  `gfx_pc.c:451`; ROOM checks at `:1418`, `:3420`, `:5162`); character features to
  CHRPROP; viewmodel (WEAPON) and HUD are always excluded (viewmodel is unlit baked-color
  — roadmap §8 last bullet; HUD/ortho excluded by the perspective test
  `fabsf(rsp.P_matrix[2][3]) > 0.5f`, the exact predicate SSAO uses at `gfx_pc.c:15445`).

### 4.1 New flags, settings, and shader bits (the complete registry)

| Feature | `SHADER_OPT` bit | Env var | Settings key (registered like `platform_sdl.c:1601`) | Default |
|---|---|---|---|---|
| Smooth env normals (CPU) | *(none — no shader change)* | `GE007_ENV_SMOOTH_NORMALS` | `Video.EnvSmoothNormals` (0/1), `Video.EnvRelightBlend` (0..1, default 0.6) | off |
| World-pos attribute | `SHADER_OPT_WORLD_POS (1u<<5)` | *(internal; set by consumers below)* | — | off |
| Sun shadow receive | `SHADER_OPT_SUN_SHADOW (1u<<6)` | `GE007_SUN_SHADOW` | `Video.SunShadow` (0/1), `Video.SunShadowRes` (1024/2048/4096, default 2048), `Video.SunShadowRadius` (world units, default 60) | off |
| Per-pixel directional (dFdx) | `SHADER_OPT_DFDX_LIGHT (1u<<7)` | `GE007_PERPIXEL_LIGHT` | `Video.PerPixelLight` (0/1) | off |
| Material maps (normal/rough) | `SHADER_OPT_MATERIAL_MAPS (1u<<29)` | `GE007_MATERIAL_MAPS` | `Video.MaterialMaps` (0/1) | off |
| Character per-pixel normals | `SHADER_OPT_CHR_NORMALS (1u<<30)` | `GE007_CHR_NORMALS` | `Video.CharacterNormals` (0/1) | off |
| Dynamic lights (N≤4) | `SHADER_OPT_DYN_LIGHTS (1u<<31)` | `GE007_DYN_LIGHTS` | `Video.DynamicLights` (0/1) | off |

Consumers imply dependencies: setting `SUN_SHADOW`, `DFDX_LIGHT`, `MATERIAL_MAPS`, or
`DYN_LIGHTS` forces `WORLD_POS` on for that draw. `--remaster` (preset table
`platform_sdl.c:1960`) eventually turns the shipped ones on; each stays off until its
epic's review checkpoint, exactly as `Video.Ssao` did (`platform_sdl.c:202`).
All bits verified free at `gfx_cc.h:50-75` (taken: 0–4, 8–28).

### 4.2 Per-frame camera capture (shared substrate, epic E2.T1)

> **SHIPPED (W1.E2.T1, merged 987d48b).** The per-frame view-inverse capture
> (`g_pc_view_inv`) and the audited sun-dir accessor (`bgGetGlobalLightDir`) landed.

New file-scope globals in `gfx_pc.c` beside the SSAO coefficients (`:15384-15387`):

```c
/* World-space lighting substrate (W1). Captured render-side; sim never reads. */
float g_pc_view_inv[4][4];      /* inverse of the main-scene camera view      */
bool  g_pc_view_inv_valid;      /* false until the first world-perspective mv */
float g_pc_sun_dir_world[3];    /* GlobalLight dir, normalized, world space   */
float g_pc_shadow_mat[4][4];    /* lightProj*lightView, world -> shadow clip  */
```

Capture: in `gfx_sp_matrix`, when a **modelview LOAD** arrives whose source is a room
matrix while the projection is the world perspective (the same
`is_room_matrix_addr` / `rsp.projection_is_field_10e0` machinery used at
`gfx_pc.c:15398/16116`), the loaded MV *is* `roomLocal→view`. Rather than inverting a
composite, capture the camera directly: the game already exposes world→view as
`camGetWorldToScreenMtxf()` (returns `Mtxf *`, extern-declared at `bg.c:7670`; copy into
a local `float[4][4]` before inverting; render-side consumer precedent at `gfx_pc.c:16137`).
Once per frame, at the first world-perspective room draw:
`g_pc_view_inv = inverse4x4(camGetWorldToScreenMtxf())` (standard cofactor inverse,
new `static void gfx_matrix_inverse(float out[4][4], const float in[4][4])` next to
`gfx_matrix_mul`). Reset `g_pc_view_inv_valid=false` in the backends' `start_frame`
(both: `gfx_opengl_start_frame` mirror of `gfx_metal.mm:1083`).
`g_pc_sun_dir_world` = normalized `GlobalLight.l[0].l.dir` (`bg.c:2740`) — constant, but
read through a tiny accessor `bgGetGlobalLightDir()` added to `bg.c` so the renderer
does not reach into game data layout (R1 hygiene: render reading sim is allowed; we
still keep it to one audited accessor).

Rail note: this is render code reading sim state (allowed direction). It adds **zero**
sim writes; `scripts/ci/check_sim_render_separation.sh` still passes because game TUs
gain no renderer-backend imports (only the reverse).

Env caveat: `GE007_LEGACY_ROOM_MV_PROJ_ORDER` (`gfx_pc.c:15303-15311`, default off)
flips the MP composition order and breaks this world-space reconstruction. E2's capture
code must check the env once and, when set, leave `g_pc_view_inv_valid=false` (all
world-attr features silently off) — never produce wrong world positions.

### 4.3 Epic E1 — T1.3 Smooth env normals (CPU relight; ships before any VBO work)

**Decision: normals are computed CPU-side from the static room vertex pools, once per
room, cached — not per-load, and not (yet) via fragment `dFdx`.** Rationale, having read
the code paths:

- Fragment `dFdx` reconstruction needs a world-pos varying — that is T2.1 (E2). Doing it
  first inverts the roadmap's T1.3→T2.1 order *and* yields flat per-face normals, which
  fix nothing about facet seams (the seams ARE per-face discontinuities; only
  **averaged** normals remove them).
- Per-vertex averaging at draw time is impossible with full fidelity: `gfx_sp_vertex`
  sees ≤32-vertex loads (`gfx_pc.c:16340`) and triangles arrive after the load — no
  adjacency. But **room geometry is static** and the port already contains two
  production DL walkers that recover a room's full triangle list + vertex pool from the
  raw big-endian bytes: the collision walker (`bgTestLineIntersectionInRoom`,
  `bg.c:11404` — the roadmap's `bg.c:9750` anchor has drifted) and the
  shoot-out-lights walker (`lightfixture.c:159-183` G_VTX backtrack + `:200-203`
  G_TRI1/G_TRI4 decode). We reuse that exact decode.

**Data structure + API (new file `src/platform/fast3d/gfx_room_normals.c/.h`):**

```c
struct RoomNormals {
    const Vtx *pool_base;   /* identity key: the room's vertex pool pointer  */
    uint32_t   count;       /* vertices in pool                              */
    int8_t   (*n)[3];       /* averaged unit normals, n[i] ∈ [-127,127]      */
};
/* Lazily built on first flagged draw of a room; keyed by pool pointer.
 * O(rooms) map, built once per room per level load — NOT per frame
 * (RENDERING_ARCHITECTURE.md §1 "nothing runs per-primitive"). */
const int8_t *gfx_room_normal_lookup(int room_id, uintptr_t vtx_src_addr);
void gfx_room_normals_reset(void);   /* called at level unload / stage change */
```

Builder algorithm (runs once per room, ~1–4 ms estimated for the largest rooms —
same order of work as the room-collision walk the sim already does per query; E1.T1
acceptance measures it under `GE007_ENV_NORMALS_DIAG=1`):
1. Walk the room's expanded DL exactly as `lightfixture.c` does (backtrack-free forward
   walk: track current G_VTX base, decode G_TRI1 (idx/10) and G_TRI4 raw nibbles —
   `lightfixture.c:200-203` documents both strides).
2. For each triangle, compute the face normal from the three **object-space** `ob[]`
   (room-local space is a uniform scale+translate of world — the landed W1.E2 identity
   at `gfx_pc.c:16445-16450` — so object-space normals equal world-space normals for
   rooms; no matrix needed).
3. Accumulate area-weighted face normals into per-pool-index bins, **merging bins whose
   positions are byte-identical** (GE rooms duplicate vertices across quads with
   independent UVs — this position-merge is precisely what erases the Dam per-quad
   seams). Normalize, quantize to int8.

**Application (in `gfx_sp_vertex`, the `else` branch of the G_LIGHTING test at
`gfx_pc.c:16684-16687` — landed W1.E1, relight block follows at `:16688+` — gated so
flag-off costs one predictable branch):**

```c
} else {
    d->color.r = v->cn[0]; d->color.g = v->cn[1]; d->color.b = v->cn[2];
    if (g_pcEnvSmoothNormals && source_room >= 0 &&
        g_current_draw_class == DRAWCLASS_ROOM) {
        const int8_t *n = gfx_room_normal_lookup(source_room, src_base + i*src_stride);
        if (n) {
            /* Lambert vs GlobalLight in room-local == world space. */
            float ndl = fmaxf(0.f, (n[0]*sunx + n[1]*suny + n[2]*sunz) * (1.f/127.f));
            /* Relight = replace the baked DIRECTIONAL luma, keep chroma.
             * lit ∈ [amb, amb+dif]; normalized so a light-facing surface keeps
             * its baked brightness: amb=150/255, dif=105/255 (GlobalLight, bg.c:2745,
             * rescaled so amb+dif = 1.0). */
            float lit = 0.588f + 0.412f * ndl;
            float b = g_pcEnvRelightBlend;               /* Video.EnvRelightBlend */
            /* luma replace; max() guards black baked verts (division) */
            float scale = (1.f - b) + b * (lit / fmaxf(luma01(v->cn), 0.05f));
            d->color.r = clamp_u8(v->cn[0] * scale);     /* chroma preserved */
            d->color.g = clamp_u8(v->cn[1] * scale);
            d->color.b = clamp_u8(v->cn[2] * scale);
        }
    }
}
```

Why *luma replace* and not multiply: the baked color already encodes 1997's lighting;
multiplying N·L on top double-darkens and cannot remove the per-quad baked
discontinuity. Replacing the luminance with the recomputed Lambert (blend-weighted)
converges neighboring quads that share positions to the same brightness — the seam fix —
while `EnvRelightBlend < 1` retains authored mood. **Zero shader / VBO / backend change:
both backends inherit this for free** (the color flows through the existing SHADE input
packing at `gfx_pc.c:18929-18966`).

Perf guard: the lookup is an O(1) hash of `(pool_base, (src_addr-pool)/stride)`; the
one-rule (`RENDERING_ARCHITECTURE.md` §1) is respected — no per-vertex allocation,
string work, or scans.

### 4.4 Epic E2 — T2.1 VBO plumbing (the Track-2 gate)

**LoadedVertex extension** (`gfx_pc.c:2435`; landed — `wx/wy/wz` + `nrm[3]` at
`:2462-2467`): append

```c
    float wx, wy, wz;      /* world-space position — valid only when
                            * g_pc_world_attrs_active for this load  */
    int8_t nrm[3];         /* world-space unit normal *127 (0,0,0 = none):
                            * rooms: smooth cache (E1); chars: Vtx_tn.n
                            * transformed to world (E6); else 0 */
```

Computed in `gfx_sp_vertex` right after the clip transform, only when any consumer flag
is live AND the draw is world-perspective (ROOM class).

> **CORRECTION (measured, W1.E2.T2 2026-07-03).** The `world = (ob·MV) · g_pc_view_inv`
> reconstruction above is **WRONG for rooms**: the room modelview does **not** carry the
> filepos offset (rooms are drawn with ≈the camera as modelview), so `(ob·MV)·V⁻¹` lands
> ~`filepos·scale` off (verified numerically under `GE007_WORLD_DIAG`: recon
> `(21322,-672,15932)` vs truth `(19876,-51,17586)`). It is **not** a convention/transpose
> issue — `mtx4TransformVecInPlace` is row-vector, matching the clip transform. Rooms use
> the game's **exact identity** instead (the §4.0 "cheaper" alternative, which the render
> trace block already uses before projecting through `camGetWorldToScreenMtxf`):
> `world = (ptr_bgdata_room_fileposition_list[room].pos + ob) * room_data_float2`, with
> `room` = `source_room` or `gfx_find_room_for_vtx_addr(src_base)`. E2 is ROOM-only, so this
> is exact and sufficient. `g_pc_view_inv` (T1) is retained for the camera-world position
> (its row-3 translation) and the character path (E4/E5/E6, whose modelviews *are* clean
> object→view). E4+ must also revisit per-viewport capture for split-screen.

Flag-off writes nothing (identity: struct grows, values untouched, no packing reads them —
`buf_vbo` layout unchanged because the pack is feature-keyed).

**cc_options**: at the assembly site (`gfx_pc.c:17722+`; the WORLD_POS half landed at
`:17729-17740` — consumer sub-bits are OR'd in by their epics):

```c
bool world_attrs = gfx_world_attrs_wanted();   /* any of shadow/dfdx/matmaps/dyn on */
if (world_attrs && g_current_draw_class == DRAWCLASS_ROOM && world_perspective) {
    cc_options |= SHADER_OPT_WORLD_POS;
    if (g_pcSunShadow)      cc_options |= SHADER_OPT_SUN_SHADOW;
    if (g_pcPerPixelLight)  cc_options |= SHADER_OPT_DFDX_LIGHT;
    if (settex_active && settex_has_material_maps(texturenum))
                            cc_options |= SHADER_OPT_MATERIAL_MAPS;
    if (g_pcDynLights && g_dyn_light_count > 0)
                            cc_options |= SHADER_OPT_DYN_LIGHTS;
}
```

**Packing** (`gfx_pc.c:18839` loop): immediately after the pos/diag block, before
texcoords (landed at `:18863-18872`):

```c
if (cc_options & SHADER_OPT_WORLD_POS) {
    buf_vbo[buf_vbo_len++] = v_arr[vi]->wx;
    buf_vbo[buf_vbo_len++] = v_arr[vi]->wy;
    buf_vbo[buf_vbo_len++] = v_arr[vi]->wz;
    if (cc_options & (SHADER_OPT_MATERIAL_MAPS | SHADER_OPT_CHR_NORMALS)) {
        buf_vbo[buf_vbo_len++] = v_arr[vi]->nrm[0] * (1.f/127.f);
        buf_vbo[buf_vbo_len++] = v_arr[vi]->nrm[1] * (1.f/127.f);
        buf_vbo[buf_vbo_len++] = v_arr[vi]->nrm[2] * (1.f/127.f);
    }
}
```

**GL generator** (`gfx_opengl.c`, VS attr block after the aDiagTri block `:868-871`;
`aWorldPos` landed at `:874-879`):
`in vec3 aWorldPos;` + `out vec3 vWorldPos;` (+ optional `aNormal/vNormal`),
`num_floats += 3 (+3)`; attrib binding added at the same ordinal position in the
`:1385-1443` walk (`aWorldPos` bind landed `:1396-1403`; order == pack order, enforced
by the existing convention comment at `gfx_metal.mm:133-136`).

**Metal generator** (`gfx_metal.mm`, after the aDiagTri `add_attr` at `:404-407`;
landed at `:417`):
`add_attr("float3", "aWorldPos", 3)` (+ normal); VertexOut gains `float3 vWorldPos;`.
The vertex descriptor and stride update automatically (`mtl_build_vertex_descriptor`
consumes `attrs[]`/`numFloats`, `:913-930`).

**Uniforms**: GL — per-program locations for `uSunDirWorld (vec3)`, `uSunColor (vec3)`,
`uAmbient (vec3)`, `uShadowMat (mat4)`, `uDynLight[4] (vec4 pos_radius)`,
`uDynLightColor[4] (vec4 rgb_intensity)`, resolved at link (pattern: `:1458-1476`),
uploaded in `gfx_opengl_load_shader`/draw when dirty. Metal — extend the 48-byte
`Uniforms` (`gfx_metal.mm:299-308`; C mirror `MtlUniforms` `:733`) to a 288-byte struct
(append: `float4x4 shadowMat`,
`float4 sunDirWorld`, `float4 sunColor`, `float4 ambient`, `float4 dynPos[4]`,
`float4 dynCol[4]`), updating the C mirror + the static-assert both structs carry.
Metal shadow-map texture binds at `[[texture(5)]]` + comparison sampler `[[sampler(5)]]`;
GL uses texture unit 5 (units 0/1 combiner, 2 diag — verified free above).

### 4.5 Epic E3 — T1.4 Sun shadow map

**Architecture problem**: the backend never sees a scene — only pre-transformed clip
triangles (`gfx_rendering_api.h:42`). You cannot "re-render from the light" without a
second DL interpretation pass. **Decision: capture-and-replay with one frame of
latency.** During the main pass (flags on), world-space triangles of ROOM + CHRPROP
draws are appended to a shadow-geometry ring (positions only, 9 floats/tri; Dam worst
case ≈ 40k tris ≈ 1.4 MB — measured budget from the perf census tri counts); at the
**next** frame's `start_frame`, before the scene encoder opens
(`mtl_start_frame` `:1151` / GL equivalent before `ensure_scene_target` bind), the
backend renders that buffer with a trivial depth-only pipeline into the shadow map.
Sun is static and the camera-fit lags one frame — invisible in practice and standard
for capture-based shadow injections. Kill-switch honesty: characters animate, so their
shadow lags 1 frame (~16 ms); acceptance explicitly screenshots a moving guard.

**Known limitation — frustum-limited casters (accepted, risk R8).** The pack loop
only sees triangles that survived clip rejection (`clip_rej`, upstream of
`gfx_pc.c:18839`), so the capture ring misses casters just outside the view frustum
(a guard a step off-screen, geometry above/behind the camera). Consequence: shadows
can pop in at screen edges and tall off-screen structures cast nothing. Mitigations
in order: (1) the camera-centered ortho fit means most missing casters would land
outside the shadow radius anyway; (2) E3.T4's acceptance must include a pan capture
past a guard to characterize edge-pop; (3) if M3 review rejects it, the pre-approved
escalation is capturing at `gfx_sp_tri` pre-rejection for ROOM+CHRPROP when the flag
is on (more tris in the ring — re-measure the 2 MB budget), not a scene re-render.

- **Light matrix fit (single cascade — decision).** Ortho frustum centered on the
  camera position (from `g_pc_view_inv` row 3), radius `Video.SunShadowRadius`
  (default 60 world units ≈ the fog-visible playfield; GE view distances are short —
  Dam far plane ~200, `gfx_pc.c:15688` comment). Texel-snap the ortho origin to
  eliminate swimming. Cascades rejected: fog + short draw distances make one 2048²
  map ≈ 3.4 px/world-unit at radius 60 — above the acne-free PCF floor; a second
  cascade is the pre-approved fallback (risk R3) not the default.
- **Depth-only pass.** GL: `GL_DEPTH_COMPONENT24` texture FBO,
  `glPolygonOffset(2.0, 4.0)` slope-scaled bias, front-face culling. Metal: reuse the
  `Depth32Float` descriptor pattern (`gfx_metal.mm:846-853`), own
  `MTLRenderPassDescriptor` + `[enc setDepthBias:4 slopeScale:2 clamp:0]`, PSO with nil
  color attachments and a 12-byte float3 vertex layout; one static MSL vertex function
  `shadowVertex(worldPos, shadowMat)` compiled at init (not per-combiner — geometry
  only).
- **Receiver injection (both generators).** Under `SHADER_OPT_SUN_SHADOW`, after the
  combiner clamp (GL: after `texel = clamp(...)`; MSL: after `gfx_metal.mm:557`) and
  **before** the fog mix (fog must win — lighting must not brighten fog):

```glsl
// GLSL (gfx_opengl.c generator)                 // MSL mirrors 1:1 (sample_compare)
vec4 sc = uShadowMat * vec4(vWorldPos, 1.0);
vec3 suv = sc.xyz / sc.w * 0.5 + 0.5;
float sh = 0.0;
for (int dy = -1; dy <= 1; ++dy)                 // 3x3 PCF, 9 taps
  for (int dx = -1; dx <= 1; ++dx)
    sh += texture(uShadowMap, vec3(suv.xy + vec2(dx,dy)*uShadowTexel, suv.z - uShadowBias));
sh /= 9.0;
if (any(greaterThan(abs(suv - 0.5), vec3(0.5)))) sh = 1.0;   // outside map = lit
texel.rgb *= mix(uShadowUmbra, 1.0, sh);         // uShadowUmbra default 0.55
```

GL sampler: `sampler2DShadow`, `GL_COMPARE_REF_TO_TEXTURE`/`LEQUAL`. Metal:
`depth2d<float>` + `sample_compare` with a `compareFunction:LessEqual` sampler
(`mtl_sampler_for` gains a comparison variant, `:1015-1035`).
- **Blob retirement**: when `Video.SunShadow=1`, `doshadow` early-returns
  (`model.c:10980` gate extended: `if (!s_shadows_enabled || gfx_sun_shadow_active())`),
  via a new render-state query header — a **game-TU → renderer-flag read**. The R1
  checker is a backend-symbol *denylist* (`_gfx_opengl_`, `_texture_pack_`, `_gl[A-Z]`,
  … — `check_sim_render_separation.sh:33`); the fast3d interpreter surface (`gfx_*`)
  is the documented allowed submission/query family (roadmap line 301), so the query
  MUST live in `gfx_pc.c` under a `gfx_*` name — never in `gfx_opengl.c`/`gfx_metal.mm`.
  The sim-invariance gate must still prove it harmless: the query affects only DL
  contents, never tick state.

### 4.6 Epic E4 — T2.2 Per-pixel geometric-normal directional shading

Cheapest real lighting once E2 lands (roadmap: "days"). Under `SHADER_OPT_DFDX_LIGHT`
(ROOM only), inject before the shadow block:

```glsl
vec3 gN = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));   // MSL: dfdx/dfdy
gN *= DERIV_SIGN;   // generation-time constant: GL +1.0, Metal -1.0 (dFdy flip) — note below
float ndl = max(dot(gN, uSunDirWorld), 0.0);                     // +sign — see CORRECTION
float lit = uAmbientLuma + uSunLuma * ndl;                        // 0.588/0.412 as E1
texel.rgb *= mix(vec3(1.0), vec3(lit) / max(bakedLuma(vShade), 0.05), uRelightBlend);
```

> **CORRECTION (landed, W1.E4 2026-07-03).** Two points above were wrong and are fixed
> in the shipped E4 (`SHADER_OPT_DFDX_LIGHT (1u<<7)`, `gfx_cc.h`):
> 1. **Sun sign is `+uSunDirWorld`, not `-`.** The shipped E1 relight uses
>    `dot(nrm, +g_pc_sun_dir_world)` (`gfx_pc.c:16706`); E4 matches it for cross-epic
>    consistency (the GL uniform is fed `g_pc_sun_dir_world` verbatim).
> 2. **The `CCFeatures.shade_input_idx` mechanism is un-implementable — use a dedicated
>    `aShade`/`vShade` attribute instead.** `gfx_cc_get_features(shader_id0, shader_id1)`
>    never sees the combiner, and SHADE-vs-PRIM is NOT recoverable from `shader_id0`
>    (`TEXEL0*SHADE` and `TEXEL0*PRIM` hash to the same `shader_id0` and share one cached
>    program, so baking a shade index is cache-incorrect). The landed design adds a
>    `bool opt_dfdx_light` to `CCFeatures` (from the `shader_id1` DFDX bit — cache-correct)
>    and, when set, emits an `in vec3 aShade; out vec3 vShade;` attribute carrying the
>    per-vertex baked shade colour, packed right after `aWorldPos` (DFDX implies WORLD_POS).
>    `bakedLuma(vShade)` is the luma-replace reference; no `gfx_cc.c` `shade_input_idx` field.

Winding orientation: GE rooms are consistently wound for `G_CULL_BACK`
(`bg.c:7016-7017` sets it), so the derivative normal has stable sign per backend;
Metal's flipped y in `dFdy` is compensated by a per-backend constant (GL `+1`, Metal
`-1`) baked at generation time (empirically validated: Metal `+1.0` gives ~58% inverted
vs GL, `-1.0` matches). `gfx_world_attrs_wanted()` gains `|| g_pcPerPixelLight` so E4
drives `WORLD_POS` (the §4.4 "consumers imply dependencies" intent).
When E4 is on it **supersedes** E1's CPU relight for that draw (CPU path checks
`!g_pcPerPixelLight` to avoid double application); E1 remains the low-spec/GL-legacy
path and the smoothing source for E5's normal attribute.

### 4.7 Epic E5 — T2.3 Normal/roughness sidecars + T2.4 tangent hardening

**Loader contract extension** (`texture_pack.c`): new

```c
/* Probes <pack>/textures/tok%04d_n.png (tangent-space normal, RGB=XYZ*0.5+0.5)
 * and tok%04d_r.png (grayscale roughness, R channel). Same miss-cache pattern
 * as texture_pack.c:29. Returns a bitmask of which sidecars loaded. */
unsigned texture_pack_try_load_sidecars(int token, struct TexPackSidecars *out);
```

Hooked beside `gfx_pc.c:21525`: on a successful HD (or stock) settex decode, sidecars
upload to two additional `gfx_rapi->new_texture()` ids cached in the settex cache entry
(the cache already keys per token: `settex_cache` struct `gfx_pc.c:942-953`, per-token
lookup `:21151`). At bind time
(`gfx_flush` texture binding), if the draw's cc_options carry `MATERIAL_MAPS`, bind
normal→unit 3, roughness→unit 4 (`select_texture(3/4, id)` — the vtable already takes a
tile index, `gfx_rendering_api.h:34`; GL maps it to `GL_TEXTURE0+n`, Metal to
`[[texture(n)]]`). **Verified**: units/indices 3+ are unused (GL `:1481-1495`; Metal
`:472-476` — snapshot uses 2, shadow map takes 5 per §4.4).

**Rail R2**: sidecars generated from ROM-derived diffuse (e.g. normal-from-height on an
upscale) are **Tier B — local only, never committed**; `_n/_r` for CC0 imports or
procedural synth output are A1. `build_pack.py` gains `--emit-material-maps`
(Sobel normal-from-height + flatness-based roughness) writing sidecars beside each
`tok####.png`; `scripts/ci/check_no_rom_data.sh` already hard-fails any tracked PNG, so
enforcement is inherited.

**Shader injection** (ROOM + `MATERIAL_MAPS`; both generators, same slot as E4 which it
replaces when present). **Tangent decision: no vertex tangents — derivative cotangent
frame.** Per-quad independent UVs (the Dam wall problem) make authored/accumulated
vertex tangents seam-prone by construction; the screen-space cotangent frame is exact
per pixel for exactly this geometry and removes the entire T2.4 vertex-plumbing class:

```glsl
vec3 N  = (has vNormal attribute) ? normalize(vNormal)                 // smooth (rooms: E1 cache)
                                  : normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));
vec3 dp1 = dFdx(vWorldPos), dp2 = dFdy(vWorldPos);
vec2 duv1 = dFdx(vTexCoord0), duv2 = dFdy(vTexCoord0);
vec3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
float invmax = inversesqrt(max(dot(T,T), dot(B,B)));
vec3 nTex = texture(uNormMap, sampleTexCoord0).xyz * 2.0 - 1.0;
N = normalize(mat3(T*invmax, B*invmax, N) * nTex);
float rough = texture(uRoughMap, sampleTexCoord0).r;                    // 1.0 if no _r
float ndl = max(dot(N, -uSunDirWorld), 0.0);
vec3 H = normalize(-uSunDirWorld + normalize(uCamPosWorld - vWorldPos));
float spec = pow(max(dot(N, H), 0.0), mix(64.0, 4.0, rough)) * (1.0 - rough) * 0.25;
vec3 lit = uAmbient + uSunColor * (ndl * shadow) ;
texel.rgb = texel.rgb * mix(vec3(1.0), lit / max(bakedLumaV, 0.05), uRelightBlend)
          + uSunColor * spec * shadow;
```

Composition decision (the "multiply after texel?" question): **lighting multiplies the
full combiner output `texel.rgb` after the final clamp and before fog**, with the E4
luma-replace normalization; **specular is additive after the multiply** (spec on top of
albedo is physically ordered) and is shadow-masked. The combiner is untouched upstream —
prim/env/shade semantics, decals, and effects composite exactly as stock. T2.4 becomes a
hardening task (mirrored-UV sign flips: `invmax` handles magnitude, a
`sign(duv1.x*duv2.y - duv2.x*duv1.y)` factor handles mirroring; wrap seams: derivatives
of wrapped UVs spike at the seam — clamp `|duv|` to 4 texels and fall back to geometric
N above that) rather than a plumbing project.

### 4.8 Epic E6 — T2.5 Character normals

Characters/props draw **with** `G_LIGHTING` and real `Vtx_tn.n[]` normals, consumed
CPU-side at `gfx_pc.c:16623-16660` (per-vertex Lambert into `d->color`, lights
transformed via `calculate_normal_dir` / `rsp.current_lights_coeffs`, `:15392`).
Isolated win, independent of the room work: under `SHADER_OPT_CHR_NORMALS`
(CHRPROP class + flag), also store the normal into `LoadedVertex.nrm` transformed
object→world (rotation part of `MV · V⁻¹`, renormalized), pack it (E2's optional normal
slot), and light per-pixel in the FS (same lit formula, `uRelightBlend` vs the
CPU-lit vertex color — here the baked color IS the CPU Lambert, so blend=1 replaces it
cleanly and adds sun-shadow reception on characters). The existing CPU lighting path is
left untouched (it still feeds the SHADE input for identity and for the blend base).

### 4.9 Epic E7 — Bounded dynamic lights (assessed against roadmap §8)

Roadmap §8 defers "full deferred / authored lights" — **that deferral stands**. What
ships here is strictly bounded: a per-frame, render-side registry of ≤4 point lights,
forward-added in the lit paths (ROOM/CHRPROP draws with `WORLD_POS`):

```c
/* gfx_pc.h — render-side, cleared in gfx_start_frame; sim never reads it. */
void gfx_register_dynamic_light(float wx, float wy, float wz,
                                float r, float g, float b, float radius);
```

Emitters (game TUs calling a `gfx_register_*` API — explicitly allowlisted by the R1
checker, roadmap line 301):
- **Muzzle flash**: at the existing flash draw site that already computes
  `flash_world_pos` (`portBuildFirstPersonFlashMatrix`, `gun.c:3379`, math
  `:3397-3410`), register a 1-tick warm light
  (radius ~8 wu, color 1.0/0.85/0.6) whenever the flash quad is drawn (and
  `portDisableMuzzleFlash` is off, `gun.c:60`).
- **Shoot-out-lights**: in the darkening path behind `GE007_SHOOT_OUT_LIGHTS`
  (`lightfixture.c:110-124`), register a 3-tick flicker at the fixture position on the
  destroy event. No persistent lamp lights (fixtures are not light entities — roadmap
  §4 "Phase 4 territory" verified: `g_CurrentEnvironment` holds only fog/sky).

Shader add (both generators, inside the lit block, after sun):

```glsl
for (int i = 0; i < uDynLightCount; ++i) {                 // uniform int, <= 4
    vec3 L = uDynLight[i].xyz - vWorldPos;
    float d = length(L);
    float att = clamp(1.0 - d / uDynLight[i].w, 0.0, 1.0); // linear falloff, cheap
    lit += uDynLightColor[i].rgb * (max(dot(N, L / max(d,1e-3)), 0.0) * att * att);
}
```

Honest assessment: with N=4, linear falloff, no shadows, and registration only from two
existing effect sites, this is days-scale and cannot metastasize into the deferred
rewrite §8 warns about. The kill criterion (§7 R6) keeps it that way.

---

## 5. Work breakdown

Rails column: R1 = sim-invariance obligations, R2 = asset tier, R3 = gating flag.
Estimates are junior-engineer-days including tests/validation, excluding review latency.

### E1 — Smooth env normals (T1.3) — 13d — **DONE** (merged to main, 2932033; ◆ T3 blend-look review pending)

| ID | Task | Files | Steps | Acceptance (runnable) | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W1.E1.T1 | Room normal cache builder | new `src/platform/fast3d/gfx_room_normals.c/.h`, new `tests/test_room_normals.c`, CMakeLists.txt | Reuse `lightfixture.c:137-179` BE DL decode; per-room walk → area-weighted, **position-merged** normals; lazy build keyed on pool ptr; reset hook at level unload; add diag env `GE007_ENV_NORMALS_DIAG` (**new**, this task; latch-once like `GE007_VERBOSE`, `gfx_pc.c:13035`) logging per-room build stats | **New** ctest target `room_normals`: add `tests/test_room_normals.c` + `add_test(NAME room_normals …)` copying the `sim_state_hash` pattern (`CMakeLists.txt:191-196`); `ctest -R room_normals` passes — synthetic 2-quad shared-edge pool yields identical normals at the duplicated positions. Dam boot logs `rooms=N built<5ms` under `GE007_ENV_NORMALS_DIAG=1` | 6 | — | R3: builder runs only when `Video.EnvSmoothNormals=1` |
| W1.E1.T2 | Relight application in `gfx_sp_vertex` | `gfx_pc.c:16306-16310`, `platform_sdl.c` (register `Video.EnvSmoothNormals`, `Video.EnvRelightBlend`) | Luma-replace formula §4.3; skip when `g_pcPerPixelLight` on; one latched flag read; register both settings with `settingsRegisterInt`/`Float` + env names (pattern: `Video.Ssao`, `platform_sdl.c:1601`) | Identity: `tools/renderer_parity_capture.sh --no-build` on the feature build, flags off — screenshot SHAs match pre-change baseline on both backends. A/B: run the §8 canonical command twice (labels `e1t2_off`/`e1t2_on`; add `GE007_ENV_SMOOTH_NORMALS=1` to the ON run), then `tools/compare_screenshots.py screenshot_e1t2_off.bmp screenshot_e1t2_on.bmp --max-changed-pct 35` exits 0 (rooms change; HUD/viewmodel pixels must not) | 4 | E1.T1 | R1: gate run (see §8); R3: `GE007_ENV_SMOOTH_NORMALS` |
| W1.E1.T3 | Tuning + seam validation ◆ | no new files — captures + settings sweep | With `GE007_ENV_SMOOTH_NORMALS=1`, run the §8 canonical command 3× adding `--config-override Video.EnvRelightBlend=0.3` / `0.6` / `0.9` (labels `blend03/06/09`) on `--level dam` (rock wall `tok0949`) and again on `--level surface1` (snow); ALSO one interior sanity capture (`--level bunker1` or `facility`) — `GlobalLight`'s WSW sun direction is meaningless indoors, so verify the relight doesn't fight interior baked mood; frame the roadmap-§3 seam repro angle; pick the default with the reviewer | Reviewer-visible: the per-quad wall seam A/B pair archived locally; `tools/audit_screenshot_health.py screenshot_blend*.bmp` green; perf: `GE007_ENV_SMOOTH_NORMALS=1 tools/perf_census.sh dam` ≥ 95% of the fps from a plain `tools/perf_census.sh dam` baseline (exported env vars propagate into the census runs) | 3 | E1.T2 | R2: screenshots local-only (never committed) |

### E2 — World-space VBO plumbing (T2.1) — 12d — **DONE** (T1–T4 merged to main, 987d48b)

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W1.E2.T1 | Per-frame camera capture + inverse | `gfx_pc.c` (globals by `:15384`; `gfx_matrix_inverse` by `gfx_matrix_mul`), `bg.c` (`bgGetGlobalLightDir` accessor), both backends' `start_frame` reset | §4.2; reset mirrors the `g_pc_ssao_proj_b` per-frame reset at `gfx_metal.mm:1083` | Diag print under `GE007_WORLD_DIAG=1` (**new** diag env, this task): `world = view·view_inv` round-trip error < 1e-3 on Dam frame 120; `scripts/ci/check_sim_render_separation.sh` passes on the built tree | 3 | — | R1: render-reads-sim only |
| W1.E2.T2 | LoadedVertex + pack + cc_options bit | `gfx_pc.c:2434` (struct), `:16087+` (compute), `:17288+` (bit), `:18409+` (pack) | §4.4; `SHADER_OPT_WORLD_POS (1u<<5)` in `gfx_cc.h`; also add diag env `GE007_FORCE_WORLD_ATTRS` (**new**, render-side latch): makes `gfx_world_attrs_wanted()` return true so the attribute path is testable before any consumer epic lands | With all W1 flags off: full-suite screenshot SHAs unchanged (both backends, `renderer_parity_capture.sh`); `ctest` green | 4 | E2.T1 | R3: bit set only under consumer flags (or the diag force env) |
| W1.E2.T3 | GL + Metal generator attribute | `gfx_opengl.c:846+/:1349+`, `gfx_metal.mm:394+` | §4.4 — same ordinal slot in VS walk, attrib bind, `add_attr`; extend Metal `Uniforms` + C mirror (`gfx_metal.mm:289-297`, `:712`) with §4.4 fields + static size assert | With `GE007_FORCE_WORLD_ATTRS=1`: Metal MSL dump (`GE007_METAL_DUMP_SHADERS=1`) and GL shader summary (`GE007_VERBOSE=1` prints `[SHADER_n]` for the first 3 shaders, `gfx_opengl.c:1291-1293` — extend to dump full GLSL source under the same gate if needed) show `aWorldPos` at the matching ordinal; a temporary `fragColor.rgb=fract(vWorldPos*0.01)` diag build shows a stable world-anchored pattern while strafing, identical GL vs Metal | 3 | E2.T2 | R3 |
| W1.E2.T4 | Identity + parity gates wired | `tools/renderer_parity_capture.sh` (current scenes: `facility_scissor`, `surface_sky_fog` — see its `usage()`; copy a `run_capture` block) | Add a `world_attrs` scene (`--scene world_attrs`), off/on variants via `GE007_FORCE_WORLD_ATTRS` as the scene env | Gate: flags-off byte-identical; flags-on GL vs Metal `tools/compare_screenshots.py <gl.bmp> <metal.bmp> --max-changed-pct 4` (**CORRECTED from 0.5**: the inherent GL↔Metal difference on the normal dam render is already ~3.0% — edge/coverage/precision — so 0.5% is unreachable for ANY GL-vs-Metal compare; use the documented 2.4–4% tolerance. Measured: world-pos `WORLD_POS_DIAG` parity = 2.8% < the 3.0% baseline, i.e. the attribute adds zero divergence); R1: run `tools/sim_invariance_gate.sh dam1 600 2` twice — plain, then with `GE007_FORCE_WORLD_ATTRS=1` exported (the gate's `env` launch inherits it) — all four printed hashes identical | 2 | E2.T3 | R1 (the gate itself) |

### E3 — Sun shadow map (T1.4) — 21d

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W1.E3.T1 | Shadow-geometry capture ring | `gfx_pc.c` (append at pack loop `:18839`, ROOM+CHRPROP, flag-on) | 9 floats/tri into a 2 MB ring + tri count; cleared per frame; no capture when flag off | Diag counter matches scene tri count ±0 on Dam frame 120; zero cost flag-off (perf census delta < 1%) | 4 | E2.T2 | R3: `GE007_SUN_SHADOW` |
| W1.E3.T2 | GL shadow pass | `gfx_opengl.c` (`start_frame`; new FBO beside `:657-675`; light-fit + texel snap in `gfx_pc.c` shared helper `gfx_compute_shadow_mat`) | §4.5: depth-only FBO `Video.SunShadowRes`², ortho fit radius `Video.SunShadowRadius`, `glPolygonOffset(2,4)`, front-face cull | `GE007_DUMP_SHADOW_MAP=1` (**new** diag env, this task) writes a local PGM: depth silhouette of Dam visible (manual once), non-blank assert in trace; identity-off unchanged | 5 | E3.T1 | R3 |
| W1.E3.T3 | Metal shadow pass | `gfx_metal.mm` (`mtl_start_frame` before `:1151`; static `shadowVertex` MSL; depth-only PSO; comparison sampler variant of `mtl_sampler_for:1015`) | §4.5; own encoder, `setDepthBias:4 slopeScale:2` | Same PGM dump path via a blit+readback; GL-vs-Metal shadow-map compare `--max-changed-pct 2` | 4 | E3.T2 | R3 |
| W1.E3.T4 | Receiver injection, both generators | `gfx_opengl.c` (~after combiner clamp), `gfx_metal.mm:557+`, uniforms per §4.4 | `SHADER_OPT_SUN_SHADOW (1u<<6)`; 3×3 PCF; `uShadowBias` default 0.0015; outside-map=lit | A/B Dam frame 120: guard + Bond shadows visible, direction matches `(77,77,46)` (`bg.c:2745`); acne sweep screenshots at bias 0.0005/0.0015/0.005 archived; GL==Metal `--max-changed-pct 1.0` | 5 | E3.T3, E2.T3 | R3; R1 gate rerun |
| W1.E3.T5 | Blob retirement + moving-target check | `model.c:10980` gate + renderer query header | `gfx_sun_shadow_active()` render-state query (must live in `gfx_pc.c` under a `gfx_*` name — §4.5); blob returns when flag off | Faithful mode screenshot: blob present, byte-identical; remaster: blob absent; moving-guard capture shows ≤1-frame lag artifact documented; R1: `tools/sim_invariance_gate.sh dam1 600 2` twice — plain vs `GE007_SUN_SHADOW=1` exported — all hashes identical (query affects DL only) | 3 | E3.T4 | R1 (query audited), R3 |

### E4 — Per-pixel directional (T2.2) — 5d

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W1.E4.T1 | dFdx lighting, both generators | `gfx_opengl.c`, `gfx_metal.mm`, `gfx_cc.c` (`shade_input_idx`), `gfx_pc.c:17729+` (landed WORLD_POS block) | §4.6 incl. per-backend dFdy sign + `shade_input_idx` plumbing; CPU relight (E1) auto-defers | Dam wall grazing-light A/B: faceting visible per-face (expected — geometric N); GL==Metal `--max-changed-pct 1.0`; identity-off byte-identical | 3 | E2.T3 | R3: `GE007_PERPIXEL_LIGHT` |
| W1.E4.T2 | Interaction matrix validation | new `tools/w1_interaction_matrix.sh` | Script loops the 8 combos of {`GE007_ENV_SMOOTH_NORMALS`, `GE007_PERPIXEL_LIGHT`, `GE007_SSAO`} × {0,1}, runs the §8 canonical command per combo (label = combo bits, e.g. `m110`), then `tools/compare_screenshots.py <all-off.bmp> <combo.bmp> --json-out <combo>.json`; compute mean luma from the JSON `mean_rgb` field; assert no double-darkening: mean-luma delta of E1+E4 vs E4 alone < 2% | `tools/w1_interaction_matrix.sh` committed; running it prints one PASS line per combo and exits 0 (the <2% assertion lives in the script) | 2 | E4.T1 | R1 gate |

### E5 — Material sidecars + tangent hardening (T2.3+T2.4) — 22d

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W1.E5.T1 | Sidecar loader | `texture_pack.c/.h` (`try_load_sidecars` per §4.7), hook near `gfx_pc.c:21525`, settex cache entry +2 texture ids | Same miss-cache pattern (`texture_pack.c:29`); dims may differ from diffuse (independent UV normalize) | Unit: fixture pack with `tok0022_n.png` loads, `tok0022_r.png` missing → mask=NORMAL_ONLY; bad PNG → stock (no crash, ASan clean) | 3 | — | R2: sidecars from ROM-derived sources are Tier B local-only (guard inherited: `check_no_rom_data.sh`); R3: needs `Video.TexturePack` + `Video.MaterialMaps` |
| W1.E5.T2 | Sampler units 3/4 both backends | `gfx_opengl.c` (bind uNormMap/uRoughMap units 3/4 at the `:1481-1495` sampler-binding pattern), `gfx_metal.mm:472+` (`[[texture(3)]]/[[texture(4)]]`), `gfx_flush` bind path | Bind only when cc bit set; white/flat 1×1 fallbacks (Metal precedent `mtl_ensure_white:900`) | Diag shader visualizing `texture(uNormMap).xyz` shows the sidecar on Dam ground; combiner draws without maps untouched | 4 | E5.T1, E2.T3 | R3 |
| W1.E5.T3 | Normal/rough shading injection | both generators; `SHADER_OPT_MATERIAL_MAPS (1u<<29)` | §4.7 cotangent frame + Blinn-Phong-lite spec, shadow-masked; supersedes E4 block when present | Dam pack with `--emit-material-maps` output: gravel relief visible in raking sun; spec sweep rough=0/0.5/1 archived; GL==Metal `--max-changed-pct 1.5`; identity-off byte-identical | 6 | E5.T2, E3.T4 | R2: demo pack local-only; R3 |
| W1.E5.T4 | T2.4 hardening (mirror/wrap/per-quad) | generators (mirroring sign, `\|duv\|` clamp→geometric-N fallback §4.7) | Target the known-hostile set: Dam per-quad rock, mirrored settex (`settex_mirror_tex1` pack path `gfx_pc.c:18875-18879`), wrapped floors | No inverted-lighting quads on the Dam wall orbit capture (12 angles); wrap-seam capture shows fallback not sparkle | 5 | E5.T3 | R3 |
| W1.E5.T5 | `build_pack.py --emit-material-maps` | `tools/texpack/build_pack.py` (flag = a thin wrapper), `tools/texpack/make_sidecars.py` (the single implementation — created by W2.E6; if W2.E6 hasn't landed yet, THIS task creates `make_sidecars.py` and W2.E6 adopts it — one Sobel implementation, never two) | Sobel normal-from-height + flatness roughness in `make_sidecars.py`; `--emit-material-maps` calls it per emitted diffuse; deterministic (pure function); emits beside diffuse | Fixture: synthetic dump → `tok0022_n.png` stable SHA256; README documents Tier B propagation (upscale-derived ⇒ local-only) | 4 | E5.T1 | R2: tooling A1, output inherits input tier |

### E6 — Character normals (T2.5) — 6d

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W1.E6.T1 | Char normal capture + per-pixel path | `gfx_pc.c:16623+` (store `nrm` world-transformed), `:17729+` (CHRPROP bit `1u<<30`, beside the landed WORLD_POS block), both generators (normal-attr variant §4.4/§4.8) | CPU lighting kept as-is (feeds SHADE + identity); FS replaces at blend=1 | Guard close-up A/B (`--level dam`, engage a guard): banding on limbs/face smoothed; sun-shadow received on characters; viewmodel untouched (WEAPON class excluded — roadmap §8); identity-off byte-identical | 6 | E2.T3, E3.T4 | R3: `GE007_CHR_NORMALS` |

### E7 — Bounded dynamic lights — 11d

| ID | Task | Files | Steps | Acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|---|
| W1.E7.T1 | Registry + emitters | `gfx_pc.c/.h` (`gfx_register_dynamic_light`, cleared per frame), `gun.c:3397+` (`portBuildFirstPersonFlashMatrix` flash site), `lightfixture.c` destroy path | §4.9; registration only from existing draw/effect sites; NOTE: the lamp-flicker emitter rides the `GE007_SHOOT_OUT_LIGHTS` destroy path (`lightfixture.c:110-124`) — W4.E1 has LANDED (default-off), so the path exists behind its flag (the ◆ default-ON flip, W4.E1.T4, is not a dependency); the muzzle-flash emitter has no such dependency | `scripts/ci/check_sim_render_separation.sh` green (`gfx_register_*` is the documented allowed submission family — checker header + roadmap line 301); R1: `tools/sim_invariance_gate.sh dam1 600 2` twice — plain vs `GE007_DYN_LIGHTS=1` exported — all hashes identical; diag: firing logs 1 light for flash lifetime; ≤4 enforced (5th dropped by distance) | 4 | — | R1: emitters are draw-path code, gate rerun; R3: `GE007_DYN_LIGHTS` |
| W1.E7.T2 | Shader add, both generators | generators (`1u<<31`), uniforms §4.4 | §4.9 loop; count uniform; zero-count draws keep bit clear (no shader churn) | Muzzle-flash capture: `GE007_RENDERER=metal GE007_DYN_LIGHTS=1 build/ge007 --rom baserom.u.z64 --level bunker1` — hold fire facing a wall; wall visibly lit during flash frames (screenshot pair archived); GL==Metal `--max-changed-pct 1.5`; identity-off byte-identical | 5 | E2.T3, E7.T1 | R3 |
| W1.E7.T3 | Perf + cap validation | shader-pool log line (GL: `shader_program_pool_size`, near `gfx_opengl.c:1381-1384`; Metal: the shader-pool counter in `gfx_metal.mm`) + captures | Add a shutdown log of shader-pool size under `GE007_VERBOSE=1`; manual stress on `--level bunker1`: 30 s sustained fire + destroy a lamp with `GE007_SHOOT_OUT_LIGHTS=1 GE007_DYN_LIGHTS=1` | `GE007_DYN_LIGHTS=1 tools/perf_census.sh bunker1` ≥ 90% of the fps from a plain `tools/perf_census.sh bunker1` run; logged shader-pool size flag-on < 2× flag-off; no visible hitch during the stress session | 2 | E7.T2 | — |

**Cross-workstream dependencies** (IDs per 00-MASTER-PLAN.md): **W2.E4** (AI material
sidecars + manifest/router, roadmap §6 P1.2/P2.3) decides *which* tokens get sidecars —
W1.E5 functions without it (hand-listed tokens) but production packs want it.
**W4.E1** (shoot-out-lights) has **LANDED** (default-off) — E7.T1's lamp-flicker emitter
depends on that landed code, not on the still-pending ◆ W4.E1.T4 default-ON flip (and
the muzzle-flash emitter is independent, so E7 does not block on it either way).
**W3.E2** (SSAO v2) should land before W1.M4 to avoid re-tuning AO under new lighting.
The sun-shadow + material look feeds any "showcase/trailer" workstream.

---

## 6. Milestones & deliverables

| M | Name | Contents | Demo script (reviewer runs) |
|---|---|---|---|
| M1 | **Faceted no more** (T1.3) — **code-complete** (◆ blend review pending) | E1 complete; Dam wall/ground seam fix | `GE007_RENDERER=metal GE007_ENV_SMOOTH_NORMALS=1 build/ge007 --rom baserom.u.z64 --level dam --deterministic --screenshot-frame 120 --screenshot-label m1_on --screenshot-exit`; re-run with `GE007_ENV_SMOOTH_NORMALS=0`, label `m1_off`; `tools/compare_screenshots.py screenshot_m1_off.bmp screenshot_m1_on.bmp --heatmap m1_diff.png` |
| M2 | **World in the shader** (T2.1+T2.2) — **half-complete** (E2 done on main; only E4 per-pixel sun, 5 jd, remains) | E2 + E4; per-pixel sun on rooms, dual-backend parity | `tools/renderer_parity_capture.sh --scene world_attrs`; then `GE007_RENDERER=metal GE007_PERPIXEL_LIGHT=1 build/ge007 --rom baserom.u.z64 --level dam --deterministic --screenshot-frame 120 --screenshot-exit` A/B vs the same command without the flag |
| M3 | **First real shadows** (T1.4) | E3; blob retired in remaster | `GE007_RENDERER=metal GE007_SUN_SHADOW=1 build/ge007 --rom baserom.u.z64 --level dam --deterministic --screenshot-frame 240 --screenshot-exit` (guard shadow in frame) |
| M4 | **Materials light up** (T2.3/T2.4) | E5; Dam pack with `_n/_r` sidecars | `tools/texpack/build_pack.py --emit-material-maps …` (local), then `GE007_RENDERER=metal GE007_TEXTURE_PACK=<pack> GE007_MATERIAL_MAPS=1 GE007_SUN_SHADOW=1 build/ge007 --rom baserom.u.z64 --level dam --deterministic --screenshot-frame 120 --screenshot-exit` |
| M5 | **Living light** (T2.5 + dyn) | E6 + E7; full `--remaster` flip review ◆ | `build/ge007 --rom baserom.u.z64 --remaster --level bunker1` + sustained fire; then the full §8 gate suite for the default-flip decision |

Each milestone independently shippable; program can stop after any of M1–M4 with a
coherent visual story (M2 is the only "plumbing-heavy" one, and E4 gives it a visible
payoff on the same day).

---

## 7. Risks & mitigations (ranked)

| # | Risk | Mitigation | Kill / de-scope criterion |
|---|---|---|---|
| R1 | **Luma-replace relight (E1/E4) fights authored mood** — GE bakes dramatic per-room lighting; replacing luma may flatten it | `EnvRelightBlend` is a dial, default 0.6; per-level grade presets already exist to compensate | If Dam+Surface+Bunker review says "flat" at every blend ≥ 0.3, de-scope to seam-fix-only mode: apply relight **only** where position-merged normals disagree with face normals (seam vertices), keep baked elsewhere |
| R2 | **Shadow acne/peter-panning unsolvable at single-cascade res** on GE's huge outdoor levels | Texel-snap + slope bias + front-face cull + PCF; radius dial | If bias sweep can't find acne-free + attached at 2048²/radius 60 on Dam+Surface, add the pre-approved 2nd cascade (E3+4d); if still failing, ship character-only shadow (cull ROOM casters), which is most of the visible win |
| R3 | **1-frame shadow latency reads as jitter** on fast camera pans | Sun is static; only the ortho *fit* lags — snap-fit hysteresis (refit only when camera moves > 1 texel) hides it | If a 240 fps capture review still shows crawl, pin the fit to player position (sim-read, render-side — allowed) instead of camera, eliminating pan-lag entirely |
| R4 | **Shader permutation explosion** — 4 new bits × existing options bloat the pool (GL pool linear-scan `gfx_opengl.c:1455`; Metal PSO cache) | Bits are class-gated (ROOM/CHRPROP only) and co-occur in fixed bundles in practice; log pool size in acceptance (E7.T3) | If pool > 2× baseline or level-load hitches appear, collapse `SUN_SHADOW+DFDX_LIGHT+DYN_LIGHTS` into one `LIT` bit with uniform-driven sub-toggles |
| R5 | **CPU cost of world-pos per vertex** (2 extra mat-vecs in the T&L hot loop) breaches the perf plan | Flag-off is branch-only; flag-on measured by perf census; the M1/M2 perf work bought 40%+ headroom (101–189 fps) | If remaster fps < 90 on any level, move world-pos computation to the pack loop (3 verts/tri, only for batched tris that survive clipping) |
| R6 | **Dynamic lights scope creep** toward the §8-deferred light system | Hard API cap N=4, two emitters only, no shadows, no persistence | Any request for authored/persistent lights ⇒ new workstream proposal, not an E7 extension; E7 itself is cut (not shipped half-way) if flash lighting reads badly in the M5 review |
| R7 | **Per-quad UV geometry defeats derivative tangents** (sparkle at seams) | §4.7 fallback: `\|duv\|` clamp → geometric normal; Dam wall is the acceptance fixture | If > 5% of Dam-wall pixels fall back (diag counter), leave `MATERIAL_MAPS` off for per-quad-UV tokens via a router exclusion list — exactly the §3 decision-tree posture |
| R8 | **Shadow capture misses frustum-rejected casters** (§4.5) — edge pop-in, no shadows from tall off-screen structures | Camera-centered fit bounds the loss; E3.T4 pan-capture characterizes it | If M3 review rejects edge-pop, capture at `gfx_sp_tri` pre-rejection (ROOM+CHRPROP, flag-on) and re-measure the ring budget; scene re-render stays out of scope |

---

## 8. Validation strategy (every task; commands verbatim)

Canonical environment (matches `tools/renderer_parity_capture.sh:101-118`; replace
`<task-id>` with your label — the screenshot lands as `screenshot_<task-id>.bmp` in the
current working directory):

```sh
mkdir -p /tmp/w1
SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1 \
GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
build/ge007 --rom baserom.u.z64 --level dam --deterministic \
  --trace-state /tmp/w1/trace.jsonl --screenshot-frame 120 \
  --screenshot-label <task-id> --screenshot-exit
```

Valid `--level` slugs are the `kPcStartStages` names (`main_pc.c:71+`), the same list
`tools/perf_census.sh` uses: `dam facility runway surface1 bunker1 silo frigate surface2
bunker2 statue archives streets depot train jungle control caverns cradle aztec egypt`.

Per-commit ladder (roadmap §7):

1. **Identity** — baseline (pin via `--config-override Video.RemasterFX=0
   --config-override Video.TexturePack= --config-override Video.RenderScale=1
   --config-override Video.MSAA=0`, all W1 flags off) vs feature build with flags off:
   screenshot SHA
   equal on **both** backends (`GE007_RENDERER=metal` and default GL) via
   `tools/renderer_parity_capture.sh`. Reminder from the P1.1 shipped notes: use
   `--level`, never `--ramrom`, for screenshots (ramrom renders black headless).
2. **Feature A/B** — only the target flag flipped;
   `tools/compare_screenshots.py <off.bmp> <on.bmp> --max-changed-pct <task threshold>` +
   `tools/audit_screenshot_health.py <shots…>` +
   `tools/audit_render_trace.py /tmp/w1/trace.jsonl`.
3. **Backend parity** — same flags, GL vs Metal, `--max-changed-pct` per task table
   (0.5–1.5). Metal is flagship; GL must not rot (it is the Linux/Windows reference).
4. **R1 gate** — the gate's own OFF/ON legs toggle only the screen-space pipeline
   (`Video.RemasterFX/Ssao` — see the script header), so for a W1 feature run it
   **twice**: `tools/sim_invariance_gate.sh dam1 600 2` plain, then again with the
   feature's env var exported (e.g. `GE007_SUN_SHADOW=1 tools/sim_invariance_gate.sh
   dam1 600 2` — the script's `env` launch inherits exported vars); all four printed
   hashes must be identical. `ctest -R sim_state_hash` in CI;
   `scripts/ci/check_sim_render_separation.sh` and `scripts/ci/check_timing_lock.sh`
   green (the E3.T5/E7.T1 game-TU touches are the audit hotspots).
5. **Sanitizers** — ASan/UBSan build, `tools/asan_smoke.sh` + the feature's demo
   script (hot paths touched: T&L loop, pack loop, new caches).
6. **Contamination guard** — `scripts/ci/check_no_rom_data.sh` (E5 sidecars are the
   exposure: all demo packs and screenshot refs stay local/gitignored).
7. **Perf** — `tools/perf_census.sh <level>` flag-on vs flag-off; budget ≥ 90% of
   baseline fps, no level below 90 fps absolute (headroom from
   docs/design/PERFORMANCE_PLAN.md).
8. **Broad gate** — `tools/playability_smoke.sh --all` before each milestone review.

---

## 9. Open questions (genuinely undecidable without the user)

1. **Default-flip policy**: which of these join `--remaster` (`platform_sdl.c:1960`)
   at M5 — all of E1–E7, or the conservative set (E1+E3+E4)? Same policy question the
   Dam remaster left open (VISUAL_MODES §3); needs a product call at the M5 checkpoint.
2. **Look target for relight strength**: `EnvRelightBlend` default 0.6 is my call from
   the Dam captures; the "how 2026 should it look" dial is taste — needs the user's eye
   at M1 (◆) before we tune E4/E5 on top of it.
3. **Sidecar packs for distribution**: do we want a shippable Tier-A1 sidecar set
   (procedural `_n/_r` from `synth_texture.py` presets) as a program deliverable, or are
   sidecars user-built-only like upscale packs? Affects E5.T5 scope (+3d for preset
   authoring) and the NOTICE story.
4. **GL minimum version**: shadow receivers use `sampler2DShadow` + `textureSize`
   (already used, `gfx_opengl.c:1020`) — fine on GL 3.2/150, but macOS GL stays the
   hang-prone translator. Do we gate the *GL* shadow path off on macOS (Metal-only
   there), accepting a platform feature gap until someone runs Linux? My default: yes,
   macOS-GL keeps E1 only.
