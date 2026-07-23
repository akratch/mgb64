# FID-0046 residual — opaque-pool pointer range collision

## Symptom

The verified Streets cross-process gate began failing intermittently again. Two
fix-on processes at the same input and timer produced different hashes such as
`a94bc858e1ed651f`, `f332e08cdfd32d9d`, and `4de98be1a3031a9f`, while all four
fix-on/fix-off screenshots remained byte-identical. The original effect-buffer
zero-initialization was still active and its opt-out still reproduced unstable
hashes, so treating this as a revert would have been incorrect.

Per-region attribution reduced every failure to region 00, the 8 MiB stage
pool. Timers, `prop_pool`, RNG, all stan/navigation regions, and
`g_BgRoomInfo` were identical.

## Exact root cause

A preserved pre-relink binary reproduced the residual on process 20. Raw pool
dumps from a stable and divergent process were canonicalized offline with the
same algorithm as `sim_state_hash.c`. Only two canonical words differed:

| pool offset | raw word in both processes | stable classification | divergent classification |
|---:|---:|---:|---:|
| `0x009848` | `0x00000004a14b0000` | neutral pointer token | `pool + 0xb0000` |
| `0x051c48` | `0x00000004a14b0000` | neutral pointer token | `pool + 0xb0000` |

The raw words were byte-identical. The divergent process merely happened to
receive pool base `0x4a1400000`, putting that pre-existing pointer-shaped word
inside its moving range. The old canonicalizer checked registered-region
membership before its stable userspace-pointer window and therefore invented a
logical target in only one process. This was an instrumentation false positive,
not a gameplay or simulation delta.

An untyped byte arena cannot distinguish a live pointer from stale pointer data
or a pointer-shaped literal from one snapshot. Zeroing those bytes would mutate
game/asset storage to accommodate a diagnostic and still leave the general
collision unsolved.

## Fix

`SimHashRegion.flags` now makes pointer policy explicit:

- typed/curated regions keep the existing relative `(region, offset)` target
  canonicalization;
- the opaque 8 MiB pool tests the stable userspace-pointer value window first,
  neutralizing target addresses while retaining pointer liveness (`NULL` still
  differs from non-`NULL`);
- bytes outside that host-pointer window retain full sensitivity. A scalar whose
  whole 64-bit representation falls inside the same window is deliberately
  ambiguous and neutralized too: an untyped snapshot cannot distinguish it from
  a pointer. Pointer topology that must remain exact belongs in a typed registry
  region (as `prop_pool` already does).

`GE007_SIM_HASH_CANON_DUMP=path` writes the exact final canonical pool stream.
`tools/streets_determinism_regression.sh --diagnostic-dumps` records raw and
canonical dumps plus per-region hashes for each of its four processes, turning a
future headline mismatch into an exact-word diff without a custom build.

The ROM-free `sim_state_hash` test reproduces the old failure directly: two
opaque regions contain identical bytes, but the literal points inside only one
region's range. Their hashes must be equal. A companion assertion proves
`NULL`/non-`NULL` sensitivity remains intact.

## Baseline migration and proof

Changing pointer policy changes the hash computation once without changing tape
inputs or gameplay. The seven tape expectations were re-baselined in place; no
tape was re-recorded. All seven defaults and three declared variants pass. The
Dam Forward widescreen ON/OFF hashes remain identical, while its fire-rate
ON/OFF hashes remain distinct. AK47 authentic/legacy also remain distinct and
both match their recorded anchors.

| tape | old hash | new hash |
|---|---:|---:|
| Bunker 1 traversal | `30836eeb11a94ed3` | `558b68e551d5ca6b` |
| Dam AK47 sustained | `c52be97c2c404c36` | `81cf7074ac1a64e5` |
| Dam combat guard 6 | `65e7caa6c170dc1d` | `b87df93ce1749ea4` |
| Dam forward | `c6d1bd05d67a8902` | `709b429cfb7ff98e` |
| Facility traversal | `6676c7d05c662d57` | `ead252387d42e2eb` |
| Runway traversal | `c3ad0defd0ae0ec3` | `7c2d2a684bf5d51d` |
| Surface 1 traversal | `a1a1bde5683d6ea8` | `06836fc5979069fb` |

The effect-buffer negative control remains load-bearing: a real Streets gate
after the policy change produced fixed `3631f2dc47ba1639` twice, while opt-out
processes produced distinct `b9c1d98e9d5e9acf` and `a9368f28d5705595`.
The post-fix cross-process soak then repeated the fixed branch 20 times; all 20
produced `3631f2dc47ba1639`. The preserved pre-fix binary diverged by process 20
under the same bounded sampling method.

## Full validation

The authoritative dirty-tree Tier-3 run is recorded in
`docs/fidelity/reports/verify_9295b9831fce-dirty.json`. Every executable lane
outside the machine-local regression goldens passed:

- all 58 ROM-gated `port_` tests, including the Streets negative-control gate;
- screen-space sim invariance and the quick three-level uncap matrix;
- the full 20-level uncap matrix, both seeds, with zero divergent or unstable
  levels;
- all seven tape replays and every declared variant;
- combat, sustained AK47, six-route repeatability, patrol, aspect-cull, trace,
  and RDP ratchets.

The report's sole failure was `tools/regression_test.sh` against 60 ignored
local artifacts last captured on 2026-07-17: 18/20 levels had accepted
post-baseline state/visibility changes, while audio and spawn health remained
green. That set was preserved byte-for-byte at
`/tmp/mgb64_fid0135_baselines_pre.CB9cRj` (116 MiB, aggregate content manifest
SHA-256 `43a9ca07a953411d191989d2ae701aec51e68b63d9d5062439268f3996f32e1d`).
After a current-build recapture, the regression comparison passed 20/20 with
0.0% pixel deltas and exact 179-frame state, audio, render-health, and spawn
checks. The accepted widescreen set is independently preserved at
`/tmp/mgb64_fid0135_baselines_wide.uwE6IL` (aggregate
`293ca801b850af3e7ad66e2fd9ee78e1fafebff23d1ce46555f1dc651f272bb9`) for the
separate FID-0135 4:3 capture-contract migration.

The one Tier-3 skip is explicit pre-existing S2 coverage debt: there is no
`gate:true` pixel checkpoint with a cached stock PPM, so the verifier refused a
vacuous pixel-oracle pass. It is unrelated to hash computation and remains
visible in the report rather than being converted to green.
