// input_actions.h — the app<->engine input-binding contract (single source).
//
// Included by the engine (stubs.c reads bindings; main_pc.c loads them / forces
// defaults) AND by the app's Controls panel. Kept collision-free in src/app so
// both sides share ONE definition; NOT gated on MGB64_APP, so the bare engine
// still has a working (default) keymap.
#ifndef MGB64_INPUT_ACTIONS_H
#define MGB64_INPUT_ACTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

// Rebindable keyboard actions. Non-rebindable keys (Enter/Tab/Esc, the arrow
// C-aim, I/J/K/L d-pad, and the alternate reload/lean keys) stay hardcoded.
typedef enum {
    IB_FORWARD,
    IB_BACK,
    IB_LEFT,
    IB_RIGHT,
    IB_FIRE,
    IB_AIM,
    IB_RELOAD,
    IB_LEAN_L,
    IB_LEAN_R,
    IB_COUNT
} InputAction;

int inputBindingCount(void);                 // == IB_COUNT
const char *inputActionLabel(InputAction a);
int  inputBindingScancode(InputAction a);    // current SDL scancode (or default under force)
void inputBindingSet(InputAction a, int sdl_scancode);
void inputBindingResetDefaults(void);
void inputBindingLoad(void);                 // from ge007_bindings.ini (no-op if absent)
void inputBindingSave(void);
void inputBindingForceDefaults(int on);      // automation/deterministic: ignore the user file

#ifdef __cplusplus
}
#endif

#endif  // MGB64_INPUT_ACTIONS_H
