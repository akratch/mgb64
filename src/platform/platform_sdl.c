/**
 * platform_sdl.c — SDL2 window, OpenGL context, and frame timing.
 *
 * Provides the display window and frame pacing for the PC port.
 * The N64's VI retrace interrupt is replaced by SDL frame timing.
 */
#include <ultra64.h>
#include <sched.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <SDL.h>
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <glad/glad.h>
#endif
#include "config_pc.h"
#include "gfx_pc.h"
#include "settings.h"
#include "game/front.h"
#include "game/initmenus.h"
#include "game/title.h"

/* Forward declarations */
void platformShutdownSDL(void);
void platformSetWindowTitle(const char *title);

/* ===== Gamepad state ===== */
/* Up to MAXCONTROLLERS pads are opened and bound to fixed player slots.
 * The mapping is stable: a pad keeps its slot for its lifetime, and a
 * hot-unplug frees only that slot (no reshuffling of other players). Slot 0
 * doubles as the keyboard/mouse player, so a pad opened first lands in slot 0
 * but the data[0] path still merges keyboard/mouse on top of it (see stubs.c).
 *
 * g_gameController/g_gameControllerID are kept as aliases for slot 0 so the
 * existing single-pad call sites (right-stick read, shutdown) keep working. */
typedef struct {
    SDL_GameController *handle;      /* NULL when the slot is free */
    SDL_JoystickID      instance_id; /* -1 when the slot is free */
    int                 slot;        /* fixed player slot, == array index */
} PlatformPad;

#define PLATFORM_MAX_PADS 4 /* matches N64 MAXCONTROLLERS */

static PlatformPad g_pads[PLATFORM_MAX_PADS];

SDL_GameController *g_gameController = NULL;
static SDL_JoystickID g_gameControllerID = -1;

/* Refresh the legacy single-pad aliases from slot 0. */
static void platformSyncPad0Alias(void) {
    g_gameController   = g_pads[0].handle;
    g_gameControllerID = g_pads[0].instance_id;
}

/* Open an SDL game controller (by joystick device index) into the first free
 * slot. Returns the slot used, or -1 if no slot is free or the open failed. */
static int platformOpenPad(int deviceIndex) {
    SDL_GameController *gc;
    SDL_JoystickID id;
    int slot;

    if (!SDL_IsGameController(deviceIndex)) {
        return -1;
    }

    id = SDL_JoystickGetDeviceInstanceID(deviceIndex);
    /* Reject duplicates: a controller already opened in some slot. */
    for (slot = 0; slot < PLATFORM_MAX_PADS; slot++) {
        if (g_pads[slot].handle && g_pads[slot].instance_id == id) {
            return -1;
        }
    }

    for (slot = 0; slot < PLATFORM_MAX_PADS; slot++) {
        if (!g_pads[slot].handle) {
            break;
        }
    }
    if (slot >= PLATFORM_MAX_PADS) {
        return -1; /* all slots full */
    }

    gc = SDL_GameControllerOpen(deviceIndex);
    if (!gc) {
        return -1;
    }

    g_pads[slot].handle      = gc;
    g_pads[slot].instance_id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));
    g_pads[slot].slot        = slot;
    printf("[SDL] Gamepad connected (P%d): %s\n", slot + 1, SDL_GameControllerName(gc));

    platformSyncPad0Alias();
    return slot;
}

/* Free the slot whose pad has the given instance id, without disturbing the
 * other slots (stable mapping survives hot-unplug). */
static void platformClosePadByInstance(SDL_JoystickID id) {
    int slot;
    for (slot = 0; slot < PLATFORM_MAX_PADS; slot++) {
        if (g_pads[slot].handle && g_pads[slot].instance_id == id) {
            printf("[SDL] Gamepad disconnected (P%d)\n", slot + 1);
            SDL_GameControllerClose(g_pads[slot].handle);
            g_pads[slot].handle      = NULL;
            g_pads[slot].instance_id = -1;
            break;
        }
    }
    platformSyncPad0Alias();
}

/* ===== Window state ===== */
SDL_Window   *g_sdlWindow  = NULL;  /* non-static: fast3d needs access for swap/dimensions */
static SDL_GLContext  g_glContext  = NULL;
static int g_sdlQuit = 0;
static int g_forceNoVsync = 0;
static int g_backgroundWindow = 0;
static int g_disableInputGrab = 0;
static int g_traceRequested = -1;

/* ===== Frame timing ===== */
static u32 g_lastFrameTime = 0;
static int g_frameSyncCallCount = 0;

static int platformTraceRequested(void) {
    extern const char *g_traceStatePath;

    if (g_traceRequested >= 0) {
        return g_traceRequested;
    }

    g_traceRequested =
        g_traceStatePath != NULL
        || getenv("GE007_GUARD_ORACLE_TRACE") != NULL
        || getenv("GE007_DUMP_MISSION_GATES") != NULL
        || getenv("GE007_DUMP_STAGE_PADS") != NULL
        || getenv("GE007_DUMP_STAGE_CHRS") != NULL;

    return g_traceRequested;
}

/* ===== Fly camera state ===== */
/* Initial position (stan-space coords, overridden by gameplay camera sync) */
float g_pcCamX = 0.0f, g_pcCamY = 50.0f, g_pcCamZ = -400.0f;
float g_pcCamYaw = 0.0f, g_pcCamPitch = 0.0f;
static int g_mouseGrabbed = 0;
int g_pcDebugFlyCamera = 0;  /* 0 = gameplay camera, 1 = fly cam. Toggle with F1. */
#define FLY_SPEED 50.0f
#define MOUSE_SENSITIVITY 0.003f
f32 g_pcVideoGamma = 1.0f;
f32 g_pcRenderScale = 2.0f;   /* remaster default: 2x SSAA (clean edges; raise to 4x for max IQ) */
s32 g_pcMsaaSamples = 0;
f32 g_pcFovY = 60.0f;
f32 g_pcVideoSaturation = 1.15f; /* remaster default: subtly richer palette */
f32 g_pcVideoContrast = 1.08f;   /* remaster default: gentle contrast pop */
f32 g_pcVideoBrightness = 0.0f;  /* kept neutral (brightness offset is taste-sensitive) */
s32 g_pcOutputDither = 1;        /* remaster default: on (anti-banding under the grade) */
f32 g_pcVignette = 0.25f;        /* remaster default: soft edge falloff for depth */
s32 g_pcBloom = 1;               /* remaster default: on (light bleed on emitters/sky) */
f32 g_pcBloomThreshold = 0.8f;
f32 g_pcBloomIntensity = 0.5f;
s32 g_pcFxaa = 1;                /* remaster default: on (sprite/alpha/HUD edge cleanup atop SSAA) */
f32 g_pcSharpen = 0.3f;          /* remaster default: mild CAS sharpen (no-op at 0; pairs with SSAA) */
f32 g_pcFogDensity = 1.0f;
s32 g_pcGradePresets = 1;        /* remaster default: on (subtle per-level mood grade atop the global grade) */
f32 g_pcGradeLevelSat = 1.0f;    /* renderer-internal: per-level saturation mult (identity until set by table) */
f32 g_pcGradeLevelCon = 1.0f;    /* renderer-internal: per-level contrast mult */
f32 g_pcGradeLevelTintR = 1.0f;  /* renderer-internal: per-level tint R */
f32 g_pcGradeLevelTintG = 1.0f;  /* renderer-internal: per-level tint G */
f32 g_pcGradeLevelTintB = 1.0f;  /* renderer-internal: per-level tint B */

/* ===== Window mode state ===== */
typedef enum PlatformWindowMode {
    PLATFORM_WINDOW_MODE_WINDOWED = 0,
    PLATFORM_WINDOW_MODE_BORDERLESS = 1,
    PLATFORM_WINDOW_MODE_EXCLUSIVE = 2
} PlatformWindowMode;

static s32 g_windowMode = PLATFORM_WINDOW_MODE_WINDOWED;
static const ConfigEnumOption k_windowModeOptions[] = {
    { "windowed", PLATFORM_WINDOW_MODE_WINDOWED },
    { "borderless", PLATFORM_WINDOW_MODE_BORDERLESS },
    { "exclusive", PLATFORM_WINDOW_MODE_EXCLUSIVE },
};

typedef enum PlatformVSyncMode {
    PLATFORM_VSYNC_OFF = 0,
    PLATFORM_VSYNC_ON = 1,
    PLATFORM_VSYNC_ADAPTIVE = 2
} PlatformVSyncMode;

static s32 g_vsyncMode = PLATFORM_VSYNC_ADAPTIVE;
static const ConfigEnumOption k_vsyncOptions[] = {
    { "off", PLATFORM_VSYNC_OFF },
    { "on", PLATFORM_VSYNC_ON },
    { "adaptive", PLATFORM_VSYNC_ADAPTIVE },
};

typedef enum PlatformFrameCapMode {
    PLATFORM_FRAME_CAP_DISPLAY = 0,
    PLATFORM_FRAME_CAP_30 = 30,
    PLATFORM_FRAME_CAP_60 = 60
} PlatformFrameCapMode;

static s32 g_frameCapMode = PLATFORM_FRAME_CAP_60;
static const ConfigEnumOption k_frameCapOptions[] = {
    { "30", PLATFORM_FRAME_CAP_30 },
    { "60", PLATFORM_FRAME_CAP_60 },
    { "display", PLATFORM_FRAME_CAP_DISPLAY },
};

static const ConfigEnumOption k_msaaOptions[] = {
    { "0", 0 },
    { "2", 2 },
    { "4", 4 },
    { "8", 8 },
};

typedef enum PlatformRetroFilterMode {
    PLATFORM_RETRO_FILTER_AUTO = 0,
    PLATFORM_RETRO_FILTER_OFF = 1,
    PLATFORM_RETRO_FILTER_ON = 2
} PlatformRetroFilterMode;

s32 g_pcRetroFilterMode = PLATFORM_RETRO_FILTER_AUTO;
static const ConfigEnumOption k_retroFilterOptions[] = {
    { "auto", PLATFORM_RETRO_FILTER_AUTO },
    { "off", PLATFORM_RETRO_FILTER_OFF },
    { "on", PLATFORM_RETRO_FILTER_ON },
};

/* ===== Configurable window/display settings ===== */
static s32 g_cfgWindowW = 1440;
static s32 g_cfgWindowH = 810;
static s32 g_cfgWindowX = -1;
static s32 g_cfgWindowY = -1;
static s32 g_cfgDisplayIndex = 0;
static s32 g_cfgFullscreenW = 0;
static s32 g_cfgFullscreenH = 0;
static s32 g_cfgFullscreenRefresh = 0;

int platformPrintDisplays(FILE *f)
{
    int initialized_here = 0;
    int display_count;

    if (f == NULL) {
        f = stdout;
    }

    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
            fprintf(stderr, "[SDL] Failed to initialize video for display listing: %s\n",
                    SDL_GetError());
            return 0;
        }
        initialized_here = 1;
    }

    display_count = SDL_GetNumVideoDisplays();
    if (display_count < 0) {
        fprintf(stderr, "[SDL] Failed to enumerate displays: %s\n", SDL_GetError());
        if (initialized_here) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
        return 0;
    }

    fprintf(f, "SDL displays (%d):\n", display_count);
    for (int i = 0; i < display_count; i++) {
        const char *name = SDL_GetDisplayName(i);
        SDL_Rect bounds = {0, 0, 0, 0};
        SDL_Rect usable = {0, 0, 0, 0};
        SDL_DisplayMode current = {0};
        int mode_count;

        SDL_GetDisplayBounds(i, &bounds);
        SDL_GetDisplayUsableBounds(i, &usable);
        SDL_GetCurrentDisplayMode(i, &current);

        fprintf(f,
                "  [%d] %s bounds=%dx%d+%d+%d usable=%dx%d+%d+%d current=%dx%d@%dHz\n",
                i,
                (name != NULL && name[0] != '\0') ? name : "(unnamed)",
                bounds.w, bounds.h, bounds.x, bounds.y,
                usable.w, usable.h, usable.x, usable.y,
                current.w, current.h, current.refresh_rate);

        mode_count = SDL_GetNumDisplayModes(i);
        if (mode_count < 0) {
            fprintf(f, "      modes: unavailable (%s)\n", SDL_GetError());
            continue;
        }

        for (int m = 0; m < mode_count; m++) {
            SDL_DisplayMode mode = {0};

            if (SDL_GetDisplayMode(i, m, &mode) != 0) {
                continue;
            }
            fprintf(f, "      mode[%d] %dx%d@%dHz %s\n",
                    m,
                    mode.w,
                    mode.h,
                    mode.refresh_rate,
                    SDL_GetPixelFormatName(mode.format));
        }
    }

    if (initialized_here) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    return 1;
}

/* ===== Screenshot support ===== */
#define SCREENSHOT_W 640
#define SCREENSHOT_H 480
static int g_screenshotRequested = 0;
static int g_screenshotCounter = 0;
int g_autoScreenshotFrame = -1;  /* frame number to auto-capture (-1 = disabled) */
int g_autoScreenshotGameTimer = -1; /* g_GlobalTimer value to auto-capture (-1 = disabled) */
int g_autoScreenshotExit = 0;    /* exit after auto-screenshot */
static char g_screenshotLabelStorage[96];
static const char *g_screenshotLabel = NULL; /* label for screenshot filename */
int g_freezeInput = 0;           /* zero all controller input for deterministic screenshots */

extern s32 g_diagDisplayCastLastIndex;
extern s32 g_diagDisplayCastLastAnimIndex;
extern s32 g_diagDisplayCastLastCameraPreset;
extern s32 g_diagDisplayCastLastTimer;

static void platformResampleFramebufferToScreenshot(const unsigned char *src,
                                                    int src_w, int src_h,
                                                    unsigned char *dst,
                                                    int dst_w, int dst_h) {
    int scaled_w;
    int scaled_h;
    int offset_x;
    int offset_y;

    memset(dst, 0, (size_t)dst_w * (size_t)dst_h * 3);

    if (src == NULL || src_w <= 0 || src_h <= 0 || dst == NULL || dst_w <= 0 || dst_h <= 0) {
        return;
    }

    if ((int64_t)dst_w * src_h <= (int64_t)dst_h * src_w) {
        scaled_w = dst_w;
        scaled_h = (src_h * dst_w + src_w / 2) / src_w;
    } else {
        scaled_h = dst_h;
        scaled_w = (src_w * dst_h + src_h / 2) / src_h;
    }

    if (scaled_w < 1) {
        scaled_w = 1;
    } else if (scaled_w > dst_w) {
        scaled_w = dst_w;
    }

    if (scaled_h < 1) {
        scaled_h = 1;
    } else if (scaled_h > dst_h) {
        scaled_h = dst_h;
    }

    offset_x = (dst_w - scaled_w) / 2;
    offset_y = (dst_h - scaled_h) / 2;

    for (int y = 0; y < scaled_h; y++) {
        int src_y = ((y * 2 + 1) * src_h) / (scaled_h * 2);

        if (src_y < 0) {
            src_y = 0;
        } else if (src_y >= src_h) {
            src_y = src_h - 1;
        }

        for (int x = 0; x < scaled_w; x++) {
            int src_x = ((x * 2 + 1) * src_w) / (scaled_w * 2);
            int dst_index;
            int src_index;

            if (src_x < 0) {
                src_x = 0;
            } else if (src_x >= src_w) {
                src_x = src_w - 1;
            }

            dst_index = ((y + offset_y) * dst_w + (x + offset_x)) * 3;
            src_index = (src_y * src_w + src_x) * 3;
            dst[dst_index + 0] = src[src_index + 0];
            dst[dst_index + 1] = src[src_index + 1];
            dst[dst_index + 2] = src[src_index + 2];
        }
    }
}

static void platformResizeRgbBilinear(const unsigned char *src,
                                      int src_w, int src_h,
                                      unsigned char *dst,
                                      int dst_w, int dst_h) {
    if (src == NULL || dst == NULL || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return;
    }

    for (int y = 0; y < dst_h; y++) {
        float src_y = ((float)y + 0.5f) * (float)src_h / (float)dst_h - 0.5f;
        int y0 = (int)floorf(src_y);
        float fy = src_y - (float)y0;

        if (y0 < 0) {
            y0 = 0;
            fy = 0.0f;
        } else if (y0 >= src_h - 1) {
            y0 = src_h - 1;
            fy = 0.0f;
        }

        int y1 = y0 + 1;
        if (y1 >= src_h) {
            y1 = y0;
        }

        for (int x = 0; x < dst_w; x++) {
            float src_x = ((float)x + 0.5f) * (float)src_w / (float)dst_w - 0.5f;
            int x0 = (int)floorf(src_x);
            float fx = src_x - (float)x0;

            if (x0 < 0) {
                x0 = 0;
                fx = 0.0f;
            } else if (x0 >= src_w - 1) {
                x0 = src_w - 1;
                fx = 0.0f;
            }

            int x1 = x0 + 1;
            if (x1 >= src_w) {
                x1 = x0;
            }

            int dst_index = (y * dst_w + x) * 3;
            int idx00 = (y0 * src_w + x0) * 3;
            int idx10 = (y0 * src_w + x1) * 3;
            int idx01 = (y1 * src_w + x0) * 3;
            int idx11 = (y1 * src_w + x1) * 3;

            for (int c = 0; c < 3; c++) {
                float v0 = (float)src[idx00 + c] + ((float)src[idx10 + c] - (float)src[idx00 + c]) * fx;
                float v1 = (float)src[idx01 + c] + ((float)src[idx11 + c] - (float)src[idx01 + c]) * fx;
                float v = v0 + (v1 - v0) * fy;
                int out = (int)(v + 0.5f);

                if (out < 0) {
                    out = 0;
                } else if (out > 255) {
                    out = 255;
                }
                dst[dst_index + c] = (unsigned char)out;
            }
        }
    }
}

static void platformApplyScreenshotViFilter(unsigned char *pixels, int width, int height) {
    const char *env = getenv("GE007_DIAG_SCREENSHOT_VI_FILTER");
    int filter_w = 0;
    int filter_h = 0;
    unsigned char *small = NULL;
    unsigned char *filtered = NULL;

    if (env == NULL || env[0] == '\0' || strcmp(env, "0") == 0) {
        return;
    }

    if (sscanf(env, "%dx%d", &filter_w, &filter_h) != 2 ||
        filter_w <= 0 || filter_h <= 0 ||
        filter_w > width || filter_h > height) {
        fprintf(stderr,
                "[SDL] Ignoring invalid GE007_DIAG_SCREENSHOT_VI_FILTER=%s "
                "(expected WxH within %dx%d)\n",
                env, width, height);
        return;
    }

    small = (unsigned char *)malloc((size_t)filter_w * (size_t)filter_h * 3);
    filtered = (unsigned char *)malloc((size_t)width * (size_t)height * 3);
    if (small == NULL || filtered == NULL) {
        free(small);
        free(filtered);
        return;
    }

    platformResizeRgbBilinear(pixels, width, height, small, filter_w, filter_h);
    platformResizeRgbBilinear(small, filter_w, filter_h, filtered, width, height);
    memcpy(pixels, filtered, (size_t)width * (size_t)height * 3);
    printf("[SDL] Screenshot VI filter %dx%d -> %dx%d (GE007_DIAG_SCREENSHOT_VI_FILTER)\n",
           filter_w, filter_h, width, height);

    free(small);
    free(filtered);
}

void platformSetScreenshotLabel(const char *label) {
    size_t out = 0;

    if (label == NULL || *label == '\0') {
        g_screenshotLabelStorage[0] = '\0';
        g_screenshotLabel = NULL;
        return;
    }

    while (*label != '\0' && out < sizeof(g_screenshotLabelStorage) - 1) {
        char c = *label++;
        if ((c >= 'a' && c <= 'z')
            || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9')
            || c == '-'
            || c == '_'
            || c == '.') {
            g_screenshotLabelStorage[out++] = c;
        }
    }

    g_screenshotLabelStorage[out] = '\0';
    g_screenshotLabel = out > 0 ? g_screenshotLabelStorage : NULL;
}

void platformSaveScreenshot(void) {
    int w = SCREENSHOT_W, h = SCREENSHOT_H;
    int src_w = w;
    int src_h = h;
    int row_size;
    int data_size;
    int file_size;
    unsigned char *pixels = NULL;
    unsigned char *source_pixels = NULL;
    int native_size_screenshot = getenv("GE007_DIAG_SCREENSHOT_NATIVE_SIZE") != NULL;

    if (g_sdlWindow != NULL) {
        SDL_GL_GetDrawableSize(g_sdlWindow, &src_w, &src_h);
    }

    if (src_w < 1) {
        src_w = w;
    }

    if (src_h < 1) {
        src_h = h;
    }

    if (native_size_screenshot) {
        w = src_w;
        h = src_h;
    }

    row_size = ((w * 3 + 3) & ~3);  /* BMP rows must be 4-byte aligned */
    data_size = row_size * h;
    file_size = 54 + data_size;

    source_pixels = (unsigned char *)malloc((size_t)src_w * (size_t)src_h * 3);
    pixels = (unsigned char *)malloc((size_t)w * (size_t)h * 3);

    if (!source_pixels || !pixels) {
        free(source_pixels);
        free(pixels);
        return;
    }

    /* Read from the FRONT buffer: this runs at the top of platformFrameSync,
     * BEFORE the current frame's swap (handled later in gfx_end_frame). The BACK
     * buffer is undefined right after the previous frame's SDL_GL_SwapWindow, so a
     * default-read-buffer (GL_BACK) glReadPixels here captures stale/garbage pixels
     * — corrupting every parity/oracle/contact-sheet capture. GL_FRONT holds the
     * last fully-presented frame (deterministic). Restore GL_BACK after. */
    glReadBuffer(GL_FRONT);
    glReadPixels(0, 0, src_w, src_h, GL_RGB, GL_UNSIGNED_BYTE, source_pixels);
    glReadBuffer(GL_BACK);

    if (src_w == w && src_h == h) {
        memcpy(pixels, source_pixels, (size_t)w * (size_t)h * 3);
    } else {
        platformResampleFramebufferToScreenshot(source_pixels, src_w, src_h, pixels, w, h);
    }
    platformApplyScreenshotViFilter(pixels, w, h);

    char filename[128];
    if (g_screenshotLabel) {
        snprintf(filename, sizeof(filename), "screenshot_%s.bmp", g_screenshotLabel);
    } else {
        snprintf(filename, sizeof(filename), "screenshot_%03d.bmp", g_screenshotCounter++);
    }
    FILE *f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "[SDL] Failed to save screenshot %s: %s\n",
                filename, strerror(errno));
        free(source_pixels);
        free(pixels);
        return;
    }

    /* BMP header (54 bytes) */
    unsigned char hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = file_size; hdr[3] = file_size >> 8; hdr[4] = file_size >> 16; hdr[5] = file_size >> 24;
    hdr[10] = 54;  /* data offset */
    hdr[14] = 40;  /* DIB header size */
    hdr[18] = w; hdr[19] = w >> 8;
    hdr[22] = h; hdr[23] = h >> 8;
    hdr[26] = 1;   /* planes */
    hdr[28] = 24;  /* bpp */
    hdr[34] = data_size; hdr[35] = data_size >> 8; hdr[36] = data_size >> 16; hdr[37] = data_size >> 24;
    fwrite(hdr, 1, 54, f);

    /* BMP stores rows bottom-to-top (OpenGL gives us bottom-to-top), RGB→BGR */
    unsigned char *row_buf = (unsigned char *)malloc(row_size);
    for (int y = 0; y < h; y++) {
        unsigned char *src = pixels + y * w * 3;
        memset(row_buf, 0, row_size);
        for (int x = 0; x < w; x++) {
            row_buf[x * 3 + 0] = src[x * 3 + 2];  /* B */
            row_buf[x * 3 + 1] = src[x * 3 + 1];  /* G */
            row_buf[x * 3 + 2] = src[x * 3 + 0];  /* R */
        }
        fwrite(row_buf, 1, row_size, f);
    }
    free(row_buf);
    fclose(f);
    free(source_pixels);
    free(pixels);
    printf("[SDL] Screenshot saved: %s\n", filename);
}

static int platformParseDiagS32Env(const char *name, s32 *out) {
    const char *env = getenv(name);
    char *end = NULL;
    long value;

    if (env == NULL || env[0] == '\0') {
        return 0;
    }

    value = strtol(env, &end, 10);
    if (end == env) {
        fprintf(stderr, "[SDL] Ignoring invalid %s=%s\n", name, env);
        return 0;
    }
    if (value < INT_MIN) value = INT_MIN;
    if (value > INT_MAX) value = INT_MAX;
    *out = (s32)value;
    return 1;
}

static int platformParseDiagF32Env(const char *name, f32 *out) {
    const char *env = getenv(name);
    char *end = NULL;
    double value;

    if (env == NULL || env[0] == '\0') {
        return 0;
    }

    value = strtod(env, &end);
    if (end == env) {
        fprintf(stderr, "[SDL] Ignoring invalid %s=%s\n", name, env);
        return 0;
    }
    *out = (f32)value;
    return 1;
}

static void platformApplyWindowSizeEnv(void) {
    const char *env = getenv("GE007_WINDOW_SIZE");
    char *end = NULL;
    long width;
    long height;

    if (env == NULL || env[0] == '\0') {
        return;
    }

    width = strtol(env, &end, 10);
    if (end == env || (*end != 'x' && *end != 'X')) {
        fprintf(stderr, "[SDL] Ignoring invalid GE007_WINDOW_SIZE=%s\n", env);
        return;
    }

    height = strtol(end + 1, &end, 10);
    if (height <= 0 || width <= 0 || (*end != '\0' && *end != ' ' && *end != '\t')) {
        fprintf(stderr, "[SDL] Ignoring invalid GE007_WINDOW_SIZE=%s\n", env);
        return;
    }

    if (width < 320) width = 320;
    if (width > 3840) width = 3840;
    if (height < 240) height = 240;
    if (height > 2160) height = 2160;
    g_cfgWindowW = (s32)width;
    g_cfgWindowH = (s32)height;
    fprintf(stderr,
            "[SDL] Window size override %dx%d (GE007_WINDOW_SIZE)\n",
            g_cfgWindowW, g_cfgWindowH);
}

static int platformFloatWithinTolerance(f32 actual, f32 expected, f32 tolerance) {
    return fabsf(actual - expected) <= tolerance;
}

static int platformDiagDisplayCastScreenshotDue(void) {
    static int checked;
    static int done;
    static s32 target_timer = INT_MIN;
    static s32 target_index = INT_MIN;
    static s32 target_anim_index = INT_MIN;
    static s32 target_camera_preset = INT_MIN;
    static s32 capture_delay = 0;
    static s32 pending_delay = -1;

    if (done) {
        return 0;
    }

    if (pending_delay >= 0) {
        if (pending_delay > 0) {
            pending_delay--;
        }
        if (pending_delay == 0) {
            pending_delay = -1;
            done = 1;
            fprintf(stderr,
                    "[SDL] DIAG DISPLAYCAST SCREENSHOT capture frame=%d timer=%d index=%d anim=%d camera=%d\n",
                    g_frameSyncCallCount,
                    g_diagDisplayCastLastTimer,
                    g_diagDisplayCastLastIndex,
                    g_diagDisplayCastLastAnimIndex,
                    g_diagDisplayCastLastCameraPreset);
            return 1;
        }
        return 0;
    }

    if (!checked) {
        checked = 1;
        if (!platformParseDiagS32Env("GE007_DIAG_DISPLAYCAST_SCREENSHOT_TIMER", &target_timer)) {
            return 0;
        }
        platformParseDiagS32Env("GE007_DIAG_DISPLAYCAST_SCREENSHOT_INDEX", &target_index);
        platformParseDiagS32Env("GE007_DIAG_DISPLAYCAST_SCREENSHOT_ANIM_INDEX", &target_anim_index);
        platformParseDiagS32Env("GE007_DIAG_DISPLAYCAST_SCREENSHOT_CAMERA_PRESET", &target_camera_preset);
        platformParseDiagS32Env("GE007_DIAG_DISPLAYCAST_SCREENSHOT_DELAY", &capture_delay);
        if (capture_delay < 0) {
            capture_delay = 0;
        }
        fprintf(stderr,
                "[SDL] DIAG DISPLAYCAST SCREENSHOT armed timer=%d index=%d anim=%d camera=%d delay=%d\n",
                target_timer, target_index, target_anim_index, target_camera_preset, capture_delay);
    }

    if (target_timer == INT_MIN || g_diagDisplayCastLastTimer != target_timer) {
        return 0;
    }
    if (target_index != INT_MIN && g_diagDisplayCastLastIndex != target_index) {
        return 0;
    }
    if (target_anim_index != INT_MIN && g_diagDisplayCastLastAnimIndex != target_anim_index) {
        return 0;
    }
    if (target_camera_preset != INT_MIN && g_diagDisplayCastLastCameraPreset != target_camera_preset) {
        return 0;
    }

    fprintf(stderr,
            "[SDL] DIAG DISPLAYCAST SCREENSHOT matched frame=%d timer=%d index=%d anim=%d camera=%d\n",
            g_frameSyncCallCount,
            g_diagDisplayCastLastTimer,
            g_diagDisplayCastLastIndex,
            g_diagDisplayCastLastAnimIndex,
            g_diagDisplayCastLastCameraPreset);
    if (capture_delay > 0) {
        pending_delay = capture_delay;
        return 0;
    }

    done = 1;
    return 1;
}

static int platformDiagMenuScreenshotDue(void) {
    static int checked;
    static int done;
    static s32 target_menu = INT_MIN;
    static s32 target_timer = INT_MIN;
    static s32 timer_min = INT_MIN;
    static s32 timer_max = INT_MIN;
    static s32 target_wave = INT_MIN;
    static f32 target_title_x = 0.0f;
    static f32 target_title_y = 0.0f;
    static f32 target_rare_rotation = 0.0f;
    static f32 target_nintendo_rotation = 0.0f;
    static f32 target_nintendo_scale = 0.0f;
    static f32 tolerance = 0.5f;
    static int has_title_x;
    static int has_title_y;
    static int has_rare_rotation;
    static int has_nintendo_rotation;
    static int has_nintendo_scale;
    static s32 capture_delay = 0;
    static s32 pending_delay = -1;

    if (done) {
        return 0;
    }

    if (pending_delay >= 0) {
        if (pending_delay > 0) {
            pending_delay--;
        }
        if (pending_delay == 0) {
            pending_delay = -1;
            done = 1;
            fprintf(stderr,
                    "[SDL] DIAG MENU SCREENSHOT capture frame=%d menu=%d timer=%d "
                    "title=(x=%.4f y=%.4f rare=%.4f nintendo=%.4f scale=%.6f wave=%d)\n",
                    g_frameSyncCallCount,
                    (int)current_menu,
                    (int)g_MenuTimer,
                    g_TitleX,
                    g_TitleY,
                    D_8002A89C,
                    ninLogoRotRate,
                    ninLogoScale,
                    (int)word_CODE_bss_80069584);
            return 1;
        }
        return 0;
    }

    if (!checked) {
        checked = 1;
        if (!platformParseDiagS32Env("GE007_DIAG_MENU_SCREENSHOT_MENU", &target_menu)) {
            return 0;
        }
        if (platformParseDiagS32Env("GE007_DIAG_MENU_SCREENSHOT_TIMER", &target_timer)) {
            timer_min = target_timer;
            timer_max = target_timer;
        }
        platformParseDiagS32Env("GE007_DIAG_MENU_SCREENSHOT_TIMER_MIN", &timer_min);
        platformParseDiagS32Env("GE007_DIAG_MENU_SCREENSHOT_TIMER_MAX", &timer_max);
        platformParseDiagS32Env("GE007_DIAG_MENU_SCREENSHOT_WAVE", &target_wave);
        platformParseDiagF32Env("GE007_DIAG_MENU_SCREENSHOT_TOLERANCE", &tolerance);
        has_title_x = platformParseDiagF32Env("GE007_DIAG_MENU_SCREENSHOT_TITLE_X", &target_title_x);
        has_title_y = platformParseDiagF32Env("GE007_DIAG_MENU_SCREENSHOT_TITLE_Y", &target_title_y);
        has_rare_rotation = platformParseDiagF32Env("GE007_DIAG_MENU_SCREENSHOT_RARE_ROTATION", &target_rare_rotation);
        has_nintendo_rotation = platformParseDiagF32Env("GE007_DIAG_MENU_SCREENSHOT_NINTENDO_ROTATION", &target_nintendo_rotation);
        has_nintendo_scale = platformParseDiagF32Env("GE007_DIAG_MENU_SCREENSHOT_NINTENDO_SCALE", &target_nintendo_scale);
        platformParseDiagS32Env("GE007_DIAG_MENU_SCREENSHOT_DELAY", &capture_delay);
        if (capture_delay < 0) {
            capture_delay = 0;
        }
        if (tolerance < 0.0f) {
            tolerance = 0.0f;
        }
        fprintf(stderr,
                "[SDL] DIAG MENU SCREENSHOT armed menu=%d timer=%d min=%d max=%d "
                "rare=%s nintendo=%s scale=%s tolerance=%.4f delay=%d\n",
                (int)target_menu,
                (int)target_timer,
                (int)timer_min,
                (int)timer_max,
                has_rare_rotation ? "set" : "-",
                has_nintendo_rotation ? "set" : "-",
                has_nintendo_scale ? "set" : "-",
                tolerance,
                (int)capture_delay);
    }

    if (target_menu == INT_MIN || (s32)current_menu != target_menu) {
        return 0;
    }
    if (timer_min != INT_MIN && g_MenuTimer < timer_min) {
        return 0;
    }
    if (timer_max != INT_MIN && g_MenuTimer > timer_max) {
        return 0;
    }
    if (target_wave != INT_MIN && (s32)word_CODE_bss_80069584 != target_wave) {
        return 0;
    }
    if (has_title_x && !platformFloatWithinTolerance(g_TitleX, target_title_x, tolerance)) {
        return 0;
    }
    if (has_title_y && !platformFloatWithinTolerance(g_TitleY, target_title_y, tolerance)) {
        return 0;
    }
    if (has_rare_rotation && !platformFloatWithinTolerance(D_8002A89C, target_rare_rotation, tolerance)) {
        return 0;
    }
    if (has_nintendo_rotation && !platformFloatWithinTolerance(ninLogoRotRate, target_nintendo_rotation, tolerance)) {
        return 0;
    }
    if (has_nintendo_scale && !platformFloatWithinTolerance(ninLogoScale, target_nintendo_scale, tolerance)) {
        return 0;
    }

    fprintf(stderr,
            "[SDL] DIAG MENU SCREENSHOT matched frame=%d menu=%d timer=%d "
            "title=(x=%.4f y=%.4f rare=%.4f nintendo=%.4f scale=%.6f wave=%d)\n",
            g_frameSyncCallCount,
            (int)current_menu,
            (int)g_MenuTimer,
            g_TitleX,
            g_TitleY,
            D_8002A89C,
            ninLogoRotRate,
            ninLogoScale,
            (int)word_CODE_bss_80069584);
    if (capture_delay > 0) {
        pending_delay = capture_delay;
        return 0;
    }

    done = 1;
    return 1;
}

static void platformFinishAutoScreenshotIfRequested(void) {
    if (g_autoScreenshotExit) {
        extern int g_crashRecoveryCount;
        if (g_crashRecoveryCount > 0) {
            printf("[GE007-PC] Auto-screenshot complete, but %d crash recoveries occurred; build needed recovery, exiting with error.\n",
                   g_crashRecoveryCount);
            platformShutdownSDL();
            exit(3);
        }
        printf("[GE007-PC] Auto-screenshot complete, exiting.\n");
        platformShutdownSDL();
        exit(0);
    }
}

/* ===== Mouse delta for N64 controller emulation ===== */
static int g_mouseDeltaX = 0;
static int g_mouseDeltaY = 0;

/* ===== PC input state (consumed by game-side NATIVE_PORT blocks) ===== */
static int g_mouseWheelY = 0;       /* scroll wheel accumulator */
int g_pcEscapePressed = 0;          /* Escape key edge-detect flag */
int g_pcCrouchToggle = 0;           /* C/Ctrl edge-detect flag */
int g_pcMouseRegrabFrame = 0;       /* suppress mouse buttons for 1 frame after regrab */
float g_pcMouseSensitivity = 0.15f; /* configurable mouse sensitivity */
float g_pcMouseSensAim = 0.05f;     /* aim-mode mouse sensitivity */
int g_pcInvertY = 0;                /* invert Y axis */
float g_pcGamepadLookSpeed = 8.0f;  /* gamepad right stick scaling */
/* Opt-in right-stick feel knobs (all default to vanilla-identity). */
float g_pcGamepadLookCurve = 1.5f;  /* remaster default: mild ease-in for fine aim (1.0 = linear) */
float g_pcGamepadDeadzone = 0.15f;  /* remaster default: tighter modern deadzone (paired with radial) */
int g_pcGamepadRadialDeadzone = 1;  /* remaster default: true radial rescale-from-edge */
int g_pcGamepadFpsScale = 1;        /* remaster default: frame-rate-independent look (no-op at 60fps) */

/* ADS (aim-down-sights) feature flags. Master flag g_pcAdsEnabled ships OFF;
 * when 0 every ADS branch is bypassed and behavior is byte-identical to vanilla.
 * Consumers declare these inline as `extern` at their use sites. */
s32 g_pcAdsEnabled       = 0;     /* Input.AdsEnabled        master, OFF by default */
f32 g_pcAdsSensitivity   = 1.0f;  /* Input.AdsSensitivity    flat aimed look mult   */
s32 g_pcAdsFovCoupleSens = 1;     /* Input.AdsFovCoupleSens  FOV-coupled slow-look   */
s32 g_pcAdsCenterCrosshair = 1;   /* Input.AdsCenterCrosshair stick center-pull      */
s32 g_pcAdsSpreadEnabled = 1;     /* Input.AdsSpreadEnabled  per-weapon spread mult  */
s32 g_pcAdsMovePenalty   = 1;     /* Input.AdsMovePenalty    aimed movement penalty  */
f32 g_pcAdsMoveScale     = 1.0f;  /* Input.AdsMoveScale      forward-speed trim      */
f32 g_pcAdsStrafeScale   = 1.0f;  /* Input.AdsStrafeScale    strafe-speed trim       */
s32 g_pcAdsFaithfulZoom  = 0;     /* Input.AdsFaithfulZoom   disable mild-iron clamp */
s32 g_pcAdsModelPose     = 1;     /* Input.AdsModelPose      sighted model pose      */
s32 g_pcAdsModernReticle = 1;     /* Input.AdsModernReticle  modern dot+ticks reticle */
s32 g_pcAdsSteadyView    = 1;     /* Input.AdsSteadyView     damp walk/strafe head-bob in ADS */
f32 g_pcAdsBobFloor      = 0.15f; /* Input.AdsBobFloor       residual ADS weapon bob (0=rigid) */
f32 g_pcAdsRecoilReduce  = 0.0f;  /* Input.AdsRecoilReduce   cosmetic recoil cut     */
f32 g_pcViewmodelSway    = 1.0f;  /* Input.ViewmodelSway     remaster default: subtle weapon sway ON */
s32 g_pcModernCrosshair  = 1;     /* Input.ModernCrosshair   remaster default: crisp hip-fire reticle ON */
s32 g_pcHitMarkers       = 1;     /* Input.HitMarkers        remaster default: on-hit marker ON */

extern int g_pcScriptedMouseDeltaX;
extern int g_pcScriptedMouseDeltaY;
#ifdef MACOS_APP_BUNDLE
extern int g_pcBridgeRightStickX;
extern int g_pcBridgeRightStickY;
#endif

static int platformEnvFlagEnabled(const char *name)
{
    const char *value = getenv(name);

    if (value == NULL || value[0] == '\0') {
        return 0;
    }

    if (value[0] == '0' && value[1] == '\0') {
        return 0;
    }

    return 1;
}

#define AUTO_MUTE_TOGGLE_MAX 16

static int g_audioMuted = -1;
static int g_autoMuteToggleParsed = 0;
static int g_autoMuteToggleCount = 0;
static int g_autoMuteToggleFrames[AUTO_MUTE_TOGGLE_MAX];
static int g_autoMuteToggleFired[AUTO_MUTE_TOGGLE_MAX];

static int platformAudioMuteTraceEnabled(void)
{
    return platformEnvFlagEnabled("GE007_AUDIO_MUTE_TRACE");
}

static void platformToggleAudioMute(const char *source)
{
    extern SDL_AudioDeviceID portAudioGetDevice(void);
    extern SDL_AudioDeviceID portAiGetDevice(void);
    extern int portAudioShouldStartMuted(void);
    SDL_AudioDeviceID audio_dev;
    SDL_AudioDeviceID ai_dev;

    if (g_audioMuted < 0) {
        g_audioMuted = portAudioShouldStartMuted();
    }

    g_audioMuted = !g_audioMuted;
    audio_dev = portAudioGetDevice();
    ai_dev = portAiGetDevice();

    if (audio_dev) {
        SDL_PauseAudioDevice(audio_dev, g_audioMuted);
    }
    if (ai_dev && ai_dev != audio_dev) {
        SDL_PauseAudioDevice(ai_dev, g_audioMuted);
    }

    printf("[AUDIO] %s (%s)\n", g_audioMuted ? "MUTED" : "UNMUTED", source);
    if (platformAudioMuteTraceEnabled()) {
        printf("[AUDIO_MUTE_TRACE] frame=%d source=%s muted=%d audio_dev=%u ai_dev=%u unified=%d\n",
               g_frameSyncCallCount,
               source,
               g_audioMuted ? 1 : 0,
               (unsigned int)audio_dev,
               (unsigned int)ai_dev,
               (audio_dev != 0 && ai_dev == audio_dev) ? 1 : 0);
    }
}

static void platformAddAutoMuteToggleFrame(long frame)
{
    if (frame < 0 || g_autoMuteToggleCount >= AUTO_MUTE_TOGGLE_MAX) {
        return;
    }

    g_autoMuteToggleFrames[g_autoMuteToggleCount] = (int)frame;
    g_autoMuteToggleFired[g_autoMuteToggleCount] = 0;
    g_autoMuteToggleCount++;
}

static void platformParseAutoMuteToggleValue(const char *value)
{
    const char *cursor = value;

    while (cursor != NULL && *cursor != '\0' && g_autoMuteToggleCount < AUTO_MUTE_TOGGLE_MAX) {
        char *end = NULL;
        long frame;

        while (*cursor == ' ' || *cursor == '\t' || *cursor == ',' || *cursor == ';') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        frame = strtol(cursor, &end, 10);
        if (end == cursor) {
            break;
        }

        platformAddAutoMuteToggleFrame(frame);
        cursor = end;
    }
}

static void platformParseAutoMuteToggles(void)
{
    g_autoMuteToggleParsed = 1;
    platformParseAutoMuteToggleValue(getenv("GE007_AUTO_MUTE_TOGGLE_FRAME"));
    platformParseAutoMuteToggleValue(getenv("GE007_AUTO_MUTE_TOGGLE_SCRIPT"));
}

static void platformApplyAutoMuteToggles(void)
{
    int i;

    if (!g_autoMuteToggleParsed) {
        platformParseAutoMuteToggles();
    }

    for (i = 0; i < g_autoMuteToggleCount; i++) {
        if (!g_autoMuteToggleFired[i] && g_frameSyncCallCount == g_autoMuteToggleFrames[i]) {
            g_autoMuteToggleFired[i] = 1;
            platformToggleAudioMute("auto mute toggle");
        }
    }
}

static Uint32 platformFullscreenFlagForWindowMode(s32 mode)
{
    switch (mode) {
        case PLATFORM_WINDOW_MODE_BORDERLESS:
            return SDL_WINDOW_FULLSCREEN_DESKTOP;
        case PLATFORM_WINDOW_MODE_EXCLUSIVE:
            return SDL_WINDOW_FULLSCREEN;
        case PLATFORM_WINDOW_MODE_WINDOWED:
        default:
            return 0;
    }
}

static void platformSyncWindowSizeForRenderer(void)
{
    int w;
    int h;

    if (!g_sdlWindow) {
        return;
    }

    SDL_GetWindowSize(g_sdlWindow, &w, &h);
    gfx_set_window_size(w, h);
}

static int platformConfiguredDisplayIndex(void)
{
    int display_count = SDL_GetNumVideoDisplays();

    if (display_count <= 0) {
        return 0;
    }
    if (g_cfgDisplayIndex < 0 || g_cfgDisplayIndex >= display_count) {
        fprintf(stderr,
                "[SDL] Display index %d is not available; using display 0 of %d.\n",
                g_cfgDisplayIndex, display_count);
        return 0;
    }

    return (int)g_cfgDisplayIndex;
}

static int platformGetDisplayBounds(int display_index, SDL_Rect *bounds)
{
    if (!bounds) {
        return 0;
    }
    if (SDL_GetDisplayUsableBounds(display_index, bounds) == 0) {
        return 1;
    }
    return SDL_GetDisplayBounds(display_index, bounds) == 0;
}

static int platformClampIntToRange(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void platformGetConfiguredWindowPosition(int *x_out, int *y_out)
{
    int display_index = platformConfiguredDisplayIndex();

    if (g_cfgWindowX < 0 || g_cfgWindowY < 0) {
        int centered_pos = SDL_WINDOWPOS_CENTERED_DISPLAY(display_index);
        if (x_out) {
            *x_out = centered_pos;
        }
        if (y_out) {
            *y_out = centered_pos;
        }
        return;
    }

    SDL_Rect bounds;
    if (platformGetDisplayBounds(display_index, &bounds)) {
        int max_rel_x = bounds.w > g_cfgWindowW ? bounds.w - g_cfgWindowW : 0;
        int max_rel_y = bounds.h > g_cfgWindowH ? bounds.h - g_cfgWindowH : 0;
        int rel_x = platformClampIntToRange(g_cfgWindowX, 0, max_rel_x);
        int rel_y = platformClampIntToRange(g_cfgWindowY, 0, max_rel_y);

        if (x_out) {
            *x_out = bounds.x + rel_x;
        }
        if (y_out) {
            *y_out = bounds.y + rel_y;
        }
        return;
    }

    if (x_out) {
        *x_out = g_cfgWindowX;
    }
    if (y_out) {
        *y_out = g_cfgWindowY;
    }
}

static void platformRememberWindowGeometry(void)
{
    int x;
    int y;
    int w;
    int h;
    int display_index;
    SDL_Rect bounds;

    if (!g_sdlWindow || g_windowMode != PLATFORM_WINDOW_MODE_WINDOWED) {
        return;
    }

    SDL_GetWindowPosition(g_sdlWindow, &x, &y);
    SDL_GetWindowSize(g_sdlWindow, &w, &h);
    display_index = SDL_GetWindowDisplayIndex(g_sdlWindow);
    if (display_index < 0) {
        display_index = platformConfiguredDisplayIndex();
    }

    g_cfgWindowW = w;
    g_cfgWindowH = h;
    g_cfgDisplayIndex = display_index;

    if (platformGetDisplayBounds(display_index, &bounds)) {
        g_cfgWindowX = x - bounds.x;
        g_cfgWindowY = y - bounds.y;
    } else {
        g_cfgWindowX = x;
        g_cfgWindowY = y;
    }
}

static void platformMoveWindowToConfiguredDisplay(void)
{
    int x;
    int y;

    if (!g_sdlWindow) {
        return;
    }

    platformGetConfiguredWindowPosition(&x, &y);
    SDL_SetWindowPosition(g_sdlWindow, x, y);
}

static int platformFindConfiguredFullscreenMode(SDL_DisplayMode *out_mode)
{
    int display_index;
    int mode_count;

    if (out_mode == NULL || g_cfgFullscreenW <= 0 || g_cfgFullscreenH <= 0) {
        return 0;
    }

    display_index = platformConfiguredDisplayIndex();
    mode_count = SDL_GetNumDisplayModes(display_index);
    if (mode_count <= 0) {
        return 0;
    }

    for (int i = 0; i < mode_count; i++) {
        SDL_DisplayMode mode = {0};

        if (SDL_GetDisplayMode(display_index, i, &mode) != 0) {
            continue;
        }
        if (mode.w != g_cfgFullscreenW || mode.h != g_cfgFullscreenH) {
            continue;
        }
        if (g_cfgFullscreenRefresh > 0 &&
            mode.refresh_rate != g_cfgFullscreenRefresh) {
            continue;
        }

        *out_mode = mode;
        return 1;
    }

    fprintf(stderr,
            "[SDL] Fullscreen mode %dx%d@%dHz is not available on display %d; using SDL default.\n",
            g_cfgFullscreenW,
            g_cfgFullscreenH,
            g_cfgFullscreenRefresh,
            display_index);
    return 0;
}

static void platformApplyFullscreenDisplayMode(void)
{
    SDL_DisplayMode mode;

    if (!g_sdlWindow) {
        return;
    }

    if (g_windowMode != PLATFORM_WINDOW_MODE_EXCLUSIVE ||
        !platformFindConfiguredFullscreenMode(&mode)) {
        SDL_SetWindowDisplayMode(g_sdlWindow, NULL);
        return;
    }

    if (SDL_SetWindowDisplayMode(g_sdlWindow, &mode) < 0) {
        fprintf(stderr,
                "[SDL] Failed to set fullscreen mode %dx%d@%dHz: %s\n",
                mode.w,
                mode.h,
                mode.refresh_rate,
                SDL_GetError());
    }
}

static void platformApplyWindowMode(void)
{
    Uint32 fullscreen_flag;

    if (!g_sdlWindow) {
        return;
    }

    fullscreen_flag = platformFullscreenFlagForWindowMode(g_windowMode);
    platformMoveWindowToConfiguredDisplay();
    platformApplyFullscreenDisplayMode();
    if (SDL_SetWindowFullscreen(g_sdlWindow, fullscreen_flag) < 0) {
        fprintf(stderr, "[SDL] Failed to apply window mode %d: %s\n",
                g_windowMode, SDL_GetError());
        return;
    }

    platformSyncWindowSizeForRenderer();
}

static void platformApplyVSync(void)
{
    if (g_forceNoVsync) {
        SDL_GL_SetSwapInterval(0);
        return;
    }

    switch (g_vsyncMode) {
        case PLATFORM_VSYNC_OFF:
            SDL_GL_SetSwapInterval(0);
            break;
        case PLATFORM_VSYNC_ON:
            SDL_GL_SetSwapInterval(1);
            break;
        case PLATFORM_VSYNC_ADAPTIVE:
        default:
            if (SDL_GL_SetSwapInterval(-1) < 0) {
                SDL_GL_SetSwapInterval(1);
            }
            break;
    }
}

static u32 platformFrameDelayMs(void)
{
    switch (g_frameCapMode) {
        case PLATFORM_FRAME_CAP_30:
            return 1000 / 30;
        case PLATFORM_FRAME_CAP_DISPLAY:
            if (!g_forceNoVsync && g_vsyncMode != PLATFORM_VSYNC_OFF) {
                return 0;
            }
            return 1000 / 60;
        case PLATFORM_FRAME_CAP_60:
        default:
            return 1000 / 60;
    }
}

/* Register platform settings with the config system.
 * Called from main_pc.c before configInit(). */
void platformRegisterConfig(void)
{
    settingsRegisterInt("Video.WindowWidth", &g_cfgWindowW, 1440, 320, 3840,
                        SETTING_SCOPE_RESTART, "GE007_WINDOW_WIDTH",
                        "--config-override Video.WindowWidth=VALUE",
                        "Window width",
                        "Initial SDL window width in pixels.");
    settingsRegisterInt("Video.WindowHeight", &g_cfgWindowH, 810, 240, 2160,
                        SETTING_SCOPE_RESTART, "GE007_WINDOW_HEIGHT",
                        "--config-override Video.WindowHeight=VALUE",
                        "Window height",
                        "Initial SDL window height in pixels.");
    settingsRegisterInt("Video.WindowX", &g_cfgWindowX, -1, -1, 32767,
                        SETTING_SCOPE_RESTART, "GE007_WINDOW_X",
                        "--config-override Video.WindowX=VALUE",
                        "Window X",
                        "Window X position relative to the selected display; -1 centers it.");
    settingsRegisterInt("Video.WindowY", &g_cfgWindowY, -1, -1, 32767,
                        SETTING_SCOPE_RESTART, "GE007_WINDOW_Y",
                        "--config-override Video.WindowY=VALUE",
                        "Window Y",
                        "Window Y position relative to the selected display; -1 centers it.");
    settingsRegisterInt("Video.Display", &g_cfgDisplayIndex, 0, 0, 31,
                        SETTING_SCOPE_RESTART, "GE007_DISPLAY",
                        "--config-override Video.Display=VALUE",
                        "Display",
                        "Zero-based SDL display index for window and fullscreen placement.");
    settingsRegisterInt("Video.FullscreenWidth", &g_cfgFullscreenW, 0, 0, 7680,
                        SETTING_SCOPE_RESTART, "GE007_FULLSCREEN_WIDTH",
                        "--config-override Video.FullscreenWidth=VALUE",
                        "Fullscreen width",
                        "Exclusive fullscreen mode width; 0 uses SDL/default.");
    settingsRegisterInt("Video.FullscreenHeight", &g_cfgFullscreenH, 0, 0, 4320,
                        SETTING_SCOPE_RESTART, "GE007_FULLSCREEN_HEIGHT",
                        "--config-override Video.FullscreenHeight=VALUE",
                        "Fullscreen height",
                        "Exclusive fullscreen mode height; 0 uses SDL/default.");
    settingsRegisterInt("Video.FullscreenRefresh", &g_cfgFullscreenRefresh, 0, 0, 1000,
                        SETTING_SCOPE_RESTART, "GE007_FULLSCREEN_REFRESH",
                        "--config-override Video.FullscreenRefresh=VALUE",
                        "Fullscreen refresh",
                        "Exclusive fullscreen refresh rate in Hz; 0 accepts any/default.");
    settingsRegisterEnum("Video.WindowMode", &g_windowMode, PLATFORM_WINDOW_MODE_WINDOWED,
                         k_windowModeOptions,
                         (s32)(sizeof(k_windowModeOptions) / sizeof(k_windowModeOptions[0])),
                         SETTING_SCOPE_LIVE, "GE007_WINDOW_MODE",
                         "--config-override Video.WindowMode=VALUE",
                         "Window mode",
                         "SDL display mode: windowed, borderless, or exclusive.");
    settingsRegisterEnum("Video.VSync", &g_vsyncMode, PLATFORM_VSYNC_ADAPTIVE,
                         k_vsyncOptions,
                         (s32)(sizeof(k_vsyncOptions) / sizeof(k_vsyncOptions[0])),
                         SETTING_SCOPE_LIVE, "GE007_VSYNC",
                         "--config-override Video.VSync=VALUE",
                         "VSync",
                         "Swap interval: off, on, or adaptive.");
    settingsRegisterEnum("Video.FrameCap", &g_frameCapMode, PLATFORM_FRAME_CAP_60,
                         k_frameCapOptions,
                         (s32)(sizeof(k_frameCapOptions) / sizeof(k_frameCapOptions[0])),
                         SETTING_SCOPE_LIVE, "GE007_FRAME_CAP",
                         "--config-override Video.FrameCap=VALUE",
                         "Frame cap",
                         "Frame pacing cap: 30, 60, or display.");
    settingsRegisterFloat("Video.Gamma", &g_pcVideoGamma, 1.0f, 0.5f, 2.5f,
                          SETTING_SCOPE_LIVE, "GE007_GAMMA",
                          "--config-override Video.Gamma=VALUE",
                          "Gamma",
                          "Output gamma correction. 1.0 leaves colors unchanged.");
    settingsRegisterFloat("Video.Saturation", &g_pcVideoSaturation, 1.15f, 0.0f, 2.0f,
                          SETTING_SCOPE_LIVE, "GE007_SATURATION",
                          "--config-override Video.Saturation=VALUE",
                          "Saturation",
                          "Output color saturation. 1.0 leaves colors unchanged.");
    settingsRegisterFloat("Video.Contrast", &g_pcVideoContrast, 1.08f, 0.5f, 2.0f,
                          SETTING_SCOPE_LIVE, "GE007_CONTRAST",
                          "--config-override Video.Contrast=VALUE",
                          "Contrast",
                          "Output color contrast. 1.0 leaves colors unchanged.");
    settingsRegisterFloat("Video.Brightness", &g_pcVideoBrightness, 0.0f, -0.5f, 0.5f,
                          SETTING_SCOPE_LIVE, "GE007_BRIGHTNESS",
                          "--config-override Video.Brightness=VALUE",
                          "Brightness",
                          "Output brightness offset. 0.0 leaves colors unchanged.");
    settingsRegisterInt("Video.OutputDither", &g_pcOutputDither, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_OUTPUT_DITHER",
                        "--config-override Video.OutputDither=VALUE",
                        "Output dither",
                        "4x4 ordered Bayer dither to hide RGBA8 banding in skies/fades.");
    settingsRegisterFloat("Video.Vignette", &g_pcVignette, 0.25f, 0.0f, 1.0f,
                          SETTING_SCOPE_LIVE, "GE007_VIGNETTE",
                          "--config-override Video.Vignette=VALUE",
                          "Vignette",
                          "Soft darkening toward screen edges. 0.0 = off.");
    settingsRegisterInt("Video.Bloom", &g_pcBloom, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_BLOOM",
                        "--config-override Video.Bloom=VALUE",
                        "Bloom",
                        "In-shader bright-pass bloom on bright emitters. 0 = off.");
    settingsRegisterFloat("Video.BloomThreshold", &g_pcBloomThreshold, 0.8f, 0.0f, 1.0f,
                          SETTING_SCOPE_LIVE, "GE007_BLOOM_THRESHOLD",
                          "--config-override Video.BloomThreshold=VALUE",
                          "Bloom threshold",
                          "Luma above which pixels contribute to bloom.");
    settingsRegisterFloat("Video.BloomIntensity", &g_pcBloomIntensity, 0.5f, 0.0f, 2.0f,
                          SETTING_SCOPE_LIVE, "GE007_BLOOM_INTENSITY",
                          "--config-override Video.BloomIntensity=VALUE",
                          "Bloom intensity",
                          "Strength of the bloom halo added to the image.");
    settingsRegisterInt("Video.Fxaa", &g_pcFxaa, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_FXAA",
                        "--config-override Video.Fxaa=VALUE",
                        "FXAA",
                        "Fast approximate anti-aliasing on the output pass; cleans sprite/alpha/HUD edges. 0 = off.");
    settingsRegisterFloat("Video.Sharpen", &g_pcSharpen, 0.3f, 0.0f, 1.0f,
                          SETTING_SCOPE_LIVE, "GE007_SHARPEN",
                          "--config-override Video.Sharpen=VALUE",
                          "Sharpen",
                          "Contrast-adaptive output sharpening. 0.0 = off; ~0.3 mild; higher risks ringing.");
    settingsRegisterInt("Video.GradePresets", &g_pcGradePresets, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_GRADE_PRESETS",
                        "--config-override Video.GradePresets=VALUE",
                        "Per-level grade",
                        "Subtle per-level mood color grade composed atop the global grade. 0 = off (identity).");
    settingsRegisterFloat("Video.RenderScale", &g_pcRenderScale, 2.0f, 1.0f, 4.0f,
                          SETTING_SCOPE_RESTART, "GE007_RENDER_SCALE",
                          "--config-override Video.RenderScale=VALUE",
                          "Render scale",
                          "Scene framebuffer scale. 1.0 matches the window; up to 4.0 supersamples (clamped to GPU texture/renderbuffer limits).");
    settingsRegisterEnum("Video.MSAA", &g_pcMsaaSamples, 0,
                         k_msaaOptions,
                         (s32)(sizeof(k_msaaOptions) / sizeof(k_msaaOptions[0])),
                         SETTING_SCOPE_LIVE, "GE007_MSAA",
                         "--config-override Video.MSAA=VALUE",
                         "MSAA",
                         "Scene multisample anti-aliasing samples: 0, 2, 4, or 8.");
    settingsRegisterFloat("Video.FovY", &g_pcFovY, 60.0f, 45.0f, 90.0f,
                          SETTING_SCOPE_LIVE, "GE007_FOV_Y",
                          "--config-override Video.FovY=VALUE",
                          "Vertical FOV",
                          "Gameplay vertical field of view in degrees.");
    settingsRegisterFloat("Video.FogDensity", &g_pcFogDensity, 1.0f, 0.25f, 4.0f,
                          SETTING_SCOPE_LIVE, "GE007_FOG_DENSITY",
                          "--config-override Video.FogDensity=VALUE",
                          "Fog density",
                          "Cosmetic haze thickness multiplier. 1.0 leaves fog unchanged; AI sight range is unaffected.");
    settingsRegisterEnum("Video.RetroFilter", &g_pcRetroFilterMode, PLATFORM_RETRO_FILTER_AUTO,
                         k_retroFilterOptions,
                         (s32)(sizeof(k_retroFilterOptions) / sizeof(k_retroFilterOptions[0])),
                         SETTING_SCOPE_LIVE, "GE007_RETRO_FILTER",
                         "--config-override Video.RetroFilter=VALUE",
                         "Retro filter",
                         "Output VI soft-filter mode: auto, off, or on.");
    settingsRegisterFloat("Input.MouseSensitivity", &g_pcMouseSensitivity, 0.15f, 0.01f, 2.0f,
                          SETTING_SCOPE_LIVE, "GE007_MOUSE_SENSITIVITY",
                          "--config-override Input.MouseSensitivity=VALUE",
                          "Mouse sensitivity",
                          "Mouse-look sensitivity during normal aim.");
    settingsRegisterFloat("Input.MouseSensitivityAim", &g_pcMouseSensAim, 0.05f, 0.005f, 1.0f,
                          SETTING_SCOPE_LIVE, "GE007_MOUSE_SENSITIVITY_AIM",
                          "--config-override Input.MouseSensitivityAim=VALUE",
                          "Aim mouse sensitivity",
                          "Mouse-look sensitivity while aiming.");
    settingsRegisterInt("Input.InvertY", &g_pcInvertY, 0, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_INVERT_Y",
                        "--config-override Input.InvertY=VALUE",
                        "Invert Y axis",
                        "Invert mouse-look Y input.");
    settingsRegisterFloat("Input.GamepadLookSpeed", &g_pcGamepadLookSpeed, 8.0f, 1.0f, 30.0f,
                          SETTING_SCOPE_LIVE, "GE007_GAMEPAD_LOOK_SPEED",
                          "--config-override Input.GamepadLookSpeed=VALUE",
                          "Gamepad look speed",
                          "Right-stick look speed multiplier.");
    settingsRegisterFloat("Input.GamepadLookCurve", &g_pcGamepadLookCurve, 1.5f, 1.0f, 4.0f,
                          SETTING_SCOPE_LIVE, "GE007_GAMEPAD_LOOK_CURVE",
                          "--config-override Input.GamepadLookCurve=VALUE",
                          "Gamepad look curve",
                          "Response exponent: 1.0 linear (vanilla), >1 finer near center.");
    settingsRegisterFloat("Input.GamepadDeadzone", &g_pcGamepadDeadzone, 0.15f, 0.0f, 0.9f,
                          SETTING_SCOPE_LIVE, "GE007_GAMEPAD_DEADZONE",
                          "--config-override Input.GamepadDeadzone=VALUE",
                          "Gamepad deadzone",
                          "Right-stick inner deadzone as a fraction of full deflection (radial mode).");
    settingsRegisterInt("Input.GamepadRadialDeadzone", &g_pcGamepadRadialDeadzone, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_GAMEPAD_RADIAL_DEADZONE",
                        "--config-override Input.GamepadRadialDeadzone=VALUE",
                        "Radial deadzone",
                        "1 = circular deadzone with rescale-from-edge; 0 = legacy square.");
    settingsRegisterInt("Input.GamepadFpsScale", &g_pcGamepadFpsScale, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_GAMEPAD_FPS_SCALE",
                        "--config-override Input.GamepadFpsScale=VALUE",
                        "FPS-independent gamepad",
                        "Scale right-stick look by frame delta so speed is fps-independent.");

    /* ADS (aim-down-sights) — opt-in modern aiming. Master flag ships OFF;
     * when 0 every ADS branch is bypassed and behavior is byte-identical. */
    settingsRegisterInt("Input.AdsEnabled", &g_pcAdsEnabled, 0, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_ADS_ENABLED",
                        "--config-override Input.AdsEnabled=VALUE",
                        "Enable ADS",
                        "Modern aim-down-sights (opt-in). 0 = vanilla aiming.");
    settingsRegisterFloat("Input.AdsSensitivity", &g_pcAdsSensitivity, 1.0f, 0.1f, 2.0f,
                          SETTING_SCOPE_LIVE, "GE007_ADS_SENSITIVITY",
                          "--config-override Input.AdsSensitivity=VALUE",
                          "ADS sensitivity",
                          "Flat look-speed multiplier while aiming.");
    settingsRegisterInt("Input.AdsFovCoupleSens", &g_pcAdsFovCoupleSens, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_ADS_FOV_COUPLE_SENS",
                        "--config-override Input.AdsFovCoupleSens=VALUE",
                        "FOV-coupled ADS sens",
                        "Slow aimed look proportionally to the narrowed FOV.");
    settingsRegisterInt("Input.AdsCenterCrosshair", &g_pcAdsCenterCrosshair, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_ADS_CENTER_CROSSHAIR",
                        "--config-override Input.AdsCenterCrosshair=VALUE",
                        "ADS center crosshair",
                        "Ramp the stick aim accumulators toward center while aiming.");
    settingsRegisterInt("Input.AdsSpreadEnabled", &g_pcAdsSpreadEnabled, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_ADS_SPREAD_ENABLED",
                        "--config-override Input.AdsSpreadEnabled=VALUE",
                        "ADS spread tighten",
                        "Apply the per-weapon spread multiplier while aiming.");
    settingsRegisterInt("Input.AdsMovePenalty", &g_pcAdsMovePenalty, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_ADS_MOVE_PENALTY",
                        "--config-override Input.AdsMovePenalty=VALUE",
                        "ADS movement penalty",
                        "Slow forward/strafe movement while aiming. 0 = vanilla speed.");
    settingsRegisterFloat("Input.AdsMoveScale", &g_pcAdsMoveScale, 1.0f, 0.1f, 2.0f,
                          SETTING_SCOPE_LIVE, "GE007_ADS_MOVE_SCALE",
                          "--config-override Input.AdsMoveScale=VALUE",
                          "ADS move scale",
                          "Trim on the per-weapon aimed forward-speed multiplier.");
    settingsRegisterFloat("Input.AdsStrafeScale", &g_pcAdsStrafeScale, 1.0f, 0.1f, 2.0f,
                          SETTING_SCOPE_LIVE, "GE007_ADS_STRAFE_SCALE",
                          "--config-override Input.AdsStrafeScale=VALUE",
                          "ADS strafe scale",
                          "Trim on the per-weapon aimed strafe-speed multiplier.");
    settingsRegisterInt("Input.AdsFaithfulZoom", &g_pcAdsFaithfulZoom, 0, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_ADS_FAITHFUL_ZOOM",
                        "--config-override Input.AdsFaithfulZoom=VALUE",
                        "Faithful ADS zoom",
                        "Use each weapon's true Zoom (no mild-iron clamp) when aiming.");
    settingsRegisterInt("Input.AdsModelPose", &g_pcAdsModelPose, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_ADS_MODEL_POSE",
                        "--config-override Input.AdsModelPose=VALUE",
                        "ADS model pose",
                        "Blend the weapon model toward the sighted pose while aiming.");
    settingsRegisterFloat("Input.AdsRecoilReduce", &g_pcAdsRecoilReduce, 0.0f, 0.0f, 1.0f,
                          SETTING_SCOPE_LIVE, "GE007_ADS_RECOIL_REDUCE",
                          "--config-override Input.AdsRecoilReduce=VALUE",
                          "ADS recoil reduce",
                          "Cosmetic aimed recoil reduction (0 = off).");
    settingsRegisterInt("Input.AdsModernReticle", &g_pcAdsModernReticle, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_ADS_MODERN_RETICLE",
                        "--config-override Input.AdsModernReticle=VALUE",
                        "ADS modern reticle",
                        "Clean dot+ticks aiming reticle while aiming (vs the classic crosshair).");
    settingsRegisterInt("Input.AdsSteadyView", &g_pcAdsSteadyView, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_ADS_STEADY_VIEW",
                        "--config-override Input.AdsSteadyView=VALUE",
                        "ADS steady view",
                        "Damp walk/strafe head-bob out of the aimed view (1 = steadier).");
    settingsRegisterFloat("Input.AdsBobFloor", &g_pcAdsBobFloor, 0.15f, 0.0f, 1.0f,
                          SETTING_SCOPE_LIVE, "GE007_ADS_BOB_FLOOR",
                          "--config-override Input.AdsBobFloor=VALUE",
                          "ADS bob floor",
                          "Residual weapon bob kept while aiming (0 = rigid, ~0.15 = subtle).");
    settingsRegisterFloat("Input.ViewmodelSway", &g_pcViewmodelSway, 1.0f, 0.0f, 3.0f,
                          SETTING_SCOPE_LIVE, "GE007_VIEWMODEL_SWAY",
                          "--config-override Input.ViewmodelSway=VALUE",
                          "Viewmodel sway",
                          "Additive breathing sway on the first-person weapon (0 = off, 1 = subtle).");
    settingsRegisterInt("Input.ModernCrosshair", &g_pcModernCrosshair, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_MODERN_CROSSHAIR",
                        "--config-override Input.ModernCrosshair=VALUE",
                        "Modern crosshair",
                        "Always-on crisp dot+ticks hip-fire reticle instead of the textured crosshair (0 = off).");
    settingsRegisterInt("Input.HitMarkers", &g_pcHitMarkers, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_HIT_MARKERS",
                        "--config-override Input.HitMarkers=VALUE",
                        "Hit markers",
                        "Flash a marker on the crosshair when your shot registers (white hit, yellow head, red kill; 0 = off).");
}

void platformGetMouseDelta(int *dx, int *dy) {
    extern int g_freezeInput;

    if (g_freezeInput) {
        *dx = g_pcScriptedMouseDeltaX;
        *dy = g_pcScriptedMouseDeltaY;
    } else {
        *dx = g_mouseDeltaX + g_pcScriptedMouseDeltaX;
        *dy = g_mouseDeltaY + g_pcScriptedMouseDeltaY;
    }
    g_mouseDeltaX = 0;
    g_mouseDeltaY = 0;
    g_pcScriptedMouseDeltaX = 0;
    g_pcScriptedMouseDeltaY = 0;
}

int platformGetMouseWheel(void) {
    int v = g_mouseWheelY;
    g_mouseWheelY = 0;
    return v;
}

/* Right stick raw values for direct aim injection (bypasses C-button acceleration) */
void platformGetRightStick(int *rx_out, int *ry_out) {
    int rx = 0, ry = 0;
    if (g_gameController) {
        rx = SDL_GameControllerGetAxis(g_gameController, SDL_CONTROLLER_AXIS_RIGHTX);
        ry = SDL_GameControllerGetAxis(g_gameController, SDL_CONTROLLER_AXIS_RIGHTY);
        /* Apply deadzone */
        if (rx > -8000 && rx < 8000) rx = 0;
        if (ry > -8000 && ry < 8000) ry = 0;
    }
#ifdef MACOS_APP_BUNDLE
    rx += g_pcBridgeRightStickX;
    ry += g_pcBridgeRightStickY;
    if (rx > 32767) rx = 32767;
    if (rx < -32767) rx = -32767;
    if (ry > 32767) ry = 32767;
    if (ry < -32767) ry = -32767;
#endif
    *rx_out = rx;
    *ry_out = ry;
}

/* ===== Per-pad accessors (multi-controller / split-screen) =====
 * Slot index k is the player number (0..PLATFORM_MAX_PADS-1). Every accessor
 * bounds-checks k and returns neutral values for an absent or out-of-range pad,
 * so callers never touch a NULL handle on hot-unplug. */

int platformGetPadCount(void) {
    int count = 0;
    int k;
    for (k = 0; k < PLATFORM_MAX_PADS; k++) {
        if (g_pads[k].handle) {
            count++;
        }
    }
    return count;
}

/* Raw SDL button state for pad k (0 if absent). Mapping to N64 buttons is done
 * by the caller (stubs.c) so all players share one mapping. */
unsigned int platformGetPadButtons(int k) {
    SDL_GameController *gc;
    unsigned int mask = 0;
    int b;

    if (k < 0 || k >= PLATFORM_MAX_PADS) {
        return 0;
    }
    gc = g_pads[k].handle;
    if (!gc) {
        return 0;
    }
    for (b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
        if (SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)b)) {
            mask |= (1u << b);
        }
    }
    return mask;
}

/* Raw left-stick axes for pad k (range -32768..32767, 0 if absent). */
void platformGetPadLeftStick(int k, int *lx_out, int *ly_out) {
    SDL_GameController *gc;
    int lx = 0, ly = 0;

    if (k >= 0 && k < PLATFORM_MAX_PADS && (gc = g_pads[k].handle) != NULL) {
        lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
        ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
    }
    if (lx_out) *lx_out = lx;
    if (ly_out) *ly_out = ly;
}

/* Raw right-stick axes for pad k with deadzone applied (0 if absent).
 * Matches the deadzone used by the legacy platformGetRightStick(). */
void platformGetPadRightStick(int k, int *rx_out, int *ry_out) {
    SDL_GameController *gc;
    int rx = 0, ry = 0;

    if (k >= 0 && k < PLATFORM_MAX_PADS && (gc = g_pads[k].handle) != NULL) {
        /* Default: legacy per-axis square deadzone (|axis|<8000 -> 0). When the
         * opt-in radial deadzone is on, relax this pre-clip to a small noise
         * floor so lvl.c can apply a true radial magnitude deadzone + rescale
         * (otherwise the square clip would zero diagonals before lvl.c sees
         * them). Default (flag 0) keeps the exact vanilla 8000 clip. */
        int dz_axis = g_pcGamepadRadialDeadzone ? 1638 : 8000;
        rx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX);
        ry = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY);
        if (rx > -dz_axis && rx < dz_axis) rx = 0;
        if (ry > -dz_axis && ry < dz_axis) ry = 0;
    }
#ifdef MACOS_APP_BUNDLE
    /* The Swift bridge only feeds player 1. */
    if (k == 0) {
        rx += g_pcBridgeRightStickX;
        ry += g_pcBridgeRightStickY;
        if (rx > 32767) rx = 32767;
        if (rx < -32767) rx = -32767;
        if (ry > 32767) ry = 32767;
        if (ry < -32767) ry = -32767;
    }
#endif
    if (rx_out) *rx_out = rx;
    if (ry_out) *ry_out = ry;
}

/* Trigger axes for pad k (range 0..32767, 0 if absent). leftTrig/rightTrig may
 * be NULL. */
void platformGetPadTriggers(int k, int *leftTrig, int *rightTrig) {
    SDL_GameController *gc;
    int lt = 0, rt = 0;

    if (k >= 0 && k < PLATFORM_MAX_PADS && (gc = g_pads[k].handle) != NULL) {
        lt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        rt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    }
    if (leftTrig) *leftTrig = lt;
    if (rightTrig) *rightTrig = rt;
}

/* ===== Scheduler references ===== */
/* We need access to the scheduler's client list to send retrace messages */
extern OSSched os_scheduler;

/**
 * Initialize SDL2 and create a window with OpenGL context.
 * Returns 0 on success, -1 on failure.
 */
int platformInitSDL(void) {
    if (getenv("GE007_FLYCAM") != NULL) {
        g_pcDebugFlyCamera = 1;
    }
    if (getenv("GE007_NO_VSYNC") != NULL) {
        g_forceNoVsync = 1;
    }
    g_backgroundWindow = platformEnvFlagEnabled("GE007_BACKGROUND");
    g_disableInputGrab = g_backgroundWindow || platformEnvFlagEnabled("GE007_NO_INPUT_GRAB");
    platformApplyWindowSizeEnv();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "[SDL] Init failed: %s\n", SDL_GetError());
        return -1;
    }

    /* Request OpenGL Core Profile for shader-based rendering */
#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    {
        int disable_highdpi = platformEnvFlagEnabled("GE007_DIAG_DISABLE_HIGHDPI");
        int window_x;
        int window_y;
        Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;

        if (!disable_highdpi) {
            window_flags |= SDL_WINDOW_ALLOW_HIGHDPI;
        }

        if (g_backgroundWindow) {
            window_flags |= SDL_WINDOW_HIDDEN;
        } else {
            window_flags |= SDL_WINDOW_SHOWN;
        }

        platformGetConfiguredWindowPosition(&window_x, &window_y);
        g_sdlWindow = SDL_CreateWindow(
        "MGB64",
        window_x, window_y,
        g_cfgWindowW, g_cfgWindowH,
        window_flags
        );
    }

    if (!g_sdlWindow) {
        fprintf(stderr, "[SDL] Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    platformApplyWindowMode();

    g_glContext = SDL_GL_CreateContext(g_sdlWindow);
    if (!g_glContext) {
        fprintf(stderr, "[SDL] GL context creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_sdlWindow);
        SDL_Quit();
        return -1;
    }

    /* Load OpenGL function pointers via glad (not needed on macOS) */
#ifndef __APPLE__
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "[SDL] Failed to load OpenGL functions via glad\n");
        SDL_GL_DeleteContext(g_glContext);
        SDL_DestroyWindow(g_sdlWindow);
        SDL_Quit();
        return -1;
    }
#endif

    /* macOS can block indefinitely in SwapWindow when the test window never
     * receives focus. Allow explicit no-vsync runs for automated capture. */
    platformApplyVSync();
    if (g_forceNoVsync) {
        printf("[SDL] VSync disabled (GE007_NO_VSYNC)\n");
    }

    g_lastFrameTime = SDL_GetTicks();
    printf("[SDL] Window created (OpenGL %s, GLSL %s)\n",
           glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
    if (platformEnvFlagEnabled("GE007_DIAG_DISABLE_HIGHDPI")) {
        printf("[SDL] HiDPI window mode disabled (GE007_DIAG_DISABLE_HIGHDPI)\n");
    }
    if (g_backgroundWindow) {
        printf("[SDL] Background window mode enabled (GE007_BACKGROUND)\n");
    }
    if (g_disableInputGrab) {
        printf("[SDL] Input grab disabled (GE007_NO_INPUT_GRAB)\n");
    }

    /* Open every available game controller into its own player slot. The first
     * opened pad lands in slot 0 (player 1) and shares that slot with the
     * keyboard/mouse merge in stubs.c. */
    for (int i = 0; i < PLATFORM_MAX_PADS; i++) {
        g_pads[i].handle      = NULL;
        g_pads[i].instance_id = -1;
        g_pads[i].slot        = i;
    }
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (platformOpenPad(i) < 0 && platformGetPadCount() >= PLATFORM_MAX_PADS) {
            break; /* table full */
        }
    }
    platformSyncPad0Alias();

    return 0;
}

/**
 * Process SDL events and check for quit.
 */
void platformPollEvents(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                g_sdlQuit = 1;
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                    /* Restore the configured swap interval when focus returns. */
                    platformApplyVSync();
                } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    /* Disable vsync when unfocused to prevent macOS SwapWindow hang */
                    SDL_GL_SetSwapInterval(0);
                } else if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    gfx_set_window_size(event.window.data1, event.window.data2);
                    platformRememberWindowGeometry();
                } else if (event.window.event == SDL_WINDOWEVENT_MOVED) {
                    platformRememberWindowGeometry();
                }
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_RETURN &&
                    (event.key.keysym.mod & KMOD_ALT)) {
                    g_windowMode = (g_windowMode == PLATFORM_WINDOW_MODE_WINDOWED)
                        ? PLATFORM_WINDOW_MODE_BORDERLESS
                        : PLATFORM_WINDOW_MODE_WINDOWED;
                    platformApplyWindowMode();
                } else if (event.key.keysym.sym == SDLK_ESCAPE && !event.key.repeat) {
                    if (g_mouseGrabbed) {
                        /* In gameplay: pause (START_BUTTON) and ungrab mouse */
                        g_pcEscapePressed = 1;  /* 1 = was in gameplay → START */
                        SDL_SetRelativeMouseMode(SDL_FALSE);
                        g_mouseGrabbed = 0;
                    } else {
                        /* In menus: back (B_BUTTON) */
                        g_pcEscapePressed = 2;  /* 2 = was in menus → B */
                    }
                } else if (event.key.keysym.sym == SDLK_c ||
                           event.key.keysym.sym == SDLK_LCTRL) {
                    if (!event.key.repeat) {
                        g_pcCrouchToggle = 1;
                    }
                } else if (event.key.keysym.sym == SDLK_F1) {
                    g_pcDebugFlyCamera = !g_pcDebugFlyCamera;
                    printf("[SDL] Fly camera %s (F1 toggle)\n",
                           g_pcDebugFlyCamera ? "ON" : "OFF — gameplay input active");
                } else if (event.key.keysym.sym == SDLK_F2) {
                    g_screenshotRequested = 1;
                } else if (event.key.keysym.sym == SDLK_h && !event.key.repeat) {
                    printf("\n"
                        "=== CONTROLS ===\n"
                        "WASD        Move\n"
                        "Mouse       Look\n"
                        "L Click     Fire\n"
                        "R Click     Aim (hold)\n"
                        "Scroll      Cycle weapon\n"
                        "R           Reload\n"
                        "F           Interact\n"
                        "C / LCtrl   Crouch toggle\n"
                        "Q / E       Lean L/R (aim mode)\n"
                        "Esc         Pause\n"
                        "Tab         Watch menu\n"
                        "M           Mute audio\n"
                        "H           Show this help\n"
                        "\n"
                        "GAMEPAD: LT=Aim RT=Fire Y=Next weapon Back=Prev weapon L3=Crouch\n"
                        "================\n");
                } else if (event.key.keysym.sym == SDLK_m && !event.key.repeat) {
                    /* M key: toggle audio mute on the unified queue device. */
                    platformToggleAudioMute("M key toggle");
                } else if (event.key.keysym.sym == SDLK_BACKQUOTE) {
                    extern void debugDumpRequest(void);
                    debugDumpRequest();
                } else if (event.key.keysym.sym >= SDLK_F3 && event.key.keysym.sym <= SDLK_F12) {
                    /* F3-F12: Quick level switch.
                     * F3=Dam(33), F4=Facility(34), F5=Runway(35), F6=Surface(36),
                     * F7=Bunker1(9), F8=Silo(20), F9=Frigate(26), F10=Surface2(43),
                     * F11=Jungle(37), F12=Cradle(41) */
                    static const int levelMap[10] = {33,34,35,36,9,20,26,43,37,41};
                    int idx = event.key.keysym.sym - SDLK_F3;
                    extern int g_pcStartLevel;
                    extern void bossSetLoadedStage(int);
                    extern void set_solo_and_ptr_briefing(int);
                    extern int selected_stage;
                    g_pcStartLevel = levelMap[idx];
                    selected_stage = levelMap[idx];
                    set_solo_and_ptr_briefing(selected_stage);
                    bossSetLoadedStage(selected_stage);
                    pcPrimePostStageMenuForDirectBoot((LEVELID)selected_stage, FALSE);
                    printf("[SDL] Level switch requested: LEVELID %d (F%d)\n",
                           levelMap[idx], idx + 3);
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (!g_disableInputGrab && !g_mouseGrabbed) {
                    SDL_SetRelativeMouseMode(SDL_TRUE);
                    g_mouseGrabbed = 1;
                    g_pcMouseRegrabFrame = 1; /* suppress fire on regrab click */
                }
                break;
            case SDL_MOUSEWHEEL: {
                int wy = event.wheel.y;
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) wy = -wy;
                g_mouseWheelY += wy;
                break;
            }
            case SDL_CONTROLLERDEVICEADDED:
                /* event.cdevice.which is a device index for ADDED. */
                platformOpenPad(event.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                /* event.cdevice.which is an instance id for REMOVED. Free only
                 * that slot; other players keep their stable mapping. */
                platformClosePadByInstance(event.cdevice.which);
                break;
            case SDL_MOUSEMOTION:
                if (g_mouseGrabbed) {
                    if (g_pcDebugFlyCamera) {
                        /* Mouse look for fly camera */
                        g_pcCamYaw   += event.motion.xrel * MOUSE_SENSITIVITY;
                        g_pcCamPitch -= event.motion.yrel * MOUSE_SENSITIVITY;
                        if (g_pcCamPitch > 1.5f)  g_pcCamPitch = 1.5f;
                        if (g_pcCamPitch < -1.5f) g_pcCamPitch = -1.5f;
                    } else {
                        /* Accumulate for N64 controller emulation only */
                        g_mouseDeltaX += event.motion.xrel;
                        g_mouseDeltaY += event.motion.yrel;
                    }
                }
                break;
        }
    }

    /* WASD fly camera movement (only when fly camera is active) */
    if (g_pcDebugFlyCamera) {
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        float sx = sinf(g_pcCamYaw), cx = cosf(g_pcCamYaw);
        /* Forward/backward along yaw direction */
        if (keys[SDL_SCANCODE_W]) { g_pcCamX -= sx * FLY_SPEED; g_pcCamZ -= cx * FLY_SPEED; }
        if (keys[SDL_SCANCODE_S]) { g_pcCamX += sx * FLY_SPEED; g_pcCamZ += cx * FLY_SPEED; }
        /* Strafe left/right */
        if (keys[SDL_SCANCODE_A]) { g_pcCamX -= cx * FLY_SPEED; g_pcCamZ += sx * FLY_SPEED; }
        if (keys[SDL_SCANCODE_D]) { g_pcCamX += cx * FLY_SPEED; g_pcCamZ -= sx * FLY_SPEED; }
        /* Up/down */
        if (keys[SDL_SCANCODE_SPACE])  g_pcCamY -= FLY_SPEED;
        if (keys[SDL_SCANCODE_LSHIFT]) g_pcCamY += FLY_SPEED;
    }
}

/**
 * Drain any stale SDL events (e.g. macOS sends SDL_QUIT during long init).
 * Call right before entering the game loop.
 */
void platformDrainEvents(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        /* discard all queued events */
    }
    g_sdlQuit = 0;
}

/**
 * Check if user requested quit.
 */
int platformShouldQuit(void) {
    return g_sdlQuit;
}

/**
 * Wait for next frame boundary and send retrace messages to scheduler clients.
 * This replaces the N64 VI retrace interrupt.
 */
/* Defined in stubs.c — check and fire expired OS timers */
extern void platformCheckTimers(void);

void platformSetWindowTitle(const char *title) {
    if (g_sdlWindow) {
        SDL_SetWindowTitle(g_sdlWindow, title);
    }
}

void platformFrameSync(void) {
    OSScClient *client;

    g_frameSyncCallCount++;
    {
        extern void pcAdvanceDeterministicCountForFrame(void);
        pcAdvanceDeterministicCountForFrame();
    }

    /* Fire any expired OS timers */
    platformCheckTimers();

    /* Process SDL events */
    platformPollEvents();
    platformApplyAutoMuteToggles();

    if (g_sdlQuit) {
        printf("[GE007-PC] Quit requested, shutting down.\n");
        platformShutdownSDL();
        exit(0);
    }

#ifdef MACOS_APP_BUNDLE
    /* Check if the Swift app shell requested shutdown via game_request_shutdown() */
    {
        extern int gameBridgeCheckShutdown(void);
        if (gameBridgeCheckShutdown()) {
            printf("[GameBridge] Shutdown requested by app shell.\n");
            platformShutdownSDL();
            exit(0);
        }
    }
#endif

    /* Frame pacing — wait until next configured frame boundary. */
    u32 now = SDL_GetTicks();
    u32 elapsed = now - g_lastFrameTime;
    u32 frame_delay_ms = platformFrameDelayMs();
    if (frame_delay_ms > 0 && elapsed < frame_delay_ms) {
        SDL_Delay(frame_delay_ms - elapsed);
    }
    g_lastFrameTime = SDL_GetTicks();

    /* Swap the GL framebuffer (shows whatever was rendered) */
    if (g_sdlWindow) {
        if (platformDiagDisplayCastScreenshotDue()) {
            platformSaveScreenshot();
            platformFinishAutoScreenshotIfRequested();
        }
        if (platformDiagMenuScreenshotDue()) {
            platformSaveScreenshot();
            platformFinishAutoScreenshotIfRequested();
        }
        /* Auto-screenshot at specified frame */
        if (g_autoScreenshotFrame >= 0 && g_frameSyncCallCount == g_autoScreenshotFrame) {
            platformSaveScreenshot();
            g_autoScreenshotFrame = -1;
            platformFinishAutoScreenshotIfRequested();
        }
        /* Auto-screenshot at specified gameplay timer. This captures matched
         * simulation states even when startup/loading consumes render frames. */
        if (g_autoScreenshotGameTimer >= 0) {
            extern s32 g_GlobalTimer;
            if (g_GlobalTimer >= g_autoScreenshotGameTimer) {
                platformSaveScreenshot();
                g_autoScreenshotGameTimer = -1;
                platformFinishAutoScreenshotIfRequested();
            }
        }
        /* Manual screenshot (F2) */
        if (g_screenshotRequested) {
            platformSaveScreenshot();
            g_screenshotRequested = 0;
        }
        /* Swap is now handled by gfx_end_frame() — don't double-swap */
    }

    { extern void portAudioFrame(void); portAudioFrame(); }
    /* Only call trace if tracing was requested. Dump-only trace modes do not
     * set g_traceStatePath, so include their env gates here too. */
    if (platformTraceRequested()) {
        extern void portTraceFrame(void);
        portTraceFrame();
    }

    /* Send retrace message to all registered scheduler clients */
    os_scheduler.frameCount++;
    for (client = os_scheduler.clientList; client != NULL; client = client->next) {
        osSendMesg(client->msgQ, (OSMesg)&os_scheduler.retraceMsg, OS_MESG_NOBLOCK);
    }
}

/**
 * Shutdown SDL2.
 */
void platformShutdownSDL(void) {
    /* Save config on clean shutdown */
    platformRememberWindowGeometry();
    configSave();
    /* NOTE: portTraceShutdown() is NOT called here. DL buffer overruns
     * can corrupt static FILE pointers in port_trace.c, causing fclose()
     * to SIGSEGV. The OS reclaims all file handles on process exit.
     * The trace file is flushed every 60 frames, so data loss is minimal. */
    for (int i = 0; i < PLATFORM_MAX_PADS; i++) {
        if (g_pads[i].handle) {
            SDL_GameControllerClose(g_pads[i].handle);
            g_pads[i].handle      = NULL;
            g_pads[i].instance_id = -1;
        }
    }
    g_gameController   = NULL;
    g_gameControllerID = -1;
    /* After SIGSEGV recovery via siglongjmp, the GL context may be corrupt.
     * Skip GL cleanup and just destroy the window + quit SDL. The OS will
     * reclaim the GL resources on process exit anyway. */
    extern volatile int g_gfxRecoveryActive;
    extern int g_crashRecoveryCount;
    if (g_crashRecoveryCount == 0 && g_glContext) {
        SDL_GL_DeleteContext(g_glContext);
    }
    if (g_sdlWindow) SDL_DestroyWindow(g_sdlWindow);
    SDL_Quit();
}
