# AUDIT-0070: Gatekeeper Rejection Is Nonfatal to the Signing Pipeline

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S3 - a signed release can be declared complete after the end-user launch gate rejects it |
| Priority | P1 |
| Area | macOS release / signing and notarization |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | `scripts/release.sh --sign` without `--skip-notarize` |

## Summary

The signing script treats a failed `spctl` Gatekeeper assessment as a warning,
asserts that the app will still work for users, and exits successfully. The
release helper then packages that result as the signed macOS asset.

## Evidence

[`sign_and_notarize.sh`](../../../macos/Scripts/sign_and_notarize.sh) verifies
the code signature, submits and staples notarization, and finally runs
`spctl --assess --type execute`. Its failure branch emits two warnings and
continues to `Signing Complete` and exit 0.

[`release.sh`](../../../scripts/release.sh) invokes this script for `--sign` and
packages the app immediately afterward. The documented release end state in
the design material requires `spctl` to report accepted and a clean-machine
launch without a Gatekeeper block.

This is source-proven. Signing credentials were not available for a natural
notarization run during this audit pass.

## Reproduction

Run the signing pipeline against a bundle or assessment environment for which
the preceding steps succeed but `spctl --assess` returns nonzero. Observe the
warning, final success banner, and zero script status.

## Root Cause

The final consumer-facing policy check is treated as host variance rather than
a release invariant, with no explicit local-only escape mode separating the
two contracts.

## Required End State

A distributable signed/notarized pipeline must fail when Gatekeeper rejects the
final assembled bundle. If maintainers need to bypass assessment for a known
environment limitation, require an explicit non-release option and label the
artifact ineligible for publication.

## Acceptance Criteria

- Nonzero `spctl --assess` makes the distributable signing script nonzero.
- The success banner appears only after Gatekeeper acceptance.
- Release packaging refuses an artifact whose final assessment did not pass.
- Any assessment bypass is explicit, audited, and incompatible with publish mode.
- A clean-machine launch test validates the same final zipped app.

## Verification Plan

Stub `spctl` to return success and failure in script tests, then exercise a real
signed candidate on the oldest supported macOS release and a clean user account.
Record assessment output alongside the artifact hashes used by publication.

## Related Work

- AUDIT-0052 covers binding release assets to the verified source commit.
- AUDIT-0059 covers incomplete runtime-library bundling before signing.
