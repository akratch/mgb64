# AUDIT-0056: Unknown Config Preservation Silently Drops Forward-Compatible Data

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S4 - older builds can silently erase settings they do not recognize |
| Priority | P3 |
| Area | Configuration parser / forward compatibility |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | INI files with more than 128 unknown keys or unknown values over 384 bytes |

## Summary

The config system attempts to preserve unknown settings across load and save,
but its preservation store silently caps the count at 128 and each value at 384
bytes. Opening a configuration from a newer build in an older build and then
saving can truncate long future values and erase every unknown key beyond the
cap.

## Evidence

[`config_pc.c`](../../../src/platform/config_pc.c) defines
`CONFIG_MAX_UNKNOWN_SETTINGS=128` and `CONFIG_MAX_VALUE_LENGTH=384`.
`rememberUnknownEntry` silently returns when the count is full and uses
`snprintf` without diagnosing value truncation. The subsequent atomic full-file
rewrite only emits entries present in this bounded store.

The load line buffer is 2048 bytes and registered current string settings can
be up to 1023 bytes, demonstrating that a future unknown string can be valid at
lengths the preservation buffer cannot retain.

A natural current config does not need more than the cap; this is a rollback
and forward-compatibility data-loss path.

## Reproduction

Create an INI with 129 unknown keys and one 500-byte unknown value, load it, and
trigger any normal save. Compare before and after. The 129th key is absent and
the long value is cut to 384 bytes without a warning.

## Root Cause

Unknown entries were copied into a fixed auxiliary schema instead of preserving
their original parsed representation or surfacing capacity exhaustion.

## Required End State

Preserve unknown sections, keys, and values losslessly within the accepted INI
file limit, or refuse to rewrite when preservation cannot be guaranteed. Emit a
clear diagnostic rather than silently dropping data. Keep known-setting
canonicalization separate from unknown raw content.

## Acceptance Criteria

- At least 129 unknown entries round-trip without loss.
- Unknown values up to the accepted line/file limit round-trip exactly.
- Capacity or malformed-line limits produce a non-destructive save refusal.
- Duplicate unknown keys and section placement follow a documented policy.
- Known settings still serialize canonically with metadata comments.
- A newer-to-older-to-newer round trip retains all future settings.

## Verification Plan

Add byte-comparison fixtures at count and length boundaries, including unknown
sections, duplicate keys, comments, and a current 1023-byte-style path. Inject
allocation/capacity failure and verify the original file remains untouched.

## Related Work

- AUDIT-0039 and AUDIT-0049 cover atomicity of other preference stores.
