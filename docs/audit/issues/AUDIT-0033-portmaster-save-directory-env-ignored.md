# AUDIT-0033: PortMaster Save Directory Export Is Ignored by the Shipped Target

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - handheld saves and configuration miss the designated persistent directory |
| Priority | P1 |
| Area | PortMaster launcher / save persistence |
| Evidence level | Runtime reproduced |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | PortMaster's engine-only `MGB64_APP=OFF` build |

## Summary

The PortMaster launcher creates `conf/` and exports it as
`MGB64_APP_SAVEDIR`, but the engine-only executable shipped for PortMaster does
not read that variable. It therefore selects the writable game directory and
writes `ge007.ini` and EEPROM data beside the binary, outside the directory
intended to survive packaging and updates.

## Evidence

[`pm-Goldeneye007.sh`](../../../pm-Goldeneye007.sh) creates `CONFDIR`, exports
`MGB64_APP_SAVEDIR="$CONFDIR"`, and launches `./ge007` with only `--rom`.
[`portmaster_build_check.sh`](../../../tools/portmaster_build_check.sh) documents
and builds the authoritative PortMaster configuration with `MGB64_APP=OFF`.

[`main_pc.c`](../../../src/platform/main_pc.c) accepts save-directory selection
only through `--savedir`; it never reads `MGB64_APP_SAVEDIR`. That variable is
consumed only in [`main_app.cpp`](../../../src/app/main_app.cpp), which is absent
from the engine-only target.

A ROM-free reproduction with `MGB64_PORTMASTER=1`, a requested
`MGB64_APP_SAVEDIR=/tmp/.../conf`, and `--list-settings` printed
`[SAVEDIR] Using CWD` and created `ge007.ini` in the working directory. The
requested `conf/` directory remained empty.

## Reproduction

```sh
tmp=$(mktemp -d)
mkdir -p "$tmp/conf" "$tmp/home"
cd "$tmp"
HOME="$tmp/home" MGB64_PORTMASTER=1 MGB64_APP_SAVEDIR="$tmp/conf" \
  /path/to/ge007 --list-settings
find "$tmp" -maxdepth 2 -type f
```

The config appears at `$tmp/ge007.ini`, not `$tmp/conf/ge007.ini`.

## Root Cause

The shell was written against the app-shell environment contract, while the
actual PortMaster build deliberately excludes the app shell. No test asserts
that the launcher's persistence argument reaches `savedirInit`.

## Required End State

Pass the PortMaster persistence directory through the engine interface it
actually supports, preferably `--savedir "$CONFDIR"`, or add one documented
environment contract shared by both executable configurations. All handheld
configuration, EEPROM, and binding files must live in the persistent directory.

## Acceptance Criteria

- A fresh PortMaster launch creates `ge007.ini` and `ge007_eeprom.bin` under
  `conf/`, never beside the executable.
- Existing save files in the designated PortMaster directory continue loading.
- Paths containing spaces are passed without word splitting.
- Engine-only and any future app-shell handheld builds apply the same location.
- A package/update simulation preserves the save directory and progress.

## Verification Plan

Add a ROM-free launcher contract test with a fake PortMaster control file and a
test executable that records argv. Follow with a real engine `--list-settings`
check and a ROM-backed EEPROM persistence run across a simulated game-directory
replacement.

## Related Work

- AUDIT-0034 covers a separate app-shell initialization bug that also defeats
  later save-directory overrides.
