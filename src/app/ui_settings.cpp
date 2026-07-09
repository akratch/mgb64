// ui_settings.cpp — see ui_settings.h.
#include "ui_settings.h"
#include "config_schema.h"
#include "ui_common.h"

#include "imgui.h"
#include "nfd.h"

#include <cstring>

namespace {

// String settings (e.g. Video.TexturePack): show the current path read-only
// plus a native folder picker and a Clear (= stock). Fixes the old "(unsupported
// type)" fall-through — string settings are now first-class + editable.
void drawStringEntry(const MgbCfgEntry &e, const char *label) {
    char buf[512];
    if (!mgb_config_get_string(e.key, buf, (int)sizeof(buf))) buf[0] = '\0';

    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(ui::kControlWidth);
    ImGui::InputText("##path", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::Button("Browse…")) {
        if (NFD_Init() == NFD_OKAY) {
            nfdu8char_t *out = nullptr;
            if (NFD_PickFolderU8(&out, buf[0] ? buf : nullptr) == NFD_OKAY && out) {
                mgb_config_set_string(e.key, out);
                NFD_FreePathU8(out);
            }
            NFD_Quit();
        }
    }
    if (buf[0]) {
        ImGui::SameLine();
        if (ImGui::Button("Clear")) mgb_config_set_string(e.key, "");
    }
}

void drawEntry(const MgbCfgEntry &e) {
    ImGui::PushID(e.key);
    const char *label = e.label[0] ? e.label : e.name;

    switch (e.kind) {
        case MGB_CFG_INT:
        case MGB_CFG_UINT:
            if (e.min_val == 0.0f && e.max_val == 1.0f) {
                bool b = e.cur_int != 0;
                if (ImGui::Checkbox(label, &b)) mgb_config_set_int(e.key, b ? 1 : 0);
            } else {
                int v = e.cur_int;
                ImGui::SetNextItemWidth(ui::kControlWidth);
                if (ImGui::SliderInt(label, &v, (int)e.min_val, (int)e.max_val))
                    mgb_config_set_int(e.key, v);
            }
            break;
        case MGB_CFG_FLOAT: {
            float v = e.cur_float;
            ImGui::SetNextItemWidth(ui::kControlWidth);
            if (ImGui::SliderFloat(label, &v, e.min_val, e.max_val, "%.2f"))
                mgb_config_set_float(e.key, v);
            break;
        }
        case MGB_CFG_ENUM: {
            const char *cur = mgb_config_enum_token(e.key, e.cur_enum_index);
            ImGui::SetNextItemWidth(ui::kControlWidth);
            if (ImGui::BeginCombo(label, cur)) {
                for (int i = 0; i < e.enum_count; ++i) {
                    const char *t = mgb_config_enum_token(e.key, i);
                    bool sel = (i == e.cur_enum_index);
                    if (ImGui::Selectable(t, sel)) mgb_config_set_enum(e.key, i);
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            break;
        }
        case MGB_CFG_STRING:
            drawStringEntry(e, label);
            break;
        default:
            ImGui::TextDisabled("%s (unsupported type)", label);
            break;
    }

    if (e.help[0] && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", e.help);
    if (!e.is_live) ui::RestartBadge();
    ImGui::PopID();
}

}  // namespace

void Settings_draw() {
    static const char *kSections[] = {"Video", "Input", "Game", "Audio"};

    if (ui::PrimaryButton("Save Settings", ImVec2(150, 36))) mgb_config_save();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Persist to ge007.ini");
    ImGui::SameLine();
    ui::TextSubtle("Live settings apply instantly; \"restart\" settings apply next launch.");
    ui::Gap(ui::kGapS);

    const int n = mgb_config_count();
    if (ImGui::BeginTabBar("##settingsTabs")) {
        for (const char *sec : kSections) {
            if (ImGui::BeginTabItem(sec)) {
                ui::Gap(ui::kGapXS);
                ImGui::BeginChild("##sec", ImVec2(0, 0), false);
                for (int i = 0; i < n; ++i) {
                    MgbCfgEntry e;
                    if (!mgb_config_get(i, &e)) continue;
                    if (std::strcmp(e.section, sec) != 0) continue;
                    drawEntry(e);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
}
