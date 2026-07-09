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

/* Upper bound on registered state regions (pool + curated globals + the stan
 * collision-navmesh region set appended by stanBuildHashRegions). */
#define SIM_HASH_MAX_REGIONS 64

typedef struct {
    const char *name;   /* stable identity; hashed so region-set changes are caught */
    const void *base;   /* runtime base address of the region                       */
    size_t      size;   /* region length in bytes                                   */
} SimHashRegion;

/*
 * FNV-1a over the regions with intra-state pointer normalization. A word inside a
 * region is canonicalized to a base-independent (index,offset) token; a word in
 * the userspace-pointer value window is neutralized to a constant; anything else
 * (genuine data) is hashed literally. Deterministic for identical logical state
 * regardless of ASLR.
 */
uint64_t sim_state_hash_compute(const SimHashRegion *regions, int n);

/* Write the gate result as JSON (hash, frame, replay, region names+sizes). */
int sim_state_hash_emit_json(const char *path, uint64_t hash,
                             const SimHashRegion *regions, int n,
                             int frame, const char *replay);

/* Fill `out` with the state region set: [0] = pool, [1..] = curated globals.
 * `*n` returns the count. Defined in sim_state_hash_registry.c (game build only). */
void simHashRegistryBuild(SimHashRegion *out, int *n);

/* Append the stan collision-navmesh region set starting at index *n (advances
 * *n). Defined in src/game/stan.c where the collision globals' types are visible
 * (M8.1/FID-0030). Game build only. */
void stanBuildHashRegions(SimHashRegion *out, int *n);

/* Append the g_BgRoomInfo region (room_rendered render->sim read-back, FID-0012)
 * at index *n (advances *n). Defined in src/game/bg.c. Game build only. */
void bgBuildHashRegions(SimHashRegion *out, int *n);

/* If g_simStateHashOut is set, build the registry, hash it, and emit the JSON.
 * Called at deterministic screenshot-exit, before teardown. Game build only. */
void simStateHashEmitIfRequested(int frame, const char *replay);

#endif /* SIM_STATE_HASH_H */
