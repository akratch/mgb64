# AUDIT-0024: Rebindable System Hotkeys Use Impractical Raw Numeric Sliders

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - advertised rebinding is effectively unusable for keyboard keys |
| Priority | P2 |
| Area | Input settings / controls UI |
| Evidence level | Source and UI proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Launcher and in-game settings UI on all desktop backends |

## Summary

The menu key, menu gamepad button, and FPS key are registered as generic
integers. The settings UI consequently renders them as numeric sliders, while
the dedicated press-to-bind Controls panel omits them. Selecting an exact SDL
keyboard keycode from a range exceeding one billion values is not a usable
rebinding workflow.

## Evidence

[`platform_sdl.c`](../../../src/platform/platform_sdl.c) registers
`Input.MenuToggleKey` and `Input.FpsToggleKey` over `0..0x40000FFF`, and
`Input.MenuToggleButton` over `0..20`. Their values are raw SDL identifiers.

[`ui_settings.cpp`](../../../src/app/ui_settings.cpp) renders every non-boolean
integer setting with `ImGui::SliderInt`; it has no keycode or controller-button
editor. [`ui_bindings.cpp`](../../../src/app/ui_bindings.cpp) only iterates the
gameplay keyboard and gamepad action tables. Native UI inspection confirmed
that Controls exposes gameplay bindings but not these three system actions.

## Reproduction

1. Open Settings, enable Advanced settings if required, and locate Menu key or
   FPS key.
2. Try to select a specific key such as F2 using the integer slider.
3. Open Controls and observe that neither system action is offered for capture.
4. Locate Menu button and observe a number rather than the controller button's
   human-readable name.

## Root Cause

The settings schema has no semantic input-binding type. Host UI bindings were
added as generic integers without integrating them into the established
capture-and-name Controls workflow.

## Required End State

Expose these actions in Controls with press-to-bind capture, readable key and
button names, reset behavior, cancellation, and conflict feedback. Remove the
raw sliders from generic Settings or introduce a specialized schema kind that
renders the same complete binding interaction.

## Acceptance Criteria

- A user can bind Menu and FPS toggle keys by pressing the desired key.
- A user can bind the menu controller button by pressing the desired button.
- Current values are always displayed as names rather than SDL integers.
- Capture can be cancelled and each action can be reset to its default.
- Keyboard and controller conflicts are validated before commit.
- Hand-edited numeric configuration remains loadable when valid.

## Verification Plan

Add UI-level binding tests for capture, cancel, reset, and name rendering using
ordinary and extended SDL keycodes. Manually exercise keyboard-only and
gamepad-only navigation and confirm that the settings registry no longer
offers billion-range sliders.

## Related Work

- AUDIT-0025 defines the cross-domain conflict rules required for the menu
  controller button.
