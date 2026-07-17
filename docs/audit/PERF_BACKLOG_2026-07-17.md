# Performance Backlog — Ranked (2026-07-17)

Derived from `docs/audit/PERF_REVIEW_WEB_NATIVE_2026-07-17.md` (6-lane audit). Every code
snippet below was re-read and verified against the working tree at HEAD `35d4449` on
2026-07-17 — line numbers are current, not inherited from the review.

**Ranking rule** (per owner direction): *universal* items — those that benefit both the
native build and the browser build — rank above platform-specific items, regardless of raw
impact. Within each tier, items are ordered by impact-per-effort. One consequence worth
naming: PERF-030 (web RenderScale) would be #1 overall on raw impact/effort, but it is
web-only, so it leads Tier B instead.

**Ground rules for every item**
- The 60 Hz sim is bit-faithful and untouchable. Every item here is render/platform-side;
  items with any coupling risk say so explicitly.
- Validation harness: `tools/perf_census.sh` + `GE007_PERF_TRACE=1` (re-record local
  baselines after intentional render changes), screenshot/oracle suites,
  `tools/sim_invariance_gate.sh`, tape gate, `GE007_AUDIO_DUMP` (audio byte-identity),
  `web_boot_smoke` + wasm size budget (web). House rule: >2% frame-time regression fails
  review (`docs/BACKLOG_v0.4.0.md:55`).

## Execution status — Wave 1 (2026-07-17, branch `perf/wave1-quick-wins`)

Wave 1 (universal quick wins + the S-effort web/native items) executed end to
end. Every landed item was validated against its gates (render parity /
sim-state / settex / env-ref / web boot smoke as applicable) with no
regressions; `port_renderer_parity_smoke` stayed byte-identical across all the
render-path changes.

| ID | Status | Commit | Note |
|----|--------|--------|------|
| PERF-002 | **LANDED** | `fb3322f` | dbg_* gate is a strict superset of every consumer (incl. `g_diag_trace_eye_bind`); render byte-identical |
| PERF-003 | **LANDED** | `755621f` | slot+1 direct index, re-verify safety net, evict-reindex, lockstep clears; behavior-identical |
| PERF-050 | **LANDED** | `7207971` | nanosleep tail on macOS/Linux; Windows keeps the original spin; sim-neutral |
| PERF-030 | **LANDED** | `639cffc` | web joins the PortMaster native-res carve-out; native unchanged; web boot verified |
| PERF-010 | **LANDED** | `e79d871` | `Audio.QueueTargetFrames` LIVE setting; default 1.5 byte-identical; control surface only (auto-derivation deferred) |
| PERF-032 | **LANDED** | `ba69d94` | `instantiateWasm` streaming + pre-gate fetch; WEB-031/032/033 preserved; boot smoke green |
| PERF-033 | **LANDED** | `0091980` | INITIAL_MEMORY 256→128 MiB + 512 MiB ceiling; boot smoke green |
| PERF-061 | **LANDED** | `8fb4393` | ENV_FLAGS drift reconciled (`GE007_WEBGPU_TRACE_VIEWPORT` — `env_reference_current` was red on `main`) |
| PERF-001 | **DEFERRED** | — | a provably-correct flush-skip needs a non-mutating cache resolve (all 14 key fields, incl. CI palette-addr); that resolve/bind split *is* PERF-006, so it folds into that wave rather than shipping a risky standalone batching change |

Not a backlog item but a blocker found and fixed en route: **`fix(webgpu)
57c3638`** — `f694eab` (depth-clamp) used wgpu-native's `WGPU_TRUE`/`WGPU_FALSE`
macros, which the emdawnwebgpu header lacks, so the **web build was broken on
`main`** (native compiled; the next web deploy would have failed). Defined at
the compat seam; web build + `web_boot_smoke` green again.

Native perf on the M3 Max dev box is dominated by run-to-run census noise
(±20% at 200–550 fps); these micro-opts target min-spec native + wasm where CPU
binds, so they were validated by correctness gates + mechanism, not by a native
census delta on this hardware.

## Index

| ID | Title | Scope | Impact | Effort |
|----|-------|-------|--------|--------|
| PERF-001 | Skip redundant texture-rebind flushes | universal | Med-High | S |
| PERF-002 | Gate per-vertex debug payload writes | universal | Med | S |
| PERF-003 | SETTEX cache: linear scan → direct map | universal | Med | S |
| PERF-004 | Hoist per-triangle diag gates; cull first | universal | Med-High | M |
| PERF-005 | Shader/pipeline prewarm + async create | universal | High(web)/Med(native) | M |
| PERF-006 | Material-derivation split in triangle emit | universal | High | L |
| PERF-007 | Memory-blend pass-split coalescing | universal (TBDR) | Med-High | M |
| PERF-008 | Present without the extra full-res copy | universal | Med | M |
| PERF-009 | Skinned-character matrix rebuild dedupe | universal | Med | M |
| PERF-010 | Adaptive audio queue target | universal | Med | S |
| PERF-011 | DSP inner-loop telemetry hoist / vectorize | universal | Med(wasm)/Low(native) | S-M |
| PERF-012 | TLUT invalidation: pool sweep → keyed index | universal | Med (verify) | M |
| PERF-013..021 | Micro-cleanup bundle | universal | Low each | S each |
| PERF-030 | Web RenderScale default | web | **Highest single web win** | S |
| PERF-031 | Narrow the ASYNCIFY instrumentation set | web | High | M |
| PERF-032 | Streaming wasm instantiation + early fetch | web | Med | S |
| PERF-033 | INITIAL_MEMORY 256→~128 MiB | web | Med (reach) | S |
| PERF-034 | LTO + closure on the web link | web | Low-Med | S-M |
| PERF-035 | Level-load yields + audio prefill | web | Med | M |
| PERF-036 | Service worker (repeat visits + offline) | web | Med | M |
| PERF-037 | Per-frame JS-crossing cleanup | web | Low | S |
| PERF-038 | Deliberate hidden-tab pause | web | Low | S |
| PERF-039 | AudioWorklet output sink | web | Med (long-term) | L |
| PERF-050 | Pacer spin-tail → real sleep | native | High (thermals) | S |
| PERF-051 | Present-mode knob (FIFO/Mailbox) | native | Low-Med (F5 prereq) | S |
| PERF-052 | Audio-synthesis worker thread | native | Med | L |
| PERF-053 | Minimap overlay: one pass, mid-pass scissor | native | Low-Med | S-M |
| PERF-060 | Skip synth under GE007_MUTE (CI) | tooling | CI wall-clock | S |
| PERF-061 | Ledger reconciliation | docs | — | S |

---

# Tier A — Universal (native + browser)

## PERF-001 — Skip redundant texture-rebind flushes  `S · Med-High`   — ⏸ **DEFERRED → PERF-006** (see status table; a safe skip needs the resolve/bind split that is PERF-006)

**Problem.** Batch count is the dominant driver of backend cost (per-draw overhead ×
wasm↔JS crossings on web). `gfx_pc.c` breaks a batch every time `textures_changed[ti]` is
set — but that flag is set by *every* `G_LOADBLOCK`/`G_LOADTILE`/`G_LOADTLUT`, including
loads that resolve to the texture that is already bound. GoldenEye's room/prop display
lists re-issue full material setup per draw group, so consecutive groups sharing a texture
still split the batch today.

**Evidence** — the flush happens before we know the binding is redundant
(`gfx_pc.c:18893-18897`):
```c
if (rdp.textures_changed[ti]) {
    gfx_flush();
    import_texture(ti, tile_desc);
    rdp.textures_changed[ti] = false;
}
```
…while the cache-hit path shows the same-texture case rebinding the identical id
(`gfx_pc.c:14793-14796`):
```c
/* HIT: move to LRU head (most recently used) */
tex_lru_remove(node->pool_idx);
tex_lru_push_front(node->pool_idx);
gfx_bind_texture(tile, node->texture_id);
```

**Fix.** Reorder: run the cache lookup first, and only `gfx_flush()` when the resolved
`texture_id` (or sampler state) actually differs from the currently bound one
(`rendering_state`). The upload-on-miss path keeps its flush.

**Validation.** Screenshot oracle suite (any batching bug is visible), perf census with
batch-count delta (add a `GE007_PERF_TRACE` counter for flushes if not present).

## PERF-002 — Gate per-vertex debug payload writes  `S · Med`   — ✅ **LANDED `fb3322f`**

**Problem.** The vertex loop stores ~96 bytes of diagnostic payload per vertex,
unconditionally — including 16 matrix floats — roughly doubling the loop's memory traffic.
`LoadedVertex` is ~150 B, over half debug fields, which also bloats the working set that
triangle assembly re-reads. The fix pattern already exists ten lines up: the `ob[]` /
`src_addr` fields are latched behind trace-enabled checks.

**Evidence** — existing gated pattern followed by the ungated block (`gfx_pc.c:17105-17144`):
```c
if (gfx_trace_vtx_source_enabled() || gfx_effect_tri_trace_is_enabled()) {
    d->ob[0] = v->ob[0]; ...          /* gated — the right pattern */
} else { d->ob[0] = 0; ... }
d->dbg_vtx_load_seq = vtx_load_seq;   /* ungated from here on */
d->dbg_vtx_cmd_addr = g_diag_current_cmd_addr;
...
d->dbg_mv_row3[0] = mv[3][0];         /* 16 matrix floats per vertex */
...
d->dbg_mp_col3[3] = rsp.MP_matrix[3][3];
```

**Fix.** Wrap the `dbg_*` stores in the same latch (one new master flag covering the
consumers), or `#ifdef` them out of Release entirely if no shipped diag reads them.
Optionally move `dbg_*` to a parallel array so `LoadedVertex` shrinks toward a cache line.

**Validation.** Build with each diag env flag on and confirm traces still populate;
perf census.

## PERF-003 — SETTEX cache: linear scan → direct map  `S · Med`   — ✅ **LANDED `755621f`**

**Problem.** `G_SETTEX` is issued per material for most world/prop textures. The cache
lookup is a linear scan of up to `SETTEX_CACHE_SIZE=2048` entries at ~56 B stride — with a
few hundred cached textures and a few hundred SETTEX commands per frame that is 10⁴–10⁵
strided compares per frame through a ~114 KB array: pure cache-miss traffic.

**Evidence** (`gfx_pc.c:21726-21728`):
```c
for (int i = 0; i < settex_cache_count; i++) {
    if (settex_cache[i].valid && settex_cache[i].texturenum == (uint32_t)texturenum) {
        settex_active = true;
```

**Fix.** `texturenum` is a 12-bit id — add a 4096-entry `int16_t` direct-map index
(`texturenum → cache slot`), keep the array only for storage/eviction order, update the
index on insert/evict. O(1) per command, no behavior change.

**Validation.** Screenshot suite; the settex diagnostics (`gfx_log_settex_event`) make
hit/miss behavior easy to A/B.

## PERF-004 — Hoist per-triangle diagnostic gates; cull before diagnostics  `M · Med-High`

**Problem.** Every emitted triangle evaluates ~35 diagnostic gate booleans — including a
room-command attribution lookup and focus matching — *before* clip rejection and backface
culling. With all diagnostics off (the shipping configuration), every backface-culled
triangle (~half of room geometry) still pays several hundred instructions of dead
diagnostic arithmetic. Separately, the room-DL attribution memo is 1-entry deep, so each
new command address pays an O(rooms) scan (`gfx_pc.c:13239-13263`).

**Evidence** — head of the gate block, which runs unconditionally per triangle
(`gfx_pc.c:17372-17383`):
```c
const char *dl_which = NULL;
int dl_room = -1;
uintptr_t cmd_offset = 0;
bool has_cmd_offset = gfx_diag_room_cmd_offset(g_diag_current_cmd_addr,
                                               &dl_room, &dl_which,
                                               NULL, &cmd_offset);
bool focus_match = gfx_diag_focus_matches(g_diag_current_cmd_addr, dl_room);
bool skip_by_range = g_diag_skip_cmd_range_enabled > 0 && ...
```
…followed by `skip_by_room_offset`, `debug_by_room_mode`, `tint_by_room_offset`,
`tint_by_room_mode`, etc. through `:17503`; clip rejection is at `:17616` and backface
cull at `:17637-17761`.

**Fix.** Three parts: (1) compute one `g_any_diag_active` master flag when env latches
resolve and branch over the entire gate block when false; (2) move clip/backface culling
ahead of the diagnostics (when a diag *is* active and needs culled triangles, a diag-mode
flag can keep the old order); (3) set the room-cmd attribution once per DL command at
dispatch time instead of comparing per triangle.

**Validation.** Each `GE007_*` diag env flag exercised once (they're the debugging
lifeline for the parity program — must keep identical semantics when enabled); perf
census; screenshot suite.

## PERF-005 — Shader/pipeline prewarm + async creation  `M · High(web)/Med(native)`  *(tracked: WEB-054)*

**Problem.** The first time gfx_pc meets a new combiner, the backend builds WGSL, creates
the shader module, and creates the render pipeline synchronously *inside the draw call*.
Every new material on level entry — and every first-use effect mid-mission (explosion,
glass) — is a one-off main-thread spike. In the browser this stacks with the
main-thread audio path (PERF-039 context): the same long frame that hitches also blows the
audio deadline, so the user sees a stutter *and* hears a crackle.

**Evidence** — module compile at first sight (`gfx_webgpu.c:1560-1573`):
```c
char *wgsl = gfx_webgpu_build_wgsl(shader_id0, shader_id1, &prg->info);
...
prg->module = wgpuDeviceCreateShaderModule(s_device, &smd);
```
…and synchronous pipeline creation on the draw path (`gfx_webgpu.c:1744`, reached from
`wgpu_draw_triangles`):
```c
WGPURenderPipeline pipe = wgpuDeviceCreateRenderPipeline(s_device, &pd);
```

**Fix.** (1) During level load, prewarm the known-hot set: record combiner-id ×
(blend|depth)-key sets per stage (the shader-slot LRU already proves the working set is
small), ship or cache the manifest, and create those pipelines while the load screen is
up. (2) Optionally switch first-sight creation to `wgpuDeviceCreateRenderPipelineAsync`
and skip the draw until ready (one-frame pop-in of a new material vs a full-frame hitch).

**Validation.** Frame-spike measurement on level entry (`GE007_PERF_TRACE` max-frame
column), web tape/boot smoke; visual check that skip-until-ready doesn't drop
load-bearing geometry (prefer prewarm as primary, async as backstop).

## PERF-006 — Material-derivation split in the triangle emit path  `L · High`

**Problem.** The structural CPU hot spot of the whole port. `gfx_emit_loaded_triangle`
(`gfx_pc.c:17351-19807`, ~2,450 lines) executes once per emitted triangle and re-derives
the complete material state each time: the blend-mode classifier switch, combiner raw-
feature decode (**twice** — `gfx_pc.c:18278-18279` and again at `:18492`), the full
`cc_options` assembly with clamp/mask helpers and tile-state fetches, room-water/coverage
gates, combiner lookup, and per-tile texture/sampler setup. All of it is loop-invariant
between RDP state changes — a 200-triangle same-material batch pays the derivation 200×.
Upstream fast3d's equivalent path is ~200 lines. There is no render thread; `gfx_pc.c`
CPU *is* the frame's critical path (`docs/RENDERING_ARCHITECTURE.md` §2), so this bounds
min-spec native and every browser.

**Evidence** — one of the duplicated loop-invariant derivations (`gfx_pc.c:18278-18279`):
```c
struct GfxCcRawFeatures cc_features_for_options =
    gfx_cc_id_raw_features(cc_id, cc_options);
```

**Fix.** Split into (a) a `derive_material()` step executed only when a dirty flag is set
— any `G_SETCOMBINE`/othermode/tile/settex/geometry-mode mutation sets it — producing a
cached `MaterialState` struct; and (b) a slim per-triangle emit that consumes the cache.
The diagnostic gates (PERF-004) and NDC work (PERF-018) naturally fold into the same
refactor. Recommended sequencing: land PERF-001/002/003/004 first (independent, de-risk
this one), then profile, then do this split.

**Validation.** Full screenshot/oracle suite, 20-level perf census before/after,
sim-invariance hash (should be untouched — this layer is render-only), tape gate.

## PERF-007 — Memory-blend pass-split coalescing  `M · Med-High on TBDR`  *(tracked: WEB-021 residual)*

**Problem.** N64 "memory-blend" materials (glass, fences) need the current scene as a
texture, so the backend ends the scene render pass, copies scene→snapshot, and reopens
the pass with `loadOp=Load` on **color and depth** — once per qualifying batch. On
tile-based GPUs — Apple Silicon (every macOS browser *and* native), all mobile — every
split is a full-target store + reload of both attachments. A fence-heavy frame (Facility,
Runway) splits several times. The rect-bounded copy half already landed; the remaining
cost is the pass split itself.

**Evidence** (`gfx_webgpu.c:2173-2205`, abridged):
```c
wgpuRenderPassEncoderEnd(s_pass);
...
(void)wgpu_batch_snapshot_rect(buf_vbo, buf_vbo_len, stride_floats, &rx, &ry, &rw, &rh);
...
wgpuCommandEncoderCopyTextureToTexture(s_encoder, &cs, &cd, &ext);
...
att.loadOp  = WGPULoadOp_Load;      /* color reload */
depth.depthLoadOp = WGPULoadOp_Load; /* depth reload */
s_pass = wgpuCommandEncoderBeginRenderPass(s_encoder, &rp);
```

**Fix.** (1) Skip the split entirely when the computed batch rect is empty/offscreen.
(2) Coalesce consecutive memory-blend batches into a single snapshot+pass (they currently
each pay their own). The backlog's own caveat applies: measure on an Apple GPU first —
that's where the win lives.

**Validation.** Dam/Facility glass screenshot parity (this path was built for
correctness — FID-0005 lineage), Apple-GPU frame time, perf census.

## PERF-008 — Present without the extra full-res copy  `M · Med`

**Problem.** Every frame ends with a full-resolution texture-to-texture copy from the
offscreen present target to the surface texture — on top of the post-FX chain that
already produced a final image. At RenderScale-2 1440p that's ~14.7 MB/frame (~0.9 GB/s
at 60 fps) of pure copy bandwidth, plus an extra pass-store on TBDR. The surface is also
permanently configured with `CopySrc` usage solely for a debug dump, which can block
compositor fast-paths on some platforms.

**Evidence** (`gfx_webgpu.c:1192-1198` and `:474/:478`):
```c
if (present_ok) {
    ...
    WGPUExtent3D ext = { s_scene_w, s_scene_h, 1 };
    wgpuCommandEncoderCopyTextureToTexture(s_encoder, &cs, &cd, &ext);
}
```
```c
cfg.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
...
cfg.presentMode = WGPUPresentMode_Fifo;   /* vsync; matches the default GL/Metal swap */
```

**Fix.** When post-FX is active, point the final post-FX pass (and the minimap/overlay
passes) directly at the acquired surface view; keep the offscreen present target only for
frames that need readback (screenshot/parity harness — AUDIT-0003 lineage). Configure
`CopySrc` only when `GE007_WEBGPU_DUMP_SURFACE` is set.

**Validation.** Screenshot harness still works (it reads the offscreen target), visual
parity, bandwidth-bound devices (mobile browser) frame time.

## PERF-009 — Skinned-character matrix rebuild dedupe  `M · Med`  *(sim-neutral, needs oracle)*

**Problem.** Every render pass re-runs the full skeleton matrix computation
(`subcalcmatrices`) for every skinned character with a model — even when the animation
tick produced no change since the matrices were last built, and per-viewport in
split-screen. Guard-dense levels (Bunker, Archives, Frigate) pay the most; the cost
doubles under future F5 render-only frames. This code runs identically in the browser.

**Evidence** (`src/game/chr.c:8661-8674`):
```c
/* Re-running subcalcmatrices in place keeps the existing render_pos
 * allocation but guarantees every slot is rebuilt for this pass
 * before drawjointlist dispatches the binary DL. */
if (chrmodel != NULL && chrmodel->render_pos != NULL
        && chrmodel->obj != NULL && chrmodel->obj->Skeleton != NULL) {
    ModelRenderData matrixdata = D_8002CC6C;
    matrixdata.unk_matrix = camGetWorldToScreenMtxf();
    ...
    subcalcmatrices(&matrixdata, chrmodel);
```

**Fix.** Add an anim-state/tick generation counter per character; skip the rebuild when
(generation, camera matrix) are unchanged since the last build for this viewport. Writes
only `render_pos` (render data, not part of the hashed sim state) — sim-neutral.

**Caution.** The unconditional rebuild was itself a correctness fix (see the comment) —
the dirty-flag must cover *every* input (`camGetWorldToScreenMtxf`, aim-bone callback
state). Validate against the screenshot oracle suite on guard-dense scenes and the intro
cinematics (D35/D43 lineage).

## PERF-010 — Adaptive audio queue target  `S · Med`  *(both: latency + stall absorption)*   — ✅ **LANDED `e79d871`** (control surface only; auto-derivation deferred)

**Problem.** The audio occupancy controller targets a fixed 1.5 audio frames (~50 ms at
22050 Hz). That single number is simultaneously (a) the entire stall-absorption budget —
any frame longer than ~50 ms is an audible gap (level loads and first-sight pipeline
compiles do this today) — and (b) a floor on output latency (~75–100 ms total with the
device buffer; gunshots land 4–6 display frames after the trigger). The telemetry already
computes a device-aware `soft_target` (`audi_port.c:578-583`) — the controller ignores it.

**Evidence** (`audi_port.c:465-475`):
```c
const u32 queued_bytes = osAiGetLength();
const s32 queued_samples = (s32)(queued_bytes >> 2);
const s32 target_samples = (s32)(g_FrameSize + (g_FrameSize / 2));
...
s32 chosen = full_samples + ((target_samples - queued_samples) / 2);
```

**Fix.** Derive the target from device buffer size + measured frame-time jitter (both
already available), and expose it via the existing `Audio.*` settings surface. Two
profiles fall out naturally: low-latency (native, small device buffer) and high-absorption
(web, jank-prone). Deterministic mode (`g_deterministic`, `:459-463`) keeps its Bresenham
cadence untouched — no byte-identity constraint on the adaptive path.

**Validation.** Underrun counters (already tracked, verbose-gated), `GE007_AUDIO_DUMP`
unchanged in deterministic mode.

## PERF-011 — DSP inner-loop telemetry hoist / vectorization  `S-M · Med(wasm)/Low(native)`

**Problem.** The envelope mixer — the hottest DSP kernel, run per active voice (up to 24)
per 160-sample subframe — threads a stats-counter *pointer* through every clamp and
xor-swizzles the sample index. Both defeat autovectorization (wasm `-msimd128` and native
alike). The reverb pole filter additionally tracks per-sample input *and* output peaks
unconditionally, though the only consumer is an env-gated JSONL trace.

**Evidence** (`mixer.c:603-621`, abridged):
```c
gain[0] = clamp16((vol[0] * voldry + 0x4000) >> 15);
...
const int16_t insamp = in[sample_idx];               /* sample_idx = i ^ rspa.env_sample_xor */
dry[0][sample_idx] = clamp16Counted(
    dry[0][sample_idx] + ((insamp * gain[0]) >> 15),
    &rspa.stats.envMixerClampHits);                  /* per-sample counter pointer */
```

**Fix.** (1) Accumulate clamp hits in a local and add to `rspa.stats` once per call.
(2) Specialize the common `env_sample_xor == 0` and settled-ramp (`rate == 0`) cases so
the compiler sees contiguous, branch-light loops. (3) Gate pole-filter peak tracking on
the already-cached trace flag; pass NULL stats when telemetry is off. All byte-identical
transformations — provable with `GE007_AUDIO_DUMP`.

Companion micro-fixes in the same lane: silence early-out for FX sections whose input
*and* delay-line span are zero (`audio_compat.c:1850-1895`), and caching the last-loaded
ADPCM codebook to skip the per-voice-per-subframe byte-copy (`mixer.c:324-333`).

**Validation.** `GE007_AUDIO_DUMP` byte-compare before/after — this is the strongest
oracle in the repo; use it.

## PERF-012 — TLUT invalidation: pool sweep → keyed index  `M · Med`  *(verify at runtime first)*

**Problem.** Every `G_LOADTLUT` opens with two full sweeps of the 1024-node texture-cache
pool, and deletes every cached texture decoded against the palette pointers being
replaced. Consequence: CI-format textures whose TLUTs alternate within a frame are
re-decoded and re-uploaded on every alternation — the only recurring texel-decode cost
left in the layer (fonts, HUD, runtime palettes are the exposure). Dynamic
loadblock/loadtile paths pay one sweep each (`gfx_pc.c:21045-21047`).

**Evidence** (`gfx_pc.c:20852-20856` and the sweep it calls, `:2926`):
```c
static void gfx_dp_load_tlut(uint8_t tile, ...) {
    gfx_texture_cache_delete_by_palette_addr(rdp.palette_addrs[0]);
    gfx_texture_cache_delete_by_palette_addr(rdp.palette_addrs[1]);
```
```c
for (int idx = 0; idx < TEXTURE_CACHE_MAX_SIZE; idx++) {   /* full pool per call */
```

**Fix.** (1) Secondary index (hash or per-key chain) on `palette_addr` /
`texture_source_key` so invalidation touches only matching nodes. (2) Key CI cache
entries on a palette *content generation* counter (bump on TLUT upload whose bytes
differ) instead of deleting on every replace — turns the re-decode churn into cache hits.
**First**: add a one-shot counter (`GE007_PERF_TRACE` column) for TLUT-invalidation
deletes per frame to confirm the churn exists in real scenes before investing.

**Validation.** Dam monitor / font rendering screenshots (CI-palette lineage — this area
had correctness bugs before; the IA16/CI4 fixes must stay green), perf census.

## PERF-013..021 — Micro-cleanup bundle  `S each · Low each`

Each is small, safe, and universal unless noted; land opportunistically alongside
neighboring work.

- **PERF-013** Combiner lookup (`gfx_pc.c:14704-14713`): 1-entry memo then linear pool
  scan, called per state change; alternating materials defeat the memo. Small
  open-addressing hash on `cc_id ^ cc_options`, or move-to-front.
- **PERF-014** Per-draw encoder state (`gfx_webgpu.c:2484-2487`): `SetPipeline`/
  `SetBindGroup` are re-issued every draw even when unchanged — the one state pair the
  WEB-023-lite dedup missed. Cache last-applied per pass, reset in
  `wgpu_reset_pass_dynamic_state()` (the reset hook already exists, `:2206-2208`).
- **PERF-015** DL-dispatch range scans (`gfx_pc.c:602-609`, `:765-790`): per-command
  linear scans over up to 512 draw-class ranges; sort + binary search, or memoize per
  contiguous command run.
- **PERF-016** Room-XLU defer arena (`gfx_pc.c:14277-14284`): malloc + memcpy + free of
  every deferred XLU batch, every frame; replace with a persistent high-water arena.
- **PERF-017** Failed-pipeline tombstone (`gfx_webgpu.c:1745`): `CreateRenderPipeline`
  returns a non-NULL *error* handle on validation failure, so `if (pipe != NULL)` caches
  it and every subsequent frame re-binds it and logs via `on_device_error`
  (`fprintf`+`fflush` per submit, `:285-287`). Tombstone failed keys; rate-limit the
  error callback.
- **PERF-018** NDC/divide reuse (`gfx_pc.c:3301-3338`, `:17649-17652`): ~10 divides per
  triangle, half wasted on culled triangles; backface cull re-divides x/w,y/w instead of
  reusing `ndc_metrics.ndc`. Folds into PERF-006.
- **PERF-019** Bind-group cache tail (`gfx_webgpu.c:1383-1423`): O(512-set) sweep per
  released texture view (level-transition delete storms = texture_count × 512 scans) and
  process-lifetime pinning of up to 2048 bind groups + their textures. Per-view reverse
  index + a level-transition flush. (Self-documented watch item in the code.)
- **PERF-020** Resize debounce (`gfx_webgpu.c:734-790`): window drag recreates surface +
  3 full-res targets every frame; recreate only after N stable frames.
- **PERF-021** Env-latch stragglers: `lvl.c:1375` runs `getenv("GE007_PC_BG_RENDERER")`
  per `lvlRender`; minimap dump-path `getenv` per frame even with minimap off
  (`minimap_overlay.c:144-153`). Apply the standard `static int cached = -1` idiom.
  Also: modern-mesh decor writes one 96 B uniform per draw (`gfx_webgpu.c:3028`) — shadow
  the ring like `s_vbuf_shadow` and upload once per frame (native-leaning; web ships
  decor off).

---

# Tier B — Web-only

## PERF-030 — Web RenderScale default  `S · Highest single web win`   — ✅ **LANDED `639cffc`**

**Problem.** The browser build inherits the desktop remaster default `RenderScale = 2`
(2× supersampling) on top of CSS-size × devicePixelRatio — and the WebGPU *surface
itself* is configured at the supersampled size, so a DPR-2 laptop fullscreen
(1512×944 CSS) renders, post-processes, and presents a ~23-megapixel swapchain per frame
(~5× the native default's pixel load), which the compositor then downscales. The PortMaster
build already got exactly this carve-out for the same class of GPU; web did not.

**Evidence** (`platform_sdl.c:378-382`):
```c
#ifdef MGB64_PORTMASTER_GLES
f32 g_pcRenderScale = 1.0f;   /* R36S: native res — 2x SSAA renders at 1280x960 on a Mali-G31 */
#else
f32 g_pcRenderScale = 2.0f;   /* remaster default: 2x SSAA (clean edges; raise to 4x for max IQ) */
#endif
```
Resolution = CSS × DPR (`gfx_pc.c:4637-4656`), surface configured at that size
(`gfx_webgpu.c:726-735` → `:460-479`). The shell deliberately passes only HUD overrides
(`web/mgb64-shell.js:527-534`).

**Fix.** Default `1.0f` under `__EMSCRIPTEN__` (a DPR-2 canvas is already effectively
supersampled relative to the game's 320×240 content); add a quality selector in the shell
via the existing `--config-override` path for users with GPU headroom. Follow-up
(M): present at CSS×DPR and keep SSAA offscreen-only, so swapchain/composite cost stays
bounded even when the user opts into SSAA.

**Validation.** `web_boot_smoke`, manual IQ check at DPR 1 (a 1× screen *does* lose the
SSAA edge quality — the quality selector covers that user), live-demo frame time on an
iGPU laptop.

## PERF-031 — Narrow the ASYNCIFY instrumentation set  `M · High`  *(tracked: WEB-022)*

**Problem.** The link uses blanket `-sASYNCIFY`, which instruments every potentially-
suspending call path in the 3.4 MB code section — call-graph-wide spill/restore
scaffolding — for what is essentially one recurring yield site (the rAF frame wait) plus
bring-up pumps. Typical cost is 1.5–3× on instrumented paths, every frame: the single
largest whole-program CPU multiplier in the browser build, and it compounds with
PERF-030.

**Evidence** (`CMakeLists.txt:2056-2057`):
```cmake
"-sASYNCIFY"
"-sASYNCIFY_STACK_SIZE=65536"
```
Yield sites: `EM_ASYNC_JS platformWaitAnimationFrame` (`platform_sdl.c:20-27`),
`emscripten_sleep` in the hidden-tab path and `WGPU_COMPAT_WAIT` pumps
(`gfx_webgpu_compat.h:28-32`, screenshot-only in the frame path).

**Fix.** `-sASYNCIFY_IGNORE_INDIRECT` + explicit `ASYNCIFY_ADD` list covering the real
suspend chains (frame wait, boot pumps, readback). JSPI (`-sJSPI`) is Chrome-only as of
2026 — usable as a second build flavor at most, not a replacement. The backlog's own
precondition stands: bring up the browser tape lane first as the regression net, then
land this with before/after `work_ms` from `GE007_PERF_TRACE`.

**Validation.** Browser tape lane, `web_boot_smoke`, wasm size budget (should *shrink*),
manual soak (an Asyncify list that's too narrow crashes loudly at the missed suspend
point — test all paths: boot, level load, screenshot, hidden-tab).

## PERF-032 — Streaming wasm instantiation + earlier fetch  `S · Med`   — ✅ **LANDED `ba69d94`**

**Problem.** The shell downloads the entire wasm to an ArrayBuffer and hands it to
Emscripten as `wasmBinary` — so compilation starts only after the full 1.38 MB (wire)
download finishes, instead of overlapping download+compile. The stated reason is a Pages
MIME concern that is no longer true (live check today: `content-type: application/wasm`).
The fetch also starts only after the async WebGPU adapter gate resolves.

**Evidence** (`web/mgb64-shell.js:170-179`):
```js
// may serve .wasm with a wrong MIME). Kicked off when the gate passes so it runs
// while the user reads the page and picks a ROM; boot() awaits the stored promise.
function fetchWasm() {
  return fetch("ge007_web.wasm" + _verParam).then((resp) => {
    if (!resp.ok) throw new Error(`engine download failed (HTTP ${resp.status})`);
    return resp.arrayBuffer();
  });
}
```

**Fix.** Provide `Module.instantiateWasm` using
`WebAssembly.instantiateStreaming(fetch(url))` with an ArrayBuffer fallback (keeping the
WEB-033 HTTP-status guard), and start the fetch before `await requestAdapter()`. Frees
the 4 MB binary copy from memory too (it currently coexists with the compiled module).

**Validation.** `web_boot_smoke`, throttled-network boot timing, the WEB-033 error-path
check (404 must still surface cleanly).

## PERF-033 — INITIAL_MEMORY 256 MiB → measured (~128 MiB)  `S · Med (reach)`   — ✅ **LANDED `0091980`**

**Problem.** The web link commits 256 MiB of WebAssembly.Memory at boot. The comment's
own itemization (12 MB ROM + 8 MB sim pool + staging) lands nowhere near 256, growth is
already enabled, and a large upfront commit raises boot-failure and tab-eviction odds on
exactly the marginal devices (iOS Safari, low-RAM Android/Chromebooks) that broad
playability targets. This is the orphaned build half of WEB-062.

**Evidence** (`CMakeLists.txt:2058-2059`):
```cmake
"-sALLOW_MEMORY_GROWTH=1"
"-sINITIAL_MEMORY=268435456"      # 256 MiB: 12 MB ROM + 8 MB sim pool + GPU staging + headroom
```

**Fix.** Measure the real high-water mark (`HEAP8.buffer.byteLength` after a mission
soak), set INITIAL_MEMORY to measured + headroom (likely ~128 MiB), and add
`-sMAXIMUM_MEMORY` to document the ceiling. A handful of grow events at these sizes is
noise.

**Validation.** Full-mission web soak without a grow-related hitch at level load
(pre-grow before `lvlStageLoad` if one shows up), `web_boot_smoke`.

## PERF-034 — LTO + closure on the web link  `S-M · Low-Med`   — ❌ **MEASURED & REJECTED** (both halves fail on this toolchain)

**Outcome (2026-07-17):** attempted and rejected — neither half is a clean win here.
- **`-flto` INFLATED the wasm ~25%** (4,010,596 → 5,039,510 B) instead of shrinking it.
  Whole-program `-sASYNCIFY` (no ONLY/ADD list) must instrument every call site LTO's
  cross-module inlining exposes, so the Asyncify scaffolding grows faster than LTO
  trims. `web_boot_smoke` stayed green (correct, just bigger) — but bigger download is
  the opposite of the goal. This is the exact Asyncify × LTO pathology PERF-031 flags.
- **`--closure=1` (ADVANCED) fails the build outright**: `JSC_UNDEFINED_VARIABLE` on
  SDL2's legacy message-box `ASM_CONST` (`allocate(intArrayFromString(reply),"i8",
  ALLOC_NORMAL)`) — `allocate`/`ALLOC_NORMAL` are runtime APIs modern emscripten
  removed; they survive only as dead inline JS in SDL2's Emscripten backend, which
  closure resolves statically and rejects.
- **Revisit** only after PERF-031 narrows the Asyncify set (which should also flip LTO
  from a loss to a win) and after the SDL2/closure `allocate` issue is patched (externs
  or an upstream SDL fix). Reverted; a CMakeLists comment records the finding.

**Original plan (for reference).** Release-config only: add `-flto` (compile + link) and
`--closure 1` (link). Validate the Asyncify × LTO interaction explicitly (known-fiddly
combination) — if it misbehaves, take closure alone. *(Both failed as above.)*

**Validation.** Tape gate, `web_boot_smoke`, wasm size budget gate (expect a drop),
full-mission soak.

## PERF-035 — Level-load yields + audio prefill  `M · Med`

**Problem.** `lvlStageLoad` legitimately blocks the main loop for seconds (the code
suppresses the stall watchdog around it, `boss.c:429-433`). On web that means a frozen
tab (browser "page unresponsive" risk), and a guaranteed audio gap — the AI queue cap was
raised to 12 frames (~200 ms) on web precisely because loads are "a long synchronous
stretch that produces no audio" (`stubs.c:428-438`), but 200 ms doesn't cover seconds.

**Fix.** Under `__EMSCRIPTEN__`: (1) insert periodic `emscripten_sleep(0)` yields +
audio pumps at the resource-load loop's natural boundaries (the same pattern as
`WGPU_COMPAT_PUMP`); (2) prefill the audio queue to its 12-frame cap immediately before
entering the load. Native is unaffected (load screen covers it).

**Validation.** Web level-transition with the console performance panel (no long-task
warnings > ~1 s), no audio underrun counter spike across a load, tape gate (yields must
not perturb the deterministic path — gate them off under `--deterministic`).

## PERF-036 — Service worker: repeat visits + offline  `M · Med`

**Problem.** GitHub Pages serves everything with `cache-control: max-age=600` and allows
no header control: returning players re-validate every asset after 10 minutes and
re-download after each deploy. Meanwhile the ROM already lives in OPFS and saves in IDBFS
— the game is one service worker away from instant repeat boots and full offline play.

**Fix.** Hash-versioned, cache-first service worker over the 5 deployed files, keyed on
the existing `__MGB64_BUILD__` stamp, atomic activate-on-complete (also closes the
WEB-032 glue/wasm skew window more robustly than `?v=`). Requires updating the deploy
whitelist in `web-demo.yml:50-54` and the 5-file invariant in `docs/WEB.md`.

**Validation.** `web_boot_smoke` (must tolerate the SW), airplane-mode boot test, deploy
flow: old tab + new deploy → next load atomically switches.

## PERF-037 — Per-frame JS-crossing cleanup  `S · Low`

**Problem.** Steady-state per frame the loop crosses wasm↔JS for: the canvas CSS size
**twice** (`gfx_pc.c:4637` and `platform_sdl.c:3351` — each hits getComputedStyle), a
`document.pointerLockElement` EM_ASM query (`platform_sdl.c:3324`), a `document.hidden`
EM_JS query *per pacer-wait iteration* (`platform_sdl.c:31-33`, 2–3×/frame on
high-refresh panels), and a fresh `new Promise(resolve => requestAnimationFrame(...))`
allocation per frame wait (`platform_sdl.c:24-26`) — steady minor-GC pressure on the one
thread that does everything.

**Fix.** Cache CSS size + DPR in JS globals updated by one ResizeObserver; mirror
`document.hidden` and pointer-lock state via `visibilitychange`/`pointerlockchange`
listeners into a wasm-readable flag; consider a persistent rAF-bridge callback instead of
a Promise per frame.

**Validation.** `web_boot_smoke`, resize/fullscreen/pointer-lock manual pass, WEB-016
unscale behavior unchanged.

## PERF-038 — Deliberate hidden-tab pause  `S · Low`

**Problem.** The hidden-tab fallback assumes the 1–4 ms setTimeout clamp; real browsers
clamp *background-tab* timers to ≥1 s (Chrome: ~1/min after 5 minutes for silent tabs).
Net effect: the sim runs at ~1 Hz while hidden — a de facto pause — but still burns synth
work producing ~3%-of-realtime audio: gaps with occasional blips. Resync-on-refocus is
already verified clean, so nothing breaks; the code just spends CPU to produce broken
audio, and the comment misleads future work.

**Fix.** Make "hidden = paused" deliberate: when `platformTabHidden()`, skip
`portAudioFrame` and queue silence (ramped), correct the comment (`platform_sdl.c:3903-3921`),
and keep the existing no-catch-up deadline resync.

**Validation.** Background/foreground cycling with audio; the WEB-005 resync behavior
must stay intact.

## PERF-039 — AudioWorklet output sink  `L · Med (long-term)`

**Problem.** SDL2's Emscripten audio driver is a deprecated main-thread
ScriptProcessorNode at the context rate — every callback shares the main thread with
sim+render (one long frame starves production *and* consumption), SDL resamples
22050→context-rate inside that callback, and SPN is on Chromium's removal track (a future
breakage, not just a perf item). Self-documented at `audio_pc.c:28-38`; the mitigation
(2048-sample buffer) is the right short-term call.

**Fix.** AudioWorklet needs COOP/COEP headers, which raw GitHub Pages cannot serve — so
this is gated on either a host with header control or the coi-serviceworker pattern
(natural pairing with PERF-036). Then: `-sAUDIO_WORKLET` or a custom worklet sink fed
from the existing queue; opening the device at context rate with one in-engine resample
(opt-in) removes SDL's hidden second resampler either way. Sequence *after*
PERF-030/031/035 — those remove most of the pressure that makes SPN audible.

**Validation.** Cross-browser (Safari/Firefox) soak, underrun counters, latency check.

---

# Tier C — Native-only

## PERF-050 — Pacer spin-tail → real sleep  `S · High (thermals/battery)`   — ✅ **LANDED `7207971`**

**Problem.** The frame pacer coarse-sleeps to within ~2–2.5 ms of the deadline, then
`SDL_Delay(0)` yield-spins the rest — ~12–15% of a core, continuously, on every native
machine. On handhelds (PortMaster), laptops, and Steam Deck that is battery drain and
heat; sustained heat means clock throttling, which then eats real frame budget. (Web uses
the rAF path and is unaffected.)

**Evidence** (`platform_sdl.c:3929-3937`):
```c
while (pace_now < g_paceDeadline) {
    double rem_ms = (double)(g_paceDeadline - pace_now) * 1000.0 / (double)freq;
    if (rem_ms > 2.5) {
        SDL_Delay((u32)(rem_ms - 2.0));  /* coarse sleep, ~2ms precise tail */
    } else {
        SDL_Delay(0);                    /* yield-spin the final stretch */
    }
    pace_now = SDL_GetPerformanceCounter();
}
```

**Fix.** Shrink the spin window to ~0.5 ms using sub-ms sleeps (`nanosleep`/`usleep(500)`
chunks; SDL3 offers `SDL_DelayNS`), keeping the absolute deadline. Optionally calibrate
the tail to measured per-OS sleep overshoot at startup. The deadline pacer design (the
2026-07-03 hitching fix) is untouched — only the tail granularity changes.

**Validation.** Frame-pacing histogram before/after (the PM1 evidence tooling), CPU% of
an idle-scene run, no reintroduced hitching (this pacer exists because integer-ms sleep
caused beat hitching — keep the deadline absolute).

## PERF-051 — Present-mode knob  `S · Low-Med (F5 prerequisite)`

**Problem.** Present mode is hard-coded FIFO (`gfx_webgpu.c:478`, snippet under
PERF-008). FIFO adds swapchain queue-depth latency on top of the pacer, and blocks the F5
uncapped-FPS plan outright — render-only frames cannot exceed refresh under FIFO.

**Fix.** Scan `caps.presentModes`, pick Mailbox when advertised behind a
`GE007_WEBGPU_PRESENT` env/ini knob; FIFO stays the default for baseline byte-identity.
Browser is rAF-driven regardless (`gfx_webgpu_compat.h:36` no-ops present) — nothing to do
there.

## PERF-052 — Audio-synthesis worker thread  `L · Med`

**Problem.** The full N64 synth chain runs synchronously at the frame tail on the main
thread (`audi_port.c:435-439` "No thread on port"; call site `platform_sdl.c:4003-4011`),
serialized with sim + DL build + GPU submit + present. On min-spec everything must fit in
16.7 ms; any GPU stall delays audio production directly (see PERF-010 for the absorber
math).

**Fix.** Native-only: move `alAudioFrame` production to a dedicated thread fed by a
command/ring buffer. **Hard constraint (FID-0089)**: the audio pump *cadence* leaks into
hashed sim state via `sndDisposeSound` writing `ChrRecord.ptr_SEbuffer` slots — voice
lifecycle (allocation/disposal/`sndGetPlayingState`) must stay on the main thread at
per-frame cadence; only DSP synthesis moves. `--deterministic` keeps the fully
synchronous path. Not portable to web (no SharedArrayBuffer on Pages).

**Validation.** Sim-invariance hash + tape gate (the FID-0089 leak is exactly what these
catch), `GE007_AUDIO_DUMP` in deterministic mode, thread-sanitizer pass.

## PERF-053 — Minimap overlay: one pass, mid-pass scissor  `S-M · Low-Med`

**Problem.** On WebGPU, every `minimap_overlay_flush()` opens and closes its own
Load/Store render pass (`gfx_webgpu.c:2664-2674`); flushes are forced by every scissor
toggle and buffer-full (`minimap_overlay.c:406-450, 639, 1696-1712`), so a normal minimap
frame produces ~2–6 full passes (× players in split-screen) — each a full-target
load+store on TBDR. Web ships the minimap off by default, so this is native-leaning.

**Fix.** Open one overlay pass per frame and use `SetScissorRect` inside it (the pass
encoder supports mid-pass scissor — the scene path already relies on it). Also cache the
per-flush 16-byte UBO write when the value is unchanged (`:2654`).

**Validation.** Minimap on/off screenshot parity, split-screen visual check.

---

# Tier D — Tooling / bookkeeping

## PERF-060 — Skip synthesis under GE007_MUTE when nothing consumes it  `S · CI wall-clock`

Muted runs (`GE007_MUTE=1`, the standard test configuration) still execute the entire DSP
chain and then attenuate to zero (`audio_pc.c:1021-1035`). Correct — the
`GE007_AUDIO_DUMP` tap needs real signal — but headless runs *without* a dump consumer
burn the full synth for silence, across every CI lane, every day. Fix: when muted and no
dump/trace consumer is registered, skip synthesis and queue silence-sized frames;
auto-detected so `GE007_AUDIO_DUMP` and deterministic baselines are untouched.

## PERF-061 — Ledger reconciliation  `S · docs only`   — ◑ **PARTIAL `8fb4393`** (ENV_FLAGS drift fixed; the WEB_BACKLOG/PERFORMANCE_PLAN entries below remain)

- ✅ **Done (`8fb4393`)**: the source↔`docs/ENV_FLAGS.md` invariant — `GE007_WEBGPU_TRACE_VIEWPORT`
  had drifted, leaving the `env_reference_current` ctest red on `main`; regenerated.
- WEB-062's build half (INITIAL_MEMORY drop → PERF-033, now LANDED `0091980`) is in neither
  LANDED nor STILL-DEFERRED in `docs/WEB_BACKLOG.md`.
- WEB-012's AudioWorklet long-term half (→ PERF-039) is absent from STILL DEFERRED.
- WEB-039 dropped streaming instantiation on a now-disproven MIME assumption (→ PERF-032).
- `docs/design/PERFORMANCE_PLAN.md` header still says "not yet implemented" though M1/M2
  shipped by content; its §3 census numbers are GL-era (current numbers:
  `docs/design/WEBGPU_BACKEND_STATUS_2026-07-13.md`).
- `docs/audit/PRIORITIZATION.md` lists AUDIT-0019 open; fixed at `cf3944a`.

---

# Relationship to existing plans (not re-opened here)

- **F5 uncapped FPS** (`docs/design/UNCAPPED_FPS_PLAN.md`): owns the `waitForNextFrame`
  busy-wait replacement (`unk_0C0A70.c:315-328` — a pure `osGetCount()` spin, masked
  today by the pacer, a 100%-core burn the moment the cap lifts) and the sim/render
  decoupling architecture. PERF-050 and PERF-051 are its practical prerequisites.
- **PERFORMANCE_PLAN M3** (draw-submission efficiency): per
  `docs/RENDERING_ARCHITECTURE.md` §2, submission is ~7% of the heaviest frame post-M1/M2
  — "no draw-call optimizations without a profile." Tier A items reduce *frontend* CPU
  and batch count (the documented critical path), not backend submission; they are not M3.
- **PERFORMANCE_PLAN M4** (room over-admission): gameplay-coupled
  (`room_rendered → chr.c:5205` auto-aim) — stays ship-last per plan; nothing in this
  backlog touches room admission.

# Suggested execution waves

1. **Wave 1 — universal quick wins**: PERF-001, 002, 003, 010, plus PERF-050 (native) and
   PERF-030/032/033 (web) if a mixed wave is acceptable. All S-effort, independently
   testable, existing gates cover them.
2. **Wave 2 — jank killers**: PERF-005 (prewarm), PERF-004 (diag hoist), PERF-035 (web
   load yields), PERF-034 (link flags).
3. **Wave 3 — structural, profile-gated**: PERF-006 (material split — biggest single
   win), PERF-007/008 (pass restructuring), PERF-012 (after the churn counter confirms).
4. **Wave 4 — long poles**: PERF-052 (audio thread), PERF-031 (Asyncify, after the
   browser tape lane exists), PERF-036/039 (SW + worklet), then F5 per its own plan.
