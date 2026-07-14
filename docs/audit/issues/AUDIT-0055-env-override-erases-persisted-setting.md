# AUDIT-0055: Transient Environment Override Erases the Persisted Setting

| Field | Value |
| --- | --- |
| Status | Fixed |
| Severity | S3 - a one-run override permanently discards the user's saved value |
| Priority | P1 |
| Area | Configuration persistence / transient overrides |
| Evidence level | Runtime reproduced |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Plain sessions with a `GE007_*` override followed by any config save |

## Summary

Environment overrides are intended to affect one launch without changing the
user's saved configuration. On the next full-file save, the writer omits every
environment-overridden key. Because it rewrites the entire INI, omission deletes
the prior persisted value; the following launch falls back to the compiled
default.

## Evidence

[`settings.c`](../../../src/platform/settings.c) applies an environment value
directly to the live setting and marks its source `SETTING_OVERRIDE_ENV`.
[`config_pc.c`](../../../src/platform/config_pc.c) sees that source in
`saveEntry` and returns without writing the key. Its comment says omission keeps
the user's own value, but the save operation creates a complete temporary file
and atomically replaces the original, so omitted keys do not survive.

[`platform_sdl.c`](../../../src/platform/platform_sdl.c) saves configuration on
clean shutdown. Direct UI or CLI changes can trigger the same rewrite during
the session.

A ROM-free sequence saved `Video.FovY=70`, then launched with
`GE007_FOV_Y=60` while persisting an unrelated setting. Afterward the FovY key
was absent; the next launch reported the default 50.

## Reproduction

```sh
ge007 --savedir "$tmp" --config-set Video.FovY=70
GE007_FOV_Y=60 ge007 --savedir "$tmp" \
  --config-set Audio.MasterVolume=0.9
grep FovY "$tmp/ge007.ini"
ge007 --savedir "$tmp" --dump-config | grep FovY
```

The grep of the file finds no key and the final dump reports 50, not 70.

## Root Cause

The configuration model stores only the current live value plus override-source
metadata. It has no shadow of the loaded persistent value to serialize when a
transient layer is active.

## Required End State

Separate persisted configuration from effective launch values. Full-file saves
must serialize the user's last durable value for transiently overridden keys,
while persisting explicit UI or `--config-set` changes according to a clear
precedence policy. Removing an environment variable must reveal the exact prior
saved value.

## Acceptance Criteria

- A one-run environment override never deletes or changes the prior saved key.
- Saving an unrelated setting while an override is active preserves that key.
- Removing the environment override restores the exact durable value.
- A user edit to an actively overridden key has defined, visible persistence
  behavior and is not silently omitted.
- CLI `--config-override` and launch presets retain their documented transient
  semantics.
- String, enum, integer, unsigned, and float settings share the policy.

## Verification Plan

Add a two-launch persistence matrix for every setting type: seed durable value,
apply transient env value, save unrelated and same-key edits, inspect the file,
then relaunch without the environment. Repeat for clean-shutdown and UI-save
paths and assert exact values and override-source diagnostics.

## Related Work

- AUDIT-0036 requires the UI to distinguish durable save outcomes.

## Resolution

Fixed on `feat/webgpu-backend` by giving each config key a **persisted-value
shadow** so a full-file save never has to omit (and thereby delete) an
env-overridden key.

- `src/platform/config_pc.c`:
  - `ConfigEntry` gains a `persisted[CONFIG_PERSIST_MAX=1024]` shadow + a
    `has_persisted` flag. The per-type value serialization is factored out of
    `saveEntry` into `formatEntryValue()` so the shadow is byte-identical to a
    normal save of the same value.
  - `configInit()` captures the shadow for every registered entry **after** load
    (or after writing defaults) and **before** any caller applies a transient
    override — the durable on-disk value.
  - `saveEntry()` now serializes the **shadow** (not the live value) for keys
    whose `Setting.override_source == SETTING_OVERRIDE_ENV`, instead of omitting
    them. The user's saved value survives the atomic rewrite untouched; removing
    the env var reveals it exactly.
  - `configSetValue()` — the single mutation choke point — calls
    `settingsNoteDurableEdit()` only when the value was actually **applied**
    (via `setFromStringEx`'s new `applied` out-param), so an explicit UI /
    `--config-set` edit to an env-overridden key drops the ENV marking and
    persists its new value (not the shadow, not the env value). A *rejected*
    value (a NaN float, an unrecognized enum token — which keep the previous
    value) is not a durable edit and must not clear the marking, or it would
    persist the live env value instead of the shadow.
- `src/platform/settings.c` / `.h`: `settingsGetOverrideSource`,
  `settingsSetOverrideSource`, `settingsNoteDurableEdit`; and
  `settingsResetAllToDefaults()` now clears every override source (a
  `--reset-config` persists defaults, not the pre-reset shadow).
- `src/platform/config_schema.c`: the staging **live-preview** snapshots the
  override source on preview-on and restores it on preview-off, so a
  previewed-then-discarded env key does not leak its env value on a later save.

CLI `--config-override` keeps `SETTING_OVERRIDE_CLI` and is still saved via the
live path (config-pinning, unchanged); `--faithful`/`--remaster` remain
save-suppressed. All five setting types share the policy.

**Verification:**
- New ROM-free unit test `tests/test_config_env_shadow.c` (ctest
  `config_env_shadow`): a two-launch persistence matrix over int/uint/float/enum/
  string, the staging live-preview containment, a durable-edit-under-env case, a
  rejected-edit-under-env case (NaN float / unknown enum token must keep the
  shadow), and a `settingsResetAllToDefaults`-under-env case (reset persists the
  default, not the shadow), all driven through the real `configInit →
  settingsApplyEnvOverrides → configSetValue → configSave → reload` lifecycle
  against a temp savedir. Failed (11 assertions) before the fix, passes after.
- The diff was reviewed by an adversarial multi-lens agent pass; the two
  confirmed findings (a rejected `--config-set` clearing the ENV marking, and the
  reset-clears-overrides line lacking coverage) are the `applied`-guard and the
  reset-under-env test above.
- `tools/config_roundtrip_check.py` gains
  `assert_env_override_preserves_persisted()` (the report's exact two-launch
  scenario for a float, an int and an enum through the real binary) and is now a
  registered self-skipping ctest `port_config_roundtrip`.
- Full `ctest` green (87/87 in build-webgpu); both configs build clean; 7/7
  fidelity tapes byte-exact (sim-neutral).
