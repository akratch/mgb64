# AUDIT-0052: Release Assets Are Not Bound to the Verified Source Commit

| Field | Value |
| --- | --- |
| Status | Open |
| Severity | S1 - stale or substituted binaries can be published under a verified tag |
| Priority | P1 |
| Area | Release provenance / publication |
| Evidence level | Source proven |
| Confidence | High |
| Origin | Newly confirmed by this audit |
| Affected configurations | Versioned releases assembled from preexisting `dist/` artifacts |

## Summary

The release helper verifies the current source and tag, then uploads every
`dist/` file whose name contains the requested version. It does not verify which
commit or workflow produced those files, nor compare their hashes with an
attestation. A stale, locally modified, or substituted binary can therefore be
attached to a correctly verified source tag.

## Evidence

[`release.sh`](../../../scripts/release.sh) runs the public source guard chain
and records `head_sha`, but asset selection is only:

```sh
ls -1 "$dist"/mgb64-*-"$version".*
```

Those files are passed directly to `gh release upload/create`. The script
itself prints `PROVENANCE CAVEAT` stating that assets are matched by version
glob and are not cryptographically bound to the verified commit.

Linux and Windows artifacts are manually downloaded from a dispatched workflow
before this local publication step. No run ID, checked-out SHA, artifact digest,
builder identity, or signed manifest is consumed by the publisher.

## Reproduction

Place an arbitrary file at `dist/mgb64-linux-vX.Y.Z.tar.gz`, use a valid
`vX.Y.Z` tag and otherwise passing dry-run guards, and inspect the asset list
selected by `release.sh --version vX.Y.Z`. Selection depends on the filename,
not the file's origin or content.

## Root Cause

Source verification and artifact assembly are independent manual workflows.
The version string is used as their only join key, with a printed caveat in
place of an enforceable provenance contract.

## Required End State

Generate a release manifest binding every asset digest, platform, product
version, builder/workflow identity, and source commit. Verify that manifest
against the target tag immediately before upload and fail closed on any missing,
extra, stale, or mismatched asset. Prefer signed attestations from controlled
builders for remotely produced artifacts.

## Acceptance Criteria

- Every uploaded asset has a verified cryptographic digest in a tracked or
  signed release manifest.
- The manifest source commit exactly equals the version tag target.
- Linux/Windows workflow artifacts prove their checkout SHA and build run.
- Locally built macOS output records the same commit and version.
- A renamed stale or modified artifact is rejected before any upload.
- The final release publishes the manifest and checksums for users.

## Verification Plan

Create fixture manifests for correct, wrong-SHA, wrong-version, modified,
missing, extra, and replayed artifacts. Run publication in a disposable test
repository and assert that only the fully matching set reaches the release,
then independently verify downloaded release assets against the manifest.

## Related Work

- AUDIT-0037 covers an unpinned executable inside the Linux artifact build.
- AUDIT-0053 covers mismatched compiled product versions.
