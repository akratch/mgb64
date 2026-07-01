/*
 * Sim-state region registry (game-coupled half of the P0.2 invariance rail).
 *
 * Kept separate from sim_state_hash.c so the pure hash primitive can be unit-
 * tested ROM-free without pulling in game/SDK headers. This file is compiled
 * only into the game executable (globbed from src/platform), never the test.
 */
#include "sim_state_hash.h"
#include "boss.h"        /* bossGetPcPoolBase / bossGetPcPoolSize */
#include "game/lvl.h"    /* g_ClockTimer, g_GlobalTimer          */

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
    *n = i;
}
