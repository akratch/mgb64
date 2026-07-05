// ui_settings.h — auto-generated settings panels from the engine config schema.
#ifndef MGB64_UI_SETTINGS_H
#define MGB64_UI_SETTINGS_H

// Draw the settings tabs (Video / Input / Game / Audio) inside the current
// content region. Reads/writes live engine config; live settings apply next
// frame. Shared by the launcher and the in-game overlay.
void Settings_draw();

#endif  // MGB64_UI_SETTINGS_H
