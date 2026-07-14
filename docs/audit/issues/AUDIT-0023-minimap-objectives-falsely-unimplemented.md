# AUDIT-0023: Implemented Minimap Objectives Are Hidden as Having No Effect

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S4 - implemented player functionality is mislabeled and hidden |
| Priority | P2 |
| Area | Minimap / settings discoverability |
| Evidence level | Runtime and test proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Builds exposing the generated settings registry |

## Summary

`Input.MinimapObjectives` controls a working objective-pin layer, but its help
says the option is reserved, unimplemented, and has no effect. It is also moved
to Advanced on that obsolete premise, preventing normal discovery of a useful
minimap control.

## Evidence

[`platform_sdl.c`](../../../src/platform/platform_sdl.c) registers the setting
with help ending in `Not yet implemented -- no effect currently` and marks it
Advanced with the comment `unimplemented layer`.

[`minimap.c`](../../../src/game/minimap.c) checks the setting in
`minimap_copy_objective_pins` and emits supported destroy, collect, deposit,
photograph, enter-room, and deposit-room objectives. That function is called
for every copied minimap frame.

The full audit test run passed the minimap tests. Its enabled level-32 artifact
contained one objective pin with finite coordinates, and the audit reported
`PASS objectives=1 overlay drawn`. [`minimap_smoke.sh`](../../../tools/minimap_smoke.sh)
explicitly enables the setting, while
[`audit_minimap_dump.py`](../../../tools/audit_minimap_dump.py) can require
stage-derived objective pins.

## Reproduction

1. Open Settings and inspect the non-Advanced minimap controls; the objective
   layer is absent.
2. Enable Advanced settings and read Minimap objectives; it claims no effect.
3. Run `tools/minimap_smoke.sh` with a valid ROM and inspect its enabled JSON.
4. Observe nonzero `objective_count` and a rendered objective pin.

## Root Cause

The registry metadata and Advanced classification were not updated when the
objective-copy and rendering implementation landed.

## Required End State

Publish accurate help describing the objective-pin layer and expose the toggle
beside the other player-facing minimap controls. Keep genuinely diagnostic or
unsupported objective controls Advanced, but not the working layer switch.

## Acceptance Criteria

- Settings no longer describe the option as reserved or ineffective.
- The objective toggle is available without enabling Advanced settings.
- Turning only `Input.MinimapObjectives` off suppresses objective pins while
  leaving the minimap and other pin layers enabled.
- Turning it on restores eligible stage objectives.
- Generated help and configuration documentation use the same description.

## Verification Plan

Add a minimap smoke pair that differs only in `Input.MinimapObjectives` and
asserts zero versus nonzero objective pins on a stage with a resolvable
objective. Retain the existing whole-minimap-disabled test and visually inspect
the player-facing settings placement.

## Related Work

- None.

## Resolution <!-- triage-2026-07-14 -->

Verified already fixed in commit `6c1a23d` (ledger Status was stale). The minimap-objectives help no longer claims 'no effect'; the setting is wired (default on) and drives objective pins (minimap.c:694).
