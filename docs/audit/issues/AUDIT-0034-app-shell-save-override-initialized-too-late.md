# AUDIT-0034: App-Shell Save Overrides Arrive After the Save Directory Is Frozen

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S3 - promised automation isolation and embedded save routing are ineffective |
| Priority | P1 |
| Area | App shell / embedded engine initialization |
| Evidence level | Runtime reproduced |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | App autoplay or embedded boots supplying `MgbBootConfig.save_dir` |

## Resolution

Fixed 2026-07-13. `mgb_config_init()` (via config_schema.c) calls `savedirInit(NULL)`, freezing the save-dir singleton to the engine default before the app shell's autoplay/PortMaster branches provide MGB64_APP_SAVEDIR — so the later engine-boot `savedirInit(override)` was a no-op (s_initialized already set). Fix: `main_app.cpp` now seeds `savedirInit(getenv("MGB64_APP_SAVEDIR"))` BEFORE `mgb_config_init()`; a NULL/unset env keeps normal auto-selection, and an unusable override fails fast (returns non-zero -> abort, via the AUDIT-0054 status contract). savedirInit is idempotent, so the subsequent engine-boot call with the same dir is a consistent no-op. (The bare engine also honors the env as a --savedir fallback — see AUDIT-0033.)

## Summary

The app shell initializes the engine configuration before it evaluates autoplay
or handheld boot settings. That first call freezes the process-global save
directory with no override. The later embedded engine boot synthesizes
`--savedir`, but `savedirInit` immediately returns because it was already
initialized, so the requested isolation directory is ignored.

## Evidence

[`main_app.cpp`](../../../src/app/main_app.cpp) calls `mgb_config_init()` before
the autoplay and PortMaster branches. [`config_schema.c`](../../../src/platform/config_schema.c)
implements that call by invoking `savedirInit(NULL)`. The autoplay branch later
copies `MGB64_APP_SAVEDIR` into `MgbBootConfig.save_dir`, and
[`main_pc.c`](../../../src/platform/main_pc.c) correctly converts it to
`--savedir` for `mgb64_headless_main`.

[`savedir.c`](../../../src/platform/savedir.c) starts `savedirInit` with
`if (s_initialized) return;`. The late non-null override therefore cannot take
effect.

In a scratch-directory autoplay run requesting `/tmp/.../save`, the log printed
`[SAVEDIR] Using CWD`, created `ge007.ini` in the scratch root, and left the
requested save directory empty.

## Reproduction

Run the app build from an empty scratch directory and HOME:

```sh
HOME="$tmp/home" MGB64_APP_AUTOPLAY=1 \
MGB64_APP_SAVEDIR="$tmp/save" /path/to/ge007
```

An absent ROM is sufficient: configuration initializes before ROM failure.
Compare the location of `ge007.ini` with `$tmp/save` and inspect the SAVEDIR log.

## Root Cause

The app wants settings available for launcher rendering, but save-directory
selection is a one-shot singleton shared with the later engine boot. Boot
configuration is applied after the singleton's irreversible initialization.

## Required End State

Resolve the embedded boot's save directory before the first configuration or
save access. Either initialize the shared savedir with the final override up
front or introduce a safe pre-I/O reconfiguration contract. Never migrate the
directory after files have already been read or written in the same process.

## Acceptance Criteria

- Autoplay puts configuration, EEPROM, and bindings only under the requested
  save directory.
- No config file is created in the launch working directory first.
- An embedded boot without an override retains the normal per-platform policy.
- Repeated idempotent initialization with the same directory is accepted.
- A conflicting late directory fails explicitly instead of being ignored.
- App-shell configuration enumeration still works before interactive launch.

## Verification Plan

Add an integration test that launches from one directory with a distinct
`MGB64_APP_SAVEDIR`, forces an early engine failure, and checks every created
file. Repeat with a valid short boot and confirm EEPROM and bindings use the
override, then cover no override and conflicting double initialization.

## Related Work

- AUDIT-0033 covers the engine-only PortMaster script's unsupported environment
  variable.
