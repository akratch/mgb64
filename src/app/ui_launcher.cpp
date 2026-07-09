// ui_launcher.cpp — launcher shell: brand + table-driven nav rail + router.
// Individual panels live in ui_rom.cpp / ui_launch.cpp / ui_settings.cpp.
#include "ui_launcher.h"
#include "app_host.h"
#include "app_theme.h"
#include "ui_common.h"
#include "ui_settings.h"

#include "imgui.h"

#include <cstdlib>
#include <cstring>

namespace {

// Wrappers so the panel table has a uniform signature.
void SettingsPanel_draw(LauncherState & /*s*/, LauncherAction & /*out*/) {
    ui::SectionHeader("Settings",
                      "Everything configurable \xE2\x80\x94 generated from the engine's config schema.");
    Settings_draw();
}

void AboutPanel_draw(LauncherState & /*s*/, LauncherAction & /*out*/) {
    ui::SectionHeader("About MGB64", "A native source port built on a faithful decompilation.");
    ImGui::TextWrapped(
        "MGB64 ships no game data \xE2\x80\x94 bring your own ROM. This app is one "
        "portable codebase (Dear ImGui rendered in-process) targeting macOS, "
        "Windows, and Linux.");
    ui::Gap(ui::kGapM);
    ui::TextSubtle("Renderer: OpenGL   \xE2\x80\xA2   License: MIT (first-party)");
    ui::Gap(ui::kGapXS);
    ui::TextSubtle("Overlay: press F1 in-game for live settings and quit.");
}

struct PanelDef {
    const char *label;
    const char *envKey;  // MGB64_APP_PANEL keyword
    void (*draw)(LauncherState &, LauncherAction &);
};

const PanelDef kPanels[] = {
    {"Game ROM", "rom", RomPanel_draw},
    {"Settings", "settings", SettingsPanel_draw},
    {"Launch Options", "launch", LaunchPanel_draw},
    {"Controls", "controls", BindingsPanel_draw},
    {"Modes & Toggles", "modes", ModesPanel_draw},
    {"Diagnostics", "diag", DiagPanel_draw},
    {"About", "about", AboutPanel_draw},
};
const int kNumPanels = IM_ARRAYSIZE(kPanels);

// Full-width nav item: accent left-bar + primary tint when selected, white label.
bool navButton(const char *label, bool selected) {
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
    if (selected) {
        ImVec4 p = AppTheme::primary();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(p.x, p.y, p.z, 0.22f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(p.x, p.y, p.z, 0.30f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(p.x, p.y, p.z, 0.38f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));  // white selected label
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    }
    bool clicked = ImGui::Button(label, ImVec2(-FLT_MIN, 42.0f));
    if (selected) {
        ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(mn.x, mn.y + 6), ImVec2(mn.x + 3.0f, mx.y - 6),
                                                  ImGui::GetColorU32(AppTheme::accent()), 2.0f);
        ImGui::PopStyleColor(4);
    } else {
        ImGui::PopStyleColor(1);
    }
    ImGui::PopStyleVar(2);
    return clicked;
}

void drawNavRail(int &active) {
    ImGui::BeginChild("##nav", ImVec2(ui::kNavWidth, 0), true);

    ui::Gap(ui::kGapXS);
    ImGui::PushFont(AppTheme::fonts().title);
    ImGui::PushStyleColor(ImGuiCol_Text, AppTheme::accent());
    ImGui::TextUnformatted("MGB64");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::PushFont(AppTheme::fonts().small);
    ui::TextSubtle("Man with the Golden Build");
    ImGui::PopFont();
    ui::Gap(ui::kGapM);

    for (int i = 0; i < kNumPanels; ++i) {
        if (navButton(kPanels[i].label, active == i)) active = i;
        // Seed initial nav focus on the active tab (once, on window appear) so a
        // gamepad/keyboard has an anchor. Does not force a visible highlight
        // until nav is actually engaged, so mouse/keyboard UX is unchanged.
        if (active == i) ImGui::SetItemDefaultFocus();
    }

    float avail = ImGui::GetContentRegionAvail().y;
    float vh = ImGui::GetFrameHeight();
    if (avail > vh) ui::Gap(avail - vh);
    ImGui::PushFont(AppTheme::fonts().small);
    ui::TextSubtle("v0.3.0-dev \xE2\x80\xA2 GL");
    ImGui::PopFont();

    ImGui::EndChild();
}

}  // namespace

LauncherAction Launcher::draw(AppHost & /*host*/) {
    LauncherAction action;

    if (!panelEnvChecked_) {
        panelEnvChecked_ = true;
        if (const char *p = std::getenv("MGB64_APP_PANEL")) {
            for (int i = 0; i < kNumPanels; ++i) {
                if (std::strcmp(p, kPanels[i].envKey) == 0) { active_ = i; break; }
            }
        }
    }
    RomPanel_ensureInit(state_);  // load remembered ROM regardless of active panel

    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui::kGapM, ui::kGapM));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##launcher", nullptr, flags);

    drawNavRail(active_);

    ImGui::SameLine();
    ImGui::BeginChild("##content", ImVec2(0, 0), false);
    ui::Gap(2.0f);
    if (active_ >= 0 && active_ < kNumPanels) {
        kPanels[active_].draw(state_, action);
    }
    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleVar(2);
    return action;
}
