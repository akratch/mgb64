// input_bindings.c — rebindable keyboard registry. Defaults are byte-identical
// to the previously-hardcoded map in stubs.c. See input_actions.h.
#include "../app/input_actions.h"

#include <SDL.h>

#include <stdio.h>
#include <string.h>

static const int kDefault[IB_COUNT] = {
    SDL_SCANCODE_W,       // IB_FORWARD
    SDL_SCANCODE_S,       // IB_BACK
    SDL_SCANCODE_A,       // IB_LEFT
    SDL_SCANCODE_D,       // IB_RIGHT
    SDL_SCANCODE_LSHIFT,  // IB_FIRE
    SDL_SCANCODE_RSHIFT,  // IB_AIM
    SDL_SCANCODE_R,       // IB_RELOAD (F / Backspace remain hardcoded alternates)
    SDL_SCANCODE_Q,       // IB_LEAN_L (Left arrow remains a hardcoded alternate)
    SDL_SCANCODE_E,       // IB_LEAN_R (Right arrow remains a hardcoded alternate)
};

static const char *kName[IB_COUNT] = {
    "forward", "back", "left", "right", "fire", "aim", "reload", "lean_l", "lean_r",
};

static const char *kLabel[IB_COUNT] = {
    "Move Forward", "Move Back", "Strafe Left", "Strafe Right",
    "Fire", "Aim", "Reload / Use", "Lean Left", "Lean Right",
};

static int g_binding[IB_COUNT];
static int g_initialized = 0;
static int g_force = 0;

static void ensureInit(void) {
    if (!g_initialized) {
        memcpy(g_binding, kDefault, sizeof(g_binding));
        g_initialized = 1;
    }
}

int inputBindingCount(void) { return IB_COUNT; }

const char *inputActionLabel(InputAction a) {
    return (a >= 0 && a < IB_COUNT) ? kLabel[a] : "?";
}

int inputBindingScancode(InputAction a) {
    ensureInit();
    if (a < 0 || a >= IB_COUNT) return 0;
    return g_force ? kDefault[a] : g_binding[a];
}

void inputBindingSet(InputAction a, int sc) {
    ensureInit();
    if (a >= 0 && a < IB_COUNT && sc > 0 && sc < SDL_NUM_SCANCODES) g_binding[a] = sc;
}

void inputBindingResetDefaults(void) {
    memcpy(g_binding, kDefault, sizeof(g_binding));
    g_initialized = 1;
}

void inputBindingForceDefaults(int on) { g_force = on; }

void inputBindingSave(void) {
    ensureInit();
    FILE *f = fopen("ge007_bindings.ini", "w");
    if (!f) return;
    fprintf(f, "# MGB64 input bindings (action=SDL_scancode)\n");
    for (int i = 0; i < IB_COUNT; i++) fprintf(f, "%s=%d\n", kName[i], g_binding[i]);
    fclose(f);
}

void inputBindingLoad(void) {
    ensureInit();
    FILE *f = fopen("ge007_bindings.ini", "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        int sc = atoi(eq + 1);
        for (int i = 0; i < IB_COUNT; i++) {
            if (strcmp(line, kName[i]) == 0) {
                if (sc > 0 && sc < SDL_NUM_SCANCODES) g_binding[i] = sc;
                break;
            }
        }
    }
    fclose(f);
}
