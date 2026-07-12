// ui_settings.cpp — see ui_settings.h.
#include "ui_settings.h"
#include "config_schema.h"
#include "ui_common.h"
#include "app_theme.h"

#include "imgui.h"
#include "nfd.h"

#include <cstdio>
#include <cstring>

namespace {

// String settings (e.g. Video.TexturePack): show the current path read-only
// plus a native folder picker and a Clear (= stock). Fixes the old "(unsupported
// type)" fall-through — string settings are now first-class + editable.
void drawStringEntry(const MgbCfgEntry &e, const char *label) {
    char buf[512];
    if (!mgb_config_get_string(e.key, buf, (int)sizeof(buf))) buf[0] = '\0';

    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(ui::kControlWidth());
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

// The single key (if any) currently pushed live via configStagingPreview so the
// owner can feel a value on the frozen frame. Empty = none. Only one at a time
// (the config layer enforces this too).
char g_previewKey[96] = {0};

// FOV and mouse sensitivity are the two settings worth "feeling" before you keep
// them, so they get a Preview toggle in the staged (in-game) panel.
bool isPreviewable(const char *key) {
    return std::strcmp(key, "Video.FovY") == 0 ||
           std::strcmp(key, "Input.MouseSensitivity") == 0;
}

// A compact "Preview" toggle that pushes this key's staged value to the live
// global (and reverts on un-toggle / switching keys). Only drawn while staging.
void drawPreviewToggle(const MgbCfgEntry &e) {
    bool on = std::strcmp(g_previewKey, e.key) == 0;
    ImGui::SameLine();
    if (ImGui::Checkbox("Preview", &on)) {
        if (on) {
            if (g_previewKey[0]) configStagingPreview(g_previewKey, 0);  // revert the old one
            configStagingPreview(e.key, 1);
            std::snprintf(g_previewKey, sizeof(g_previewKey), "%s", e.key);
        } else {
            configStagingPreview(e.key, 0);
            g_previewKey[0] = '\0';
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Feel this value on the live game now; Apply keeps it, Cancel reverts");
}

void drawEntry(const MgbCfgEntry &e) {
    ImGui::PushID(e.key);
    const char *label = e.label[0] ? e.label : e.name;

    // ADS sub-knobs grey out when the ADS master toggle is off, so they read as
    // "child of Input.AdsEnabled" rather than orphaned always-on rows.
    const bool adsSub = std::strncmp(e.key, "Input.Ads", 9) == 0 &&
                        std::strcmp(e.key, "Input.AdsEnabled") != 0;
    const bool disabled = adsSub && mgb_config_get_int("Input.AdsEnabled", 0) == 0;
    if (disabled) ImGui::BeginDisabled();

    switch (e.kind) {
        case MGB_CFG_INT:
        case MGB_CFG_UINT:
            if (e.min_val == 0.0f && e.max_val == 1.0f) {
                bool b = e.cur_int != 0;
                if (ImGui::Checkbox(label, &b)) mgb_config_set_int(e.key, b ? 1 : 0);
            } else {
                int v = e.cur_int;
                ImGui::SetNextItemWidth(ui::kControlWidth());
                if (ImGui::SliderInt(label, &v, (int)e.min_val, (int)e.max_val))
                    mgb_config_set_int(e.key, v);
            }
            break;
        case MGB_CFG_FLOAT: {
            float v = e.cur_float;
            ImGui::SetNextItemWidth(ui::kControlWidth());
            if (ImGui::SliderFloat(label, &v, e.min_val, e.max_val, "%.2f"))
                mgb_config_set_float(e.key, v);
            if (configStagingActive() && isPreviewable(e.key)) drawPreviewToggle(e);
            break;
        }
        case MGB_CFG_ENUM: {
            const char *cur = mgb_config_enum_token(e.key, e.cur_enum_index);
            ImGui::SetNextItemWidth(ui::kControlWidth());
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

    if (!e.is_live) ui::RestartBadge();
    // Visible help: an inline muted caption under the control, in the small font.
    // Hover tooltips are invisible to controller/touch users (the handheld
    // audience), so the description is always shown, compact.
    if (e.help[0]) {
        ImGui::PushFont(AppTheme::fonts().small);
        ImGui::PushStyleColor(ImGuiCol_Text, AppTheme::subtle());
        ImGui::TextWrapped("%s", e.help);
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ui::Gap(ui::kGapXS);
    }

    if (disabled) ImGui::EndDisabled();
    ImGui::PopID();
}

// Draw every entry of one section belonging to the given tier (advanced or not),
// in registry order.
void drawSection(const char *sec, bool advanced) {
    const int n = mgb_config_count();
    for (int i = 0; i < n; ++i) {
        MgbCfgEntry e;
        if (!mgb_config_get(i, &e)) continue;
        if (std::strcmp(e.section, sec) != 0) continue;
        if ((e.advanced != 0) != advanced) continue;
        drawEntry(e);
    }
}

bool sectionHasAdvanced(const char *sec) {
    const int n = mgb_config_count();
    for (int i = 0; i < n; ++i) {
        MgbCfgEntry e;
        if (!mgb_config_get(i, &e)) continue;
        if (std::strcmp(e.section, sec) == 0 && e.advanced) return true;
    }
    return false;
}

// Restore every setting in one section (both player + advanced tiers) to its
// registered default. Validated/clamped by the engine; does NOT auto-save (the
// "Save Settings" button persists, matching the rest of this panel).
void resetSectionToDefaults(const char *sec) {
    const int n = mgb_config_count();
    for (int i = 0; i < n; ++i) {
        MgbCfgEntry e;
        if (!mgb_config_get(i, &e)) continue;
        if (std::strcmp(e.section, sec) != 0) continue;
        mgb_config_reset_default(e.key);
    }
}

// Per-tab "Reset to defaults" footer with a confirm popup — high value for pad
// users who can't retype a value they nudged off. One-click reset would be too
// easy to trigger by accident on a controller, so it confirms first.
void drawResetFooter(const char *sec) {
    ui::Gap(ui::kGapM);
    ImGui::Separator();
    ui::Gap(ui::kGapS);

    char popupId[80];
    std::snprintf(popupId, sizeof(popupId), "Reset %s settings?##reset_%s", sec, sec);
    if (ImGui::Button("Reset to defaults")) ImGui::OpenPopup(popupId);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Restore this tab's settings to their defaults");

    if (ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Reset all %s settings to their defaults?", sec);
        ui::TextSubtle("Affects only the %s tab. Use \"Save Settings\" to keep it.", sec);
        ui::Gap(ui::kGapS);
        if (ui::PrimaryButton("Reset", ImVec2(120, 0))) {
            resetSectionToDefaults(sec);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::SetItemDefaultFocus();  // B/Esc-friendly: focus Cancel by default
        ImGui::EndPopup();
    }
}

}  // namespace

SettingsResult Settings_draw() {
    static const char *kSections[] = {"Video", "Input", "Game", "Audio", "UI"};
    SettingsResult result = SETTINGS_NONE;

    // Two button models: the in-game overlay opens a staging session (edits are
    // held on a working copy while the game keeps running), so it gets Apply /
    // Cancel. The launcher has no running game to disturb, so it keeps the old
    // instant-apply + "Save Settings" model.
    if (configStagingActive()) {
        if (ui::PrimaryButton("Apply", ImVec2(120, 36))) {
            g_previewKey[0] = '\0';           // preview folds into the commit
            configStagingApply();
            result = SETTINGS_APPLIED;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keep these changes and save to ge007.ini");
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 36))) {
            g_previewKey[0] = '\0';
            configStagingDiscard();
            result = SETTINGS_CANCELLED;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Discard these changes");
        ImGui::SameLine();
        ui::TextSubtle("Changes are held until Apply — the game keeps running on the current values.");
    } else {
        if (ui::PrimaryButton("Save Settings", ImVec2(150, 36))) mgb_config_save();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Persist to ge007.ini");
        ImGui::SameLine();
        ui::TextSubtle("Live settings apply instantly; \"restart\" settings apply next launch.");
    }
    ui::Gap(ui::kGapS);

    if (ImGui::BeginTabBar("##settingsTabs")) {
        for (const char *sec : kSections) {
            if (ImGui::BeginTabItem(sec)) {
                ui::Gap(ui::kGapXS);
                ImGui::BeginChild("##sec", ImVec2(0, 0), false);

                // Player-facing settings first.
                drawSection(sec, false);

                // Dev/diagnostic knobs behind a collapsed "Advanced (expert)"
                // disclosure (collapsed by default: no DefaultOpen flag).
                if (sectionHasAdvanced(sec)) {
                    ui::Gap(ui::kGapM);
                    if (ImGui::CollapsingHeader("Advanced (expert)")) {
                        ui::TextSubtle("Dev/diagnostic tuning — the defaults are recommended.");
                        ui::Gap(ui::kGapS);
                        drawSection(sec, true);
                    }
                }

                drawResetFooter(sec);

                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    return result;
}
