# AUDIT-0050: Duplicate Bindings Trigger Both Actions Despite Last-Writer Message

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S3 - a permitted binding conflict produces unintended simultaneous game actions |
| Priority | P2 |
| Area | Input bindings / conflict semantics |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Any duplicate keyboard or gamepad gameplay binding |

## Summary

Controls allows the same key, button, or trigger to be assigned to multiple
gameplay actions and only adds a conflict label. Its tooltip says the last one
set wins in-game. The input implementation independently queries every action,
so a shared physical input activates all matching actions instead.

## Evidence

[`ui_bindings.cpp`](../../../src/app/ui_bindings.cpp) detects duplicate values
after commit but neither rejects the new value nor clears the old owner. Its
tooltip states `the last one set wins in-game` for both keyboard and gamepad
tables.

[`stubs.c`](../../../src/platform/stubs.c) checks every registered keyboard
action in independent `if` statements. It likewise calls
`gamepadBindingActive` independently for Jump, Reload, Pause, Look, Alt Fire,
Aim, Fire, C-look, weapon cycle, and crouch. There is no winner selection or
binding timestamp in [`input_bindings.c`](../../../src/platform/input_bindings.c).

For example, binding Fire and Aim to the same input sets both N64 Z and R in the
same poll; binding Forward and Reload together moves and reloads.

## Reproduction

1. Bind Fire and Aim to the same keyboard key or gamepad control.
2. Observe both rows marked conflict and read the last-writer tooltip.
3. Press the control in gameplay.
4. Both actions activate; the previously assigned action did not lose ownership.

## Root Cause

Conflict display was added without defining a commit policy. UI wording assumes
last-writer ownership, while the runtime stores only one value per action and
has no reverse physical-input owner map.

## Required End State

Define and enforce one coherent policy. For exclusive gameplay bindings,
transactionally move the physical input to the newly selected action, reject
the conflict, or ask before clearing the previous owner. If deliberate chords
are supported, label them as such and explicitly enumerate safe combinations.
UI and runtime semantics must match exactly.

## Acceptance Criteria

- A completed exclusive rebind leaves one gameplay owner per physical input.
- Reject/cancel leaves both prior bindings unchanged.
- Any reassignment is visible before it is persisted.
- Hand-edited duplicate files receive the same deterministic policy at load.
- Tooltips describe actual runtime behavior.
- Keyboard, button, and trigger conflicts share the policy.

## Verification Plan

Add binding-model tests for duplicate keyboard, button, and trigger assignment,
covering commit, cancel, load, and reset. Feed the resulting states into the
input mapper and assert exactly the intended N64 control bits or edge actions.

## Related Work

- AUDIT-0025 covers conflicts between the system menu button and gameplay.
- AUDIT-0051 covers hidden fixed-alternate conflicts.

## Resolution

Fixed on `feat/webgpu-backend`. The Controls panel's move-or-reject
duplicate-binding policy (added in 2f4805d) was applied to the keyboard and
gamepad-button capture branches but **not** to the two gamepad TRIGGER-capture
branches (`src/app/ui_bindings.cpp`), which called `gamepadBindingSetTrigger` +
`gamepadBindingSave` unconditionally — so a trigger already owned by another
action silently double-acted in-game.

- New SDL-free primitive `src/platform/binding_conflict.{h,c}` (`bindingOwnerOf`)
  operating on the raw encoded-binding array; `GB_NONE`/`GB_AXIS_BASE` moved here
  as the single source shared by the runtime, the UI, and the test.
- `input_bindings.c` exports `gamepadTriggerEncoded(sdl_axis)` (so the UI never
  hardcodes the trigger encoding) and `gamepadBindingOwnerOf(enc, exclude)`
  (respecting force-defaults like `gamepadBindingEncoded`).
- `padOwner` (ui_bindings.cpp) now delegates to `gamepadBindingOwnerOf`, and both
  trigger-capture branches apply the same reject-if-owned policy as the button
  branch (naming the owner, no set/save on conflict).

Guarded by the ROM-free/SDL-free ctest `binding_conflict`
(tests/test_binding_conflict.c): `bindingOwnerOf` finds button and trigger owners,
treats `GB_NONE` as never-conflicting, honours `exclude`, and confirms trigger
(≥ `GB_AXIS_BASE`) and button (< `GB_AXIS_BASE`) encodings never alias. Full
engine + app build links; gamepad bindings are host input mapping (force-defaults
uses `kGpDefault`, untouched), so the 7 input tapes stay byte-exact. The
interactive on-screen reject-message verification is left for owner UI validation.
