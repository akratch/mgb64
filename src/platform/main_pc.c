/**
 * main_pc.c — PC entry point for the GoldenEye native port.
 *
 * Replaces the N64 boot chain (boot.s → init() → mainproc() → bossEntry()).
 * On PC we initialize SDL2 + ROM, then set up the scheduler and call
 * bossEntry() which runs the infinite bossMainloop() loop.
 */

#include <ultra64.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <dirent.h>
#include "rom_io.h"
#include "savedir.h"
#include "bondconstants.h"
#include "game/ramromreplay.h"

#ifndef __has_feature
#define __has_feature(x) 0
#endif

uintptr_t g_lastMtxAddr = 0;
int g_mtxCallCount = 0;

/* Level selection from command line: -1 = default (menu), else raw LEVELID */
int g_pcStartLevel = -1;
int g_pcStartDifficulty = DIFFICULTY_AGENT;
const char *g_pcStartRamrom = NULL;
static int g_pcStartLevelForcedRaw = 0;

static inline void pc_diag_write_stderr(const char *msg, int len)
{
    if (len > 0) {
        ssize_t written = write(STDERR_FILENO, msg, (size_t)len);
        (void)written;
    }
}

typedef struct PcStartStage {
    int mission_num;      /* 1-based solo campaign order */
    int level_id;         /* raw LEVELID enum value */
    const char *slug;     /* CLI-friendly name */
    const char *name;     /* display name */
} PcStartStage;

static const PcStartStage kPcStartStages[] = {
    {  1, LEVELID_DAM,      "dam",       "Dam" },
    {  2, LEVELID_FACILITY, "facility",  "Facility" },
    {  3, LEVELID_RUNWAY,   "runway",    "Runway" },
    {  4, LEVELID_SURFACE,  "surface1",  "Surface 1" },
    {  5, LEVELID_BUNKER1,  "bunker1",   "Bunker 1" },
    {  6, LEVELID_SILO,     "silo",      "Silo" },
    {  7, LEVELID_FRIGATE,  "frigate",   "Frigate" },
    {  8, LEVELID_SURFACE2, "surface2",  "Surface 2" },
    {  9, LEVELID_BUNKER2,  "bunker2",   "Bunker 2" },
    { 10, LEVELID_STATUE,   "statue",    "Statue" },
    { 11, LEVELID_ARCHIVES, "archives",  "Archives" },
    { 12, LEVELID_STREETS,  "streets",   "Streets" },
    { 13, LEVELID_DEPOT,    "depot",     "Depot" },
    { 14, LEVELID_TRAIN,    "train",     "Train" },
    { 15, LEVELID_JUNGLE,   "jungle",    "Jungle" },
    { 16, LEVELID_CONTROL,  "control",   "Control" },
    { 17, LEVELID_CAVERNS,  "caverns",   "Caverns" },
    { 18, LEVELID_CRADLE,   "cradle",    "Cradle" },
    { 19, LEVELID_AZTEC,    "aztec",     "Aztec" },
    { 20, LEVELID_EGYPT,    "egypt",     "Egyptian" },
};

static const PcStartStage *pcFindStageByLevelId(int level_id) {
    size_t i;
    for (i = 0; i < sizeof(kPcStartStages) / sizeof(kPcStartStages[0]); i++) {
        if (kPcStartStages[i].level_id == level_id) {
            return &kPcStartStages[i];
        }
    }
    return NULL;
}

static const PcStartStage *pcFindStageByMission(int mission_num) {
    size_t i;
    for (i = 0; i < sizeof(kPcStartStages) / sizeof(kPcStartStages[0]); i++) {
        if (kPcStartStages[i].mission_num == mission_num) {
            return &kPcStartStages[i];
        }
    }
    return NULL;
}

static const PcStartStage *pcFindStageByName(const char *name) {
    size_t i;
    if (!name || !*name) return NULL;

    for (i = 0; i < sizeof(kPcStartStages) / sizeof(kPcStartStages[0]); i++) {
        if (strcasecmp(kPcStartStages[i].slug, name) == 0 ||
            strcasecmp(kPcStartStages[i].name, name) == 0) {
            return &kPcStartStages[i];
        }
    }

    if (strcasecmp(name, "surface") == 0) return pcFindStageByLevelId(LEVELID_SURFACE);
    if (strcasecmp(name, "bunker") == 0) return pcFindStageByLevelId(LEVELID_BUNKER1);
    if (strcasecmp(name, "egyptian") == 0) return pcFindStageByLevelId(LEVELID_EGYPT);
    return NULL;
}

static int pcParseIntArg(const char *arg, int *out_value) {
    char *end = NULL;
    long value;

    if (!arg || !*arg) return 0;
    value = strtol(arg, &end, 10);
    if (end == arg || *end != '\0') return 0;
    *out_value = (int)value;
    return 1;
}

static int pcParseDifficultyArg(const char *arg, int *out_value) {
    int parsed;

    if (!arg || !*arg) return 0;

    if (strcasecmp(arg, "agent") == 0) {
        *out_value = DIFFICULTY_AGENT;
        return 1;
    }
    if (strcasecmp(arg, "secret") == 0 ||
        strcasecmp(arg, "secretagent") == 0 ||
        strcasecmp(arg, "sa") == 0) {
        *out_value = DIFFICULTY_SECRET;
        return 1;
    }
    if (strcasecmp(arg, "00") == 0 ||
        strcasecmp(arg, "00agent") == 0 ||
        strcasecmp(arg, "double0") == 0 ||
        strcasecmp(arg, "double0agent") == 0) {
        *out_value = DIFFICULTY_00;
        return 1;
    }
    if (strcasecmp(arg, "007") == 0 ||
        strcasecmp(arg, "007mode") == 0 ||
        strcasecmp(arg, "007-mode") == 0) {
        *out_value = DIFFICULTY_007;
        return 1;
    }

    if (pcParseIntArg(arg, &parsed)) {
        if (parsed >= DIFFICULTY_AGENT && parsed <= DIFFICULTY_007) {
            *out_value = parsed;
            return 1;
        }
        return 0;
    }

    return 0;
}

#include <setjmp.h>
sigjmp_buf g_gfxRecoveryJmp;
volatile int g_gfxRecoveryActive = 0;
int g_crashRecoveryCount = 0;  /* non-static: used by platformShutdownSDL */

static void crashHandler(int sig) {
    /* If we're in DL processing and recovery is available, skip this frame.
     * NOTE: this handler avoids fprintf/printf (which use locks that can
     * deadlock if the signal interrupted stdio). We use write() for output
     * and snprintf() for formatting. snprintf() is not strictly
     * async-signal-safe per POSIX, but is safe in practice on macOS/glibc
     * when writing to a stack buffer (no heap allocation or locks). */
    /* In debug/investigation builds, allow many recoveries to gather data.
     * In release, cap low — a build that needs recovery is not green.
     * CMake defines NDEBUG in Release builds; its absence means debug. */
#ifdef NDEBUG
#define MAX_CRASH_RECOVERY 10
#else
#define MAX_CRASH_RECOVERY 10000
#endif
    if (g_gfxRecoveryActive && g_crashRecoveryCount < MAX_CRASH_RECOVERY) {
        g_crashRecoveryCount++;
        extern volatile uintptr_t g_lastDlCmd;
        extern volatile uint32_t g_lastDlOpcode, g_lastDlW0;
        extern volatile uintptr_t g_lastDlW1;
        extern volatile uintptr_t g_diag_tex_addr;
        extern volatile uint32_t g_diag_tex_size_bytes, g_diag_tex_needed;
        extern volatile uint8_t g_diag_tex_fmt, g_diag_tex_siz, g_diag_tex_slot, g_diag_tex_tile;
        char msg[512];
        int n = snprintf(msg, sizeof(msg),
            "[GFX-RECOVER] sig=%d #%d op=0x%02X w0=0x%08X w1=0x%lX cmd=%p"
            " tex_addr=%p tex_sz=%u tex_need=%u tex_fmt=%u tex_siz=%u tex_slot=%u tex_tile=%u\n",
            sig, g_crashRecoveryCount,
            g_lastDlOpcode, g_lastDlW0, (unsigned long)g_lastDlW1,
            (void*)g_lastDlCmd,
            (void*)g_diag_tex_addr, g_diag_tex_size_bytes, g_diag_tex_needed,
            g_diag_tex_fmt, g_diag_tex_siz, g_diag_tex_slot, g_diag_tex_tile);
        pc_diag_write_stderr(msg, n);
        g_gfxRecoveryActive = 0;
        siglongjmp(g_gfxRecoveryJmp, 1);
    }
    {
        extern volatile uintptr_t g_lastDlCmd;
        extern volatile uint32_t g_lastDlOpcode, g_lastDlW0;
        extern volatile uintptr_t g_lastDlW1;
        extern volatile uintptr_t g_diag_current_cmd_addr;
        extern int g_frame_count_diag;
        extern volatile uintptr_t g_diag_tex_addr;
        extern volatile uint32_t g_diag_tex_size_bytes, g_diag_tex_needed;
        extern volatile uint8_t g_diag_tex_fmt, g_diag_tex_siz, g_diag_tex_slot, g_diag_tex_tile;
        char diag[768];
        int dlen = snprintf(diag, sizeof(diag),
            "\n[CRASH] Signal %d (unrecoverable)\n"
            "[CRASH-DL] frame=%d op=0x%02X w0=0x%08X w1=0x%lX cmd=%p diag_cmd=%p\n"
            "[CRASH-TEX] addr=%p size_bytes=%u needed=%u fmt=%u siz=%u slot=%u tile=%u\n",
            sig, g_frame_count_diag,
            g_lastDlOpcode, g_lastDlW0, (unsigned long)g_lastDlW1,
            (void*)g_lastDlCmd, (void*)g_diag_current_cmd_addr,
            (void*)g_diag_tex_addr, g_diag_tex_size_bytes, g_diag_tex_needed,
            g_diag_tex_fmt, g_diag_tex_siz, g_diag_tex_slot, g_diag_tex_tile);
        pc_diag_write_stderr(diag, dlen);
    }
    void *bt[32];
    int n = backtrace(bt, 32);
    backtrace_symbols_fd(bt, n, STDERR_FILENO);
    _exit(1);
}

/* Game entry — defined in boss.c */
extern void bossEntry(void);

/* Scheduler init — called by mainproc() on N64, we call it explicitly on PC.
 * Creates gfxFrameMsgQ, initializes the scheduler, registers the gfx client. */
extern void schedulerInitThread(void);

/* SDL platform — defined in platform_sdl.c */
extern int  platformInitSDL(void);
extern void platformShutdownSDL(void);
extern void platformSetScreenshotLabel(const char *label);
extern void pcSetTraceStatePath(const char *path);

/* GBI translator — defined in gfx_pc.c */
extern void gfx_init(void);

/* Audio backend — defined in audio_pc.c */
extern void portAudioInit(void);
extern void portAudioRegisterConfig(void);

/* Config system — defined in config_pc.c */
extern void configInit(void);

/* Platform config — defined in platform_sdl.c */
extern void platformRegisterConfig(void);

/* Default ROM path — can be overridden via command line */
static const char *DEFAULT_ROM_PATHS[] = {
    "baserom.u.z64",
    "GoldenEye 007 (USA).z64",
    "ge007.z64",
    "goldeneye.z64",
    "../GoldenEye 007 (USA).z64",
    "../baserom.u.z64",
    "../../GoldenEye 007 (USA).z64",
    NULL
};

/* N64 ROM size for all GoldenEye regions (0xC00000). */
#define GE_ROM_SIZE 12582912L

/* Heuristic: does `path` look like a GoldenEye .z64 ROM? Checks size, the
 * big-endian N64 bootstrap magic, and the "GOLDENEYE" internal cartridge name.
 * No SHA-1 needed — this is just for zero-config auto-detection; an explicit
 * --rom always bypasses it. Avoids auto-loading some unrelated N64 ROM. */
static int looksLikeGoldeneyeRom(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long sz = ftell(f);
    if (sz != GE_ROM_SIZE) { fclose(f); return 0; }
    unsigned char hdr[0x40];
    rewind(f);
    size_t n = fread(hdr, 1, sizeof(hdr), f);
    fclose(f);
    if (n != sizeof(hdr)) return 0;
    if (!(hdr[0] == 0x80 && hdr[1] == 0x37 && hdr[2] == 0x12 && hdr[3] == 0x40))
        return 0; /* not a big-endian (.z64) N64 ROM */
    for (int i = 0x20; i + 9 <= 0x34; i++) {
        if (memcmp(&hdr[i], "GOLDENEYE", 9) == 0) return 1;
    }
    return 0;
}

/* Scan one directory for a GoldenEye ROM; on first match writes the full path
 * to `out` and returns 1. */
static int scanDirForRom(const char *dir, char *out, size_t outsz) {
    if (!dir || !*dir) return 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while ((e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);
        if (len < 5) continue;
        const char *ext = e->d_name + len - 4;
        if (strcasecmp(ext, ".z64") != 0 && strcasecmp(ext, ".n64") != 0 &&
            strcasecmp(ext, ".v64") != 0)
            continue;
        char cand[1100];
        snprintf(cand, sizeof(cand), "%s/%s", dir, e->d_name);
        if (looksLikeGoldeneyeRom(cand)) {
            snprintf(out, outsz, "%s", cand);
            found = 1;
            break;
        }
    }
    closedir(d);
    return found;
}

static const char *findRomFile(void) {
    /* 1) Known names next to the executable / in parent dirs (fast path). */
    for (int i = 0; DEFAULT_ROM_PATHS[i]; i++) {
        FILE *f = fopen(DEFAULT_ROM_PATHS[i], "rb");
        if (f) {
            fclose(f);
            return DEFAULT_ROM_PATHS[i];
        }
    }
    /* 2) Zero-config auto-detect: scan common locations for a GoldenEye ROM. */
    static char s_found[1100];
    if (scanDirForRom(".", s_found, sizeof(s_found))) {
        printf("[ROM] Auto-detected ROM: %s\n", s_found);
        return s_found;
    }
    const char *home = getenv("HOME");
    if (home && *home) {
        const char *rel[] = { "Downloads", "Documents", "Desktop", NULL };
        char dir[1100];
        for (int i = 0; rel[i]; i++) {
            snprintf(dir, sizeof(dir), "%s/%s", home, rel[i]);
            if (scanDirForRom(dir, s_found, sizeof(s_found))) {
                printf("[ROM] Auto-detected ROM: %s\n", s_found);
                return s_found;
            }
        }
        if (scanDirForRom(home, s_found, sizeof(s_found))) {
            printf("[ROM] Auto-detected ROM: %s\n", s_found);
            return s_found;
        }
    }
    return NULL;
}

/* When building as a macOS app bundle, the Swift AppDelegate owns the entry
 * point and calls game_init()/game_run() via GameBridge.h instead. */
#ifndef MACOS_APP_BUNDLE
int main(int argc, char **argv)
{
    const char *romPath = NULL;
    const char *saveDirOverride = NULL;
    extern int g_autoScreenshotExit;
    extern const char *g_traceStatePath;

    /* Force line-buffered stdout so crash diagnostics appear */
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
    signal(SIGSEGV, crashHandler);
#if !defined(__SANITIZE_ADDRESS__) && !__has_feature(address_sanitizer)
    /* ASAN uses SIGBUS internally for shadow memory probing.
     * Don't intercept it in sanitizer builds. */
    signal(SIGBUS, crashHandler);
#endif
    signal(SIGABRT, crashHandler);

    /* Parse command line */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--rom") == 0 && i + 1 < argc) {
            romPath = argv[++i];
        } else if (strcmp(argv[i], "--background") == 0) {
            setenv("GE007_BACKGROUND", "1", 1);
            setenv("GE007_NO_INPUT_GRAB", "1", 1);
        } else if (strcmp(argv[i], "--no-input-grab") == 0) {
            setenv("GE007_NO_INPUT_GRAB", "1", 1);
        } else if (strcmp(argv[i], "--screenshot-frame") == 0 && i + 1 < argc) {
            extern int g_autoScreenshotFrame;
            g_autoScreenshotFrame = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--screenshot-exit") == 0) {
            g_autoScreenshotExit = 1;
        } else if (strcmp(argv[i], "--screenshot-label") == 0 && i + 1 < argc) {
            platformSetScreenshotLabel(argv[++i]);
        } else if (strcmp(argv[i], "--freeze-input") == 0) {
            extern int g_freezeInput;
            g_freezeInput = 1;
        } else if (strcmp(argv[i], "--deterministic") == 0) {
            extern int g_deterministic;
            extern int g_freezeInput;
            g_deterministic = 1;
            g_freezeInput = 1; /* deterministic implies frozen input */
        } else if (strcmp(argv[i], "--trace-state") == 0 && i + 1 < argc) {
            pcSetTraceStatePath(argv[++i]);
        } else if (strcmp(argv[i], "--mission") == 0 && i + 1 < argc) {
            int mission = 0;
            const PcStartStage *stage;
            if (!pcParseIntArg(argv[++i], &mission) || !(stage = pcFindStageByMission(mission))) {
                fprintf(stderr, "[GE007-PC] Invalid --mission value. Use 1-20 for the solo campaign.\n");
                return 2;
            }
            g_pcStartLevel = stage->level_id;
        } else if (strcmp(argv[i], "--level") == 0 && i + 1 < argc) {
            const char *arg = argv[++i];
            const PcStartStage *stage = pcFindStageByName(arg);
            int raw_level_id = 0;

            if (stage != NULL) {
                g_pcStartLevel = stage->level_id;
            } else if (pcParseIntArg(arg, &raw_level_id)) {
                stage = pcFindStageByLevelId(raw_level_id);
                if (!stage) {
                    const PcStartStage *mission = pcFindStageByMission(raw_level_id);
                    if (mission) {
                        fprintf(stderr,
                                "[GE007-PC] --level %d is a raw LEVELID, not solo mission %d.\n"
                                "[GE007-PC] If you meant %s, use --mission %d or --level %s (raw LEVELID %d).\n",
                                raw_level_id, raw_level_id,
                                mission->name, mission->mission_num, mission->slug, mission->level_id);
                    } else {
                        fprintf(stderr,
                                "[GE007-PC] Raw LEVELID %d is not a supported direct-boot solo mission.\n"
                                "[GE007-PC] Use --stage-id %d to force an internal stage id.\n",
                                raw_level_id, raw_level_id);
                    }
                    return 2;
                }
                g_pcStartLevel = raw_level_id;
            } else {
                fprintf(stderr, "[GE007-PC] Unknown level '%s'. Use a solo stage name like 'facility' or a raw LEVELID like 34.\n", arg);
                return 2;
            }
        } else if (strcmp(argv[i], "--stage-id") == 0 && i + 1 < argc) {
            g_pcStartLevel = atoi(argv[++i]);
            g_pcStartLevelForcedRaw = 1;
        } else if (strcmp(argv[i], "--difficulty") == 0 && i + 1 < argc) {
            if (!pcParseDifficultyArg(argv[++i], &g_pcStartDifficulty)) {
                fprintf(stderr, "[GE007-PC] Invalid --difficulty value. Use agent, secret, 00, 007, or 0-3.\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--ramrom") == 0 && i + 1 < argc) {
            g_pcStartRamrom = argv[++i];
            if (!pcRamromReplayNameIsValid(g_pcStartRamrom)) {
                fprintf(stderr,
                        "[GE007-PC] Invalid --ramrom value '%s'. Use a built-in symbol like ramrom_Facility_3 or an alias like facility3.\n",
                        g_pcStartRamrom);
                return 2;
            }
            g_pcStartLevel = -1;
        } else if (strcmp(argv[i], "--savedir") == 0 && i + 1 < argc) {
            saveDirOverride = argv[++i];
        } else if (argv[i][0] != '-') {
            romPath = argv[i];
        }
    }

    if ((g_autoScreenshotExit || g_traceStatePath != NULL)
        && getenv("GE007_BACKGROUND") == NULL
        && getenv("GE007_NO_INPUT_GRAB") == NULL
        && getenv("GE007_SHOW_AUTOMATION_WINDOW") == NULL) {
        setenv("GE007_BACKGROUND", "1", 1);
        setenv("GE007_NO_INPUT_GRAB", "1", 1);
    }

    if (!romPath) {
        romPath = findRomFile();
    }

    /* Phase 1: Initialize save directory (before anything that reads/writes files) */
    savedirInit(saveDirOverride);

    /* Phase 1b: Config system — register settings, then load ge007.ini */
    platformRegisterConfig();
    portAudioRegisterConfig();
    configInit();

    printf("[GE007-PC] Starting...\n");
    if (g_pcStartLevel >= 0) {
        const PcStartStage *stage = pcFindStageByLevelId(g_pcStartLevel);
        if (stage) {
            printf("[GE007-PC] Start stage: %s (mission %d, LEVELID %d)\n",
                   stage->name, stage->mission_num, stage->level_id);
        } else if (g_pcStartLevelForcedRaw) {
            printf("[GE007-PC] Start stage: raw internal LEVELID %d\n", g_pcStartLevel);
        }
    } else if (g_pcStartRamrom != NULL) {
        printf("[GE007-PC] Start RAMROM demo: %s\n", g_pcStartRamrom);
    }

    /* Phase 2: Load ROM file (fail-fast — game cannot function without ROM) */
    if (!romPath) {
        fprintf(stderr,
            "[GE007-PC] No GoldenEye ROM found.\n"
            "  * Place your ROM next to the executable (e.g. baserom.u.z64), or\n"
            "  * pass it explicitly:  ge007 --rom /path/to/your_rom.z64\n"
            "  Auto-detect also scans: ./, ~/Downloads, ~/Documents, ~/Desktop, ~\n"
            "  You must supply your own legally-dumped ROM (see DISCLAIMER.md).\n");
        return 1;
    }
    if (platformInitRom(romPath) != 0) {
        fprintf(stderr, "[GE007-PC] Failed to load ROM: %s\n", romPath);
        return 1;
    }
    platformPatchFileTable(g_romData);
    printf("[ROM] File table patched with %u-byte ROM\n", g_romSize);

    /* Phase 3: SDL2 window + OpenGL context */
    if (platformInitSDL() != 0) {
        fprintf(stderr, "[GE007-PC] SDL init failed, exiting\n");
        return 1;
    }

    /* Phase 4: Initialize SDL2 audio backend */
    portAudioInit();

    /* Phase 5: Initialize GBI display list translator */
    gfx_init();

    /* Initialize the N64 scheduler infrastructure.
     * On N64 this is called from mainproc() before bossEntry().
     * It creates gfxFrameMsgQ and registers the gfx scheduler client. */
    schedulerInitThread();

    printf("[GE007-PC] Entering game loop...\n");
    printf("[GE007-PC] Controls: WASD=move Mouse=look LClick=fire RClick=aim Scroll=weapon R=reload F=interact C=crouch Q/E=lean Esc=pause  [H for full list]\n");

    /* On N64: init() → mainproc() → schedulerInitThread() → bossEntry()
     * bossEntry() calls bossInitMainthreadData() then loops on bossMainloop().
     * bossMainloop() blocks on osRecvMesg(&gfxFrameMsgQ, ..., OS_MESG_BLOCK)
     * waiting for retrace messages from the scheduler.
     *
     * On PC, the scheduler thread never runs (osStartThread is a no-op).
     * Instead, osRecvMesg's blocking path calls platformFrameSync() which
     * sends retrace messages at ~60fps via SDL timing. */
    bossEntry();

    platformShutdownSDL();
    return 0;
}
#endif /* MACOS_APP_BUNDLE */
