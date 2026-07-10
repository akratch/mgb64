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

## Non-quirks (adjudicated)

Claims investigated and **rejected** as quirks — kept here so they are not
re-litigated:

- **Ammo crates "max out ammo" is NOT a retail quirk.** The 2026-07-05 claim
  was refuted against retail ASM (`interact_ammobox_object` reads 4-byte
  pair-stride `slots[i].quantity`): the max-out was a live port defect in the
  collect loop, fixed 2026-07-06 (public PR #23). See the ammo-crate ledger
  history.
