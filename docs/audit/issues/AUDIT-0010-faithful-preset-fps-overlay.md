# AUDIT-0010: Faithful Presets Retain the Non-Original FPS Overlay

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S4 - visible preset-contract violation |
| Priority | P2 |
| Area | Presentation / launch presets |
| Evidence level | Source proven; behavior is explicitly intentional |
| Confidence | High |
| Origin | Standardized from renderer audit 2026-07-06 finding 29 and backlog owner-decision item |
| Affected configurations | Interactive `--faithful` and `--faithful-hd` launches without a later FPS-overlay override |

## Summary

Both faithful launch presets leave `Video.FpsOverlay` at its enabled default.
The resulting top-right FPS, frame-time, and one-percent-low widget is native
debug UI that did not exist in the retail game. This contradicts the presets'
documented purpose of pinning remaster departures to the original look.

This report does not require changing the ordinary or remaster defaults. It is
limited to the semantic promise made by `--faithful` and `--faithful-hd`.

## Evidence

[`g_pcFpsOverlay`](../../../src/platform/platform_sdl.c#L388) defaults to one.
Its source comment explicitly identifies the widget as non-original and says it
is deliberately absent from both faithful preset tables.

The registered [`Video.FpsOverlay`](../../../src/platform/platform_sdl.c#L1897)
default is also one. The widget emits its display list whenever that value is
nonzero and the separate benchmark/screenshot suppression conditions do not
apply in [`pc_fps_overlay.c`](../../../src/game/pc_fps_overlay.c#L120).

[`s_faithfulPreset`](../../../src/platform/platform_sdl.c#L2502) pins other
native HUD departures such as the modern crosshair, hit markers, target
feedback, and minimap to zero, but contains no `Video.FpsOverlay` entry.
[`s_faithfulHdPreset`](../../../src/platform/platform_sdl.c#L2598) has the same
omission. The surrounding comments describe these tables as the original look
and state that they pin remaster departures to pre-remaster values.

The older backlog already classifies the always-on overlay as an owner decision
rather than an accidental implementation detail. The current decision can
remain valid for the normal launch profile while still honoring the narrower
faithful-preset contract.

## Reproduction

1. Start an interactive session with a clean/default configuration and
   `--faithful`.
2. Do not use `--deterministic`, a screenshot-frame option, or
   `GE007_BACKGROUND`; those modes independently suppress the widget.
3. Enter gameplay and observe the top-right performance overlay.
4. Repeat with `--faithful-hd`; the same overlay remains.
5. Add `--config-override Video.FpsOverlay=0` as a control; the overlay
   disappears.

No runtime reproduction was needed to establish the preset state: the default,
preset tables, and render gate are direct and deterministic source evidence.

## Root Cause

The code treats performance diagnostics as outside the visual-look contract,
while the preset already governs other native HUD additions. That distinction
is not visible to a player and conflicts with the literal faithful-mode result:
the final frame contains UI absent from the original game.

## Required End State

Add `Video.FpsOverlay=0` to both faithful preset tables. Keep the setting live
and independently overridable so a player can deliberately re-enable the
widget after selecting a preset. Preserve the current normal/remaster default
unless a separate product decision changes it.

The transient preset behavior must remain read-only: launching faithfully must
not overwrite the persisted `ge007.ini` preference.

## Acceptance Criteria

- A clean interactive `--faithful` launch begins with the FPS overlay disabled.
- A clean interactive `--faithful-hd` launch begins with the overlay disabled.
- The same commands followed by a higher-precedence
  `--config-override Video.FpsOverlay=1` show the overlay.
- The normal and `--remaster` profiles retain their currently selected default.
- Neither faithful launch writes the transient zero value to `ge007.ini`.
- Existing deterministic, background, benchmark, and screenshot suppression
  remains unchanged.
- The preset tests assert the effective value and override precedence.

## Verification Plan

Extend the ROM-free preset test matrix to inspect the effective
`Video.FpsOverlay` value for faithful, faithful-HD, remaster, and explicit CLI
override cases. Add one interactive screenshot smoke for each faithful preset
that masks nondeterministic game content and asserts the overlay region contains
no widget draw.

## Related Work

- Prior monolithic finding:
  [`RENDERER_SIM_AUDIT_2026-07-06.md` finding 29](../../RENDERER_SIM_AUDIT_2026-07-06.md).
- Existing owner-decision entry:
  [`BACKLOG_v0.4.0.md`](../../BACKLOG_v0.4.0.md).

## Resolution <!-- triage-2026-07-14 -->

Verified already fixed in commit `6c1a23d` (ledger Status was stale). Both faithful presets pin `Video.FpsOverlay=0` (platform_sdl.c:2526, :2623), so the non-original overlay is off under --faithful/--faithful-hd.
