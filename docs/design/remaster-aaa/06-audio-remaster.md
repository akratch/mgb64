# W6 — Audio Remaster: Synth Fidelity + Enhancement Layers

**Workstream:** W6 of the MGB64 AAA Remaster program.
**Parent plans:** [REMASTER_ROADMAP.md](../REMASTER_ROADMAP.md) (the constitution — rails R1/R2/R3) and
[AUDIO_QUALITY_PLAN.md](../AUDIO_QUALITY_PLAN.md) (the fidelity investigation this workstream executes and extends).
**Branch baseline:** `feat/metal-backend` (audio code is renderer-independent; nothing here touches GL/Metal).
> ✔ Anchors in §2.3 and the W6.E1.T1-T5 + W6.E3.T1 task rows (the PM2 slice) were
> **re-verified against `main` @ `096249c` on 2026-07-03** — audio-side anchors
> (`audi_port.c`, `mixer.c`, `music.c`, `audio_compat.c`, `audio_pc.c`, `snd.c`)
> are all current; only the frame-pacing-shifted `platform_sdl.c` preset anchor
> moved. Anchors in OTHER sections may have drifted since authoring — note in
> particular the presets are now `s_faithfulPreset` `platform_sdl.c:1942` /
> `s_remasterPreset` `:1985` (were `:1917`/`:1960`, e.g. in §2.6/§4.1) —
> re-verify before dispatch.

---

## 1. Executive summary

The port's audio synthesis chain is a faithful reimplementation of N64 libaudio + the RSP audio microcode — ADPCM decode, the authentic 64-phase×4-tap polyphase resampler, envelope mixer, and Rare's 6-section custom FX all verified correct (`src/platform/mixer.c`, `src/platform/audio_compat.c`); the one true decoder bug (RAW16 byte order) is already fixed default-on (AUDIO_QUALITY_PLAN.md §RESOLVED). What remains splits cleanly in two: **(a) finish the fidelity plan** — measure against a real N64 capture, then land the ranked hypotheses H1 (output low-pass, knob exists default-off at `audi_port.c:131`), H2 (`env_sample_xor`, `mixer.c:588`), H3 (per-instrument bank defects, Surface 2 worst); and **(b) build the opt-in remaster layers** — a working per-bus volume system (today `Audio.MasterVolume` does not even touch the main synth output), a 44.1/48 kHz output-stage resampler, room-keyed algorithmic reverb, stereo width, and **music/SFX pack loaders** modeled on the shipped HD-texture loader (`texture_pack.c`) so users supply their own high-fidelity audio locally (Tier B stays local; the loaders are Tier A1 and ship). Everything is `GE007_*`/`Audio.*`-gated, default-identity, and validated with the existing deterministic audio-dump + reference-comparison harness.

| # | Headline deliverable | Player-visible result |
|---|---|---|
| 1 | H1/H2/H3 fidelity fixes, reference-fitted | Music/SFX timbre matches real N64 capture within measured band-energy budgets; Surface 2 no longer "crappy MIDI" |
| 2 | Per-bus volume (Master/Music/SFX) + working master | Sliders that actually work, live, persisted in `ge007.ini`, exposed to the W5 menu |
| 3 | Output-chain remaster: 48 kHz out, room reverb, stereo width | Warmer, wider, roomier mix under `--remaster`; byte-identical when off |
| 4 | Music pack loader (`<pack>/music/trackNN.ogg`) | Users drop in re-orchestrated/AI-enhanced soundtracks; seamless engine integration incl. fades |
| 5 | SFX pack loader (`<pack>/sfx/sfxNNNN.wav`) | Per-sound HD replacement with engine pan/volume/position still applied |

Total estimate: **96 junior-days (~19 junior-weeks)** across 5 milestones, each independently demoable.

---

## 2. Current state (verified, file:line)

### 2.1 Architecture — one real engine, one dead, one half-alive

- **Engine A (faithful libaudio)** — `src/platform/audio_compat.c` (5150 L): bank parsing (`bank_make_adpcm_book` `audio_compat.c:99`, `bank_make_adpcm_loop` `:139`, instrument pan at `:340`), CSP MIDI sequence players, voice allocation, and the frame driver `alAudioFrame` (`audio_compat.c:2245`) which walks the synth client/filter chain and emits mixer ops.
- **DSP core** — `src/platform/mixer.c` (823 L) executes those ops: ADPCM (`mixerADPCMdec` `:381`), the authentic polyphase resampler (`mixerResample` `:441`, canonical N64 table `:189-222`), envelope mixer with the contested `i ^ env_sample_xor` ordering (`:588`), mix (`:644`), pole filter (Rare custom-FX sections, `:683`), and the shipped RAW16 byte-swap fix (`mixerLoadBufferSwap16` `:288`, default-on, `GE007_DISABLE_RAW16_BYTESWAP` hatch).
- **Frame pump** — `src/platform/audi_port.c`: `portAudioFrame()` (`:368`) synthesizes one game-frame of audio at **22050 Hz** (`OUTPUT_RATE 0x5622` `:24`), applies the (default-off) H1 low-pass (`:415`), optionally dumps music PCM (`:418-426`), mixes PortVoice SFX (`:428`), and queues to SDL (`osAiSetNextBuffer` `:431` → `stubs.c:450`, single unified queue-mode SDL device opened at 22050 Hz stereo in `audio_pc.c:911-940`).
- **SFX** — real `ALSndPlayer` path is default (`PORT_SOUNDPLAYER_REAL` ON, `CMakeLists.txt:402-416`): `sndPlaySfx` `snd.c:3153` (bare `snd.c`/`music.c` refs throughout = `src/snd.c`/`src/music.c`; `bg.*`/`bondview.c` = `src/game/`) → `sndPlaySfxInternal` `:3170` → event queue → `alSynStartVoice` (started at `snd.c:1704`; **snd-player voice volume flows exclusively through `alSynSetVol`**, `snd.c:1726-1727` + update handlers `:1903/:1938/:1967` — this is the SFX-bus choke point, §4.3). The stub PortVoice path (`audio_pc.c`) is compiled but idle — **however its mixer (`portAudioMixSfxIntoBuffer` `audio_pc.c:1200`) still runs every frame** (`audi_port.c:428`) and its voice API (`portAudioPlaySoundDetailedInternal` `audio_pc.c:976`) is live infrastructure we will reuse for the SFX pack (§4.8).
- **Engine B (simple synth)** — `audio_pc.c` `musicNoteOn`/`musicMixSamples`: dead code, not a factor.

### 2.2 Positional/pan source data (exists — answers the HRTF question)

- 3D position → pan already flows: `sndSetPositionHint` (real-path statics at `snd.c:1188-1191`), `sndComputePanFromPosition` (`snd.c:1195-1208`) calls `portAudioComputeSpatialMix` (`audio_pc.c:736`) and live updates post `AL_SNDP_PAN_EVT` (`snd.c:1240`, handled at `:1820`, counted `panUpdates` `:1831`).
- Pan resolution is **ALPan 0–127** through a 128-entry equal-power table (`NATIVE_EQPOWER_LENGTH 128`, `audio_compat.c:1218-1275`; applied in `native_env_target_left/right` `:1334-1347`). Music channel pan: `__vsPan` (`audio_compat.c:2897`).
- **Constraint:** all voices collapse into one stereo dry bus + one mono wet bus inside DMEM before output — there is no per-source bus at the output stage. True HRTF would require per-voice bus splitting in `alAudioFrame` (major surgery). See §7 R5.

### 2.3 Volume plumbing today (the surprise finding)

- `Audio.MasterVolume` is registered (default 0.7, LIVE scope, `GE007_MASTER_VOLUME` env, `audio_pc.c:859-863`) but is applied **only** in the idle PortVoice mixer (`audio_pc.c:793`) and the dead simple synth (`:2056`). **The real music+SFX output (the libaudio mix queued at `audi_port.c:431`) ignores it entirely.**
- Music volume: per-track via `musicTrackNApplySeqpVol` (`music.c:1300`, track2/3 analogues) → `alCSPSetVol`; fades tick in `musicFadeTick` (`music.c:1921`). SFX volume: `g_sndSfxVolumeScale` (`snd.c:1408`) via `sndSetScalerApplyVolumeAllSfxSlot` (`snd.c:3472`). These are the game's *internal* mix levers — no user-facing bus settings exist.

### 2.4 Reverb, low-pass, and the H-hypotheses

- Rare's 6-section custom FX (echo/reverb) is ON by default: table `s_portAudioCustomFxParams` (`audi_port.c:46-54`) passed to `alInit` (`:326-331`); kill switch `GE007_DISABLE_NATIVE_REVERB` (`:323`). This is *faithful* reverb — the remaster reverb (§4.5) is a separate, additive layer.
- **H1 knob:** `portAudioApplyLibaudioLowPass` (`audi_port.c:131-169`), one-pole, alpha Q15 = 26840 (`:44`), enabled only by `GE007_ENABLE_LIBAUDIO_LOWPASS` (`:139`), latched once. Comment at `:42-43`: current Ares captures matched better *without* it — the alpha is a guess and must be fitted (AUDIO_QUALITY_PLAN.md Phase 1).
- **H2 knob:** `GE007_MIXER_ENV_SAMPLE_XOR` / `GE007_MIXER_POLE_SAMPLE_XOR` (defaults 0, `mixer.c:39-40`, read in `mixerInit` `:226-242`); per-section pole override `GE007_MIXER_POLE_FC_XOR_MASK` (`:108-142`).
- **H3 instrumentation:** per-note MIDI trace `GE007_MUSIC_MIDI_TRACE_JSONL` (`audio_compat.c:3226`), program solo/mute `GE007_MUSIC_SOLO_PROGRAMS` (`:3300`).
- **H4:** DMA free-list exhaustion returns a recycled pointer (`amDmaCallback` `audi_port.c:197-201`); window 64×1024 B (`:35-36`). **H5:** 24 phys voices / 16 per seq player (`music.c:52,55`); steal in `alSynAllocVoice` (`audio_compat.c:4867+`).

### 2.5 Harness & determinism (all exists)

- `GE007_AUDIO_DUMP` — raw s16le 22050 final-mix dump at the queue write (`stubs.c:510-515`, writer `port_trace.c:8307`). `GE007_MUSIC_AUDIO_DUMP` — synth dump taken *before the PortVoice mix* (`audi_port.c:418-426`, writer `port_trace.c:8310`). **Tap semantics matter:** the "music" dump is everything `alAudioFrame` renders — music **and** real-path SFX voices (both live in the same libaudio synth); it excludes only the (idle) PortVoice mix. Bus-acceptance recipes in §5 exploit this: mute one bus and the dump isolates the other.
- `GE007_AUDIO_TRACE` — per-frame JSONL with per-stage clamp counters, xor state, voice/queue stats (`audi_port.c:519-601`).
- **Determinism:** `g_deterministic` forces Bresenham 720/736 frame sizing (`audi_port.c:388-392`) and `osAiGetLength()==0` (`stubs.c:517-520`) → audio dumps are bit-reproducible → byte-identical A/B gating is possible.
- Tools: `tools/compare_audio.py` (regression gate, positional `baseline.raw test.raw`: RMS diff ≤ 500, ZCR/silence checks), `tools/compare_audio_reference.py` (envelope-aligned band-energy fidelity vs an emulator/hardware WAV **or raw** capture; threshold flags at `:823-830`), `tools/startup_music_reference_check.sh` (end-to-end capture+compare wrapper: builds, sets `GE007_MUSIC_AUDIO_DUMP`/`GE007_AUDIO_DUMP` itself, runs `--deterministic`, always passes `--print-bands` + `--json-out <out-dir>/boot_audio_compare.json`), `tools/ares_startup_audio_reference.sh` + `tools/prepare_ares_audio_dump_build.sh` (reference capture from Ares — **requires the locally built instrumented ares**; stock ares fails by design; output is raw s16le 44.1 kHz, `ares_boot_44100.raw`, not a WAV — full protocol in W6.E1.T1).
- Contamination guard **already covers audio**: `.gitignore:79-83` (`*.raw/*.wav/*.mp3/*.ogg/*.flac`) and `scripts/ci/check_no_rom_data.sh:55-63` hard-fail tracked audio formats. R2 enforcement for packs is already in place.

### 2.6 Settings & preset machinery (W5 tie-in surface)

`settingsRegisterInt/Float/String` (`src/platform/settings.h:53-66`) with LIVE/RESTART scope, env alias, CLI override; `ge007.ini` load/save lives in `src/platform/config_pc.c` (save block at `config_pc.c:272+`; note `--faithful`/`--remaster` sessions suppress saving, `config_pc.c:440-458`). Presets: `s_faithfulPreset` (`platform_sdl.c:1917`) / `s_remasterPreset` (`:1960`) — **neither contains a single `Audio.*` key today.** The pack-loader pattern to copy: `texture_pack.c` (`Video.TexturePack` string setting, validate-once with loud warning `:40-62`, per-token miss cache `:29`, graceful fallback).

---

## 3. Target state — the AAA bar

A reviewer running `build/ge007 --remaster` hears:

1. **N64-correct timbre, not "crappy MIDI":** the mix matches a real-hardware capture within `compare_audio_reference.py` budgets (band MAE ≤ 8 dB, high bands ≤ 10 dB, spectral cosine ≥ 0.90 — the tool defaults, `compare_audio_reference.py:824-830`) on the boot theme, Dam, and Surface 2. Deep bass is clean (RAW16 fix already shipped), high end has the console's analog warmth (fitted H1), sustained pads on Surface 2 are loop-clean (H3).
2. **A modern output chain:** the device runs at 48 kHz with a high-quality final-stage upsampler (the *authentic* 22050 Hz synth untouched), a subtle room-keyed reverb tail that opens up in the Facility halls and dries out on Surface's exterior, and adjustable stereo width. All opt-in; `--faithful` (and all-flags-off) is byte-identical raw 22050 output.
3. **Real audio settings:** Master/Music/SFX sliders that work live and persist; visible in the W5 pause menu.
4. **User soundtrack & SFX packs:** drop `mypack/music/track02.ogg` next to the game, set `Audio.MusicPack=mypack`, and the boot theme is your re-orchestration — engine fades, track switching, and volume still work. Same for `sfx/sfx0042.wav` with engine 3D pan intact. Missing files fall back to stock per-asset. The repo ships only the loader (A1); AI-enhanced ROM-derived packs are generated and kept locally (Tier B), exactly like texture packs.
5. **Regression-proof:** every landing gated by deterministic byte-identical-off checks + `compare_audio.py` on a fixed capture; clamp-counter deltas flat; sim-invariance hash green.

---

## 4. Technical design

### 4.1 New settings surface (one table, registered once)

All registered in `portAudioRegisterConfig()` (extend the existing block at `audio_pc.c:857-868`), following `settings.h:53-66`. Every key gets a `GE007_*` env alias (named explicitly at registration, e.g. `GE007_MASTER_VOLUME` at `audio_pc.c:860` — settings keys themselves are **not** env vars). **This table is the canonical, final key/alias list**: W5.E2 menu rows (doc 05) render from the settings registry, and every task below uses exactly these names — do not improvise variants.

| Key | Env alias (created by) | Type/range | Default | Scope | Consumed by |
|---|---|---|---|---|---|
| `Audio.MasterVolume` | `GE007_MASTER_VOLUME` (exists, `audio_pc.c:860`) | f32 0–1 | 0.7 | LIVE | **new** final-mix scaler (§4.3) + existing PortVoice path |
| `Audio.MusicVolume` | `GE007_MUSIC_VOLUME` (E3.T1) | f32 0–1 | 1.0 | LIVE | `musicTrackNApplySeqpVol` multiplier + pack-stream gain |
| `Audio.SfxVolume` | `GE007_SFX_VOLUME` (E3.T1) | f32 0–1 | 1.0 | LIVE | `alSynSetVol` snd-voice scaler (§4.3) + SFX-pack voice gain |
| `Audio.OutputFilter` | `GE007_OUTPUT_FILTER` (E1.T2) | int 0/1 | 0 | LIVE | H1 low-pass (§4.2); `--remaster` sets 1 after fitting |
| `Audio.OutputFilterAlpha` | `GE007_OUTPUT_FILTER_ALPHA` (E1.T2) | int 1024–32767 | *fitted* | LIVE | H1 alpha Q15 (replaces the hardcoded 26840) |
| `Audio.OutputRate` | `GE007_OUTPUT_RATE` (E3.T2) | int {22050,44100,48000} | 22050 | RESTART | device open + final resampler (§4.4) |
| `Audio.RoomReverb` | `GE007_ROOM_REVERB` (E3.T3) | f32 0–1 (wet) | 0 | LIVE | algorithmic reverb (§4.5); `--remaster` sets 0.25 |
| `Audio.StereoWidth` | `GE007_STEREO_WIDTH` (E3.T4) | f32 0–2 | 1.0 | LIVE | mid/side width (§4.6); `--remaster` sets 1.2 |
| `Audio.PanLaw` | `GE007_PAN_LAW` (E3.T4) | f32 0.5–2 (exponent) | 1.0 | RESTART | eqpower-table build exponent (§4.6); default = byte-identical table |
| `Audio.MusicPack` | `GE007_MUSIC_PACK` (E4.T2) | string path | "" | RESTART | music pack loader (§4.7) |
| `Audio.SfxPack` | `GE007_SFX_PACK` (E5.T1) | string path | "" | RESTART | SFX pack loader (§4.8) |
| `Audio.EnhancedPolyphony` | `GE007_ENHANCED_POLYPHONY` (E2.T4) | int 0/1 | 0 | RESTART | H5 voice-cap bump (§4.9) |

`--faithful` preset additions (`s_faithfulPreset`, `platform_sdl.c:1917`): pin `Audio.OutputFilter=0, Audio.OutputRate=22050, Audio.RoomReverb=0, Audio.StereoWidth=1, Audio.PanLaw=1, Audio.MusicPack=, Audio.SfxPack=, Audio.EnhancedPolyphony=0`. `--remaster` (`:1960`): `Audio.OutputFilter=1, Audio.OutputRate=48000, Audio.RoomReverb=0.25, Audio.StereoWidth=1.2` (packs stay user-set — presets must not point at nonexistent local dirs).

**R3 note:** with every key at default, the queue write at `stubs.c:450` receives bit-identical bytes (verified by the identity gate, §8.2).

### 4.2 H1 — output low-pass: fit, settings-ize, promote (executes AUDIO_QUALITY_PLAN Phase 1)

The knob exists (`audi_port.c:131-169`); three defects to fix:

1. **Latched env read** (`:138-146`): replace with the two settings above (LIVE — re-read each frame; the settings registry gives lock-free f32/s32 reads). Keep `GE007_ENABLE/DISABLE_LIBAUDIO_LOWPASS` behavior via the env alias for old scripts.
2. **Unfitted alpha:** the fit procedure (offline, one-time):
   - Capture reference: run the E1.T1 protocol once (instrumented-ares build + `tools/ares_startup_audio_reference.sh --ares-bin <instrumented> --out-dir ~/mgb64_refs`; default out-dir is a throwaway `/tmp/mgb64_ares_audio_ref_$$` — pass a persistent dir; output is `~/mgb64_refs/ares_boot_44100.raw`; local, Tier B — never committed; already gitignored).
   - Capture ours per alpha: `GE007_ENABLE_LIBAUDIO_LOWPASS=1 GE007_OUTPUT_FILTER_ALPHA=<a> tools/startup_music_reference_check.sh --no-build --reference ~/mgb64_refs/ares_boot_44100.raw --reference-format raw --reference-raw-rate 44100 --out-dir /tmp/alpha_<a>` (the script sets `GE007_MUSIC_AUDIO_DUMP` itself; `GE007_OUTPUT_FILTER_ALPHA` is the alias E1.T2 registers for `Audio.OutputFilterAlpha` — use the env alias, not the `Audio.*` name: env vars propagate through the script, `--config-override` does not).
   - Sweep alpha ∈ {32767, 30000, 28000, 26840, 24000, 20000, 16000} and pick the minimizer of `high_band_mae_db` read from each `/tmp/alpha_<a>/boot_audio_compare.json` (bands ≥ 2560 Hz per `compare_audio_reference.py` `BANDS_HZ`, `:19`). One-pole at 22050 Hz: `fc ≈ -fs/(2π)·ln(1-α/32768)`; α=26840 ≈ 6.2 kHz — plausibly too dark; the sweep decides, not taste.
   - **Note the code comment (`audi_port.c:42-43`)**: prior captures matched better with the filter OFF. If the sweep confirms OFF is the best fit, H1 resolves as "no missing coloration"; ship the settings plumbing anyway (harmless, useful knob) but do **not** add it to `--remaster`. This is the honest outcome path.
3. **Ordering:** filter currently runs before the PortVoice SFX mix (`:415` vs `:428`); once §4.3 restructures the tail of `portAudioFrame`, run it after all sources so pack audio is filtered consistently (filter is only meaningful in 22050 mode; at 48 kHz output apply it pre-resample to the synth bus only — it models the console DAC, which pack audio never passed through).

### 4.3 Per-bus volume + final-mix stage (fixes the dead master)

Restructure the tail of `portAudioFrame` (`audi_port.c:413-431`) into an explicit chain:

```
alAudioFrame(...)                          /* faithful synth @22050, untouched      */
→ portAudioApplyLibaudioLowPass(...)       /* H1, synth bus only                    */
→ [music-pack stream mix]                  /* §4.7, replaces muted CSP tracks       */
→ portAudioMixSfxIntoBuffer(...)           /* PortVoice: SFX pack + legacy          */
→ portAudioApplyRoomReverb(...)            /* §4.5, default 0 = exact passthrough   */
→ portAudioApplyStereoWidth(...)           /* §4.6, default 1.0 = exact passthrough */
→ portAudioApplyMasterVolume(...)          /* NEW — the master fix                  */
→ [output resampler §4.4]                  /* 22050 default = exact passthrough     */
→ osAiSetNextBuffer(...)
```

`portAudioApplyMasterVolume`: Q15 integer multiply, and **identity-bypass when volume == 1.0** so default output is bit-identical *(but note: registered default is 0.7 — see Open Question Q1; until resolved, default master to 1.0 on the synth bus and keep 0.7 only for the PortVoice path it already governed, preserving today's bytes)*.

- **Music bus:** multiply inside `musicTrackNApplySeqpVol` (`music.c:1300` + track2/3): `t1 = t1 * musicBusQ15 >> 15` before `alCSPSetVol`. This single choke point catches direct sets, fades (`musicFadeTick` `music.c:1921`), and game callers (`bondview.c:20605`). Live changes: re-apply via `musicTrackNApplySeqpVol(musicTrackNGetVolume())` on setting change.
- **SFX bus:** the game owns `g_sndSfxVolumeScale` (`snd.c:3472` setter); do **not** write it (sim-owned mixing state). The choke point is `alSynSetVol` (`audio_compat.c:4931`): the snd player starts voices with `alSynStartVoice` (no volume arg, `snd.c:1704`) and drives **all** SFX voice volume through `alSynSetVol` (`snd.c:1726-1727` + `:1903/:1938/:1967`), so scale `volume` there by sfxBusQ15 for snd-player voices only. Attribution: the seq player tags its voices via `voice->clientPrivate = voice_state` (`audio_compat.c:2725`; field at `include/PR/libaudio.h:279`) — snd.c never sets it; make attribution sound by zeroing `voice->clientPrivate` in `alSynAllocVoice` (`audio_compat.c:4867`, one line), then `clientPrivate == NULL` ⇔ snd-player voice. Do **not** scale in `alSynStartVoiceParams` (`audio_compat.c:4994`) — its only caller is the seq player (`:3562`); it never sees SFX voices. Simpler alternative rejected: scaling the dry/wet output crushes music too.

### 4.4 Output-rate option — enhancement *beyond* the authentic polyphase

The N64 polyphase (`mixerResample`) is authentic per-voice pitch-shifting and stays untouched (R-faithful). The enhancement is a **final-stage** upsampler so the host DAC never runs at 22050 (many drivers resample cheaply) and so pack audio can play at native quality:

- `audio_pc.c:917`: `want.freq = s_audioOutputRate` (from `Audio.OutputRate`). `stubs.c` queue math is byte-based and follows automatically **except** `portAudioGetFrameSize()` consumers — audit `stubs.c:451-465` (`nominal_frame_bytes`) and scale by `rate/22050`.
- New `src/platform/audio_resample_out.c`: windowed-sinc polyphase, 32 taps, Kaiser β=8.6, fixed L/M ratios (22050→44100 = ×2 i.e. L=2/M=1; 22050→48000 = L=320/M=147; precompute the phase table at init, 320 phases × 32 taps s16 = 20 KB). Stereo, stateful across frames (32-sample history). Float accumulate, clamp to s16. This is textbook DSP a junior can implement from: `y[n] = Σ_k h[(nM mod L) + kL] · x[floor(nM/L) - k]`.
- **Deterministic mode forces 22050** (`g_deterministic` check at init) so all existing golden dumps stay valid; a separate 48 kHz golden lane is added in §8.
- Music/SFX pack streams (already at device rate) mix **after** the resampler in 44.1/48 k mode — pack audio is never downsampled through 22050. In 22050 mode packs are downsampled at load (packs still work everywhere, just capped).

### 4.5 Room-keyed algorithmic reverb (convolution-free) — feasibility CONFIRMED

**Key question answered from code:** room geometry is available render-side. `g_BgRoomInfo[room_id].minbounds/.maxbounds` are per-room AABBs (`s_room_info` fields, `src/game/bg.h:46-47`; corner walk `src/game/bg.c:1299-1301`; world-space via the `room_data_float2` scale, `bg.c:515-520` — the accessor must apply the same scale); the player's room is `g_BgCurrentRoom` (defined `bg.c:2726`) / `bondviewGetCurrentPlayersRoom()` (`src/game/bondview.c:21788`, returns `u8`; use player 1 in split-screen). Audio reading sim state is the **allowed direction** under R1 (render/presentation may read sim; sim must never read us) — same contract as the renderer's room attribution. The reverb writes nothing back; the R3 identity + P0.2 sim-hash gates prove it (§8).

**Algorithm** (`src/platform/audio_reverb.c`, ~250 lines): Schroeder/Freeverb-lite — 4 parallel comb filters + 2 series allpasses per channel, stereo-decorrelated by +23-sample right offsets. At 22050 Hz mono-sum feed:

```c
typedef struct { float rt60, predelay_ms, damp, wet; } ReverbParams;
/* comb delays (samples @22050): 1116,1188,1277,1356 (+23 R); allpass 556,441 */
/* feedback g_i = powf(10.f, -3.f * delay_i / (rt60 * rate)); damp = one-pole in loop */
```

**Room keying** (update once per game frame, cheap):
```c
float dx,dy,dz = aabb extents of g_BgRoomInfo[bondviewGetCurrentPlayersRoom()];
float vol = dx*dy*dz;
rt60      = clampf(0.25f + 0.16f * cbrtf(vol) / 300.f, 0.3f, 1.6f);  /* tune vs Dam/Facility */
wet_eff   = Audio.RoomReverb * (vol > OUTDOOR_VOL_CUTOFF ? 0.35f : 1.f); /* huge AABB ⇒ exterior */
```
Smooth parameter changes with a 250 ms one-pole ramp (no zipper on room transitions). Interaction with the faithful Rare FX: additive and subtle (wet default 0.25 max ≈ −12 dB); the faithful reverb remains the primary character. `Audio.RoomReverb=0` ⇒ function returns before touching the buffer (bit-exact identity).

### 4.6 Stereo width + the HRTF decision

- **Ship:** mid/side width on the final mix — `m=(l+r)/2; s=(l-r)/2; l'=m+w·s; r'=m−w·s` with `w=Audio.StereoWidth`, Q15, identity-bypass at 1.0. 15 lines, zero risk, audibly modernizes the narrow eqpower image.
- **Decide against (documented):** true per-source HRTF. The synth folds all voices into one stereo dry + mono wet DMEM bus before we ever see them (§2.2); per-voice binaural would mean N separate output buses through `alAudioFrame` — a rewrite of the faithful command-builder for a subtle payoff at 22050 Hz source material. Revisit only if the pack ecosystem matures (48 kHz sources) — see §7 R5 kill criterion.
- **Cheap middle option (included, opt-in):** a *pan-law exponent* on the eqpower table build (`audio_compat.c:1271-1280`) — `Audio.PanLaw` widening of per-voice placement. Default exactly reproduces the current table (verify byte-identical table at default).

### 4.7 Music pack loader — the texture-pack pattern applied to soundtrack (R2 §c)

**Contract:** `<pack>/music/trackNN.ogg` (also `.wav`), `NN` = zero-padded decimal game track index `1..63` (`NUM_MUSIC_TRACKS 63`, `src/music.h:13`) — the same integer passed to `musicTrackNPlay(track)`. Stereo, any sample rate (resampled at load/stream to device rate). Loop: whole-file seamless loop (matches GE's looping sequences); optional `trackNN.loop` sidecar containing a sample offset for intro+loop form.

**Decoder:** vendor `stb_vorbis.c` into `lib/stb/` (public domain — Tier A1, committable, consistent with the existing `lib/stb/stb_image.h`). WAV via a 60-line RIFF reader. No new build deps.

**New file `src/platform/music_pack.c`** (mirrors `texture_pack.c` discipline: validate-once loud warning, per-track miss cache, graceful fallback):

```c
bool musicPackEnabled(void);                       /* Audio.MusicPack[0] != 0 */
bool musicPackTryStart(int trackSlot /*1..3*/, int trackNum);  /* true = stream started */
void musicPackStop(int trackSlot);
void musicPackSetVolume(int trackSlot, float vol01);           /* engine volume incl. fades */
void musicPackMix(int16_t *out, int32_t frames, int outRate);  /* called from portAudioFrame */
```

Streaming: decode ~8192 frames ahead into a ring buffer on the main thread inside `musicPackMix` (audio here is synchronous per-frame already — no thread needed; a 370 KB/s decode is negligible next to the synth).

**Hook points** (all three track slots; track1 shown):
- `musicTrack1Play` (`music.c:1134`, NATIVE_PORT branch `:1158-1213`): after the track number is validated, `if (musicPackTryStart(1, track)) { /* skip decompress + alCSPSetSeq + alCSPPlay entirely — no synth voices spent */ portMusicTraceEvent("pack_play",...); return; }`. Fallback path unchanged. **Bookkeeping caution:** the pack path must still update the slot state exactly like the synth path (`g_musicXTrack1CurrentTrackNum`, fade-state reset) — `musicTrack1Play`'s same-track early-out (`:1150`) and the `alCSPGetState(...) != AL_STOPPED` guard (`:1166`) branch on it, and game code calls these functions from sim context (`bondview.c:20605`). Only the synth submission is skipped.
- `musicTrack1Stop` (`:1253`): `musicPackStop(1)` first (safe no-op when inactive).
- `musicTrack1ApplySeqpVol` (`:1300`): the single volume choke point — also call `musicPackSetVolume(1, appliedVol/32767.f)`; fades (`musicFadeTick` `:1921`) and game-driven ducking (`bondview.c:20605`) then work on packs for free.
- `musicTrack1FadeIn` (`:1362`) calls `alCSPPlay` directly — add the pack-resume equivalent.
- `musicFadeTick` fade-out completion calls `alCSPStop` directly when volume reaches 0 (`music.c:1939` track1, `:1963` track2, `:1987` track3) — add `musicPackStop(slot)` beside each (volume itself already flows through the ApplySeqpVol hook above).
- `portAudioFrame`: `musicPackMix(...)` in the §4.3 chain position.

**Tiers (R2):** loader + docs = **A1** (committed). Pack *content*: AI-enhanced/upsampled ROM music = **Tier B, local-only, never committed** (already enforced: `.gitignore:79-83` + `check_no_rom_data.sh:58`); user's original re-orchestrations = **A1 but out of scope to author** — we ship the mechanism, a `docs/AUDIO_PACKS.md` spec, and a `tools/audiopack/build_music_pack.py` skeleton that tags output dirs with a `TIER-B-LOCAL` marker file when sourced from dumps.

### 4.8 SFX pack loader

**Contract:** `<pack>/sfx/sfxNNNN.wav`, `NNNN` = zero-padded **public SFX id** (the `soundIndex` arg of `sndPlaySfx`, pre-translation — stable, human-meaningful, and what the `GE007_SFX_TRACE_JSONL` trace (`audio_pc.c:182`) logs as `public_sound`, `snd.c:3253`). Mono s16 (stereo downmixed), any rate.

**Routing — reuse the live PortVoice infrastructure** (§2.1): new `src/platform/sfx_pack.c` with the miss-cache/validate pattern. Hook in `sndPlaySfxInternal` (`snd.c:3170`) right after `sndPublicSfxIdToBankIndex`:

1. If `sfxPackTryLoad(publicId)` hits: allocate a PortVoice via `portAudioPlaySoundDetailedInternal` (`audio_pc.c:976`) with `PortSfxPlayParams` filled from the bank sound's envelope/pan/sampleVolume (same fields the stub path fills at `snd.c` PORT_SND_STUBS branch — reuse that param-building logic, lifted out of the `#ifdef`), **and** create the normal `ALSoundState` bookkeeping *without posting the synth play event* — game code still gets a valid state handle for deactivate/pan/pitch calls.
2. Forward live updates: in `sndCreatePostEvent` (`snd.c:3422`), if the state maps to a pack voice, translate `AL_SNDP_PAN_EVT` (`:1820`) / pitch / volume / decay / stop events to the PortVoice setters (`portAudioStopVoice` `audio_pc.c:1241`, `portAudioSetVoicePanLocked` `:553`, `portAudioSetVoiceMixLocked` `:589` already exist). Keep a `ALSoundState* → voiceIdx` map (fixed array, state count is bounded).
3. **Chained keymap sounds** (the `do/while` walk at `snd.c:3225-3290`): v1 replaces only single-sound entries; chained entries fall back to synth whole-chain (mixing replaced+synth segments of one logical sound would sound broken). Log a one-line notice per skipped chain id.

Master/SFX bus gain applies via the PortVoice `s_masterVolume` path (`audio_pc.c:793`) + new sfx-bus factor. Position hints: `sndSetPositionHint` values are captured into params.pan via the same `portAudioComputeSpatialMix` — parity with the synth path.

### 4.9 H2/H3/H4/H5 execution (from AUDIO_QUALITY_PLAN §4, unchanged in substance)

- **H2:** A/B `GE007_MIXER_ENV_SAMPLE_XOR=1` (then pole xor / `POLE_FC_XOR_MASK` per section) against the reference capture; the music dump enables near-sample comparison. Bake the winner into `MIXER_ENV_SAMPLE_XOR_DEFAULT` / `MIXER_POLE_SAMPLE_XOR_DEFAULT` (`mixer.c:39-40`); envs remain as hatches.
- **H3:** `GE007_MUSIC_MIDI_TRACE_JSONL` on Surface 2 → distinct program list → `GE007_MUSIC_SOLO_PROGRAMS=N` per program → offenders → audit that program's `bank_make_keymap/_envelope/_adpcm_loop/_adpcm_book` fields (`audio_compat.c:99-262`) against the `.ctl` layout; fix offsets/units. Selective, per-instrument; each fix validated by soloed-program dump vs reference.
- **H4:** add `dmaFallbackHits` counter in the bogus-pointer branch (`audi_port.c:197-201`), surface in the `GE007_AUDIO_TRACE` JSONL (append to the fprintf at `:520`); if it fires on Surface 2, bump `NUMBER_DMA_BUFFERS`/`AUDIO_DMA_MAX_BUFFER_LENGTH` (`:35-36`) — allocation is malloc'd (`:345`), so cost is trivial.
- **H5:** watch `sndp_active_voices`/`sfx_active_voices` vs the 24-voice cap; `Audio.EnhancedPolyphony=1` bumps `MUSIC_SYN_CONFIG_MAX_P_VOICES` 0x18→0x30 and seq `maxVoices` 0x10→0x20 (`music.c:52,55` made init-time-configurable). Default stays N64-exact.

---

## 5. Work breakdown

Estimates are **junior-engineer-days**. Rails column: R1 = gameplay-invariance note, R2 = asset tier, R3 = gating flag.
Common acceptance preamble (**ACC-ID**, used below): deterministic identity A/B —

```bash
cmake --build build -j && \
SDL_AUDIODRIVER=dummy GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 GE007_NO_VSYNC=1 \
GE007_AUDIO_DUMP=/tmp/base.raw build/ge007 --mission 1 --deterministic --screenshot-frame 900 --screenshot-exit && \
<same with feature branch build, flags OFF> → cmp /tmp/base.raw /tmp/off.raw   # byte-identical
python3 tools/compare_audio.py /tmp/base.raw /tmp/on.raw                        # feature-ON sanity (no static/silence)
```

> **CORRECTION (landed W6.E1.T2 2026-07-03) — `--level <int>` no longer boots a mission.**
> A raw-LEVELID guard (`main_pc.c` ~`:620-638`) now rejects `--level N` when `N` is a solo
> **mission number**, exiting 2 with *"`--level N` is a raw LEVELID… use `--mission N` or
> `--level <slug>`."* Throughout this doc, every `--level 1/2/8` means **`--mission 1/2/8`**
> = **dam / facility / surface2** (slugs also work: `--level dam` / `--level facility` /
> `--level surface2`). `tools/audio_ab_gate.sh`'s own `--level N` CLI arg must therefore invoke
> ge007 as `--mission N` (or the slug), never `--level N`. `--screenshot-exit` added so the
> process terminates after the dump.

### Epic W6.E1 — Ground truth + generalized fidelity (H1, H2) — 17 d

| ID | Task | Files | Steps / acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|
| **W6.E1.T1** | Phase-0 reference capture + baseline census | none (tools exist) | Prereqs: ROM at `./baserom.u.z64`, macOS GUI session, network + git/cmake/python3, ares not running. (1) `tools/prepare_ares_audio_dump_build.sh` — clones a pinned ares commit into `build/ares-audio-dump/`, patches in the `ARES_AUDIO_DUMP` PCM hook, builds `desktop-ui`; on success it **prints the instrumented binary path** (`build/ares-audio-dump/ares/build-audio-dump/desktop-ui/ares.app/Contents/MacOS/ares`). (2) Boot-theme ref: `tools/ares_startup_audio_reference.sh --ares-bin <that path> --out-dir ~/mgb64_refs` — a timed (~85 s) kiosk-mode GUI run, **not headless**; expect `PASS: captured N bytes -> ~/mgb64_refs/ares_boot_44100.raw` + a manifest JSON. Do NOT omit `--ares-bin`: the default points at stock ares and fails by design. (3) Surface 2 + Dam refs: manual session — `ARES_AUDIO_DUMP=~/mgb64_refs/surface2.raw ARES_AUDIO_DUMP_FRAMES=8000000 <instrumented ares> baserom.u.z64`, play into the level, quit (budget most of a day; raw is s16le stereo 44100). (4) Baseline compare: run the exact command step 2 prints (`tools/startup_music_reference_check.sh --no-build --reference ~/mgb64_refs/ares_boot_44100.raw --reference-format raw --reference-raw-rate 44100 --reference-raw-channels 2 --min-compared-seconds 60 --report-only --out-dir ~/mgb64_refs/mgb64`); band table prints, metrics land in `.../boot_audio_compare.json`; archive locally. **Accept:** three reference `.raw` captures exist locally; `boot_audio_compare.json` records current band deltas; a one-page findings note ranks which of H1–H5 the data supports. | 4 | — | R2: captures Tier B local (gitignored). No code. |
| **W6.E1.T2** | H1 settings-ization + alpha fit + promote/reject | `audi_port.c:42-44,131-169`; `audio_pc.c:859+` (registration); `platform_sdl.c:1985` (preset) | Add `Audio.OutputFilter`/`Audio.OutputFilterAlpha` (LIVE); de-latch the env read; alpha sweep per §4.2 against T1 refs; if best-fit beats OFF on high-band MAE, add `Audio.OutputFilter=1` + fitted alpha to `s_remasterPreset`, else document rejection in AUDIO_QUALITY_PLAN. **Accept:** ACC-ID passes; sweep table (alpha → `high_band_mae_db` from each run's `boot_audio_compare.json`) committed to AUDIO_QUALITY_PLAN; chosen setting's `high_band_mae_db` ≤ the OFF run's value. | 4 | E1.T1 | R3: `Audio.OutputFilter` (+ legacy `GE007_ENABLE_LIBAUDIO_LOWPASS`). R1: output-only. |
| **W6.E1.T3** | H2 env/pole xor resolution | `mixer.c:39-40` | A/B `GE007_MIXER_ENV_SAMPLE_XOR` / `GE007_MIXER_POLE_SAMPLE_XOR` / `GE007_MIXER_POLE_FC_XOR_MASK` on the T1 dump at sample level; bake winner into the two `*_DEFAULT` macros. **Accept:** ACC-ID (if defaults change, the *new* dump becomes golden after review — record before/after RMS + `env_mixer_clamp_delta` flat in `GE007_AUDIO_TRACE`); reference comparison not worse in any band. | 3 | E1.T1 | R3: envs are the hatch; default change is a deliberate promotion. |
| **W6.E1.T4** | Runtime confirmations + trace additions | `audi_port.c:519+`, `snd.c` counters | Verify `sndp_real_path>0, sndp_stub_path==0` (existing JSONL keys, `audi_port.c:545`) in a gameplay trace: `GE007_AUDIO_TRACE=/tmp/s2.jsonl build/ge007 --level 8 --deterministic --screenshot-frame 9000` (5 min at 30 Hz) then grep the last row; add `dmaFallbackHits` (H4 counter, `amDmaCallback` bogus-pointer branch `audi_port.c:197-201`) to the JSONL fprintf (`:520`). **Accept:** trace JSONL contains the new key; the Surface-2 run shows `sndp_stub_path==0` and `sndp_real_path>0`. | 2 | — | R3: trace-only, env-gated. |
| **W6.E1.T5** | Deterministic audio A/B gate script | new `tools/audio_ab_gate.sh` | CLI: `tools/audio_ab_gate.sh --level N --frames N --flag Key=Value [--baseline-binary PATH] [--music-pack DIR] [--sfx-pack DIR] [--sim-hash]`. Lanes: (1) OFF lane — dump from the baseline binary (default: `git stash`-free rebuild of merge-base into `build-baseline/`, or `--baseline-binary`) `cmp`'d against current build with all flags default; (2) ON lane — `--config-override <flag>`, checked with `compare_audio.py`; (3) clamp-delta guard from `GE007_AUDIO_TRACE` (§8.1 snippet); (4) `--sim-hash` — replicate `tools/sim_invariance_gate.sh`'s `run()` pattern (`sim_invariance_gate.sh:42-50`: isolated CWD + `--sim-state-hash-out hash.json`, a real CLI flag, `main_pc.c:603`) with the audio flag OFF vs ON and require identical hashes. This lane exists because **`sim_invariance_gate.sh` itself is hardcoded to `Video.*` toggles** (`:53-57`) and cannot exercise audio flags. **Accept:** seeded fault (leak `GE007_ENABLE_LIBAUDIO_LOWPASS=1` into the OFF lane — a flag that exists today; `Audio.StereoWidth` doesn't land until E3) fails the gate; clean tree passes. | 4 | — | The enforcement machine for every task below. |

### Epic W6.E2 — Selective fidelity + robustness (H3, H4, H5) — 15 d

| ID | Task | Files | Steps / acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|
| **W6.E2.T1** | Surface-2 instrument census | none | `GE007_MUSIC_MIDI_TRACE_JSONL=/tmp/s2midi.jsonl` on Surface 2 (`--level 8 --deterministic`); enumerate distinct `program` values from the JSONL; per program: `GE007_MUSIC_SOLO_PROGRAMS=<p> GE007_MUSIC_AUDIO_DUMP=/tmp/p<p>.raw` run + listen + `compare_audio_reference.py /tmp/p<p>.raw <surface2 ref> --json-out /tmp/p<p>.json`. **Accept:** ranked offender list, one row per program with its `band_mae_db`/`high_band_mae_db` and a listening note. | 3 | E1.T1 | Diagnostic only. |
| **W6.E2.T2** | Bank-parser fixes for offenders | `audio_compat.c:99-262` | Per offender: verify keymap/envelope/loop/book field offsets & units vs `.ctl`; fix behind `GE007_BANK_PARSER_V2` (new env, default 0 = old parser) until promoted. **Accept:** per-fix soloed A/B improves `band_mae_db`; full-mix ACC-ID with hatch at 0; non-regression = soloed `GE007_MUSIC_AUDIO_DUMP` dumps for ≥3 *non*-offender programs are `cmp`-identical old-parser vs new-parser (same discipline as the RAW16 fix, AUDIO_QUALITY_PLAN §RESOLVED). | 6 | E2.T1 | R3: parser-version env hatch during bake-in. |
| **W6.E2.T3** | H4 DMA window sizing | `audi_port.c:35-36,197-201` | With T4's counter: play Surface 2 dense fight; if hits>0, raise buffers until 0; measure RSS delta. **Accept:** `dmaFallbackHits==0` across `tools/campaign_route_smoke.sh` (export `GE007_AUDIO_TRACE=/tmp/route.jsonl` in the calling shell — env propagates through the script); ACC-ID (sizing must not change deterministic output — if it does, that *is* H4 evidence: document + promote deliberately). | 3 | E1.T4 | R3: constants change only after evidence. |
| **W6.E2.T4** | `Audio.EnhancedPolyphony` opt-in | `music.c:52,55,1063-1097`; registration | Make caps init-configurable; register key; A/B voice-steal counters. **Accept:** default build byte-identical (ACC-ID); flag-on shows `sndp_voice_stops` (steals) reduced on Surface 2; no clamp-delta spike. | 3 | E1.T5 | R3: `Audio.EnhancedPolyphony`, default 0 = N64-exact caps. |

### Epic W6.E3 — Output-chain remaster — 24 d

| ID | Task | Files | Steps / acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|
| **W6.E3.T1** | Final-mix restructure + master-volume fix + music/SFX buses | `audi_port.c:413-431`; `music.c:1300` (+track2 `:1568`, track3 `:1835`); `audio_compat.c:4931` (`alSynSetVol` scaler) + `:4867` (`clientPrivate` zeroing) per §4.3; `audio_pc.c:857+` | Implement §4.3 chain with identity bypasses; register 3 bus keys; attribute voices via `clientPrivate` (§4.3 — **not** `alSynStartVoiceParams`, which is music-only). **Accept:** ACC-ID at defaults; **music-bus lane** — `GE007_MUSIC_VOLUME=0 tools/startup_music_reference_check.sh --no-build --report-only --out-dir /tmp/musbus` then `python3 -c "import numpy as np; print(abs(np.fromfile('/tmp/musbus/mgb64_boot_music.raw',dtype=np.int16)).max())"` prints ≤ 1 (boot theme muted; the synth dump includes real SFX, but none fire during the no-input boot); **SFX-bus lane** — `GE007_MUSIC_VOLUME=0 GE007_AUTO_DAMAGE_TAG=19 ... --level 1 --deterministic` (deterministic glass-shatter SFX, `stubs.c:2796`): music-tap dump peak > 500 at default `Audio.SfxVolume`, and ≤ 1 with `GE007_SFX_VOLUME=0` added; **master lane** — `GE007_MASTER_VOLUME=0.5` halves final-dump RMS vs default (compare the two `RMS:` lines from `tools/compare_audio.py`); LIVE change audible without restart. | 6 | E1.T5 | R3: three `Audio.*` keys. R1: never writes `g_sndSfxVolumeScale` (sim-owned). |
| **W6.E3.T2** | `Audio.OutputRate` + windowed-sinc output resampler | new `audio_resample_out.c/h`; `audio_pc.c:917`; `stubs.c:450-521` | Implement §4.4; audit every `portAudioGetFrameSize()` consumer for rate scaling (definition `audi_port.c:625`; consumers `stubs.c:451` `nominal_frame_bytes` and the deterministic limit `:477`); force 22050 in deterministic mode; unit test the resampler ROM-free (sine sweep in → THD+N < −80 dB, alias rejection > 60 dB) as `ctest -R audio_resample`. **Accept:** unit test green in CI; ACC-ID (22050 default untouched); 48 kHz smoke: `GE007_AUDIO_DUMP` at 48 k has band-limited spectrum (no images above 11.025 k beyond −60 dB, checked with a 20-line numpy script committed as `tools/check_resample_spectrum.py`). | 8 | E3.T1 | R3: `Audio.OutputRate` RESTART, default 22050. R2: A1 (all first-party math). |
| **W6.E3.T3** | Room-keyed algorithmic reverb | new `audio_reverb.c/h`; `audi_port.c` chain; new const accessor `bgGetRoomAabb(room, min, max)` in `src/game/bg.c` (+ decl in `bg.h`), applying the `room_data_float2` scale per §4.5 | Implement §4.5; expose `Audio.RoomReverb`; parameter ramping; outdoor cutoff tuned on Dam-exterior vs Facility-interior captures. **Accept:** ACC-ID at 0; wet=0.25 Facility dump RT60 in 0.5–1.2 s and Dam exterior < 0.5 s, measured on the E3.T1 glass-shatter transient (`GE007_AUTO_DAMAGE_TAG=19`) by a new committed `tools/measure_rt60.py` (numpy Schroeder backward integral over the dump tail, prints RT60 seconds — ~40 lines); **sim-invariance**: `tools/audio_ab_gate.sh --sim-hash --flag Audio.RoomReverb=0.25` hash-identical (`sim_invariance_gate.sh` itself only toggles `Video.*` — see E1.T5); ASan clean. | 7 | E3.T1 | R1: presentation reads sim room id/AABB — allowed direction; gate proves it. R3: `Audio.RoomReverb` default 0. |
| **W6.E3.T4** | Stereo width + pan-law option | `audi_port.c` chain; `audio_compat.c:1271` | §4.6 mid/side + `Audio.PanLaw` exponent (default reproduces the exact current eqpower table — assert byte-equal table at default in a unit test). **Accept:** ACC-ID; width=2 dump has side/mid energy ratio ≈ 2× baseline (numpy check); pan-law default table byte-identical (ctest). | 3 | E3.T1 | R3: `Audio.StereoWidth`/`Audio.PanLaw` defaults identity. |

### Epic W6.E4 — Music pack loader — 20 d

| ID | Task | Files | Steps / acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|
| **W6.E4.T1** | Vendor stb_vorbis + WAV reader + stream core | `lib/stb/stb_vorbis.c`; new `music_pack.c/h` | Decode→ring-buffer→`musicPackMix` with load-time/stream resample to device rate (reuse E3.T2 resampler); miss cache + validate-once warning per `texture_pack.c:40-62` pattern. Unit test fixture: generate a 440 Hz WAV **in the test itself** (no committed audio blob — `check_no_rom_data.sh` forbids tracked `.wav`/`.ogg`); the ogg decode path is covered by the E4.T4 local-pack lane, not by CI. **Accept:** `ctest -R music_pack` green (WAV path); ASan clean on 10-min stream soak. | 7 | E3.T2 | R2: stb_vorbis is public domain = A1 committable (add to THIRD_PARTY.md). |
| **W6.E4.T2** | Engine hooks (3 track slots) | `music.c:1134,1253,1300,1340,1362,1921` (incl. fadeTick `alCSPStop` sites `:1939/:1963/:1987`, §4.7) + track2 (Play `:1395`, Stop `:1520`, ApplySeqpVol `:1568`, FadeIn `:1629`) + track3 (Play `:1662`, Stop `:1787`, ApplySeqpVol `:1835`, FadeIn `:1896`) | §4.7 hook set; when pack track active skip `alCSPSetSeq/alCSPPlay` (zero synth voices) **but keep slot bookkeeping identical** (§4.7 caution — `g_musicXTrackNCurrentTrackNum`, fade state); `portMusicTraceEvent("pack_play")` telemetry (`music.c:154`); add a `seq_active_voices` key to the `GE007_AUDIO_TRACE` JSONL (E4.T4 asserts on it). Test pack: `mkdir -p /tmp/testpack/music` + a python-generated 30 s 440 Hz `track02.wav` (WAV is a valid pack format per §4.7; no ffmpeg needed). **Accept:** ACC-ID with `Audio.MusicPack=` empty; with `--config-override Audio.MusicPack=/tmp/testpack`: boot plays `track02.wav`, level fade-out fades the stream (audible + gain trace), `musicTrackNStop` kills it; missing file → stock synth (log line, no crash). | 6 | E4.T1 | R3: `Audio.MusicPack` default "" = zero filesystem touch. R1: hooks are in presentation-side music control (no sim reads/writes added). |
| **W6.E4.T3** | Pack spec + builder skeleton + provenance guard | new `docs/AUDIO_PACKS.md`; `tools/audiopack/build_music_pack.py` | Document contract (§4.7 incl. loop sidecar, tiers table mirroring roadmap §1b); builder validates naming/rate/channels and writes `TIER-B-LOCAL` marker when `--from-dump` used. **Accept:** `tools/check_markdown_links.py` green; `scripts/ci/check_no_rom_data.sh` still hard-fails a deliberately staged `.ogg` (test the guard, then unstage). | 3 | E4.T2 | R2: this task *is* the R2 story — loader A1, content tiered. |
| **W6.E4.T4** | Music pack validation lane | `tools/audio_ab_gate.sh` extension | Add `--music-pack <dir>` lane: deterministic run with pack vs without; assert `sndp_active_voices` unaffected and `seq_active_voices == 0` while the pack track plays (both in `GE007_AUDIO_TRACE`; the latter added by E4.T2); 30-min soak no underruns (`underruns` key flat); `--sim-hash` lane with the pack configured vs empty — hash identical (the music.c hooks sit in sim-called code — R1's RAMROM gate applies; `sim_invariance_gate.sh` can't toggle audio keys, see E1.T5). **Accept:** gate green with the local test pack; identity lane still `cmp`-exact; sim hash green. | 4 | E4.T2 | R3 validation + R1 RAMROM gate. |

### Epic W6.E5 — SFX pack loader — 16 d

| ID | Task | Files | Steps / acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|
| **W6.E5.T1** | Loader + submission routing | new `sfx_pack.c/h`; `snd.c:3170-3230`; `audio_pc.c:976+` | §4.8 step 1: public-id keyed WAV load (device-rate resample at load), PortVoice submission with bank-derived params; chained keymap ids fall back whole-chain. To find the PP7 fire id: fire once in a windowed run with `GE007_SFX_TRACE_JSONL=/tmp/sfx.jsonl` and read `public_sound` from the `sndp_submit` row (`snd.c:3253`); name the file `sfx<id 0-padded to 4>.wav`. **Accept:** ACC-ID with `Audio.SfxPack=` empty; test pack replacing the PP7 fire id: shot plays replacement (dump diff localizes to trigger frames), pan from position hint matches synth-path pan within 1 ALPan step (compare `state_position` rows in the SFX trace). | 7 | E3.T1 | R3: `Audio.SfxPack` default "". R2: loader A1; content tiered like music. |
| **W6.E5.T2** | Live event forwarding | `snd.c:3422` (`sndCreatePostEvent`), `:1820` region | State→voice map; forward pan/pitch/vol/decay/stop to PortVoice setters; dispose path (`sndDeactivate`) releases the voice. **Accept:** moving-source test (guard walking past) shows pan updates in the `GE007_SFX_TRACE_JSONL` trace for the pack voice; no leaked voices after 10-min soak (`sfx_active_voices` returns to 0). | 6 | E5.T1 | R1: forwards existing presentation events only. |
| **W6.E5.T3** | SFX pack validation lane + docs | `tools/audio_ab_gate.sh`; `docs/AUDIO_PACKS.md` | `--sfx-pack` lane mirroring E4.T4 (incl. the `--sim-hash` lane pack-ON vs OFF — the snd.c hook sits in sim-called code); document SFX contract + chained-id limitation. **Accept:** gate green; sim hash green; docs list the exact fallback rules. | 3 | E5.T1 | R3 validation + R1 RAMROM gate. |

### Epic W6.E6 — Menu/settings integration handoff (W5 tie) — 4 d

| ID | Task | Files | Steps / acceptance | Est | Deps | Rails |
|---|---|---|---|---|---|---|
| **W6.E6.T1** | Registry hygiene + W5 handoff | registration sites above | Ensure every §4.1 key has help text + sane min/max + correct LIVE/RESTART scope (the W5 menu renders from the registry); write the one-page key table into `docs/AUDIO_PACKS.md` appendix; verify `--config-override Audio.X=Y` works for each. **Accept:** `build/ge007 --list-settings` and `--dump-config` (parsed `main_pc.c:543-548`, printed via `settingsPrintList` `settings.c:467` / `settingsPrintDump` `:504`) list all §4.1 keys with help text; W5 owner sign-off that scopes render correctly. | 4 | E3.T1, E4.T2, E5.T1 | R3 is the whole point. |

**Total: 96 junior-days** (E1 17 + E2 15 + E3 24 + E4 20 + E5 16 + E6 4).

---

## 6. Milestones & deliverables

| # | Milestone (deliverable) | Contents | Est (junior-weeks) | Demo script (reviewer runs) |
|---|---|---|---|---|
| **M1** | **Measured fidelity baseline + H1/H2 landed** | E1 complete (17 d) | 3.5 | `tools/startup_music_reference_check.sh --reference ~/mgb64_refs/ares_boot_44100.raw --reference-format raw --reference-raw-rate 44100` → PASS within budgets (band table printed by default) |
| **M2** | **Working audio settings (buses + master fix)** | E3.T1 + first pass of E6.T1 covering the keys that exist at M2 (bus/output keys; E6.T1 fully closes after E4/E5 land the pack keys) (10 d) | 2 | `build/ge007 --level 2 --config-override Audio.MusicVolume=0` → SFX only; live slider change audible |
| **M3** | **Remaster output chain** | E3.T2–T4 (18 d); `--remaster` preset gains the §4.1 audio rows | 3.5 | `build/ge007 --remaster --level 2` (mission 2 = Facility in the `kPcStartStages` table, `main_pc.c:71+`; slug form `--level facility` also valid): 48 kHz device, room tail audible in halls; `tools/audio_ab_gate.sh --flag Audio.RoomReverb=0.25` green |
| **M4** | **Music packs live** | E4 complete (20 d) | 4 | `build/ge007 --level 1 --config-override Audio.MusicPack=~/mypack` → user track on boot; delete file → stock fallback |
| **M5** | **SFX packs + Surface-2 instrument fixes + robustness** | E5, E2 complete (31 d) | 6 | `--config-override Audio.SfxPack=~/mypack` PP7 demo; Surface 2 (`--level 8`) before/after soloed-program captures; `tools/audio_ab_gate.sh` full suite green |

Each milestone independently landable; M2 can ship before M1 finishes if reference capture stalls (they share no code).

---

## 7. Risks & mitigations (ranked)

| # | Risk | Likelihood/Impact | Mitigation | Kill / de-scope criterion |
|---|---|---|---|---|
| **R1** | **H1 fit concludes "filter off is correct"** (code comment `audi_port.c:42-43` already hints this) — headline fidelity win evaporates | Med / Med | The sweep is cheap (T2, 1 day of it); the settings plumbing ships regardless as a user knob | If OFF wins on high-band MAE, mark H1 resolved-negative, remove from `--remaster`, done — no sunk cost |
| **R2** | **SFX pack event-forwarding whack-a-mole** — GE fires odd event sequences (chained keymaps, delayed play events `snd.c:3268-3320`) | High / Med | v1 scope fence: single-sound ids only, whole-chain fallback, forwarding limited to the 5 event types with counters (`snd.c:1820+`) | If >20% of commonly-fired ids are chained (measure via a `GE007_SFX_TRACE_JSONL` gameplay census in E5.T1 — `entry_submit`/chain rows expose it), de-scope E5.T2 to pan+stop only and document; if the state-map ever desyncs in soak, ship E5 as "one-shot ambient/UI sounds only" |
| **R3** | **Output-rate change breaks queue pacing** (underruns/latency: all `stubs.c` limits are byte-denominated at 22050 assumptions) | Med / Med | E3.T2 audits every `portAudioGetFrameSize` consumer; 30-min soak watching `underruns`/`dropped_buffers` in the trace | If 48 kHz can't hold `underruns==0` on the perf-census levels, ship 44100 (×2 integer, simplest) only |
| **R4** | **Reverb reads sim state → someone later reverses the arrow** (a sim system reading reverb state would breach R1) | Low / High | Accessor is one-way (`bgGetRoomAabb` const); `check_sim_render_separation.sh` denylist gains `_audio_reverb_`; sim-invariance hash in E3.T3 acceptance | Non-negotiable rail: any gate red = feature blocked until fixed |
| **R5** | **HRTF/per-voice spatial ambition creep** | Med / Low | Explicitly decided-against in §4.6 with the DMEM-bus rationale; mid/side + pan-law are the shipped substitutes | Revisit only after M4 ships AND a 48 kHz SFX pack ecosystem exists; otherwise permanently out (roadmap §8 spirit) |
| **R6** | **H3 parser fixes regress good instruments** (shared parse code) | Med / Med | Per-program soloed dumps before/after; ADPCM-region byte-compare like the RAW16 fix validation (plan §RESOLVED); `GE007_BANK_PARSER_V2` hatch | If a fix can't be isolated to the offender's fields, revert that fix — selective defects don't justify global parser risk |
| **R7** | **Determinism split-brain**: new DSP stages accidentally active in deterministic mode → golden dumps churn | Low / High | Every stage has identity-bypass at default; `tools/audio_ab_gate.sh` `cmp` lane runs in every task's acceptance | Any unexplained golden churn = stop-the-line |

---

## 8. Validation strategy (exact commands)

### 8.1 Per-commit (every W6 task)

```bash
# Build + byte-identity at defaults (the R3 gate) + feature sanity:
tools/audio_ab_gate.sh --level 2 --frames 900 --flag <Key=Value>       # E1.T5 deliverable
# Under the hood it runs the roadmap §7 pattern:
SDL_AUDIODRIVER=dummy GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 GE007_NO_VSYNC=1 \
GE007_AUDIO_DUMP=/tmp/off.raw GE007_AUDIO_TRACE=/tmp/off.jsonl \
build/ge007 --mission 2 --deterministic --screenshot-frame 900 --screenshot-exit   # (--level 2 no longer boots — see §5 CORRECTION)
cmp /tmp/baseline.raw /tmp/off.raw                                      # flags-off: byte-identical
python3 tools/compare_audio.py /tmp/baseline.raw /tmp/on.raw            # flags-on: no static/silence/blowup
python3 - <<'EOF'                                                       # clamp-delta guard (regression = new clipping)
import json,sys
rows=[json.loads(l) for l in open('/tmp/on.jsonl')]
assert sum(r['env_mixer_clamp_delta']+r['mix_clamp_delta']+r['pole_filter_clamp_delta'] for r in rows[30:]) \
     <= sum(r['env_mixer_clamp_delta']+r['mix_clamp_delta']+r['pole_filter_clamp_delta'] for r in
            [json.loads(l) for l in open('/tmp/off.jsonl')][30:]) * 1.05
EOF
```

### 8.2 Fidelity lane (M1, then re-run at each milestone)

```bash
tools/prepare_ares_audio_dump_build.sh                                # once: clones+patches+builds instrumented ares
tools/ares_startup_audio_reference.sh \
  --ares-bin build/ares-audio-dump/ares/build-audio-dump/desktop-ui/ares.app/Contents/MacOS/ares \
  --out-dir ~/mgb64_refs                                              # once, local Tier B; --ares-bin REQUIRED (default = stock ares, fails)
tools/startup_music_reference_check.sh --reference ~/mgb64_refs/ares_boot_44100.raw \
  --reference-format raw --reference-raw-rate 44100 --frames 2400
# Thresholds are the compare_audio_reference.py defaults (spectral cosine 0.90,
# band MAE 8 dB, high-band MAE 10 dB — `:824-826`); the wrapper always passes
# --print-bands + --json-out <out-dir>/boot_audio_compare.json. For custom
# thresholds invoke tools/compare_audio_reference.py directly on the dumped raw
# (the wrapper rejects unknown args, incl. all --max-*/--min-* threshold flags).
```

### 8.3 Rails lanes

```bash
tools/audio_ab_gate.sh --sim-hash --flag <Key=Value>   # R1: audio-flag ON-vs-OFF sim hash identical (E3.T3, E4/E5 acceptance).
                                                   # NOTE: tools/sim_invariance_gate.sh exists but is HARDCODED to Video.* toggles
                                                   # (sim_invariance_gate.sh:53-57; positional args are replay/frame/render_scale
                                                   # only) — audio flags go through the audio_ab_gate lane, which replicates its
                                                   # run() pattern with --sim-state-hash-out (main_pc.c:599).
scripts/ci/check_sim_render_separation.sh          # R1 static: add _audio_reverb_/_music_pack_/_sfx_pack_ to its DENY regex (:32) —
                                                   # it scans src/game/*.c objects only; music.c/snd.c live in src/, outside the
                                                   # scanned set, so the pack hooks there are legal (and gated dynamically above)
scripts/ci/check_no_rom_data.sh                    # R2: no tracked audio blobs (already covers wav/ogg/etc.)
ctest --test-dir build -R 'audio_resample|music_pack|pan_law'   # ROM-free unit lanes (new, CI-safe)
tools/asan_smoke.sh                                # ASan/UBSan on the new DSP + streaming code (soak: 10 min Surface 2)
tools/playability_smoke.sh --all                   # broad gate before merge
```

### 8.4 Promotion protocol (for any default change: H2 xor bake, H3 parser, H1 if it wins)

1. Land behind flag, default old behavior. 2. Fidelity lane shows measured improvement. 3. Flip default in a dedicated commit whose message records the before/after metrics and re-baselines `/tmp/baseline.raw` goldens. 4. Keep the env hatch one full milestone before considering removal. (Same discipline as the shipped RAW16 fix, AUDIO_QUALITY_PLAN §RESOLVED.)

---

## 9. Open questions (genuinely undecidable without the user)

1. **Q1 — Master-volume default semantics:** `Audio.MasterVolume` defaults to 0.7 (`audio_pc.c:859`) but has never applied to the main mix; making it apply would drop shipped loudness by ~3 dB for everyone. Options: (a) default→1.0 and let users lower it (preserves today's bytes — my recommendation, and what E3.T1 assumes), (b) keep 0.7 and accept the one-time loudness change as a deliberate promotion. Needs a call.
2. **Q2 — `--remaster` reverb/width taste values:** 0.25 wet / 1.2 width are engineering placeholders; the final numbers need the user's ears on M3 builds (Facility vs Dam vs Surface). The mechanism, ranges, and outdoor cutoff are decided; only the two scalars are open.
3. **Q3 — Music pack crossfade behavior:** when the game hard-switches tracks (`musicTrackNPlay` while another plays), stock behavior is stop-then-start. Should packs add a short (≤500 ms) crossfade (nicer, slightly unfaithful pacing of *presentation*, sim-neutral) or mirror the hard cut? Default in E4.T2 is hard cut (faithful); crossfade would be one more opt-in key.
4. **Q4 — Public SFX id stability for packs:** `sfxNNNN` uses the public id (pre-`sndPublicSfxIdToBankIndex`). If a future decomp rename renumbers public ids, packs break. Acceptable (document "ids are engine-version-bound"), or should the pack carry a `manifest.json` mapping names→ids that we also emit from a dump tool? E5.T3 documents version-bound as the default; upgrading to a manifest is +2 days if the user wants pack portability.
5. **Q5 — Pack-mix vs reverb/width ordering at 48 kHz:** the §4.3 chain diagram mixes pack streams *before* reverb/width (at 22050), but §4.4 mixes pack audio *after* the resampler — under `Audio.OutputRate=48000` those conflict: the §4.4 order means pack audio bypasses reverb/width (plays dry). Options: (a) move reverb/width post-resample to device rate so pack audio participates (retune comb/allpass delays in device-rate samples; keeps one effects chain — recommended default), or (b) accept pack audio dry (packs are usually pre-produced with their own ambience). E3.T3/E4 implementers: build (a) unless this question is answered otherwise; note the retuned constants beside the Q2 placeholders.
