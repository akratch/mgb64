// ui_overlay.cpp — see ui_overlay.h.
#include "ui_overlay.h"
#include "app_theme.h"
#include "engine_entry.h"   // AppOverlayHooks, platformSetOverlayHooks
#include "ui_common.h"
#include "ui_settings.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if !defined(_WIN32)
#include <unistd.h>  // execvp
#endif

namespace {

bool        g_open = false;
bool        g_showSettings = false;
int         g_confirm = 0;   // 0 none, 1 return-to-launcher, 2 quit
bool        g_justOpened = false;  // request initial nav focus on the next render
SDL_Window *g_window = nullptr;
char        g_argv0[1024] = {0};
bool        g_prevRelMouse = false;

void setOpen(bool open) {
    if (open == g_open) return;
    g_open = open;
    g_confirm = 0;
    if (g_open) {
        g_justOpened = true;  // focus the first control so a pad has an anchor
        g_prevRelMouse = (SDL_GetRelativeMouseMode() == SDL_TRUE);
        SDL_SetRelativeMouseMode(SDL_FALSE);  // free the cursor for the overlay
    } else {
        SDL_SetRelativeMouseMode(g_prevRelMouse ? SDL_TRUE : SDL_FALSE);
    }
}

void quitToDesktop() {
    SDL_Event q{};  // zero-init: don't queue uninitialized bytes
    q.type = SDL_QUIT;
    SDL_PushEvent(&q);
}

void returnToLauncher() {
#if !defined(_WIN32)
    if (g_argv0[0]) {
        char *argv[] = {g_argv0, nullptr};
        execvp(g_argv0, argv);  // replaces the process image → fresh launcher
    }
#endif
    quitToDesktop();  // fallback (and Windows, wired in Part 3)
}

void onProcessEvent(const void *ev) {
    const SDL_Event *e = (const SDL_Event *)ev;
    ImGui_ImplSDL2_ProcessEvent(e);

    // Toggle keys/buttons: F1 (keyboard) and Back/View (gamepad) open/close the
    // overlay. Back is the conventional system-UI button and its old default
    // (weapon-prev) is a port invention, so pad Start stays the N64 Start —
    // GoldenEye's watch is the game's core system menu (review F2 adjudication).
    if (e->type == SDL_KEYDOWN && !e->key.repeat && e->key.keysym.sym == SDLK_F1) {
        setOpen(!g_open);
        return;
    }
    if (e->type == SDL_CONTROLLERBUTTONDOWN) {
        // P1-only: in split-screen, P2-4 Start/Back must reach their own N64
        // pad (pcFillPadFromController), not drive the shared overlay — the
        // input gate would freeze every player. (-1 = no pad in slot 0, and no
        // pad means no controller events, so requiring a match is safe.)
        if ((int)e->cbutton.which != platformGetPad0InstanceId()) return;
        if ((int)e->cbutton.button == Overlay_gamepadToggleButton()) {
            setOpen(!g_open);
        } else if (g_open && e->cbutton.button == SDL_CONTROLLER_BUTTON_B) {
            // B = back one level (cancel confirm -> hide settings -> close). Skip
            // when ImGui itself is consuming B (an open combo/popup), so its own
            // nav-cancel closes that first rather than the whole overlay.
            if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
                if (g_confirm) g_confirm = 0;
                else if (g_showSettings) g_showSettings = false;
                else setOpen(false);
            }
        }
    }
}

int onWantsInput() { return g_open ? 1 : 0; }

void onRender() {
    if (!g_open) return;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(8, 9, 11, 180));

    float w = g_showSettings ? 720.0f : 440.0f;
    float h = g_confirm ? 250.0f : (g_showSettings ? 560.0f : 300.0f);
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::Begin("##overlay", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGui::PushFont(AppTheme::fonts().title);
    ImGui::PushStyleColor(ImGuiCol_Text, AppTheme::accent());
    ImGui::TextUnformatted("MGB64");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ui::TextSubtle("Paused overlay  \xE2\x80\xA2  F1 / View to resume  \xE2\x80\xA2  gamepad: D-pad move, A select, B back");
    ui::Gap(ui::kGapS);
    ImGui::Separator();
    ui::Gap(ui::kGapM);

    // Give a freshly-opened overlay an initial nav focus so a gamepad/keyboard
    // has an anchor without first hunting with the stick. Focuses the next
    // widget drawn (Resume, or the confirm's primary action).
    if (g_justOpened) {
        ImGui::SetKeyboardFocusHere();
        g_justOpened = false;
    }

    if (g_confirm) {
        ui::TextSubtle(g_confirm == 1 ? "Return to the launcher? This ends the current game."
                                      : "Quit to desktop? This ends the current game.");
        ui::Gap(ui::kGapM);
        if (ui::PrimaryButton(g_confirm == 1 ? "Return to Launcher" : "Quit", ui::kBtnWide)) {
            if (g_confirm == 1) returnToLauncher();
            else quitToDesktop();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ui::kBtnSecondary)) g_confirm = 0;
    } else {
        if (ui::PrimaryButton("Resume", ui::kBtnSecondary)) setOpen(false);
        ImGui::SameLine();
        if (ImGui::Button(g_showSettings ? "Hide Settings" : "Settings", ui::kBtnSecondary))
            g_showSettings = !g_showSettings;

        if (g_showSettings) {
            ui::Gap(ui::kGapS);
            ImGui::BeginChild("##ovsettings", ImVec2(0, 340), true);
            Settings_draw();  // live settings apply to the running game immediately
            ImGui::EndChild();
        }

        ui::Gap(ui::kGapM);
        if (ImGui::Button("Return to Launcher", ui::kBtnWide)) g_confirm = 1;
        ImGui::SameLine();
        if (ImGui::Button("Quit to Desktop", ui::kBtnWide)) g_confirm = 2;
    }

    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

}  // namespace

int Overlay_gamepadToggleButton() { return SDL_CONTROLLER_BUTTON_BACK; }

void Overlay_install(SDL_Window *window, const char *argv0) {
    g_window = window;
    if (argv0) std::snprintf(g_argv0, sizeof(g_argv0), "%s", argv0);
    if (std::getenv("MGB64_APP_OVERLAY_TEST")) g_open = true;  // headless render validation
    static AppOverlayHooks hooks;
    hooks.process_event = onProcessEvent;
    hooks.wants_input = onWantsInput;
    hooks.render = onRender;
    platformSetOverlayHooks(&hooks);
}
