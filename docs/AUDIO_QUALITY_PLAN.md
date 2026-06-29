# Audio Quality Plan — "Crappy MIDI" Timbre Investigation & Solution Trace

---

## ✅ RESOLVED (2026-06-29): the "deep bass → buzz" is a RAW16 big-endian bug

**Root cause:** RAW16 (uncompressed 16-bit PCM) instrument samples are stored
**big-endian** in the N64 ROM. The port serves the ROM raw big-endian
(`rom_io.c`) and the mixer reads sample data from DMEM as **host-native
little-endian `int16_t`** (`mixerResample`/`BUF_S16`). The RAW16 sample load
(`alRaw16Pull` → `aLoadBuffer` → `mixerLoadBuffer`) never byte-swapped, so every
RAW16 sample was byte-swapped garbage: a smooth low waveform became huge
sample-to-sample jumps = a high-frequency **buzz** with a faint residual low
envelope. ADPCM is unaffected (decoded byte-by-byte; codebook explicitly
big-endian-decoded), which is why ~all instruments sounded fine.

**Proof (empirical):** captured the boot/intro music dump (`Mintro_eye`, track 2).
The deep-bass instrument is **program 2** — the *only* RAW16 instrument
(`wave_type=1`); all 397 other bank wavetables are ADPCM. Extracting its sample
bytes from the ROM at the runtime DMA offset:
- big-endian (N64 correct): dominant **62 Hz**, hi/bass **0.00** → clean bass.
- little-endian (port read): dominant **2563 Hz**, hi/bass **19.4** → buzz.
The in-game soloed measurement matched exactly (hi/bass ~20, dom ~2.5 kHz).

**Fix:** `mixerLoadBufferSwap16()` (byte-swaps 16-bit words BE→host), used only by
`alRaw16Pull`'s three sample loads. ADPCM/FX loads keep the raw `aLoadBuffer`.
Default-on correctness fix; `GE007_DISABLE_RAW16_BYTESWAP=1` is the A/B hatch.
Files: `src/platform/mixer.c`, `src/platform/mixer.h`, `src/platform/audio_compat.c`.

**Validated:** solo bass 19× buzz → 0.00 clean (62–82 Hz); full intro mix
high-freq energy ~halved; **ADPCM-only region byte-identical** fix-on vs fix-off
(0.0000% diff → zero ADPCM regression).

**Scope note:** RAW16 is rare in the bank (only the deep-bass instrument), but it
is shared across tracks, so any music leaning on that bass (incl. Surface levels)
benefits. SFX are all ADPCM → unaffected by this bug; any residual SFX/"brightness"
complaint is a separate matter (see H1 output-coloration below). The earlier
broad-survey conclusion that "the synth is faithful" was correct for ADPCM but
**missed this RAW16 path** — a reminder to measure, not just read.

---


**Status:** investigation complete (static); solution trace proposed, empirical
confirmation pending.
**Symptom (user report):** in-game music *and* many SFX sound like "crappy MIDI
instruments" — thin / buzzy / dry / synthetic — instead of the warm N64 sampled
sound. **Many** sounds are fine, **many** are bad. **Surface 2** is the worst
offender.

---

## 0. TL;DR

The audio **synthesis chain is a faithful, high-quality reimplementation of the
N64 libaudio + RSP audio microcode**. The ADPCM decoder, the resampler (the
*authentic* 64-phase × 4-tap polyphase FIR, with the canonical N64 coefficient
table), the envelope mixer, the reverb, the MIDI sequence player, voice
allocation, and pitch/tuning are all correct. Music and SFX both run through this
path; the "simple synth" in `audio_pc.c` is **dead code**.

So "crappy MIDI" is **not** a broken decoder/resampler. It is most likely a
combination of:

1. **Missing N64 output coloration** (the console's analog/DAC low-pass that
   warms the sound) — a *generalized* defect: the port emits the raw, bright
   digital synth. **A knob already exists and is default-OFF.** ← biggest win.
2. **Contested RSP env-mixer / pole-filter sample ordering** (`env_sample_xor`,
   default 0) — a *generalized* subtle-distortion suspect with an existing flag.
3. **Per-instrument bank defects** (wrong sample / loop points / tuning for some
   programs) — *selective*; best explains "some good, many bad, Surface 2 worst."
4. **Robustness-under-load** issues (sample-DMA window exhaustion → garbage;
   voice starvation) — *intermittent*, plausibly worst on dense/ambient Surface 2.
5. Partly **authentic**: GE's score uses a compact ADPCM bank and is somewhat
   synthetic by design. The "better-than-N64" goal needs a remaster sample pack.

The repo **already has a music-fidelity reference-comparison harness**
(`tools/compare_audio_reference.py`, `tools/startup_music_reference_check.sh`,
`GE007_MUSIC_AUDIO_DUMP`, per-stage clamp counters, per-instrument solo/mute, and
per-note MIDI tracing). The plan centers on using it to convert "sounds crappy"
into measured deltas, then fixing in flag-gated, A/B-validated phases.

---

## 1. Architecture as built (verified, file:line)

### Engines
- **Engine A — faithful libaudio reimplementation.** `src/platform/audio_compat.c`
  (~5150 L): sequence players (`alCSPNew`/`alCSPPlay`/`alCSPSetSeq`), bank parse
  (`alBnkfNew`, `bank_make_adpcm_book/_envelope/_keymap/_adpcm_loop`), voice
  alloc/steal (`alSynAllocVoice` `audio_compat.c:4867`), envelope/pan/vol/FX param
  building, and the command-list builder `alAudioFrame` (`:2245`).
- **Engine B — simple synth.** `src/platform/audio_pc.c` `musicNoteOn` /
  `calcNotePitch` / `findInstSound` / `musicMixSamples` / `portMusicPlaySequence`.
  **DEAD CODE** — never referenced outside `audio_pc.c` (legacy "fallback
  diagnostics", `audio_pc.c:2193`). Not a factor in shipped audio.
- **DSP core — RSP audio-microcode reimplementation.** `src/platform/mixer.c`
  (~793 L) *executes* the Acmd list `alAudioFrame` builds. This is where the
  actual sample math lives.
- **Audio manager (PC).** `src/platform/audi_port.c` (`NATIVE_PORT`). The N64
  `audi.c` body is compiled out (`#ifdef NATIVE_PORT` wins, `audi.c:13`).

### Routing (both music and SFX use Engine A)
- **Music → Engine A:** `src/music.c:1106/1208/1210` (`alCSPNew`/`alCSPSetSeq`/
  `alCSPPlay`), three track players + an SFX sequence player (`sndNewPlayerInit`,
  `music.c:1118`).
- **SFX → Engine A (real path):** `PORT_SOUNDPLAYER_REAL` is **ON by default**
  (`CMakeLists.txt:365`), so `PORT_SND_STUBS` is **undefined** (`snd.h:12-14`),
  so `snd.c` compiles the real `ALSndPlayer` using `alSynStartVoice`
  (`snd.c:1661`) — **not** the simpler `portAudioPlaySfx`/PortVoice path
  (`snd.c:919`, the `PORT_SND_STUBS` branch). Confirm at runtime via the
  `sndp_real_path` vs `sndp_stub_path` trace counters.

### What is confirmed faithful / correct
- **Output rate** = 22050 Hz (`audi_port.c:304`), matching the rate the GE bank
  was authored against → note-pitch ratio needs no extra rate term (matches N64).
- **Reverb is wired & ON by default.** `audi_port.c` passes the real 6-section FX
  table `s_portAudioCustomFxParams` (`audi_port.c:46-54`) to `alInit`
  (`:326-331`); mirrors N64 `CUSTOM_FX_PARAMS_N` (`audi.c:169-182`). Kill switch:
  `GE007_DISABLE_NATIVE_REVERB`. `native_fx_params_for_config` (`audio_compat.c:1230`)
  returns the real table because `params != NULL`.
- **ADPCM decode** (`mixer.c:351 mixerADPCMdec`): correct order-2 VADPCM
  (scale = high nibble, predictor index = low nibble, `tbl[0]*prev2 +
  tbl[1]*prev1 + ins<<11`, inner convolution, `>>11`, clamp). ✓
- **Resampler** (`mixer.c:411 mixerResample`): authentic **64-phase × 4-tap
  polyphase FIR**; `resample_table` (`mixer.c:188-221`) is the **canonical N64 RSP
  coefficient table**. Not linear / not zero-order-hold. ✓
- **Envelope mixer** (`mixer.c:509`), **mix** (`:614`), **pole/low-pass**
  (`:653`): standard, dry/wet split correct. ✓
- **CSP MIDI handler** (`audio_compat.c:3463`): faithful — pitch =
  `alCents2Ratio((key-keyBase)*100 + detune)` × pitchBend × vibrato
  (`:3535-3547`), envelope attack/decay/release, sustain pedal, pan, volume,
  program change, pitch bend. ✓
- **Program solo/mute filtering** (`audio_compat.c:3290`): **DEFAULT OFF** (only
  active with `GE007_MUSIC_SOLO_PROGRAMS` / `GE007_MUSIC_MUTE_PROGRAMS`).
- **No recent audio churn** on `feat/dam-hd-remaster`; audio is at its
  as-released state (last real work: commit `974cec9`, parity checks).

---

## 2. Why "some good, many bad," and why Surface 2 is worst

A faithful synth that nonetheless sounds cheap points away from a global math bug
and toward **content-dependent exposure** of a few real defects:

- **Sustained vs one-shot.** One-shot/percussive SFX and short stabs decode once
  and stop — they sound fine. **Sustained, looped instruments** (pads, drones,
  sustained leads) re-trigger the loop and run long volume envelopes, so they
  expose loop-point errors (H3) and env-ramp ordering (H2). Surface 2's theme is
  **sparse, ambient, winter-exterior** — pad/drone heavy — and the level layers
  **continuous ambient SFX**, so it maximally exposes H2/H3 and pushes polyphony
  (H4/H5).
- **Sparse arrangement = nowhere to hide.** In a dense action cue, one cheap
  instrument is masked. In Surface 2's thin mix, every instrument is exposed, so
  any per-instrument defect is most audible there.
- **Missing output warmth (H1) is uniform** but most noticeable on sustained,
  exposed tones — again Surface 2.

---

## 3. Ranked root-cause hypotheses (each with a verification method)

| # | Hypothesis | Class | Evidence / anchor | How to confirm |
|---|---|---|---|---|
| **H1** | **No N64 output-coloration low-pass** → raw bright digital synth sounds "fizzy/cheap" | Generalized | `portAudioApplyLibaudioLowPass` exists but **default OFF** (`audi_port.c:131-169`, alpha Q15 = 26840); `GE007_ENABLE_LIBAUDIO_LOWPASS` | Spectral diff of `GE007_MUSIC_AUDIO_DUMP` vs N64 reference (`compare_audio_reference.py`); A/B the flag |
| **H2** | **RSP env-mixer / pole-filter sample-ordering** wrong → zipper/buzz on envelopes | Generalized | `i ^ env_sample_xor` (`mixer.c:558`); defaults 0 (`mixer.c:39-40`); flags `GE007_MIXER_ENV_SAMPLE_XOR`, `GE007_MIXER_POLE_SAMPLE_XOR` exist (⇒ contested) | A/B each flag against the reference at sample level; watch `env_mixer_clamp_*` counters |
| **H3** | **Per-instrument bank defect** (wrong sample/loop/tuning for some programs) | Selective (explains pattern best) | bank parsers `audio_compat.c:99-262`; loop fields `bank_make_adpcm_loop:139`; MIDI trace logs program/sound_index/keyBase/detune/loop_start/end/count/wave_* per note | `GE007_MUSIC_MIDI_TRACE_JSONL` to list Surface-2 programs; `GE007_MUSIC_SOLO_PROGRAMS=N` to isolate offenders |
| **H4** | **Sample-DMA window exhaustion → bogus pointer** = garbage samples under dense polyphony | Intermittent | `amDmaCallback` returns bogus ptr on free-list exhaustion (`audi_port.c:198-202`); window only 1024 B (`:36`) | Add counter to the bogus-ptr branch; watch under Surface-2 load; enlarge `AUDIO_DMA_MAX_BUFFER_LENGTH`/`NUMBER_DMA_BUFFERS` |
| **H5** | **Voice starvation** → stolen/dropped notes = thin "MIDI" | Selective | 24 phys (`MUSIC_SYN_CONFIG_MAX_P_VOICES=0x18`), 16 seq voices; steal at `alSynAllocVoice:4890` | `sndp_active_voices`/`sfx_active_voices` counters peg the cap?; A/B raise caps |
| **H6** | GE score is **partly authentically synthetic** (compact ADPCM bank) | Perception | — | Remaster sample pack (Phase 5) for "better than N64" |

> Note: `AL_DEFAULT_FXMIX = 0` (`libaudio.h:56`) — per-channel reverb send starts
> dry until the sequence sends FX1 CC; this is faithful, but worth confirming the
> FX1 controller is honored for Surface-2 channels (it is handled at
> `audio_compat.c:3707`).

---

## 4. Solution trace (ordered, dependency-aware, flag-gated)

### Phase 0 — Capture ground truth (no code changes) — **do this first**
Convert "sounds crappy" into measured deltas.
- Obtain a known-good reference capture of the same cues (Ares/hardware/movie),
  per `docs/INSTRUMENTATION.md §Reference audio comparison`.
- For **Surface 2** and a **control** ("good") level, capture:
  `GE007_MUSIC_AUDIO_DUMP` (music-only WAV), `GE007_AUDIO_TRACE` (per-stage clamp
  counters), `GE007_MUSIC_MIDI_TRACE_JSONL` (per-note instrument/sample/loop).
- Run `tools/compare_audio_reference.py` to quantify RMS / spectral / brightness
  deltas vs the reference. **This decides which of H1–H5 are actually real.**

### Phase 1 — H1 output coloration (highest-leverage generalized win)
- Enable `portAudioApplyLibaudioLowPass`; **tune the cutoff (alpha Q15) to match
  the reference's high-frequency rolloff** (don't keep the guessed 26840 — fit it
  to the capture).
- Validate via spectral diff. If it matches the reference and improves perceived
  warmth without dulling, **promote to default-ON** (keep
  `GE007_DISABLE_LIBAUDIO_LOWPASS` as the escape hatch).
- Risk: over-filtering muffles SFX transients. Mitigate by fitting to capture, not
  by ear alone; A/B retained.

### Phase 2 — H2 envmixer / pole sample ordering
- A/B `GE007_MIXER_ENV_SAMPLE_XOR` (then pole equivalents) against the Phase-0
  reference at the sample level (the music dump enables near-bit comparison).
- Lock in whichever matches the reference; bake into
  `MIXER_ENV_SAMPLE_XOR_DEFAULT` / `MIXER_POLE_SAMPLE_XOR_DEFAULT` (`mixer.c:39-40`).
- Risk: low — pure A/B against ground truth.

### Phase 3 — H3 per-instrument / loop correctness (most likely *selective* fix)
- From the Surface-2 MIDI trace, enumerate distinct programs; `GE007_MUSIC_SOLO_PROGRAMS=N`
  each and listen/dump to find the offenders.
- For each offender, verify the bank parser against the `.ctl` layout:
  `bank_make_keymap` (keyBase/detune/keyMin/keyMax/vel), `bank_make_envelope`
  (attack/decay/release time & volume **units**), `bank_make_adpcm_loop`
  (start/end/count + 16-word state), `bank_make_adpcm_book` (order/npredictors).
  A wrong offset/unit corrupts tuning/loop for *that* instrument only — exactly
  the "some good, many bad" signature.
- Fix offsets/units in `audio_compat.c:99-262`. Gate behind a parser-version flag
  if risk warrants; validate the fixed program's dump vs reference.

### Phase 4 — H4/H5 robustness under load
- Instrument the DMA bogus-pointer branch (`audi_port.c:198-202`) with a counter
  surfaced in the trace. If it ever fires in gameplay, raise
  `AUDIO_DMA_MAX_BUFFER_LENGTH` / `NUMBER_DMA_BUFFERS`.
- If `*_active_voices` counters peg the voice cap on Surface 2, A/B raise
  `MUSIC_SYN_CONFIG_MAX_P_VOICES` / `MUSIC_SEQ_CONFIG_MAX_VOICES`. (Faithfulness
  note: raising caps deviates from N64 polyphony; keep default at N64 values, make
  the bump an opt-in "enhanced" flag.)

### Phase 5 — Optional remaster ("better than N64"), opt-in default-OFF
- HD audio pack mirroring the existing HD-texture-loader discipline: replace the
  ADPCM bank with higher-fidelity samples — either a higher-rate ADPCM/RAW16 bank,
  or route sequences through a soundfont/higher-quality sample set. Strictly
  opt-in, default-off, byte-identical when off.

---

## 5. Validation & instrumentation discipline

- **Default-off / identity:** every fix behind a flag; the default path stays
  behavior-identical to current until a fix is *proven* against the reference and
  deliberately promoted. (Audio isn't gameplay-deterministic, but keep the same
  A/B discipline used elsewhere in this port.)
- **Tools (already present):** `GE007_MUSIC_AUDIO_DUMP` (before/after WAV),
  `tools/compare_audio_reference.py` (vs N64 capture), `tools/startup_music_reference_check.sh`
  (capture workflow), `GE007_AUDIO_TRACE` (per-stage clamp/rail counters),
  `GE007_MUSIC_MIDI_TRACE_JSONL` (per-note metadata), `GE007_MUSIC_SOLO/MUTE_PROGRAMS`
  (per-instrument A/B), `compare_audio.py` (synth-chain breakage gate).
- **Regression guard:** per-stage clamp/rail-hit counters
  (adpcm/resample/envmix/mix/pole) must not spike — a spike = new clipping
  distortion. Music-only dump RMS/spectrum must not regress on the control level.
- **Test matrix:** Surface 2 (worst), Dam (showcase), one known-good control level.

---

## 6. Risks & unknowns to confirm in-game

- **Timbre cannot be judged statically** — Phase 0 capture is mandatory to confirm
  which hypotheses are real before any code changes.
- **H1 cutoff** must be *fitted* to a real capture, not guessed.
- **H2 "correct" xor** must be determined empirically vs ground truth.
- **H3** requires the trace to first point at specific bad programs; only then is
  the parser audit targeted.
- Need to confirm at runtime that SFX really take the real path
  (`sndp_real_path` > 0, `sndp_stub_path` == 0) — assumed from build flags but
  cheap to verify in the trace.
