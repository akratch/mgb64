# Public Release Checklist

Use this before flipping the repository public, tagging a release, or posting a
public announcement. The goal is to make the same promises other credible
decompilation projects make: bring your own ROM, no bundled assets, clear
provenance, and honest status.

For the dated go/no-go matrix, see
[PUBLIC_LAUNCH_READINESS.md](PUBLIC_LAUNCH_READINESS.md). If that matrix lists a
hard blocker as open, do not treat this checklist as sufficient by itself.

## Required Checks

For the maintainer launch path, prefer the one-command preflight:

```sh
scripts/release_preflight.sh \
  --deep-runtime \
  --rom baserom.u.z64 \
  --macos-app-bundle-sdl2
```

Before the repository is ready to flip public, run the same preflight from a
fresh or scrubbed launch checkout with `--strict-ignored --github` so the final
pass also verifies there are no local ignored ROM/media/capture artifacts and
checks GitHub metadata/ref/artifact/security settings. Hosted GitHub Actions is
not a launch gate. In strict mode, keep the ROM outside the repository checkout:

```sh
scripts/release_preflight.sh \
  --deep-runtime \
  --rom /path/outside/repo/baserom.u.z64 \
  --macos-app-bundle-sdl2 \
  --strict-ignored \
  --github
```

Omit `--macos-app-bundle-sdl2` when running the source/native preflight from
Linux or Windows; the macOS app-bundle lane is only available on macOS.

The equivalent lower-level checks are:

```sh
./scripts/ci/check_release_ready.sh
./tools/validate_quick.sh
cmake --build build --parallel 4 2>&1 | tee /tmp/mgb64-build.log
python3 tools/summarize_build_warnings.py \
  /tmp/mgb64-build.log \
  --json-out /tmp/mgb64-build-warnings.json
ctest --test-dir build --output-on-failure
./tools/spawn_health_check.sh --all --no-build --rom baserom.u.z64 --binary build/ge007
./tools/save_persistence_check.sh --no-build --rom baserom.u.z64 --binary build/ge007
docker build --check .
scripts/make_public_source_archive.sh --force
scripts/smoke_public_source_archive.sh \
  dist/mgb64-$(git rev-parse --short=12 HEAD).tar.gz \
  --max-warnings 0
git status --short
```

`git status --short` should show no tracked changes. Before cutting a release
artifact from a local checkout, also review `git clean -ndX` so ignored ROMs,
extracted assets, screenshots, audio dumps, and build outputs cannot be swept
into an archive by mistake.
For final public launch, prefer `scripts/release_preflight.sh --strict-ignored`
from a fresh checkout or scrubbed workspace, with any ROM-backed validation
using `--rom` outside the repository. That stricter gate allows normal build
output but fails on ignored ROMs, extracted assets, saves, screenshots, audio
dumps, and other high-risk media outside build directories.
For the repository-replacement launch path, the same strict gate can be captured
inside the non-destructive launch bundle:

```sh
scripts/prepare_public_launch_bundle.sh \
  --repo akratch/mgb64 \
  --strict-preflight-rom /path/outside/clean-launch-repo/baserom.u.z64 \
  --strict-preflight-macos-app \
  --strict-preflight-macos-app-bundle-sdl2
```

Add `--strict-preflight-macos-app-strict-deployment-target` only when
`pkg-config` points at a controlled SDL2 build with the intended minimum macOS
version.

Review `/tmp/mgb64-build-warnings.json` before posting a release announcement:
local release builds and the source-archive smoke build are expected to be
warning-clean. Treat any compiler/linker warning as a launch blocker unless the
threshold is deliberately changed with an accompanying issue and rationale.
Use `scripts/make_public_source_archive.sh` for source archives; it uses
`git archive` so only tracked files are packaged.
Then run `scripts/smoke_public_source_archive.sh` on that exact archive to prove
the user-facing tarball configures, builds, and passes the ROM-free CTest suite
without relying on `.git` or ignored local files.
For a launch gate, pass `--max-warnings 0` so the isolated archive build also
fails on compiler/linker warnings.
For a launch gate with a local ROM, include the all-level spawn health and save
persistence lanes. Those are what `scripts/release_preflight.sh --deep-runtime`
runs after the short quick-validation spawn smoke.
`docker build --check .` validates the Dockerfile and `.dockerignore` parse
without needing to install packages or include local ignored files in a build
context. If local Docker storage is healthy, a full Docker image build is a
useful extra check, but the release requirement is the local preflight and
source-archive smoke proof for the exact launch commit.

For a stronger local pass when a ROM and native binary are available:

```sh
./tools/spawn_health_check.sh --all --no-build
./tools/save_persistence_check.sh --no-build
./tools/renderer_parity_capture.sh --no-build
```

The renderer parity lane writes local ROM-derived screenshots/traces under
`/tmp` and prints compare commands. Inspect the images locally; do not commit or
attach the captured artifacts.

For the local macOS app shell, verify the unsigned source-built bundle:

```sh
./macos/Scripts/build_universal.sh --release --build-dir build-macos-universal
./macos/Scripts/verify_asset_free.sh build-macos-universal/libge007_lib.a
./macos/Scripts/build_app_bundle.sh --release \
  --build-dir build-macos-app \
  --output build-macos-app/MGB64.app
./macos/Scripts/verify_asset_free.sh build-macos-app/libge007_lib.a
./macos/Scripts/verify_asset_free.sh build-macos-app/MGB64.app
```

This proves the universal static library lane used by the macOS workflow, local
`.app` assembly, and asset hygiene. It does not prove signing, notarization,
DMG creation, a controlled SDL2 runtime/deployment target, or end-user
redistributable packaging.
For a redistributable macOS app candidate, rebuild the app with a controlled
SDL2 prefix and fail closed on the deployment target before signing:

```sh
PKG_CONFIG_PATH=/path/to/controlled-sdl2/lib/pkgconfig \
  ./macos/Scripts/build_app_bundle.sh --release \
    --build-dir build-macos-app \
    --output build-macos-app/MGB64.app \
    --deployment-target 13.0 \
    --strict-deployment-target \
    --bundle-sdl2
```

For `.app` inputs, `verify_asset_free.sh` checks both the executable and the
bundle resource allowlist, so accidental ROMs, captures, audio dumps, or
unexpected media resources fail the gate.

For audio changes, capture at least one deterministic trace and confirm the
final PCM rail-hit count is not unexpectedly nonzero:

```sh
mkdir -p /tmp/ge007_audio_savedir
GE007_AUDIO_TRACE=/tmp/ge007_audio.jsonl \
  GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
  ./build/ge007 --rom baserom.u.z64 --level 33 --deterministic \
  --savedir /tmp/ge007_audio_savedir \
  --screenshot-frame 120 --screenshot-exit
```

Review `output_peak` and `output_rail_hits` in the trace before claiming an
audio/gain-staging fix.

For music-fidelity changes, also compare against an emulator/hardware reference
capture when one is available:

```sh
tools/startup_music_reference_check.sh \
  --no-build \
  --rom baserom.u.z64 \
  --out-dir /tmp/mgb64_audio_ref \
  --reference /tmp/mgb64_audio_ref/reference_boot.wav
```

Treat this as a spectral/envelope regression lane: it catches thin, overly
bright, or missing-frequency output without requiring human listening, but it is
not a bit-perfect N64 audio proof.

## Public Claims

- Say clearly that users must provide their own legally dumped ROM.
- Say clearly that no ROM, extracted assets, or ROM media are distributed.
- Do not describe the repository as fully clean-room while the SDK/libultra
  compatibility material listed in [../THIRD_PARTY.md](../THIRD_PARTY.md)
  remains in-tree.
- Avoid "from-scratch decompilation" shorthand in public launch copy while the
  matching-target SDK/libultra provenance exception remains in-tree.
- Do not leave direct SDK/devkit source-path or copied-documentation comments in
  public source. The contamination guard fails on those; keep provenance in
  [../THIRD_PARTY.md](../THIRD_PARTY.md) and use project-owned source comments.
- Keep [PROVENANCE_AUDIT.md](PROVENANCE_AUDIT.md) aligned when touching
  native-compiled SDK-shaped support code.
- Do not claim a DMG, signed binary, notarized binary, or redistributable macOS
  release until that path is actually wired and verified. The current `.app`
  path is an unsigned local source build.
- Do not add a Homebrew Cask, appcast URL, release-download URL, or package
  SHA placeholder until a real asset-free signed/notarized artifact exists.
- Keep [../PORT.md](../PORT.md), [STATUS.md](STATUS.md), and
  [../RELEASE_NOTES.md](../RELEASE_NOTES.md) aligned before posting.

## GitHub Setup

- Enable Discussions or another low-friction place for contributor questions.
- Enable private vulnerability reporting if available.
- Enable secret scanning and secret-scanning push protection if GitHub exposes
  them for the repository/account.
- Disable hosted GitHub Actions for the launch repository. The checked-in
  workflows are manual-only local-CI mirrors and pin external actions to full
  SHAs. The release guard fails if a future workflow adds an automatic hosted
  trigger or a tag-/branch-pinned action.
- Protect `main` without required hosted status checks. Require review and
  conversation resolution, and disallow force pushes/deletions. Use
  `scripts/configure_github_launch_settings.sh --repo akratch/mgb64` to preview
  the repository/security/branch-protection settings before applying them with
  `--yes`.
- Run `scripts/check_github_launch_ready.sh` after GitHub settings are final.
  Before flipping public, `scripts/check_github_launch_ready.sh --allow-private`
  gives a dry-run view while still checking that local `HEAD` matches GitHub
  `main`. The script also scans local reachable git history for launch-blocking
  public paths, verifies the repository Actions/local-CI policy,
  scans GitHub branch, tag, pull-request, and workflow-run history surfaces for
  commits outside the current public branch, and scans public repository
  metadata, labels, milestones, release notes/assets, issue text, PR/commit
  comments, PR review summaries, and Discussion text for high-risk private
  paths, stale handoff language, token-shaped strings, proprietary notice
  fragments, and resolvable stale commit references. Uploaded release assets and
  unexpired GitHub Actions artifacts are also checked for ROM-shaped media,
  archive, app, and binary payload names; treat findings there like
  repository-history contamination. GitHub keeps closed PR refs read-only; if
  any stale `refs/pull/*`
  refs remain after a history rewrite, purge them through GitHub support or
  recreate/replace the public repository before launch. Use
  [GITHUB_REPO_REPLACEMENT.md](GITHUB_REPO_REPLACEMENT.md) for that runbook.
  GitHub Support purge only addresses hidden refs/caches; it does not make an
  otherwise unsafe preserved branch history suitable for public launch.
  The script also checks that the contributor triage labels used by the issue
  templates and launch roadmap are present.
- If `tools/check_public_history_paths.py` reports removed local-only tool
  source in reachable history, do not publish the existing commit graph. Use
  `scripts/create_public_launch_repo.sh --smoke-archive` to produce, archive
  smoke-test, and validate a fresh single-root launch repository, or perform an
  approved history rewrite. For the full replacement handoff, use
  `scripts/prepare_public_launch_bundle.sh --repo akratch/mgb64`; it prepares
  the clean repo, source-archive smoke evidence, scrubbed issue/label export,
  GitHub blocker report, and a manifest of commands without modifying GitHub.
- Set repository topics that make the project discoverable without implying an
  official affiliation.
- Keep issue and PR templates enabled.
- Do not attach prebuilt binaries that bundle ROM-derived data. Native binaries
  must remain asset-free and should pass `macos/Scripts/verify_asset_free.sh`
  where applicable.

## Known Launch Caveat

The largest unresolved release-hygiene issue is remaining SDK/libultra-lineage
provenance. The current tree inventories that material and the release guard
fails if proprietary SDK notice text is reintroduced, but the most conservative
public branch would keep replacing or isolating matching-target-only SDK
compatibility source and complete the native-compiled SDK/demo-lineage audit.
