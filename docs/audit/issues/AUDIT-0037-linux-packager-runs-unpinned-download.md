# AUDIT-0037: Linux Release Packaging Executes an Unpinned Mutable Download

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S1 - mutable build-time executable can compromise release artifacts |
| Priority | P1 |
| Area | Release supply chain / Linux AppImage packaging |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Linux release jobs without a preinstalled `appimagetool` |

## Summary

The Linux packager downloads `appimagetool` from a mutable `continuous` release
URL, marks it executable, and runs it without checking a version, digest, or
signature. The official release workflow follows this path. A changed or
compromised upstream asset would execute inside the build job and could alter
the binaries published to users.

## Evidence

[`package_linux_appimage.sh`](../../../scripts/package_linux_appimage.sh) uses
`wget` to fetch:

```text
https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
```

It immediately applies `chmod +x` and executes the file with
`--appimage-extract-and-run`. There is no pinned release identifier, expected
SHA-256, signature verification, or provenance check.

[`release.yml`](../../../.github/workflows/release.yml) installs `wget` and
calls this packager in the Linux artifact job. The resulting AppImage and
tarball are uploaded under the release artifact name.

## Reproduction

Run the Linux packager in a clean environment without `appimagetool` on PATH
and trace network and process execution. It resolves the mutable `continuous`
asset and executes whatever bytes were returned. Changing a local test URL or
download shim demonstrates that no content check rejects substituted bytes.

## Root Cause

The AppImage step was made best effort for convenience, but its executable
dependency was treated like data and fetched by a moving alias rather than as
a pinned, verified release input.

## Required End State

Pin `appimagetool` to an immutable version and verified digest or consume a
trusted, digest-pinned build image containing it. Fail closed on mismatch.
Record the tool version and digest in release provenance, and keep dependency
selection reviewable in the repository.

## Acceptance Criteria

- The packaging job never executes bytes selected only by a mutable URL.
- Tool version and cryptographic digest are pinned in tracked source.
- A substituted or corrupted download fails before execution.
- Release provenance records the exact packaging tool identity.
- Updating the tool requires an explicit reviewed source change.
- The tarball fallback remains buildable without the AppImage tool.

## Verification Plan

Add a packager test with a local download shim for the valid pinned fixture,
wrong digest, truncated file, and unexpected version. Inspect the workflow log
for the recorded digest and reproduce the AppImage from the same pinned input
in a clean job.

## Related Work

- None.
