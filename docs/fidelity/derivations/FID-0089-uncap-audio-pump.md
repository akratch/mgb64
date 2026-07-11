# FID-0089 — render-only (0-tick) frames advance SFX voices, leaking audio cadence into hashed sim state

Status: **fix landed** (default ON, negative control `GE007_NO_UNCAP_AUDIO_FIX=1`).
Found by the FID-0033 full `uncap_purity_gate.sh` (20-level matrix), which the
FID-0014 QUICK run (33/9/24) never exercised. Exposed — not introduced — by the
FID-0014 patrol-magic merge.

Report: `docs/fidelity/reports/verify_fb9beceae183_uncap_purity_gate_2.log`
(levels 22 and 24 RED: "render-only frames perturbed sim state (vanilla stable)").

## 1. Symptom

`tools/uncap_purity_gate.sh` (mode=full) went RED on **level 22 and level 24**
after the FID-0014 merge (main `2eceb40..32c6d19`). The gate runs a level twice —
vanilla vs `GE007_UNCAP_FUZZ=<seed>` (a seeded schedule that turns ~75% of loop
iterations into RENDER-ONLY / 0-tick frames) — to the same SIM time
(`--screenshot-game-timer 900`) and asserts an identical final sim-state hash.
Under fuzz the two runs' hashes diverged deterministically (e.g. level 22 seed
1337: vanilla `770360a2a8022ccf` vs fuzz `4c1e45bdbef0170f`).

FID-0033's own verified note (2026-07-10) recorded the full 20-level matrix GREEN
(19/20, Streets/29 pre-existing UNSTABLE). So this is a regression *exposure*
introduced by the FID-0014 world change, on a rail FID-0014's quick run skipped.

## 2. Localization (evidence, not assumption)

The task's top suspect was the FID-0014 sim-field stamp
(`act_patrol.lastvisible60` / `act_gopos.unk9c`). **Refuted by evidence:**

- `chrTickBeams` (where all FID-0014 code lives) is reached only from `lvlRender`,
  which `boss.c:615` (`if (g_pcUncapRenderOnlyFrame) goto pc_fuzz_render_only_skip`)
  jumps over on a render-only frame. So FID-0014 code never runs on a 0-tick frame.
- Instrumenting every FID-0014 visibility evaluation (g_GlobalTimer, camera-matrix
  hash, `vis`, prop pos) and diffing vanilla vs fuzz: **3496/3496 lines byte-identical.**
  The stamp and the magic pos-sync are cadence-invariant.
- Per-region hash breakdown (`GE007_SIM_HASH_PER_REGION`): only region `pool`
  diverged; `prop_pool`, `stan:*`, `g_BgRoomInfo`, `g_randomSeed` all identical.
- 3-way raw pool dump (van1/van2/fuzz, subtracting ASLR pointer noise): **exactly
  one** 8-byte word diverged — pool offset `0x061870`, `NULL` in vanilla vs a live
  pointer `0x…` in fuzz.
- Owner: `pos_data_entry` prop of `type==3` (guard), ChrRecord field native
  offset `0x1b0`. `offsetof(ChrRecord, ptr_SEbuffer1) == 0x1b0`,
  `sizeof(ChrRecord) == 568 == 0x238` (= the observed chr-alloc stride).
- Decisive control: with `GE007_NO_PATROL_MAGIC_FIX=1` (FID-0014 OFF) the fuzz run
  is byte-pure (`e27b70070d990f14` == vanilla). FID-0014's world change is
  *necessary* to expose the leak; the leaking field is not one it writes.

The divergent field is **`ChrRecord.ptr_SEbuffer1`** — an `ALSoundState*` handle
for a guard's fired-weapon SFX (set by `sub_GAME_7F02BFE4` via `sndPlaySfx`,
`chrlv.c:7147`), which lives in the hashed 8 MB game pool.

## 3. Root cause

`ptr_SEbuffer1` is NULLed lazily: `sndGetPlayingState()` (`snd.c:623`) checks
whether the voice's samples are exhausted (`portAudioVoiceIsActive`) and, when they
are, calls `sndDisposeSound()` → `*meta->ownerSlot = NULL` (`snd.c:602`), writing
NULL back into the ChrRecord slot.

Voice sample consumption is advanced by `portAudioFrame()`
(`src/platform/audi_port.c:417` → `portAudioMixSfxIntoBuffer` →
`portAudioMixActiveVoices`), which `platformFrameSync()` calls **once per loop
iteration** (`platform_sdl.c:3407`) — i.e. on EVERY frame, including render-only
0-tick fuzz frames. (The SDL device is opened in queue mode, `want.callback = NULL`
at `audio_pc.c:1082`, so `audioCallback` is dead code; the game-thread
`portAudioFrame` pump is the sole, deterministic voice advancer — which is why
vanilla is stable.)

Consequence: under `GE007_UNCAP_FUZZ` there are ~3.6× more loop iterations per sim
tick, so a guard's gunshot voice is drained ~3.6× faster in sim-tick terms. It
therefore finishes — and its handle is disposed/NULLed — at a **different
g_GlobalTimer** than the all-tick vanilla run. On levels 22/24 the FID-0014
faithful magic-freeze shifts guard fire timing so a gunshot's finish straddles the
gate's exit tick cadence-sensitively; the OK levels have no such straddle.

On the N64 the audio task runs once per video frame == once per 60 Hz sim tick;
there are no 0-tick frames, so retail never exhibits this. The port's
per-loop-iteration pump advancing audio that the sim reads back is the divergence.
Classification: **port-defect** (render/audio cadence → hashed sim state).

## 4. Fix (default ON; `GE007_NO_UNCAP_AUDIO_FIX=1` = legacy per-frame pump)

`platform_sdl.c` — skip `portAudioFrame()` on a render-only (0-tick) frame:

```c
if (!(g_pcUncapRenderOnlyFrame && portUncapAudioSkipEnabled())) {
    portAudioFrame();
}
```

A 0-tick frame passes no sim time, so it advances no audio — mirroring boss.c's
render-only skip of sim + present + maintenance, and matching the N64's
once-per-frame == once-per-tick audio task. `g_pcUncapRenderOnlyFrame` is 0 in all
normal play (armed only by the `--deterministic` fuzz harness, never in play), so
the deterministic faithful path is **byte-identical** (charter rule 5). The fix
makes the fuzz run's SFX-voice advance count equal the vanilla tick count, so the
`ptr_SEbuffer*` disposal is sim-tick-locked and hash-pure.

## 5. Proof

- Purity gate: level 22 seed 1337/4242 GREEN (`770360a2a8022ccf`); level 24 GREEN
  (`a8509ca96ee69294`); quick set (33/9/24) + patroller levels 20/27/43 GREEN.
- Negative control: `GE007_NO_UNCAP_AUDIO_FIX=1` under fuzz → RED again (both 22/24).
- `port_patrol_magic_profile_smoke` PASS both polarities (freeze/warp profile
  unchanged — the fix is audio-only, does not touch the patrol sim).
- `tape_regression` 7/7 byte-exact (isolated) — sim-tick behavior unchanged.

## 6. Residual concern (separate, pre-existing; NOT this fix's scope)

There is a distinct, rare frame-timing determinism gap orthogonal to the audio
leak. The deterministic bug this fix closes is stable: level 22 seed 4242 runs at
render frame count 3271 every time, fix-OFF `dc61cb8c…` (6/6), fix-ON `770360a2…`
(vanilla, 6/6). But `waitForNextFrame`'s synthetic-clock loop (`unk_0C0A70.c:315`,
`osGetCount`/`pcAdvanceDeterministicCountForFrame`) can *rarely* settle on a
different loop-iteration count under load, shifting the deterministic fuzz xorshift
schedule to a different render-only pattern (observed once: level 22 seed 4242 at a
frame count ≠ 3271 producing `a30bd21e…`, ~1 in 13 runs). Because the fuzz schedule
is closed-loop on the iteration count, a shift re-patterns the 0-tick frames and can
expose a residual render→sim coupling (not the audio one — that stays sim-tick-locked
under this fix regardless of pattern). The same class flaked two long combat tapes
once under a 4-way concurrent game load (bunker1_traverse, dam_forward_30s; both
stable-PASS 3/3 in isolation on clean and fixed binaries). This is a pre-existing
frame-timing/harness gap (fix-OFF exhibits the identical `osGetCount` timing), not
introduced by this fix. A full close is F5 uncapped-FPS scope (pin the deterministic
loop-iteration count so the fuzz schedule cannot shift). Documented, not fixed here.
