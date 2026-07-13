# AUDIT-0054: Invalid Explicit Save Directory Is Accepted as Usable

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - a session can start while configuration and progress cannot persist |
| Priority | P1 |
| Area | Save-directory initialization / startup |
| Evidence level | Fault injected |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Invalid, uncreatable, or unwritable `--savedir` paths |

## Summary

An explicit save-directory override is accepted and logged even when the
directory cannot be created. Initialization returns no status, config creation
failure is ignored, and ROM-free commands still exit zero. A full game session
can proceed with no durable settings or EEPROM target despite the authoritative
`Using override` message.

## Evidence

[`savedir.c`](../../../src/platform/savedir.c) copies an explicit override,
calls `ensure_dir(s_saveDir)`, ignores its return value, prints
`[SAVEDIR] Using override`, and returns. The API itself returns void and performs
no write probe for POSIX overrides.

[`config_pc.c`](../../../src/platform/config_pc.c) calls `configSave` when no
config exists but ignores its result during `configInit`.
[`main_pc.c`](../../../src/platform/main_pc.c) therefore has no failed savedir
status to propagate before continuing.

Fault injection used `--savedir /dev/null/mgb64-save --list-settings`. The
process printed:

```text
[SAVEDIR] Using override: /dev/null/mgb64-save/
[CONFIG] Failed to save .../ge007.ini.tmp: Not a directory
```

It then listed settings and exited zero.

## Reproduction

```sh
ge007 --savedir /dev/null/mgb64-save --list-settings
echo "$?"
```

Use an unwritable existing directory for the same persistence failure after a
successful directory existence check.

## Root Cause

Save-directory selection is a best-effort void singleton, while callers assume
that selecting a path also validated it. The explicit user contract is not
distinguished from fallback probing.

## Required End State

Return a validated save-directory result before any configuration or gameplay
initialization. Explicit overrides must be creatable directories with a real
write/replace probe; failure must stop startup or require an explicit temporary
no-save choice. Never print `Using` until validation succeeds.

## Acceptance Criteria

- Non-directory, missing-parent, unwritable, overlong, and failed-replace paths
  are rejected with nonzero process status.
- A valid new override is created and accepts atomic config and EEPROM writes.
- No gameplay starts after an invalid explicit override without informed user
  consent to a no-save session.
- The log distinguishes requested, validated, fallback, and failed paths.
- App-shell callers receive the same result contract.
- Valid portable and per-user automatic selection remains unchanged.

## Verification Plan

Add startup tests for valid existing/new directories, regular files, denied
directories, missing parents, path-length limits, and injected write/replace
failure. Assert process status, logs, created files, and that a ROM-backed game
loop is never entered after rejected explicit selection.

## Related Work

- AUDIT-0036 covers settings UI feedback after a save attempt.
- AUDIT-0033 and AUDIT-0034 cover overrides that fail to reach this initializer.
