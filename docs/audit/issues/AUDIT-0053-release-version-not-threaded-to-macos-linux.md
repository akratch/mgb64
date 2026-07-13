# AUDIT-0053: macOS and Linux Release Builds Do Not Receive the Release Version

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - shipped binaries and bundle metadata misidentify their version |
| Priority | P1 |
| Area | Release packaging / product version |
| Evidence level | Source and built-artifact proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | macOS and Linux release artifacts |

## Summary

The release version is applied to archive filenames, but macOS and Linux builds
do not pass it to CMake. Their app shell remains `0.0.0-dev`; the macOS plist is
independently hardcoded to `0.1.0`. A release can therefore be named and tagged
vX.Y.Z while About, sysinfo, update checks, and Finder metadata report unrelated
versions.

## Evidence

[`CMakeLists.txt`](../../../CMakeLists.txt) defaults `MGB64_VERSION` to
`0.0.0-dev` and compiles it into `AppVersion`. In
[`release.yml`](../../../.github/workflows/release.yml), the Windows configure
passes `-DMGB64_VERSION` but the Linux configure does not.

[`build_gl_app.sh`](../../../macos/Scripts/build_gl_app.sh) has no version
option and does not pass `MGB64_VERSION`. [`release.sh`](../../../scripts/release.sh)
uses its `--version` only when naming the macOS zip and selecting assets.
[`Info.plist`](../../../macos/Resources/Info.plist) hardcodes both bundle version
fields to `0.1.0`, and the build script never changes them.

The current built `ge007` contains the string `0.0.0-dev`; the generated
`build-macos-app/MGB64.app` plist reports `0.1.0`. Dev versions intentionally
disable update banners, so this also breaks update availability for shipped
macOS/Linux binaries.

## Reproduction

Dispatch a Linux build or run `scripts/release.sh --version v9.8.7` for macOS.
Inspect the archive name, launcher About version, diagnostic sysinfo, compiled
strings, and macOS plist. The artifact name uses v9.8.7 while the product uses
`0.0.0-dev` and the plist uses `0.1.0`.

## Root Cause

Version propagation was implemented only in the Windows workflow. Packaging
scripts treat version as an output filename label rather than one build input
shared by code and platform metadata.

## Required End State

Resolve one canonical normalized product version at release entry, pass it to
every platform build, write it into native package metadata, and verify all
observable versions against the target tag before publication.

## Acceptance Criteria

- About and diagnostic sysinfo equal the release tag version on every platform.
- Linux and macOS CMake configure receive `MGB64_VERSION` explicitly.
- macOS `CFBundleShortVersionString` and `CFBundleVersion` are generated from
  the release version under documented Apple formatting rules.
- Windows VERSIONINFO continues matching the same source.
- Archive filenames, manifest, tag, binary, and package metadata all agree.
- A default/dev version is rejected by versioned release publication.

## Verification Plan

Build all platform artifacts with a synthetic prerelease version and inspect
the app UI/version function, binary strings or a version command, Windows
VERSIONINFO, macOS plist, archive names, and release manifest. Add a publication
gate that compares each result with the tag and rejects `0.0.0-dev`.

## Related Work

- AUDIT-0052 requires the version to be part of the artifact provenance
  manifest.
