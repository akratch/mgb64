// ui_modes.cpp — Modes & Toggles: visual preset, gameplay hatches, expert env.
#include "ui_launcher.h"
#include "app_config.h"
#include "ui_common.h"

#include "imgui.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

void portableSetenv(const char *k, const char *v) {
#if defined(_WIN32)
    _putenv_s(k, v);
#else
    setenv(k, v, 1);
#endif
}

void save(const LauncherState &s) {
    AppConfig::set("mode_preset", std::to_string(s.modePreset));
    AppConfig::set("hatch_shoot_lights", s.shootOutLights ? "1" : "0");
    AppConfig::set("hatch_auto_aim", s.autoAim ? "1" : "0");
    AppConfig::set("advanced_env", s.advancedEnv);
    AppConfig::save();
}

}  // namespace

void ModesPanel_ensureInit(LauncherState &s) {
    if (s.modesInitialized) return;
    s.modesInitialized = true;
    AppConfig::load();
    s.modePreset = std::atoi(AppConfig::get("mode_preset", "0").c_str());
    s.shootOutLights = AppConfig::get("hatch_shoot_lights", "1") != "0";
    s.autoAim = AppConfig::get("hatch_auto_aim", "1") != "0";
    std::snprintf(s.advancedEnv, sizeof(s.advancedEnv), "%s",
                  AppConfig::get("advanced_env", "").c_str());
}

void applyModeEnv(const LauncherState &s) {
    // Convenience hatches default ON in the engine, so only set the env when the
    // user turned them OFF (otherwise leave the engine default untouched).
    if (!s.shootOutLights) portableSetenv("GE007_SHOOT_OUT_LIGHTS", "0");
    if (!s.autoAim) portableSetenv("GE007_AUTO_AIM", "0");

    // Advanced: parse KEY=VALUE lines and setenv each GE007_* pair.
    std::string text(s.advancedEnv);
    size_t i = 0;
    while (i < text.size()) {
        size_t nl = text.find('\n', i);
        std::string line = text.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
        i = (nl == std::string::npos) ? text.size() : nl + 1;
        size_t a = line.find_first_not_of(" \t\r");
        if (a == std::string::npos) continue;
        size_t b = line.find_last_not_of(" \t\r");
        line = line.substr(a, b - a + 1);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k.rfind("GE007_", 0) == 0) portableSetenv(k.c_str(), v.c_str());
    }
}

void ModesPanel_draw(LauncherState &s, LauncherAction &out) {
    ModesPanel_ensureInit(s);
    ui::SectionHeader("Modes & Toggles",
                      "Pick a visual preset and gameplay hatches; expert env overrides below.");

    ImGui::TextUnformatted("Visual preset");
    ui::Gap(ui::kGapXS);
    const char *presets[] = {"Custom (use Settings)", "Faithful (1997 look)",
                             "Faithful HD", "Remaster (modern post-FX)"};
    for (int i = 0; i < 4; ++i) {
        if (ImGui::RadioButton(presets[i], s.modePreset == i)) {
            s.modePreset = i;
            save(s);
        }
    }
    ui::Gap(ui::kGapM);

    ImGui::TextUnformatted("Gameplay toggles");
    ui::Gap(ui::kGapXS);
    if (ImGui::Checkbox("Shoot out lights", &s.shootOutLights)) save(s);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bullets can destroy light fixtures.");
    if (ImGui::Checkbox("Auto-aim assist", &s.autoAim)) save(s);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("N64 auto-aim targeting assist.");
    ui::Gap(ui::kGapM);

    if (ImGui::CollapsingHeader("Advanced (expert, unsupported)")) {
        ui::TextSubtle("Raw GE007_* overrides, one KEY=VALUE per line. Applied at launch.");
        ui::Gap(ui::kGapXS);
        ImGui::InputTextMultiline("##adv", s.advancedEnv, sizeof(s.advancedEnv),
                                  ImVec2(-FLT_MIN, 140.0f));
        if (ImGui::IsItemDeactivatedAfterEdit()) save(s);
    }

    ui::Gap(ui::kGapL);
    PlayButton_draw(s, out);
}
