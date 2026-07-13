# AUDIT-0038: PortMaster Launcher Masks Game Boot Failures Behind Cleanup

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - failed handheld launches can finish with a misleading script status |
| Priority | P2 |
| Area | PortMaster launcher / failure handling |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | PortMaster launcher failures before or during `ge007` execution |

## Summary

The PortMaster script does not stop when its game directory is unavailable and
does not preserve the exit status of `./ge007`. It always calls `pm_finish` as
the final command, so the launcher's status reflects cleanup rather than the
game boot that the user requested.

## Evidence

[`pm-Goldeneye007.sh`](../../../pm-Goldeneye007.sh) has no `set -e` or explicit
failure checks. `cd "$GAMEDIR"` is unchecked; if it fails, the script continues
from an unintended directory. The script then runs:

```sh
./ge007 --rom "$GAMEDIR/baserom.u.z64"

pm_finish
```

It never stores the game status and has no explicit final `exit`. ShellCheck
also identifies the unchecked `cd` and unquoted initial control-file source.
The exact status returned by `pm_finish` is external, but by shell semantics it
unconditionally replaces the game's status as the script result.

## Reproduction

Run the launcher against a fake PortMaster control file with `ge007` replaced by
a helper that exits 23 and `pm_finish` by a helper that exits zero. The current
launcher exits zero. Repeat with a missing `GAMEDIR`; it continues past the
failed `cd` and attempts relative commands from the caller's directory.

## Root Cause

Cleanup is sequenced as an ordinary final command rather than a trap or status-
preserving epilogue, and required path transitions are not treated as fatal.

## Required End State

Validate and enter the game directory before launching. Always run PortMaster
cleanup, but capture the first boot failure and return it after cleanup. Quote
all path-bearing source and command operands and emit one clear error into the
launcher log.

## Acceptance Criteria

- A missing game directory or binary exits nonzero before running helpers that
  assume them.
- A game exit status of 23 remains 23 after `pm_finish` succeeds.
- Cleanup still runs after both successful and failed game execution.
- Control paths containing spaces are sourced correctly.
- The launcher log names the failed prerequisite or game status.
- A successful game and cleanup sequence exits zero.

## Verification Plan

Add a shell fixture supplying fake `control.txt`, `GPTOKEYB`, helper, game, and
`pm_finish` commands. Cover success, failed `cd`, missing binary, game failure,
cleanup failure, and space-containing paths, asserting order and final status.

## Related Work

- AUDIT-0033 covers the same launcher's ineffective save-directory argument.
