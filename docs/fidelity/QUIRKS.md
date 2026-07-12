# N64-Quirk Registry (FID-0045)

Retail bugs that the port **faithfully preserves**. Per the charter taxonomy
(rule 8), an `n64-quirk` is a defect present in the retail game itself —
reproduced on real hardware / the ares oracle — that the port must keep to
stay faithful. Player-helping mitigations, when they exist at all, ship
**opt-in default-OFF** (charter rule 4).

Registry contract (charter §4.6): every entry carries (a) the retail anchor
(ASM citation, authored-data citation, or oracle capture) proving the bug is
retail's, (b) the player-visible effect, and (c) the opt-in mitigation flag
if one exists. Entries without a retail anchor do not belong here — file a
ledger finding first.

New quirks are filed as `n64-quirk` findings via `tools/fidelity/ledger.py`
and added here when the anchor is verified. The ledger entry is the source of
truth for status; this file is the human-readable registry.

| # | Quirk | Retail anchor | Player-visible effect | Mitigation flag |
|---|-------|---------------|----------------------|-----------------|
| Q1 | Guard shuffle: a guard on patrol/stopped with line of sight to Bond at >20 m shuffles about uselessly instead of committing to an action | `src/game/chraidata.c:715` — authored AI list; the distance check falls through to the loop when LOS holds beyond 20 m (annotated `// BUG` in the matched decomp, present in retail AI data) | Distant alerted guards jitter in place rather than advancing or returning to patrol | none (preserved as-is) |

## Quirks not reproducible on the port (waived)

A distinct sub-class: retail bugs that depend on N64-specific *machine state*
(uninitialized stack slots, prior-frame RAM contents) which a host port cannot
faithfully reproduce. Faithfully replaying the quirk would require reading
uninitialized host memory — undefined behavior with non-deterministic, possibly
crashing results — whereas on console the read returns the deterministic (if
garbage) contents left by the previous call/frame. The port therefore
substitutes a deterministic, safe value (zero-init). These are registered here
so the substitution is auditable, and carry a concrete retest condition rather
than an opt-in mitigation flag. The ledger entry (status `waived`) is the source
of truth for status.

| # | Quirk (retail) | Retail anchor | Port substitute | Retest condition (ledger FID) |
|---|----------------|---------------|-----------------|-------------------------------|
| W1 | `stan.c` reads two uninitialized stack locals: the on-edge seam flag at `sp+0x88` (sub_GAME_7F0AF20C) and `lastCrossEdge` at `sp+0x98` (sub_GAME_7F0B0914). Neither slot is stored before it is read, so retail runs the midpoint walk-back disambiguation / writes `stanSavedColl_pointI` from whatever the caller left on the stack | `src/game/stan.c:1026` (`lw $s2,0x88($sp)`) and `src/game/stan.c:3463` (`lw $fp,0x98($sp)`), re-derived from GLOBAL_ASM; no prior store to those stack offsets in either prologue/body | Port zero-inits both locals (`src/game/stan.c:746` on-edge flag, `src/game/stan.c:3290` lastCrossEdge), yielding a deterministic result | FID-0081. Re-open if a movement/seam oracle lane shows a tile-pick or `stanSavedColl_pointI` divergence at scaled tile edges within 2.0 units — that would prove the garbage-dependent branch is reachable and needs an ares/console capture of the actual stack contents to model |

## Opt-in cosmetic mitigations (faithful default OFF)

A third sub-class: the faithful default reproduces retail exactly, but the port
offers an **opt-in** deviation that softens an N64 artifact for players who want
it. Per charter rule 4 these default **OFF** (faithful), enabled by an env flag,
and are registered here so the deviation is auditable. The ledger entry is the
source of truth for status.

| # | Mitigation (opt-in) | Retail anchor (faithful default) | Opt-in effect | Enable flag (ledger FID) |
|---|---------------------|----------------------------------|---------------|--------------------------|
| M1 | Glass shot-depth tolerance: accept a glass / tinted-glass hit up to N world units *behind* the shot's limit plane, so cracks attach to a pane coplanar with the background collision surface instead of the wall behind it | Retail hit-depth gates `sub_GAME_7F04E720` / `sub_GAME_7F04E9BC` reject any object hit behind the plane outright (`c.le.s` then `bc1fl` return when `-transformed.z > shotdata->unk34`); the faithful default tolerance is 0.0, which is byte-identical to this rejection | Registers an extra prop hit retail never makes — plus the crack/shatter bookkeeping and PRNG draws that follow it — so it is sim-visible and deliberately OFF by default | `GE007_GLASS_SHOT_DEPTH_TOLERANCE` (a positive value, clamped to 20.0, enables it), FID-0083. The ROM-free lane `glass_shot_depth` pins both sides |

## Non-quirks (adjudicated)

Claims investigated and **rejected** as quirks — kept here so they are not
re-litigated:

- **Ammo crates "max out ammo" is NOT a retail quirk.** The 2026-07-05 claim
  was refuted against retail ASM (`interact_ammobox_object` reads 4-byte
  pair-stride `slots[i].quantity`): the max-out was a live port defect in the
  collect loop, fixed 2026-07-06 (public PR #23). See the ammo-crate ledger
  history.
