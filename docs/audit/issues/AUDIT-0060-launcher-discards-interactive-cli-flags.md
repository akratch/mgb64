# AUDIT-0060: Launcher Discards Interactive CLI Launch Flags

| Field | Value |
| --- | --- |
| Status | Fixed (parser + launcher seed wired; automated parse/reject + pure seed coverage green; interactive Play-click screenshot smoke owner-verifiable) |
| Severity | S3 - documented direct-launch choices are silently ignored by the default build |
| Priority | P1 |
| Area | App shell / command-line handoff |
| Evidence level | Runtime and UI reproduced |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Default `MGB64_APP=ON` invocation without an automation flag or `--no-ui` |

## Summary

Argument triage intentionally sends interactive flags such as `--rom`,
`--level`, `--difficulty`, presets, multiplayer, and `--savedir` to the launcher
so it can preselect them. No code parses those arguments into launcher state.
They are silently discarded, and remembered/default UI choices win instead.
The documented direct-launch commands therefore do not do what they request on
the default shipping build.

## Evidence

[`arg_triage.cpp`](../../../src/app/arg_triage.cpp) explicitly excludes
interactive direct-play flags from the automation list and comments that the
launcher can preselect them. Its test asserts that `--rom`, `--level`,
`--faithful`, and `--remaster --rom` all route to the launcher.

[`main_app.cpp`](../../../src/app/main_app.cpp) creates `Launcher launcher`
without passing `argc` or `argv`. [`ui_rom.cpp`](../../../src/app/ui_rom.cpp),
[`ui_launch.cpp`](../../../src/app/ui_launch.cpp), and
[`ui_modes.cpp`](../../../src/app/ui_modes.cpp) initialize only from AppConfig,
scanning, and defaults. There is no interactive argument parser elsewhere.

README and BUILDING examples invoke the default `build/ge007` with these flags.
An isolated UI capture passed a Downloads ROM, Dam, and Remaster. The launcher
instead displayed a different remembered ROM and `Will boot: Boot to menu`,
proving that the supplied ROM and level were ignored.

## Reproduction

```sh
build/ge007 --rom /outside/scan/valid.z64 --level dam --remaster
```

Observe the launcher fields. They reflect saved/default state rather than the
command. Add `--no-ui` and the same engine options take effect, showing the
split. A positional ROM path from file association is ignored by the same path.

## Root Cause

Routing policy landed before the promised launcher preselection parser. The
test covers only which top-level path is chosen, not whether arguments survive
that choice.

## Required End State

Parse supported interactive arguments once into a validated launch-intent
structure and seed ROM, save directory, level/mission, difficulty, visual
preset, multiplayer options, and related fields before the first launcher draw.
Show the resolved intent for confirmation and reject unknown/conflicting values
instead of silently falling back. Preserve `--no-ui` direct boot.

## Acceptance Criteria

- Every documented interactive flag appears accurately in launcher state.
- Explicit CLI values override remembered launcher preferences for that run.
- ROM and savedir paths outside scan/default locations are honored exactly.
- Positional/file-association ROM opening selects and validates that file.
- Invalid or conflicting arguments produce an actionable error and nonzero
  status or a clearly blocked launcher state.
- Automation and `--no-ui` keep their current engine-path behavior.
- End-to-end tests assert resolved boot argv, not only triage classification.

## Verification Plan

Add table-driven launch-intent parsing tests and app-smoke screenshots for ROM,
level/mission, difficulty, each preset, multiplayer players/stage/scenario,
savedir, positional ROM, invalid values, and remembered-state precedence. Use
a bridge spy to assert the exact argv passed to `mgb64_headless_main` after Play.

## Resolution

Two-phase fix under HARNESS_STRATEGY §8:

- **e97662a (Phase B)** landed the pure, presence-tracked `parseLaunchIntent`
  (`src/app/launch_intent.{h,cpp}`) plus the single-sourced CLI stage/difficulty
  tables (`src/app/cli_stage_tables.{h,c}`), with resolution/validation parity
  against the headless `src/platform/main_pc.c`. ctest `launch_intent` covers it.

- **This change (E1)** wires the parser into the launcher:
  - `src/app/main_app.cpp` now calls `parseLaunchIntent` on the interactive path
    (after `arg_triage` routes automation/`--no-ui` to the unchanged headless
    engine) and, on any invalid flag or unusable ROM, prints an actionable
    stderr message naming the offending argument and exits nonzero **before any
    window/device is created** — no UI flash.
  - New `Launcher::seed` (`src/app/ui_launcher.cpp`) runs the panel `*_ensureInit`
    loaders first (so absent fields keep remembered prefs), then overrides only
    the CLI-present fields via the pure `applyLaunchIntent`
    (`src/app/launch_seed.cpp`), setting each panel's `*Initialized` flag so the
    lazy draw-time init cannot clobber the CLI value. A CLI `--rom` is validated
    with `mgb_validate_rom` and honored exactly (scan/default-independent).
  - `LauncherState` gained a `savedir` field; `fillBoot` forwards it into
    `MgbBootConfig.save_dir`, which `mgb64_engine_boot` already synthesizes as
    `--savedir` at Play — closing the savedir plumbing gap.
  - `ui_launch.cpp` difficulty model extended to 0..3 (adds `007`) so a CLI
    `--difficulty 007` round-trips instead of reading out of bounds.

Seeded-field → MgbBootConfig trace: `rom_path`→romPath→`rom_path`;
`level.mission`→launchLevelIndex→`level_slug`; `difficulty`→launchDifficulty→
`difficulty`; `preset`→modePreset→`preset`; `multiplayer`/`players`→
launchMultiplayer/launchPlayers→`multiplayer`/`players`; `savedir`→savedir→
`save_dir`.

Coverage: pure ctest `launcher_seed` (per-field override-only-when-present +
flag discipline); `launch_intent` + `arg_triage` remain green; process-level
headless checks confirm unknown flag / unknown level / bad difficulty /
unmodeled flag / unusable ROM each exit nonzero with an arg-naming message and
no window, while a valid parse proceeds into the UI (smoke exit 0) and the
no-arg path is unchanged. The end-to-end Play-click boot-argv screenshot smoke
(Verification Plan) needs a display and is owner-verifiable.

## Related Work

- AUDIT-0034 covers app-shell save-directory ordering after an override is
  successfully conveyed.
- AUDIT-0045 covers launcher ROM validation after a path is selected.
