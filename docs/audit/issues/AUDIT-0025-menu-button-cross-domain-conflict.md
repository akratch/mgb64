# AUDIT-0025: Menu-Button Rebinding Can Create a Reserved-Button Double Role

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - a valid UI setting can conflict with gameplay and disrupt input |
| Priority | P2 |
| Area | Gamepad binding validation / overlay input |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Gamepad play after changing `Input.MenuToggleButton` |

## Summary

Gameplay capture excludes whichever gamepad button currently opens the overlay,
but changing the overlay button later does not validate existing gameplay
bindings. For example, setting Menu button to A leaves the default Jump=A
binding intact, so a single press becomes both a system-menu command and a game
action. Some choices can repeatedly gate gameplay behind the overlay.

## Evidence

[`ui_bindings.cpp`](../../../src/app/ui_bindings.cpp) excludes
`Overlay_gamepadToggleButton()` and Guide only while scanning a new gameplay
capture. Its conflict checker compares gameplay actions with one another, not
system actions.

[`input_bindings.c`](../../../src/platform/input_bindings.c) validates loaded
bindings against hardcoded Back and Guide values. It does not consult the
configured menu button. [`platform_sdl.c`](../../../src/platform/platform_sdl.c)
accepts any menu-button integer from 0 through 20 without cross-validation.

[`ui_overlay.cpp`](../../../src/app/ui_overlay.cpp) reads the configured button
for every controller event, while `gamepadBindingActive` independently reads
the same physical button for gameplay. There is no exclusive ownership layer.

## Reproduction

1. Keep the default Jump=A gameplay binding.
2. Change `Input.MenuToggleButton` from Back to A in Settings or the INI file.
3. Press A in gameplay.
4. The overlay opens even though A remains assigned to Jump; no conflict was
   shown or resolved when the system binding changed.

Equivalent conflicts can be created with other gameplay buttons, and the raw
range also includes Guide despite its existing reserved status.

## Root Cause

System bindings and gameplay bindings use separate validation domains. The
capture-time exclusion assumes the system binding is immutable and therefore
does not protect later system reconfiguration or hand-edited configuration.

## Required End State

Model overlay and OS-reserved buttons in the same conflict system as gameplay
bindings. Make a system-button change transactional: reject it with a clear
conflict, or explicitly clear/reassign the affected gameplay action with user
confirmation. Apply the same policy during configuration load.

## Acceptance Criteria

- A menu-button change cannot silently duplicate a gameplay binding.
- Guide and any other platform-reserved buttons cannot become game bindings.
- Loading a hand-edited INI applies the same dynamic reserved-button rules as
  UI capture.
- Changing the menu button updates subsequent gameplay capture exclusions.
- A rejected or cancelled change leaves every prior binding intact.
- Default Back and gameplay bindings remain unchanged.

## Verification Plan

Add a binding-model test that moves Menu from Back to A while Jump uses A,
covers reject and confirmed-reassignment policies, and repeats the matrix via
INI load. Exercise the result on a controller to verify that each accepted
physical button has exactly one owner per input context.

## Related Work

- AUDIT-0024 requires a usable capture UI for these system actions.
