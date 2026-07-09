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

/* ===== Gamepad bindings (player 1) =====
 * A binding value is encoded as: a button index 0..SDL_CONTROLLER_BUTTON_MAX-1,
 * or GB_AXIS_BASE + axis for a trigger axis (LT/RT), or GB_NONE for unbound.
 * The encoding stays internal to this file + the consumer (stubs.c reads it via
 * gamepadBindingActive), so input_actions.h needs no SDL dependency. */
#define GB_NONE       (-1)
#define GB_AXIS_BASE  1000
#define GB_TRIG_THRESH 8000   /* == GAMEPAD_DEADZONE in stubs.c */

static const int kGpDefault[GB_COUNT] = {
    GB_AXIS_BASE + SDL_CONTROLLER_AXIS_TRIGGERRIGHT, /* GB_FIRE        */
    GB_AXIS_BASE + SDL_CONTROLLER_AXIS_TRIGGERLEFT,  /* GB_AIM         */
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,             /* GB_ALT_FIRE    */
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER,              /* GB_LOOK        */
    SDL_CONTROLLER_BUTTON_A,                         /* GB_JUMP        */
    SDL_CONTROLLER_BUTTON_B,                         /* GB_RELOAD      */
    SDL_CONTROLLER_BUTTON_START,                     /* GB_PAUSE       */
    SDL_CONTROLLER_BUTTON_Y,                         /* GB_WEAPON_NEXT */
    SDL_CONTROLLER_BUTTON_RIGHTSTICK,                /* GB_WEAPON_PREV */
    SDL_CONTROLLER_BUTTON_LEFTSTICK,                 /* GB_CROUCH      */
    SDL_CONTROLLER_BUTTON_DPAD_UP,                   /* GB_LOOK_UP     */
    SDL_CONTROLLER_BUTTON_DPAD_DOWN,                 /* GB_LOOK_DOWN   */
    SDL_CONTROLLER_BUTTON_DPAD_LEFT,                 /* GB_LOOK_LEFT   */
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT,                /* GB_LOOK_RIGHT  */
};

static const char *kGpName[GB_COUNT] = {
    "fire", "aim", "alt_fire", "look_l", "jump", "reload", "pause",
    "weapon_next", "weapon_prev", "crouch",
    "look_up", "look_down", "look_left", "look_right",
};

static const char *kGpLabel[GB_COUNT] = {
    "Fire", "Aim", "Alt Fire", "Look/Zoom (N64 L)", "Jump (N64 A)", "Reload / Use",
    "Pause / Watch (N64 Start)", "Next Weapon", "Previous Weapon", "Crouch",
    "Look Up", "Look Down", "Look Left", "Look Right",
};

/* Pretty names for the standard XInput/SDL button indices. */
static const char *kGpButtonName[SDL_CONTROLLER_BUTTON_MAX] = {
    "A", "B", "X", "Y", "Back", "Guide", "Start",
    "Left Stick Click", "Right Stick Click",
    "Left Bumper", "Right Bumper",
    "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right",
};

static int g_gpBind[GB_COUNT];
static int g_gpInit = 0;
static int g_gpForce = 0;

static void gpEnsureInit(void) {
    if (!g_gpInit) {
        memcpy(g_gpBind, kGpDefault, sizeof(g_gpBind));
        g_gpInit = 1;
    }
}

static int gpValid(int v) {
    if (v == GB_NONE) return 1;
    if (v >= GB_AXIS_BASE)
        return (v == GB_AXIS_BASE + SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
                v == GB_AXIS_BASE + SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    return (v >= 0 && v < SDL_CONTROLLER_BUTTON_MAX);
}

int gamepadBindingCount(void) { return GB_COUNT; }

const char *gamepadActionLabel(GamepadAction a) {
    return (a >= 0 && a < GB_COUNT) ? kGpLabel[a] : "?";
}

static const char *gpEncodedName(int v) {
    if (v == GB_NONE) return "None";
    if (v == GB_AXIS_BASE + SDL_CONTROLLER_AXIS_TRIGGERLEFT)  return "Left Trigger";
    if (v == GB_AXIS_BASE + SDL_CONTROLLER_AXIS_TRIGGERRIGHT) return "Right Trigger";
    if (v >= 0 && v < SDL_CONTROLLER_BUTTON_MAX) return kGpButtonName[v];
    return "?";
}

const char *gamepadBindingName(GamepadAction a) {
    gpEnsureInit();
    if (a < 0 || a >= GB_COUNT) return "?";
    return gpEncodedName(g_gpForce ? kGpDefault[a] : g_gpBind[a]);
}

void gamepadBindingSetButton(GamepadAction a, int sdl_button) {
    gpEnsureInit();
    if (a >= 0 && a < GB_COUNT && sdl_button >= 0 && sdl_button < SDL_CONTROLLER_BUTTON_MAX)
        g_gpBind[a] = sdl_button;
}

void gamepadBindingSetTrigger(GamepadAction a, int sdl_axis) {
    gpEnsureInit();
    if (a >= 0 && a < GB_COUNT &&
        (sdl_axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
         sdl_axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT))
        g_gpBind[a] = GB_AXIS_BASE + sdl_axis;
}

void gamepadBindingResetDefaults(void) {
    memcpy(g_gpBind, kGpDefault, sizeof(g_gpBind));
    g_gpInit = 1;
}

void gamepadBindingForceDefaults(int on) { g_gpForce = on; }

int gamepadBindingActive(void *handle, GamepadAction a) {
    SDL_GameController *gc = (SDL_GameController *)handle;
    int v;
    if (!gc || a < 0 || a >= GB_COUNT) return 0;
    v = g_gpForce ? kGpDefault[a] : g_gpBind[a];
    if (v == GB_NONE) return 0;
    if (v >= GB_AXIS_BASE) {
        int axis = v - GB_AXIS_BASE;
        return SDL_GameControllerGetAxis(gc, (SDL_GameControllerAxis)axis) > GB_TRIG_THRESH;
    }
    return SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)v) ? 1 : 0;
}

void gamepadBindingSave(void) {
    gpEnsureInit();
    FILE *f = fopen("ge007_gp_bindings.ini", "w");
    int i;
    if (!f) return;
    fprintf(f, "# MGB64 gamepad bindings (action=encoded: button index, or %d+axis for LT/RT, %d=none)\n",
            GB_AXIS_BASE, GB_NONE);
    for (i = 0; i < GB_COUNT; i++) fprintf(f, "%s=%d\n", kGpName[i], g_gpBind[i]);
    fclose(f);
}

void gamepadBindingLoad(void) {
    gpEnsureInit();
    FILE *f = fopen("ge007_gp_bindings.ini", "r");
    char line[128];
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        char *eq;
        int v, i;
        if (line[0] == '#') continue;
        eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        v = atoi(eq + 1);
        for (i = 0; i < GB_COUNT; i++) {
            if (strcmp(line, kGpName[i]) == 0) {
                if (gpValid(v)) g_gpBind[i] = v;
                break;
            }
        }
    }
    fclose(f);
}
