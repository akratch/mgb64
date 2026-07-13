# AUDIT-0048: Windows Diagnostics Export Leaks the Full ROM Path

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - support bundle violates its path-redaction privacy contract |
| Priority | P1 |
| Area | Diagnostics export / privacy |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Windows ROM paths using backslash separators |

## Summary

The diagnostics sysinfo writer promises to redact the ROM path to its filename,
but it searches only for `/`. A normal Windows path uses `\`, so the exported
`rom-file` line contains the entire path, commonly including the user's account
name and private directory structure.

## Evidence

[`ui_diag.cpp`](../../../src/app/ui_diag.cpp) contains the comment `path
redacted to filename`, then implements:

```cpp
const char *fn = std::strrchr(s.romPath, '/');
fn = fn ? fn + 1 : s.romPath;
```

No backslash search or platform filesystem filename operation follows. For
`C:\Users\Alice\Downloads\GoldenEye.z64`, `strrchr(..., '/')` returns null and
the complete string is written to `sysinfo.txt`.

The launcher already has a separate basename helper that recognizes both
separators, but diagnostics does not reuse it.

## Reproduction

Populate launcher state with a Windows-style ROM path, export diagnostics, and
read `sysinfo.txt`. The `rom-file` value is the full `C:\Users\...` path rather
than only `GoldenEye.z64`.

## Root Cause

Redaction used a POSIX-only separator assumption in cross-platform UI code and
has no privacy-focused fixture matrix.

## Required End State

Derive the filename with a cross-platform filesystem API or a shared basename
helper that handles both separator forms independent of build host. Apply an
explicit allowlist to sysinfo fields and test that no parent component or user
directory reaches the bundle.

## Acceptance Criteria

- POSIX and Windows paths export only the final filename.
- Mixed-separator and UNC path fixtures do not expose parent directories.
- Empty and filename-only inputs remain well formed.
- Sysinfo contains no full ROM path elsewhere.
- A privacy regression test scans the entire exported bundle for seeded private
  path components.
- Existing region, byte-order, size, and validity metadata remain present.

## Verification Plan

Add export fixtures for POSIX, drive-letter, UNC, mixed-separator, filename-only,
and empty paths. Seed unique account/directory tokens, export the full bundle,
and assert none occur while the expected filename and ROM metadata do.

## Related Work

- AUDIT-0047 covers missing configuration and error reporting in the exporter.
