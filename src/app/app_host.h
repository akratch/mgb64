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

#include <string>

class AppHost {
public:
    // Create the window + GL context + ImGui. Returns false on failure.
    bool init(const char *title, int width, int height);

    // RX.2: size the launcher window per UI.LauncherFullscreen. `mode` is the raw
    // enum value (0=auto, 1=on, 2=off). Auto fills the screen on small/high-DPI
    // handheld panels (the launcher would otherwise "float" on a 1920x1200 7-inch
    // display) and leaves a resizable window on desktop monitors so the dev
    // workflow is unchanged. Safe to call once after init() + config load.
    // Returns true if the window was switched to borderless fullscreen-desktop.
    bool applyLauncherFullscreen(int mode);

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

    // RX.4: the path of a file the user dragged onto the window since the last
    // call (SDL_DROPFILE), or "" if none. Returns + clears it, so each drop is
    // consumed once. The launcher routes it into the ROM panel.
    std::string takeDroppedFile();

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
    std::string droppedFile_;      // last SDL_DROPFILE path, consumed by takeDroppedFile()
};

#endif  // MGB64_APP_HOST_H
