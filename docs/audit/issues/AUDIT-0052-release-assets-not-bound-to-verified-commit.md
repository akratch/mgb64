# AUDIT-0052: Release Assets Are Not Bound to the Verified Source Commit

| Field | Value |
| --- | --- |
| Status | Fixed (enforced commit-binding; cryptographic attestation owner-gated) |
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

## Resolution

Split into the automatable, enforceable core (done + validated here) and a
cryptographic-attestation layer that is owner-gated by construction.

**Enforced commit-binding (automatable — landed on `feat/webgpu-backend`).**
The commit→artifact link is no longer a printed caveat; it is a structural,
fail-closed check:

- `scripts/release/stamp_provenance.sh` writes a `<asset>.provenance.json`
  sidecar for each release asset recording its sha256 + the source commit
  (`GITHUB_SHA` in CI, `git rev-parse HEAD` locally) + builder/run identity.
  Invoked from the macOS packaging step in `scripts/release.sh` and the Linux +
  Windows jobs in `.github/workflows/release.yml`. python-free (printf JSON) so
  it runs under msys2.
- `scripts/release/verify_provenance.sh` runs at publish time (replacing the old
  `PROVENANCE CAVEAT` reminder in `release.sh`): every `dist/mgb64-*-<version>.*`
  asset must carry a sidecar whose recorded sha256 matches the file on disk and
  whose `commit == git rev-parse HEAD` and `version ==` the release. It **exits 1
  and refuses to publish** on any missing sidecar, extra/renamed unstamped asset,
  orphan sidecar, modified asset, wrong sha, wrong commit, or wrong version, and
  on success emits `mgb64-SHA256SUMS-<version>.txt` + `mgb64-manifest-<version>.json`
  that ship with the release for user-side verification. (Adversarial-review
  follow-up: `release.sh`'s publish glob now excludes the internal
  `*.provenance.json` sidecars, so only the real binaries + SHA256SUMS + manifest
  are uploaded to the public release, not the raw per-asset sidecars.)

Guarded by the ROM-free ctest `release_provenance_guard`
(`tools/tests/test_release_provenance.sh`, **13/13**): a fully commit-bound set is
accepted (checksums + manifest emitted, re-verify ignores the generated outputs)
while wrong-commit, wrong-version, modified-asset, missing-sidecar,
extra-unstamped-asset, and orphan-sidecar are each rejected with exit 1.

**Signing scaffold (landed on `feat/webgpu-backend` — inert until the owner
mints the key).** The cryptographic-attestation layer is now built, fail-closed,
and secret-free; minisign was chosen (over cosign / `actions/attest-build-provenance`)
precisely because it needs **no CI-permission change** — the secret key is
minted and kept only on the owner's Mac, same posture as the Apple `--sign`
path:

- `release.sh --sign-manifest`: fails fast **before the build** unless
  `MGB64_MANIFEST_SIGNING_KEY` names an existing minisign secret key and the
  minisign binary is present (mirrors the `--sign` fail-fast). After the
  provenance gate emits them, it minisign-signs `mgb64-manifest-<v>.json` AND
  `mgb64-SHA256SUMS-<v>.txt` (trusted comment binds version + commit; optional
  `MGB64_MANIFEST_PASSPHRASE` is piped to stdin, never echoed); both `.minisig`
  files join the publish set while the `.provenance.json` sidecar exclusion
  stays intact. **Without the flag: zero behavior change.**
- `scripts/release/verify_release.sh`: the user-side verifier (bash 3.2-safe,
  `sha256sum`/`shasum` portable). Verifies both signatures, checks every listed
  asset digest, prints the bound commit for comparison with the tag target.
  Fails closed on: missing minisign, bad/missing signature, digest mismatch,
  and — via a marker check — the committed **placeholder** public key at
  `scripts/release/mgb64-release-pubkey.txt` (refused with an actionable
  message until the real key is committed).
- Guarded by the ROM-free, secret-free ctest `manifest_signing_guard`
  (`tools/tests/test_manifest_signing.sh`, **12/12**): an EPHEMERAL tempdir
  keypair signs a fixture release; accepted intact, rejected on tampered
  manifest / tampered SHA256SUMS / tampered asset / placeholder pubkey /
  missing minisign; `--sign-manifest` without the key env fails fast while the
  same invocation without the flag stays a clean no-op.
- Owner + user runbooks: docs/RELEASING.md "Verifying a download (users)" and
  "First signed release (owner)".

**Remaining owner work (the only remainder):** mint the keypair on the owner
Mac (`minisign -G`), commit the real public key over the placeholder in
`scripts/release/mgb64-release-pubkey.txt`, and run the next release with
`--sign --sign-manifest` (plus the already-documented real-release exercise of
the CI producer + publish path).
