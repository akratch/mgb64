# PERF-005 — Async pipeline creation (kill first-sight compile hitches)

**Status:** DESIGNED, ready to implement. Deferred from the 2026-07-18 Wave 2 session
for focused execution (the async-completion drain needs empirical web-build iteration).
Vetted against the code at `d0bad20`.

## Problem
The first time `gfx_pc` meets a new combiner, the WebGPU backend builds WGSL, creates
the shader module, and creates the render pipeline **synchronously inside the draw call**
(`gfx_webgpu.c` `wgpu_pipeline_for` → `wgpuDeviceCreateRenderPipeline`, ~line 1770).
Every new material on level entry — and first-use effects mid-mission (explosion, glass)
— is a one-off main-thread spike. On web it stacks with the main-thread audio path, so
the user sees a stutter **and** hears a crackle.

## Why not static prewarm (Option A)
The full pipeline key is `(shader_id0, shader_id1) × {blend[0..6] × depth_test ×
depth_update × depth_compare × decal}` (the 8 dynamic bits packed at `gfx_webgpu.c`
~1706-1710). The blend/depth half comes from the interleaved RDP othermode state
machine and from per-frame dynamic content (chars, weapons, effects) — **not** statically
discoverable from a level's display lists without replaying a frame. So static prewarm
can't cover cold-start or first-use effects. (A record/replay manifest keyed on
`g_StageNum` is a possible additive Phase 2, but doesn't cover the true cold start.)

## Recommended: Option B — async creation, deterministic-gated, web-only slice
Covers cold-start + level-entry + mid-mission uniformly; reuses the single create site;
failure mode (brief pop-in) is strictly better than today's full-frame freeze.

### Steps
1. **Refactor `wgpu_pipeline_for`** (`gfx_webgpu.c` ~1701) into: (a) `wgpu_fill_pipeline_desc(prg, key, &pd, scratch)` that builds the descriptor from the **explicit** 8-bit key (decode → blend/depth/decal) rather than reading globals; (b) `wgpu_pipeline_for` keeps computing `key` from globals (~1706-1710) and delegates. **This is the #1 correctness guard** — async/sync must build a bit-identical descriptor (same `s_surface_format`, `s_unclipped_depth_supported`, `prg->vattrs`/`playout`).
2. **Add a PENDING state** to `WgpuPipeEntry` (`~1345`: today `{key, pipe}`) — e.g. `uint8_t state` EMPTY/PENDING/READY/FAILED, or treat `key-set + pipe==NULL + pending flag`.
3. **On cache-miss**:
   - `if (g_deterministic || !__EMSCRIPTEN__)` → **synchronous** `wgpuDeviceCreateRenderPipeline` (today's exact code). Keeps native fully sync and web deterministic sync → **every byte-exact gate stays green** (parity/screenshot/tape all run `--deterministic`).
   - else (web live) → insert PENDING entry, call `wgpuDeviceCreateRenderPipelineAsync` (`WGPUCallbackMode_AllowSpontaneous`, `userdata1=prg`, `userdata2=(void*)(uintptr_t)key`), return NULL. `wgpu_draw_triangles` already early-outs on NULL (`~2335`) → batch skipped this frame (transient pop-in, not permanent loss).
4. **Callback** `on_pipeline_ready(status, pipeline, msg, prg, key)`: match the sub-cache slot by key; set `pipe`+READY on `Success`; mark FAILED + log on error (avoid a re-kick storm). Slot-eviction ABA is unreachable (whole game ≪ `WGPU_SHADER_MAX=1024`); mirror the WEB-068 handle discipline if paranoid.
5. **Completion drain (the empirical unknown):** emdawnwebgpu is documented to fire `AllowSpontaneous` callbacks on its own event loop during the per-frame rAF yield, so **no explicit drain may be needed**. If `web_boot_smoke` shows the canvas stays incomplete/black after warmup (pipelines never complete), add a guarded per-frame `wgpuInstanceProcessEvents(s_instance)` in `wgpu_end_frame` **only when a PENDING count > 0** (skip when nothing is pending, to avoid firing unrelated futures). Validate empirically.

### Scope decision: WEB-ONLY async (native stays sync)
Native LIVE (non-deterministic) rendering has **no headless coverage** (all native tests
are `--deterministic` → sync path), so a broken native async-poll would ship untested.
`web_boot_smoke` runs non-deterministic and checks non-black after warmup → it **is** the
catch-net for the web async path. So: async only when `!g_deterministic && __EMSCRIPTEN__`;
native always sync (zero risk, no native win). Native prewarm (Phase 2) can follow once a
native non-deterministic render smoke exists.

## Validation
- **Byte-exact (must stay green, by construction):** `port_renderer_parity_smoke`,
  screenshot/oracle suites, `sim_invariance_gate.sh`, tape gate — all `--deterministic`
  → sync path → identical to HEAD.
- **Web async machinery:** `web_boot_smoke` MUST pass. If pipelines never complete, boot
  renders black after warmup → smoke FAILS → revert. **This is the safety gate: no silent
  regression can ship — it either works (green) or is caught (red).**
- **Spike measurement:** `GE007_PERF_TRACE=1` prints per-frame `interval_ms`; add a
  max/p99 reduction over the entry frames (drop the census warmup skip). First-effect
  repro: `GE007_AUTO_DAMAGE_TAG=19` on Dam (glass), plus a mid-mission explosion.

## Risks
1. **Divergent descriptor** (async builds a different pipeline than the draw) → structural fix: both go through `wgpu_fill_pipeline_desc`. Highest-value guard.
2. **Determinism** → hard-gated off under `g_deterministic`. Non-negotiable.
3. **Pop-in** → transient (renders once the pipeline lands, 1–few frames); confirm no *load-bearing* first-frame geometry stays dropped on Dam entry.
4. **Completion drain** → the empirical unknown; `web_boot_smoke` decides whether the guarded ProcessEvents is needed (see step 5).
