# Releasing MGB64 (prebuilt apps)

How the prebuilt **MGB64** apps for macOS, Windows, and Linux are built,
packaged, and published. One portable codebase (`ge007` with `MGB64_APP=ON`)
produces all three; per-platform binding details are in
**[APP_ARCHITECTURE.md](APP_ARCHITECTURE.md)**.

Everything here is **asset-free** — no ROM or ROM-derived data is ever built,
packaged, or published (bring-your-own-ROM).

---

## The model: hybrid local + free CI

Public repos get free GitHub Actions runners, so cost is not a factor — the only
constraint is this repo's deliberate **local-preflight** provenance posture
(hosted workflows are `workflow_dispatch`-only; they never auto-run and never
write repository state). Releases are therefore assembled from two sources:

| Platform | Built by | Why |
| --- | --- | --- |
| **macOS** (universal) | **Locally** on the maintainer's Mac (`scripts/release.sh`) | This is the developed + tested platform; it also needs the local Apple toolchain. |
| **Windows** (portable `.zip`) | **CI** (`.github/workflows/release.yml`, MSYS2/MinGW) | Can't be built natively on macOS. |
| **Linux** (AppImage + `.tar.gz`) | **CI** (`.github/workflows/release.yml`, ubuntu-22.04) | Can't be built natively on macOS. |

The CI build jobs are asset-free and write no repo state — they only compile the
public source and upload the results as workflow artifacts, so they stay
consistent with the provenance policy.

---

## Build the app from source (any platform)

Full per-OS toolchain + build commands are in
[APP_ARCHITECTURE.md §4](APP_ARCHITECTURE.md#4-build). In brief:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DMGB64_APP=ON
cmake --build build -j          # produces ./build/ge007 (the launcher)
```

Package it for distribution:

| Platform | Command | Output |
| --- | --- | --- |
| macOS | `./macos/Scripts/build_gl_app.sh [--universal]` | `build-macos-app/MGB64.app` (asset-free verified, ad-hoc signed) |
| Linux | `./scripts/package_linux_appimage.sh --binary build/ge007 --version vX.Y.Z` | `dist/mgb64-linux-vX.Y.Z.{AppImage,tar.gz}` |
| Windows (MSYS2) | `./scripts/package_windows_zip.sh --binary build/ge007.exe --version vX.Y.Z` | `dist/mgb64-windows-vX.Y.Z.zip` |

Every packager and the bundler run `verify_asset_free.sh` on the binary.

---

## Code signing + notarization (macOS)

Requires an active [Apple Developer Program](https://developer.apple.com/programs)
enrollment ($99/yr; Individual is fine for a solo maintainer). Signing runs
**locally**, alongside the rest of the macOS build — it is deliberately not
wired into hosted CI, so the Developer ID private key and Apple credentials
never need to leave this machine (see "The model" above).

One-time setup:

1. In Xcode → Settings → Accounts (or the [Certificates page](https://developer.apple.com/account/resources/certificates/list)),
   create a **Developer ID Application** certificate (not "Apple Distribution" —
   that one's for the App Store). This installs the cert + private key into
   your login keychain.
2. Note your **Team ID** (Membership page) and identity string, e.g.
   `Developer ID Application: Jane Doe (ABCDE12345)`.
3. Generate an app-specific password at
   [appleid.apple.com](https://appleid.apple.com) → Sign-In and Security →
   App-Specific Passwords, for `notarytool` to authenticate with.

Export these before running a signed release:

```sh
export DEVELOPER_ID_APPLICATION="Developer ID Application: Jane Doe (ABCDE12345)"
export APPLE_ID="you@example.com"
export APPLE_TEAM_ID="ABCDE12345"
export APPLE_APP_PASSWORD="xxxx-xxxx-xxxx-xxxx"   # or @keychain:label
```

Then pass `--sign` to `scripts/release.sh` (see below). Under the hood this
calls `macos/Scripts/sign_and_notarize.sh`, which signs the app bundle,
submits it to Apple's notary service, waits for approval, and staples the
ticket — all before the `.zip` is created, so the staple travels with it.
Without `--sign`, the app ships ad-hoc signed and Gatekeeper will warn
end users on first launch.

---

## Cut a release

1. **Windows + Linux (CI):** a maintainer dispatches the release workflow
   (Actions → "Release build (Windows + Linux)" → Run, with a version label).
   It builds, packages, verifies asset-free, headless-smokes the Linux launcher,
   and uploads `mgb64-windows-*` / `mgb64-linux-*` artifacts. Download them into
   `dist/`.
2. **macOS + publish (local):**
   ```sh
   # build + validate macOS, stage all dist/ assets, and publish the release:
   scripts/release.sh --version v0.3.0 --repo akratch/mgb64 --publish
   # signed + notarized (credentials exported per "Code signing" above):
   scripts/release.sh --version v0.3.0 --repo akratch/mgb64 --sign --publish
   # or a rolling 'latest' prerelease refreshed from main:
   scripts/release.sh --version v0.3.0 --repo akratch/mgb64 --rolling-latest --publish
   ```
   Without `--publish` it only builds/stages `dist/` assets and prints the next
   command. `--publish` requires `gh` auth and creates/updates the GitHub
   Release, attaching every `dist/mgb64-*-<version>.*` present (macOS locally +
   Windows/Linux from CI). Without `--sign`, the macOS asset ships ad-hoc
   signed and Gatekeeper will warn end users on first launch.

The README's **Download** table links to `/releases/latest`, so a rolling
`latest` prerelease keeps the download current between tagged majors.

---

## The two repositories

- **`akratch/mgb64`** — the **public** repo. Canonical, developed **in the open**
  with real, additive history. Its root is a one-time clean snapshot (the private
  pre-history could not ship — it contained proprietary/SDK-notice text — so the
  public history begins at the launch snapshot and grows normally from there).
- **`akratch/mgb64-prepublic-*`** — the **private** staging/archive. Holds the
  full pre-launch development history (kept private for the reason above). Useful
  for private experimentation; only clean work is ported to public.

`main` on the public repo is protected: **changes go through a PR** (0 required
approvals so a solo maintainer can self-merge; admins can merge). No direct
pushes, no force-pushes.

## Developing in the open (going forward)

This is now a normal open-source project — no more single-root re-squashes.

```sh
git clone git@github.com:akratch/mgb64.git   # work from the public repo
git checkout -b my-change
# ...edit, build (MGB64_APP=ON), test...
git push -u origin my-change
gh pr create --base main --fill                # open a PR
gh pr merge --rebase --delete-branch           # merge (self-merge is fine)
```

Outside contributors PR the same way; you review + merge theirs. Keep the tree
asset-free — `scripts/ci/check_release_ready.sh` is the guard, and `.gitattributes
export-ignore` keeps internal dev docs (`*_PLAN`/`*_GUIDE`/
`*_THEORY`/`*_REVIEW`/`*_WIP`) out of the public tree.

### The one-time launch snapshot (historical, done)

The v0.3.0 launch published a **single clean snapshot** of the sanitized tree as
one commit on top of the previous public root, via a PR (not a force-push). The
sanitized tree is produced by `scripts/create_public_launch_repo.sh` (applies the
`export-ignore` filter and re-runs every release guard); the maintainer gate is
`scripts/release_preflight.sh`. That path only matters if a future re-baseline is
ever needed — routine work is just PRs (above).

---

## Checklist

- [ ] `scripts/release_preflight.sh` passes (clean tree, warning-clean build, ROM-free tests, asset-free).
- [ ] Windows + Linux CI artifacts downloaded into `dist/`.
- [ ] `scripts/release.sh --version <v> --repo <r> --publish` (macOS built locally + all assets attached).
- [ ] README Download links resolve (macOS asset present on `/releases/latest`).
- [ ] `RELEASE_NOTES.md` updated.
