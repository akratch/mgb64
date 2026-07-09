// ui_launch.cpp — Launch Options panel + the shared Play button + boot config.
#include "ui_launcher.h"
#include "ui_common.h"

#include "imgui.h"

#include <cstdio>

namespace {
// The 20 solo campaign levels, keyed by their stable CLI slug.
const struct { const char *slug; const char *name; } kLevels[] = {
    {"dam", "Dam"},           {"facility", "Facility"},  {"runway", "Runway"},
    {"surface1", "Surface 1"}, {"bunker1", "Bunker 1"},  {"silo", "Silo"},
    {"frigate", "Frigate"},   {"surface2", "Surface 2"}, {"bunker2", "Bunker 2"},
    {"statue", "Statue"},     {"archives", "Archives"},  {"streets", "Streets"},
    {"depot", "Depot"},       {"train", "Train"},        {"jungle", "Jungle"},
    {"control", "Control"},   {"caverns", "Caverns"},    {"cradle", "Cradle"},
    {"aztec", "Aztec"},       {"egypt", "Egyptian"},
};
const int kNumLevels = IM_ARRAYSIZE(kLevels);
const char *kDifficulties[] = {"Agent", "Secret Agent", "00 Agent"};
}  // namespace

void fillBoot(const LauncherState &s, MgbBootConfig &b) {
    b.preset = s.modePreset;
    if (s.launchMultiplayer) {
        b.multiplayer = 1;
        b.players = s.launchPlayers;
    } else if (s.launchLevelIndex > 0 && s.launchLevelIndex <= kNumLevels) {
        b.level_slug = kLevels[s.launchLevelIndex - 1].slug;
        b.difficulty = s.launchDifficulty;
    }
    applyModeEnv(s);  // setenv the mode hatches + advanced overrides for this boot
}

void launchSummary(const LauncherState &s, char *buf, int n) {
    if (s.launchMultiplayer) {
        std::snprintf(buf, n, "Multiplayer \xE2\x80\xA2 %d players", s.launchPlayers);
    } else if (s.launchLevelIndex > 0 && s.launchLevelIndex <= kNumLevels) {
        std::snprintf(buf, n, "%s \xE2\x80\xA2 %s", kLevels[s.launchLevelIndex - 1].name,
                      kDifficulties[s.launchDifficulty]);
    } else {
        std::snprintf(buf, n, "Boot to menu");
    }
}

void PlayButton_draw(LauncherState &s, LauncherAction &out) {
    char summary[96];
    launchSummary(s, summary, sizeof(summary));

    if (s.romInfo.valid != 0) {
        if (ui::PrimaryButton("Play", ui::kBtnPrimary())) {
            out.type = LauncherActionType::Play;
            out.boot.rom_path = s.romPath;  // stable: member of the Launcher's state
            fillBoot(s, out.boot);
        }
        ui::Gap(ui::kGapXS);
        ui::TextSubtle("Will boot: %s", summary);
    } else {
        ImGui::BeginDisabled(true);
        ImGui::Button("Play", ui::kBtnPrimary());
        ImGui::EndDisabled();
        ui::Gap(ui::kGapXS);
        ui::TextSubtle("Select a valid ROM on the Game ROM tab first.");
    }
}

void LaunchPanel_draw(LauncherState &s, LauncherAction &out) {
    ui::SectionHeader("Launch Options",
                      "Boot to the menu, jump straight to a level, or start multiplayer.");

    ImGui::Checkbox("Multiplayer", &s.launchMultiplayer);
    ui::Gap(ui::kGapS);

    if (s.launchMultiplayer) {
        ImGui::SetNextItemWidth(ui::kControlWidth());
        ImGui::SliderInt("Players", &s.launchPlayers, 2, 4);
        ui::TextSubtle("Split-screen deathmatch on the default stage.");
    } else {
        ImGui::SetNextItemWidth(ui::kControlWidth());
        const char *cur = s.launchLevelIndex == 0 ? "Boot to menu"
                                                  : kLevels[s.launchLevelIndex - 1].name;
        if (ImGui::BeginCombo("Start at", cur)) {
            if (ImGui::Selectable("Boot to menu", s.launchLevelIndex == 0)) s.launchLevelIndex = 0;
            for (int i = 0; i < kNumLevels; ++i) {
                bool sel = (s.launchLevelIndex == i + 1);
                if (ImGui::Selectable(kLevels[i].name, sel)) s.launchLevelIndex = i + 1;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (s.launchLevelIndex > 0) {
            ImGui::SetNextItemWidth(ui::kControlWidth());
            ImGui::Combo("Difficulty", &s.launchDifficulty, kDifficulties, IM_ARRAYSIZE(kDifficulties));
        }
    }

    ui::Gap(ui::kGapL);
    PlayButton_draw(s, out);
}
