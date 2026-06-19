# Public Launch Readiness

Snapshot date: 2026-06-19.

This document is the maintainer-facing source of truth for deciding when MGB64
is ready to flip public and announce broadly. It intentionally separates hard
launch blockers from known follow-up work so the project can be promoted without
overstating its legal, technical, or parity status.

## Launch Decision

Do not make the repository public until all hard blockers are closed:

| Area | Status | Evidence | Required action |
| --- | --- | --- | --- |
| Local release/preflight proof | Passing locally | Public launch uses strict local preflight evidence instead of hosted GitHub Actions. The current tree passes the release guard locally; the final replacement bundle must be refreshed for the exact launch commit. | Keep `scripts/release_preflight.sh` and the strict clean-launch bundle passing before launch. |
| Reachable git history provenance | Blocked | `tools/check_public_history_paths.py` reports old `tools/ido5.3_recomp/*` source/tooling paths in the current preserved history. The generated fresh single-root launch repository must pass this guard before publication. | Publish from a fresh single-root launch repository created from the current clean tree, or perform an approved history rewrite before launch. |
| Branch/tag refs | Passing | `scripts/check_github_launch_ready.sh --allow-private` verifies advertised `refs/heads/*` and `refs/tags/*` point into current public history. | Keep passing before launch; do not create public release tags from any other history. |
| Hidden pull-request refs | Blocked | `scripts/check_github_launch_ready.sh --allow-private` reports stale `refs/pull/*` refs outside current public history. | Get GitHub Support to purge the hidden refs and unreachable objects, or replace the GitHub repository from the clean branch. |
| Current-tree/source-archive hygiene | Passing locally | The strict clean-launch bundle path generates a one-commit launch repo, passes release/history guards, builds a warning-clean source archive, and records exact commit/archive hashes in the generated bundle manifest. | Keep passing before launch. |
| ROM-free source test suite | Passing locally | Clean launch repo and source-archive smoke both pass ROM-free CTest `7/7` during the strict bundle proof. | Keep passing before launch through local preflight and archive smoke. |
| GitHub public text/artifact hygiene | Passing | Public repository metadata, labels, milestones, release notes/assets, unexpired Actions artifacts, issues, PR/commit comments, PR review summaries, Discussions, workflow history, and commit-reference surfaces pass the launch checker. | Re-run after any GitHub metadata migration, label/milestone change, release edit, workflow artifact upload, issue edit, or PR review. |
| Contributor triage labels | Passing | `scripts/check_github_launch_ready.sh --allow-private` verifies the launch labels for audio, renderer, parity, validation, provenance, build, and newcomer triage are present. | Re-run after repository replacement or label migration. |
| Public claims | Passing locally | Release guard rejects overbroad clean-room, signed-binary, packaged-release, and proprietary-notice claims. | Keep `README.md`, `PORT.md`, `ROADMAP.md`, `docs/STATUS.md`, and release notes aligned. |
| Branch protection, Actions policy, and security settings | Deferred | GitHub branch-protection and some security endpoints are not fully readable while private/account-limited. The launch checker enforces GitHub Actions disabled for the local-CI launch policy. If Actions are deliberately re-enabled later, workflow tokens must stay read-only, workflow PR approval must stay disabled, action pins must remain full-SHA, and artifact/log retention must stay 14 days or less. | Configure before or immediately after the public flip. |

## Full Local Launch Proof

For the final local proof, run the strict bundle path from a clean/scrubbed
workspace with a ROM path outside the generated clean launch checkout:

```sh
scripts/prepare_public_launch_bundle.sh \
  --repo akratch/mgb64 \
  --strict-preflight-rom /path/outside/clean-launch-repo/baserom.u.z64 \
  --strict-preflight-macos-app \
  --strict-preflight-macos-app-bundle-sdl2
```

The generated `PUBLIC_LAUNCH_BUNDLE.md` is the place to record exact source
HEAD, clean-launch HEAD, tree hash, archive path, archive SHA-256, exported
issue count, and GitHub blocker evidence. A launch-ready bundle must prove:

- the clean launch repository has exactly one root commit;
- the clean launch tree matches the source HEAD tree;
- release guard and public-history path guard pass inside the clean launch repo;
- the clean source archive builds warning-free and passes ROM-free CTest;
- strict preflight passes ROM-backed quick validation, all 20 single-player
  level spawn checks, save persistence, Dockerfile context check, source-archive
  smoke, and macOS unsigned app asset-free verification;
- if claiming a redistributable macOS app rather than source/local packaging,
  the macOS app lane also passes with `--macos-app-strict-deployment-target`
  and `--macos-app-bundle-sdl2` using a controlled SDL2 build;
- GitHub launch labels/issues export and apply dry-run pass after excluding
  repo-replacement-only issues `#7` and `#30`.

This proof shows the current tree can be turned into an asset-free clean public
launch repository. It does **not** make the preserved current GitHub repository
safe to flip public directly; preserved-history, hidden PR ref, Actions policy,
and branch/security setting gates still apply.

## What Is Already In Good Shape

- The repo is asset-free: no ROM, extracted assets, media captures, save files,
  or generated asset data are tracked.
- The public source guard scans current files and reachable git history for ROM
  contamination, and the launch checker now also flags launch-blocking public
  history paths such as removed local-only tool source.
- Native play is the supported path: clean checkout plus a user-supplied ROM can
  build and run the port locally.
- The local release lane covers release hygiene, CMake configure/build/test,
  source-archive smoke, all-level spawn health, save persistence, Dockerfile
  parse checks, and macOS asset-free bundle verification when invoked through
  `scripts/release_preflight.sh`.
- Public contributor scaffolding exists: issue templates, PR template,
  contributing guide, code of conduct, security policy, roadmap, status docs,
  instrumentation docs, launch labels, and labeled launch issues.
- GitHub issue migration tooling exists for the fresh-repository path:
  `tools/export_github_launch_items.py` exports only open launch issues and
  labels, not comments, closed PRs, Actions history, or hidden refs.

## Conservative Caveats To Keep Public

These are not necessarily "do not launch" blockers if they are stated honestly,
but they are the areas experienced decompilation and source-port contributors
will inspect first.

| Area | Public posture | Tracking issue |
| --- | --- | --- |
| SDK/libultra provenance | Do not call the project fully clean-room while matching-target SDK/libultra-lineage compatibility material remains inventoried in-tree. | #26 |
| N64 byte-matching target | Native play is supported; byte-matching ROM rebuild still needs local matching toolchain files and extracted data-table link work. | #2 |
| Audio/music parity | SFX mapping is much improved, but startup music still needs emulator or hardware reference comparison before fidelity claims get stronger. | #17 |
| Intro camera parity | Bond is still absent from authored level intro cameras such as Dam's early establishing camera. | #18 |
| Renderer parity | Several compatibility defaults are intentionally approximate and need reference-backed scenes before promotion. | #21 |
| Linux/Windows validation | Local Linux/GCC validation and Windows/MSYS2 instructions still need outside verification. | #20, #28, #29 |
| macOS distribution | Local unsigned app bundle is asset-free; signed/notarized DMG release path and controlled SDL2 deployment target are not yet proven. | #22 |
| Warning backlog | Local release and source-archive smoke builds should stay warning-clean; any recurring GCC backlog should be triaged in public. | #25 |

## Final Launch Sequence

1. Resolve reachable-history provenance by publishing from a fresh single-root
   launch repository created from the current clean tree with
   `scripts/create_public_launch_repo.sh --smoke-archive` or the fuller
   non-destructive `scripts/prepare_public_launch_bundle.sh --repo akratch/mgb64`,
   or by an approved history rewrite.
   When a local ROM is available outside the generated launch checkout, prefer
   the fuller strict dry run:
   `scripts/prepare_public_launch_bundle.sh --repo akratch/mgb64 --strict-preflight-rom /path/outside/repo/baserom.u.z64 --strict-preflight-macos-app --strict-preflight-macos-app-bundle-sdl2`.
2. Resolve hidden stale PR refs through GitHub Support or the repository
   replacement runbook in `docs/GITHUB_REPO_REPLACEMENT.md`. GitHub Support can
   only purge hidden refs/caches; it does not fix launch-blocking paths in
   preserved branch history. The replacement path fixes both stale PR refs and
   reachable-history provenance when it uses a fresh single-root launch
   repository.
3. From a clean or scrubbed checkout, run:

```sh
scripts/release_preflight.sh \
  --deep-runtime \
  --rom /path/outside/repo/baserom.u.z64 \
  --macos-app-bundle-sdl2 \
  --strict-ignored \
  --github
```

Add `--macos-app-strict-deployment-target` only when `pkg-config` points at a
controlled SDL2 build with the intended minimum macOS version.

4. Configure GitHub Actions policy for the launch repository so hosted Actions
   are disabled. The checked-in workflows remain manual-only, SHA-pinned local
   CI mirrors for future maintainers who deliberately re-enable Actions.
5. Configure branch protection without required hosted status checks; require
   reviews/conversation resolution and keep force-push/deletion protections.
6. Enable or confirm Discussions, Dependabot vulnerability alerts, private
   vulnerability reporting, and secret scanning/push protection where GitHub
   exposes them.
7. Flip public only after `scripts/check_github_launch_ready.sh` passes without
   `--allow-private`.
8. Announce with the same constraints used in the README: bring your own ROM, no
   copyrighted assets, experimental native port, matching target in progress,
   and SDK/libultra compatibility provenance still inventoried.

## Do Not Announce Yet If

- The strict local release preflight or clean-launch bundle proof has not been
  run for the exact launch commit.
- `tools/check_public_history_paths.py` reports launch-blocking paths in
  reachable git history.
- `git ls-remote origin 'refs/heads/*' 'refs/tags/*' 'refs/pull/*'` exposes
  commits outside current public history.
- `scripts/check_github_launch_ready.sh` reports public text, release asset,
  Actions artifact, workflow-history, or commit-reference contamination.
- A source archive smoke has not been run for the exact launch commit.
- Public docs imply signed binaries, a DMG, Homebrew package, full clean-room
  provenance, bundled assets, or byte-matching ROM rebuild support that is not
  actually wired.
