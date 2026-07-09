// test_update_check.cpp — ROM-free, network-free unit test for the update check
// core: GitHub tag_name extraction, dev-version detection, and semver comparison.
#include "update_check_core.h"

#include <cstdio>
#include <cstring>

static int g_failures = 0;

#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
            g_failures++;                                                     \
        }                                                                     \
    } while (0)

// Convenience: extract into a fresh buffer and compare.
static bool tagEquals(const char *json, const char *want) {
    char out[128];
    int ok = mgb_update_extract_tag(json, std::strlen(json), out, sizeof(out));
    return ok && std::strcmp(out, want) == 0;
}

int main(void) {
    // ---- tag extraction ----
    CHECK(tagEquals("{\"tag_name\":\"v0.3.4\"}", "v0.3.4"), "basic tag");
    CHECK(tagEquals("{ \"tag_name\" : \"v1.2.3\" }", "v1.2.3"), "whitespace around colon");
    // Field order independence: tag_name after other fields.
    CHECK(tagEquals("{\"url\":\"x\",\"id\":42,\"tag_name\":\"v9.9.9\",\"name\":\"y\"}", "v9.9.9"),
          "tag after other fields");
    // Escaped characters in the value.
    CHECK(tagEquals("{\"tag_name\":\"v1.0.0\\/beta\"}", "v1.0.0/beta"), "escaped slash");
    CHECK(tagEquals("{\"tag_name\":\"a\\\"b\"}", "a\"b"), "escaped quote");
    // A "tag_name" substring appearing earlier as a value must not fool the scan
    // enough to matter; the first key match wins and yields a real value.
    {
        char out[128];
        int ok = mgb_update_extract_tag("{\"body\":\"see tag_name notes\",\"tag_name\":\"v2.0.0\"}",
                                        std::strlen("{\"body\":\"see tag_name notes\",\"tag_name\":\"v2.0.0\"}"),
                                        out, sizeof(out));
        // The first literal "tag_name" is inside body's value WITHOUT quotes around
        // the token, so it won't match "\"tag_name\""; the real key does.
        CHECK(ok && std::strcmp(out, "v2.0.0") == 0, "quoted-key match skips prose mention");
    }
    // Malformed / missing.
    CHECK(!tagEquals("{\"name\":\"v1.0.0\"}", "v1.0.0"), "no tag_name -> fail");
    {
        char out[8];
        CHECK(mgb_update_extract_tag("{\"tag_name\":null}", 17, out, sizeof(out)) == 0,
              "null value -> fail");
        CHECK(mgb_update_extract_tag("{\"tag_name\":\"\"}", 15, out, sizeof(out)) == 0,
              "empty value -> fail");
    }
    // Output truncation stays in-bounds.
    {
        char out[4];
        int ok = mgb_update_extract_tag("{\"tag_name\":\"v1.2.3-longsuffix\"}",
                                        std::strlen("{\"tag_name\":\"v1.2.3-longsuffix\"}"),
                                        out, sizeof(out));
        CHECK(ok && std::strlen(out) == 3, "truncated to cap-1");
    }

    // ---- dev-version detection ----
    CHECK(mgb_update_version_is_dev("0.0.0-dev") == 1, "0.0.0-dev is dev");
    CHECK(mgb_update_version_is_dev("") == 1, "empty is dev");
    CHECK(mgb_update_version_is_dev("garbage") == 1, "unparseable is dev");
    CHECK(mgb_update_version_is_dev("v1.2.3-dev") == 1, "any -dev is dev");
    CHECK(mgb_update_version_is_dev("0.0.0") == 1, "0.0.0 treated as dev");
    CHECK(mgb_update_version_is_dev("0.3.3") == 0, "real release not dev");
    CHECK(mgb_update_version_is_dev("v0.3.3") == 0, "v-prefixed release not dev");

    // ---- remote_is_newer ----
    // Basic ordering.
    CHECK(mgb_update_remote_is_newer("v0.3.4", "0.3.3") == 1, "patch bump newer");
    CHECK(mgb_update_remote_is_newer("v0.4.0", "0.3.9") == 1, "minor bump newer");
    CHECK(mgb_update_remote_is_newer("v1.0.0", "0.9.9") == 1, "major bump newer");
    CHECK(mgb_update_remote_is_newer("v0.3.3", "0.3.3") == 0, "same version not newer");
    CHECK(mgb_update_remote_is_newer("v0.3.2", "0.3.3") == 0, "older remote not newer");
    // v-prefix tolerance on both sides.
    CHECK(mgb_update_remote_is_newer("0.3.4", "v0.3.3") == 1, "mixed v-prefix newer");
    // Suffix rules: a suffixed remote counts as newer only if its base is greater.
    CHECK(mgb_update_remote_is_newer("v0.4.0-rc1", "0.3.3") == 1, "prerelease of greater base newer");
    CHECK(mgb_update_remote_is_newer("v0.3.3-rc1", "0.3.3") == 0, "prerelease of same base not newer");
    // The full release of the prerelease you're running IS newer.
    CHECK(mgb_update_remote_is_newer("v0.3.3", "0.3.3-rc1") == 1, "release over your prerelease newer");
    CHECK(mgb_update_remote_is_newer("v0.3.3-rc2", "0.3.3-rc1") == 0, "rc2 over rc1 (same base) not newer");
    // Dev current: never newer even against a much greater remote.
    CHECK(mgb_update_remote_is_newer("v9.9.9", "0.0.0-dev") == 0, "dev build never nagged");
    CHECK(mgb_update_remote_is_newer("v9.9.9", "garbage") == 0, "unparseable current never nagged");
    // Unparseable remote: not newer.
    CHECK(mgb_update_remote_is_newer("not-a-version", "0.3.3") == 0, "unparseable remote not newer");
    // Two-component tags parse (missing patch = 0).
    CHECK(mgb_update_remote_is_newer("v1.1", "1.0.9") == 1, "two-component newer");

    if (g_failures == 0) {
        std::printf("PASS: update_check core\n");
        return 0;
    }
    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
