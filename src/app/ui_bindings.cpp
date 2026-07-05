// ui_bindings.cpp — Controls panel: press-to-rebind keyboard actions.
#include "ui_launcher.h"
#include "input_actions.h"
#include "ui_common.h"

#include "imgui.h"
#include <SDL.h>

#include <cstdio>

void BindingsPanel_draw(LauncherState & /*s*/, LauncherAction & /*out*/) {
    ui::SectionHeader("Controls",
                      "Rebind keyboard actions: click a binding, then press a key. Esc cancels.");

    static int capturing = -1;  // action index currently awaiting a key, or -1

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
    if (ImGui::Button("Reset to Defaults", ImVec2(180.0f, 36.0f))) {
        inputBindingResetDefaults();
        inputBindingSave();
        capturing = -1;
    }
    ui::Gap(ui::kGapM);
    ui::TextSubtle("Fixed keys: F / Backspace = Reload, Left / Right arrows = lean, I/J/K/L = C-aim, "
                   "Enter = A, Tab = Start, Esc = pause.");
    ui::TextSubtle("Gamepad (fixed): left stick move, right stick look, RT/RClick fire, LT/RClick aim, "
                   "A jump, B/X reload.");
}
