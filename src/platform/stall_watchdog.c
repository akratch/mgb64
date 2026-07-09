/**
 * stall_watchdog.c — BUG-1 silent-freeze capture rig. See stall_watchdog.h.
 *
 * Design constraints (docs/RENDERER_SIM_AUDIT + BUG-1 investigation):
 *  - The freeze signature is a silent wedge (sim infinite loop or GL/driver
 *    present wedge), so the dump must not depend on the wedged thread:
 *    everything runs on a dedicated watchdog thread.
 *  - Thread backtraces come from spawning `/usr/bin/sample <pid> 2 -file ...`
 *    (macOS). One approach, chosen over in-process unwinding because: (a) it
 *    needs no async-signal-unsafe work inside a process whose heap/locks may
 *    be corrupt or held by the wedged thread, (b) it captures ALL threads
 *    including GPU-driver worker threads wedged in Metal/GL (the #1 field
 *    hypothesis), and (c) a failure to sample cannot take the game down with
 *    it. posix_spawn (no fork of the possibly-corrupt address space on
 *    macOS) + waitpid on the watchdog thread.
 *  - Stall detection counts consecutive unchanged-heartbeat watchdog wakes
 *    instead of comparing wall-clock timestamps: under a debugger/instrument
 *    pause the watchdog thread is suspended too, so wakes don't accrue and
 *    resume does not fire a false stall. (A pause targeting only the main
 *    thread still false-positives; accepted.)
 *  - stderr writes in the dump path use write(2) into a private buffer, never
 *    stdio, so a sim thread wedged while holding the stdio lock can't wedge
 *    the watchdog as well. stderr is tee'd into mgb64.log by the app shell's
 *    diag log; the same block is also appended to <savedir>/stall_watchdog.log
 *    so launches without the shell keep the evidence too.
 */
#include "stall_watchdog.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if defined(_WIN32)

/* Windows: clean no-op. The watchdog's capture path depends on the diag-log
 * tee and crash diagnostics, both currently stubbed on Windows (backlog M6.1
 * diag-log tee, M6.2 crash-handler rework); the Windows watchdog lands with
 * that lane. */
void portWatchdogInit(void) {}
void portWatchdogLoadBegin(void) {}
void portWatchdogLoadEnd(void) {}
void portWatchdogFrameTick(void) {}

#else /* POSIX */

#include <SDL.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#if defined(__APPLE__)
#include <spawn.h>
extern char **environ;
#endif

#include "port_env.h"
#include "savedir.h"

/* Sim-loop diagnostics the stall block reports (all read-only here). */
extern int g_frame_count_diag;   /* render frame ordinal (gfx_pc.c) */
extern int g_StageNum;           /* current stage (boss.c) */

static SDL_atomic_t s_heartbeat;      /* published once per frame */
static SDL_atomic_t s_running;        /* 1 = frame loop, 0 = load/init */

static int  s_armed = 0;              /* watchdog enabled + thread started */
static int  s_stallSecs = 5;
static int  s_testStall = 0;
static char s_stallLogPath[1024];
static char s_samplePath[2][1024];

/* ---- dump path (watchdog thread only) ---------------------------------- */

/* Private append buffer: the whole dump is formatted here and flushed with
 * write(2) so the dump path never takes a stdio lock. Watchdog-thread-only,
 * so a single static buffer is safe. */
static char s_dumpBuf[48 * 1024];
static int  s_dumpLen = 0;

static void dumpReset(void) {
    s_dumpLen = 0;
}

static void dumpf(const char *fmt, ...) {
    va_list ap;
    int room = (int)sizeof(s_dumpBuf) - s_dumpLen;
    int n;

    if (room <= 1) {
        return;
    }
    va_start(ap, fmt);
    n = vsnprintf(s_dumpBuf + s_dumpLen, (size_t)room, fmt, ap);
    va_end(ap);
    if (n > 0) {
        s_dumpLen += (n < room) ? n : (room - 1);
    }
}

static void dumpFlush(void) {
    ssize_t w;
    int fd;

    if (s_dumpLen <= 0) {
        return;
    }
    w = write(STDERR_FILENO, s_dumpBuf, (size_t)s_dumpLen);
    (void)w;

    fd = open(s_stallLogPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        w = write(fd, s_dumpBuf, (size_t)s_dumpLen);
        (void)w;
        close(fd);
    }
}

static void dumpSpawnSample(int dump_no) {
#if defined(__APPLE__)
    char pidbuf[16];
    const char *path = s_samplePath[dump_no - 1];
    char *argv[] = { "/usr/bin/sample", pidbuf, "2", "-file", (char *)path, NULL };
    pid_t child = -1;
    int rc;

    snprintf(pidbuf, sizeof(pidbuf), "%d", (int)getpid());
    rc = posix_spawn(&child, "/usr/bin/sample", NULL, NULL, argv, environ);
    if (rc == 0) {
        int status = 0;
        dumpf("[WATCHDOG] sampling all threads -> %s (2s)...\n", path);
        dumpFlush();
        dumpReset();
        waitpid(child, &status, 0);
        dumpf("[WATCHDOG] sample done (status %d)\n", status);
    } else {
        dumpf("[WATCHDOG] posix_spawn(/usr/bin/sample) failed: %d\n", rc);
    }
#else
    (void)dump_no;
    dumpf("[WATCHDOG] thread sampling unavailable on this platform "
          "(stall block only)\n");
#endif
}

static void watchdogDumpStall(Uint32 hb_now, int stalled_secs, int dump_no) {
    dumpReset();
    dumpf("\n[WATCHDOG] SIM STALL: heartbeat frozen for ~%ds "
          "(hb=%u frame=%d stage=%d, dump %d/2)\n",
          stalled_secs, (unsigned)hb_now, g_frame_count_diag, g_StageNum, dump_no);
    dumpSpawnSample(dump_no);
    dumpf("[WATCHDOG] end of stall dump %d/2 (process left running for capture)\n\n",
          dump_no);
    dumpFlush();
    dumpReset();
}

/* ---- watchdog thread ---------------------------------------------------- */

static int watchdogThreadMain(void *arg) {
    Uint32 last_hb = (Uint32)SDL_AtomicGet(&s_heartbeat);
    int stalled = 0;
    int dumps = 0;

    (void)arg;
    for (;;) {
        Uint32 hb;

        SDL_Delay(1000);

        if (!SDL_AtomicGet(&s_running)) {
            /* load/init phase: loads legitimately block the main loop */
            last_hb = (Uint32)SDL_AtomicGet(&s_heartbeat);
            stalled = 0;
            dumps = 0;
            continue;
        }

        hb = (Uint32)SDL_AtomicGet(&s_heartbeat);
        if (hb != last_hb) {
            last_hb = hb;
            stalled = 0;
            dumps = 0; /* stall cleared: re-arm for the next one */
            continue;
        }

        stalled++;
        if (dumps == 0 && stalled >= s_stallSecs) {
            dumps = 1;
            watchdogDumpStall(hb, stalled, 1);
        } else if (dumps == 1 && stalled >= 2 * s_stallSecs) {
            dumps = 2;
            watchdogDumpStall(hb, stalled, 2);
        }
        /* dumps == 2: stay silent until the heartbeat advances again */
    }
    return 0;
}

/* ---- hooks (main/sim thread) -------------------------------------------- */

void portWatchdogInit(void) {
    SDL_Thread *thread;

    if (port_env_bool("GE007_NO_WATCHDOG", 0,
            "disable the sim stall watchdog (heartbeat monitor + stall dump)")) {
        return;
    }
    s_stallSecs = port_env_int("GE007_WATCHDOG_STALL_SECS", 5,
        "seconds without a sim heartbeat before the stall watchdog dumps (default 5)");
    if (s_stallSecs < 2) {
        s_stallSecs = 2; /* below the 1s wake granularity the count is noise */
    }
    s_testStall = port_env_bool("GE007_WATCHDOG_TEST", 0,
        "negative control: synthetically stall the sim thread at frame ~600 to prove the watchdog capture path");

    /* savedirPath returns a static buffer and is not thread-safe: resolve
     * every path the watchdog thread will ever need here, on the main
     * thread, before the thread exists. */
    snprintf(s_stallLogPath, sizeof(s_stallLogPath), "%s", savedirPath("stall_watchdog.log"));
    snprintf(s_samplePath[0], sizeof(s_samplePath[0]), "%s", savedirPath("stall_sample_1.txt"));
    snprintf(s_samplePath[1], sizeof(s_samplePath[1]), "%s", savedirPath("stall_sample_2.txt"));

    SDL_AtomicSet(&s_heartbeat, 0);
    SDL_AtomicSet(&s_running, 0);

    thread = SDL_CreateThread(watchdogThreadMain, "ge007-stall-watchdog", NULL);
    if (thread == NULL) {
        fprintf(stderr, "[WATCHDOG] SDL_CreateThread failed: %s (watchdog disabled)\n",
                SDL_GetError());
        return;
    }
    SDL_DetachThread(thread);
    s_armed = 1;
}

void portWatchdogLoadBegin(void) {
    if (!s_armed) {
        return;
    }
    SDL_AtomicSet(&s_running, 0);
}

void portWatchdogLoadEnd(void) {
    if (!s_armed) {
        return;
    }
    SDL_AtomicSet(&s_running, 1);
}

void portWatchdogFrameTick(void) {
    Uint32 hb;

    if (!s_armed) {
        return;
    }

    hb = (Uint32)SDL_AtomicGet(&s_heartbeat);
    SDL_AtomicSet(&s_heartbeat, (int)(hb + 1));

    if (s_testStall && hb == 600) {
        int sleep_ms = (3 * s_stallSecs + 2) * 1000;
        fprintf(stderr, "[WATCHDOG-TEST] synthetic sim stall: sleeping %d ms at heartbeat %u\n",
                sleep_ms, (unsigned)hb);
        SDL_Delay((Uint32)sleep_ms);
        fprintf(stderr, "[WATCHDOG-TEST] synthetic stall over; sim resumes\n");
    }
}

#endif /* _WIN32 */
