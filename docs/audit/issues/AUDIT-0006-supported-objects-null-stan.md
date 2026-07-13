# AUDIT-0006: Supported-Object Destruction Dereferences Nullable STAN Pointers

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S2 - data-dependent object-destruction crash |
| Priority | P2 |
| Area | Gameplay / objects / destruction |
| Evidence level | Analyzer and source proven; natural trigger not yet reproduced |
| Confidence | High for the unsafe path; medium for ordinary-play frequency |
| Origin | Newly confirmed on 2026-07-12 |
| Affected configurations | Destruction of a top-level object without STAN, or scans containing a STAN-less object/weapon |

## Summary

`object_explosion_related` explicitly treats a top-level object's STAN pointer
as nullable when deciding whether to create an explosion. It then calls
`objDestroySupportedObjects` for the same top-level object. That helper
immediately dereferences the nullable STAN to obtain its room and later
dereferences every object or weapon STAN in the active position list without a
guard.

This creates two independent null-dereference sites during object destruction.
Static analysis proves the first path from the explicit `stan != NULL` branch.
A natural shipped-stage trigger has not yet been reproduced, so frequency must
not be inferred from the S2 crash impact.

## Evidence

- [`objDestroySupportedObjects`](../../../src/game/chrobjhandler.c#L34799)
  evaluates `tableprop->stan->room` at line 34809 without validating
  `tableprop`, `tableprop->obj`, or `tableprop->stan`.
- Its scan condition at line 34818 evaluates `prop->stan->room` for every
  `PROP_TYPE_OBJ` or `PROP_TYPE_WEAPON`, also without checking `prop->stan`.
- [`object_explosion_related`](../../../src/game/chrobjhandler.c#L34839) stores
  `topProp->stan`, checks it for NULL at line 34874, and deliberately skips only
  explosion creation when it is absent.
- If the object is top-level and not removed immediately, control reaches
  `objDestroySupportedObjects(prop, owner)` at line 34909 regardless of the
  earlier NULL result.
- Clang's static analyzer follows that branch and reports the dereference at
  line 34809.

The relevant control-flow contradiction is:

```c
stan = topProp->stan;
if (stan != NULL) {
    /* explosion creation */
}
/* ... */
objDestroySupportedObjects(prop, owner); /* dereferences prop->stan */
```

## Reproduction

Use a focused object fixture rather than waiting for an incidental stage:

1. Create a destructible, top-level object whose `prop->stan` is NULL, with no
   remove-on-destroy flag.
2. Drive `object_explosion_related` through first destruction.
3. The current code reaches `objDestroySupportedObjects` and dereferences NULL.
4. Independently, use a valid table STAN but place an object or weapon with a
   NULL STAN in the position list; the scan dereferences that member at line
   34818.

Run both fixtures under ASan/UBSan. A stage census should separately determine
whether either state exists in shipped setups or can arise through pickup,
parenting, removal, or dynamic spawn transitions.

## Root Cause

Explosion creation correctly models STAN as optional, but the supported-object
fall helper retains an older invariant that every relevant prop is on a STAN
tile. The helper uses STAN solely to establish room equality, so a missing tile
has no defined fallback and should make that prop ineligible for the room-local
support scan.

## Required End State

Make `objDestroySupportedObjects` total over nullable runtime props:

- Return without scanning if the table prop, table object, or table STAN is
  absent, because there is no room anchor for support discovery.
- During the list walk, require `prop->stan != NULL` before comparing rooms.
- Preserve collision-bound calculation, vertical checks, polygon membership,
  `objFall` ordering, and player ownership for every fully populated prop.
- Do not invent a room from stale fields or the first `rooms[]` entry unless a
  separate invariant proof establishes that as the canonical replacement.

The caller must continue the destroyed object's own deformation/fall logic when
the supported-object scan is skipped. A missing STAN should suppress only the
room-local dependent-object search, not the remainder of destruction.

## Acceptance Criteria

- Both NULL-STAN fixtures complete without a signal under ASan/UBSan.
- A NULL table STAN causes zero supported-object scans and does not prevent the
  table object's remaining destruction/fall logic.
- A NULL STAN on an unrelated object or weapon skips only that list entry.
- Valid same-room supported objects still fall in the same order and receive
  the same owner as before.
- Different-room and out-of-polygon objects remain unaffected.
- No extra RNG call is introduced on the normal or guarded paths.
- A shipped-stage census documents whether and when NULL STAN states occur.

## Verification Plan

Add ROM-free fixtures for the two null sites and a populated control fixture.
Instrument `objFall` and the RNG call count. Then run destruction-heavy stage
smokes under ASan and census active object/weapon STAN pointers at setup,
pickup/drop, parenting, explosion, and removal transitions.

## Related Work

This report concerns native memory safety. Retail assembly should still be
checked before restructuring the valid-STAN path, but no retail behavior can
make a native NULL dereference a valid end state.
