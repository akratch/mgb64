# AUDIT-0011: Malformed Numeric Settings Are Silently Accepted and Coerced

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S4 - user configuration is silently changed to unintended values |
| Priority | P2 |
| Area | Configuration / command-line validation |
| Evidence level | Runtime reproduced and source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | INI, `GE007_*` environment, preset, app staging, `--config-set`, and `--config-override` numeric values |

## Summary

The shared configuration parser does not require a numeric string to contain a
valid, finite, fully consumed value. Completely nonnumeric text becomes zero,
valid prefixes ignore arbitrary suffixes, infinity is clamped to a range edge,
and conversion overflow is not checked. The setter still returns success, so
CLI callers mark the value as an active override and exit zero without warning.

The result can silently disable a feature, force a setting to its minimum or
maximum, or persist the unintended value when `--config-set` is used.

## Evidence

[`setFromString`](../../../src/platform/config_pc.c#L179) calls `strtol`,
`strtof`, and `strtoul` with a NULL end pointer for integer, float, and unsigned
settings at lines 186, 191, and 209. It therefore cannot distinguish no parse,
a valid prefix followed by junk, or a fully valid value. It does not reset or
inspect `errno` for range errors.

The float path rejects only NaN by testing `v != v`. Positive and negative
infinity pass that test and are clamped to a finite endpoint. The integer path
casts the potentially wider `long` result to `s32` before clamping. The
unsigned path accepts a leading minus sign through `strtoul` and then clamps the
wrapped value.

[`configSetValue`](../../../src/platform/config_pc.c#L624) returns the parser's
success result. Recognized numeric keys return one even when conversion failed.
[`pcApplyConfigArg`](../../../src/platform/main_pc.c#L212) consequently treats
the malformed value as valid and records it as a CLI override.

Runtime controls against the current native binary produced these results:

| Override | Effective dump value | Exit | Diagnostic |
| --- | --- | --- | --- |
| `Video.FovY=garbage` | `FovY=45` | 0 | none |
| `Video.FpsOverlay=enabled` | `FpsOverlay=0` | 0 | none |
| `Video.FovY=72degrees` | `FovY=72` | 0 | none |
| `Video.FpsOverlay=1junk` | `FpsOverlay=1` | 0 | none |
| `Video.FovY=inf` | `FovY=105` | 0 | none |

The run used `--faithful --dump-config` so the initial values were known and the
test could not persist changes to `ge007.ini`.

## Reproduction

No ROM is required:

```sh
build/ge007 --faithful \
  --config-override Video.FovY=garbage \
  --config-override Video.FpsOverlay=enabled \
  --dump-config
```

The command exits zero, prints `FovY=45` and `FpsOverlay=0`, labels both as
active CLI overrides, and emits no invalid-value warning. Replace the values
with `72degrees`, `1junk`, or `inf` to reproduce the other cases above.

## Root Cause

Key recognition and value validation share a boolean return that currently
means only "the key exists." The scalar conversion branches omit the normal
`end == input`, trailing-character, `errno == ERANGE`, and finiteness checks.
The enum branch has stronger validation, but even its numeric fallback accepts
a valid numeric prefix followed by junk because it does not require `*end` to
be the string terminator.

## Required End State

Parse every numeric setting with a checked conversion helper that:

- resets `errno` before conversion and rejects `ERANGE`;
- requires at least one consumed character;
- permits surrounding whitespace but requires no other trailing characters;
- rejects NaN and both infinities for float settings;
- checks the destination type's bounds before narrowing;
- rejects a leading minus sign for unsigned settings; and
- leaves the prior value and override source unchanged on rejection.

Return distinct outcomes for unknown key, invalid value, and accepted value so
the CLI can print an accurate diagnostic. `--config-override` and
`--config-set` must exit nonzero for an invalid value. INI and environment
loads may continue after a rate-limited warning while preserving the previous
value.

## Acceptance Criteria

- Valid decimal, signed, hexadecimal where currently supported, and enum-token
  values retain their current behavior.
- Empty, whitespace-only, alphabetic, mixed-suffix, overflow, underflow, NaN,
  infinity, and negative-unsigned inputs are rejected.
- Rejected values never mutate the destination or its recorded override source.
- `--config-override` and `--config-set` diagnose the key and bad value and exit
  nonzero.
- Malformed INI and environment values warn once and leave the compiled or
  previously accepted value intact.
- Parsing uses locale-stable syntax or explicitly documents and tests the
  locale contract.
- Existing range clamping for syntactically valid finite values remains intact.

## Verification Plan

Add a ROM-free table-driven unit test that registers one setting of every type
and drives `configSetValue` with valid boundaries plus each malformed class.
Exercise the CLI wrapper separately to assert exit codes, diagnostics, and
override-source state. Add temporary savedir fixtures for malformed INI input
and environment cases, then verify that `--dump-config` reports the unchanged
values and that `--config-set` never persists rejected text or a coerced value.

## Related Work

- The existing [`test_config_staging.c`](../../../tests/test_config_staging.c)
  covers valid values and range clamping but no malformed strings.
- The enum branch already rejects unknown tokens and unmatched numeric values;
  it should use the same full-consumption rule as the scalar branches.
