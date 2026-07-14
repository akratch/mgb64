// test_env_ownership.cpp — AUDIT-0022 regression guard.
//
// The launcher must be AUTHORITATIVE over the GE007_* env keys it owns so a mode
// toggle re-enabled or an advanced override deleted after a Return-to-Launcher
// re-exec cannot survive as a stale inherited variable. Pins the pure core
// (env_ownership.cpp): hatch ops, advanced parse, claim/restore/unset lifecycle,
// external original-value restoration, idempotency, and the record round-trip.
// ROM-free, SDL-free: links only env_ownership.cpp.
#include "env_ownership.h"

#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace EnvOwnership;

static int g_fails = 0;
#define CHECK(name, cond) do { \
    if (!(cond)) { std::printf("FAIL: %s\n", (name)); ++g_fails; } \
} while (0)

// Fake process environment for reconcile()'s lookupEnv callback.
struct FakeEnv {
    std::map<std::string, std::string> vars;
    std::optional<std::string> operator()(const std::string &k) const {
        auto it = vars.find(k);
        if (it == vars.end()) return std::nullopt;
        return it->second;
    }
};

// Apply ops onto a fake process env so we can assert what the spawned engine
// would getenv().
static void applyOps(std::map<std::string, std::string> &env,
                     const std::vector<EnvOp> &ops) {
    for (const auto &op : ops) {
        if (op.set) env[op.key] = op.value;
        else        env.erase(op.key);
    }
}
static bool has(const std::map<std::string, std::string> &e, const std::string &k) {
    return e.find(k) != e.end();
}
static std::string val(const std::map<std::string, std::string> &e, const std::string &k) {
    auto it = e.find(k);
    return it == e.end() ? std::string("<unset>") : it->second;
}

int main(void) {
    // ---- A: hatches ON clear any inherited value, never forge a value. -------
    {
        std::vector<EnvOp> on = hatchOps(true, true);
        CHECK("hatch A: 2 ops", on.size() == 2);
        std::map<std::string, std::string> env;
        env["GE007_SHOOT_OUT_LIGHTS"] = "0";  // stale inherited across re-exec
        env["GE007_AUTO_AIM"] = "0";
        applyOps(env, on);
        CHECK("hatch A: ON clears inherited shoot-lights", !has(env, "GE007_SHOOT_OUT_LIGHTS"));
        CHECK("hatch A: ON clears inherited auto-aim", !has(env, "GE007_AUTO_AIM"));
        for (const auto &op : on) CHECK("hatch A: ON never sets a value", !op.set);
    }
    // ---- B: hatches OFF set exactly "0"; auto-aim never a non-"0" value. -----
    {
        std::vector<EnvOp> off = hatchOps(false, false);
        std::map<std::string, std::string> env;
        applyOps(env, off);
        CHECK("hatch B: OFF sets shoot-lights=0", val(env, "GE007_SHOOT_OUT_LIGHTS") == "0");
        CHECK("hatch B: OFF sets auto-aim=0", val(env, "GE007_AUTO_AIM") == "0");
        for (const auto &op : off)
            if (op.key == "GE007_AUTO_AIM" && op.set)
                CHECK("hatch B: auto-aim value is exactly 0", op.value == "0");
    }
    // ---- Parse: prefix filter, no-'=' drop, comment/blank skip, dup last-wins.
    {
        std::string text =
            "GE007_FOO=bar\n"
            "   GE007_IND=indented\n"
            "# comment line\n"
            "\n"
            "NOTGE007=x\n"
            "GE007_NOEQ\n"
            "GE007_FOO=second\n";
        std::map<std::string, std::string> d = parseAdvanced(text);
        CHECK("parse: size==2", d.size() == 2);
        CHECK("parse: dup last-wins", d["GE007_FOO"] == "second");
        CHECK("parse: indent trimmed", d.count("GE007_IND") && d["GE007_IND"] == "indented");
        CHECK("parse: non-prefix dropped", d.find("NOTGE007") == d.end());
        CHECK("parse: no-'=' dropped", d.find("GE007_NOEQ") == d.end());
    }
    // ---- C: claim then remove -> unset (originally absent). -----------------
    {
        FakeEnv env;
        std::map<std::string, std::string> proc;
        Reconciliation r1 = reconcile(parseAdvanced("GE007_A=1\n"), "", env);
        applyOps(proc, r1.ops);
        CHECK("claim: A set to 1", val(proc, "GE007_A") == "1");
        Reconciliation r2 = reconcile(parseAdvanced(""), r1.record, env);
        applyOps(proc, r2.ops);
        CHECK("remove: A unset (was absent)", !has(proc, "GE007_A"));
        CHECK("remove: record empty", decodeRecord(r2.record).empty());
    }
    // ---- D: external value overridden then removed -> RESTORED, not deleted. -
    {
        FakeEnv env;
        env.vars["GE007_EXT"] = "external";
        std::map<std::string, std::string> proc;
        proc["GE007_EXT"] = "external";
        Reconciliation r1 = reconcile(parseAdvanced("GE007_EXT=override\n"), "", env);
        applyOps(proc, r1.ops);
        CHECK("ext: overridden", val(proc, "GE007_EXT") == "override");
        auto own = decodeRecord(r1.record);
        CHECK("ext: original captured", own.count("GE007_EXT") &&
              own["GE007_EXT"].first && own["GE007_EXT"].second == "external");
        Reconciliation r2 = reconcile(parseAdvanced(""), r1.record, env);
        applyOps(proc, r2.ops);
        CHECK("ext: restored to external", val(proc, "GE007_EXT") == "external");
    }
    // ---- E: idempotency + value change keeps captured original. --------------
    {
        FakeEnv env;
        env.vars["GE007_K"] = "orig";
        Reconciliation r1 = reconcile(parseAdvanced("GE007_K=v1\n"), "", env);
        Reconciliation r2 = reconcile(parseAdvanced("GE007_K=v1\n"), r1.record, env);
        CHECK("idem: record stable", r1.record == r2.record);
        Reconciliation r3 = reconcile(parseAdvanced("GE007_K=v2\n"), r2.record, env);
        auto own = decodeRecord(r3.record);
        CHECK("idem: original preserved across value change",
              own["GE007_K"].first && own["GE007_K"].second == "orig");
        std::string setv = "<none>";
        for (const auto &op : r3.ops)
            if (op.key == "GE007_K" && op.set) setv = op.value;
        CHECK("idem: value updated to v2", setv == "v2");
    }
    // ---- F: record round-trip with tricky bytes. ----------------------------
    {
        std::map<std::string, std::pair<bool, std::string>> owned;
        owned["GE007_A"] = {false, ""};
        owned["GE007_B"] = {true, "val=with=eq and \\ backslash"};
        owned["GE007_C"] = {true, std::string("ctrl\x1e\x1f" "bytes")};
        std::string enc = encodeRecord(owned);
        auto dec = decodeRecord(enc);
        CHECK("rt: size", dec.size() == 3);
        CHECK("rt: A absent-marker", !dec["GE007_A"].first && dec["GE007_A"].second == "");
        CHECK("rt: B value", dec["GE007_B"].first &&
              dec["GE007_B"].second == "val=with=eq and \\ backslash");
        CHECK("rt: C control bytes", dec["GE007_C"].first &&
              dec["GE007_C"].second == std::string("ctrl\x1e\x1f" "bytes"));
    }

    if (g_fails == 0) { std::printf("PASS: all env_ownership cases\n"); return 0; }
    std::printf("%d failure(s)\n", g_fails);
    return 1;
}
