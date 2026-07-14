# AUDIT-0051: Fixed Alternate Controls Are Absent from Conflict Detection

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S3 - apparently conflict-free rebinds can trigger a hidden second action |
| Priority | P2 |
| Area | Input bindings / fixed controls |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Rebinds using a keyboard or gamepad input claimed by a fixed alternate |

## Summary

The Controls conflict checker compares only rebindable action values. Runtime
also owns several fixed inputs, including F and Backspace for Reload, arrows and
I/J/K/L for C controls, Enter/Space/Tab, and gamepad X for Reload. Assigning one
of those inputs to another action shows no conflict but activates both roles.

## Evidence

[`ui_bindings.cpp`](../../../src/app/ui_bindings.cpp) implements keyboard
conflicts by comparing `inputBindingScancode` values and gamepad conflicts by
comparing `gamepadBindingName` values. Fixed alternates are only listed as
informational footer text; they are not entries in either comparison.

[`stubs.c`](../../../src/platform/stubs.c) ORs fixed keyboard scancodes and
registered actions into the same N64 button word. Reload uses its binding plus
F and Backspace; fixed arrows and I/J/K/L set C buttons; Return, Space, and Tab
have fixed roles. The gamepad path applies registered actions and then maps X
to reload independently.

Binding keyboard Fire=F or gamepad Fire=X therefore produces Fire plus Reload
without any conflict label in Controls.

## Reproduction

1. Bind Fire to keyboard F; observe no conflict label and press F in gameplay.
2. Bind gamepad Fire to X; observe no conflict label and press X in gameplay.
3. In both cases, Fire activates together with the hidden fixed Reload role.

Equivalent hidden collisions exist for other fixed keyboard inputs.

## Root Cause

The UI models only configurable records, while the runtime input ownership
surface also contains hardcoded alternates outside the registry.

## Required End State

Represent every physical-input claim in one conflict graph, including immutable
system/gameplay alternates. Either reserve fixed inputs from capture, make their
roles configurable, or transactionally remove the fixed role under an explicit
policy. Do not rely on footer documentation as validation.

## Acceptance Criteria

- Capturing F, Backspace, arrows, I/J/K/L, Enter, Space, Tab, or gamepad X
  resolves or rejects every conflicting fixed role.
- No accepted exclusive binding has an unreported runtime double action.
- The policy also applies to hand-edited files.
- Fixed frontend and gameplay contexts are distinguished where simultaneous
  ownership is actually safe.
- Conflict UI names the other action before commit.
- Default controls retain all intended alternates.

## Verification Plan

Build a physical-input ownership table test covering every SDL scancode,
button, and trigger used by runtime fixed or configurable paths. Exercise each
collision through capture and file load, then assert resulting N64 buttons and
edge actions in frontend and gameplay contexts.

## Related Work

- AUDIT-0050 covers duplicate configurable actions and the incorrect
  last-writer message.
- AUDIT-0024 covers missing capture UI for system hotkeys.

## Resolution <!-- triage-2026-07-14 -->

Verified already fixed in commit `2f4805d` (ledger Status was stale). kReservedKeys/padReserved now enumerate every fixed physical-input claim and conflict detection consults them (ui_bindings.cpp:29+).
