/*
 * Sim-state region registry (game-coupled half of the P0.2 invariance rail).
 *
 * Kept separate from sim_state_hash.c so the pure hash primitive can be unit-
 * tested ROM-free without pulling in game/SDK headers. This file is compiled
 * only into the game executable (globbed from src/platform), never the test.
 */
#include "sim_state_hash.h"
#include "boss.h"          /* bossGetPcPoolBase / bossGetPcPoolSize          */
#include "game/lvl.h"      /* g_ClockTimer, g_GlobalTimer                    */
#include "game/chrai.h"    /* pos_data_entry, POS_DATA_ENTRY_LEN, PropRecord  */
#include <stdio.h>
#include <stdlib.h>

/* Set from --sim-state-hash-out (defined in main_pc.c). */
extern const char *g_simStateHashOut;

/*
 * The mutable simulation-state region set. [0] is the 8 MB s_pcPool arena where
 * the bulk of dynamic game state lives; the remainder are curated non-pooled
 * game globals. Rendering flags must never perturb any of these. Keep additions
 * to genuine simulation state — a global that legitimately varies (a render
 * cache, a frame counter tied to wall-clock) would false-trip the gate.
 */
void simHashRegistryBuild(SimHashRegion *out, int *n) {
    int i = 0;
    out[i].name = "pool";
    out[i].base = bossGetPcPoolBase();
    out[i].size = bossGetPcPoolSize();
    i++;
    out[i].name = "g_ClockTimer";
    out[i].base = &g_ClockTimer;
    out[i].size = sizeof g_ClockTimer;
    i++;
    out[i].name = "g_GlobalTimer";
    out[i].base = &g_GlobalTimer;
    out[i].size = sizeof g_GlobalTimer;
    i++;

    /*
     * Prop record pool (M8.1). Guard/door/object/pickup positions, collision
     * tile, room membership, regen timers, and the parent/child/prev/next links
     * live here — a static BSS array (pos_data_entry), NOT inside s_pcPool, so
     * movement and collision drift escaped the pool region entirely. Chr and
     * player records are pool-allocated (mempAllocBytesInBank, MEMPOOL_STAGE)
     * and already covered by [0]; this is the missing half. The free/active
     * prop lists are threaded through these records' link fields, so the list
     * topology is hashed here too — the standalone list-head globals add no
     * state this region doesn't already carry. Embedded pointers are safe: the
     * intra-array links canonicalize to the prop-pool region token, chr/obj
     * links to the pool token, and model/anim links to the neutral
     * pointer-window constant — all ASLR- and render-flag-invariant. Size is
     * element-count * element-size (not sizeof of a decayed array pointer).
     */
    out[i].name = "prop_pool";
    out[i].base = pos_data_entry;
    out[i].size = (size_t)POS_DATA_ENTRY_LEN * sizeof(PropRecord);
    i++;

    *n = i;
}

void simStateHashEmitIfRequested(int frame, const char *replay) {
    if (!g_simStateHashOut) {
        return;
    }
    SimHashRegion regs[SIM_HASH_MAX_REGIONS];
    int n = 0;
    simHashRegistryBuild(regs, &n);

    /* Optional raw-pool dump for divergence diagnosis (GE007_SIM_HASH_DUMP=path). */
    {
        const char *dump = getenv("GE007_SIM_HASH_DUMP");
        if (dump && bossGetPcPoolBase()) {
            FILE *df = fopen(dump, "wb");
            if (df) {
                fwrite(bossGetPcPoolBase(), 1, bossGetPcPoolSize(), df);
                fclose(df);
            }
        }
    }

    uint64_t h = sim_state_hash_compute(regs, n);
    sim_state_hash_emit_json(g_simStateHashOut, h, regs, n, frame, replay);
    fprintf(stderr, "[SIM-HASH] %016llx frame=%d replay=%s regions=%d -> %s\n",
            (unsigned long long)h, frame, replay ? replay : "", n, g_simStateHashOut);
}
