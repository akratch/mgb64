#ifndef SIM_STATE_HASH_H
#define SIM_STATE_HASH_H
/*
 * Sim-state invariance hash (remaster P0.2 rail).
 *
 * Computes a deterministic 64-bit hash over the native port's mutable simulation
 * state so a RAMROM replay can be proven bit-identical with remaster rendering
 * flags on vs off. State lives in native host memory (the 8 MB s_pcPool arena +
 * curated game globals), so raw pointer values differ run-to-run under ASLR.
 * The hash therefore CANONICALIZES any word pointing into a registered region to
 * a base-independent (region, offset) token, making it ASLR-invariant without
 * needing -no-pie (unavailable on macOS arm64).
 */
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;   /* stable identity; hashed so region-set changes are caught */
    const void *base;   /* runtime base address of the region                       */
    size_t      size;   /* region length in bytes                                   */
} SimHashRegion;

/* FNV-1a over the regions, with intra-state pointer normalization. Deterministic
 * for identical logical state regardless of ASLR. */
uint64_t sim_state_hash_compute(const SimHashRegion *regions, int n);

/* Write the gate result as JSON (hash, frame, replay, region names+sizes). */
int sim_state_hash_emit_json(const char *path, uint64_t hash,
                             const SimHashRegion *regions, int n,
                             int frame, const char *replay);

/* Fill `out` with the state region set: [0] = pool, [1..] = curated globals.
 * `*n` returns the count. Defined in sim_state_hash.c (registry section). */
void simHashRegistryBuild(SimHashRegion *out, int *n);

#endif /* SIM_STATE_HASH_H */
