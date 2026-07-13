# AUDIT-0059: Linux and macOS Packagers Allow Missing SDL Runtime Bundles

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - green portable packages may fail on systems without developer SDL installs |
| Priority | P1 |
| Area | Release packaging / runtime dependencies |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Linux or macOS packaging when linked SDL2 cannot be resolved for bundling |

## Summary

The Linux and macOS packagers describe their outputs as portable but treat a
missing bundled SDL2 library as a warning. Both continue and succeed, allowing a
release artifact whose executable still depends on a host/developer SDL path
and may fail immediately on a clean user's machine.

## Evidence

[`package_linux_appimage.sh`](../../../scripts/package_linux_appimage.sh) uses
`ldd` to find SDL2. If it cannot, it prints that the AppImage needs a system
SDL2 and continues to build both outputs.

[`build_gl_app.sh`](../../../macos/Scripts/build_gl_app.sh) uses `otool` and
`pkg-config` to resolve the linked SDL2 dylib. If resolution fails, it prints
that the app will need a system SDL2 and continues through signing and the
successful final message. [`release.sh`](../../../scripts/release.sh) packages
that result without a clean-machine dependency gate.

The Windows packager attempts a stricter runtime-DLL bundle; the affected
contract here is Linux and macOS.

## Reproduction

Substitute `ldd`/`otool` output whose SDL path is absent or unresolvable and run
the corresponding packager. It emits a warning but exits zero and produces the
archive. Inspect the binary's dynamic dependencies inside the package; SDL2 is
not satisfied internally.

## Root Cause

Dependency bundling is implemented as convenience logic rather than a release
invariant, and asset-free verification does not prove runtime dependency
closure.

## Required End State

For portable release mode, resolve and bundle every non-system runtime library,
rewrite load paths where required, and fail if dependency closure is incomplete.
Validate the assembled artifact in a clean environment without Homebrew,
developer prefixes, or host SDL packages.

## Acceptance Criteria

- A missing SDL2 bundle fails Linux and macOS release packaging.
- AppImage/tar and app-bundle dependency scans resolve SDL internally.
- No absolute Homebrew, build-tree, or runner-only SDL path remains.
- Clean-container/clean-VM smoke launch opens the ROM-free launcher.
- Local developer-only packages are distinctly labeled if system SDL is allowed.
- Signing/notarization occurs after final dependency rewriting and bundling.

## Verification Plan

Add dependency-scan gates using `ldd`/`readelf` and `otool` against staged
artifacts, plus fault fixtures for missing and wrong-architecture SDL. Smoke
launch Linux in a minimal container and macOS in a clean VM/user environment
without package-manager SDL.

## Related Work

- AUDIT-0058 covers missing AppImage output rather than a broken produced image.
- AUDIT-0052 binds validated final packages to the release commit.
