# Sim-hash coverage audit — FID-0030 / M8.1 (S-Tier Task 0.4)

**Verdict: 0 UNCOVERED.** "What the sim-state invariance hash covers" is now a
machine-checked property (ctest `fidelity_hash_coverage`), not tribal knowledge.

## How it works
- `ge007 --print-sim-hash-regions` emits the live `SimHashRegion` table
  (`name base size`, one per line) with **no ROM** — it only reads the static
  region bases plus the not-yet-allocated pool.
- `tools/fidelity/hash_coverage_audit.py` runs `nm` over the built decomp objects
  (`src/game/*.c.o` + top-level `src/*.c.o`) to enumerate every writable
  (`.data`/`.bss`/`__common`) symbol, computes the ASLR load slide from a region
  whose name is a global symbol (`g_ClockTimer`/`g_GlobalTimer`), and dispositions
  each symbol: **hashed** (runtime address inside a region), **waived**
  (`docs/fidelity/hash_waivers.txt` pattern, with reason), or **UNCOVERED**
  (fails, exit 1).

## Result (this build)
| bucket | count |
|---|---|
| writable decomp symbols | 3316 |
| hashed (inside a region) | 26 |
| waived (with reason) | 3285 |
| **UNCOVERED** | **0** |
| absent from linked exe (dead-stripped; nothing at runtime) | 5 |

Machine-readable detail: regenerate with
`python3 tools/fidelity/hash_coverage_audit.py --binary build/ge007 --objroot
build/CMakeFiles/ge007.dir --waivers docs/fidelity/hash_waivers.txt --report
build/hash_coverage_report.json` (the ctest writes the same JSON to
`build/hash_coverage_report.json`; the `docs/fidelity/reports/` dir is ephemeral).

## Regions added this task (the blind spots)
Registry grew from 4 → 27 regions:
- **`g_randomSeed`** — master RNG stream (`src/random.c`), a plain global, not
  pool-resident. Advances only on sim `random()` draws.
- **stan collision navmesh** (`stanBuildHashRegions`, `src/game/stan.c`) — the
  M8.1-named blind spot: navmesh topology (`firststaninroom` 2048B,
  `stan_room_bbox` 3072B, `stan_prefix`, room counts, scale/anchors), the
  per-query saved-collision cache (`stanSavedColl_*`), and the BFS tile stack
  (`bfsTileStack` 2816B). Array extents are `_Static_assert`-guarded so a future
  edit cannot silently decay one to an 8-byte pointer (the M8.1 prop-pool bug).
- **`g_BgRoomInfo`** (15600B, `bgBuildHashRegions`) — its `room_rendered`
  visibility bytes are written by the renderer's portal/frustum pass but **read
  back by the sim tick** (auto-aim in `chr.c`/`chrprop.c`). Per the FID-0012
  read-back rule (a render-written field consumed by sim is NOT waivable) it is
  hashed, not waived.

## Pointer canonicalization contract

Typed/curated regions retain relative pointer-target sensitivity: an in-region
pointer hashes as `(region, offset)`, so changing a prop link to another target
still reddens the invariant. The 8 MiB `pool` is explicitly marked opaque. Its
bytes contain typed sim objects, raw/converted asset payloads, allocator scratch,
and dead storage without a field map; therefore a word falling inside the
current process's ASLR-moved pool range is not proof that the word is a live
pointer. Opaque-pool pointer-shaped words preserve liveness (`NULL` differs from
non-`NULL`) but neutralize target identity. This necessarily also neutralizes a
non-pointer scalar whose complete 64-bit representation happens to fall inside
the host pointer-value window: the untyped byte stream provides no sound way to
tell those cases apart. Values outside that window retain byte sensitivity, and
pointer-rich simulation structures should be promoted to typed registry regions
when target topology is a required invariant (as `prop_pool` already is).

FID-0046 proved why this is necessary: two Streets processes had byte-identical
raw words at pool offsets `0x9848` and `0x51c48`, both `0x00000004a14b0000`.
Only one process happened to allocate its live pool at `0x4a1400000`, so the old
range-membership heuristic reclassified the identical literal as
`pool + 0xb0000` in that process and emitted a different hash. No logical
simulation payload differed. `test_sim_state_hash` pins this exact ASLR
range-collision class.

## Waiver categories (docs/fidelity/hash_waivers.txt)
Object-scoped (`@file.c.o`) or name-pattern waivers, each with a one-line reason:
mechanical retail-address-named artifacts (`D_8*`, `dword_*`, jump tables, …),
function-static per-call scratch (`*.*`), immutable object/definition tables
(`*objdata*`, `init*`), debug/diagnostics/cheat TUs, port/host + audio TUs,
render/DL/HUD/menu/text TUs (verified free of render→sim read-back beyond the
registered `g_BgRoomInfo`), and render-invariant game-logic sim TUs (their
per-symbol determinism is enforced by the trace-state lane + the purity-fuzz
gate FID-0033, not this render-invariance hash). A bare `*` waiver is rejected so
a newly added mutable global in a new TU still surfaces as UNCOVERED.

## Re-baseline (regions changed the hash value once, as expected)
- `tools/sim_invariance_gate.sh dam1 400 2` → **PASS** (OFF hash == ON hash =
  `ae6cb6fe43a92840`): the new regions — including `g_BgRoomInfo` — are
  byte-identical render-OFF vs render-ON.
- Dam (level 33) deterministic capture, my build vs the pre-change (stashed)
  build: **state trace byte-identical** (`diff` clean) and **screenshot
  byte-identical** (md5 `03b87f7a20bc4ef35739a7c885def135`). Only the internal
  sim-hash *value* shifted (more regions), which is the expected one-time change;
  no screenshot/trace lane moved.
