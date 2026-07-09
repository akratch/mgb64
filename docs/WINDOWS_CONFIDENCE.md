# Windows Confidence Attestation (MW.1)

**Date:** 2026-07-09 · **Branch:** `fix/mw1-windows-scrutiny` · **Scope:** the entire
Windows surface of the tree (every `_WIN32`/`__MINGW32__` branch, every
platform-divergent assumption), per backlog MW.1.

**Claim being attested:** to the best of our knowledge the Windows build compiles,
links, and — at the level static scrutiny can establish — runs correctly. Compile/link
is proven by execution (MW.2 lane); runtime behavior is attested by construction with
the specific gaps named in §5.

How to read this: §1 is the audited inventory, §2 the warning triage, §3 what is
verified by construction, §4 what is verified by execution, §5 what remains untested
and why we believe it holds anyway.

---

## 1. `_WIN32` branch inventory — 31 guard sites, all audited

Mechanical census (`#if/#ifdef/#ifndef/#elif` mentioning `_WIN32`; there are no
`__MINGW32__`/`_MSC_VER`/bare-`WIN32` guards in `src/` or `include/`). MSVC is
rejected at configure time (`CMakeLists.txt:34`); MinGW-w64 is *the* Windows toolchain.

| File | Sites | What the Windows branch does | Verdict |
|---|---|---|---|
| `src/platform/main_pc.c` | 6 | `setenv`→`_putenv_s` shim; `windows.h` (lean, `near/far` undef'd); crash handling: SEH filter + SIGABRT handler, recovery-longjmp compiled out; POSIX `sigaltstack/sigaction` kept off Windows | **Fixed this pass (M6.2)** — was `signal(SIGSEGV)` + longjmp-out-of-handler (SEH UB) |
| `src/platform/savedir.c` | 4 | `direct.h/io.h`; real write-probe (msvcrt `access(W_OK)` ignores ACLs); `_mkdir`; `%APPDATA%\ge007` priority | Correct; forward slashes are valid Win32 path separators |
| `src/platform/port_trace.c` | 4 | `_open/_close/_write` with `_O_BINARY` for the JSONL state trace; chunked `_write` loop | Correct; binary mode explicit |
| `src/platform/stubs.c` | 3 | `windows.h` (NOMINMAX, `near/far` undef'd for `guPerspective`); `_commit(_fileno)` for fsync; `MoveFileExA(REPLACE_EXISTING\|WRITE_THROUGH)` for atomic EEPROM replace | Correct; POSIX `rename`-over-existing semantics reproduced |
| `src/platform/config_pc.c` | 3 | `windows.h`; `MoveFileExA` atomic ge007.ini replace; `GetLastError` reporting | Correct; path buffers were `PATH_MAX` (260 on Windows) — **fixed this pass** |
| `src/platform/stall_watchdog.c` | 2 | was a 4-function no-op stub | **Fixed this pass** — full watchdog now compiles on Windows (CRT-fd dump IO; Apple-only sampler degrades like Linux) |
| `src/game/textrelated.c` | 2 | `direct.h`, `_mkdir` for the glyph-dump diagnostic dir | Correct |
| `src/app/ui_overlay.cpp` | 2 | no `execvp`: "return to launcher" falls back to quit-to-desktop | Acceptable degradation, documented in-source |
| `src/platform/fast3d/gfx_pc.c` | 1 | recovery gate forced shut (`GE007_ENABLE_RECOVERY` logged as unsupported) | **Added this pass (M6.2)** |
| `src/platform/platform.h` | 1 | `sigjmp_buf`→`jmp_buf` mapping so recovery TUs compile; recovery itself forced off on Windows | Comment updated this pass |
| `src/app/diag_log.cpp` | 1 | was a stub (no log file, empty snapshot) | **Fixed this pass (M6.1)** — CRT-fd tee (`_pipe`/`_dup2` + reader thread), delete-before-rename rotation, `_IONBF` (Windows `_IOLBF` = full buffering) |
| `src/app/ui_modes.cpp` | 1 | `_putenv_s` in `portableSetenv` | Correct. Edge: `KEY=` (empty value) in the expert env box *unsets* on Windows vs sets-empty on POSIX — matters only for pure-presence flags, accepted |
| `src/random.c` | 1 | no `dladdr`: random-trace symbolizer degrades gracefully | Correct (diagnostic-only) |

**Inverse sweep (POSIX calls *outside* guards).** Compile+link success in the MW.2
lane already rules out any POSIX symbol MinGW doesn't provide. The residual class —
functions MinGW provides with different *runtime* semantics — was swept by hand:
`clock_gettime(CLOCK_MONOTONIC)` (winpthreads, QPC-backed — linked via the explicit
`winpthread` in CMake), `opendir/readdir/dirent.h`, `unistd.h` `write()` in the
crash/diag writers, `strcasecmp`, `stat()` — all provided and semantics-compatible for
their uses here. `usleep`/`nanosleep`/`pthread_*`/`mmap`/`posix_spawn`: **zero unguarded
uses** (timing is SDL_Delay + `SDL_GetPerformanceCounter`; threads are SDL threads; the
watchdog sampler is `__APPLE__`-gated). No unlink-while-open patterns (all
temp-file writers close before `remove()`/rename).

---

## 2. MinGW warning triage — all 61 censused, 3 real, 3 fixed

Lane: `tools/mingw_cross_check.sh` (MW.2), brew GCC 16.1 x86_64-w64-mingw32, vendored
SDL2 2.32.10. Baseline 61 warnings reproduced exactly; after this pass **58**.
**Caveat:** brew GCC 16.1 is stricter than the MSYS2 GCC the release CI runs — counts
are not comparable across the two lanes; the *classes* are what's attested here.

| Class | n | Verdict |
|---|---|---|
| `-Wformat` `%lu` vs `uintptr_t`, `src/game/model.c:7298` | 1 | **REAL (LLP64) — fixed.** The only true varargs-type mismatch in the tree. Swept the whole `(unsigned long)`-cast family behind it (which silences `-Wformat` while truncating): fixed 3 more sites printing real 64-bit pointer values (`gfx_pc.c` `[PC_UNKNOWN_OP]`, `title.c` `[GUNBARREL-CMD]`, `bg.c` `[BG-VIS]`, plus the two `[CRASH]`/`[GFX-RECOVER]` lines touched by M6.2). Remaining `(unsigned long)` print sites carry bounded offsets/sizes that fit 32 bits by construction, or true DWORDs (`GetLastError`). |
| `-Wformat-truncation` `config_pc.c` | 1 | **REAL (Windows `PATH_MAX`=260) — fixed.** Buffers now sized to the savedir contract (1024), mirroring the EEPROM writer. |
| `-Wbuiltin-declaration-mismatch` `lround`, `gfx_room_normals.c` | 1 | **REAL (shim gap) — fixed.** `include/math.h` shadows the system header and lacked `lround`; declared. Codegen was accidentally correct (builtin + ±127 clamp). |
| `-Wbuiltin-declaration-mismatch` `asin`/`acos` (4), `bcopy` (2), `bzero` (1) | 7 | Benign, intentional. `asin/acos` are the decomp's N64 integer-LUT overrides (`math_asinacos.h` — the clang equivalent of this warning is already pragma'd there; no libm-`double` caller exists in the binary). `bcopy/bzero` are decomp idioms GCC lowers to `memmove/memset` builtins; identical on every platform. |
| `-Wmaybe-uninitialized` | 30 | Benign for Windows: decomp-faithful control flow, identical source and semantics on all three platforms (clang simply doesn't run the same interprocedural pass). Sampled: `bondview.c:3406` trio feeds only the opt-in `GE007_TRACE_BOND_BUF` diag line; `zlib.c` is the classic inflate false positive. Not Windows work — if any is a genuine sim bug it belongs to a sim-correctness item, not MW. |
| `-Warray-bounds` (snd.c ALEventQueue 6, gun.c hand[2] 2, textrelated fontchar 2) | 10 | Benign: decomp struct-as-array-head punning, covered by `-fno-strict-aliasing` + `-mno-ms-bitfields` on every platform. Same object layout everywhere. |
| `-Wmisleading-indentation` (3), `-Wcomment` (1), `FTOFIX32` redefined (1), useless storage class (1), `-Wenum-int-mismatch` (2) | 8 | Benign style/decomp noise; no codegen effect. |
| `-Wsequence-point` `chrlv.c:2365` | 1 | Audited (MW.2 flagged it): `sp70.p[0] = sp70.p[0] = 1;` — a *faithful transcription of a retail typo*, commented as such in-source. Both stores write the same value to the same lvalue; every compiler emits "store 1". Formally UB, practically pinned, and deliberately preserved per the authority hierarchy. |
| `-Woverflow` `file2.c:312/331` | 2 | Audited (MW.2 flagged it): `save->times[]` is `u8`; the retail-shaped 16-bit masks (`&= 0xff00` etc.) truncate to exactly the intended byte masks (`0xff00`→`0x00` = clear whole byte, matching the bit-packed reader at `:256-264`). Platform-independent either way. |

---

## 3. Verified by construction

- **Struct layout (the v0.3.2 crash class):** `-mno-ms-bitfields` is on `ge007` and
  `ge007_lib` (the latter is Apple-only anyway). `mgb64_app` does not get the flag but
  compiles **no** N64-layout structs: its engine seam (`src/app/engine_entry.h`) is
  pointers-and-ints only — `-mms-bitfields` differences exist *only* for bit-field
  members, so the seam is layout-safe by construction. No bit-field struct crosses the
  app↔engine boundary; grep-verified.
- **printf/scanf format portability:** local lane GCC targets **UCRT** (C99 formats
  native); the release CI (MSYS2 `MINGW64`) targets msvcrt, where mingw-w64's
  `_mingw.h` auto-enables `__USE_MINGW_ANSI_STDIO=1` for C11/C++17 TUs
  (`__STDC_VERSION__ >= 199901L && __MSVCRT_VERSION__ < 0xE00`) — header-verified. So
  `%zu`/`%lld`/`%llX` (75 uses) are safe on **both** runtimes, and `-Wformat` was live
  across the whole tree during the lane (exactly one hit, fixed).
- **File IO:** every binary artifact uses explicit binary modes — ROM `"rb"`
  (`rom_io.c`, `main_pc.c`), EEPROM `"rb"/"wb"` + `_commit` + `MoveFileExA`
  (`stubs.c`), state trace `_O_BINARY` fd IO (`port_trace.c`), screenshots/texture
  dumps `"wb"`, HD textures via `stbi_load` (internally `"rb"`). Every text-mode
  `fopen` is a log/trace/ini where CRLF is correct or harmless. Atomic-replace uses
  `MoveFileExA` because POSIX `rename()`-over-existing doesn't exist on Windows —
  both writers do this correctly (and M6.1's rotation now deletes before rename).
- **Paths:** built with `/` separators throughout (valid for Win32 APIs), bounded by
  the 1024-byte savedir contract; `%APPDATA%\ge007` for saves, `SDL_GetPrefPath`
  (`%APPDATA%\MGB64\MGB64`) for logs/app prefs. No `MAX_PATH`-sized buffers remain.
  No case-sensitivity assumptions found (all fixed-case literal filenames).
- **Config parsing / CRLF / locale:** `ge007.ini` read in text mode (CRT strips
  `\r\n`) *and* the parser's `trimWhitespace` uses `isspace()` which eats `\r` —
  CRLF-safe on every platform in both directions. `ge007_bindings.ini` values go
  through `atoi` (trailing `\r` inert). The expert-env parser trims `\r` explicitly.
  No `setlocale()` anywhere → C locale → `strtof`/`%f` decimal points are stable.
- **LLP64:** full-tree sweep of `(long)`/`(unsigned long)` casts and `%l` formats
  (§2 row 1). `ftell`-returns-`long` sites cap at ROM size (64 MB). `time_t` is not
  used for arithmetic anywhere in engine code. The N64 `str.c`/`xprintf.c` (which do
  have 32-bit-`long` assumptions) are **not compiled** into the native port —
  object-file-verified in the MinGW build tree; config parsing binds to the real CRT
  `strtol`.
- **Crash/diag/watchdog stubs (M6.1/M6.2/watchdog):** all three implemented this pass
  (see §1). Windows now has: mgb64.log + F1 diagnostics + bug-report snapshot, SEH
  `[CRASH]` diagnostics from any thread, and the sim-stall breadcrumb dump with the
  `GE007_WATCHDOG_TEST` negative control available. Tee×crash interaction (review
  F2): a fatal write into the tee *pipe* would race the reader thread's death at
  process exit (and block on a full pipe), so on Windows the fatal writers bypass
  the pipe entirely — `DiagLog_install` publishes the saved real-console fd and
  mgb64.log's raw fd (`g_diagLogRealErrFd`/`g_diagLogFileFd`), and
  `pc_diag_write_stderr` raw-`_write`s those directly; the SEH filter then ends in
  `TerminateProcess` (no ExitProcess/loader-lock machinery in a crashed process).
  Watchdog *stall* dumps (process alive, reader alive) flow through the tee normally.
  Accepted tee degradations, for the record: if another process holds
  `mgb64.prev.log` open, rotation loses the previous log (current log still opens);
  two concurrent instances share `mgb64.log` (CRT `_SH_DENYNO`) and interleave;
  console QuickEdit select can pause console writes (any console app).
- **SDL2 specifics:** GL 3.3 core over WGL via plain `SDL_CreateWindow(OPENGL)` +
  glad loader (the standard SDL Windows path); Metal correctly `__APPLE__`-gated
  incl. the `gfx_metal_set_vsync` guard; audio via `SDL_OpenAudioDevice(NULL, …)`
  (WASAPI under SDL); controllers via the SDL_GameController API only — XInput
  (ROG Ally's mode) is SDL's best-supported Windows backend, no raw joystick or
  platform-specific input code. `SDL_MAIN_HANDLED` + `SDL_SetMainReady()` +
  a plain `int main()` is the documented SDL entry pattern. **Updated:** as of
  the subsystem flip (§6) the Release build links the **GUI subsystem**
  (`-mwindows`, no console window on launch); `mgb64.log` is the log sink and
  `-DMGB64_WIN_CONSOLE=ON` restores the console subsystem for debugging.

## 4. Verified by execution

- **MW.2 cross lane (compile/link truth):** `tools/mingw_cross_check.sh` — PASS,
  zero errors, on the exact CMake config the release CI ships. Re-run in this
  worktree before (61 warnings, reproducing MW.2's census exactly) and after
  (58, PASS) this pass's fixes. This retires: missing-symbol risk for every unguarded
  call in the tree, header availability, the `winpthread`/COM link set, and the
  `-mno-ms-bitfields`/`-fno-strict-aliasing` flag plumbing.
- **Prior field evidence:** v0.3.x ran on player hardware (the v0.3.2 bitfield crash
  was found *and its fix confirmed* by an external Windows report); the shipping
  Windows zip is produced by the same CMake target this lane builds.
- **macOS regression rail for this pass:** native worktree build clean,
  `ctest -R sim_state_hash` green; every fix is Windows-branch-only, comment-only, or
  a diagnostic format printing identical text on LP64.

## 5. Untested on Windows, with rationale

| Gap | Why we believe it holds | Retirement lane |
|---|---|---|
| M6.1 tee / M6.2 SEH filter / watchdog **firing at runtime** | Mechanical CRT/Win32 idioms (`_pipe`+`_dup2`, `SetUnhandledExceptionFilter`, `_open/_write`); code compiles against real headers; POSIX twins are field-proven | MW.4 (Wine: tee+watchdog; **not** SEH — Wine's SEH isn't the Windows kernel's), MW.5 (real kernel), or first Windows contributor run |
| **Crash dump delivery under the tee** — the direct-fd mirror (§3) is the mechanism that makes `[CRASH]` reach mgb64.log/console; it is unexercised on a real kernel | Raw `_write` to already-open fds is the narrowest possible crash-time IO; no pipe, no reader thread, no stdio, no allocation | MW.4/MW.5 must test the **teed** crash path specifically (interactive shell + forced fault), not just the headless one. On POSIX the pre-existing pipe race remains (out of MW scope — POSIX diag backlog) |
| **Windows stack-overflow faults die silently** — the SEH filter runs on the faulting thread's exhausted stack (`head[128]`+`diag[768]`+snprintf frames will usually double-fault on `STATUS_STACK_OVERFLOW`); POSIX covers this exact case with `sigaltstack` | Every *other* fault class still gets diagnostics; stack overflow is rare in a fixed-arena engine; fix (`SetThreadStackGuarantee`) is known and cheap if MW.5 shows it matters | Attested asymmetry, not scheduled this cycle |
| WGL driver behavior (GL 3.3 core shaders, vsync, fullscreen-desktop) | SDL's most-traveled Windows path; shaders are GLSL 330 core with no vendor extensions; prior v0.3.x field runs rendered | MW.3 (headless smoke on windows-latest), MW.5 (visual) |
| WASAPI audio timing under the 22050 Hz pull model | SDL converts/paces internally; same callback code runs CoreAudio today | MW.5 / field |
| XInput on ROG Ally specifically | SDL GameController abstracts it; Ally presents a standard XInput pad | MC sprint hardware pass |
| msvcrt-vs-UCRT runtime differences (CI artifact links msvcrt) | §3 format-posture analysis; no other CRT-divergent API in use (fsync→`_commit`, rename→`MoveFileExA` already explicit) | Consider migrating CI to MSYS2 `UCRT64` to match the locally-tested runtime |
| `-Wmaybe-uninitialized` decomp sites being genuine sim bugs | Identical on all platforms — not a *Windows* risk by definition | Sim-correctness backlog (not MW) |

**Bottom line:** compile/link is a fact (MW.2, reproduced here); every known
Windows-pitfall class has been swept with 6 concrete fixes landed (M6.1, M6.2,
watchdog, LLP64 formats, PATH_MAX truncation, math-shim gap); what remains untested
is runtime-only and is exactly what MW.3–MW.5 exist to execute.

---

## 6. Subsystem flip: console → GUI (`-mwindows`), with the fd re-analysis MW.1 required

MW.1 kept the console subsystem but flagged that any flip must **re-run the fd
analysis**, because under the GUI subsystem "pre-install printf output would be
lost and install order becomes load-bearing." That analysis, and the decision:

**Decision — FLIP to the GUI subsystem by default.** Release Windows builds now
link `-mwindows` (PE subsystem = Windows GUI), so no empty console window appears
when the exe is launched from Explorer or a game library. A console build stays
one CMake flag away: `-DMGB64_WIN_CONSOLE=ON` restores the console subsystem for
debugging. Both variants were cross-linked once in the MW.2 lane to prove each
links (GUI = default, CUI = option ON). MinGW keeps `mainCRTStartup → int main()`
under `-mwindows` (no `WinMain` needed; `SDL_MAIN_HANDLED` means SDL injects no
`WinMain`), verified against the toolchain.

**The fd re-analysis (the crux MW.1 named).** With no console, fds 0–2 are invalid
at CRT startup. `DiagLog_install()` (src/app/diag_log.cpp) was written to *create*
its fds, not duplicate invalid ones, so it holds:

- `g_realErr = _dup(2)` → fd 2 invalid ⇒ returns **-1**. The console write-through
  is guarded `if (g_realErr >= 0)`, so it degrades to a no-op; the published
  crash-mirror fd `g_diagLogRealErrFd` is likewise -1, and the SEH/crash writer
  simply skips the (absent) console and still writes `g_diagLogFileFd` (mgb64.log).
- `_pipe(pfd, …)` returns **two fresh valid fds** regardless of console state.
- `_dup2(pfd[1], 1)` / `_dup2(pfd[1], 2)` — `_dup2` does **not** require the target
  fd to be currently open; it *assigns* it. So this **creates** valid fds 1 and 2
  pointing at the pipe. After it, `printf`/`fprintf(stderr)` (bound to fds 1/2)
  flow into the pipe → reader thread → `mgb64.log`. This is exactly the "create
  rather than duplicate invalid fds" property MW.1 asked to verify.

Net: under `-mwindows`, `mgb64.log` capture is preserved by construction; only the
*live console mirror* (a bonus) drops out, gracefully.

**Install order (now load-bearing).** `DiagLog_install()` was moved to run **before**
`host.init()` in `src/app/main_app.cpp`. The only code that could emit before the
tee is now `mgb_is_automation_invocation()` (which prints nothing) and the tee
itself; the previously-exposed window — `host.init()`'s fatal
`[app] SDL_Init/CreateWindow/GL/ImGui failed` diagnostics — is now captured in
`mgb64.log` instead of being lost to the absent console. `DiagLog_install` depends
only on `SDL_GetPrefPath` + a pipe (no `SDL_Init`), so running it first is safe on
all platforms (macOS/Linux gain the same early-capture).

**Automation path under `-mwindows` (accepted).** Automation/CLI invocations
(`mgb64_headless_main`) return before the tee is installed and deliberately stay
byte-identical (no redirection). Under the GUI subsystem their stdout/stderr reach
inherited handles: **valid** when spawned by a shell/CI with a pipe or `>`
redirect (so `ctest`, the byte-identity harness, and MW.3 all still capture
output), **absent** only when an automation flag is run by double-click — a
non-real case. A developer wanting live automation logs uses `-DMGB64_WIN_CONSOLE=ON`
or shell redirection. Documented, not a regression for any shipping path.

**What is *still* runtime-untested (unchanged from §5, but now higher-stakes).**
The tee itself has never executed on a real Windows kernel (§5, MW.4/MW.5). Under
`-mwindows` the tee is the *sole* log path (no console fallback), so a first
hardware run should confirm `mgb64.log` receives engine output and that a forced
fault's `[CRASH]` block lands there. The fd mechanics above are sound by
construction; only their runtime behavior on a real kernel remains to be observed.
