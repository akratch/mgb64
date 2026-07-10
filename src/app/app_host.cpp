// app_host.cpp — see app_host.h.
#include "app_host.h"
#include "app_theme.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

#include <cstdio>
#include <cstdint>
#include <vector>

#if defined(__APPLE__)
#  include <OpenGL/gl3.h>
#else
// Non-Apple: the engine uses glad; the app shell resolves GL via SDL. We only
// call a couple of raw GL entry points (glClear/glViewport), declared here to
// avoid a hard glad dependency in the shell TU. They resolve at link time
// against the same GL the engine links.
#  include <glad/glad.h>
#endif

// The GLSL version string handed to the ImGui GL3 backend. Matches the engine's
// "#version 330 core" shaders; valid on the macOS 4.1-core context too.
static const char *kGlslVersion = "#version 330 core";

bool AppHost::init(const char *title, int width, int height) {
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) < 0) {
            std::fprintf(stderr, "[app] SDL_Init failed: %s\n", SDL_GetError());
            return false;
        }
        sdlOwned_ = true;
    }

    // Request the SAME GL context attributes the engine uses (platform_sdl.c).
#if defined(MGB64_PORTMASTER_GLES)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#elif defined(__APPLE__)
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

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                   SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN;
    window_ = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               width, height, flags);
    if (!window_) {
        std::fprintf(stderr, "[app] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    gl_ = SDL_GL_CreateContext(window_);
    if (!gl_) {
        std::fprintf(stderr, "[app] SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(window_, gl_);
    SDL_GL_SetSwapInterval(1);  // vsync

#if !defined(__APPLE__)
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::fprintf(stderr, "[app] gladLoadGLLoader failed\n");
        return false;
    }
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;  // don't litter imgui.ini in the cwd (persist later)
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    AppTheme::setup(framebufferScale());
    if (!ImGui_ImplSDL2_InitForOpenGL(window_, gl_)) {
        std::fprintf(stderr, "[app] ImGui_ImplSDL2_InitForOpenGL failed\n");
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init(kGlslVersion)) {
        std::fprintf(stderr, "[app] ImGui_ImplOpenGL3_Init failed\n");
        return false;
    }
    imguiReady_ = true;
    return true;
}

// Auto heuristic for UI.LauncherFullscreen=auto: fill the display when it looks
// like a handheld panel — physically small (high reported DPI) or low-resolution
// — and stay windowed on a desktop monitor. Best-effort: SDL_GetDisplayDPI is
// unreliable on some platforms (returns 0 / a 96 default), so a panel whose DPI
// isn't reported and whose short edge is > 800 stays windowed; those users can
// force UI.LauncherFullscreen=on. Never over-triggers on a real desktop monitor,
// which is the invariant that keeps the desktop dev workflow windowed.
static bool autoWantsFullscreen(SDL_Window *w) {
    if (!w) return false;
    int disp = SDL_GetWindowDisplayIndex(w);
    if (disp < 0) disp = 0;
    SDL_Rect b;
    if (SDL_GetDisplayBounds(disp, &b) != 0) return false;
    const int shortEdge = (b.w < b.h) ? b.w : b.h;

    float ddpi = 0.0f, hdpi = 0.0f, vdpi = 0.0f;
    const bool haveDpi = (SDL_GetDisplayDPI(disp, &ddpi, &hdpi, &vdpi) == 0) && ddpi > 1.0f;

    // ~7" 1080p+ handheld panels report ~180+ DPI; 24"+ desktop monitors ~90-110.
    if (haveDpi && ddpi >= 180.0f) return true;
    // Small / low-resolution panel (e.g. 1280x720/800), regardless of DPI query.
    if (shortEdge > 0 && shortEdge <= 800) return true;
    return false;
}

bool AppHost::applyLauncherFullscreen(int mode) {
    if (!window_) return false;
    bool wantFs;
    if (mode == 1) {          // on
        wantFs = true;
    } else if (mode == 2) {   // off
        wantFs = false;
    } else {                  // auto
        wantFs = autoWantsFullscreen(window_);
    }
    if (!wantFs) return false;

    if (SDL_SetWindowFullscreen(window_, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
        std::fprintf(stderr, "[app] launcher fullscreen failed: %s\n", SDL_GetError());
        return false;
    }
    std::fprintf(stderr, "[app] launcher: borderless fullscreen (mode=%d)\n", mode);
    return true;
}

void AppHost::beginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    int w = drawableWidth(), h = drawableHeight();
    glViewport(0, 0, w, h);
    glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

// Save the current GL back buffer as a bottom-up 24-bit BMP. GL reads
// bottom-to-top, which matches BMP row order, so no vertical flip is needed.
static void writeBackbufferBmp(const char *path, int w, int h) {
    if (w <= 0 || h <= 0) return;
    std::vector<uint8_t> rgb((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());

    const int rowBytes = w * 3;
    const int pad = (4 - (rowBytes % 4)) % 4;
    const size_t imgSize = (size_t)(rowBytes + pad) * (size_t)h;  // size_t: no int overflow on 8K+
    const size_t fileSize = 54 + imgSize;

    FILE *f = std::fopen(path, "wb");
    if (!f) { std::fprintf(stderr, "[app] could not open %s\n", path); return; }
    uint8_t hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = fileSize & 0xFF; hdr[3] = (fileSize >> 8) & 0xFF;
    hdr[4] = (fileSize >> 16) & 0xFF; hdr[5] = (fileSize >> 24) & 0xFF;
    hdr[10] = 54;                       // pixel data offset
    hdr[14] = 40;                       // DIB header size
    hdr[18] = w & 0xFF; hdr[19] = (w >> 8) & 0xFF;
    hdr[20] = (w >> 16) & 0xFF; hdr[21] = (w >> 24) & 0xFF;
    hdr[22] = h & 0xFF; hdr[23] = (h >> 8) & 0xFF;
    hdr[24] = (h >> 16) & 0xFF; hdr[25] = (h >> 24) & 0xFF;
    hdr[26] = 1;                        // planes
    hdr[28] = 24;                       // bpp
    hdr[34] = imgSize & 0xFF; hdr[35] = (imgSize >> 8) & 0xFF;
    hdr[36] = (imgSize >> 16) & 0xFF; hdr[37] = (imgSize >> 24) & 0xFF;
    std::fwrite(hdr, 1, 54, f);

    std::vector<uint8_t> row(rowBytes + pad, 0);
    for (int y = 0; y < h; ++y) {
        const uint8_t *src = &rgb[(size_t)y * rowBytes];
        for (int x = 0; x < w; ++x) {  // RGB -> BGR
            row[x * 3 + 0] = src[x * 3 + 2];
            row[x * 3 + 1] = src[x * 3 + 1];
            row[x * 3 + 2] = src[x * 3 + 0];
        }
        std::fwrite(row.data(), 1, rowBytes + pad, f);
    }
    std::fclose(f);
    std::fprintf(stderr, "[app] wrote %s (%dx%d)\n", path, w, h);
}

void AppHost::endFrame(const char *captureBmpPath) {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (captureBmpPath) {
        writeBackbufferBmp(captureBmpPath, drawableWidth(), drawableHeight());
    }
    SDL_GL_SwapWindow(window_);
}

float AppHost::framebufferScale() const {
    int lw = 0, lh = 0, dw = 0, dh = 0;
    if (window_) {
        SDL_GetWindowSize(window_, &lw, &lh);
        SDL_GL_GetDrawableSize(window_, &dw, &dh);
    }
    return (lw > 0) ? (float)dw / (float)lw : 1.0f;
}

bool AppHost::pumpAndShouldQuit() {
    bool quit = false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        ImGui_ImplSDL2_ProcessEvent(&e);
        if (e.type == SDL_QUIT) {
            quit = true;
        } else if (e.type == SDL_DROPFILE && e.drop.file) {
            droppedFile_ = e.drop.file;  // consumed by takeDroppedFile()
            SDL_free(e.drop.file);
        } else if (e.type == SDL_WINDOWEVENT &&
                   e.window.event == SDL_WINDOWEVENT_CLOSE &&
                   e.window.windowID == SDL_GetWindowID(window_)) {
            quit = true;
        }
    }
    return quit;
}

int AppHost::drawableWidth() const {
    int w = 0, h = 0;
    if (window_) SDL_GL_GetDrawableSize(window_, &w, &h);
    return w;
}

int AppHost::drawableHeight() const {
    int w = 0, h = 0;
    if (window_) SDL_GL_GetDrawableSize(window_, &w, &h);
    return h;
}

std::string AppHost::takeDroppedFile() {
    std::string s;
    s.swap(droppedFile_);
    return s;
}

void AppHost::shutdown() {
    if (imguiReady_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        imguiReady_ = false;
    }
    if (gl_) { SDL_GL_DeleteContext(gl_); gl_ = nullptr; }
    if (window_) { SDL_DestroyWindow(window_); window_ = nullptr; }
    if (sdlOwned_) { SDL_Quit(); sdlOwned_ = false; }
}
