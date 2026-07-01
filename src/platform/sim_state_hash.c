#include "sim_state_hash.h"
#include <stdio.h>
#include <string.h>

/* ---- FNV-1a 64-bit ------------------------------------------------------- */
#define FNV64_OFFSET 1469598103934665603ULL
#define FNV64_PRIME  1099511628211ULL

static inline uint64_t fnv1a(uint64_t h, const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= FNV64_PRIME; }
    return h;
}

/*
 * If `w` points into one of the registered regions, return a base-independent
 * token (region index + 1, in the high bits) XOR the in-region offset. Otherwise
 * return `w` verbatim. This canonicalizes intra-state pointers so the hash does
 * not depend on ASLR-varied absolute addresses, while a genuine (non-pointer)
 * data word is hashed literally.
 */
static uint64_t canon_word(uintptr_t w, const SimHashRegion *rg, int n) {
    for (int k = 0; k < n; k++) {
        if (!rg[k].base) continue;
        uintptr_t lo = (uintptr_t)rg[k].base;
        uintptr_t hi = lo + rg[k].size;
        if (w >= lo && w < hi) {
            return ((uint64_t)(k + 1) << 48) ^ (uint64_t)(w - lo);
        }
    }
    return (uint64_t)w;
}

uint64_t sim_state_hash_compute(const SimHashRegion *rg, int n) {
    uint64_t h = FNV64_OFFSET;
    for (int k = 0; k < n; k++) {
        /* Hash the region name so a changed/reordered region set is detectable. */
        h = fnv1a(h, rg[k].name ? rg[k].name : "", rg[k].name ? strlen(rg[k].name) : 0);
        if (!rg[k].base || rg[k].size == 0) continue;

        const unsigned char *b = (const unsigned char *)rg[k].base;
        size_t sz = rg[k].size, i = 0;
        /* Pointer-aligned words get canonicalized; a trailing sub-word tail is
         * hashed raw (it cannot hold a full pointer). */
        for (; i + sizeof(uintptr_t) <= sz; i += sizeof(uintptr_t)) {
            uintptr_t w;
            memcpy(&w, b + i, sizeof w);                 /* alignment-safe */
            uint64_t c = canon_word(w, rg, n);
            h = fnv1a(h, &c, sizeof c);
        }
        if (i < sz) h = fnv1a(h, b + i, sz - i);
    }
    return h;
}

int sim_state_hash_emit_json(const char *path, uint64_t hash,
                             const SimHashRegion *rg, int n,
                             int frame, const char *replay) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "{\n");
    fprintf(f, "  \"hash\": \"%016llx\",\n", (unsigned long long)hash);
    fprintf(f, "  \"frame\": %d,\n", frame);
    fprintf(f, "  \"replay\": \"%s\",\n", replay ? replay : "");
    fprintf(f, "  \"regions\": [");
    for (int k = 0; k < n; k++) {
        fprintf(f, "%s{\"name\": \"%s\", \"size\": %zu}",
                k ? ", " : "", rg[k].name ? rg[k].name : "", rg[k].size);
    }
    fprintf(f, "]\n}\n");
    fclose(f);
    return 0;
}

/* simHashRegistryBuild() lives in sim_state_hash_registry.c (game-coupled, so it
 * is kept out of this file to keep the ROM-free unit test dependency-free). */
