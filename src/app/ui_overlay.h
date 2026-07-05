// ui_overlay.h — in-game overlay (F1): live settings, return-to-launcher, quit.
#ifndef MGB64_UI_OVERLAY_H
#define MGB64_UI_OVERLAY_H

struct SDL_Window;

// Register the overlay hooks with the engine. Call before mgb64_engine_boot().
// argv0 is the app executable path, used to re-exec back to the launcher.
void Overlay_install(SDL_Window *window, const char *argv0);

#endif  // MGB64_UI_OVERLAY_H
