// env_ownership.cpp — see env_ownership.h.
//
// Pure, SDL-/ImGui-free core of the launcher's authoritative env ownership
// (AUDIT-0022). No process globals: the caller feeds current state in and applies
// the returned ops + persists the returned record, so it unit-tests standalone.
#include "env_ownership.h"

namespace {

// Escape a field so the record's delimiter bytes (US 0x1f between key/value, RS
// 0x1e between entries) can never appear inside a key or value. Backslash escapes
// itself; a stray newline is neutralized too (values never legitimately contain
// one, but be defensive against a hand-edited ini).
std::string esc(const std::string &s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\':   o += "\\\\"; break;
            case '\n':   o += "\\n";  break;
            case '\x1e': o += "\\R";  break;
            case '\x1f': o += "\\U";  break;
            default:     o += c;      break;
        }
    }
    return o;
}

std::string unesc(const std::string &s) {
    std::string o;
    o.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            switch (n) {
                case '\\': o += '\\';   break;
                case 'n':  o += '\n';   break;
                case 'R':  o += '\x1e'; break;
                case 'U':  o += '\x1f'; break;
                default:   o += n;      break;
            }
        } else {
            o += s[i];
        }
    }
    return o;
}

}  // namespace

namespace EnvOwnership {

std::vector<EnvOp> hatchOps(bool shootOutLights, bool autoAim) {
    // The launcher fully owns these two keys, so it emits an authoritative op for
    // each every apply. OFF => set "0"; ON => UNSET so the engine default (ON)
    // applies and any value inherited across a Return-to-Launcher re-exec is
    // cleared. GE007_AUTO_AIM is NEVER set to a non-"0" value: in the engine that
    // key is a scripted-input frame pattern, so "1" would forge input on frames
    // 1-2 — unset is the only sim-safe "on".
    std::vector<EnvOp> ops;
    ops.push_back({"GE007_SHOOT_OUT_LIGHTS", !shootOutLights, "0"});
    ops.push_back({"GE007_AUTO_AIM", !autoAim, "0"});
    return ops;
}

std::map<std::string, std::string> parseAdvanced(const std::string &text) {
    // Mirrors the launcher's historical inline parse EXACTLY: split on '\n', trim
    // outer whitespace/CR of the whole line, skip blank and '#' lines, require an
    // '=', require a "GE007_" key prefix. Duplicate keys: last line wins.
    std::map<std::string, std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t nl = text.find('\n', i);
        std::string line = text.substr(
            i, nl == std::string::npos ? std::string::npos : nl - i);
        i = (nl == std::string::npos) ? text.size() : nl + 1;
        std::size_t a = line.find_first_not_of(" \t\r");
        if (a == std::string::npos) continue;
        std::size_t b = line.find_last_not_of(" \t\r");
        line = line.substr(a, b - a + 1);
        if (line.empty() || line[0] == '#') continue;
        std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k.rfind("GE007_", 0) == 0) out[k] = v;
    }
    return out;
}

Reconciliation reconcile(
    const std::map<std::string, std::string> &desired,
    const std::string &priorRecord,
    const std::function<std::optional<std::string>(const std::string &)> &lookupEnv) {
    std::map<std::string, std::pair<bool, std::string>> owned =
        decodeRecord(priorRecord);
    std::map<std::string, std::pair<bool, std::string>> newOwned;
    Reconciliation r;

    // 1. Keys we owned before but no longer want: restore the captured original
    //    external value, or unset if none existed. (Do this before setting the
    //    current keys.)
    for (const auto &e : owned) {
        if (desired.find(e.first) == desired.end()) {
            if (e.second.first) {
                r.ops.push_back({e.first, true, e.second.second});
            } else {
                r.ops.push_back({e.first, false, std::string()});
            }
        } else {
            newOwned[e.first] = e.second;  // still owned: keep original capture
        }
    }

    // 2. Newly-claimed keys: capture the pre-claim external value so a later
    //    removal restores it instead of deleting an intentional external value.
    for (const auto &d : desired) {
        if (owned.find(d.first) == owned.end()) {
            std::optional<std::string> cur = lookupEnv(d.first);
            newOwned[d.first] = {cur.has_value(),
                                 cur.has_value() ? *cur : std::string()};
        }
    }

    // 3. Set every desired key to its current value (idempotent on re-apply).
    for (const auto &d : desired) {
        r.ops.push_back({d.first, true, d.second});
    }

    r.record = encodeRecord(newOwned);
    return r;
}

std::string encodeRecord(
    const std::map<std::string, std::pair<bool, std::string>> &owned) {
    std::string out;
    for (const auto &e : owned) {
        out += e.second.first ? '1' : '0';
        out += esc(e.first);
        out += '\x1f';
        out += esc(e.second.second);
        out += '\x1e';
    }
    return out;
}

std::map<std::string, std::pair<bool, std::string>> decodeRecord(
    const std::string &rec) {
    std::map<std::string, std::pair<bool, std::string>> out;
    std::size_t i = 0;
    while (i < rec.size()) {
        std::size_t rs = rec.find('\x1e', i);
        if (rs == std::string::npos) break;
        std::string entry = rec.substr(i, rs - i);
        i = rs + 1;
        if (entry.empty()) continue;
        bool had = entry[0] == '1';
        std::size_t us = entry.find('\x1f', 1);
        if (us == std::string::npos) continue;
        std::string k = unesc(entry.substr(1, us - 1));
        std::string v = unesc(entry.substr(us + 1));
        if (!k.empty()) out[k] = {had, v};
    }
    return out;
}

}  // namespace EnvOwnership
