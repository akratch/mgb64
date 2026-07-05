// app_host.h — portable app-shell host: owns the SDL window + GL context + ImGui.
//
// The AppHost is the cross-platform equivalent of the macOS Swift AppDelegate:
// it creates the window and rendering context, drives ImGui, and (in later
// tasks) hands the window to the engine via platformSetHostWindow() when the
// user starts a game. It requests the SAME GL attributes the engine's
// platform_sdl.c uses, so the engine's fast3d renderer works correctly when it
// adopts this context.
#ifndef MGB64_APP_HOST_H
#define MGB64_APP_HOST_H

#include <SDL.h>

class AppHost {
public:
    // Create the window + GL context + ImGui. Returns false on failure.
    bool init(const char *title, int width, int height);

    // Begin an ImGui frame and clear the framebuffer.
    void beginFrame();

    // Render ImGui draw data and present the frame. If captureBmpPath is
    // non-null, the finished frame is saved as a 24-bit BMP before the swap
    // (used by the headless smoke / design review / CI).
    void endFrame(const char *captureBmpPath = nullptr);

    // Framebuffer/logical ratio (2.0 on Retina). Valid after init().
    float framebufferScale() const;

    // Pump SDL events into ImGui. Returns true when the user requested quit
    // (window close / Cmd-Q).
    bool pumpAndShouldQuit();

    // Tear down ImGui + GL + SDL.
    void shutdown();

    SDL_Window   *window()    const { return window_; }
    SDL_GLContext glContext() const { return gl_; }
    int drawableWidth()  const;
    int drawableHeight() const;

private:
    SDL_Window   *window_ = nullptr;
    SDL_GLContext gl_     = nullptr;
    bool imguiReady_      = false;
    bool sdlOwned_        = false;  // did we SDL_Init (vs. reuse an existing init)?
};

#endif  // MGB64_APP_HOST_H
