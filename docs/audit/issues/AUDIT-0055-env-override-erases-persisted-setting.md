# AUDIT-0055: Transient Environment Override Erases the Persisted Setting

| Field | Value |
| --- | --- |
| Status | Open |
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
