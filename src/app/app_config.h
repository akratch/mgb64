// app_config.h — app-shell preferences (last ROM, window size, ...).
//
// Distinct from the engine's ge007.ini. Persisted as mgb64_app.ini in the
// per-OS prefs directory (SDL_GetPrefPath): %APPDATA% on Windows,
// ~/Library/Application Support on macOS, ~/.local/share on Linux.
#ifndef MGB64_APP_CONFIG_H
#define MGB64_APP_CONFIG_H

#include <string>

namespace AppConfig {

void load();
void save();
std::string get(const std::string &key, const std::string &fallback = "");
void set(const std::string &key, const std::string &value);

}  // namespace AppConfig

#endif  // MGB64_APP_CONFIG_H
