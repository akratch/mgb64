# AUDIT-0008: Vertical-Only Auto-Aim Uses Uninitialized Horizontal State

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S3 - undefined target scoring in a supported debug configuration |
| Priority | P3 |
| Area | Gameplay / targeting / auto-aim |
| Evidence level | Analyzer and source proven; runtime symptom not yet reproduced |
| Confidence | High |
| Origin | Newly confirmed on 2026-07-12 |
| Affected configurations | Vertical auto-aim enabled while horizontal auto-aim is disabled |

## Summary

The auto-aim target scorer leaves two horizontal values undefined when
horizontal auto-aim is off: the horizontal output coordinate and a width-based
score divisor. Its caller still copies the horizontal output, and the scorer can
use the divisor when the manual aim position is inside a target but the screen
center is outside it. Both operations read indeterminate automatic values.

Normal option initialization sets horizontal and vertical auto-aim together,
so this is not claimed as a default gameplay failure. The compiled debug menu
can toggle the two axes independently, making the configuration expressible in
the current program and relevant to fidelity/debug work.

## Evidence

[`sub_GAME_7F03D188`](../../../src/game/chrprop.c#L6532) declares `sp48` without
initializing it. It assigns `sp48` only inside the horizontal-auto-aim block at
lines 6585-6601. When horizontal auto-aim is off, target eligibility instead
uses the current bullet angle at line 6604.

After that shared gate:

- `arg4[1]` is always written at line 6629.
- `arg4[0]` is written only if horizontal auto-aim is enabled at lines
  6631-6645.
- The result calculation at lines 6647-6658 can divide by `sp48` even when the
  horizontal-auto-aim block did not initialize it.

[`chrpropUpdateAutoaimTarget`](../../../src/game/chrprop.c#L6676) passes
`&sp6C.x` as the two-component output and, after a candidate wins, copies both
`sp6C.x` and `sp6C.y` at lines 6732-6734. `sp6C.x` is indeterminate in the
vertical-only configuration. Clang reports the undefined assignment at line
6733.

The two axis setters are independently exposed by the compiled debug menu in
[`debugmenu_handler.c`](../../../src/game/debugmenu_handler.c#L750). Ordinary
player setup currently writes both axes from the same option in
[`bondview.c`](../../../src/game/bondview.c#L15890).

## Reproduction

1. Set horizontal auto-aim to 0 and vertical auto-aim to 1 through the debug
   controls or a focused harness.
2. Present an armed target that passes the on-screen and line-of-sight gates.
3. Place the manual bullet-angle X inside the target while the screen center is
   outside the target's horizontal bounds.
4. Call `chrpropUpdateAutoaimTarget` under MemorySanitizer.

The scorer reads uninitialized `sp48`; once a candidate wins, the caller also
reads uninitialized `sp6C.x`. A simpler static-analysis reproduction reaches the
second read without requiring a complete scene fixture.

## Root Cause

Horizontal and vertical aim were implemented as separable flags, but the score
and output structures are only partially populated when one axis is disabled.
The caller then treats the two-component output as fully initialized. Control
flow later avoids applying X auto-aim, but that does not legalize earlier reads
of the unused X value.

## Required End State

Define the vertical-only scoring contract explicitly. Initialize both output
coordinates before candidate evaluation and avoid any use of the horizontal
width divisor when horizontal auto-aim is disabled. A vertical-only candidate
must be ranked using defined vertical/manual-aim criteria, not a stack value.

The minimal C hardening must be checked against the US retail assembly before
changing target selection. If retail gives the disabled axis a concrete score,
reproduce it. If the retail path is genuinely undefined, select a stable native
policy and document it. Preserve candidate iteration order, line-of-sight calls,
target tie behavior, and RNG use.

## Acceptance Criteria

- All four axis combinations (off/off, X-only, Y-only, X+Y) execute without an
  uninitialized read under MemorySanitizer and static analysis.
- Vertical-only scoring is deterministic across stack fill patterns and
  optimization levels.
- Y-only mode never updates the X auto-aim target/time.
- X-only and X+Y target selection match retail or the existing defined path for
  fixed fixtures.
- Default player option behavior and simulation hashes are unchanged.
- No extra RNG call or candidate-list reorder is introduced.

## Verification Plan

Build a focused target fixture with controlled projected bounds, bullet angle,
screen center, and unobstructed STAN result. Test center-inside and center-outside
cases for all axis combinations. Record selected target and normalized X/Y
values, then compare the defined configurations with the retail US oracle.

## Related Work

This is not the same as renderer visibility influencing target eligibility.
That broader parity behavior belongs in the fidelity ledger; this report is the
local native undefined state after a candidate has entered scoring.

## Resolution

Fixed on `feat/webgpu-backend`. In `sub_GAME_7F03D188` (src/game/chrprop.c) two
horizontal-only values were populated only under the `autoaim_x` branch: the
width-based score divisor `sp48` and the horizontal output coordinate `arg4[0]`.
With `autoaim_x` OFF but `autoaim_y` ON, the shared eligibility gate could pass,
then the returned score divided by an **uninitialized** `sp48` (indeterminate
candidate ranking) and the caller copied an uninitialized `arg4[0]`.

- The width divisor is now computed **unconditionally** before the `autoaim_x`
  branch via the new pure helper `autoaimTargetDivisor` (src/platform/autoaim_score.c).
  In every `autoaim_x`-on path this is the exact same value the inner block used
  (`sp4c` can only become TRUE there, after the assignment), so X-only / X+Y
  selection is unchanged; the vertical-only path now gets a deterministic native
  policy (the same divisor) in place of retail's undefined stack read.
- `arg4[0]` is written **unconditionally** (same clamp retail applies on the
  `autoaim_x`-on path). In the vertical-only path the value is provably dead: the
  caller `chrpropUpdateAutoaimTarget` guards the X auto-aim update on `autoaim_x`,
  and the copied `sp9C.x` is read only at a dead `if (sp9C.x > 1.0f);` empty
  statement and inside an `if (autoaim_x)` block — neither reachable/observable in
  the Y-only config — so defining it is sim-neutral and removes the UB read.
- The three-branch score is routed through the byte-faithful `autoaimTargetScore`
  (operand order preserved; `center` is a pure getter expression identical across
  evaluations).

Guarded by the ROM-free ctest `autoaim_score` (tests/test_autoaim_score.c) over
the extracted divisor + three-branch score. **Byte-identity of the reachable
(`autoaim_x`-on) paths is proven empirically: all 7 input tapes — including the
combat tapes `dam_combat_guard6` and `dam_ak47_sustained` that exercise auto-aim
during play — replay byte-exact with hashes unchanged.** The vertical-only
selection cannot be byte-matched to retail (retail divides by an uninitialized
stack slot there, so there is no defined oracle); the documented native policy is
sanctioned by the Required End State.
