# Performance & Best-Practices Review — Web + Native (2026-07-17)

Six-lane read-only audit of the web platform layer, WebGPU backend, fast3d frontend
(`gfx_pc.c`), core loop/timing, audio pipeline, and prior-art docs. Goal: the biggest
remaining wins for broad playability (low-end laptops, mobile browsers, handhelds).

**Method note**: all findings are code-verified with file:line evidence at HEAD `2eb1b9b`.
Items already tracked in `docs/WEB_BACKLOG.md` / `docs/design/PERFORMANCE_PLAN.md` /
`docs/design/UNCAPPED_FPS_PLAN.md` are marked; this doc does not re-open them, only ranks
them against the new findings. Nothing here was applied — review only.

**What is already good (do not re-fix)**: M1/M2 shipped (XLU snapshot per-batch, room-
attribution memo — jungle 18→131 fps); WebGPU default beats GL on all 20 levels; one
vertex `writeBuffer`/frame + bind-group cache + no readbacks in the frame path; texture
cache is address-keyed (zero per-frame texel hashing); HD-pack path free in steady state;
per-frame env/config discipline nearly everywhere; wasm is 3.8 MiB (1.38 MiB wire, Pages
gzips); syncfs debounced; rAF pacer correct (no double-pacing); frame path malloc-free.

---

## Tier 1 — biggest wins

### P1. Web ships desktop remaster defaults: RenderScale 2 × DPR swapchain  [NEW · S effort · web]
`g_pcRenderScale = 2.0f` default with no web override (`platform_sdl.c:381`; PortMaster
gets a 1.0 carve-out at `:379`, web does not). Resolution = CSS × devicePixelRatio ×
RenderScale, and the **surface itself** is configured at that size (`gfx_pc.c:4637-4656`,
`gfx_webgpu.c:726-735` → `:460-479`). A DPR-2 laptop fullscreen renders/presents a
~23 MP swapchain (vs ~4.7 MP native default) with bloom/FXAA/sharpen/grade at that res,
then the compositor downscales it. Single biggest lever for iGPU laptops, 4K, mobile;
multiplies with the Asyncify tax (P3).
**Fix**: default `Video.RenderScale=1` (or DPR-aware cap) under `__EMSCRIPTEN__`; add a
shell quality selector via the existing `--config-override` path; consider presenting at
CSS×DPR with SSAA offscreen-only.

### P2. `gfx_emit_loaded_triangle` re-derives full material state per triangle  [NEW · L effort · both]
~2,450-line function (`gfx_pc.c:17351-19807`) runs per emitted triangle: blend-mode
classifier switch, `gfx_cc_id_raw_features()` **twice**, full cc_options assembly,
room-water/cvg gates, combiner lookup, per-tile texture/sampler setup — all loop-invariant
between RDP state changes; a 200-tri same-material batch pays it 200×. Structural hot spot
of the whole CPU frame (and `gfx_pc.c` is the critical path — no render thread).
**Fix**: split into a dirty-flag-gated "derive material" step + slim per-triangle emit.
Long-pole but the largest native+web CPU win available. Near-free companions to land first:
- **P2a** [S]: redundant `gfx_flush()` on texture rebind — `textures_changed` set by every
  load even when the cache resolves to the already-bound texture (`gfx_pc.c:18893-18897`,
  `:14793-14798`); check the resolved id before flushing. Batch count drives backend cost.
- **P2b** [S]: ~96 B of unconditional debug payload written **per vertex** (16 matrix
  floats etc., `gfx_pc.c:17118-17144`) — roughly doubles vertex-loop memory traffic;
  gate behind the existing trace latches.
- **P2c** [M]: ~35 per-triangle diagnostic gate booleans evaluated **before** clip/backface
  cull even with all diags off (`gfx_pc.c:17372-17503`); add one `g_any_diag_active`
  master flag and move culling first.
- **P2d** [S]: G_SETTEX cache = linear scan of up to 2048 × 56 B entries per command
  (`gfx_pc.c:21726-21754`); `texturenum` is 12-bit → direct-map index.

### P3. Blanket `-sASYNCIFY` taxes the whole wasm binary  [TRACKED WEB-022 · M · web]
`CMakeLists.txt:2056-2057`, no ONLY/ADD/IGNORE_INDIRECT lists; instruments the full
3.4 MB code section for essentially one recurring yield site (the rAF wait). Typical
1.5–3× on instrumented paths — the largest whole-program browser CPU multiplier, and it
compounds with P1. JSPI is Chrome-only (2026), so `ASYNCIFY_IGNORE_INDIRECT` + explicit
ADD list is the cross-browser move. Backlog wants the browser tape lane as the safety net.

### P4. First-sight shader+pipeline compile mid-frame  [TRACKED WEB-054 · M · both, worst on web]
`wgpu_create_and_load_new_shader` (`gfx_webgpu.c:1560-1573`) and synchronous
`wgpuDeviceCreateRenderPipeline` inside the draw call (`:1744`, from `:2308`). Every new
material on level entry / first explosion / first glass = a main-thread spike; in the
browser it stacks with the ScriptProcessorNode audio deadline → hitch **plus** crackle.
**Fix**: prewarm known-hot combiner×(blend|depth) keys during level load, and/or
`CreateRenderPipelineAsync` with skip-draw-until-ready.

### P5. Frame pacer yield-spins ~2–2.5 ms every frame  [NEW · S · native]
`platform_sdl.c:3929-3937`: `SDL_Delay(rem_ms - 2.0)` then `SDL_Delay(0)` spin to the
deadline — ~12–15% of a core burned continuously. Pure thermals/battery on handhelds
(PortMaster), laptops, Steam Deck; throttling then eats real budget. Sim-neutral.
**Fix**: shrink spin window to ~0.5 ms via `usleep(500)` chunks (or SDL3 `SDL_DelayNS`),
keep the absolute deadline.

---

## Tier 2 — solid wins

### P6. Audio: main-thread synth with ~50 ms of absorber  [NEW (structure) · both]
No audio thread — full faithful synth runs at frame tail on the main thread
(`audi_port.c:435-439`, `platform_sdl.c:4003-4011`); queue occupancy target is only
1.5 audio frames (`audi_port.c:467`), drop-cap 5 native/12 web (`stubs.c:434-438`).
Any frame longer than ~50 ms ⇒ audible gap; fast loops silently *drop* backlog.
Layered fixes, cheapest first:
- [S] raise/adapt the occupancy target from device buffer + measured jitter (telemetry
  already computes `soft_target`, `audi_port.c:578-583`, controller ignores it) — also
  fixes the baked-in ~75–100 ms output latency (gunshots land 4–6 frames late).
- [M] web level-load: add `emscripten_sleep(0)` yields + audio pumps inside
  `lvlStageLoad` (blocks for seconds, `boss.c:429-433`) and prefill the 12-frame queue
  before entering — kills load-time tab-freeze + guaranteed audio gap.
- [L] native audio-synthesis thread (deterministic mode stays synchronous). ⚠ FID-0089:
  audio pump cadence leaks into hashed sim state via `sndDisposeSound` — only DSP
  synthesis may move off-thread, not voice lifecycle.
- [L] AudioWorklet (WEB-012's unledgered half): needs COOP/COEP — impossible on raw
  Pages; viable via coi-serviceworker if ever needed. SPN is on Chromium's removal track
  (future breakage, not just perf).

### P7. Memory-blend (glass/fence) render-pass splits  [TRACKED WEB-021 residual · M · TBDR GPUs]
`wgpu_snapshot_scene_for_memory_blend` ends the scene pass → T2T copy → new Load pass on
color+depth, per qualifying batch (`gfx_webgpu.c:2173-2209`, `:2320-2324`). On tile-based
GPUs (Apple Silicon = every macOS browser, all mobile) each split is a full store+reload.
**Fix**: skip empty-rect splits; coalesce consecutive memory-blend batches.

### P8. Present chain: extra full-res copy every frame + permanent `CopySrc` surface  [NEW · M · both]
`wgpu_end_frame` always T2T-copies offscreen→surface (`gfx_webgpu.c:1192-1199`);
~14.7 MB/frame at RenderScale-2 1440p. Surface configured `CopySrc` permanently (`:474`)
only for the debug dump.
**Fix**: render post-FX/minimap/overlay directly into the acquired surface when active;
keep offscreen only for readback frames; drop `CopySrc` outside the dump env.

### P9. Web delivery bundle  [NEW · all S–M · web]
- **Streaming instantiation**: `fetchWasm()` buffers the whole binary then passes
  `wasmBinary` (`mgb64-shell.js:173-180, 414-427`); the MIME excuse is disproven (Pages
  serves `application/wasm`, curl-verified). Use `instantiateStreaming` via
  `Module.instantiateWasm`, start the fetch before the `requestAdapter()` await.
- **INITIAL_MEMORY 256 MiB** (`CMakeLists.txt:2059`): orphaned WEB-062 half — measure
  high-water, drop to ~128 MiB + headroom; growth already enabled. Boot-failure/eviction
  odds on iOS Safari / low-RAM Android.
- **No LTO / no closure** on the web link (verified in `link.txt`): `-flto` +
  `--closure 1` ≈ 10–20% wasm size + halved glue JS, nearly free; validate against tape
  gate + `web_boot_smoke` (Asyncify×LTO interaction).
- **Service worker** (untracked): Pages caps cache at `max-age=600`; a hash-keyed
  cache-first SW gives instant repeat visits + offline play (ROM already in OPFS) and
  closes the WEB-032 skew window properly. Needs the 5-file deploy whitelist updated.

### P10. Per-character full bone-matrix rebuild every render pass  [NEW · M · native, sim-neutral]
`chr.c:8664-8684` re-runs `subcalcmatrices` for every skinned chr every frame (×viewports
in split-screen) even when anim state didn't change. Guard-dense levels pay most; doubles
under future F5 render-only frames. Writes only render data (unhashed) — safe with an
anim-tick generation flag; validate vs screenshot oracle (the rebuild was a correctness fix).

### P11. TLUT/texture-cache pool sweeps + CI re-decode churn  [NEW · M · both — verify at runtime first]
`gfx_dp_load_tlut` opens with two full 1024-node pool scans and deletes every texture
decoded against the replaced palette (`gfx_pc.c:20853-20854`, `:2915-2926`) — CI textures
whose TLUTs alternate within a frame get re-decoded + re-uploaded per alternation (the
only recurring texel-decode cost left). Fonts/HUD/runtime TLUTs are the exposure.
**Fix**: secondary index by palette_addr; key CI entries on palette content generation.
Confirm with a frame trace before investing.

---

## Tier 3 — small cleanups (mostly S effort)

- Per-draw `SetPipeline`/`SetBindGroup` never deduped (`gfx_webgpu.c:2484-2488`) — mirror
  the WEB-023-lite pattern; ~2 wasm↔JS crossings × ~100-200 draws/frame on web.
- Modern-mesh decor: one 96 B `wgpuQueueWriteBuffer` per draw (`gfx_webgpu.c:3028`) —
  shadow the ring, upload once per frame (WEB-052 residual).
- Combiner lookup: linear pool scan + 1-entry memo per triangle (`gfx_pc.c:14704-14713`)
  — small hash or move-to-front.
- DL dispatch linear range scans per command (`gfx_pc.c:602-609`, `:765-790`) — sort +
  binary search or per-run memo.
- Room-XLU defer path malloc/memcpy/free per batch per frame (`gfx_pc.c:14277-14284`) —
  persistent arena.
- NDC metrics: ~10 divides/tri, half wasted on culled tris; backface cull re-divides
  instead of reusing `ndc_metrics` (`gfx_pc.c:3301-3338`, `:17649-17652`).
- DSP inner loops: per-sample clamp-hit counters + xor-indexing defeat autovectorization
  (`mixer.c:587-624`); always-on per-sample peak telemetry in the pole filter
  (`mixer.c:739-763`) — hoist counters, gate stats; byte-identical, verifiable via
  `GE007_AUDIO_DUMP`. FX sections run with no silence early-out (`audio_compat.c:1850-1895`);
  ADPCM codebook byte-copied per voice per subframe (`mixer.c:324-333`).
- `GE007_MUTE` CI runs still synthesize everything then attenuate to zero
  (`audio_pc.c:1021-1035`) — skip synth when muted and no dump consumer (opt-in).
- Web per-frame JS crossings: `document.hidden` EM_JS per wait iteration
  (`platform_sdl.c:31-33`), css-size query ×2/frame (`gfx_pc.c:4637` + `platform_sdl.c:3351`),
  fresh Promise per rAF wait (`:24-26`) — cache via listeners/ResizeObserver.
- Minimap overlay: new Load/Store render pass per flush, 2-6/frame (`gfx_webgpu.c:2664-2674`)
  — one pass + mid-pass `SetScissorRect` (web default ships minimap off; native cost).
- Failed pipeline cached as live error handle → per-frame stderr/console spam
  (`gfx_webgpu.c:1745-1762`, `:285-287`) — tombstone failed keys, rate-limit `on_device_error`.
- Bind-group cache: O(512-set) scan per released view + process-lifetime pinning of up to
  2048 bind groups/textures (`gfx_webgpu.c:1383-1423`) — reverse index + level-transition flush.
- Resize storms recreate 3 full-res targets per frame during drag (`gfx_webgpu.c:734-790`)
  — debounce.
- `lvl.c:1375` uncached `getenv` per `lvlRender`; minimap dump-path `getenv` per frame
  even with minimap off (`minimap_overlay.c:144-153`) — standard static latch.
- Present mode hard-coded FIFO native (`gfx_webgpu.c:478`) — Mailbox behind an env knob;
  also an F5 prerequisite (render-only frames can't exceed refresh under FIFO).
- Hidden-tab comment wrong: background timers clamp to ≥1 s, not 1-4 ms
  (`platform_sdl.c:3903-3921`) — sim ≈1 Hz while hidden; make "hidden = paused" deliberate
  and skip synth (currently: CPU spent *and* broken ~3%-realtime audio).

---

## Constraints honored / out of scope

- **Sim is sacred**: 60 Hz integer-tick, fractional-dt permanently rejected. Everything
  above is render/platform-side or explicitly flagged. M4 room over-admission is
  gameplay-coupled (`room_rendered → chr.c:5205` auto-aim) — ship-last per plan.
- **F5 uncapped FPS** (`docs/design/UNCAPPED_FPS_PLAN.md`): not re-litigated; only the
  purity-fuzz gate has landed. P5 (pacer), FIFO knob, and `waitForNextFrame` busy-wait
  (`unk_0C0A70.c:315-328` — becomes a 100% spin the moment the cap lifts) are its
  practical prerequisites.
- **M3 draw-call work**: `RENDERING_ARCHITECTURE.md` says post-M1/M2 submission is ~7% of
  the heaviest frame — "no draw-call optimizations without a profile". P2a/P2d reduce
  *frontend* CPU and batch count, which is the documented critical path, not backend
  submission.
- **Validation harness for any of the above**: `GE007_PERF_TRACE`, `tools/perf_census.sh`
  (re-record baselines after intentional render changes), tape gate 7/7, sim-invariance
  hash, `GE007_AUDIO_DUMP`, `web_boot_smoke`, >2% frame-time regression fails review.

## Ledger reconciliation flags (bookkeeping, no code)

- WEB-062 build half (INITIAL_MEMORY drop) is in neither LANDED nor DEFERRED.
- WEB-012's AudioWorklet long-term half absent from STILL DEFERRED.
- WEB-039 chose non-streaming instantiation on a now-disproven MIME assumption.
- `PERFORMANCE_PLAN.md` header still says "not yet implemented" though M1/M2 shipped;
  its §3 census numbers are GL-era (current = `WEBGPU_BACKEND_STATUS_2026-07-13.md`).
- PRIORITIZATION.md lists AUDIT-0019 open; fixed at `cf3944a`.

## Suggested sequencing

1. **Web quick bundle** (each S, independently testable): P1 RenderScale default →
   P9 streaming instantiate + INITIAL_MEMORY → LTO/closure. Validate: `web_boot_smoke`,
   tape gate, size budget.
2. **Native quick bundle**: P5 pacer tail, P2a redundant-flush fix, P2b debug-write
   gating, P2d SETTEX map. Validate: perf census + sim-invariance + screenshot oracles.
3. **Jank killers**: P4 pipeline prewarm, P6 load-time yields + audio prefill (web).
4. **Structural** (profile first, then commit): P2 material-derivation split; P7/P8 pass
   restructuring; P6 audio thread; then F5 per its own plan.
