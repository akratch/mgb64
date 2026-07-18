# Dam Rendering Deep-Dive — full correctness audit (2026-07-18)

**Scope.** A read-only, evidence-first audit of the entire Dam rendering path across
the N64→host translation: endianness/pointer-width, the shared `gfx_pc.c` RDP
interpreter, the three backend emitters (WebGPU default, GL, Metal), projection /
viewport / depth, the color-combiner / blender / TMEM material machinery, room
admission, and float-contraction divergence — plus a catalog of N64 hardware
constraints that could be *released* for better-than-1:1 rendering.

**Method.** Nine parallel investigation lanes (six top-level + material sub-lanes),
each returning `file:line` evidence with an explicit confidence rating. Non-destructive
Dam captures only (audio muted, `--deterministic`); the existing `build/ge007` binary,
no rebuilds. **No code was edited in this pass** — every "fix" below is a proposal.

**Repo state.** The audit ran while a concurrent session advanced `main` from
`6834759` → `bbd3e4d` (Wave-4 perf: PERF-006 material-split, PERF-005 phase-2 pipeline
prewarm, PERF-052 audio worker, service worker). Only `gfx_pc.c` and `gfx_webgpu.c`
moved; `bg.c`, `fog.c`, `gfx_opengl.c`, `gfx_webgpu_shader.c` are unchanged. **All
line numbers below were re-anchored against current HEAD (`bbd3e4d`) for the
top-severity findings and confirmed accurate.** (Also on HEAD: the PERF-005b
present-gating fix from the earlier "portal bleed" hunt — relevant to §A.)

**How to read confidence.** `CONFIRMED` = mechanism proven in code (and, where marked,
reproduced in a capture). `PROBABLE` = mechanism proven, visible impact inferred.
`HYPOTHESIS` = plausible, not yet decisively shown. Per repo doctrine, any "is-this-
faithful?" question that needs the real console is marked **stock-verdict-pending** —
those require a stock-N64 (ares) capture before any change ships.

---

## Executive summary — ranked findings

| # | id | area | sev | conf | backends | one-liner |
|---|-----|------|-----|------|----------|-----------|
| 1 | **DAM-R1b** | sky/surface | **P1** | PROBABLE | web only | Browser scene target is an **sRGB** surface (fmt 23) → combiner bytes get gamma-encoded on store; horizon backdrop renders over-bright. Native (fmt 27 linear) is clean. |
| 2 | **TMEM-1** | texture | **P1** | CONFIRMED | all | Format **reinterpretation dropped** on the G_SETTEX texture-by-number path (decodes by pool fmt, ignores the tile fmt) → the "monitor flat-green" class. Only the byte-order half was ever fixed. |
| 3 | **DAM-R1a** | sky | P1→**refuted** | CONFIRMED | — | The standing "native over-bright backdrop" P1 **does not reproduce**; the old f190 measurement was intro-swirl animation phase-skew. Reclassify/close. |
| 4 | **BLEND-1** | blender | P2 | CONFIRMED | web broken | WebGPU never preserves the coverage-alpha channel (GL/Metal mask it) → interleaved XLU draws clobber stored N64 coverage. |
| 5 | **AC-3** | alpha | P2 | CONFIRMED | web broken | WGSL omits the `G_AC_DITHER` alpha-dissolve entirely; GL/Metal emit it. Default backend silently drops the effect. |
| 6 | **R-01 / R-02** | room admit | P2 | CONFIRMED / PROBABLE | all | DAM-R2 distant shore not admitted from the establishing cam; root cause = the retail authored PVS list is parsed at load but its consumer has **no live caller**. Stock-verdict-pending. |
| 7 | **TMEM-2** | texture | P2 | CONFIRMED | all | `settex_cache` is keyed on `texturenum` **alone** — the structural enabler of TMEM-1. |
| 8 | **AC-1** | alpha | P2 | CONFIRMED(mech) | all | `G_AC_THRESHOLD` is never decoded and `G_SETBLENDCOLOR` is a no-op → the alpha-threshold comparand doesn't exist. Latent (GE rarely uses it). |
| 9 | **FILT-1** | filtering | P2 | CONFIRMED | web vs GL/Metal | WGSL has no magnification nearest-fast-path; GL/Metal do. Source of the documented ~5/px WebGPU↔GL "filter noise" (WebGPU is the *more* faithful side). |
| 10 | **FMA-2** | float sort | P2 | PROBABLE | web vs native | Room-XLU sort epsilon `0.0001f` is **below float ULP** at Dam depths, defeating its own tie-break → coplanar-XLU blend-order flips web vs native. |
| 11 | **PVD-001** | depth | Med | PROBABLE | web (latent) | The WebGPU depth-clip **fallback** (adapter without `DepthClipControl`) hard-clips instead of clamping. Native M3 Max grants the feature → latent, not live. |
| 12 | **PVD-002** | depth | Low | CONFIRMED | Metal | Metal ZMODE_DEC decal bias constant (`-7.5e-6`) is ~15× GL/WebGPU's `-2·r` — over-biases coplanar decals. |
| 13 | **TMEM-4** | texture | P2 | PROBABLE | all | Runtime `G_LOADTLUT` can over-read the source palette image (no source-extent clamp; sibling of the confetti fix `48d835d`). |
| 14 | **TMEM-3** | texture | P2 | CONFIRMED | all | Static format-recovery decodes CI against a possibly-stale TLUT (self-flagged in-code). |
| 15 | **EN-1** | ILP32 | P3 | HYPOTHESIS | web (latent) | `gfx_resolve_addr` is segment-first/registry-second — the inverse of the ILP32-hardened `seg_addr`. No live trigger today. |
| 16 | **FMA-1** | float cull | P2 | CONFIRMED(mech) | web vs native | Backface-cull cross-product dead-band covers only CPU-clipped room tris; guard/prop/weapon geometry can flip culled↔drawn. |
| 17 | **DAM-R1c** | shader | P3 | CONFIRMED | web | WGSL combiner emitter is a partial port; `room_water_alpha_suppress` (Frigate, not Dam) is un-ported. |

Lower-severity / by-design items (AC-2/4/5, BLEND-3/4/5, FILT-3/4/5, TMEM-5, FMA-3/4,
PVD-003, R-03/04/05) are detailed in their sections. **Beyond-1:1 constraint-release
catalog (R1–R10)** is §F.

---

## A. The DAM-R1 reframe (flagship)

The long-standing top post-release item — an "over-bright rectangular sky/backdrop
seam on Dam, WebGPU-only" — **splits into three distinct facts**, two of which
overturn the prior ledger.

### DAM-R1a — the standing native P1 does NOT reproduce — CONFIRMED (refutes a P1)
On **native** WebGPU vs GL, stationary Dam backdrop / distant-mountain / water pixels
are byte-identical to ±1 LSB (pad-70: mountain WG `[24.4,54.2,97.5]` vs GL
`[24.2,54.0,97.3]`; water WG `[14.5,41.1,61.0]` vs GL `[14.6,41.3,61.2]`). The large
frame-190 divergence the predecessor hunt measured is the **intro swirl animating** —
a sub-frame phase difference between the two backend runs lights every silhouette
edge, and the divergent pixels go *both* directions (brighter and darker), which a
one-directional shading bug cannot produce. Native surface format is `27 = BGRA8Unorm`
(linear), so there is no gamma step to diverge from GL. **Action: close the native
DAM-R1 as not-a-defect / camera-timing artifact; the 634525d "WGSL-vs-GLSL shading"
reframe was chasing a ghost on native.**

### DAM-R1b — WEB build sRGB surface double-encodes the scene — **P1 (web-only)**, PROBABLE
The genuine over-bright is browser-only. `wgpu_choose_format` (`gfx_webgpu.c:461`) takes
`caps.formats[0]` **unconditionally** on web (the WEB-049 "prefer first" path); on a
browser that first format is **`23 = RGBA8UnormSrgb`** — an sRGB format. The offscreen
scene target adopts `s_surface_format` (`gfx_webgpu.c:955`, `:982`, `:3000`: *"must match
the scene for T2T copy"*), so the WGSL combiner's raw N64 output — values authored in
the display domain, exactly as GL writes them to its **linear** default framebuffer —
is written into an sRGB attachment and **gamma-encoded on store**. That lifts darks,
turning the dim-blue horizon backdrop bright. GL never encodes, so it stays faithful;
the defect is WebGPU-only because only the web surface is sRGB. `wgpu_format_is_bgra`
(`gfx_webgpu.c:574`) treats the sRGB and non-sRGB BGRA variants identically — no code
path strips the sRGB variant.

- **Independent confirmation (this audit, main agent):** the surface-format asymmetry
  is real — the browser console logs `[webgpu] backend initialized (surface format=23)`
  (sRGB) while native logs `27` (linear). A same-backend A/B (browser vs native
  WebGPU, differing *only* by surface format) at the Dam spawn sky shows the browser
  patch **~20 levels/channel brighter** (`[32,33,35]` vs native `[12,13,15]`) — the
  direction DAM-R1b predicts.
- **Caveat (do not over-claim):** the observed lift is *milder* than a clean single
  linear→sRGB store-encode would produce (a full encode of `(15,30,52)` → ~`(69,113,151)`,
  but the ledger/observed value is ~`(19,50,98)`). So the exact gamma path (partial
  encode, copy-only, or a blend interaction) is not yet pinned. Hence **PROBABLE**, not
  CONFIRMED.
- **Proposed fix (exact, web-only):** decouple the scene byte-path from the sRGB
  surface. Add `s_scene_format` = the non-sRGB pair of `s_surface_format` (23→22, 28→27,
  else identity); render the offscreen scene target and all 3D color targets in it;
  declare that format in the surface `viewFormats` and create the present-copy surface
  view with `format = s_scene_format` (a legal non-sRGB view of the sRGB swapchain).
  The blit then moves bytes verbatim, matching GL. *Rejected weaker alt:* scanning for a
  non-sRGB format in `wgpu_choose_format` — browsers routinely advertise **only** the
  sRGB canvas format, so the view-override is the robust path.
- **Regression risk:** Low. Native is already linear (`s_scene_format == s_surface_format`,
  identity); the combiner/fog WGSL is untouched, so native baselines stay byte-identical.
  Risk surface is web-only present-copy `viewFormats` validation.
- **Promotion experiment (PROBABLE→CONFIRMED):** log `caps.formats[0]` on the web build,
  confirm it is 23, and verify a non-sRGB scene target drops the backdrop from ~`(19,50,98)`
  to GL's ~`(15,30,52)`.

> **Note on the user's original screenshot.** The "portal bleeding" the user first
> reported was a *different* web-only issue — missing geometry (sky through walls) from
> PERF-005 async-pipeline draw drops, already root-caused and fixed (PERF-005b
> present-gating, now on HEAD). DAM-R1b (over-bright backdrop) is a separate web defect
> that would have compounded the visual. They are independent.

### DAM-R1c — WGSL combiner emitter is a partial port — P3, CONFIRMED (not the Dam cause)
`gfx_webgpu_shader.c` omits several post-combiner fragment effects GL/Metal emit. Most
are diag-gated/default-off, but `room_water_alpha_suppress` is not: GL/Metal force the
shell alpha to 0 (`gfx_opengl.c:1360`, `gfx_metal.mm:639`), WGSL never references it, so
on WebGPU a suppressed shell renders at authored alpha. The predicate is **Frigate**-
specific (`settex_texturenum==655`), not Dam, and sky-tris are excluded from the class
(`gfx_pc.c:5683` returns `"sky"`), consistent with the sub-LSB native Dam match. Also
WGSL-absent: the `opt_noise` final-alpha gate (see **AC-3**, same root), `diag_*` scales,
`opt_dfdx_light` relight. **Fix:** port `room_water_alpha_suppress` + the noise gate into
the WGSL emitter after the combiner clamp. Risk very low.

**Cross-lane reconciliation.** Two other lanes independently touched DAM-R1 and were
*superseded* by the dedicated lane: the blender lane guessed the seam was the WebGPU
viewport/scissor clamp — but the projection lane proved viewport/scissor has a unique
`dx=0, dy=0` shift minimum (no offset), and DAM-R1a proves native reproduces nothing.
The FMA lane cited a stale doc ("Y-flip edge-ownership"), already refuted in the
`634525d` ledger. **DAM-R1b (sRGB) is the authoritative mechanism.**

---

## B. Confirmed correctness defects

### TMEM-1 — format reinterpretation dropped on the G_SETTEX path — **P1**, CONFIRMED (code-verified by main agent)
The "monitor flat-green" class is **not** generalized-fixed. On the texture-by-number
path, `gfx_handle_settex` decodes strictly by the pool's catalogued format —
`fmt = tex->gbiformat; sz = tex->depth` (`gfx_pc.c:22616-22617`, **verified at HEAD**) —
and passes those to the decoder; the DL's `G_SETTILE` tile format is used only for tile
*config* (wrap/mask/shift), never for decode. So a `texSelect` that re-tiles a
CI4-catalogued texture through an I8 tile (raw-TMEM-bytes reinterpretation, e.g.
`IMAGE_MONITOR_TEXT`) is still decoded as CI4 → flat green. Reinterpretation *is*
honored on the traditional LOADBLOCK/LOADTILE path (`import_texture` reads fmt/siz from
`rdp.texture_tile[tile_desc]`, the real G_SETTILE) — the gap is specific to the settex
path GE monitor/texSelect materials use. The only related fix (`fb59c95`,
`gfx_palette_entry_to_rgba32`) corrected the IA16 TLUT **byte order** — a different half
of the monitor defect. **This contradicts the "GENERALIZED" claim in the prior DAM
parity deep-dive note — flagged as a discrepancy to resolve; the code shows the
reinterpretation half is still open.**
- **Fix:** when the settex `G_SETTILE` fmt/siz disagree with `tex->gbiformat/depth`,
  decode by the *tile* fmt over the raw `tex->data` bytes (or pre-decode dual-format
  variants). Requires TMEM-2 first.
- **Risk:** Medium (depends on the cache key fix).

### TMEM-2 — `settex_cache` keyed on `texturenum` alone — P2, CONFIRMED (verified at HEAD)
The lookup matches `settex_cache[i].texturenum == texturenum` (`gfx_pc.c:22571`,
**verified**) with no fmt/siz/palette component, so the same texturenum through two tile
formats or two TLUTs returns the first decode. This is the structural blocker for
TMEM-1. **Fix:** add tile fmt/siz (+ CI palette identity) to the settex key. Risk low-med.

### BLEND-1 — WebGPU never preserves the coverage-alpha channel — P2, CONFIRMED
The RDP-CVG-memory path stores a synthetic 3-bit coverage in the scene-target **alpha**
and a later draw reads it back to emulate N64 `CVG_DST_WRAP`. GL masks alpha off for
interleaving normal XLU draws (`gfx_opengl.c:1905`, `glColorMask(T,T,T,FALSE)` under the
cvg-memory predicate); Metal mirrors it (`s_preserve_cov_alpha`, `gfx_metal.mm:3368`).
WebGPU **hard-codes `writeMask = All`** (`gfx_webgpu.c:2079`, **verified at HEAD**) with
no gate, so any ordinary alpha/modulate draw between two CVG-memory draws clobbers the
stored coverage. The path is a live default (room-cvg-memory defaults on; Dam has fogged
secondary-room backdrop strips). Contra the stale comment at `gfx_webgpu.c:2006`, the
path *is* ported. **Fix:** gate the WebGPU `writeMask` to RGB-only under the same
predicate, threading a preserve flag into the pipeline key (currently blend|depth|decal
only). **Risk medium** (adds a PSO variant; keep GL byte-identical).

### AC-3 — WGSL omits the `G_AC_DITHER` alpha-dissolve — P2, CONFIRMED
`use_noise` (`gfx_pc.c:17959`, **verified**) → `SHADER_OPT_NOISE`. GL and Metal seed
`needs_noise = cc.opt_noise` and emit the dissolve (`gfx_opengl.c:1337`,
`gfx_metal.mm:628`); **WGSL seeds `needs_noise = false`** (`gfx_webgpu_shader.c:307`,
**verified**) and has no alpha-dither line. The default/shipping backend silently drops
the stochastic ~50% dissolve on PCL_SURF particle/dissolve modes (watch-menu fade
`watch.c:2873`, BG particle LUT `bg.c:3247`). **Fix:** seed `needs_noise` with
`cc.opt_noise` and emit the alpha-multiply after the texture-edge block using the
existing noise uniform. Risk low (additive, gated). *Sub-note:* the upstream `alpha *=
floor(random+0.5)` form kills ~50% independent of alpha vs N64's alpha-vs-random compare
— a faithfulness approximation present on GL/Metal, absent on WebGPU.

### AC-1 — `G_AC_THRESHOLD` never decoded; blend-color register dropped — P2, CONFIRMED(mech)
The interpreter decodes only `G_AC_DITHER` (bits==3, `gfx_pc.c:17959`); there is **no**
`== G_AC_THRESHOLD` (bits==1) branch anywhere, and `G_SETBLENDCOLOR` (0xF9) is a no-op in
both interpreters (`gfx_pc.c:23917`, `:24959`, **verified**), so `rdp.blend_color` — the
threshold comparand — is never captured. A threshold path is unimplementable today. GE
uses `G_AC_NONE`/`G_AC_DITHER` and no explicit `G_AC_THRESHOLD` was found (search not
exhaustive → latent, not a visible Dam regression; Dam alpha-kill runs through the
texture-edge 0.19 path instead). **Fix:** capture `rdp.blend_color` in the 0xF9 handler;
decode `(other_mode_l & 3) == 1` into a shader option; emit `if (a < uBlendColor.a)
discard`. Risk low (gated on the exact bit pattern).

### FILT-1 — WGSL lacks the magnification nearest-fast-path — P2, CONFIRMED
GL/Metal wrap the N64 3-point in `if (footprint < nearest_threshold) return nearest(...)`
(`gfx_opengl.c:1119`, `gfx_metal.mm:351`); WGSL's `n64Filter3` has no dFdx branch and
*always* 3-points (`gfx_webgpu_shader.c:349`). This is the exact source of the documented
~5/px WebGPU↔GL establishing-shot "filter noise." Note WebGPU is the **more** faithful
side (real RDP 3-points in both mag and min); the GL/Metal nearest-snap under
magnification is itself a deviation (FILT-2). **Fix:** converge the three — either port
the footprint gate into WGSL (needs a filter-scale uniform WebGPU doesn't yet ship) or
drop the nearest path from GL/Metal (cleaner for fidelity, slightly costlier). Risk
medium/low respectively. *The 3-point math itself is verified byte-identical and
correct across all three backends + a CPU reference (see §E).*

---

## C. Room admission / DAM-R2

### R-01 — DAM-R2: distant reservoir shore not admitted from the establishing cam — P2, CONFIRMED (stock-verdict-pending)
Reproduced: `GE007_INTRO_CAMERA_INDEX=4` resolves to room 71; the shore-bearing far
rooms classify `class=far adj_rendered=-1` — frustum-visible but **not portal-adjacent**
to any rendered room. Every port widener (edge-rescue, visibility-supplement, camera-seed
multi-hop) is portal-adjacency-gated, so none can cross the open-water gap;
`GE007_CAMERA_SEED_WALK_HOPS=16` raised the draw-only set 10→39 (more *near* geometry)
but the shore stayed absent. The geometry renders fine from the gameplay walkway because
it *is* portal-reachable there. Classifier logic `bg.c:16894`. **Fix:** admit authored
far-vista rooms via the sim-neutral `g_BgRoomDrawOnly` set — best by wiring R-02.
**Risk medium** (shared machinery; re-run intro oracles on every level).

### R-02 — authored global visibility list (opcode-100 PVS) parsed but never consumed ← likely root cause — P2, PROBABLE
Retail admits exactly these far rooms via per-room PVS lists. The port **fixes up** the
opcode-100 table (`bg.c:5330`) but its consumer `sub_GAME_7F0B38B4` (`bg.c:3456`) has
**no live caller** (grep: only the definition + LEFTOVERDEBUG asm) — the port replaced
authored PVS with portal-BFS + heuristic wideners, which structurally cannot reach `far`
rooms. The C body also carries **FID-0121** (processes the first vis-string from the
matched byte position rather than resetting to pair-start), so it would under-admit even
if wired. **Fix:** drive `sub_GAME_7F0B38B4` (FID-0121-corrected) over the camera/current
room and feed admits into `g_BgRoomDrawOnly` (NOT `room_rendered` — retail feeds
`room_rendered`, which perturbs RNG; the port must stay draw-only). **Risk medium-high**
(new admission source; validate no over-admission of the train-sky-leak / room-14 far-
shard class). **Stock-N64 capture at `INTRO_CAMERA_INDEX=4` required before shipping.**

### R-03/04/05 (supporting) — CONFIRMED / HYPOTHESIS
- **R-03:** the visibility-supplement adjacency gate (`bg.c:2241`) makes `far` rooms
  permanently unreachable — deliberate (removing it reintroduced room-14 far-valley
  shards). Correct answer is R-02, not loosening the gate.
- **R-04:** `GE007_CAMERA_SEED_WALK_HOPS` can't admit far rooms (per R-03) and over-admits
  at high counts — leave default 0; not the DAM-R2 lever.
- **R-05:** the far-plane cap `maxZ = zrange[1]/mCurrentLevelVisibilityScale` (`bg.c:7443`)
  is a hard N64 draw-distance cap but **not** the DAM-R2 blocker (shore rooms pass the
  frustum). Sim-visible; any release must be draw-only. Stock-verdict-pending.
- **Negative results (CLEAN):** no admission hysteresis/flicker (draw-only set stable
  every frame); no stale room state across the camera cut.

---

## D. Latent / defensive items

- **PVD-001 — WebGPU depth-clip fallback incomplete — Medium, PROBABLE (web latent).**
  `gfx_init` forces `g_depth_clamp_enabled = true` for WebGPU (`gfx_pc.c:25082`), so the
  CPU clipper omits depth-plane clipping and relies on the GPU to *clamp*. But when the
  adapter lacks `DepthClipControl`, pipelines are built `unclippedDepth = FALSE`
  (`gfx_webgpu.c:708`) and the GPU *clips* instead — with no compensating path (WGSL
  doesn't apply GL's `z*=0.3` squash). **Native M3 Max grants the feature (capture logged
  the "lacks" warning 0 times; Dam far cliff intact), so this is latent, not live.**
  **Fix:** on the featureless branch, route near/far-crossing tris through the CPU
  clipper (make `needs_view_clip` include `needs_depth_clip` when the GPU can't clamp) —
  smaller and hash-neutral than lowering the global. Risk low-med.
- **EN-1 — `gfx_resolve_addr` segment-first ordering — P3, HYPOTHESIS (web latent).**
  `gfx_ptr.h:226` resolves a segment nibble before the registry (no host-pool guard) —
  the inverse of the ILP32-hardened `seg_addr` (`68afbc6`). No live trigger: the N64-interp
  call sites feed genuine big-endian segment tokens; the texture site checks the registry
  first; the one host-pointer site is diagnostic-only. **Fix (defensive):** mirror
  `seg_addr`'s registry→host-pool→segment order on ILP32, width-guarded. Risk very low.
- **TMEM-4 — runtime `G_LOADTLUT` source over-read — P2, PROBABLE.** The runtime branch
  (`gfx_pc.c:21740`) clamps `count` only to `256 - palofs`, never bounding `base+count`
  to the source image, so non-zero `uls/ult` or an undersized source over-reads (≤512 B,
  rare). Sibling of the confetti class (`48d835d`), on the palette source. **Fix:** mirror
  the static branch's source-extent clamp. Risk low.
- **TMEM-3 — static CI format-recovery vs stale TLUT — P2, CONFIRMED (self-flagged).**
  The `import_texture` recovery block (`gfx_pc.c:15852`) can decode a recovered CI format
  against `rdp.palette_addrs` from a *separate* G_LOADTLUT — the in-code comment warns of
  it. **Fix:** re-fetch via `texGetPalette` when the recovered fmt is CI. Risk low.
- **FMA-1 — cull cross-product dead-band gap — P2, CONFIRMED(mech).** The backface-cull
  sign test (`gfx_pc.c:19514`) is a contractible `dx1*dy2 - dy1*dx2`; the existing
  `|cross|<0.03` dead-band (`:19519`) covers only CPU-clipped room tris, leaving
  guard/prop/weapon geometry to flip culled↔drawn web-vs-native at grazing angles
  (transient slivers). **Fix:** extend the dead-band to all tris, or compute in double,
  or compile `gfx_pc.c` `-ffp-contract=off`. Risk low.
- **FMA-2 — XLU sort epsilon below ULP — P2, PROBABLE (strongest persistent web-vs-native
  candidate).** The room-XLU sort key is mean vertex `w` (500–10000 units); the comparators
  use an **absolute** `epsilon = 0.0001f` (`gfx_pc.c:14235`, `:14359`, **verified**). Float
  ULP at `w=2048` is ~2.4e-4 > epsilon, so beyond depth ~1000 two exactly-equal-depth
  groups (coplanar glass, split water) can differ by 1–4 ULP from transform contraction,
  exceeding epsilon on one target but not the other → **persistent** blend-order flip
  web vs native. **Fix:** relative epsilon `fmaxf(1e-4, 4e-7·max|key|)` or quantize keys
  before comparing. Risk low.
- **PVD-002 — Metal ZMODE_DEC decal bias magnitude — Low, CONFIRMED.** Metal's
  `-7.5e-6` constant (`gfx_metal.mm:3502`, Depth32Float) is ~15× GL/WebGPU's `-2·r`,
  over-biasing coplanar decals. GL≡WebGPU. **Fix:** recompute for Depth32Float
  (`-2 units ≈ -4.8e-6`) or normalize to slope-only. Risk low (macOS non-default backend).

---

## E. Verified-CLEAN areas (with evidence)

The great majority of the stack is correct — recording it prevents re-hunting.

**Projection / depth.** The `[-w,+w] → [0,+w]` half-Z remap is a single site
(`gfx_pc.c:20205`) correctly paired with each backend's clip convention (GL keeps
`[-w,w]`; Metal/WebGPU `[0,w]`) — no half-Z error. Viewport/scissor is computed once as
float and truncated to int32 **before** backend dispatch, so all three receive identical
rects (A/B shift-correlation: unique `dx=0,dy=0` minimum). `wgpu_clamp_rect` odd-size
logic is correct. PerspNorm (`G_MW_PERSPNORM`) is intentionally a no-op for a float host
T&L; retail perspnorm is still computed game-side; no w under/overflow at Dam scale
(guarded `fabsf(w)<1e-4`). Near-plane epsilon is near-only (avoids cracks). Far geometry
is submitted+clamped (faithful, not vanished).

**Endianness / pointer-width.** ROM/DL decode uses a correct dual-interpreter split
(host-endian `Gwords` for PC DLs, `read_be32` for ROM DLs) — no path byteswaps
host-built words. G_VTX/G_MTX N64 decode assemble big-endian byte-wise. All texel
decoders (RGBA16/32, IA/I, CI) either `memcpy` host-order statics or explicitly unpack
big-endian ROM data; palette entries are unpacked from a `uint16_t` value
(endian-independent) and G_LOADTLUT byteswaps ROM palettes once at load. The `gfx_ptr`
registry (65536 slots, open-addressed, per-stage clear) covers every dyn-pool/segment
pointer; the confetti over-read class (`48d835d`) is structurally closed
(decode-footprint plausibility backstop). No new unregistered pointer-bearing structure
entered the RDP token path this week.

**This week's perf work — all CLEAN (independently audited):**
- **PERF-013** (combiner hash index, `8328836`): **collision-safe.** The hash mixes both
  identity fields (`cc_id` u64, `cc_options` u32) but is only a probe-position hint; the
  match is a **full-key compare on both fields** (`gfx_pc.c:14981`), and every other
  combiner field is a pure function of that pair — a hash collision costs an extra probe,
  never a wrong combiner. Table load-factor ≤0.5 (always terminates); null-index falls
  back to linear scan. Byte-behavior-identical to the prior scan.
- **PERF-014** (SetPipeline/SetBindGroup dedup, `4765099`): **no ABA / safe.** The
  trackers are pass-scoped (reset to NULL at every `s_pass` begin) and only compare a
  handle that is still alive and bound (the render-pass encoder retains resources until
  submit on both Dawn and wgpu-native), so a recycled address always passes through a
  fresh `SetBindGroup`. Distinct from the WEB-068 ABA, which bit a *persistent* cache.
  The modern-mesh interleave correctly forces re-bind (`s_bg_applied = NULL`).
- **PERF-015/016** (`730a218`): store float offsets into a movable arena, materialize the
  pointer only post-growth — pointer-width clean. PERF-008/014 aren't token-bearing.

**Materials.** The N64 3-point filter math is byte-identical and correct across GLSL,
WGSL, MSL, and a CPU reference (canonical triangle filter, correct diagonal split and
texel-center convention); the host sampler is forced to NEAREST when the shader 3-point
is active (no double-filter). HW blend factors match GL exactly across backends
(ALPHA→(SRC_ALPHA,1−SRC_ALPHA), MODULATE→(DST,ZERO)); fog is a per-vertex attribute mixed
identically in all three fragment shaders (fog is **not** a cross-backend blend
divergence). The texture-edge cutout threshold (0.19, force-opaque) is identical across
backends. Palette/CI cache keying includes palette addr, bank, and LUT mode (no
aliasing); IA16-vs-RGBA16 TLUT decode is correct and ROM-free-tested. Output Bayer dither
(anti-banding) is consistent and opt-in across backends. Cross-draw memory-blend
snapshot stacking is correct on all three (intra-*batch* overlap is a shared limitation,
GL alone offering a `pertri` A/B).

**Lower-severity / by-design (not defects):** AC-2 (0.19 constant is tuned, consistent),
AC-4 (RGB dither deliberately post-FX only), AC-5 (GL A2C path dormant for GE content),
BLEND-2 (no A2C under MSAA — WebGPU is single-sample anyway), BLEND-3 (coarse blender-mode
mapping is intentional), BLEND-4 (per-batch snapshot granularity, shared), FILT-3/4/5
(2D/HUD not 3-pointed; clamp-edge tap wrap — all backends equally), TMEM-5 (heuristic
per-triangle LOD), FMA-3/4 (shard/frustum thresholds — bounded ±1-frame), PVD-003 (depth
format precision asymmetry).

**Partial (session-interrupted, follow-up):** the struct-layout/bitfield ILP32 sub-lane
returned a promising-CLEAN anchor (the `Gfx` command sub-unions are correctly gated
`#if IS_BIG_ENDIAN && !IS_64_BIT`; `Vtx` is force-aligned) but did not finish its full
sweep. Recommend a short follow-up.

---

## F. Beyond-1:1 constraint-release catalog (ranked by win-per-risk)

All proposals are **draw-only** and fit the existing `GE007_*` / `Video.*` flag
architecture (faithful preset stays byte-faithful; identity at the default value).

> **Two hard seams to never cross** (both break faithfulness/sim gates):
> (1) `g_ViBackData->zfar` / `g_ScaledFarFogIntensity` are **shared with AI sight**
> (`fog.c:756`) — the far plane and AI range are the *same number*; always use a
> render-local copy or the cosmetic `Video.FogDensity` multiplier (which the port already
> correctly decouples). (2) Room-admission and model-LOD changes must pass the
> `sim_state_hash` invariance gate because of the `room_rendered`→auto-aim coupling
> (`chr.c ~5205`).

| id | release | win at Dam | risk | effort |
|----|---------|-----------|------|--------|
| **R1** ★ | Honest horizon — remaster-default `Video.FogDensity ≈ 0.55–0.7` outdoors (mechanism already merged, cosmetic-only, proven decoupled from AI) | fog wall recedes; reservoir reads as real distance | very low | **S** |
| **R3** | `Video.Anisotropy` (+ optional mip for ROOM textures) in sampler creation | ground/cliff stop shimmering | low | S–M |
| **R5** | `Video.SmoothSky` — skip the `Sky.{R,G,B} &= 0xf8` 5-bit mask (`fog.c:690`), let output dither finish | banded blue sky → smooth gradient | low | **S** |
| **R2** | Extended draw distance via the existing draw-only supplement (bump `GE007_VIS_SUPPLEMENT_MAX_EXTRA` 12→24) — the DAM-R2 lever | recovers distant reservoir geometry | low-med | S–M |
| **R8** | Prop distance-fog cull softening (rides R1's lower density; `fogGetPropDistColor` is render-only) | distant props fade in, not pop | low | S |
| **R10** | `Video.NoiseDitherScale` — attenuate alpha-noise grain at high RenderScale | cleaner fades at 2×+ | low | S |
| **R4** | Extend HD-pack coverage to hash-keyed dynamic/sky/effect textures (scoped P2.1) | sharp sky + muzzle/smoke | med | M |
| **R7** | `Video.RenderFarScale` — multiply *only* the render-local `far` in `guPerspectiveF` (never the shared zfar) | modest at Dam, large on long-sightline stages | low-med | M |
| **R6** | `Video.LodDistanceScale` → `g_ModelDistanceScale <1` to hold detailed meshes farther | props keep silhouettes, no LOD pop | med (needs invariance gate) | M |
| **R9** | Bypass the projection-matrix `F2L→L2F` fixed-point round-trip (float matrix already captured) | steadier far edges at 2×+ SSAA | low-med | M |

**Ship-first trio (all Small, all provably draw-only, all fix visible baseline
artifacts):** R1, R3, R5.

---

## G. Recommended sequencing

1. **DAM-R1b (sRGB scene-target)** — the flagship user-visible web defect; well-scoped,
   native-safe, and pairs with the already-landed PERF-005b to clear the Dam-startup
   visuals. Do the promotion capture first.
2. **TMEM-2 then TMEM-1** — the monitor-text reinterpretation class; TMEM-2 (cache key)
   unblocks TMEM-1. Highest-value *faithfulness* fix; all backends.
3. **AC-3 + DAM-R1c (WGSL `opt_noise` / suppress)** — cheap, additive, closes two WebGPU
   parity gaps in one shader edit.
4. **BLEND-1 (coverage-alpha writeMask)** — medium risk (PSO variant); A/B glass/water on
   all backends.
5. **R-02 (authored PVS) → R-01 (DAM-R2)** — gated on a **stock-ares capture** at
   `INTRO_CAMERA_INDEX=4` to confirm retail draws the shore. This is the one item that
   must not proceed without a hardware reference.
6. **Latent hardening** (PVD-001 fallback, TMEM-3/4 clamps, EN-1 ordering, FMA-1/2
   epsilons) — batch as defensive fixes; each is small and low-risk.
7. **Beyond-1:1 R1/R3/R5** — the visible remaster wins.

**The one blocking dependency for the whole program:** a stock-N64 (ares) Dam capture
set — establishing camera, intro swirl, and the reservoir walkway — to adjudicate every
`stock-verdict-pending` item (DAM-R2, DAM-R3, R-05, the far-plane caps). The ares binary
exists at `build/ares-movement-oracle/…/ares.app`; run it **alone** (tape/oracle gates
false-fail under capture contention). Until then, DAM-R2-class changes are proposals, not
committed fixes.

---

## Appendix — evidence & repro

- **Captures** (per lane): `…/scratchpad/audit-sky/`, `audit-rooms/`, `audit-proj/`,
  `audit-beyond/`, plus the main-agent browser/native A/B in `…/scratchpad/webrepro/`
  and `…/scratchpad/evidence/`. Consolidated lane reports: `…/scratchpad/reports/`.
- **Deterministic Dam capture:** `SDL_AUDIODRIVER=dummy GE007_MUTE=1
  [GE007_ENABLE_LEVEL_INTRO=1] build/ge007 --rom baserom.u.z64 --level dam --no-ui
  --deterministic --savedir sd --screenshot-frame N --screenshot-exit`; `GE007_RENDERER=gl`
  for the A/B; `GE007_INTRO_CAMERA_INDEX=4` for the establishing cam.
- **In-browser repro harness** (main agent): `…/scratchpad/webrepro/webcap.mjs` — headless-
  Chrome CDP boot + ROM inject + `?arg=` CLI passthrough + key-hold scripting + CPU
  throttle + burst capture. Used to confirm the DAM-R1b surface-format asymmetry.
- **Repo state:** audited at `main` moving `6834759`→`bbd3e4d`; top-finding line numbers
  re-anchored and verified at `bbd3e4d`. No code was modified in this pass.
