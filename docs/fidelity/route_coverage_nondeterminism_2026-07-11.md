# Route-coverage non-determinism on heavier missions (Phase B evidence, FID-0075)

> Evidence artifact for FID-0075. Internal (`docs/fidelity/**`, export-ignored).
> Recorded 2026-07-11 while authoring the Phase B archetype route subset
> (FID-0031 / D4) with the deterministic input-tape gate
> (`tools/fidelity/gate_routes_smoke.sh`).

## Summary

The deterministic-tape route gate is **reliably race-free on the light early
missions (Dam + missions 2-5)** but hits **run-to-run non-determinism on heavier
missions** in two distinct modes. This bounds the deterministic-tape route
coverage to the early missions until root-caused (Phase C inflow). It is
**FID-0046-adjacent** (Streets purity-fuzz non-determinism, still OPEN).

All runs used the determinism envelope (`--deterministic`, fixed seed
`0x12345678`) + a Release build (`/tmp/mgb64-phaseb-build/ge007`).

## Race-free (committed as gate:true routes)

| Route | Level | mission | replay hash | recording |
|-------|-------|---------|-------------|-----------|
| dam_combat_guard6  | Dam       | 1 | 3c8939968e0eb50e | self-contained |
| dam_forward_traverse (dam_forward_30s tape) | Dam | 1 | 95944e2282a48178 | self-contained |
| facility_traverse  | Facility  | 2 | 4ed9aef564de2c66 | intro-disable + replay_env |
| runway_traverse    | Runway    | 3 | b94a6243900d552e | intro-disable + replay_env |
| surface1_traverse  | Surface 1 | 4 | 84f1ce11dfdc143b | intro-disable + replay_env |
| bunker1_traverse   | Bunker 1  | 5 | 0c16993802d87011 | intro-disable + replay_env |

These pass `gate_routes_smoke.sh` and `tape_regression.sh` repeatedly (>=3 runs
each, byte-exact).

## Mode A — boss-level tape-length + record-path non-determinism

Cradle (Trevelyan) and Aztec (Jaws): under the seed envelope, **the recorded tape
LENGTH varies run-to-run** (e.g. aztec 93 vs 362 ticks with identical
`--screenshot-game-timer 360`) and the **record-path final-state hash varies**
(aztec: `ea4b…`, `a326…`, `120e…` across recordings) while the *replay* of any one
tape is internally stable. Cradle: first boot `38ce…` vs settled `dddb…`. The boss
levels are therefore unusable as determinism gates.

## Mode B — heavier-mission boot-load tick-alignment race (intermittent)

Silo (20), Archives (24), Depot (30), Egypt (32), Train (25): tape replay
**intermittently** misaligns with the signature

```
[INPUT-TAPE] WARNING: tick misalignment at record 4 (expected rel 5, got 2; g_GlobalTimer=2)
[INPUT-TAPE] playback complete: N ticks consumed (TICK MISALIGNMENT DETECTED)
```

i.e. the number of sim-ticks elapsed during level BOOT (before the first scripted
input) differs between the recording run (reached rel 5 by record 4) and a replay
run (rel 2). The input tape keys on **absolute** global ticks, so a variable boot
tick-count desynchronises playback. It reproduces neither with intro-disable
(silo/archives/depot) nor self-contained (egypt) recording, and passes 3/3 in a
direct probe yet fails in the very next serial gate run — it is **intermittent**,
consistent with a boot-time race (load-dependent tick accrual) that the fixed sim
clock does not fully mask on heavier levels. Frame-based exit
(`--screenshot-frame`) stabilises tape *length* but not the boot alignment.

## Impact / next (Phase C)

- Deterministic-tape route coverage is currently bounded to the light early
  missions. Extending to heavier missions and the boss levels needs a root-cause:
  either (a) the boot tick-accrual is genuinely non-deterministic under the seed
  envelope (a port-defect to fix), or (b) the tape's absolute-tick alignment is
  too fragile and should re-sync on a level-load marker (an instrumentation fix in
  `src/platform/input_tape.c`).
- Ties into FID-0046 (Streets purity-fuzz 19/20). Recommend investigating together.
