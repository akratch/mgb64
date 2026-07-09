// ui_bindings.cpp — Controls panel: press-to-rebind keyboard AND gamepad actions.
#include "ui_launcher.h"
#include "input_actions.h"
#include "ui_common.h"

#include "imgui.h"
#include <SDL.h>

#include <cstdio>

namespace {

// Lazily open (refcounted; shared with the engine/ImGui) the first game
// controller so the capture flow can poll it. Never closed — one ref for the
// app's lifetime is fine.
SDL_GameController *appController() {
    static SDL_GameController *gc = nullptr;
    if (gc && SDL_GameControllerGetAttached(gc)) return gc;
    gc = nullptr;
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n; ++i) {
        if (SDL_IsGameController(i)) {
            gc = SDL_GameControllerOpen(i);
            if (gc) break;
        }
    }
    return gc;
}

void drawKeyboardTable() {
    static int capturing = -1;  // action index awaiting a key, or -1

    // While capturing, grab the first held key (Esc cancels). A mouse click
    // entered capture, so no key is down yet — the next press is the choice.
    if (capturing >= 0) {
        const Uint8 *ks = SDL_GetKeyboardState(NULL);
        if (ks[SDL_SCANCODE_ESCAPE]) {
            capturing = -1;
        } else {
            for (int sc = 4; sc < SDL_NUM_SCANCODES; ++sc) {
                if (ks[sc]) {
                    inputBindingSet((InputAction)capturing, sc);
                    inputBindingSave();
                    capturing = -1;
                    break;
                }
            }
        }
    }

    const int n = inputBindingCount();
    if (ImGui::BeginTable("##binds", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 240.0f);
        for (int i = 0; i < n; ++i) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(inputActionLabel((InputAction)i));

            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(i);
            char label[64];
            if (capturing == i) {
                std::snprintf(label, sizeof(label), "Press a key...");
            } else {
                const char *kn = SDL_GetScancodeName((SDL_Scancode)inputBindingScancode((InputAction)i));
                std::snprintf(label, sizeof(label), "%s", (kn && kn[0]) ? kn : "?");
            }
            bool cap = (capturing == i);
            if (cap) ui::PushPrimaryButtonColors();
            if (ImGui::Button(label, ImVec2(220.0f, 0.0f))) capturing = cap ? -1 : i;
            if (cap) ui::PopPrimaryButtonColors();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ui::Gap(ui::kGapM);
    if (ImGui::Button("Reset Keyboard to Defaults", ImVec2(220.0f, 36.0f))) {
        inputBindingResetDefaults();
        inputBindingSave();
        capturing = -1;
    }
}

void drawGamepadTable() {
    static int capturing = -1;   // gamepad action awaiting a press, or -1
    static bool waitRelease = false;  // debounce: ignore inputs still held from
                                      // the A-press that armed capture

    SDL_GameController *gc = appController();

    // Nav stays enabled (disabling the global flag risks a soft-lock if the user
    // leaves the panel mid-capture). Instead: (1) waitRelease ignores the A that
    // armed the row until release; (2) committedThisFrame stops the same commit
    // press (A = nav-activate on the focused row) from re-arming it below. The
    // capture poll runs before the row buttons, so it always wins the frame.
    bool committedThisFrame = false;

    if (capturing >= 0) {
        // Esc always cancels (keyboard escape hatch).
        const Uint8 *ks = SDL_GetKeyboardState(NULL);
        if (ks[SDL_SCANCODE_ESCAPE]) {
            capturing = -1;
        } else if (!gc) {
            // No pad attached — nothing to capture; leave the row armed.
        } else {
            bool anyDown = false;
            int b;
            for (b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b)
                if (SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)b)) anyDown = true;
            int lt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
            int rt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
            bool trigDown = (lt > 8000) || (rt > 8000);

            if (waitRelease) {
                if (!anyDown && !trigDown) waitRelease = false;
            } else if (rt > 8000) {
                gamepadBindingSetTrigger((GamepadAction)capturing, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
                gamepadBindingSave(); capturing = -1; committedThisFrame = true;
            } else if (lt > 8000) {
                gamepadBindingSetTrigger((GamepadAction)capturing, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
                gamepadBindingSave(); capturing = -1; committedThisFrame = true;
            } else {
                for (b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) {
                    if (SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)b)) {
                        gamepadBindingSetButton((GamepadAction)capturing, b);
                        gamepadBindingSave(); capturing = -1; committedThisFrame = true;
                        break;
                    }
                }
            }
        }
    }

    const int n = gamepadBindingCount();
    if (ImGui::BeginTable("##gpbinds", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed, 240.0f);
        for (int i = 0; i < n; ++i) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(gamepadActionLabel((GamepadAction)i));

            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(1000 + i);  // distinct from the keyboard table IDs
            char label[64];
            if (capturing == i) {
                std::snprintf(label, sizeof(label), "Press a button...");
            } else {
                std::snprintf(label, sizeof(label), "%s", gamepadBindingName((GamepadAction)i));
            }
            bool cap = (capturing == i);
            if (cap) ui::PushPrimaryButtonColors();
            if (ImGui::Button(label, ImVec2(220.0f, 0.0f)) && !committedThisFrame) {
                if (cap) { capturing = -1; }
                else     { capturing = i; waitRelease = true; }
            }
            if (cap) ui::PopPrimaryButtonColors();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ui::Gap(ui::kGapM);
    if (ImGui::Button("Reset Gamepad to Defaults", ImVec2(220.0f, 36.0f))) {
        gamepadBindingResetDefaults();
        gamepadBindingSave();
        capturing = -1;
    }

    if (!gc) ui::TextSubtle("No gamepad detected — connect a controller to rebind.");
}

}  // namespace

void BindingsPanel_draw(LauncherState & /*s*/, LauncherAction & /*out*/) {
    ui::SectionHeader("Controls",
                      "Click a binding, then press a key or button to rebind. Esc cancels.");

    if (ImGui::BeginTabBar("##controlsTabs")) {
        if (ImGui::BeginTabItem("Keyboard")) {
            ui::Gap(ui::kGapS);
            drawKeyboardTable();
            ui::Gap(ui::kGapM);
            ui::TextSubtle("Fixed keys: F / Backspace = Reload, Left / Right arrows = lean, "
                           "I/J/K/L = C-aim, Enter = A, Tab = Start, Esc = pause.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Gamepad")) {
            ui::Gap(ui::kGapS);
            drawGamepadTable();
            ui::Gap(ui::kGapM);
            ui::TextSubtle("Sticks are fixed: left stick = move, right stick = look/aim. "
                           "X is a fixed alternate for Reload.");
            ui::TextSubtle("The Back/View button opens the in-game overlay (with F1) and "
                           "cannot be rebound; Previous Weapon moved to the Right-Stick click.");
            ui::TextSubtle("Player 2\xE2\x80\x93" "4 pads use fixed defaults (multiplayer rebinding "
                           "is out of scope).");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}
