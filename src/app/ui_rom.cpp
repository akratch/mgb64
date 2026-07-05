// ui_rom.cpp — the Game ROM panel: native picker, validation card, Play.
#include "ui_launcher.h"
#include "app_config.h"
#include "app_theme.h"
#include "ui_common.h"

#include "imgui.h"
#include "nfd.h"

#include <cstdio>
#include <cstring>

namespace {

const char *baseName(const char *path) {
    const char *s = path;
    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') s = p + 1;
    }
    return s;
}

void setRom(LauncherState &st, const char *path) {
    if (!path) return;
    std::snprintf(st.romPath, sizeof(st.romPath), "%s", path);
    st.romInfo = mgb_validate_rom(st.romPath);
    AppConfig::set("last_rom", st.romPath);
    AppConfig::save();
}

void pickRom(LauncherState &st) {
    if (NFD_Init() != NFD_OKAY) return;
    nfdu8char_t *out = nullptr;
    nfdu8filteritem_t filters[1] = {{"Nintendo 64 ROM", "z64,n64,v64"}};
    nfdresult_t r = NFD_OpenDialogU8(&out, filters, 1, nullptr);
    if (r == NFD_OKAY && out) {
        setRom(st, out);
        NFD_FreePathU8(out);
    }
    NFD_Quit();
}

}  // namespace

void RomPanel_ensureInit(LauncherState &st) {
    if (st.romInitialized) return;
    st.romInitialized = true;
    AppConfig::load();
    std::string last = AppConfig::get("last_rom");
    if (!last.empty()) setRom(st, last.c_str());
}

void RomPanel_draw(LauncherState &st, LauncherAction &out) {
    ui::SectionHeader("Game ROM",
                      "Provide your own legally-dumped GoldenEye 007 cartridge (.z64 / .n64 / .v64).");

    if (ImGui::Button(st.romPath[0] ? "Change ROM..." : "Choose ROM...", ui::kBtnSecondary)) {
        pickRom(st);
    }
    ui::Gap(ui::kGapS);

    if (st.romPath[0]) {
        const bool ok = st.romInfo.valid != 0;
        const ImVec4 mark = ok ? AppTheme::good() : AppTheme::bad();
        ui::CardBegin("##romcard", mark, 140.0f);
        ImGui::PushFont(AppTheme::fonts().title);
        ImGui::TextUnformatted(baseName(st.romPath));
        ImGui::PopFont();
        ui::Gap(ui::kGapXS);
        ImGui::PushStyleColor(ImGuiCol_Text, mark);
        ImGui::TextWrapped("%s", st.romInfo.message);
        ImGui::PopStyleColor();
        ui::Gap(ui::kGapXS);
        ImGui::PushFont(AppTheme::fonts().small);
        ui::TextSubtle("Region %s   \xE2\x80\xA2   %s   \xE2\x80\xA2   %.1f MB", st.romInfo.region,
                       st.romInfo.byte_order, st.romInfo.size_bytes / (1024.0 * 1024.0));
        ui::TextSubtle("%s", st.romPath);
        ImGui::PopFont();
        ui::CardEnd();
    } else {
        // Same-height muted card so the layout doesn't jump before selection.
        ui::CardBegin("##romempty", AppTheme::subtle(), 140.0f);
        ui::Gap(ui::kGapM);
        ImGui::PushFont(AppTheme::fonts().title);
        ui::TextSubtle("No ROM selected");
        ImGui::PopFont();
        ui::Gap(ui::kGapXS);
        ui::TextSubtle("Click \"Choose ROM...\" to pick your legally-dumped cartridge dump.");
        ui::CardEnd();
    }

    ui::Gap(ui::kGapM);
    PlayButton_draw(st, out);
}
