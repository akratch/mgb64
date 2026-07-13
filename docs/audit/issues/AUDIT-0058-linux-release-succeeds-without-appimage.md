# AUDIT-0058: Linux Release Job Succeeds Without Producing an AppImage

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - a promised primary Linux artifact can be absent from a green release job |
| Priority | P1 |
| Area | Linux packaging / release gate |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Missing or failing `appimagetool` in the Linux release job |

## Summary

The Linux release workflow is named and documented as producing AppImage plus
tar.gz, but AppImage creation is best effort. A missing tool, failed download,
or failed execution only prints a warning. The packaging script exits zero and
the workflow uploads the tarball under a green combined artifact name.

## Evidence

[`package_linux_appimage.sh`](../../../scripts/package_linux_appimage.sh)
always creates the tarball first. If `appimagetool` is unavailable it prints a
warning; if execution fails, an `|| echo` also converts failure into success.
The final `rm -rf` leaves the script with status zero.

[`release.yml`](../../../.github/workflows/release.yml) labels the job
`Linux app (AppImage + tar.gz)`, calls the script without a strict mode, and
uploads `dist/mgb64-linux-*` without asserting that both expected files exist.
[`RELEASING.md`](../../RELEASING.md) likewise describes both outputs.

## Reproduction

Run the packager with no `appimagetool` or `wget` available, or substitute a
tool that exits nonzero. The script warns that only the tarball was produced
but returns zero. The workflow artifact glob still matches the tarball.

## Root Cause

A useful local tar fallback and the formal release contract share one execution
mode. Best-effort behavior appropriate for development is therefore inherited
by the release gate.

## Required End State

Make required artifacts explicit. The official Linux release job must fail
unless a validated AppImage and tarball both exist. Retain a clearly named
local `--allow-tar-only` mode if useful, but never enable it in release CI or
publication.

## Acceptance Criteria

- Missing, download-failed, or execution-failed AppImage tooling makes release
  packaging nonzero.
- The workflow asserts both expected artifact paths and nonzero sizes.
- AppImage runtime validation runs before upload.
- Tar-only local mode is explicit in command and output naming.
- Documentation matches strict versus fallback behavior.
- Publication rejects an incomplete platform artifact set.

## Verification Plan

Add packager fixtures for preinstalled success, verified-download success,
missing tool, download failure, tool failure, and corrupt/empty output. Run the
workflow logic against each and assert final status plus exact artifact set.

## Related Work

- AUDIT-0037 requires the downloaded AppImage tool to be immutable and verified.
- AUDIT-0052 requires complete platform artifacts in the release manifest.
