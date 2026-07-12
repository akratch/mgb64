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
#include <string.h>

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

    /*
     * RNG state (FID-0030). The Lehmer/LCG seed `g_randomSeed` (src/random.c) is
     * the master pseudo-random stream for the whole sim — spread, AI jitter,
     * spawn selection all draw from it. It is a plain global, NOT pool-resident,
     * so it escaped the invariance hash. It is advanced only by sim `random()`
     * draws, never by the renderer, so it stays identical render-OFF vs ON;
     * hashing it makes a render->RNG divergence a caught failure. The port's RNG
     * trace instrumentation (the s_pcRandomTrace / CallSeedEvent globals) is
     * debug-only and waived, not hashed.
     */
    {
        extern u64 g_randomSeed;
        out[i].name = "g_randomSeed";
        out[i].base = &g_randomSeed;
        out[i].size = sizeof g_randomSeed;
        i++;
    }

    /*
     * Stan collision-navmesh region set (M8.1/FID-0030 named blind spot). The
     * navmesh topology, per-query saved-collision cache, and BFS tile stack live
     * in file-scope BSS/data in src/game/stan.c (not s_pcPool). Appended by an
     * accessor there so the sizeof's see the real typed objects. See the block
     * comment on stanBuildHashRegions() for the pointer-safety argument.
     */
    stanBuildHashRegions(out, &i);

    /*
     * Room-visibility read-back (FID-0012). g_BgRoomInfo.room_rendered is written
     * by the renderer's portal/frustum pass and read back by the sim tick's
     * auto-aim visibility (chr.c/chrprop.c); a render-written field consumed by
     * sim is NOT waivable, so the array is hashed here. See bgBuildHashRegions.
     */
    bgBuildHashRegions(out, &i);

    *n = i;
}

/* Per-frame region-hash trace (GE007_SIM_HASH_EVERY_FRAME). Behavior-neutral
 * diagnostic for frame-locking an aspect A/B: emits the prop_pool (guard/prop
 * sim state) and g_BgRoomInfo (render->sim room_rendered read-back) region
 * hashes once per sim frame, keyed on g_GlobalTimer, so two runs' logs can be
 * diffed to the FIRST diverging frame + region. Only the cheap regions are
 * hashed per-frame (the 8 MB pool is skipped for speed). FID-0058. */
void simStateHashPerFrameTrace(int global_timer) {
    static int enabled = -1;
    if (enabled < 0) enabled = (getenv("GE007_SIM_HASH_EVERY_FRAME") != NULL);
    if (!enabled) return;
    SimHashRegion regs[SIM_HASH_MAX_REGIONS];
    int n = 0;
    simHashRegistryBuild(regs, &n);
    uint64_t prop = 0, room = 0;
    for (int r = 0; r < n; r++) {
        if (regs[r].name && strcmp(regs[r].name, "prop_pool") == 0)
            prop = sim_state_hash_compute_region(regs, n, r);
        else if (regs[r].name && strcmp(regs[r].name, "g_BgRoomInfo") == 0)
            room = sim_state_hash_compute_region(regs, n, r);
    }
    fprintf(stderr, "[SIM-HASH-FRAME] global=%d prop_pool=%016llx g_BgRoomInfo=%016llx\n",
            global_timer, (unsigned long long)prop, (unsigned long long)room);
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

    /* Per-region hash breakdown (GE007_SIM_HASH_PER_REGION). Behavior-neutral
     * diagnostic: lets an aspect-ratio A/B attribute a full-hash divergence to
     * the specific region whose bytes changed — e.g. is the render->sim
     * room_rendered read-back (g_BgRoomInfo) the only mover, or does it
     * propagate into pool/prop_pool guard state (auto-aim / targeting)?
     * FID-0058 / FID-0012. */
    if (getenv("GE007_SIM_HASH_PER_REGION")) {
        for (int r = 0; r < n; r++) {
            uint64_t rh = sim_state_hash_compute_region(regs, n, r);
            fprintf(stderr, "[SIM-HASH-REGION] %02d %-20s size=%zu base=%016llx hash=%016llx\n",
                    r, regs[r].name ? regs[r].name : "", regs[r].size,
                    (unsigned long long)(uintptr_t)regs[r].base,
                    (unsigned long long)rh);
        }
        fflush(stderr);
    }

    uint64_t h = sim_state_hash_compute(regs, n);
    sim_state_hash_emit_json(g_simStateHashOut, h, regs, n, frame, replay);
    fprintf(stderr, "[SIM-HASH] %016llx frame=%d replay=%s regions=%d -> %s\n",
            (unsigned long long)h, frame, replay ? replay : "", n, g_simStateHashOut);
}
