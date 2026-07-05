// app_config.cpp — see app_config.h.
#include "app_config.h"

#include <SDL.h>

#include <fstream>
#include <map>

namespace {
std::map<std::string, std::string> g_kv;

std::string prefsFilePath() {
    char *p = SDL_GetPrefPath("MGB64", "MGB64");
    std::string base = p ? p : "";
    if (p) SDL_free(p);
    return base + "mgb64_app.ini";
}
}  // namespace

namespace AppConfig {

void load() {
    g_kv.clear();
    std::ifstream f(prefsFilePath());
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        g_kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
}

void save() {
    std::ofstream f(prefsFilePath());
    if (!f) return;
    f << "# MGB64 app preferences\n";
    for (const auto &kv : g_kv) f << kv.first << "=" << kv.second << "\n";
}

std::string get(const std::string &key, const std::string &fallback) {
    auto it = g_kv.find(key);
    return it != g_kv.end() ? it->second : fallback;
}

void set(const std::string &key, const std::string &value) { g_kv[key] = value; }

}  // namespace AppConfig
