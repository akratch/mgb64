# Native Port — Critical Code Audit (Decompilation, Scaffolding & Renderer)

**Date:** 2026-07-03 (expanded + independently re-verified same day)
**Branch:** `main` (`d894648`)
**Method:** Two passes. **Pass 1** — read-only multi-agent static audit across seven lanes (endianness/asset loading, stub inventory & incomplete decomp, renderer correctness, memory safety/UB, audio, sim/input/save/config, performance); every finding required to be provable from quoted code with a traced data flow. **Pass 2 (this revision)** — seven independent verification agents re-checked every file:line anchor against the tree, re-ran the §4 warning sweep live, researched N64/libultra ground truth and prior art from other decomp ports (sm64ex, Ship of Harkinian, fgsfdsfgs perfect_dark), and rewrote each section to be junior-executable: exact patches, exact commands, acceptance criteria, and pitfalls. Each section ends with an **Audit-pass corrections** list recording where pass 1 was wrong or stale — read those before citing pass-1 numbers anywhere else.

> **Headline changes from the verification pass:**
> 1. **§6.1 shoot-out-the-lights verification is already DONE** (W4.E1.T2, commit `f860a41`): 6,210 fixture triangles swept across bunker1/silo/control, `vtx_base_offset` was 0 everywhere, zero DIVERGE. The remaining work is only the manual pass + default-ON flip. (Also: Facility is an invalid test level — it has no light-fixture textures.)
> 2. **§6.3 autogun beam tick upgraded to MEDIUM / confidence HIGH** — confirmed in the shipping US/JP MIPS, fires every frame an autogun exists, and is sim-visible (consumes RNG).
> 3. **§3.1 gamepad deadzone is ~59% raw deflection (not 52%) and is constant** — the "widens with framerate" claim was backwards; the sim never ticks above 60 Hz.
> 4. **§4 found a fresh regression:** `prop.c:3331` missing prototype (commit `429391c`, post-audit) — benign today, but breaks `-DPORT_STRICT=ON`. Trivial fix. §4.4's original anchors were dead code; re-scoped to two live 32-slot stack buffers.
> 5. **§5.2 settex eviction downgraded MEDIUM → MEDIUM-LOW** (needs >2048 distinct settex uploads between stage loads; not shown reachable). Fix retained (one line).
> 6. **§2.2's "one multiply loop" fix was wrong as written** — it would break byte-identical defaults. Correct fix splits `Audio.SfxVolume` (default 0.7) from `Audio.MasterVolume` (default → 1.0).
> 7. **All §5/§7 renderer anchors are in `src/platform/fast3d/gfx_pc.c`** — a same-named legacy `src/platform/gfx_pc.c` exists but is excluded from the build. Do not patch the wrong file.

---

## How to read this

Each finding carries a **severity** and, where possible, an **A/B or repro handle** (an env var or scenario) so it can be reproduced and fixed operationally rather than argued about. The report is ordered by **expected user-visible impact**, not by lane. Findings are structured for direct execution: *Background* (N64 + port context), *Verified trace* (quoted code, current line numbers), *Fix* (exact code), *Steps*, *Verification & acceptance criteria*, *Pitfalls*. A per-lane "verified clean" appendix is at the end — that list is as important as the findings, because it tells us where *not* to spend effort.

**Severity key:** CRITICAL = data loss or crash in normal play · HIGH = wrong behavior/feature-loss in normal play · MEDIUM = wrong behavior in a reachable-but-narrower scenario · LOW = latent / hardening / cosmetic.

**Build/test conventions used throughout** (discovered from the tree, cited once here):
- Build: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`; tests: `ctest --test-dir build --output-on-failure` (expect 32/32).
- Sanitizers: `cmake -B build-asan -DSANITIZE=ON` (= `-fsanitize=undefined,address -O1 -g`, `CMakeLists.txt:497-501`), or turnkey `tools/asan_smoke.sh [--gate]`.
- Strict warnings: `cmake -B build-strict -DPORT_STRICT=ON` (`CMakeLists.txt:503+`).
- Headless deterministic capture: `SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 ./build/ge007 --rom baserom.u.z64 --level <slug> --deterministic --screenshot-frame N --screenshot-label X --screenshot-exit` (BMP writer `platform_sdl.c:608`). Never use `--ramrom` for visuals (renders black headless).
- Perf: `tools/perf_census.sh` (headless 180-frame runs, samples `work_ms` after frame 80 from `GE007_PERF_TRACE=1` lines, CSV to `baselines/perf_census_latest.csv`).
- Audio capture: `GE007_AUDIO_DUMP=/path.raw` (+ `GE007_MUSIC_AUDIO_DUMP` pre-SFX tap); compare with `tools/compare_audio.py` / `tools/compare_audio_reference.py`.

---

## Top priorities (do these first)

| # | Severity | Finding | File | Handle |
|---|----------|---------|------|--------|
| 1 | **CRITICAL** | EEPROM save written non-atomically, errors ignored → crash mid-write silently wipes all campaign saves (window is 2 full-file rewrites per logical save) | `stubs.c:6397` | `kill -9` loop while saving (script in §1.1) |
| 2 | **HIGH** | Audio synth runs synchronously on the render thread → frame cost + audible underrun on any stall. Design resolved: opt-in producer thread; deterministic mode stays synchronous | `audi_port.c:392` → `platform_sdl.c:2710` | `tools/perf_census.sh` + `GE007_AUDIO_TRACE` |
| 3 | **HIGH** | Gamepad aim-mode look is int-truncated per frame with no fractional carry → crosshair dead until **~59% raw** stick deflection (constant at all framerates) | `lvl.c:5882-5884` | `GE007_TRACE_PAD_LOOK=1` (§3.1) |
| 4 | **MEDIUM→feature** | "Shoot out the lights" fully coded **and verified** (W4.E1.T2: 6,210 tris, 0 DIVERGE) — remaining work is the manual pass + default-ON flip | `lightfixture.c:117` | `tools/shoot_out_lights_regression.sh` |
| 5 | **MEDIUM** | Autogun tracer-beam tick dropped from the solo prop loop (confirmed vs shipping MIPS; beams draw+arm but never advance/expire; sim-RNG-visible) | `chrprop.c:5439` | `GE007_PROBE_AUTOGUN_BEAM=1` on Runway/Control |
| 6 | **MEDIUM** | PC-path `G_VTX` lacks `dest_idx+num_verts` bound → OOB write past the whole `rsp` global | `fast3d/gfx_pc.c:21791` | forced-param self-test under ASan (§5.1) |
| 7 | **MEDIUM-LOW** | Settex-cache eviction deletes/unbinds a GL texture without flushing buffered triangles (downgraded: overflow not shown reachable in normal play) | `fast3d/gfx_pc.c:21588` | shrink `SETTEX_CACHE_SIZE` + screenshot diff (§5.2) |
| — | **trivial** | NEW: `prop.c:3331` missing prototype for `sub_GAME_7F0B9E04` breaks `PORT_STRICT` builds (post-audit regression, commit `429391c`) | `prop.c:3331` | `ub_sweep.sh` (§4) |

---
## 1. Save / persistence

### 1.1 — CRITICAL — EEPROM save file written non-atomically with zero error checking

> **STATUS 2026-07-04 — IMPLEMENTED + VERIFIED — LANDED to main as merge `b55de48` (item commit `5aa5413`).** Two deviations from the verbatim patch, both forced by this tree and re-verified: (1) `<errno.h>`/`<unistd.h>` are scoped to the EEPROM section, not file-top — libultra's `OSContStatus.errno` field (`stubs.c:589/591/5916/6355`) collides with the `errno` macro (4 compile errors otherwise); (2) the `#ifdef _WIN32` block adds `NOMINMAX` + `#undef near`/`#undef far` — `<windows.h>`'s legacy `near`/`far` macros would break `guPerspective()` defined below in the same TU (surfaced by 3-lens review). **Also: this section's kill-loop recipe (`--level 1`) never triggers a save** — the AUTO_UNLOCK_SOLO seeder requires `MENU_FILE_SELECT` (`GE007_AUTO_START` menu-drive). Acceptance met: build both backends clean, ctest 22/22, kill-loop 12/12 no truncation, persistence+reload (414 trace frames, no reset), read-only savedir logs `fopen` failure and survives.

**Primary anchor: `src/platform/stubs.c:6397-6404` (`eeprom_save_to_file`) — verified current.**

#### Background (N64 + port context)

On real hardware GoldenEye uses a 16-Kbit EEPROM (`osEepromProbe` in the port returns `EEPROM_TYPE_16K`, `stubs.c:6407`): 2048 bytes = 256 blocks × 8 bytes. libultra's `osEepromLongWrite` is a *blocking* loop of 8-byte page writes with a 12 ms settle timer between pages — see the in-tree reference implementation `src/libultrare/io/conteeplongwrite.c:11-31`. **That file is N64-only and is NOT linked into the native build** (no `conteep*` entry in any `CMakeLists.txt` source list; the `stubs.c` definitions at 6408/6418/6431/6434 are what link). The fix touches exactly one file.

GoldenEye's save layout inside the 2048 bytes (structs in `src/game/file.h:7-29`):

- **Block 0 (bytes 0–31):** `smallSave` — `chksum1`/`chksum2` (CRC pair) + 24 bytes of header data.
- **Blocks 4+ (bytes 32–511):** `saves[]` array of `save_data`, 0x60 = 96 bytes each, **5 slots persisted** (`joyGamePakLongRead(4, (u8 *)&saves, sizeof(save_data) * 5)`, `file2.c:538`); the 6th (`SAVESLOTRAMROM`) is in-memory only. Each `save_data` carries its own `chksum1`/`chksum2` (`fileGenerateCRC` over `completion_bitflags..end`, `file2.c:102`).
- 5 physical slots back **4 logical folders** via a slot-rotation scheme: `fileOverwriteSaveSlotWithNewSave` (`file2.c:757-799`) writes the new copy into a free (`SAVEFLAG_DORESET`) slot with an incremented 2-bit rotation counter, *then* invalidates the old slot. `fileValidateSaves` (`file2.c:582-596`) uses the counter to pick the newer copy when two valid slots claim the same folder.

**The point that makes this CRITICAL:** the game *itself* implements journaled, torn-write-safe saves — on real EEPROM an interrupted write corrupts at most the 8-byte block in flight, and the CRC + slot-rotation scheme recovers the previous copy. The port's file backend defeats this entirely: all 5 slots plus the CRC header live in one `ge007_eeprom.bin` that is **truncated to zero as a whole** (`fopen("wb")`) on every write. A crash in the window between truncate and `fclose` destroys *both* the new and the old copy of every folder at once — a failure mode strictly worse than the hardware the game was designed to survive.

#### Verified code trace

**The buggy writer — `src/platform/stubs.c:6397-6404`** (quoted verbatim):

```c
static void eeprom_save_to_file(void) {
    FILE *f = fopen(savedirPath(EEPROM_FILENAME), "wb");
    if (f) {
        size_t write_count = fwrite(s_eeprom_data, 1, EEPROM_FILE_SIZE, f);
        (void)write_count;
        fclose(f);
    }
}
```

`fopen("wb")` truncates immediately; `fwrite` and `fclose` returns are discarded (disk-full is silent); no fsync; no temp file.

**Call graph (all main thread — the port is single-threaded):**

```
end_of_mission_briefing (src/game/file.c:55)          — every mission completion
cheat unlocks (file.c:60, cheat_buttons.c:1368/1413)  — cheat/stage unlock
folder create/delete/copy (file2.c:495/1147/1234)     — menu operations
boot repair (fileValidateSaves, file2.c:557/586/592/601)
port unlock seeding (stubs.c:5631, GE007_AUTO_UNLOCK_SOLO)
  └→ fileWriteSave (file2.c:98-107)
       └→ joyGamePakLongWrite (src/joy.c:1106-1115)
            └→ osEepromLongWrite (stubs.c:6418-6428)
                 └→ eeprom_save_to_file()   ← ONE full 2048-byte truncate+rewrite
```

`fileWriteSave` issues the 96-byte slot as **one** `osEepromLongWrite` call (`file2.c:105`), so the port does one full-file rewrite per long-write — good. **But** a single logical save is usually *two* long-writes: `fileOverwriteSaveSlotWithNewSave` calls `fileWriteSave` for the new slot (`file2.c:776`) and then `fileResetSave(save1)` (`file2.c:780` → `file2.c:120` → `fileWriteSave` again). So every mission completion = **2 truncate+rewrite windows**; a boot-time repair of a corrupt file (`fileValidateSaves`) can issue **up to ~6** (`fileWriteSmallSave` at `file2.c:534` plus per-folder resets). The window is exercised constantly.

**Return codes are already ignored end-to-end:** `fileWriteSave` discards `joyGamePakLongWrite`'s return (`file2.c:105`), matching N64 behavior — the retail game does not retry EEPROM writes either.

**Manifestation on next boot:** `eeprom_load_from_file` (`stubs.c:6384-6395`) `memset`s the buffer and `fread`s whatever is left, silently zero-filling a truncated tail. `fileValidateSaves` (`src/game/file2.c:499-608` — note: pass 1's `499-560` undershot; resets at 557/586/592, `fileBuildWriteNewSave` at 601) sees CRC mismatches and **silently resets every damaged folder** — total campaign progress loss, zero user-visible error.

**The template fix next door — `src/platform/config_pc.c:452-525`** (`configSave`): writes `ge007.ini.tmp`, checks `fopen` and `fclose` with `strerror(errno)` logging, `remove()`s the temp on failure, then atomically replaces via `replaceConfigFile` (`config_pc.c:432-438`):

```c
static s32 replaceConfigFile(const char *tmp_path, const char *path) {
#ifdef _WIN32
    return MoveFileExA(tmp_path, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
#else
    return rename(tmp_path, path);
#endif
}
```

(POSIX `rename()` over an existing file is atomic; plain Windows `rename()` fails if the target exists, hence `MoveFileExA`. `savedir.c` is not yet Windows-clean, but mirror the `#ifdef` anyway for parity.) `configSave` does *not* fsync; for a 2 KB file holding all campaign progress, add it.

**Trap pass 1 missed:** `savedirPath()` returns a **static buffer** (`s_pathBuf`, `savedir.c:96-105`). Calling it once for the final path and again for the temp path clobbers the first result. `configSave` avoids this by `snprintf`-ing into a local first (`config_pc.c:461-463`) — the fix must do the same.

#### Fix (exact code)

Add near the top of `stubs.c` (after line 14, `#include <time.h>`): `#include <errno.h>`, and:

```c
#ifdef _WIN32
#include <windows.h>
#include <io.h>       /* _commit, _fileno */
#else
#include <unistd.h>   /* fsync */
#endif
```

Replace `stubs.c:6397-6404` (note signature change `void` → `int`):

```c
/* Atomically persist the EEPROM image: write to a temp file in the same
 * directory, flush to disk, then rename over the real file. Returns 0 on
 * success, -1 on failure (logged; in-memory copy stays authoritative so a
 * later write can retry). Mirrors configSave() in config_pc.c. */
static int eeprom_save_to_file(void) {
    char path[1024];
    char tmp_path[1024 + 8];
    FILE *f;

    /* savedirPath() returns a static buffer -- copy before reuse. */
    snprintf(path, sizeof(path), "%s", savedirPath(EEPROM_FILENAME));
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    f = fopen(tmp_path, "wb");
    if (!f) {
        fprintf(stderr, "[GE007-PC] EEPROM save failed: fopen(%s): %s\n",
                tmp_path, strerror(errno));
        return -1;
    }

    if (fwrite(s_eeprom_data, 1, EEPROM_FILE_SIZE, f) != EEPROM_FILE_SIZE) {
        fprintf(stderr, "[GE007-PC] EEPROM save failed: short write to %s: %s\n",
                tmp_path, strerror(errno));
        fclose(f);
        remove(tmp_path);
        return -1;
    }

    if (fflush(f) != 0
#ifdef _WIN32
        || _commit(_fileno(f)) != 0
#else
        || fsync(fileno(f)) != 0
#endif
    ) {
        fprintf(stderr, "[GE007-PC] EEPROM save failed: flush/sync %s: %s\n",
                tmp_path, strerror(errno));
        fclose(f);
        remove(tmp_path);
        return -1;
    }

    if (fclose(f) != 0) {
        fprintf(stderr, "[GE007-PC] EEPROM save failed: fclose(%s): %s\n",
                tmp_path, strerror(errno));
        remove(tmp_path);
        return -1;
    }

#ifdef _WIN32
    if (!MoveFileExA(tmp_path, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fprintf(stderr, "[GE007-PC] EEPROM save failed: replace %s: Windows error %lu\n",
                path, (unsigned long)GetLastError());
        remove(tmp_path);
        return -1;
    }
#else
    if (rename(tmp_path, path) != 0) {
        fprintf(stderr, "[GE007-PC] EEPROM save failed: rename %s -> %s: %s\n",
                tmp_path, path, strerror(errno));
        remove(tmp_path);
        return -1;
    }
#endif

    return 0;
}
```

And in `osEepromLongWrite` (`stubs.c:6418-6428`), propagate the result:

```c
s32 osEepromLongWrite(OSMesgQueue *mq, u8 address, u8 *buffer, s32 nbytes) {
    s32 offset;
    (void)mq;
    eeprom_load_from_file();
    offset = (s32)address * EEPROM_BLOCK_SIZE;
    if (offset + nbytes > EEPROM_FILE_SIZE) nbytes = EEPROM_FILE_SIZE - offset;
    if (nbytes > 0 && buffer) {
        memcpy(s_eeprom_data + offset, buffer, nbytes);
        if (eeprom_save_to_file() != 0) return -1;
    }
    return 0;
}
```

**On surfacing failures to the game — return `-1`, log, do NOT add retry/UI.** Rationale: (a) libultra's contract is nonzero-on-error, so `-1` is faithful; (b) every in-tree caller (`fileWriteSave` `file2.c:105`, `joyGamePakLongWrite` `joy.c:1111`) already discards the return — retail GoldenEye does not retry, so nothing changes behaviorally and the sim-invariance gate is untouched; (c) the `memcpy` before the save keeps `s_eeprom_data` authoritative, so a transient failure (disk momentarily full) self-heals on the next save; (d) the stderr log makes the failure diagnosable, which is the actual gap. A settings-menu toast is possible future polish, not part of this fix.

#### Step-by-step implementation

1. Add the include lines to `src/platform/stubs.c`.
2. Replace `eeprom_save_to_file` with the code above.
3. Update the one caller, `osEepromLongWrite` (`stubs.c:6426`), to `if (eeprom_save_to_file() != 0) return -1;`.
4. Do not touch `file2.c`, `joy.c`, or the libultrare reference files.
5. Optional hardening (separate commit, both one-liners):
   - **Load side** (`stubs.c:6391-6392`): do not reject a short file — the game's CRC layer *is* the recovery path and first boot legitimately has no file — but log it: `if (read_count != EEPROM_FILE_SIZE) fprintf(stderr, "[GE007-PC] EEPROM file short (%zu/%d bytes); interrupted write? CRC validation will repair.\n", read_count, EEPROM_FILE_SIZE);`
   - **Reset visibility** (`src/game/file2.c:555-558`, inside `if (!checksumOK2)`): add an `#ifdef NATIVE_PORT`-guarded `fprintf(stderr, "[GE007-PC] save folder slot %d failed CRC; resetting\n", (int)i);` before `fileResetSave(&saves[i]);`. Guarding keeps the shared game source N64-clean, matching the existing `#ifdef NATIVE_PORT` instrumentation in this file (e.g. `port_save_delete_count`, `file2.c:1127`).

#### Verification & acceptance criteria

Build + ctest per the conventions block (expect 32/32, unchanged). Kill-loop repro (macOS/Linux; `--savedir` isolates, `GE007_AUTO_UNLOCK_SOLO` — `stubs.c:5469`, `=all` accepted — forces save writes at boot):

```sh
#!/bin/sh
# eeprom_kill_loop.sh — before the fix this wipes the save within ~50 iters
D=/tmp/eeprom-atomic-test; rm -rf "$D"; mkdir -p "$D"
for i in $(seq 1 200); do
  GE007_AUTO_UNLOCK_SOLO=all ./build/ge007 --savedir "$D" --level 1 --deterministic &
  PID=$!
  sleep "0.$((RANDOM % 9))"           # land inside the boot save burst
  kill -9 $PID 2>/dev/null; wait $PID 2>/dev/null
  SZ=$(stat -f %z "$D/ge007_eeprom.bin" 2>/dev/null || echo 0)
  [ "$SZ" -ne 2048 ] && [ "$SZ" -ne 0 ] && { echo "ITER $i: TRUNCATED ($SZ bytes)"; exit 1; }
done
echo PASS
```

**Pass =** (1) script prints `PASS`: `ge007_eeprom.bin` is always exactly 2048 bytes or absent, never truncated, and at most one stale `.tmp` remains (harmless, overwritten next save); (2) a subsequent normal boot logs no `[GE007-PC] save folder slot ... resetting` and unlocks persist; (3) `chmod 555 "$D"` then triggering a save prints the `EEPROM save failed: fopen` line and the game keeps running; (4) ctest 32/32; (5) sim-invariance unaffected (pure I/O plumbing, no gameplay path touched).

#### Pitfalls

- **`savedirPath` static buffer** — copy to a local before deriving `tmp_path`. This is the one way a junior turns this fix into a same-file rename no-op.
- Temp file must be in the **same directory** as the target (`"%s.tmp"` suffix guarantees this); `rename()` across filesystems fails with `EXDEV`.
- Don't move the `memcpy` after the save-failure check — in-memory EEPROM must update even when the disk write fails, or the running game's own reads (`osEepromLongRead`) desync from what it just wrote.
- Don't "fix" `fileWriteSave` to check returns in the same change — it's shared N64 game source; behavior parity is a project rail.
- `stubs.c` builds without `<errno.h>`/`<unistd.h>` today; forgetting the includes gives implicit-declaration *warnings*, not errors — check the build log.

#### Audit-pass corrections (§1)

1. `fileValidateSaves` is `file2.c:499-608`, not `499-560`.
2. Pass 1 undersold the blast radius: one *logical* save = **two** full-file truncate+rewrites (slot rotation), and boot-time repair can issue up to ~6.
3. Worth stating: the game's own dual-slot CRC rotation scheme is a torn-write journal that the flat-file backend silently defeats — the strongest argument this is CRITICAL, not just hygiene.
4. `configSave` is a good template but does **not** fsync; the EEPROM fix adds `fsync`/`_commit` rather than copying it verbatim.
5. The libultrare `osEepromLongWrite` is not linked in the native build; `stubs.c` owns the symbol, so the fix touches exactly one file.

---
## 2. Audio

> **Architecture preamble (verified, important for all work below).**
> `src/platform/audio_pc.c` is **not** dead code. Because `PORT_SND_STUBS` is defined (`src/snd.h:12-14`), every SFX is routed by `snd.c` into the **PortVoice float mixer** in `audio_pc.c` (`portAudioMixActiveVoices`, `audio_pc.c:742`), which is mixed into the synth output buffer once per frame via `portAudioMixSfxIntoBuffer` (`audio_pc.c:1215`, called from `audi_port.c:452`).
>
> What **is** dead in `audio_pc.c`: the legacy CSP music engine (`musicMixSamples`, ~800 lines, its own nearest-neighbor wrap at `:2177/:2200`) and `audioCallback` (`audio_pc.c:837-854`) — the output device is opened **queue-mode** (`SDL_QueueAudio` in `osAiSetNextBuffer`, `stubs.c:450-508`), so no SDL callback ever fires. Music/reverb proper is the faithful libaudio reimplementation: `alAudioFrame` (`audio_compat.c:2245`) walks the sequence players and emits an Acmd list, `mixer.c` executes it (ADPCM decode, authentic 64-phase×4-tap polyphase resampler — coefficient table `mixer.c:189-222`, executor `:436-477`, envmixer, reverb).
>
> **N64 reality check** (why the current shape exists): on hardware, GoldenEye's audio manager ran as its **own OS thread** feeding the osSched scheduler; each retrace it built an Acmd list with libaudio and handed it to an **RSP audio task**, whose output buffers the **AI DMA** consumed at a fixed DAC rate — audio production was decoupled from the graphics frame by the scheduler and double-buffered AI. Both music *and* SFX went through the same RSP polyphase resampler. The port collapses this to one thread: `amStartAudioThread` is an empty stub (`audi_port.c:388-390`, *"No thread on port — audio runs synchronously via portAudioFrame()"*), and `portAudioFrame()` (`audi_port.c:392`) is pumped once per render frame from `platform_sdl.c:2710`.
>
> **Prior art on PC ports:** sm64-port/sm64ex keep synthesis on the game thread but decouple via a queue-depth heuristic — each game frame they check `audio_api->buffered()` against a desired level and synthesize **two** audio frames' worth when the queue is low (this port's occupancy controller at `audi_port.c:411-433` is the same idea, tuned per-frame instead of 0-or-2). Ship of Harkinian goes further: libultraship runs an **AudioMgr on a dedicated thread**, signaled once per game frame — i.e. it restores the N64's separate-audio-thread topology. The fgsfdsfgs perfect_dark port (this game's sister decomp) drives its libultra audio frame from the port main loop with an SDL queue-occupancy check, sm64-style. Both models work; the deciding factor for us is determinism (see 2.1).
>
> **All findings below are on the live SFX side-mixer plus the frame-pump plumbing.** The libaudio music path itself is clean.

### 2.1 — HIGH — Full N64 synth runs synchronously on the render/game thread

**Background.** There is exactly one thread: sim tick, DL interpretation, T&L, GL/Metal submit, *and* the entire audio DSP chain. `portAudioFrame()` (`audi_port.c:392-645`) runs, in order: `amClearDmaBuffers()` → frame-size selection (`:411-433`) → `alAudioFrame(...)` (`:437-438`, full ADPCM + polyphase + envmixer + reverb) → optional low-pass (`:439`) → SFX mix (`:452`) → `osAiSetNextBuffer` (`:455`) → telemetry. Cost is ~0.2–0.8 ms/frame scaling with voice count — but the real problem is **coupling**: any main-thread stall (level load, heavy frame, shader compile) starves the SDL queue → audible underrun. The occupancy controller (`:411-433`) and the drop cap (`stubs.c:455-508`, `AI_QUEUE_LIMIT_FRAMES 5`) exist to fight exactly this coupling.

**Verified trace — what the synth actually reads (this decides the design).**

1. **Game→audio inputs.** Music: `src/music.c` calls `alCSPPlay` (~15 sites) and — critically — *polls synth state for game logic*: `alCSPGetState` (~30 sites, e.g. `music.c:165, 234, 1166`). Its stop-drain loop **directly pumps the sequence-player handler on the game thread** (`music.c:291-298`):
   ```c
   for (attempts = 0; attempts < 8 && alCSPGetState(seqp) != AL_STOPPED; attempts++) {
       if (seqp->node.handler == NULL) break;
       (void)seqp->node.handler(&seqp->node);   /* same handler alAudioFrame invokes */
   }
   ```
   SFX: `snd.c` (stub path) calls `portAudioPlaySfxDetailed`/update APIs in `audio_pc.c`, which **already lock `s_audioMutex`** (created `audio_pc.c:919`; ~20 lock sites).
2. **What `s_audioMutex` protects today:** only `s_voices[]`/`s_sfxSamples[]`/stats in `audio_pc.c`. It does **not** cover any libaudio state (`audio_compat.c` sequence players, event queues, `ALSynth` voice lists, `mixer.c` `rspa` state, `audi_port.c` DMA ring). Today that's safe because everything is one thread; it is the gap a threading change must close.
3. **Audio→game feedback:** only `alCSPGetState` polls (music sequencing logic) and read-only telemetry (`sndGetPlayerStats`, `mixerGetStats`). `alAudioFrame` consumes **no sim state directly** — all game intent arrives as prior API calls/events (`alEvtqPostEvent`, `audio_compat.c:2476`). So audio does *not* need to stay frame-synchronous for **sim** correctness.
4. **The determinism constraint (the real invariant):** in `--deterministic` runs the controller uses a Bresenham cadence (NTSC 720/736 alternating, `audi_port.c:412-416`) and `osAiSetNextBuffer` uses a separate deterministic queue contract (`stubs.c:476-492`). `GE007_AUDIO_DUMP` / `GE007_MUSIC_AUDIO_DUMP` regression baselines (`port_trace.c:8307-8311`, `tools/compare_audio.py`) depend on **exactly N synth frames per game frame**. Additionally, music progression as observed by `alCSPGetState` game logic must stay a deterministic function of game frames in deterministic mode. A free-running audio thread breaks both.

**Recommendation: dedicated producer thread, opt-in, deterministic mode stays synchronous.** SDL pull-callback is rejected: it inverts the whole `osAiSetNextBuffer`/queue-mode design, kills the dump/trace tooling shape, and puts libaudio on the OS realtime audio thread where a lock-hold by the game thread means guaranteed device underruns. "Bigger queue, stay sync" is just more latency (effectively what `AI_QUEUE_LIMIT_FRAMES 5` already is) and does not remove the ms from the frame budget.

**Fix (concrete plan).**

1. **Setting:** `Audio.ThreadedSynth` (int 0/1, default **0**, `SETTING_SCOPE_RESTART`, env `GE007_AUDIO_THREAD`), registered in `portAudioRegisterConfig()` (`audio_pc.c:856`). Forced OFF when `g_deterministic` — the sync path is the deterministic contract.
2. **Lock:** add one global synth lock, e.g. `SDL_mutex *g_alSynthMutex` owned by `audio_compat.c`, with `alSynthLock()/alSynthUnlock()` no-ops when threading is off. It must be taken by:
   - the producer thread around the whole body of `portAudioFrame()`;
   - every **game-thread libaudio entry point**: `alCSPPlay/alCSPStop/alCSPGetState/alCSPSetVol/alCSPSetTempo/...` and `alEvtqFlush` (instrument the public `al*` functions in `audio_compat.c`, not the ~50 `music.c` call sites), plus the manual handler pump at `music.c:291-298` (wrap it in `alSynthLock()`);
   - **not** the SFX APIs — they keep `s_audioMutex`, which becomes contended-but-correct once `portAudioMixSfxIntoBuffer` runs on the producer thread (the file documents this contract at `audio_pc.c:549`).
   Lock ordering rule: `g_alSynthMutex` → `s_audioMutex` (portAudioFrame takes synth lock, then the SFX mix takes `s_audioMutex` inside it). Never the other order — audit that no `audio_pc.c` API calls back into libaudio (today none do).
3. **Thread body:** spawn in `amStartAudioThread()` (`audi_port.c:388` — the hook already exists and is called at `music.c:1119`, after `amCreateAudioManager` at `music.c:1073`, so all state is initialized):
   ```c
   static int portAudioThreadBody(void *arg) {
       (void)arg;
       while (SDL_AtomicGet(&s_audioThreadRun)) {
           u32 queued = osAiGetLength();                       /* stubs.c:518 */
           u32 target = (portAudioGetFrameSize() * 3 / 2) * 4; /* same 1.5-frame target as :420 */
           if (queued < target) portAudioFrame();               /* takes g_alSynthMutex inside */
           else SDL_Delay(2);                                   /* ~44 samples @22050; queue can't drain past target between polls */
       }
       return 0;
   }
   ```
   The existing occupancy controller (`:418-428`) keeps working unmodified — it already sizes each frame from `osAiGetLength()`. Guard the `platform_sdl.c:2710` call with `if (!portAudioThreadActive()) portAudioFrame();`.
4. **Shutdown / level transition:** set `s_audioThreadRun=0` and `SDL_WaitThread` **before** `SDL_CloseAudioDevice`/`alClose` in the platform shutdown path. Level transitions need no special handling: music stop/start goes through the now-locked `al*` entry points, and the ROM/bank data the DMA callback reads (`amDmaCallback`, `audi_port.c:195`, reads `g_romData`) is immutable after boot (`amCreateAudioManager` runs once; verified nothing frees/reloads ctl/tbl mid-run).

**Steps.** (1) Add setting + atomic flag + thread skeleton, thread still calling nothing — build. (2) Add `g_alSynthMutex` + wrap `al*` entry points and the `music.c:291` pump — build, run **sync** mode, confirm zero behavior change. (3) Flip the pump into the thread behind the setting. (4) TSan (`-fsanitize=thread`) run on Facility with music + heavy SFX, 5 min. (5) A/B soak.

**Verification & acceptance.**
- **Determinism gate:** `GE007_AUDIO_DUMP=/tmp/a.raw` deterministic run, threaded **off**, byte-identical to pre-change baseline via `tools/compare_audio.py`. Threaded mode is exempt by design (it refuses to arm under `--deterministic`).
- **Underruns:** `GE007_AUDIO_TRACE=/tmp/t.jsonl` (per-frame JSON, `audi_port.c:470`) — `underruns` and `dropped_buffers` stay 0 over a 5-minute threaded session **including a level load** (loads are the stall the fix targets).
- **Perf:** wrap the `platform_sdl.c:2710` call in the `GE007_PERF_TRACE` timer as a new `audio_ms` column; `tools/perf_census.sh` on a music-heavy level (Surface/Statue) must show `audio_ms` ≈ 0 in threaded mode and unchanged `work_ms` otherwise.
- ASan + TSan clean; `GE007_MUTE=1` and headless paths still boot.

**Pitfalls.** (i) Do **not** move only `alAudioFrame` — the SFX mix and `osAiSetNextBuffer` must move with it or the queue is written from two threads. (ii) `alCSPGetState` polled unlocked is a data race even if it "just reads an int" — route it through the lock (it's cheap). (iii) The telemetry block (`audi_port.c:457-641`) calls `sndGetPlayerStats`/`mixerGetStats`; those move to the thread wholesale — fine, but don't add game-thread callers later without the lock. (iv) `s_masterVolume` and the H1 knobs are LIVE settings written by the game thread; they're single-word reads documented as lock-free-tolerant (`audi_port.c:106-107,161`) — leave them relaxed. (v) Watch the lock-order rule if anyone later makes `snd.c` take the synth lock.

### 2.2 — HIGH (UX) — `Audio.MasterVolume` does not affect music/reverb, only SFX

**Background / verified trace.** Registered LIVE at `audio_pc.c:859-863` (`settingsRegisterFloat("Audio.MasterVolume", &s_masterVolume, 0.7f, 0.0f, 1.0f, ...)`). Its only live use is the per-sample SFX multiply at `audio_pc.c:793`:
```c
sample = (f32)smp->pcm[pos] * s_masterVolume * gain;
```
(the other uses, `:2071/:2177`, are in the dead CSP music engine). `portAudioFrame` queues `info->data` with no master scale. So the slider reweights SFX-vs-music, and the **0.7 default attenuates SFX ~3.1 dB relative to music at stock settings**. Acknowledged in-tree: `pc_settings_menu.c:104-115` tags the row `SM_FUTURE` ("W6.E3.T1 lands the live wiring").

**Fix (exact).** Do it entirely inside `audio_pc.c` so `s_masterVolume` stays file-local, and **split the buses so defaults stay byte-identical** (this is the subtle part — naïvely scaling the whole bus by the 0.7 default would drop all music by 3 dB and invalidate every audio baseline):

1. Add `static f32 s_sfxVolume = 0.7f;` and register it as `Audio.SfxVolume` (LIVE, default 0.7). Change `Audio.MasterVolume` default to **1.0f**.
2. `audio_pc.c:793` becomes:
   ```c
   sample = (f32)smp->pcm[pos] * s_sfxVolume * gain;
   ```
3. Master applies to the *full* mixed bus at the end of `portAudioMixSfxIntoBuffer` (`audio_pc.c:1215`), after `portAudioMixActiveVoices` and inside the existing `s_audioMutex` hold. `out` is `info->data` — the s16 interleaved-stereo ALHeap buffer allocated at `audi_port.c:357-360`, `numSamples` sample frames = `frameSamples`:
   ```c
   if (s_masterVolume < 0.999f) {
       for (s32 i = 0; i < numSamples * 2; i++) {
           f32 v = (f32)out[i] * s_masterVolume;
           /* range [0,1] can't overflow s16, but clamp anyway for safety */
           if (v > 32767.0f) v = 32767.0f;
           if (v < -32768.0f) v = -32768.0f;
           out[i] = (s16)v;
       }
   }
   ```
4. Un-`SM_FUTURE` the `Audio.MasterVolume` and `Audio.SfxVolume` rows in `pc_settings_menu.c` (this *is* W6.E3.T1's MasterVolume half; MusicVolume/reverb/width remain future).

**Verification & acceptance.** (a) Defaults byte-identical: deterministic `GE007_AUDIO_DUMP` before/after via `tools/compare_audio.py` — must match exactly (SFX still ×0.7, master ×1.0 is a no-op via the `<0.999f` early-out). (b) `--config-override Audio.MasterVolume=0.25`: full-bus RMS in the dump drops ~12 dB (check with `tools/compare_audio_reference.py` — per-band dB deltas; all bands, music-only segments included, must move together). (c) `Audio.MasterVolume=0.0` → digital silence after the mix point. (d) Slider audible in-game on both a music bed and gunfire.

**Pitfalls.** Scale **after** the SFX mix (single point of truth) but be aware of 2.5's filter reorder — final order must be synth → SFX mix → master → low-pass (the filter models the DAC, which sat after everything). Don't also multiply `info->data` in `audi_port.c` — one bus scale only, or SFX get master² through the mixed buffer. The dead `audioCallback`/`musicMixSamples` uses of `s_masterVolume` need no change.

### 2.3 — MEDIUM — SFX loop wraps at end-of-buffer instead of authored `loopEnd`
### 2.4 — MEDIUM (quality) — SFX resampling is nearest-neighbor

*(One shared fix — the same eight lines of mixer inner loop.)*

> **STATUS 2026-07-04 — IMPLEMENTED + VERIFIED — LANDED to main as merge `5b717d3` (off `aed2bd7`, item commit `381351b`).** Re-derived on top of the landed W6.E3.T1-v2 audio buses: **§2.2 was superseded** by that branch (3-bus Q15 — `s_master/s_music/s_sfxBusVolume`), so this item is ONLY §2.3/2.4 — the mixer inner loop keeps `* s_masterVolume * gain` (W6's SFX-bus semantics, unchanged), NOT the doc's `s_sfxVolume`. Adds an authored-loop-window `[loopStart, loopEnd)` wrap (loopEnd is EXCLUSIVE — confirmed against `audio_compat.c:4276`) with fractional-phase preservation + linear interpolation, gated by `GE007_SFX_MIX_LEGACY=1` (restores the exact old nearest-neighbor/end-wrap path). Verified: build both backends + ctest 22/22; a standalone micro-test proves the new path loops the authored window (reads `[20,60)` vs legacy's buggy `[20,100)` incl. release tail), preserves phase (period = span/pitch = 26.67), interpolates, and never reads OOB; **audio A/B (bunker1+gunfire, facility): `GE007_SFX_MIX_LEGACY=1` dump is BYTE-IDENTICAL to the `aed2bd7` baseline** (exact old-path replication → zero regression), and the default is byte-identical on both scenes too (their SFX are pitch-1.0 one-shots → `frac=0`, fix inert — matches "no looping/pitched SFX → byte-identical"); ASan clean on the mixer new path (silo+gunfire, 0 hits). **This is a documented default change**: looping/pitched SFX now play the authored loop with interpolation; `GE007_SFX_MIX_LEGACY=1` is the A/B escape hatch. No committed audio baselines exist to re-bless (the audio gates generate them dynamically).

**Background.** Bank data authors loops as `[loopStart, loopEnd]` *inside* the decoded sample; the parser stores them (`PortDecodedSample` at `audio_pc.c:50-57`: `s16 *pcm; u32 numSamples, sampleRate; s32 hasLoop; u32 loopStart, loopEnd;`, filled at `:489-492/:530-533`). The libaudio synth honors them exactly — ADPCM: `looped = (out_count + load->sample > (s32)load->loop.end) && load->loop.count != 0` (`audio_compat.c:4274-4279`); RAW16: `load->sample = load->loop.start` reset (`audio_compat.c:4422-4443`). And on hardware, SFX went through the same 64-phase FIR as music. The PortVoice mixer does neither.

**Verified trace** — `portAudioMixActiveVoices` inner loop, `audio_pc.c:767-833` (voice fields at `:60-82`: `f32 position; f32 pitch; ...`):
```c
u32 pos = (u32)voice->position;                     /* :768  nearest-neighbor pick   */
...
if (pos >= smp->numSamples) {                       /* :776  wraps at buffer END,    */
    if (smp->hasLoop && smp->loopEnd > smp->loopStart) {
        voice->position = (f32)smp->loopStart;      /* :778  fraction snapped to 0   */
        pos = smp->loopStart;
    } else { voice->active = 0; ... break; }
}
...
sample = (f32)smp->pcm[pos] * s_masterVolume * gain; /* :793  zero-order hold        */
...
voice->position += voice->pitch;                     /* :828  pitch ≠ 1 is common    */
```
Pitch comes from `sndStubComputePitchRatioForSound` (`snd.c:221-235`, `pitch_28 * pitch_2c`, floor 0.01) — frequently ≠ 1. Consequences: any looping SFX with `loopEnd < numSamples` (engine hums, alarms, ambience) plays its release tail inside every iteration (wrong period/timbre); fractional phase resets on wrap (period error up to 1 sample/iteration); zero-order hold aliases on pitched-down SFX and zippers on bends.

**Fix (exact replacement for `audio_pc.c:768-793`).** Replace from `u32 pos = (u32)voice->position;` through the `sample = ...` line with:

```c
            /* Authored loop window: [loopStart, loopEnd) — matches the synth-side
             * convention (audio_compat.c:4278 treats loop.end as the first sample
             * NOT played before jumping to loop.start). Clamp to the decoded
             * buffer and reject degenerate windows. */
            u32 wrapEnd = smp->numSamples;
            u32 loopStart = 0;
            s32 looping = 0;
            if (smp->hasLoop && smp->loopEnd > smp->loopStart &&
                smp->loopStart < smp->numSamples) {
                looping = 1;
                loopStart = smp->loopStart;
                wrapEnd = (smp->loopEnd < smp->numSamples) ? smp->loopEnd
                                                           : smp->numSamples;
            }

            /* Wrap at the authored point, PRESERVING the fractional phase. */
            if (voice->position >= (f32)wrapEnd) {
                if (looping) {
                    f32 span = (f32)(wrapEnd - loopStart);
                    do { voice->position -= span; } while (voice->position >= (f32)wrapEnd);
                    if (voice->position < (f32)loopStart) {
                        voice->position = (f32)loopStart;   /* pitch > span safety */
                    }
                } else {
                    voice->active = 0;
                    voice->stopOnGainEnd = 0;
                    s_sfxMixStats.voiceStops++;
                    break;
                }
            }

            u32 pos = (u32)voice->position;
            f32 frac = voice->position - (f32)pos;

            gain = voice->gainCurrent;
            if (voice->envEnabled) {
                gain *= voice->envCurrent;
            }

            /* Linear interpolation with correct edge handling: the neighbor of
             * the last in-loop sample is loopStart; the neighbor past the end of
             * a one-shot is the last sample (hold, no read past numSamples). */
            {
                f32 s0 = (f32)smp->pcm[pos];
                u32 nextPos = pos + 1;
                f32 s1;
                if (nextPos >= wrapEnd) {
                    s1 = looping ? (f32)smp->pcm[loopStart]
                                 : (f32)smp->pcm[wrapEnd - 1];
                } else {
                    s1 = (f32)smp->pcm[nextPos];
                }
                sample = (s0 + (s1 - s0) * frac) * s_sfxVolume * gain;
            }
```
(`s_sfxVolume` per 2.2; keep `s_masterVolume` here if 2.2 lands later.) The declarations at `:768-774` move/merge accordingly; everything from `beforeLeft = ...` (`:794`) down, including `voice->position += voice->pitch;` (`:828`), is unchanged.

Optional A/B gate (recommended, house style): read `GE007_SFX_MIX_LEGACY` once into a static; when set, run the old nearest/end-wrap path.

**Steps.** (1) Implement + gate. (2) Build, `ctest`. (3) Captures below. (4) Listen pass on known loopers (Dam alarm, Facility ambience) and pitched SFX (distant gunfire).

**Verification & acceptance.**
- **Loop period:** `GE007_AUDIO_DUMP` a scene with a held looping SFX; the loop period must equal `(wrapEnd-loopStart)/pitch` samples and the release-tail spectral burst inside each iteration must be gone. A/B with `GE007_SFX_MIX_LEGACY=1`.
- **Fidelity:** `tools/compare_audio_reference.py <n64_or_emulator_ref.wav> <port_dump.raw> --test-format raw` — high-band (2560–10000 Hz) energy delta vs reference must *shrink* relative to the legacy path on pitched-down SFX (nearest-neighbor images live up there). It aligns by envelope, so emulator capture latency is fine.
- **Regression:** deterministic dump with **no looping/pitched SFX in frame** (menu idle) byte-identical; dumps with SFX are expected to differ — re-bless baselines in the same commit.
- ASan clean (the `wrapEnd-1`/`loopStart` reads are the new edge indices to watch).

**Pitfalls.** (i) Confirm the parser's `loopEnd` convention against a real bank before shipping: if a bank's `loopEnd` proves *inclusive* (last played sample), use `wrapEnd = loopEnd + 1` — the loop-period measurement catches an off-by-one as a 1-sample period error/click. (ii) Don't "fix" the dead `musicMixSamples` copy of this loop (`:2177/:2200`); leave dead code alone. (iii) `delaySamples`/ramp logic (`:763-765`, `:828-831`) must stay outside the edit. (iv) Guard against `loopStart >= numSamples` garbage banks (handled by the `looping` gate).

### 2.5 — LOW — Output low-pass (H1 knob) excludes SFX; PAL chained-SFX delay is wrong

**(a) Filter order.** `portAudioFrame` applies the optional DAC-coloration one-pole to the synth buffer at `audi_port.c:439` (`portAudioApplyLibaudioLowPass(info->data, info->frameSamples);`), then mixes SFX **after** it at `:452`. The N64 DAC colored *everything*; with `Audio.OutputFilter=1` (default 0, plus legacy `GE007_ENABLE/DISABLE_LIBAUDIO_LOWPASS` aliases, `audi_port.c:151-159`) SFX bypass it. Default-off → zero impact at stock settings.

**Fix (a, exact).** In `portAudioFrame`, move the call at `:439` to after the SFX mix (and after 2.2's master scale). Final order:
```c
    alAudioFrame(g_PortAudioMgr.cmdList[g_CurrentAcmdList],
                 &g_CommandLength, info->data, info->frameSamples);

    { ... GE007_MUSIC_AUDIO_DUMP block (:441-450) — keep HERE, pre-SFX ... }

    portAudioMixSfxIntoBuffer(info->data, info->frameSamples);   /* includes master scale (2.2) */
    portAudioApplyLibaudioLowPass(info->data, info->frameSamples); /* DAC model last */

    osAiSetNextBuffer(info->data, info->frameSamples * 4);
```
The music dump (`:441-450`) stays put — its documented contract is "Pre-SFX music PCM dump" (`port_trace.c:11`); with the filter enabled its content becomes pre-filter, a strictly more useful tap, and it only differs when the default-off knob is on.

**(b) PAL 735.** `snd.c:949`: `delaySamples = (s32)(delayFrames * 735);` inside the chained-SFX submit (delay accumulated at `snd.c:1022`). 735 = 22050 Hz × 2 fields / 60 (see `audi_port.c:24-31`: `OUTPUT_RATE 0x5622` = 22050, `MAYBE_FRAME_RATE` 60/50 under `#ifdef REFRESH_PAL`). PAL is a **compile-time** build flavor (`REFRESH_PAL` is `#ifdef`'d across the tree; no runtime check), so on a PAL build a game frame is 22050×2/50 = **882** samples and chained SFX fire ~17% early.

**Fix (b, exact).** In `snd.c` (top, near other port defines):
```c
/* Samples per game frame at the 22.05 kHz output rate: 22050*2/60 NTSC,
 * 22050*2/50 PAL. Keep in sync with audi_port.c MAYBE_FRAME_RATE. */
#ifdef REFRESH_PAL
#define SND_SAMPLES_PER_GAME_FRAME 882
#else
#define SND_SAMPLES_PER_GAME_FRAME 735
#endif
```
and at `snd.c:949`: `delaySamples = (s32)(delayFrames * SND_SAMPLES_PER_GAME_FRAME);`

**Verification & acceptance.** (a) Defaults byte-identical (filter off by default). With `Audio.OutputFilter=1` (the legacy env alias doubles as the A/B), a gunshot's high-band energy must now be attenuated the same as music (compare filter-on vs filter-off dumps: SFX-heavy segments move with music segments). (b) NTSC build byte-identical (macro expands to 735); PAL acceptance is analytical + capture (onset spacing = `delayFrames × 882` samples = real-time parity with NTSC). `tools/perf_census.sh` unchanged (both edits O(1)/frame).

**Pitfalls.** (i) Don't move the filter *into* `portAudioMixSfxIntoBuffer` — it must also run when zero voices are active, and its state seeding (`g_LibaudioLowPassInitialized`, `audi_port.c:175-179`) assumes it sees every output frame exactly once. (ii) If 2.1 lands first, both edits ride along unchanged (they're inside the moved pump). (iii) PAL builds are untested territory generally; don't block the NTSC hardening pass on PAL capture.

#### Audit-pass corrections (§2)

1. `platform_sdl.c:2703` → **`:2710`** (call site drifted 7 lines).
2. The SFX mix inner loop spans `:767-833`; wrap block `:776-786`; position advance at `:828` (pass 1 implied it was inside 768-793).
3. `s_audioMutex` "at `:845`" is a lock site inside the dead `audioCallback`; declaration `:92`, creation `:919`. The mutex only ever covered PortVoice/SFX state — threading needs a **new** synth-wide lock (see 2.1); the existing mutex covers only the SFX half.
4. Drop logic is `osAiSetNextBuffer` at `stubs.c:450-508` (pass 1's window clipped both ends); `osAiGetLength` is `:518`.
5. `mixer.c:189-222` is the FIR *coefficient table* only; the resampler executor is `:436-477`.
6. **2.2's "one multiply loop" fix was wrong as written** — with the 0.7 default it would attenuate all music 3 dB and double-apply master to SFX, breaking the byte-identical-at-defaults gate. Correct fix splits `Audio.SfxVolume` from `Audio.MasterVolume` (see 2.2).
7. 2.1 "sim determinism" framing resolved from code: `alAudioFrame` consumes no sim state, **but** game logic polls `alCSPGetState` (~30 sites) and `music.c:291-298` pumps the sequencer handler directly on the game thread — these, not Acmd lists, are the shared-state hazards. Deterministic-mode audio dumps + the Bresenham cadence force the threaded path to be opt-in with deterministic mode staying synchronous.
8. Prior-art note: the SoH dedicated-AudioMgr-thread and sm64ex queue-heuristic descriptions are from repository knowledge of those projects; spot-check their source before quoting verbatim elsewhere.

---
## 3. Input / timing / config

### 3.1 — HIGH — Gamepad look is integer-truncated per frame: ~59% effective aim-mode deadzone, stair-stepped hip-fire response

> **STATUS 2026-07-04 — IMPLEMENTED + VERIFIED — LANDED to main as merge `e5e90b9` (item commit `7c5cf06`).** Per-player float remainder accumulators + `pcResetGamepadLookRemainders()` (`lvl.c`, `#ifdef NATIVE_PORT`, `MAXCONTROLLERS`-sized) replace the truncating `mdx/mdy` add; added the stick-at-rest `else` reset and a `lvlStageLoad` reset call. Verified: build both backends clean; ctest 22/22; **no-pad path byte-identical** (whole-sim-hash A/B, baseline@93984e2 vs item3 both `5b59fb1a07741905`, deterministic across reruns); a standalone micro-test of the exact pipeline shows the OLD code is fully dead at 20–50% aim-mode deflection (0 px) while the accumulator tracks within ±1px of ideal and clears its carry on release; 2-lens adversarial review clean. The in-game pad A/B (criteria a–e, `GE007_TRACE_PAD_LOOK`) stays a manual step — no headless synthetic right-stick injection exists in this tree.

**Background.** On N64, GoldenEye read the analog stick directly in fixed-point each sim tick — aim mode (R-hold) moved the crosshair from raw stick values, so any deflection above the hardware notch produced movement. The port instead converts the right stick into a *synthetic mouse delta*: `lvlViewMoveTick()` (`src/game/lvl.c:5741`) fetches the real mouse delta into `mdx/mdy` (`platformGetMouseDelta`, player 1 only), then **adds the gamepad contribution into the same `mdx/mdy` integers** (confirmed: one shared path; everything below — `sens`, ADS scaling, invert-Y — treats gamepad look as mouse pixels). Mouse deltas are integers by nature; the gamepad product is a *small float* that gets truncated to `int` every frame with no fractional carry. Other N64 ports avoid this class by keeping the stick→look path in floats end-to-end (the fgsfdsfgs perfect_dark port keeps analog look in float degrees; sm64ex's analog camera likewise never round-trips through integer pixels). Since mgb64's funnel point *is* an integer mouse delta, the minimal correct fix is the standard **per-axis static float remainder accumulator**.

**Verified trace.**

- Pipeline: SDL axis → pre-clip (`platform_sdl.c:2201`: `int dz_axis = g_pcGamepadRadialDeadzone ? 1638 : 8000;` = 5% noise floor when radial is on) → normalize to `nx,ny` (`lvl.c:5851-5852`) → radial deadzone + rescale-from-edge (`lvl.c:5855-5867`, `dz = g_pcGamepadDeadzone`) → response curve (`lvl.c:5870-5877`: `f32 shaped = powf(mag, g_pcGamepadLookCurve);`) → fps factor + **truncation**:

  ```c
  /* lvl.c:5882-5884 */
  f32 fps = g_pcGamepadFpsScale ? g_GlobalTimerDelta : 1.0f;
  mdx += (int)(nx * gp_scale * fps);
  mdy += (int)(ny * gp_scale * fps);
  ```

- Aim divisor (`lvl.c:5845-5847`):

  ```c
  f32 gp_scale = g_CurrentPlayer->insightaimmode
               ? g_pcGamepadLookSpeed / 3.0f
               : g_pcGamepadLookSpeed;
  ```

- Defaults (`platform_sdl.c:1060-1065`, registered `:1786-1810`): `LookSpeed 8.0`, `LookCurve 1.5`, `Deadzone 0.15`, `RadialDeadzone 1`, `FpsScale 1`.

- **`g_GlobalTimerDelta` is an integer-valued float ≥ 1 during gameplay, never sub-1.0.** `lvl.c:2086` sets `g_ClockTimer = speedgraphframes` (whole 60 Hz ticks since the last sim frame, NATIVE_PORT-clamped to 1..4 at `lvl.c:2093-2101`), and `lvl.c:2107` does `g_GlobalTimerDelta = (f32) temp_v0;`. It is 0 only when paused/controls-locked. The sim is held at ≤60 Hz by `waitForNextFrame` regardless of display refresh, so at 120/240 Hz displays the delta is still 1.0 — **pass 1's "sub-1.0 delta widens the dead band at high fps" is wrong for this port.** The real fps interaction is the inverse: under slowdown (delta = 2..4) or `FrameCap=30` (delta = 2), `FpsScale=1` *lowers* the truncation threshold.

**Recomputed math** (delta = 1, pure-axis deflection `m` of full stick, defaults):

output px/frame = `((m − 0.15) / 0.85)^1.5 × gp_scale`. First nonzero pixel requires the product ≥ 1:

| Mode | gp_scale | shaped-mag threshold | **raw-stick threshold** |
|---|---|---|---|
| Aim (`insightaimmode`) | 8/3 ≈ 2.667 | 0.375^(1/1.5) = 0.520 | **m ≥ 0.15 + 0.520·0.85 ≈ 0.59** |
| Hip | 8.0 | 0.125^(1/1.5) = 0.250 | m ≥ 0.15 + 0.250·0.85 ≈ 0.36 |

So in aim mode the crosshair does not move until **~59% raw deflection** (pass 1's "~52%" was the post-deadzone-rescale magnitude, understating it). Hip-fire moves from ~36% but quantizes to 0..8 integer px/frame — e.g. a true 1.9 px/frame renders as 1 (up to ~50% mid-range speed loss), a permanent stair-step at any framerate. Mouse is unaffected.

**Fix (exact code).** Per-player, per-axis remainder accumulators. Player index is `local_player_number = get_cur_playernum()` (`lvl.c:5753`), range 0..3 — split-screen exists and `MAXCONTROLLERS = 4` (`src/platform/platform_os.h:227`), so size 4. There is no per-player PC-input struct to hang this on (the block uses function-local ints + globals), so file-scope statics in `lvl.c` are the right home.

```c
/* lvl.c, file scope near g_GlobalTimerDelta (~line 376) */
static f32 s_gpLookRemX[MAXCONTROLLERS];   /* fractional look px carried across frames */
static f32 s_gpLookRemY[MAXCONTROLLERS];

void pcResetGamepadLookRemainders(void)
{
    s32 i;
    for (i = 0; i < MAXCONTROLLERS; i++) {
        s_gpLookRemX[i] = 0.0f;
        s_gpLookRemY[i] = 0.0f;
    }
}
```

```c
/* lvl.c:5882-5884 — replace the truncating add */
f32 fps = g_pcGamepadFpsScale ? g_GlobalTimerDelta : 1.0f;
f32 fx = nx * gp_scale * fps + s_gpLookRemX[local_player_number];
f32 fy = ny * gp_scale * fps + s_gpLookRemY[local_player_number];
int ix = (int)fx;                              /* trunc toward zero, symmetric */
int iy = (int)fy;
s_gpLookRemX[local_player_number] = fx - (f32)ix;   /* |rem| < 1 px, always */
s_gpLookRemY[local_player_number] = fy - (f32)iy;
mdx += ix;
mdy += iy;
```

```c
/* lvl.c:5885 — the existing `if (grx != 0 || gry != 0) { ... }` gets an else: */
} else {
    /* Stick at rest (or zeroed by !live_look_allowed / g_freezeInput above,
     * lvl.c:5826-5834): drop the remainder so it can never drift the view. */
    s_gpLookRemX[local_player_number] = 0.0f;
    s_gpLookRemY[local_player_number] = 0.0f;
}
```

```c
/* lvl.c lvlStageLoad() (lvl.c:579; timer reinit at 610-613) — belt-and-braces
 * reset on level load / mode change: */
#ifdef NATIVE_PORT
    pcResetGamepadLookRemainders();
#endif
```

The release/freeze paths already zero `grx/gry` *before* the `if`, so the `else` branch clears remainders on pause, menus, demo playback, and stick release automatically. `g_pcGamepadFpsScale` interaction: the delta multiplier is folded into `fx` *before* accumulation, so the remainder is always in output-pixel units and stays correct when `g_GlobalTimerDelta` changes between frames (bounded < 1 px). An `insightaimmode` toggle carries at most 1 px of stale remainder — imperceptible; do not add complexity for it.

**Steps.**
1. Add the statics + reset function to `lvl.c`; call the reset from `lvlStageLoad`.
2. Replace the two truncating lines at `lvl.c:5883-5884`; add the `else` reset.
3. Build both backends; confirm the default no-pad path is byte-identical (with no pad connected `grx==gry==0` and the new code is exercised only through the else branch).

**Verification & acceptance.**
- Temporary trace, following the repo's `getenv` idiom (`GE007_TRACE_FRAME_TIMING`, `unk_0C0A70.c:75`):
  ```c
  if (getenv("GE007_TRACE_PAD_LOOK")) {
      fprintf(stderr, "[PAD_LOOK] p%d aim=%d nx=%.3f fx=%.3f ix=%d rem=%.3f dt=%.1f\n",
              local_player_number, g_CurrentPlayer->insightaimmode, nx, fx, ix,
              s_gpLookRemX[local_player_number], g_GlobalTimerDelta);
  }
  ```
  Repro pre-fix: `GE007_TRACE_PAD_LOOK=1 ./ge007 --level 21`, hold aim, deflect the right stick slowly — `ix` stays 0 until ~59% deflection.
- **Accept when:** (a) in aim mode the crosshair visibly creeps at just-above-deadzone deflection (~16-17% raw) — check at `Video.FrameCap=60` *and* `FrameCap=display` on a >60 Hz monitor (sim still ticks 60 Hz; verify delta stays 1.0 in the trace); (b) constant 50% deflection for 10 s yields total pixels within ±1 of `shaped·gp_scale·600` (no long-run loss); (c) with `FpsScale=1`, deg/sec at `FrameCap=30` matches `FrameCap=60` within noise; (d) releasing the stick and waiting 60 s produces **zero** further `ix/iy` (remainders cleared); (e) mouse-only play is byte-identical.

**Pitfalls.** Don't accumulate *pre*-scale stick values (breaks under `FpsScale`/aim-toggle). Don't use `floorf` for the split — `(int)` truncation is symmetric about zero and pairs with the subtraction; `floorf` would bias negative-axis look. Don't put the accumulators in `Player` (game struct layout is decomp-sensitive). Keep the arrays indexed by `local_player_number`, not pad handle — pad k drives player k by design (comment at `lvl.c:5814-5817`).

### 3.2 — MEDIUM — Multi-pad hot-unplug disables the *wrong* player's controller

**Background.** The platform layer keeps **stable pad slots**: `platformClosePadByInstance` (`platform_sdl.c:105-117`) frees only the unplugged slot, `platformOpenPad` fills the lowest free slot (`:74-90`), and every accessor reads `g_pads[k]` per slot. The game gates per-slot input on the `g_ConnectedControllers` bitmask (`src/joy.c:834+`: `(g_ConnectedControllers >> contpadnum & 1)`).

**Verified trace.** The bitmask is rebuilt from a **contiguous prefix count**, discarding slot identity:

```c
/* src/platform/stubs.c:564-569 */
static int pcConnectedPlayerCount(void) {
    int pads = platformGetPadCount();
    int count = pads > 1 ? pads : 1; /* keyboard/mouse guarantees >=1 */
    if (count > MAXCONTROLLERS) count = MAXCONTROLLERS;
    return count;
}
/* stubs.c:574-587 pcFillContStatus(data, count): slots < count = CONT_TYPE_NORMAL,
   rest = CONT_NO_RESPONSE_ERROR */
```

`osContGetQuery` (`stubs.c:615-618`) calls `pcFillContStatus(data, pcConnectedPlayerCount())`; `joyCheckStatus` (`src/joy.c:215-245`) converts errno per slot into the bitmask (`g_ConnectedControllers = slots;` at `:244`). Repro: pads in slots 0 and 1, unplug slot 0 mid-match → `platformGetPadCount()==1` → status = {slot0 present, slot1 absent} → P2's *still-plugged* pad is gated to zero while phantom slot 0 reads connected.

**Fix (exact code).** Build status from real per-slot occupancy.

```c
/* platform_sdl.c, next to platformGetPadCount (~:2143) — g_pads is static here */
int platformPadConnected(int k) {
    return (k >= 0 && k < PLATFORM_MAX_PADS) ? (g_pads[k].handle != NULL) : 0;
}
```

```c
/* stubs.c — replace pcConnectedPlayerCount()/pcFillContStatus(count) */
extern int platformPadConnected(int k);

/* Slot 0 is always present: keyboard/mouse drives P1 even with no pad. */
static u8 pcConnectedSlotMask(void) {
    u8 mask = 0x1;
    int i;
    for (i = 0; i < MAXCONTROLLERS; i++) {
        if (platformPadConnected(i)) mask |= (u8)(1u << i);
    }
    return mask;
}

static void pcFillContStatus(OSContStatus *data) {
    u8 mask = pcConnectedSlotMask();
    int i;
    if (!data) return;
    memset(data, 0, sizeof(OSContStatus) * MAXCONTROLLERS);
    for (i = 0; i < MAXCONTROLLERS; i++) {
        if (mask & (1u << i)) {
            data[i].type = CONT_TYPE_NORMAL;   /* status/errno already 0 */
        } else {
            data[i].errno = CONT_NO_RESPONSE_ERROR;
        }
    }
}
```

In `osContInit` (`stubs.c:589+`): `if (bitpattern) *bitpattern = pcConnectedSlotMask();` and `pcFillContStatus(data);`. In `osContGetQuery` (`stubs.c:615`): `pcFillContStatus(data);`. Delete `pcConnectedPlayerCount` (verified: its only callers are these two functions).

**Contiguous-count dependents (checked).** `joyGetControllerCount` (`src/joy.c:280-295`) returns the index of the **first absent slot** — pure prefix semantics. Its callers are front-end/menu gates only, not in-match input: `front.c:2063` (≥1 controller legal screen), `front.c:3897,4051,5528` and `watch.c:552,2333` (≥2 for MP options). After the fix, a *hole* (pads in slots 0 and 2 after a mid-session slot-1 unplug) makes menus count 2-not-3 while in-match slot gating stays correct per-slot — strictly better than today (today the hole silently disables the wrong live pad). New connects fill the lowest free slot, so holes self-heal on replug. No other caller depends on contiguity.

**Steps.** Add the accessor; replace the two stubs functions; update the two call sites; rebuild both backends.

**Verification & acceptance** (manual — SDL hot-unplug can't be scripted portably; use the existing connect/disconnect logging):
1. Connect two pads (console shows `[SDL] Gamepad connected (P1)` / `(P2)`), start split-screen (MP front-end gate must still open, i.e. `joyGetControllerCount() >= 2`).
2. Mid-match, unplug **pad 1** → console `[SDL] Gamepad disconnected (P1)`. **Accept when:** P2's pad keeps full input (pre-fix it dies), P1 falls back to keyboard/mouse.
3. Replug → `connected (P1)` (lowest free slot) → P1 pad input resumes.
4. Repeat unplugging **pad 2**: P1 pad unaffected.
5. Regression: single-pad and no-pad boots still report 1 controller on the legal screen, and `osContInit` bitpattern is `0x1`/`0x3` as before for the contiguous cases.

**Pitfalls.** Keep the `mask |= 0x1` keyboard guarantee — dropping it makes `joyGetControllerCount()` return 0 with no pad and trips the legal-screen gate. Don't return per-slot data from `platformGetPadCount()` callers expecting a count (used for the "all slots full" check at `platform_sdl.c:2381`). `g_pads` is `static`; add the accessor rather than externing the array.

### 3.3 — MEDIUM-LOW — Config edge cases (four fixes)

All four verified current. The registry lives in `src/platform/settings.c` (+ `settings.h` `Setting` struct at `:34-51`); ini parse/save in `src/platform/config_pc.c`.

**(a) `configSave()` drops env-overridden keys → user's saved value silently lost.**
Verified at `config_pc.c:347-360`: `saveEntry` returns early when `st->override_source == SETTING_OVERRIDE_ENV`. The comment claims "the saved config keeps the user's own (menu/file) value" — but *omitting* the key means the next plain launch gets the **default** (e.g. file `FovY=70` + one run with `GE007_FOV_Y=90` + clean quit → `FovY` gone → next launch 50). The `Setting` struct retains **no** pre-override copy (`settingsApplyEnvOverrides`, `settings.c:306-326`, writes straight through `configSetValue`).
**Fix — write back the file value, not the env value.** Snapshot before overriding:

```c
/* settings.h — add to struct Setting: */
    char env_saved_value[64];   /* pre-env-override value, for configSave round-trip */
```
```c
/* settings.c settingsApplyEnvOverrides(), before configSetValue(...): */
    settingsFormatCurrentValue(setting, setting->env_saved_value,
                               sizeof(setting->env_saved_value));
    /* new helper: mirror settingsFormatDefaultValue (settings.c:476) but read
       setting->ptr instead of setting->def — settingsPrintDump already has the
       per-type read logic to copy. */
```
```c
/* config_pc.c:355-360 — replace the early return: */
    const Setting *st = settingsFind(e->key);
    if (st != NULL && st->override_source == SETTING_OVERRIDE_ENV) {
        saveEntryMetadataComment(e, f);
        fprintf(f, "%s=%s\n", keyName, st->env_saved_value);
        return;
    }
```
*Rationale: an env override is documented as "this launch only" — the ini must round-trip byte-stable through an env-overridden session.*

**(b) `--reset-config` ordering.** Verified at `main_pc.c:777-785`: `settingsApplyEnvOverrides()` (:777) → `--config-override` loop (:778-781) → `if (resetConfig) settingsResetAllToDefaults();` (:783-785) → `--config-set` loop. **Fix:** move the `resetConfig` block *above* line 777 so precedence is file < reset < env < `--config-override` < `--config-set`.

**(c) Escape maps by mouse-grab state, not game state.** Verified at `platform_sdl.c:2429-2438` (drifted from pass 1's `:2422`): `g_mouseGrabbed` → `g_pcEscapePressed = 1` (START) else `2` (B); under `--no-input-grab`/`--background` gameplay Escape becomes B (can't pause). The consumer (`stubs.c:6250-6256`) already has the right state queries in scope: `pcNativeFrontendInputActive()` (`stubs.c:5791-5805`, true only on TITLE/front-end menus) and `checkGamePaused()` (used at `lvl.c:2080`). **Fix:** platform sets a single flag (keep the ungrab side effect); decide the button at the consumer:

```c
/* platform_sdl.c:2429 — set g_pcEscapePressed = 1 unconditionally; keep the
   SDL_SetRelativeMouseMode(SDL_FALSE)/g_mouseGrabbed=0 ungrab when grabbed. */

/* stubs.c:6250 — replace the 1/2 dispatch: */
    if (g_pcEscapePressed) {
        extern s32 checkGamePaused(void);
        if (!pcNativeFrontendInputActive() && !checkGamePaused()) {
            buttons |= START_BUTTON;    /* gameplay → pause */
        } else {
            buttons |= B_BUTTON;        /* front-end or open pause watch → back */
        }
        g_pcEscapePressed = 0;
    }
```

**(d) Enum keys accept arbitrary integers.** Verified at `config_pc.c:199-212`: `CFG_ENUM` token match, then unconditional `*(s32 *)e->ptr = (s32)strtol(val, NULL, 0);` fallback. Registered enums and valid sets (tables at `platform_sdl.c:242-308`):

| Key | Tokens → values |
|---|---|
| `Video.WindowMode` | windowed=0, borderless=1, exclusive=2 |
| `Video.VSync` | off=0, on=1, adaptive=2 |
| `Video.FrameCap` | 30=30, 60=60, display=0 |
| `Video.MSAA` | 0, 2, 4, 8 |
| `Video.RetroFilter` | auto=0, off=1, on=2 |
| `Video.SsaoMode` | planar=1, hemisphere=2 (numeric values load-bearing) |

**Fix** — validate the numeric fallback against the option table:
```c
if (!found) {
    s32 v = (s32)strtol(val, NULL, 0);
    for (s32 i = 0; i < e->enum_count; i++) {
        if (e->enum_options[i].value == v) { found = 1; break; }
    }
    if (found) *(s32 *)e->ptr = v;
    else fprintf(stderr, "[CONFIG] %s: invalid value '%s' ignored (kept %d)\n",
                 e->key, val, *(s32 *)e->ptr);
}
```
*Rationale: `Video.MSAA=3` / `Video.VSync=17` currently land raw and get re-saved; the numeric fallback stays available for every enum (all legal numerics are in the tables), so `SsaoMode=2` keeps working.*

**Verification & acceptance (all four).** The ctest harness exists (~20 `port_*` guard tests + `settings_menu_model`; run `ctest` in the build dir) — add a guard script alongside them, plus these manual round-trips:
- (a) ini with `FovY=70`; run `GE007_FOV_Y=90 ./ge007 --level 21`, quit cleanly; **accept:** `grep FovY ge007.ini` → `FovY=70` still present (pre-fix: key absent).
- (b) `./ge007 --reset-config --config-override Video.FovY=80 --dump-config` → FovY shows 80, override source `cli`.
- (c) launch with `--no-input-grab`, enter a level, press Escape → pause watch opens; Escape again → backs out (B); on TITLE menus Escape still acts as B.
- (d) `./ge007 --config-set Video.MSAA=3` → stderr warning, `--dump-config` shows MSAA unchanged; `Video.SsaoMode=2` still accepted.

**Pitfalls.** (a) Size `env_saved_value` ≥ the longest formatted value — `Video.TexturePack` is a path; either skip CFG_STRING snapshots (strings have their own capacity) or bump to `char[256]`. (b) Keep `--faithful*` read-only-session semantics intact (`configSetSaveSuppressed`, `main_pc.c:770`) — the reorder must not move those preset applications. (d) Membership-check enums, don't range-clamp — `FrameCap` values (0/30/60) are not a contiguous range.

### 3.4 — INFO — Timing landmines (correct today; guidance if you ever touch them)

- **`osGetTime` wraps every ~91.6 s.** `stubs.c:263`: `u64 osGetTime(void) { return (u64)osGetCount(); }`, where `osGetCount()` returns a `u32` at 46.875 MHz (2³² / 46 875 000 ≈ 91.6 s). All current consumers subtract. **If you add a caller:** only ever compute elapsed time as wrap-safe unsigned subtraction in `u32` — `u32 elapsed = (u32)osGetCount() - start;` — exactly as `waitForNextFrame` does (`unk_0C0A70.c:201`); never compare absolute counts (`now > deadline` breaks at wrap) and never widen to `u64` expecting monotonicity.
- **`Video.FrameCap=display` semantics + busy-spin.** `platformFrameCapPeriodMs` (`platform_sdl.c:1494-1508`): `display` with vsync **off** returns a 60 fps period (misnamed but safe); with vsync **on** it returns 0 (uncapped pacer) and on a >60 Hz display the 60 Hz *sim* lock is then held solely by `waitForNextFrame`'s hot loop (`unk_0C0A70.c:199-213`) — up to ~8 ms/frame of pegged core. Thermal, not correctness; at the default `FrameCap=60` the deadline pacer makes this loop a no-op. **If you ever replace the spin:** copy the deadline pacer's sleep shape from `platformFrameSync` (`platform_sdl.c:2583`, pacing block ~2617-2648): coarse `SDL_Delay(rem_ms - 2.0)` while >2.5 ms remain, then `SDL_Delay(0)` yield-spin the tail against the precise counter. Mind the two NATIVE_PORT hooks already inside the loop (`pcStableDeterministicCountEnabled` / `pcAdvanceDeterministicCountForFrame` — deterministic replay depends on iteration behavior; gate any sleep so `--deterministic` runs are unchanged, and re-run the R3 sim-hash gate).

#### Audit-pass corrections (§3)

1. **§3.1 severity math understated at default, and the fps claim is backwards.** The "~52%" threshold was the *post-deadzone-rescaled* magnitude; from the raw stick the aim-mode dead band is **~59%** (hip ~36%). `g_GlobalTimerDelta` is an integer tick count clamped 1..4 and the sim never runs above 60 Hz, so there is **no sub-1.0 delta and no "widens with framerate" behavior** — the dead band is a constant steady-state defect; slowdown actually *shrinks* it under `FpsScale=1`. The finding stays HIGH on the corrected grounds (felt quality at all framerates).
2. §3.1 anchors: truncation `5882-5884`; aim divisor `5845-5847`; curve `powf` at `5873`; radial deadzone `5855-5867`; SDL-side pre-clip `platform_sdl.c:2201` (missing from pass 1).
3. §3.2 anchors hold; functions actually span `stubs.c:564-569` and `574-587`; `osContGetQuery` at 615-618; `joy.c:215-245` (function opens at 215); the file is `src/joy.c`. Added: the in-match per-slot gate is `joy.c:834+`, and `joyGetControllerCount` (`joy.c:280-295`) is the contiguous-prefix dependent, with callers in `front.c`/`watch.c` only (menu gates).
4. §3.3 all four verified; Escape anchor drifted `:2422` → `:2429`. Pass 1 didn't note that the `Setting` struct retains no pre-override value — the fix requires adding storage (now specified).
5. §3.4: loop is `unk_0C0A70.c:199-213`; frame-cap fn `platform_sdl.c:1494-1508`. Added the concrete deadline-pacer reference: `platformFrameSync`, `platform_sdl.c:2583/2617-2648`.
6. Context: gamepad look confirmed to feed the same `mdx/mdy` integers as `platformGetMouseDelta` (synthetic mouse). The perfect_dark port's exact fractional-accumulation shape was not confirmable from search results — treat "float end-to-end" as the reference pattern and verify against its `input.c` before citing it in code comments.

---

## 4. Memory safety / UB (game code, 64-bit host)

> **STATUS 2026-07-04 — prop.c:3331 prototype IMPLEMENTED + VERIFIED — LANDED to main as merge `eeed4d2` (item commit `b7c538e`).** Added `s32 sub_GAME_7F0B9E04(coord3d *, coord3d *);` to prop.c's forward-declaration block (matches `bg.c:18963`). Verified: `ub_sweep.sh` now PASSES all 13 focus files (prop.c truncation-class clean — it was the lone FAIL); regular build + ctest 22/22 green. **Caveat found:** a *full* `-DPORT_STRICT=ON` build still fails, but on a **pre-existing, out-of-scope** `gun.c` `-Werror=incompatible-pointer-types` (`sndPlaySfx`/`g_musicSfxBufferPtr` — `ALBank*` vs `ALBankAlt_s*`, 13 hits) — the very class §4 below labels "non-gating." So this fix removes the prop.c implicit-declaration (item-2's purpose, verified by the sweep) but does not by itself make a whole-tree PORT_STRICT build pass.

**Headline: the compiled game paths are clean of the 64-bit truncation class — and the claim is now re-runnable (see the sweep below), with one new non-truncation finding.**

**Why this class exists at all (N64 context):** on the N64, all game pointers live in KSEG0 (`0x80000000–0x9FFFFFFF`), so every address fits in 32 bits and the original code freely stores pointers in `s32`, does `ptr + 0xCC` arithmetic through integer fields, and indexes "rwdata" scratch buffers at a hard 4-byte stride (one N64 pointer = one slot). Every decomp-based port hits this class on 64-bit hosts — the sm64 and perfect_dark PC ports solved it the same way this repo does: widen the address-carrying typedefs to `uintptr_t`/`intptr_t` and over-allocate the 4-byte-stride buffers.

The production build *suppresses* the warnings that catch this class (`-Wno-implicit-function-declaration`, `-Wno-int-conversion`, `-Wno-incompatible-pointer-types` — verified in `build/compile_commands.json`; note `-Wno-int-to-pointer-cast` is **not** actually in the flag set, correcting pass 1). The claim was verified by re-running `-fsyntax-only` on the 13 focus files with those suppressions removed.

### Reproducible warning sweep

Prereq: a configured build with a compile database (`cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`). The script replays each focus file's *exact* production compile command, strips the truncation-class suppressions, turns the diagnostics explicitly on, and gates on the truncation class only:

```sh
#!/bin/sh
# ub_sweep.sh — §4 truncation-class warning sweep. Usage: sh ub_sweep.sh /path/to/mgb64
set -u
REPO="${1:-$PWD}"; DB="$REPO/build/compile_commands.json"
[ -f "$DB" ] || { echo "missing $DB — run: cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"; exit 2; }
FILES="src/game/chrobjhandler.c src/game/gun.c src/game/bondview.c src/game/bg.c \
src/game/chrprop.c src/game/prop.c src/game/explosions.c src/game/chrlv.c \
src/game/model.c src/game/stan.c src/mema.c src/memp.c src/platform/model_convert.c"
FAIL=0
for f in $FILES; do
  CMD=$(python3 - "$DB" "$REPO/$f" <<'EOF'
import json,sys,shlex
db=json.load(open(sys.argv[1]))
for e in db:
    if e['file']==sys.argv[2]:
        args=shlex.split(e.get('command') or ' '.join(e['arguments']))
        out=[];skip=False
        for a in args:
            if skip: skip=False; continue
            if a=='-o': skip=True; continue
            if a=='-c': continue
            if a in ('-Wno-implicit-function-declaration','-Wno-int-conversion',
                     '-Wno-incompatible-pointer-types','-Wno-int-to-pointer-cast'): continue
            out.append(a)
        print(' '.join(shlex.quote(a) for a in out)); break
EOF
)
  [ -n "$CMD" ] || { echo "SKIP (not in compile db): $f"; continue; }
  OUT=$(eval "$CMD -fsyntax-only -Wint-conversion -Wint-to-pointer-cast -Wpointer-to-int-cast -Wformat -Wimplicit-function-declaration" 2>&1)
  BAD=$(printf '%s\n' "$OUT" | grep -E 'int-conversion|int-to-pointer-cast|pointer-to-int-cast|implicit-function-declaration|\-Wformat' || true)
  if [ -n "$BAD" ]; then echo "FAIL $f"; printf '%s\n' "$BAD"; FAIL=1
  else
    N=$(printf '%s\n' "$OUT" | grep -c 'incompatible-pointer-types' || true)
    echo "PASS $f (truncation-class clean; $N non-gating incompatible-pointer-type notes)"
  fi
done
exit $FAIL
```

**Pass criteria:** zero diagnostics in the truncation class. `-Wincompatible-pointer-types` hits are reported but non-gating — same-width pointer-type confusion, not width truncation; the compiled build deliberately tolerates them.

**Fresh run result (main @ `d894648`, 2026-07-03):** 12 of 13 files PASS truncation-class clean. **One FAIL:**

```
src/game/prop.c:3331: call to undeclared function 'sub_GAME_7F0B9E04' [-Wimplicit-function-declaration]
```

Triage: introduced by commit `429391c` ("fix: restore native tinted glass visibility") — *after* the pass-1 sweep, so pass 1's zero-warning claim was true when written but is stale on today's main. The callee is defined as `s32 sub_GAME_7F0B9E04(coord3d *, coord3d *)` at `src/game/bg.c:18963`, and the caller assigns the result to an `s32` — the implicit declaration's assumed `int` return matches the real ABI, so this is **not** a live truncation bug. But it is exactly the pattern that produced the historical `dynAllocateMatrix` pointer-truncation crash, and it **breaks `-DPORT_STRICT=ON` builds** (`CMakeLists.txt:503-513` appends `-Werror=implicit-function-declaration`). **Action (trivial):** add the prototype where `prop.c` can see it. A second, currently-not-compiled call sits at `prop.c:4131`.

Standing gate: `SANITIZE` and `PORT_STRICT` CMake options already exist; a periodic `cmake -B build-strict -DPORT_STRICT=ON` build is the cheapest way to keep this class extinct.

### 4.1 — RESOLVED — "Bullet-spark UB" (do not re-open)

Anchors re-verified. The historical bug in `gun.c` `sub_GAME_7F05EC1C` is **dead code** in the `#else` branch — `gun.c:4182` passes `arg0->unk6C + 0xCC` (a real pointer read through an integer-typed field plus a struct offset, which is why the naive u32 "fix" was wrong). The **compiled** path (`#ifdef NONMATCHING` + `#ifdef PORT_FIXME_STUBS`, both defined in `CMakeLists.txt:309-311`; block at `gun.c:4011`, closed at `:4190`) is the correctly-ported version: typed `Projectile *projectile` with NULL guards, passing `&projectile->unkCC` (`gun.c:4092`) — a legitimate member address. No live UB.

### 4.2 — Allocators (`mema.c` / `memp.c`) — CLEAN

Anchors re-verified, all exact. The prime truncation risk (N64 stored heap addresses in `s32`) was fixed at the typedef level: `src/mema.h:7` `typedef uintptr_t mema_addr_t;` under `NATIVE_PORT`; `src/memp.h:11-18` `MemoryPool { intptr_t start, pos, end, prevpos; }`; `mema.c:317` `return (void*)addr;` round-trips full width; `memp.c:155-224` computes in `intptr_t` throughout. Room-data frees route through host `free()` on `malloc`'d buffers (`bg.c:10321-10323`); the lossy `memaFree` size-pun at `bg.c:10325` is N64-only dead code.

### 4.3 — `model.c` vertex-stride / rwdata — CLEAN (engineered, not accidental)

Anchors re-verified. Vtx packing uses a hardcoded 16-byte stride via `modelWriteVertexToGBI` and friends (`model.c:661-738`), independent of host struct padding; `dynAllocate` is properly prototyped (`model.c:660`). The general flat rwdata buffer keeps N64's 4-byte indexing (`modelGetNodeRwData`, `model.c:2325`) but is **over-allocated 2×** (`model.c:942` — `rw_size = numRecords * 4 * 2`, 16-aligned, `calloc`'d); bool-valued slots are read as `u8` (LSB only) to ignore stride-mismatch garbage (`model.c:5504-5510`, `5535-5541`).

### 4.4 — LOW — Fixed-size weapon rwdata buffers without headroom (**re-scoped**; concrete verify task below)

**Correction to the original finding.** The previously-cited anchors are *dead code*: `gun.c:6673/6685` (`hp+0x318` inline rwdata) sit inside the `#else` of the `#ifdef NATIVE_PORT` at `gun.c:6649` (the `#else` spans `6658-6953`), so the inline buffer is never used on PC. The **compiled** first-person path is `portPrepareFirstPersonWeaponModel` (`gun.c:3294`, called at `:6651`) → `portEnsureFirstPersonWeaponModel` (`gun.c:2967`), which heap-allocates weapon rwdata with *more* headroom than the general path:

```c
modelCalculateRwDataLen(itemheader);
rwbytes = ((size_t)itemheader->numRecords * 8u) + 256u;   /* gun.c:2989-2990: 2x + 256 slack */
rwbuf = (u8 *)calloc(1, rwbytes);
```

(Note `modelCalculateRwDataLen` (`model.c:14139`) is `void` — it *sets* `itemheader->numRecords` in 4-byte units; it does not return a length as pass 1's "Verify" implied.)

**The class is still live, just elsewhere.** Two *compiled* call sites hand `modelInit` a fixed 32-slot stack buffer with **no** headroom and **no** bound check against `numRecords`:

- `gun.c:15570` — `u32 rwdata[32];` in `set_enviro_fog_for_items_in_solo_watch_menu` (solo watch-menu weapon display), `modelInit(&model, itemheader, rwdata)` at `gun.c:15628`;
- `gun.c:16171` — `u32 rwdata[32];` in `sub_GAME_7F06359C`, `modelInit(&model, itemheader, rwdata)` at `gun.c:16261`.

Both compile (`NONMATCHING` defined). `modelGetNodeRwData` indexes these at 4-byte stride: any weapon model with `numRecords > 32` overruns the stack frame. In practice weapon rwdata holds small scalars and weapon trees are tiny — *appears* benign, hence LOW.

**Concrete task (junior-executable):**

1. **Patch sketch** — one-time debug check, repo idiom (env-gated, default-off). In *both* functions, immediately after `modelCalculateRwDataLen(itemheader);`:

   ```c
   #ifdef NATIVE_PORT
       /* GE007_DEBUG_RWDATA_BOUNDS=1: prove no weapon model outgrows the
        * fixed 32-slot (128 B, 4-byte-stride) stack rwdata buffer. */
       if (getenv("GE007_DEBUG_RWDATA_BOUNDS") != NULL &&
           itemheader->numRecords > (s32)(sizeof(rwdata) / 4)) {
           fprintf(stderr, "[rwdata-bounds] gun.c watch-menu overflow: header=%p "
                   "numRecords=%d > %zu slots\n",
                   (void *)itemheader, itemheader->numRecords, sizeof(rwdata) / 4);
           abort();
       }
   #endif
   ```

2. **Build with sanitizers:** `cmake -B build-asan -DSANITIZE=ON && cmake --build build-asan -j`.
3. **Runtime protocol — cycle every weapon through the watch menu.** Use the deterministic input harness (`src/platform/stubs.c`; scripted-equip hooks require `--deterministic`):
   - `GE007_AUTO_EQUIP_ITEM_SCRIPT="frame:item,frame:item,..."` (`stubs.c:4844`; equips via `currentPlayerEquipWeaponWrapper`) — generate one pair every ~30 frames over the full `ITEM_IDS` range (`src/bondconstants.h:3342-3434`).
   - The two vulnerable functions are watch-menu render paths, so also drive the watch open: `GE007_AUTO_OPEN_WATCH` + `GE007_AUTO_WATCH_PAGE`/`GE007_AUTO_WATCH_PAGE_DELAY` (page to the inventory page so each equipped weapon actually renders there). Manual alternative: `CHEAT_ALLGUNS` (`src/game/cheat_buttons.c:686/1025`) or the debug-menu all-guns toggle (`debugmenu_handler.c:633`) and thumb through the watch inventory.
   - `GE007_DEBUG_RWDATA_BOUNDS=1 ./build-asan/ge007 --level 33 --deterministic ...`, repeat on a large weapon-set level with the all-guns cheat.
4. **Pass criteria:** clean exit — no `[rwdata-bounds]` abort, no ASan `stack-buffer-overflow` in either function, across the full item sweep. If it trips: switch the two buffers to `u32 rwdata[64]` (or route through the `portEnsureFirstPersonWeaponModel`-style sized allocation) and re-run.

### 4.5 — INFO — Arena-reset lifetime invariant (design note for netplay / save-state authors)

**The invariant:** the `mema`/`memp` stage arenas are bulk-reset on every stage (re)load — `src/boss.c:481-486` allocates a fresh stage bank and calls `memaReset(...)` — so **no pointer into stage-arena memory (props, chrs, room data, model rwdata, DL buffers) is valid across a stage transition**. There is no per-object free/refcount; validity is purely epochal.

**Where resets happen:** stage load in `boss.c:481-486` (followed by `reset_play_data_ptrs()`); per-room teardown additionally NULLs every tracker on the PC path — `bg.c:10340-10345` frees and NULLs `s_pc_room_vtx_bufs / s_pc_room_dl_bufs / s_pc_room_sec_bufs[roomID]` and the room's mapping-info pointers (preloader buffers NULLed in `lvl.c` on transition, per the comment at `bg.c:10337-10339`).

**What a violation looks like:** a pointer captured before the transition (in a global, a static, a queued event, a serialized blob) dereferenced after it. Because the arena is immediately re-used, this usually presents not as a crash but as *silent aliasing* — the stale pointer reads/writes some unrelated new object (the exact failure mode the shoot-out-lights room-teardown comment at `bg.c:10331-10335` guards against). Under ASan it presents as heap-use-after-free only for the `malloc`-backed PC buffers; arena-internal staleness is invisible to ASan.

**Checklist for netplay / save-state / replay code (grep-able):**
- Any pointer stored across a stage transition must be re-derived after load — serialize *indices/IDs* (prop number, chrnum, room ID, pad ID, item ID), never addresses.
- Audit for pointers persisted in: save-state snapshots, netplay input/event queues, deferred callbacks, and any `static`/global caches — grep for `static.*\*` and cross-frame caches in files touching `PropRecord`, `ChrRecord`, `RoomInfo`, `Model`.
- On restore, rebuild via the same creation order the stage-load path uses (the sim-hash lockstep inventory already assumes this determinism).
- Any new PC-side `malloc` tied to a room/stage must register with an existing teardown site (`bg.c:10340-10345` pattern) — never freed "later".

No cross-stage survivor was found in the focus files; this stays INFO.

*Verified clean (compiled paths): `chrobjhandler.c`, `gun.c`, `bondview.c`, `bg.c`, `chrprop.c`, `prop.c`†, `explosions.c`, `chrlv.c`, `model.c`, `stan.c`, `mema.c`, `memp.c`, `model_convert.c`.*
*† except the new `prop.c:3331` missing prototype (benign, s32-returning; see headline) — fix and re-run the sweep.*

#### Audit-pass corrections (§4)

1. **Headline "zero warnings" is stale on today's main.** Fresh sweep finds one `-Wimplicit-function-declaration` at `prop.c:3331` (`sub_GAME_7F0B9E04`), introduced post-audit by `429391c`. Benign for truncation but breaks `PORT_STRICT`. Fix = add the prototype. All other 12 files reproduce clean.
2. **`-Wno-int-to-pointer-cast` is not in the production flag set** — three suppressions exist, not four.
3. **4.4's anchors were dead code** (`gun.c:6673/6685` under N64-only `#else`); the live first-person weapon rwdata is heap-allocated with 2×+256 B headroom — *cleaner* than pass 1 feared. Re-scoped to the two genuinely compiled fixed buffers at `gun.c:15570`/`:16171`, same severity.
4. **`modelCalculateRwDataLen` is `void`**; the quantity to bound is `itemheader->numRecords` vs the buffer's slot count.
5. **The "inline span `[0x318 .. 0x870)`" was wrong** — `0x870` is the base offset of the hand array inside the player struct (`gun.c:4728/14747`), not the buffer's end. Moot (dead code), recorded so nobody re-derives the wrong layout.
6. Minor: Vtx-packing anchor is `model.c:661-738`; bool-LSB reads `model.c:5504-5510`/`5535-5541`. All other anchors verified exact.

---
## 5. Renderer correctness (fast3d / GL / Metal)

> **File identity (read this first):** the linked interpreter is **`src/platform/fast3d/gfx_pc.c`** (24,630 lines) — `src/platform/gfx_pc.c` (2,804 lines, containing its own dead `gfx_clear_n64_dl_regions`/`gfx_clear_texture_cache`) is the legacy file, **excluded from the build** by `CMakeLists.txt:328` (`list(FILTER PLATFORM_SOURCES EXCLUDE REGEX "gfx_pc\.c$")`). All line refs below are into the fast3d file unless stated. Do not patch the wrong file.

**Overall verdict: unusually hardened — re-confirmed.** Spot-checks of the "verified clean" claims all held: the LRU texture cache evicts flush-first (`tex_cache_evict_lru` `:2759`: `gfx_flush()` `:2763` → invalidate `:2764` → `delete_texture` `:2772` — the file's gold-standard ordering and the model for fix 5.2); fog is faithful `clip_z/clip_w` with saturation + NaN guard (`gfx_fog_coord_from_clip` `:13586-13602`), clamped `:16815-16816`, stored in a separate per-vertex `fog` field so shade alpha is preserved; garbage-opcode posture is `ABORT_N64_DL()` + `VALIDATE_ADDR` macros (`:22488-22516`) rate-limiting `[GFX-BAD]` to the first 5; tri indices are bounds-checked in `gfx_sp_tri1` (`:19256-19262`).

The findings below are gaps in *its own* defense posture — mostly reachable only via malformed/mis-decoded DL data (which this codebase treats as a first-class threat) — plus texture-lifetime gaps in eviction and level-transition teardown.

**Microcode context (why these limits look odd if you know sm64ex):** upstream fast3d ports use `MAX_VERTICES 64` because F3DZEX loads up to 32 verts and forks padded the buffer. GoldenEye does **not** use F3DZEX — Rare shipped an F3DEX-era variant with a **16-slot vertex buffer**, encoded directly at `gfx_pc.c:209` (`#define MAX_VERTICES 16  /* Rare's microcode uses 16, not 64 */`). The actual array is `struct LoadedVertex loaded_vertices[MAX_VERTICES + 4]` (`:3415`) = **20 slots**; the extra 4 (indices 16–19) are scratch verts for texrect quads (`:20794-20797`, drawn via `gfx_sp_tri1(MAX_VERTICES + 0, ...)` at `:20824`), which is why the tri-path bound is `MAX_VERTICES + 4`, not `MAX_VERTICES`. **Do not "fix" a G_VTX load by allowing writes into slots 16–19** — those are texrect scratch, never legitimate G_VTX destinations. Also: the file runs **three** interpreters — legacy (`case G_VTX:` at `:815`), the PC-native DL interpreter `gfx_run_dl_pc` (`:21791`), and the N64-binary big-endian interpreter (`:22612`). Finding 5.1 is about the PC one; the N64 one is the already-guarded sibling.

### 5.1 — MEDIUM — PC-path `G_VTX` has no `dest_idx + num_verts` bound → OOB write past the whole `rsp` global

**Background.** A `G_VTX` command names a destination slot (`dest_idx`, 0–15) and a count (`num_verts`, 1–16). `15 + 16 = 31 > 20`, so a hostile/mis-decoded command commands a write past the end of `rsp.loaded_vertices`. The N64-binary interpreter rejects exactly this; the PC-native interpreter — which executes DLs built at runtime by port code, i.e. the path most exposed to a port bug emitting a bad param byte — does not.

**Verified trace.**
- PC interpreter `case G_VTX:` at `:21791`. Parse at `:21796-21798`:
  ```c
  uint32_t param = C0(16, 8);
  ...
  int num_verts = (param >> 4) + 1;   /* 1..16 */
  int dest_idx  = param & 0xF;        /* 0..15 */
  ```
  A `len`-derived override at `:21802-21808` can also set `num_verts` up to 16. NULL address is rejected (`:21801`), but there is **no** `dest_idx + num_verts` check before either `gfx_sp_vertex(...)` call (`:21834` N64-decode sub-path, `:21841` PC-native sub-path). Both sub-paths are affected.
- `gfx_sp_vertex` (`:16340`) has no internal bound: the loop at `:16433` writes `rsp.loaded_vertices[dest_index]` unchecked.
- `loaded_vertices` is the **last member of `rsp`** (`:3415`) — so `dest_idx=15, num=16` writes slots 20–30, up to ~1.7 KB **past the whole `rsp` object**, corrupting whatever file globals the linker placed after it. (Pass 1 said "adjacent `rsp` globals" — it's worse.)
- The trace path has the matching OOB read at `:21845-21846` (`vlast = &rsp.loaded_vertices[dest_idx + num_verts - 1]`).
- The sibling N64-binary interpreter's guard, `:22718-22731` — the idiom to copy:
  ```c
  if (num_verts <= 0 || num_verts > 16 ||
      dest_idx < 0 || dest_idx >= MAX_VERTICES ||
      dest_idx + num_verts > MAX_VERTICES) {
      g_bad_cmd_count++;
      g_resolve_failures.vtx_fail++;
      if (g_resolve_failures.vtx_fail <= 5) {
          fprintf(stderr,
                  "[GFX-BAD] G_VTX count: w0=0x%08X w1=0x%08X count=%d dest=%d frame=%d\n",
                  w0, w1, num_verts, dest_idx, g_frame_count_diag);
          fflush(stderr);
      }
      data += 8;
      continue;
  }
  ```

**Fix (exact code).** Insert after the `num_verts_from_len` override block (after `:21808`'s closing `}`), before the `gfx_addr_is_pc_native_vertex_data` check at `:21812`:

```c
                if (num_verts <= 0 || num_verts > MAX_VERTICES ||
                    dest_idx + num_verts > MAX_VERTICES) {
                    g_bad_cmd_count++;
                    g_resolve_failures.vtx_fail++;
                    if (g_resolve_failures.vtx_fail <= 5) {
                        fprintf(stderr,
                                "[GFX-BAD] G_VTX count (PC): w0=0x%08X w1=0x%08X count=%d dest=%d frame=%d\n",
                                cmd->words.w0, (uint32_t)cmd->words.w1,
                                num_verts, dest_idx, g_frame_count_diag);
                        fflush(stderr);
                    }
                    break;   /* PC interpreter is a switch over Gfx*, not a byte cursor */
                }
```

Notes: `dest_idx` is `param & 0xF` so it can't be negative or ≥16 here — the compound check mirrors the sibling for grep-ability. Use `break` (this switch advances via the `Gfx *cmd` loop), **not** the sibling's `data += 8; continue;`.

**Steps.** Add the guard; rebuild; confirm zero `[GFX-BAD] G_VTX count (PC)` lines across a normal level boot (the guard must never fire on legitimate content).

**Verification & acceptance.**
- **There is no crafted-DL unit harness** (`gfx_run_dl_pc` and `gfx_sp_vertex` are `static`; the only C unit tests are `test_sim_state_hash`/`test_room_normals`). Two options:
  - *Cheap (recommended):* temporary self-test — inside `case G_VTX`, under `#ifdef GFX_VTX_GUARD_SELFTEST`, force `dest_idx = 15; num_verts = 16;` once; build with `-DSANITIZE=ON`, run `tools/asan_smoke.sh --no-build --binary build-asan/ge007`. **Pre-fix**: ASan `global-buffer-overflow` on `rsp`. **Post-fix**: clean run + one `[GFX-BAD] G_VTX count (PC)` line.
  - *Durable:* a `GFX_DL_UNIT_TEST`-gated test TU mirroring the `GFX_ROOM_NORMALS_UNIT_TEST` pattern (`CMakeLists.txt:207-215`) feeding a 2-command DL `{G_VTX param=0xFF, G_ENDDL}`. Only worth it if more DL findings accumulate.
- **Acceptance:** `tools/asan_smoke.sh --gate` clean; no `[GFX-BAD] G_VTX count (PC)` in `tools/perf_census.sh` output across all 20 levels (proves no false rejection).

**Pitfalls.** Don't clamp `num_verts` down instead of rejecting — a partial load followed by tris referencing unloaded slots draws stale geometry; the sibling rejects wholesale, match it. Don't bound against `MAX_VERTICES + 4` (see microcode context). The trace read at `:21845-21846` becomes safe once the guard is upstream — no second patch. The N64-decode sub-path's `Vtx temp_verts[16]` (`:21818`) already self-limits *decoding*; the OOB was only ever on the *write* side via `dest_idx`.

### 5.2 — MEDIUM-LOW (downgraded) — Settex-cache eviction deletes/unbinds a GL texture without flushing buffered triangles

**Background.** The port's custom `G_SETTEX` op keys uploaded textures by `texturenum` (12-bit, `:21131` `w1 & 0xFFF`; game-wide ID space `NUM_TEXTURES 0xBB9` = 3001, `src/game/othermodemicrocode.h:9`) in a 2048-entry cache (`SETTEX_CACHE_SIZE 2048`, `:941`). Triangles are recorded CPU-side into `buf_vbo` and only hit the GPU at the next `gfx_flush()`; deleting/unbinding a texture that pending triangles were recorded against makes them draw black/wrong when the flush finally happens.

**Verified trace.** Eviction branch, `:21588-21598`:

```c
        } else {
            /* Cache full — evict oldest entry (slot 0), shift down */
            if (settex_cache[0].valid && settex_cache[0].gl_tex_id != 0) {
                gfx_invalidate_settex_gl_texture(settex_cache[0].gl_tex_id);
                gfx_rapi->delete_texture(settex_cache[0].gl_tex_id);
            }
            free(settex_cache[0].rgba);
            memmove(&settex_cache[0], &settex_cache[1],
                    (SETTEX_CACHE_SIZE - 1) * sizeof(settex_cache[0]));
            slot = SETTEX_CACHE_SIZE - 1;
        }
```

`gfx_invalidate_settex_gl_texture` (`:4364-4386`) already does the *state* half correctly — force-reloads any unit bound to the dying GL name and clears the live settex globals. The **only** missing piece is `gfx_flush()` before it, which the LRU cache does at `:2763`. (Upstream-fast3d note: sm64ex-era forks historically overwrote pool textures without flushing — a known black-texture source; this repo's LRU path fixed that; this branch is the one spot that didn't inherit the ordering.)

**Reachability (correction to pass 1):** the settex cache is wiped at **every stage load** (`gfx_clear_n64_dl_regions` ← `src/game/lvl.c:588`) and only fills by *unique* `texturenum`; overflow needs **>2048 distinct settex textures uploaded between two stage loads** (game total = 3001 IDs). No normal single-level+menu session approaches that. Also, the "historically destabilized the GL-on-Metal driver" comment pass 1 cited belongs to the **LRU** cache's delete-vs-recycle choice (`:2768-2770`), not this branch. Real bug, one-line fix, demoted from MEDIUM.

**Fix (exact code).** At `:21589`, first statement inside the `else`:

```c
        } else {
            /* Cache full — evict oldest entry (slot 0), shift down.
             * Flush first: buf_vbo may hold triangles recorded against the
             * evicted texture (same ordering as tex_cache_evict_lru). */
            gfx_flush();
            if (settex_cache[0].valid && settex_cache[0].gl_tex_id != 0) {
```

`gfx_flush` is forward-declared at `:302` and is a no-op when `buf_vbo_len == 0`.

**Verification & acceptance.**
- Deterministic repro: temporarily set `#define SETTEX_CACHE_SIZE 8` (`:941`), run a texture-diverse level headless with `GE007_TRACE_SETTEX=1` (emits `[SETTEX-*]` CACHE-HIT/MISS/LOAD lines, capped 96/frame; emitter `gfx_log_settex_event` `:11443`) + `--screenshot-frame 120`. Eviction fires constantly; **pre-fix** you can catch black/mistextured surfaces; **post-fix** the frame matches a `SETTEX_CACHE_SIZE 2048` control capture.
- Measure real-world fill: grep a full-level trace for distinct `texnum=` values on `[SETTEX-LOAD]` lines — expect hundreds, nowhere near 2048; record the number in the PR to justify the severity downgrade.
- Acceptance: shrunken-cache screenshot == control, ASan smoke clean, **and `SETTEX_CACHE_SIZE` restored to 2048 before merging**.

**Pitfalls.** Restore the cache size — shipping `8` would thrash uploads every frame. Don't move the flush inside `gfx_invalidate_settex_gl_texture` — that helper is also called per-command in hot paths where a forced flush would fragment batches; the LRU precedent puts the flush at the *eviction site*. The `memmove` shift-down makes this an O(n) FIFO, not LRU — cosmetically fine; don't "fix" it here (it interacts with §7.4's direct-index table).

### 5.3 — LOW — Level-transition teardown leaves settex/bound-texture state dangling

**Background.** Two public teardown functions delete every settex GL texture but leave the *live-binding* globals pointing at the deleted names. If `settex_active` was true at teardown, the first post-transition draw that reaches the settex path before a fresh `G_SETTEX` samples a deleted (or recycled) GL name → black or wrong texture for a frame. Same for `rendering_state.bound_texture_id[]`/`textures[]`, which can make the flush path believe a dead texture is still bound (skipping a rebind).

**Verified trace.**
- `gfx_clear_n64_dl_regions` (`:24491-24507`, called from `lvl.c:588` on every stage load) deletes cached settex textures and clears `settex_cache`/`settex_rgba_pixels/_w/_h` — but **not** `settex_active` (`:969`), `settex_gl_tex_id` (`:970`), `settex_tex_w/h` (`:971`), `settex_fmt/siz/texturenum` (`:972-973`), `settex_tile_state[2]` (`:980`), nor `rendering_state.bound_texture_id[2]`/`textures[2]` (`:3526`/`:3525`).
- `gfx_clear_texture_cache` (`:24509-24534`) has the identical gap, *plus*: it resets the LRU pool via `tex_cache_init()` while `rendering_state.textures[i]` may still point at pool nodes — stale node pointers with `textures_changed` unset.
- All the pieces of a correct clear already exist: the `G_SETTEX` "DISABLED" branch (`:21133-21146`) clears the settex globals + `gfx_settex_clear_tile_state()` (`:995`), and `gfx_force_texture_unit_reload` (`:4347-4362`) resets one unit's `rendering_state` entry and sets `rdp.textures_changed[tile]`.

**Fix (exact code).** One shared helper, placed right after `gfx_invalidate_evicted_texture_node` (after `:4394`), used by both teardown functions:

```c
/* Full reset of live texture-binding state. Called from level-transition
 * teardown after the GL names have been deleted: without this, the first
 * post-transition draw can sample a deleted/recycled GL texture. */
static void gfx_reset_texture_binding_state(void) {
    settex_active = false;
    settex_gl_tex_id = 0;
    settex_tex_w = 0;
    settex_tex_h = 0;
    settex_fmt = 0;
    settex_siz = 0;
    settex_texturenum = -1;
    settex_rgba_pixels = NULL;
    settex_rgba_w = 0;
    settex_rgba_h = 0;
    gfx_settex_clear_tile_state();
    for (int i = 0; i < 2; i++) {
        gfx_force_texture_unit_reload(i);   /* clears rendering_state.textures[i],
                                               bound_texture_id[i], sets textures_changed */
    }
}
```

Then in `gfx_clear_n64_dl_regions`, replace the tail `:24504-24507`:

```c
    settex_cache_count = 0;
    memset(settex_cache, 0, sizeof(settex_cache));
    gfx_reset_texture_binding_state();
}
```

and identically replace the tail of `gfx_clear_texture_cache` (`:24528-24533`). (The helper subsumes the three `settex_rgba_*` lines — delete them.) Optionally add `gfx_flush();` as the first line of each teardown fn; it's a free no-op at stage load and armors against future mid-frame callers.

**Steps.** Add helper after `:4394` (both teardown fns are below it; `gfx_settex_clear_tile_state` `:995` and `gfx_force_texture_unit_reload` `:4347` precede it). Swap both function tails. `grep -n "settex_rgba_pixels = NULL" src/platform/fast3d/gfx_pc.c` — remaining sites should be only `gfx_init` (`:23388`) and the `G_SETTEX` DISABLED branch.

**Verification & acceptance.** The teardown runs on **every** stage load. Deterministic sweep: headless-capture all 20 level slugs (`dam facility runway surface1 bunker1 silo frigate surface2 bunker2 statue archives streets depot train jungle control caverns cradle aztec egypt`) at `--screenshot-frame 90`; acceptance = no black-textured screenshot, plus one interactive mission-complete transition (Dam → outro → menu → next level) with no one-frame texture glitch; `tools/asan_smoke.sh --gate` clean (levels 33/41 include intro transitions).

**Pitfalls.** `gfx_force_texture_unit_reload` calls `gfx_rapi->select_texture(tile, 0)` — a render-API call from teardown; fine at stage load (context live), but if teardown ever moves off the render thread that's a threading bug — leave a comment. Don't clear `rdp.loaded_texture[]`/TMEM shadow here — the LRU cache identity keys handle staleness; over-clearing invites re-upload storms. `gfx_clear_texture_cache` is currently uncalled from game code — fix it anyway; it's public API and the pattern *will* be copied.

### 5.4 — LOW — Light-array overflows on garbage MOVEMEM / moveword

**Background.** `MAX_LIGHTS 2` (`:208`); `rsp.current_lights` is `Light_t [MAX_LIGHTS + 1]` = 3 entries (`:3401`) and `current_lights_coeffs` is `float [2][3]` (`:3403`). The fields following `current_lights` in `rsp` (`current_lookat`, coeffs, `current_num_lights`) are what an overflow tramples. Safe on shipped content (GoldenEye uses ≤2 lights); a garbage command corrupts lighting state silently.

**Verified trace.**
1. Base-GBI MOVEMEM light case, `:19694-19704` (memcpy at `:19702`): `memcpy(rsp.current_lights + (index - G_MV_L0) / 2, data, sizeof(Light_t));` — index ∈ 0..7; 3–7 overwrite the following fields. The F3DEX2 branch immediately above (`:19680-19686`) *does* bound-check — copy its idiom.
2. `G_MW_NUMLIGHT`, `:19747-19748`: `rsp.current_num_lights = ((uint32_t)data - 0x80000000U) / 32;` — unclamped `uint8_t`. Consumers: vertex hot path `:16625-16646` — the `li` loop indexes `current_lights_coeffs[li]` (bounded 2) up to 254 → OOB read; and the ambient read `:16633` `rsp.current_lights[rsp.current_num_lights - 1]` with `num==0` → index **−1** → OOB read. (The `li` loop itself is safe at 0 — `0 < -1` is false — the ambient read is the bug.) The ambient/relight path guards this exact case (`:10610-10612`); the hot path doesn't.

**Fix (exact code).**

At `:19694-19704`, replace the base-GBI case body:
```c
        case G_MV_L0: case G_MV_L1: case G_MV_L2: case G_MV_L3:
        case G_MV_L4: case G_MV_L5: case G_MV_L6: case G_MV_L7:
        {
            int lightidx = (index - G_MV_L0) / 2;
            if (lightidx >= 0 &&
                lightidx < (int)(sizeof(rsp.current_lights) / sizeof(rsp.current_lights[0]))) {
                memcpy(&rsp.current_lights[lightidx], data, sizeof(Light_t));
                rsp.lights_changed = true;
            }
            break;
        }
```
(Silent-drop matches the sibling F3DEX2 branch exactly.)

At `:19747-19749`, clamp:
```c
        case G_MW_NUMLIGHT:
        {
            uint32_t n = ((uint32_t)data - 0x80000000U) / 32;
            if (n < 1) n = 1;                                /* ambient always exists */
            if (n > MAX_LIGHTS + 1) n = MAX_LIGHTS + 1;
            rsp.current_num_lights = (uint8_t)n;
            rsp.lights_changed = 1;
            break;
        }
```
(GBI `NUMLIGHTS(n)` encodes `(n+1)*32 + 0x80000000`, so well-formed data always yields ≥1 — the min-clamp only fires on garbage, and clamping to 1 makes the ambient read hit `current_lights[0]`, matching upstream fast3d's "num includes ambient" convention.)

**Steps.** Two mechanical edits; rebuild; behavior identical on all shipped content (game never exceeds lights 1–2 or numlight 3).

**Verification & acceptance.** No targeted repro without a DL harness — regression-only: `tools/asan_smoke.sh --gate` clean; sim-hash gate unchanged; frames byte-identical (spot-check one `--screenshot-frame 90` capture diff pre/post).

**Pitfalls.** Don't clamp `current_num_lights` at the *read* sites instead — there are multiple consumers (`:10610`, `:18787`, `:19204`) and the write-site clamp fixes them all. Keep `MAX_LIGHTS + 1` (not `MAX_LIGHTS`): the ambient light legitimately occupies the extra slot.

### 5.5 — LOW / INFO — Other (guards, one line each)

- **Matrix stack pop-to-zero.** `gfx_sp_pop_matrix` (`:15839-15848`) guards `> 0` — the stack *can* legally reach size 0 (only a DL with more pops than pushes gets there). At size 0: a non-push `G_MTX LOAD` writes `modelview_matrix_stack[size - 1]` = index −1 (`:15737-15738`), and `gfx_sp_vertex` computes `uint8_t mtx_stack_pos = ... - 1` = **255** (`:16344`) then reads `modelview_cmd_addr[255]` (arrays are `[11]`, `:3376-3388`). Mitigation confirmed: `gfx_sp_reset` (`:23326`) sets stack size to 1 **once per submitted gfx task** (`:23568`) — *not* per DL entry as pass 1 said — so corruption is confined to one frame's task. **Guard (PD-style, keep the base entry):** at `:15841`, change `if (rsp.modelview_matrix_stack_size > 0) {` to `if (rsp.modelview_matrix_stack_size > 1) {   /* never pop the base entry */` (the inner `gfx_update_mp_matrix()` condition at `:15843` becomes always-true; leave it).
- **Combiner input-slot overflow.** `gfx_generate_cc`'s `uint8_t shader_input_mapping[2][7]` (`:14260`) is written as `shader_input_mapping[j][next_input[j] - 1] = v;` at **six** sites (`:14340, :14351, :14368, :14386, :14398, :14411`) with no bound on `next_input`. `SHADER_INPUT_1..7` = 1..7 and `SHADER_TEXEL0` = 8 (`gfx_cc.h:31-38`), so the 8th distinct per-channel input both writes index 7 (one past the array) and yields `val = 8`, silently aliasing TEXEL0. Latent — no authored GE combiner uses >7 distinct non-fixed inputs per channel. **Guard:** a tiny local helper used at all six sites:
  ```c
  /* place just above gfx_generate_cc */
  static int gfx_cc_alloc_input(uint8_t mapping[7], uint8_t number[32],
                                int *next, uint32_t v) {
      if (number[v] == 0) {
          if (*next > SHADER_INPUT_7) {
              return SHADER_0;   /* out of varying slots — degrade like an
                                    out-of-range CCMUX value, never alias TEXEL0 */
          }
          mapping[*next - 1] = (uint8_t)v;
          number[v] = (uint8_t)(*next)++;
      }
      return number[v];
  }
  ```
  Land inside a hardening batch; verify shader parity with one `--screenshot-frame` diff per backend (GL + Metal) since this touches shader-ID generation for *every* combiner.
- **Mid-frame deferred-XLU flush.** `:14014-:14016`: when a frame would exceed `ROOM_XLU_DEFER_MAX_BATCHES` (4096, `:4498`), `gfx_room_xlu_deferred_draw_pending()` flushes mid-frame, reordering translucency for that frame. Memory-safe, cosmetic, pathological-only. **Guard:** a log-once (follow the `g_native_sky_overflow_logged` pattern, `:3564`/`:23972`) so it's observable if a real scene ever hits it. Note this path's per-group `malloc/memcpy/free` is §7.3's arena item — don't fix it here.

#### Audit-pass corrections (§5)

1. **File identity:** all §5 anchors are in `src/platform/fast3d/gfx_pc.c`; the same-named legacy file is excluded from the build.
2. **5.1 blast radius:** `loaded_vertices` is the *final* member of `rsp`, so the overflow corrupts following file globals, not "adjacent `rsp` globals". ASan classifies it as `global-buffer-overflow`. `dest_idx` is masked to 0–15, so only the *sum* check can fire on the PC path.
3. **5.1 "20 slots":** correct, but `MAX_VERTICES` is **16** + 4 texrect scratch slots; the right G_VTX bound is 16, not 20. Upstream fast3d's 64 does not apply to this microcode.
4. **5.2 severity/reachability:** downgraded MEDIUM → MEDIUM-LOW (overflow needs >2048 distinct settex uploads between stage loads); the "GL-on-Metal destabilization" comment belongs to the LRU cache, not this branch.
5. **5.2 state half already exists:** `gfx_invalidate_settex_gl_texture` already clears the settex globals and force-reloads bound units — the gap is *only* the missing `gfx_flush()`.
6. **5.3:** additional finding — `gfx_clear_texture_cache` also leaves `rendering_state.textures[]` pointing at reset LRU pool nodes; covered by the shared helper.
7. **5.4 anchor drift:** the base-GBI light memcpy is at `:19702` (`:19694` is the `case` label). Other anchors exact.
8. **5.5:** `gfx_sp_reset` runs once per submitted gfx task, not "at every DL entry" (effect unchanged). Combiner unguarded writes are at `:14340-14411` (six sites), not `:14315`.
9. **Verification tooling reality check:** no DL-injection harness; no attract/auto-demo mode (`port_native_ramrom_playback_smoke` in `CMakeLists.txt:254` points at a **missing** script); no `GE007_DUMP_FRAME` — real capture paths are `--screenshot-frame/--screenshot-label/--screenshot-exit` and `GE007_SCREENSHOT=/path.ppm` (frame 30). Settex observability = `GE007_TRACE_SETTEX=1`.
10. **Preamble spot-checks passed** — the "verified clean" renderer list stands as written.

---
## 6. Incomplete decompilation / stubs / gated features

**The platform stub layer is clean.** `stubs.c`, `asset_stubs.c`, `segment_stubs.c` contain only N64-hardware-only no-ops, dead `#if 0` blocks (real bodies live in `audio_compat.c`), or ROM-offset constants. No gameplay-affecting stub compiles into the native build. The build defines `NATIVE_PORT`, `PORT_FIXME_STUBS`, and `NONMATCHING` simultaneously, so the scary-looking empty stubs are shadowed by the real `#ifdef NATIVE_PORT` bodies.

### 6.1 — MEDIUM (feature-loss) — "Shoot out the lights" fully coded, **verified**, but still default-OFF

> **STATUS 2026-07-04 — DEFAULT-ON FLIP LANDED to main as merge `adb2c72` (item commit `ae81b3e`).** Polarity flipped at `lightfixture.c:121` (env-unset → ON; `GE007_SHOOT_OUT_LIGHTS=0` is the A/B escape hatch); comment + `docs/VISUAL_MODES.md` + this plan's header updated. Verified: build both backends + ctest 22/22; LF_VERIFY sweep re-run **0 DIVERGE** across bunker1(1236)/silo(5502)/control(1176 EQUAL); default-flip confirmed (env-unset populates, `=0` suppresses population); **sim-invariance flag-on == flag-off** (hash `479ad6376bdbb20a`); `shoot_out_lights_regression.sh` PASS (43.3% ROI darkening, 100%-identical persistence on re-entry, pre-fire ON==OFF byte-identical). ASan: the shoot-lights DL walks + `>>=2` writes are clean (silo fully clean under UBSan/ASan). **One out-of-scope discovery:** a UBSan finding on bunker1 (`chrobjhandler.c:28290`, float→s16 overflow in `process_monitor_animation_microcode` — monitor-screen texture coords, NOT the light path) that is **PRE-EXISTING and unrelated** — it fires identically with the feature forced OFF (`=0`). Logged separately, not fixed here. Manual windowed A/B (criterion 2) remains a manual step.

**Background — what the feature is (N64 ground truth).** In the original game, shooting a light fixture darkens it and it stays dark. "Light fixture" means a background surface textured with one of exactly ten image IDs — `check_if_imageID_is_light` (`src/game/lightfixture.c:87-107`): `IMAGE_WALL_LAMP`, `IMAGE_203/205/252/255/256_LIGHT`, `IMAGE_PANEL_LAMP`, `IMAGE_HANGING_LAMP`, `IMAGE_NEON_LAMP`, `IMAGE_LINEAR_LAMP`. Correct behavior for testers, straight from the reference decomp (original fn `0x7F0BBE0C`): (a) the shot triangle's vertex shade is quartered (`v.cn[0..3] >>= 2` — RGBA vertex color, so the surface renders at ~25% brightness); (b) spark/glass particles spawn along the triangle's three edges (`lightfixture.c:594-616`); (c) *neighbouring* triangles in the same fixture's DL run whose any vertex lies within `get_room_data_float1() * 100` of an already-darkened vertex also darken (`sub_GAME_7F0BBCCC`, `:511-542`); (d) darkening persists — `(room, vtx_index)` pairs are recorded in `darkened_light_table` (512 entries) and `redarken_lights_in_room` re-applies `>>= 2` on any room reload. It is vertex-color-only: positions are never touched, so collision/LOS/auto-aim are unaffected by construction. There is no dynamic light-state change on N64 — "the room gets darker" is entirely these vertex shades (the remaster's lamp-flicker emitter, W1.E7.T1, rides this destroy path separately).

**Verified trace (all on current `main`).** The implementation is complete and the population bug is confirmed fixed:

- Native decode helpers: `return_ptr_vertex_of_entry_room` (`lightfixture.c:156-177`, backward 8-byte walk to the governing `G_VTX` 0x04, segment-0x0E rebase onto `ptr_point_index` in `uintptr_t`), `extract_vertex_indices_from_triangle` (`:198-217`, G_TRI1 bytes 5/6/7 ÷10; G_TRI4 raw 4-bit, **no** ÷10), parent `sub_GAME_7F0BBE0C` (`:546-650`).
- Population: `lf_populate_room_lightfixtures` (`bg.c:357-383`) walks **both** primary and secondary room DLs; called from the on-demand loader at `bg.c:9923` and the preloader path at `lvl.c:280`. Unload hygiene: `bg.c:10336`; stage-transition reset: `lvl.c:310` (inside `pc_room_loader_reset`). Live call site: `chrprop.c:3069` (`sub_GAME_7F0BBE0C(hit_output.tricmd, hit_output.tri_index, best_room)`, material-gated by `check_if_imageID_is_light`).
- Gate (`lightfixture.c:558`): `if (!ge007_shoot_out_lights_enabled()) { return; }`. Helper at `:117-124`: env-only, `GE007_SHOOT_OUT_LIGHTS`, enabled iff `e[0]=='1'`. No settings-registry key; the only doc surface is the flag table row `docs/VISUAL_MODES.md:107`.

**The `vtx_base_offset` question — RESOLVED, with proof.** The concern (risk comment `lightfixture.c:354-365`): the collision hit-test parses the governing G_VTX header and subtracts `vtx_base_offset = coll_dl[1] & 0xF` (the G_VTX DMEM start slot `v0`) from every decoded triangle index — assignment `bg.c:11465`, G_TRI1 subtraction `bg.c:11488-11490`, G_TRI4 `bg.c:11608-11610` — before forming the absolute `Vertex*`. The darken path resolves the **same base** from the **same G_VTX** but does *not* subtract the nibble. Two facts settle it:

1. **The N64 original doesn't subtract either.** The reference `#else` bodies (`lightfixture.c:179-193`, `:219-250`) index `vertices[tri.v[k]/10]` directly, no offset — so *not subtracting is the faithful behavior*; subtraction would only matter if Rare ever authored a fixture run with `v0 != 0`, in which case the original game darkened wrong vertices too.
2. **Empirically, `v0` is always 0 on fixture runs.** Measured, not assumed — **W4.E1.T2 (commit `f860a41`, merged `9057123`)**: the `GE007_LF_VERIFY` sweep (`lf_verify_sweep_room`, `lightfixture.c:422-462`, driven from `bg.c:381` at every room population, plus an in-shot hook in `darken_triangle_in_room` `:478-481`) ran across **bunker1 (23 rooms), silo (25), control (17) = 6,210 fixture triangles: `vtx_base_offset` was 0 in every case, every darken pointer == the hit-record pointer (all `EQUAL`, `deltaSlots=0`, zero `DIVERGE`)**. Note: **facility has no textures matching `check_if_imageID_is_light`** — it is not a valid test level for this feature (pass 1 and older notes both named it; use silo/control instead).

W1.E2 side note for reviewers: the world-pos work changed *renderer-side* VBO plumbing only. Darkening mutates the game-side big-endian `Vtx` pool (`ptr_point_index`), which both GL and Metal re-decode every frame, so the effect is backend-agnostic and untouched by W1.E2.

**What actually remains: the default-ON flip (W4.E1.T4).**

*Step 1 — (optional but cheap) re-run the verify sweep.* It fires automatically at room population; boot a fixture level with both flags:

```bash
cmake -S . -B build && cmake --build build --parallel
GE007_SHOOT_OUT_LIGHTS=1 GE007_LF_VERIFY=1 \
  ./build/ge007 --rom baserom.u.z64 --level bunker1 --deterministic \
  --screenshot-frame 120 --screenshot-exit 2> /tmp/lf_verify_bunker1.log
grep -c ' EQUAL$'  /tmp/lf_verify_bunker1.log    # expect thousands
grep -c ' DIVERGE' /tmp/lf_verify_bunker1.log    # expect 0
```
Repeat with `--level silo` and `--level control`. Do **not** use `--ramrom` for anything visual (renders black headless). Each `[LF-VERIFY]` line reads:
```
[LF-VERIFY] room=5 tri_type=0 v0 vbo=0 darkenIdx=12 hitIdx=12 darkenPtr=0x... hitPtr=0x... deltaSlots=0 EQUAL
```
`vbo` = the G_VTX start-slot nibble; `DIVERGE` means the darken path would touch the wrong vertex, offset by `deltaSlots` `Vtx` slots.

*Step 2 — decision tree.*
- **0 DIVERGE across all swept rooms (the already-recorded outcome):** no code change to the decode; proceed to the flip.
- **Any DIVERGE:** apply the offset subtraction in exactly **one** place — `extract_vertex_indices_from_triangle` (`lightfixture.c:198-217`), never also in `return_ptr_vertex_of_entry_room`. Recover `vbo` the same way `lf_verify_vertex_base` does (`:389-397`):
  ```c
  /* lightfixture.c — extract_vertex_indices_from_triangle, tri_type==0 branch
     (and symmetrically in the G_TRI4 branch): */
  const u8 *gvtx = cmd;
  const u8 *dl_start = lf_containing_dl_start(cmd, room_index);  /* needs room_index param added */
  s32 vbo = 0;
  if (dl_start != NULL) {
      while (gvtx[0] != 0x04 && gvtx >= dl_start + 8) { gvtx -= 8; }
      if (gvtx[0] == 0x04) { vbo = gvtx[1] & 0xF; }
  }
  *idx1 = cmd[5] / 10 - vbo;
  *idx2 = cmd[6] / 10 - vbo;
  *idx3 = cmd[7] / 10 - vbo;
  ```
  (Requires threading `room_index` into the signature — all three callers have it.) Re-run Step 1 until 0 DIVERGE. This would then *deviate from the N64 reference* in favor of geometric correctness — document that if it ever happens.

*Step 3 — the flip.* Change the polarity at `lightfixture.c:121`:
```c
-        s_ge007_shoot_lights = (e != NULL && e[0] == '1') ? 1 : 0;
+        s_ge007_shoot_lights = (e != NULL && e[0] == '0') ? 0 : 1;   /* default ON; =0 is the A/B escape hatch */
```
and update the comment block at `:111-115`, the flag row in `docs/VISUAL_MODES.md:107`, and the status header of `docs/SHOOT_OUT_LIGHTS_PLAN.md`. Default-ON is justified as a faithfulness restoration (survey class C3, same bucket as portal fixes/glass shards). The gate is env-only — no `settingsRegister*` key to touch.

**Verification & acceptance criteria (matches the W4.E1.T4 row, `docs/remaster-aaa/04-content-geometry-effects.md:205`):**
1. `tools/shoot_out_lights_regression.sh` exits 0 — the purpose-built harness (bunker1, warp pad 20, scripted pose under the room-5 linear lamp, `GE007_AUTO_FIRE` burst): (a) fixture ROI luma drops ≥30% post-shot, (b) re-entry ROI ≤1.0% changed vs post-shot (persistence), (c) OFF run doesn't darken and the pre-fire frame is byte-identical ON vs OFF.
2. Manual pass: play Bunker 1 + Silo/Control windowed, shoot ≥10 distinct fixtures; confirm darkening, neighbour spread, edge sparks, and staying dark after leaving/returning. A/B with the flag=0.
3. Sim-invariance: the documented §4.1.4 flag-A/B protocol — direct `--sim-state-hash-out <path>` runs with the flag on vs off (NOT a bare `sim_invariance_gate.sh` call, which A/Bs post-FX, not this flag) — hashes identical; `tools/playability_smoke.sh --all` green; `scripts/ci/check_sim_render_separation.sh` green.
4. `tools/asan_smoke.sh --gate` clean (the DL walks and `>>=2` writes are the OOB suspects).

**Pitfalls.**
- Facility has no matching light textures — a "shoot the lamps on Facility" test silently proves nothing.
- The secondary-DL segfault in the fixture walk was fixed in `e70fc8e` (`lf_containing_dl_start` bounds the backward walk by its *own containing buffer*, `lightfixture.c:140-154`) — don't reintroduce a primary-only bound if touching this code.
- `darkened_light_table` wraps at 512 entries (`lightfixture.c:324-327`) — expected N64 behavior, not a bug.
- Stale `Video.MSAA`/`Video.RenderScale` in a CWD `ge007.ini` will break the harness's byte-identical comparisons; the harness pins them via `--config-override` — do the same in ad-hoc runs.
- Glass-shard *cosmetics* are a separate WIP (`docs/GLASS_SHARDS_WIP.md`, `GE007_GLASS_SHARDS=0` to isolate) — do not block the flip on shard looks.

### 6.2 — MEDIUM (MP-only) — "You Only Live Twice" scenario logic gated off

**Background.** YOLT is the two-lives multiplayer scenario: die twice and you're eliminated; last player standing wins. Its per-frame win-condition/elimination handling is section C4 of `lvlManageMpGame` (`lvl.c:2054`), gated at `lvl.c:2244-2254`:
```c
/* C4: YOLT (You Only Live Twice) scenario
 * Known decomp issue: this section is 63.28% match on decomp.me.
 * Gated behind GE007_MP_YOLT=1 due to known inaccuracies. */
#ifdef NATIVE_PORT
    static s32 s_yoltEnabled = -1;
    if (s_yoltEnabled < 0) {
        s_yoltEnabled = (getenv("GE007_MP_YOLT") != NULL) ? 1 : 0;
    }
    if (s_yoltEnabled)
#endif
```
Gated block body: `lvl.c:2254-2333`. With the gate off, a YOLT match runs but never ends by elimination — it only ends on the match timer.

**Verified trace — what "63.28%" means and what's actually wrong.** `63.28%` is the decomp.me match score for the whole function (scratch cited in the fn header comment `lvl.c:~2559-2573`: *"there is one big `if` block that is very wrong..."*). That "very wrong big if block" is observably this YOLT section — the compiled C has at least four structural defects visible by inspection (`lvl.c:2263-2325`):
1. `var_player_count2_again = var_player_count2 & 3` with an `else` branch whose loop bound is that same value (== 0 for a **4-player** match) — the counting loop body executes **zero** times, so `not_dead_count`/`killed_count` stay 0 and YOLT can never terminate by elimination.
2. `killed_count`/`not_dead_count` accumulate across the outer `i` loop without reset — double/triple-counted for 2-3 players.
3. `elimination_order` is initialized to 0 (`lvl.c:2067`) and **never incremented** — every eliminated player gets `order_out_in_yolt = 1`.
4. `prev_finished_death_count` is never updated (`finished_death_count = prev_finished_death_count + 1` always yields 1).

This is a mis-transcribed loop-unroll, not a tuning problem.

**Recommendation: re-decomp the section; do not fix-forward.** Fixing the C "by intent" risks inventing behavior. Unlike the repo's lying `#if 0`/`#else` reference *bodies*, `lvlManageMpGame` has the **real annotated MIPS** in-file: `GLOBAL_ASM` copies at `lvl.c:3069` (US), `:3955`, `:4849`. The YOLT section is findable in the ASM by the compare against `SCENARIO_YOLT` (== 1, `src/bondconstants.h:1662`) near the `jal mpCalculateAwards` / `jal mpwatchSetStopPlayFlag` pair and the `order_out_in_yolt` byte store. Effort: **bounded, ~0.5-1 day** — transcribe ~80 instructions of one section into the already-converged native body, keep the gate during bring-up, flip after validation.

**Steps.**
1. Read the US ASM section (`lvl.c:3069+`), reconstruct the loop structure (expect: one pass computing alive/eliminated counts per player with per-iteration reset, elimination-order increment on newly-dead, end conditions `alive <= 1` → awards, all-eliminated → stop-play).
2. Replace `lvl.c:2254-2333` under the existing gate; build.
3. Validate (below), then flip the gate default — and fix its polarity (see Pitfalls).

**Verification & acceptance criteria.** No bots exist; MP is 2-4 player split-screen, launchable headless from the CLI (`main_pc.c:662-685`), and `tools/mp_smoke.sh` already parameterizes scenario:
```bash
# boot/render/timer smoke, YOLT selected, gate ON:
GE007_MP_YOLT=1 tools/mp_smoke.sh --scenario yolt --players 2 --timelimit 60
# manual behavior check (windowed):
GE007_MP_YOLT=1 ./build/ge007 --rom baserom.u.z64 --multiplayer --players 2 --mp-stage temple --scenario yolt
```
Acceptance: (a) smoke exits 0 with the gate on for 2, 3, and 4 players; (b) manual 2P: kill P2 twice → P2 eliminated with `order_out_in_yolt` set, match ends declaring P1; (c) 4P case terminates (regression on defect #1); (d) gate-off run byte-identical to today's behavior until the flip. Deterministic scripted *kills* are not currently rig-able (the `GE007_AUTO_*` rig drives P1 only), so the elimination-semantics check is a manual lane — acceptable per repo precedent. Netplay note: lockstep netplay hashes sim state — land this *before* netplay ships or version the gate into the sim-hash inventory.

**Pitfalls.** The gate polarity is nonstandard: `getenv("GE007_MP_YOLT") != NULL` — **any** value enables it, including `GE007_MP_YOLT=0`. Fix to the `e[0]=='1'` idiom while in there. The `#else` reference copy of the whole function at `lvl.c:2574+` (second YOLT block at `:2750`) does not compile; don't transcribe from it — repo rule: reference bodies lie; the GLOBAL_ASM does not.

### 6.3 — MEDIUM (confidence: HIGH, upgraded) — Dropped autogun beam tick in the solo prop loop

**Background.** `handle_mp_respawn_and_some_things` (`chrprop.c:5377+`, the active per-frame prop tick despite the name) carries this in the OBJ/WEAPON/DOOR branch (`chrprop.c:5439`):
```c
/* Skip unk3==0xD beam tick — hardcoded offset, rare case */
```
Pass 1 rated this low-medium confidence, hedging on the repo's "reference bodies lie" caveat. That caveat does not apply here: the evidence is the **annotated shipping MIPS**, not a `#else` C body.

**Verified trace — the branch exists, and it is decoded.** The US/JP `GLOBAL_ASM` (`chrprop.c:5469-5748`) calls the beam tick `sub_GAME_7F062B00` **seven** times; the native C calls it **twice** (`:5397-5398`, `chr->unk180[0..1]`). The skipped branch, at ASM `7F03C8E4-7F03C904` (`chrprop.c:5655-5663`):
```asm
lbu   $t9, 3($s0)          # $s0 = obj: byte at +3 = object subtype
li    $at, 13              # 0xD
bnel  $t9, $at, .L7F03C9C4 # skip unless subtype == 0xD
lw    $v0, 4($s1)          # $s1 = prop: reload prop->obj
jal   sub_GAME_7F062B00
 lw   $a0, 0xcc($v0)       # arg = *(pointer at obj+0xCC)
```
Every piece resolves: object subtype **13 = `PROPDEF_AUTOGUN`** (`src/bondtypes.h:3314`) — the drone gun; offset **0xCC = `AutogunRecord.beam`** (`struct beam *beam;`, `src/bondtypes.h:3072-3130`); `sub_GAME_7F062B00` (`gun.c:14679`) is the beam advance/expiry tick (`unk28 += unk20 * dt`, retires the beam via `unk00 = -1` when it outruns `unk1c`). The native port *renders* autogun beams (`chrobjhandler.c:27621-27622`: `autogun->type == 13 && autogun->beam != NULL → sub_GAME_7F061E18(gfx, autogun->beam, 1)`, with an adjacent comment `:27612-27615` explaining the same 0xCC/struct-field translation) and *creates* them (`:12178`) — so the tick is the **only** missing leg: drone-gun tracer beams are drawn and armed but never advanced or expired. Net effect: frozen or never-visible autogun tracers on drone-gun levels. "Rare case" was a mislabel; it fires every frame an autogun exists.

Secondary observation from the same ASM: the C's `/* Skip PROP_TYPE_VIEWER — MP only */` (`:5445`) is only half-true. The ASM's type==6 (`PROP_TYPE_VIEWER`) branch (`7F03C944+`) ticks the owning player's two gun beams (`player + 0xA54` / `+0xDFC`) **unconditionally**; only the subsequent `chr+0x180/0x1AC` pair is `getPlayerCount() >= 2`-gated. If viewer props exist in solo, player tracer ticks are being dropped too — the probe below answers this for free. The `:5426` reparent skip (`obj->flags & 0x8000`) is genuinely MP-only as labeled.

**Fix/protocol.**

*Step 1 — probe (log, change nothing).* Behind an env var, at the skip site (`chrprop.c:5439`) and in the same do/while for type==`PROP_TYPE_VIEWER`:
```c
#ifdef NATIVE_PORT
{   /* GE007_PROBE_AUTOGUN_BEAM=1: count dropped beam-tick candidates */
    static int s_probe = -1;
    if (s_probe < 0) { const char *e = getenv("GE007_PROBE_AUTOGUN_BEAM"); s_probe = (e && e[0]=='1'); }
    if (s_probe && obj != NULL && obj->type == 0x0D /* PROPDEF_AUTOGUN */) {
        AutogunRecord *ag = (AutogunRecord *)obj;
        fprintf(stderr, "[AUTOGUN-BEAM] obj=%p beam=%p active=%d\n",
                (void *)obj, (void *)ag->beam,
                ag->beam ? (((ChrRecord_f180 *)ag->beam)->unk00 >= 0) : -1);
    }
}
#endif
```
Run on the drone-gun levels — Runway (`--level runway`; the `AutogunRecord` struct comments were calibrated there), Control, Aztec, Egyptian — with `GE007_AUTO_WARP_PAD` to reach the guns, and confirm hits with `active=1` (a live, never-ticked beam).

*Step 2 — the fix,* mirroring the port's own 64-bit-safe idiom from `chrobjhandler.c:27621` (struct field, not the 0xCC pointer poke), replacing the skip comment:
```c
/* N64 parity (US ASM 7F03C8E4-7F03C904): tick the autogun's tracer beam. */
if (obj != NULL && obj->type == 0x0D /* PROPDEF_AUTOGUN */) {
    AutogunRecord *ag = (AutogunRecord *)obj;
    if (ag->beam != NULL) { sub_GAME_7F062B00((ChrRecord_f180 *)ag->beam); }
}
```
Gate it (`GE007_AUTOGUN_BEAM_TICK`, default off initially) because **`sub_GAME_7F062B00` consumes `randomGetNext()`** when `g_ClockTimer >= 3` (`gun.c:14690+`) — enabling it shifts the sim RNG stream relative to the current port (while *matching* N64). That makes it sim-visible: run the `--sim-state-hash-out` A/B on a level with active autoguns expecting **different** hashes (that difference is the point — document it), and byte-identical hashes on autogun-free levels. Default-on flip is then a faithfulness call, same C3 rationale as §6.1.

**Verification & acceptance.** (a) Probe shows autogun props with live beams reaching the loop on ≥2 levels; (b) with the tick enabled, visual check on Control/Runway: drone-gun tracer beams appear when firing and fade out (not frozen); (c) flag-off run byte-identical to HEAD (screenshot cmp 0.000%); (d) `tools/asan_smoke.sh --gate` clean.

**Pitfalls.** The ASM has **no** NULL check on the beam pointer (autoguns always allocate one on N64); native must keep the NULL guard. Don't read the beam via `*(ptr*)((u8*)obj + 0xCC)` — `AutogunRecord` field layout on 64-bit no longer puts `beam` at 0xCC (exactly the trap `chrobjhandler.c:27612-27615` documents). Don't conflate this with the combat audit's `chrTickBeams` residual risk — different function.

### 6.4 — LOW-MEDIUM — Other gated defaults worth a policy decision

| Flag / key | Where | Default | What changes | Recommendation |
|---|---|---|---|---|
| `Audio.OutputFilter` (+ `Audio.OutputFilterAlpha`) | registered `audio_pc.c:873`; impl `audi_port.c:108-168` | 0 (off) | One-pole low-pass approximating the N64 analog output stage; warmer/duller, closer to console — directly relevant to the "crappy MIDI" complaint (AUDIO_QUALITY_PLAN H1) | Enable in the `--faithful` preset; leave the plain default a deliberate, documented choice pending the Phase-0 reference-capture A/B. Land §2.5(a) first so it covers SFX too |
| `Audio.MasterVolume` | registered `audio_pc.c:859` (0.7 default) | registered but dead on the synth bus (§2.2) | Nothing today — a no-op lie in the config surface | Wire per §2.2 (SfxVolume/MasterVolume split); a dead documented knob is worse than no knob |
| `GE007_SHOOT_OUT_LIGHTS` | `lightfixture.c:117-124` | 0 | §6.1 — verification complete; flip is W4.E1.T4 | Flip default-ON (see §6.1) |
| `GE007_MP_YOLT` | `lvl.c:2247-2253` | off (but *any* value enables — polarity bug) | §6.2 | Re-decomp section, fix polarity, then flip |

#### Audit-pass corrections (§6)

1. **§6.1's central claim is out of date.** The `vtx_base_offset` risk is not "unresolved": W4.E1.T2 (commit `f860a41`, merged `9057123`, 2026-07-03) swept 6,210 fixture triangles across bunker1/silo/control — `vtx_base_offset` was **always 0**, all pointers `EQUAL`, zero `DIVERGE`. Additionally the N64 reference darken code doesn't subtract the offset either, so no-subtraction is also the *faithful* behavior. Residual work = manual pass + default-ON flip only.
2. **"Shoot ceiling lamps on Bunker/Facility" is wrong for Facility** — no matching light textures. Valid levels: bunker1, silo, control.
3. **Line drift:** risk comment `lightfixture.c:354-365`; unload clear `bg.c:10336`; stage-transition clear `lvl.c:310` (plus a second populate call at `lvl.c:280` pass 1 omitted); hit-test subtraction `bg.c:11465` + `:11488-11490` + `:11608-11610`; `vtx_base` derivation `bg.c:11408`; chrprop `GLOBAL_ASM(` at `:5469`. The legacy ":341/:111/:136 three funcs" anchors are fully stale — the functions live at `lightfixture.c:156/:198/:546`.
4. **§6.3 confidence is HIGH, severity plain MEDIUM.** The `unk3==0xD` branch is confirmed in the shipping US/JP MIPS at `chrprop.c:5655-5663`: subtype 13 = `PROPDEF_AUTOGUN`, offset 0xCC = `AutogunRecord.beam`, callee = the beam advance/expiry tick. "Rare case" is a mislabel (fires every frame an autogun exists), and it is sim-visible (the tick consumes sim RNG), so restoring it is a faithfulness fix, not cosmetics.
5. **§6.3 addendum pass 1 missed:** the sibling `/* Skip PROP_TYPE_VIEWER — MP only */` (`:5445`) is only half-true per the same ASM — the two per-player gun-beam ticks in the type-6 branch are unconditional; only the `chr+0x180/0x1AC` pair is player-count-gated.
6. **§6.2 additions:** the gate polarity is buggy (`GE007_MP_YOLT=0` *enables* it); the function has real `GLOBAL_ASM` references in-file, making re-decomp of the ~80-instruction section the right fix (the compiled C is structurally broken for 4 players — elimination can never trigger); `tools/mp_smoke.sh --scenario yolt` already exists as the smoke lane.

---
## 7. Performance (residual, on `main`)

Known-fixed items (M1 per-batch XLU snapshot, M2 room-attribution memo `gfx_pc.c:13026-13058`, frame pacer) are excluded and confirmed in place. Baseline: all 20 levels 101–189 fps on M3 Max (`docs/PERFORMANCE_PLAN.md` §0). Nothing below is another "Jungle at 18 fps," but items 1–3 are real milliseconds. All file:line references are `src/platform/fast3d/gfx_pc.c` unless noted. (Audio-on-render-thread is §2.1.)

### 7.1 — MEDIUM — Per-triangle diagnostic battery (`GFX_DIAG` gate, part A)

**Background.** `docs/PERFORMANCE_PLAN.md` §M2 shipped the two behavior-identical wins (glass-label hoist + 1-entry room-cmd-offset memo) and explicitly deferred "compile-time gating of the ~27 per-vertex `dbg_*` stores and the ~40 per-triangle diagnostic predicates" as hardening. This item is that deferred work. On Dam/Frigate-class scenes (~10–50k tris/frame) the battery is realistically ~0.5–2 ms/frame.

**Verified trace.**
- `gfx_emit_loaded_triangle` is declared at **:16828** (pass 1's ":16849" is the first predicate line inside it). Its preamble (:16849–:16980) computes **~27 named predicates per emitted triangle** (`has_cmd_offset`, `focus_match`, the `skip_by_*`/`tint_by_*` family, `ndc_width/height`, `emitted_large_coverage`, `critical_emitted_shard`, `viewport_spanning_glass_shard`, `extreme_postclip_room_shard`, `skip_postclip_room_cull`, `room_seam_strip_two_sided`, ...) plus `fragile_postclip_room_fragment` at :17131.
- The pre-clip block in `gfx_sp_tri1` (:19246–:19316) adds ~13 more (`ndc_metrics`, `clip_reason_flags`, `all_non_positive_w`, `needs_view_clip`, `pathological_view_clip_shard`, ...). Total ≈ 40 **combined**. NDC metrics are computed once and passed down on the unclipped path (`precomputed_ndc`, :16840–:16847) — the M2 de-dup landed; "twice per triangle" slightly overstates it.
- **Behavior-bearing predicates — these change what renders with NO env var set. They MUST stay compiled in unconditionally:**

| Predicate | Computed | Acts | Effect |
|---|---|---|---|
| `viewport_spanning_glass_shard` | :16958 (helper :3247) | :17009 → `return` :17038 | **Rejects** post-clip viewport-spanning glass shards |
| `extreme_postclip_room_shard` | :16961 (helper :3215) | :17041 → `return` :17077 | **Rejects** extreme post-clip room shards |
| `skip_postclip_room_cull` | :16965 (flag math on `clip_reason_flags`) | :17116 | **Bypasses backface cull** for depth/mixed-W-clipped room tris |
| `room_seam_strip_two_sided` | :16972 (helper :3306 + NDC thresholds) | :17115 | **Bypasses backface cull** for small room seam strips |
| `fragile_postclip_room_fragment` | :17131 (needs `ndc_metrics.area2`) | forces `cross=0.0f` :17143, `break` :17149/:17181 | **Survives front/back cull** |
| `clip_reason_flags` / `all_non_positive_w` / `needs_view_clip` | :19292–:19308 | reject :19437, CPU-clip :19469 | Core clipping — feeds `skip_postclip_room_cull` |
| `ndc_metrics` (`gfx_tri_compute_ndc_metrics`) | :16847 / :19272 | inputs to all of the above | Must stay |

- **Conditionally behavior-bearing (env-gated):** the `skip_by_*`/`tint_by_*`/`only_*` family and `focus_match` change rendering *only* when a `GE007_*` diag env var is set (each starts with a latched `g_diag_*_enabled > 0` short-circuit). `tint_match` feeds actual draw color at :17914/:18936/:18963 but is all-false with no env set. Move these behind a **single per-frame latched `g_gfx_diag_any_active` boolean** (checked once at :16849) rather than a compile gate — keeps `GE007_TINT_SKY` etc. working in shipped builds (several regression scripts rely on them, e.g. the train sky-leak repro).
- **Pure diagnostics (safe behind `GFX_DIAG`):** `emitted_large_coverage` (:16951, logging-only consumers), `critical_emitted_shard` (:16955), `pathological_view_clip_shard` (:19309 — logging only; the rejection it once fed was removed, comment :19461–:19467), `trace_target_tri`, and all `fprintf` blocks.

**Fix (exact mechanics).**
1. **Build option:** `option(GFX_DIAG "Compile per-primitive renderer diagnostics" OFF)` in `CMakeLists.txt` (pattern: `PORT_STRICT` at :453), then `target_compile_definitions(... GFX_DIAG=1)` when ON. Put `#ifndef GFX_DIAG` helper macros at the top of `gfx_pc.c` (or a small `gfx_diag.h` beside it) — there is no existing shared header for this file's diag layer.
2. **Do NOT wrap the behavior-bearing table above.** Wrap: the log-only predicates, all `[GFX-SHARD*]`/`[GFX-ROOM-EMIT]`/`[GFX-FOCUS-*]`/`[N64_TRI_TRACE]` fprintf blocks, and `gfx_trace_guard_*`/`gfx_log_tri_reject`/`gfx_effect_tri_trace_reject` call sites (make them empty static-inline stubs when off so call sites stay untouched).
3. For the env-gated skip/tint family, prefer the **runtime latch** (one bool ORing all `g_diag_*_enabled` flags, refreshed in `gfx_check_diag_env`) over compile-time removal.

**Verification & acceptance.**
- Byte-identical rendering, `GFX_DIAG=OFF` vs `ON` (no diag env vars): `--screenshot-frame` captures diffed with `tools/compare_screenshots.py` — **0 differing pixels** on Dam, Frigate, Jungle, Surface1, Statue.
- With `GFX_DIAG=ON` + trace env vars, trace output identical to today's (the M2 acceptance criterion).
- Perf: `tools/perf_census.sh dam frigate jungle surface1` before/after; expect ~0.3–1.5 ms lower `default_ms` on Dam/Frigate/Control; no level slower.
- ASan build smoke on Dam.

**Pitfalls.** The single biggest risk is wrapping one of the five behavior-bearing predicates (they *look* like diagnostics — they were born from shard debugging but now gate rendering). Second: `ndc_metrics` must stay even though most of its consumers are logs. Third: `GE007_NO_CULL`/`GE007_FLIP_CULL`/`GE007_NEAR_CLIP_ONLY` latches inside the hot path (:17091, :17123, :19478) are behavior A/B hatches, not diagnostics — leave them.

### 7.2 — LOW-MEDIUM — Per-vertex `dbg_*` struct bloat (`GFX_DIAG` gate, part B)

**Background.** Same flag, second payoff: cache footprint of `struct LoadedVertex`.

**Verified trace.**
- Struct declared at :2435–:2468. Measured layout (arm64): **sizeof = 184 bytes**, of which the `dbg_` block (offsets 64–168 incl. padding) is **104 bytes** — 15 named `dbg_` fields = 27 scalar stores. (Pass 1 said "~160 B / ~100 debug / shrinks to ~60 B" — actuals: 184 / 104 / **80 B** after removal, keeping the W1.E2 `wx/wy/wz`+`nrm` world-pos fields, which are behavior-bearing for `SHADER_OPT_WORLD_POS` draws.)
- The 27 per-vertex stores are at :16595–:16621 inside `gfx_sp_vertex`, unconditional. The clip-lerp propagates every `dbg_` field per clipped vertex at **:3000–:3021** (`gfx_loaded_vertex_lerp`) — missed by pass 1; wrap this block too.
- **Consumer census (exhaustive — `grep -rn "dbg_"` hits only `gfx_pc.c`; zero consumers in `gfx_room_normals.c`, `gfx_opengl.c`, `gfx_metal.mm`):** :3000–:3021 lerp (infrastructure, wrap); :8646/:8655/:8692 raw-rgba/cmd-words readers whose callers (:9657–:9665, :10925–:10927) are all env-gated diagnostics; :9699–:9728, :10983–:11042, :16205–:16227, :18791 — env-gated fprintf printers. **Verdict: zero behavior-bearing consumers.** `fog_depth`/`fog_coord` (:2441–:2442) are also diag-only (the real fog factor is computed from a local before the store, :16813–:16819) — optional extra 8 bytes, out of scope for the first pass.

**Fix.** Group the 15 `dbg_` fields into `struct LoadedVertexDiag { ... };` embedded as `#ifdef GFX_DIAG struct LoadedVertexDiag dbg; #endif` (mechanical rename `dbg_X` → `dbg.X`, ~144 references, all in this one file). Wrap the write block :16595–:16621, the lerp block :3000–:3021, and the printer/probe helpers. Result: 184→80 B; 3 verts/tri = 240 B (~4 cache lines vs ~9) and `rsp.loaded_vertices[20]` drops 3.7 KB→1.6 KB — fully L1-resident.

**Steps/verification/pitfalls.** Identical harness to §7.1 (same flag, same screenshot + census + trace-parity gates; land as one or two commits behind one flag). Pitfall: `gfx_loaded_vertex_raw_rgba`'s `dbg_vtx_decode_mode` read is the one "consumer that computes something" — confirm its two call sites stay env-gated before wrapping. Expected: ~0.1–0.4 ms/frame, additive with §7.1.

### 7.3 — LOW — XLU defer path: malloc/memcpy/free per deferred group per frame

**Verified trace.** :14015–:14057: `malloc` :14029, `memcpy` :14036, batch stored into `room_xlu_deferred_batches[4096]` (:4498/:4517). Frees: `gfx_room_xlu_deferred_free_from` :13775, per-batch `free` after draw :13957. Reset each frame :23619, drain at end of `gfx_run` :23683, plus mid-frame overflow drains :14016/:14223. A/B **verified to exist**: `GE007_DISABLE_ROOM_XLU_DEFER` (any non-`0`) at :13762 (also `GE007_SORT_ROOM_XLU_DEFER=0`).

**Fix (bump arena, modeled on the Metal backend's ring — `gfx_metal.mm:837-850`).** The batches are CPU-side and never outlive the frame, so **one** arena (no ring/semaphore needed):

```c
static float *g_xlu_arena; static size_t g_xlu_arena_cap, g_xlu_arena_used, g_xlu_arena_want;
static float *gfx_xlu_arena_alloc(size_t nfloats) {
    if (g_xlu_arena_used + nfloats > g_xlu_arena_cap) {          /* overflow: one-off malloc, */
        size_t need = g_xlu_arena_used + nfloats;                 /* grow high-water for next  */
        if (need > g_xlu_arena_want) g_xlu_arena_want = need;     /* frame's realloc           */
        return malloc(nfloats * sizeof(float));                   /* batch marks owns_heap=1   */
    }
    float *p = g_xlu_arena + g_xlu_arena_used; g_xlu_arena_used += nfloats; return p;
}
```
Reset point: in `gfx_room_xlu_deferred_reset()` (:13783) set `used=0` and, if `want > cap`, realloc once (seed ~1 MB). Replace `malloc` :14029 with the arena call and add an `owns_heap` bit to `GfxRoomXluDeferredBatch`; `free_from`/post-draw free only when `owns_heap`. The mid-frame drains (:14016/:14223) draw but must **not** rewind the cursor (batches later in the frame may still be appended). Growth policy: high-water, no shrink (same as Metal's `s_vbuf_want`).

**Verification & acceptance.** Byte-identical screenshots Dam/Jungle/Surface1 vs baseline (the defer path is order-preserving); `GE007_DISABLE_ROOM_XLU_DEFER=1` still renders (visually different by design — must not crash); `tools/perf_census.sh dam jungle surface1` flat-or-better; ASan clean (arena bugs show up loudly); RSS flat over 600 frames.

**Pitfalls.** The error path at :14031–:14033 (malloc fail → `free_from(first_added)`) must keep working with mixed arena/heap batches — hence `owns_heap` rather than assuming all-arena. Don't shrink the arena mid-session (level transitions also reset via the :23558 recovery path).

### 7.4 — LOW — `G_SETTEX` cache linear scan → direct-index table

**Verified trace.** `gfx_handle_settex` :21130; 12-bit key confirmed (`w1 & 0xFFF`, :21131); linear scan over up to 2048 entries (:21150) per `G_SETTEX`. Insertion :21587; **eviction is a `memmove` shift-down of the whole array** (:21589–:21597) that renumbers every slot; cache cleared wholesale at :23387/:24502/:24528.

**Fix (exact patch).** `static uint16_t settex_idx[4096];` sentinel `0xFFFF` = empty.
- Lookup (:21150): `uint16_t s = settex_idx[texturenum]; if (s != 0xFFFF && settex_cache[s].valid && settex_cache[s].texturenum == (uint32_t)texturenum) { ...hit... }`.
- Insert (:21587): `settex_idx[texturenum] = (uint16_t)slot;`.
- **Eviction (:21589–:21597) — the trap:** the `memmove` shifts every entry down one; after it, rebuild the whole table (`memset(settex_idx, 0xFF, sizeof settex_idx); for (i...) settex_idx[settex_cache[i].texturenum] = i;` — 2048 iterations, eviction-only, fine).
- Clear sites (:23387/:24502/:24528): `memset(settex_idx, 0xFF, ...)`.

**Verification.** Texture-diverse levels (Frigate, Archives, Statue): byte-identical screenshots; `GE007_DISABLE_SETTEX=1` path unaffected (:21135 short-circuits before the scan); census flat-or-better. **Pitfall:** duplicates can't exist (lookup returns first match; insert only after miss), so a 1:1 table is sound — but the `& 0xFFF` mask already guarantees `texturenum < 4096`.

### 7.5 — LOW — Per-DL-command range scans → 1-entry memos

**Verified trace.** Two scans; pass 1's line attribution was swapped/merged:
- **Draw-class:** `gfx_draw_class_for_cmd_addr` at **:553** — reverse linear scan over `draw_class_dl_ranges[512]` (:495). Called **once per DL command** from the interpreter loop at :21716.
- **Effect-label:** `gfx_effect_label_for_addr` at **:662**, scanning `effect_dl_ranges[256]` (:486). Its per-command call at :21718 is **already env-gated** (returns immediately at :11250 unless enabled), but the per-DL `gfx_effect_push_inherited_label` → `gfx_effect_inherit_label_for_child_dl` path (:5000) and the `[GFX-SHARD]` printers hit it unconditionally-ish. Worse in firefights: each explosion registers a range (`gfx_register_effect_dl_range` :618).

**Fix (clone the M2 memo at :13026–:13058).** For each scan, a 1-entry cache keyed on `(addr, generation)`, memoized on the *containing range* (store `start/end`, hit when `addr` within — consecutive DL commands share `addr±8`, so hit rate ≈ always):

```c
static struct { uintptr_t start, end; uint32_t gen; enum DrawClass cls; bool valid; } s_dc_memo;
/* hit if valid && start <= addr < end && gen == g_range_memo_gen */
```
Ranges are append/merge-only within a frame (`gfx_register_draw_class_dl_range` :529, `gfx_register_effect_dl_range` :618, both reset in the recovery path :23561–:23562 and per-frame), so bump a shared `g_range_memo_gen` in each register function *and* once per frame (same place `g_room_cmd_cache_generation` is bumped, ~:23535).

**Verification.** Byte-identical screenshots; firefight A/B (Dam or Facility corridor fight) census flat-or-better; effect/drawclass trace lanes produce identical labels in a `GFX_DIAG=ON` build. **Pitfall:** invalidate on *register/merge*, not just per frame — ranges are registered mid-frame as effects spawn; a stale memo would misattribute the very draw class the memo exists to serve. Expected: ~0.05–0.15 ms.

### 7.6 — Deferred milestones — re-affirmed against `docs/PERFORMANCE_PLAN.md` §5

- **M3 (draw-call/vertex refactor)** — deferred there with measurement (submission ≈7% of Frigate frame); still not worth it.
- **M1 option 2 (per-pass XLU snapshot)** — remains the one un-built piece with real headroom, relevant only at 4K/min-spec. Same tier as M3.
- **M4 (open-level culling)** / **M5 (dynamic resolution)** — deferral rationale (gameplay coupling via `room_rendered→auto-aim` `chr.c:5205`; remaster ≈ free) re-verified. Agree.

#### Audit-pass corrections (§7)

1. All §7 anchors are in `src/platform/fast3d/gfx_pc.c` (see §5 file-identity note).
2. `:16849` is inside `gfx_emit_loaded_triangle`; the function is declared at `:16828`.
3. "~40 predicates in `gfx_emit_loaded_triangle`" → ~27 there + ~13 in `gfx_sp_tri1` ≈ 40 *combined*; the NDC de-dup already landed, so "twice per triangle" overstates it.
4. **The behavior-bearing list was incomplete:** add `skip_postclip_room_cull` (:16965) and `fragile_postclip_room_fragment` (:17131) to the three pass 1 named, plus the env-gated skip/tint family (runtime latch, not compile removal).
5. Struct numbers: `LoadedVertex` is **184 B** / **104 B** dbg / **~80 B** after (not 160/100/60); the W1.E2 world-pos fields must be kept.
6. The clip-lerp duplicates all dbg fields per clipped vertex at :3000–:3021 — missed by pass 1, must be gated too.
7. `dbg_` consumers: all env-gated diagnostics confined to `gfx_pc.c`; no behavior-bearing consumer exists.
8. `GE007_DISABLE_ROOM_XLU_DEFER` confirmed at :13762 (also `GE007_SORT_ROOM_XLU_DEFER=0`); the Metal bump-arena model is `gfx_metal.mm:837-850`.
9. Settex: 12-bit key confirmed; **new finding — eviction `memmove`-shifts all slots, so the direct-index table must be rebuilt on eviction**; the naive "just add a table" patch is wrong without this.
10. `:553` is the **draw-class** lookup (≤512 ranges, per-command at :21716); the **effect-label** scan is at `:662` (≤256 ranges) and its per-command call is already env-gated — the residual unconditional cost is the draw-class scan plus the effect scan's non-gated callers.
11. The 7.5 memo must be invalidated on **range registration/merge** (mid-frame effect spawns), not only per frame — the room table the M2 memo mirrors is frame-stable; effect ranges are not.

---

## Appendix — Verified clean (do NOT re-flag)

These were traced and proven correct; recording them saves the next audit from re-investigating. (Pass 2 spot-checked the renderer entries — all held; see §5 corrections #10.)

**Endianness / asset loading (whole pipeline clean):** `read_be16` signed return — every widened consumer casts or bounds-checks; AI bytecode read via byte-composition macros (endian-neutral); `objectiveEntryGetPackedWord` re-extracts fields from the scrambled layout (compensated); `dotube`/`CHILD_IMAGE`/model vertex reads compose BE bytes and use the 16-byte N64 stride; bg.c portal/room in-place swaps cover exactly rooms+sentinel with count-before-swap; stan `stanByteSwapBinary` reads tail BE before swapping; audio `.ctl` parsed field-by-field with `rd_be32`; demo/ramrom headers fully swapped incl. 64-bit seeds. **No double-swaps** (all buffers freshly loaded per stage, single swap site each). **No pointer-in-ROM-field-as-host-pointer** bugs (everything goes through `vma2ptr`/offset maps/side tables). Save file is host-endian *by design* and internally self-consistent (byte-wise CRC).

**Audio music/synth chain:** sample-rate match (22050 everywhere), stereo orientation, clipping (every s16 narrowing clamps first, rail hits instrumented), resampler cross-frame state (byte-for-byte the known-good sm64ex/PD implementation), ring/queue underrun handling (SDL emits silence, no stutter), DMEM layout bounds, init order.

**Renderer:** texture-cache identity & staleness (key includes addr/size/pitch/both palettes/LUT mode; dynamic-texture and palette-animation staleness closed; eviction flushes-then-deletes for the *LRU* cache), tri-index bounds, segment-resolution NULL-guarding, depth/decal (polygon offset, documented Z_UPD-without-Z_CMP deviation), scissor/viewport 10.2 math + Metal rect clamp, fog (faithful clip_z/clip_w, clamped, shade-alpha preserved), opcode coverage (both interpreters log-once; known-ignored ops explicit), blend classification (exhaustive + instrumented fallback), no per-frame leaks in `gfx_pc.c`.

**Stubs/decomp:** platform stub layer (all N64-hardware-only or dead), 763 asset stubs are link placeholders repointed at the ROM at runtime, 47 unpatched stubs are PAL/beta assets unreachable in the US build, most `#if 0`/`#else` divergences are goto→switch re-encodings (behaviorally identical).

**Sim/input/save:** frame pacing (deadline pacer + resync, events polled after the wait), `joy.c` decomp (ring-buffer edge detection, symmetric stick clamp math), mouse aim (framerate/aspect-independent by construction), EEPROM addressing & block layout (overflow-safe, read/write consistent), CRC (same range on write & validate), config parsing (atomic tmp+rename, unknown-key preservation, clamping), timers (correct 46.875 MHz conversion), `boss.c` mainloop (wrap-safe unsigned subtraction, `g_ClockTimer` cap present).

**Perf:** game sim tick is O(n) list walks (n in the tens, no O(n²), no per-tick heap allocs, all getenvs latched); main TMEM cache is a proper hashmap+LRU; combiner/shader lookups cached; HD-pack loader miss-cached; no framebuffer readbacks on the default frame; Metal backend ring-buffered with per-frame autoreleasepool.

---

## Suggested execution order

1. **§1.1 EEPROM atomic write** — CRITICAL, tiny diff, template exists next door. Ship first.
2. **`prop.c:3331` prototype** (§4 headline) — one line; unbreaks `PORT_STRICT`. Fold into any nearby commit.
3. **§3.1 gamepad look accumulator** — small, high felt-quality win for pad players; exact patch in-section.
4. **§6.1 shoot-out-the-lights default-ON flip** — verification already recorded (W4.E1.T2); remaining = regression harness + manual pass + one-line polarity flip.
5. **§2.2 SfxVolume/MasterVolume split** + **§2.3/2.4 SFX loop-wrap & linear interpolation** — one file, big audible win, dumps re-blessed in the same commit.
6. **§2.1 audio producer thread (opt-in)** — biggest robustness+perf win; the design is resolved in-section (new synth lock, deterministic mode stays sync).
7. **§7.1+7.2 `GFX_DIAG` compile gate** — ~1–2 ms/frame; the behavior-bearing predicate table in §7.1 is the do-not-break list.
8. **§5.1/§5.2 renderer bounds+flush** — cheap defensive fixes matching the file's own posture.
9. **§6.3 autogun beam tick** (probe → gated fix → flip) and **§6.2 YOLT re-decomp** — bounded decomp-completion tasks; both sim-visible, keep gated during bring-up.
10. Batch the LOW items (§2.5, §3.2–3.4, §4.4, §5.3–5.5, §7.3–7.5) into a hardening pass.

**Note on §4:** the memory-safety lane came back essentially clean — the compiled game code is free of the 64-bit pointer-truncation class, the historical bullet-spark UB is resolved, and the allocators are correctly widened. The two things to *do*: fix the new `prop.c:3331` prototype, and check the netplay/save-state paths (out of this audit's scope) against the arena-reset lifetime invariant (§4.5) before they ship. The sweep is now re-runnable (`ub_sweep.sh`, §4) — run it after any decomp-completion commit.
