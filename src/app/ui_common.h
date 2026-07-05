// ui_common.h — shared UI primitives + sizing constants for every panel.
//
// One place owns button sizes, spacing rhythm, and the repeated ImGui idioms
// (primary button, subtle caption, section header, card). Panels compose these
// so the app reads as one system and new panels (Part 2: bindings, diagnostics,
// modes) stay consistent by construction.
#ifndef MGB64_UI_COMMON_H
#define MGB64_UI_COMMON_H

#include "imgui.h"

namespace ui {

// --- Sizing (logical px) ---
inline const ImVec2 kBtnPrimary   = ImVec2(190, 46);
inline const ImVec2 kBtnSecondary = ImVec2(190, 40);
inline const ImVec2 kBtnWide      = ImVec2(210, 40);
constexpr float kControlWidth = 340.0f;  // sliders / combos
constexpr float kNavWidth     = 244.0f;
// Vertical rhythm scale.
constexpr float kGapXS = 4.0f;
constexpr float kGapS  = 8.0f;
constexpr float kGapM  = 14.0f;
constexpr float kGapL  = 22.0f;

// --- Primitives ---
void Gap(float y);                                   // vertical spacer
void TextSubtle(const char *fmt, ...);               // secondary/muted text
void SectionHeader(const char *title, const char *subtitle);
void RestartBadge();                                 // "(restart)" muted chip

// Primary (accent-filled) button. Push/Pop expose the style for custom layouts.
void PushPrimaryButtonColors();
void PopPrimaryButtonColors();
bool PrimaryButton(const char *label, const ImVec2 &size = kBtnPrimary);

// Bordered content card. CardBegin returns true when open (call CardEnd then).
bool CardBegin(const char *id, const ImVec4 &borderColor, float height);
void CardEnd();

}  // namespace ui

#endif  // MGB64_UI_COMMON_H
