# Public Launch Readiness

Snapshot date: 2026-06-19.

This document is the maintainer-facing source of truth for deciding when MGB64
is ready to flip public and announce broadly. It intentionally separates hard
launch blockers from known follow-up work so the project can be promoted without
overstating its legal, technical, or parity status.

## Launch Decision

Do not make the repository public until both hard blockers are closed:

| Area | Status | Evidence | Required action |
| --- | --- | --- | --- |
| Hosted CI | Blocked | Current-head GitHub Actions jobs fail before runner startup with billing/spending-limit annotations. | Fix account billing/spending settings, rerun CI on `main`, and require a green current-head run. |
| Hidden pull-request refs | Blocked | `scripts/check_github_launch_ready.sh --allow-private` reports stale `refs/pull/*` refs outside current public history. | Get GitHub Support to purge the hidden refs and unreachable objects, or replace the GitHub repository from the clean branch. |
| Repository source hygiene | Passing locally | `./scripts/ci/check_release_ready.sh` passes. | Keep passing before launch. |
| ROM-free source test suite | Passing locally | `ctest --test-dir build --output-on-failure` passes after CMake configure. | Keep passing before launch and in hosted CI. |
| GitHub public text hygiene | Passing | Public repository metadata, labels, issues, PR comments, Discussions, workflow history, and commit-reference surfaces pass the launch checker. | Re-run after any GitHub metadata migration, label change, or issue edit. |
| Public claims | Passing locally | Release guard rejects overbroad clean-room, signed-binary, packaged-release, and proprietary-notice claims. | Keep `README.md`, `PORT.md`, `ROADMAP.md`, `docs/STATUS.md`, and release notes aligned. |
| Branch protection and security settings | Deferred | GitHub branch-protection and some security endpoints are not fully readable while private/account-limited. | Configure after CI can run and before or immediately after the public flip. |

## What Is Already In Good Shape

- The repo is asset-free: no ROM, extracted assets, media captures, save files,
  or generated asset data are tracked.
- The public source guard scans current files and reachable git history for ROM
  and high-risk provenance contamination.
- Native play is the supported path: clean checkout plus a user-supplied ROM can
  build and run the port locally.
- The local release lane covers release hygiene, CMake configure/build/test,
  source-archive smoke, all-level spawn health, save persistence, Dockerfile
  parse checks, and macOS asset-free bundle verification when invoked through
  `scripts/release_preflight.sh`.
- Public contributor scaffolding exists: issue templates, PR template,
  contributing guide, code of conduct, security policy, roadmap, status docs,
  instrumentation docs, and labeled launch issues.
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
| Linux/Windows validation | Linux CI must go green, and Windows/MSYS2 instructions still need outside verification. | #20, #28, #29 |
| macOS distribution | Local unsigned app bundle is asset-free; signed/notarized DMG release path is not yet proven. | #22 |
| Warning backlog | Local and CI release builds should stay warning-clean; any recurring GCC backlog should be triaged in public. | #25 |

## Final Launch Sequence

1. Fix GitHub Actions billing/spending so hosted runners can start.
2. Resolve hidden stale PR refs through GitHub Support or the repository
   replacement runbook in `docs/GITHUB_REPO_REPLACEMENT.md`.
3. From a clean or scrubbed checkout, run:

```sh
scripts/release_preflight.sh \
  --deep-runtime \
  --rom /path/outside/repo/baserom.u.z64 \
  --macos-app \
  --strict-ignored \
  --github
```

4. Confirm the latest `main` CI run is green for the exact launch commit.
5. Configure branch protection so `Release hygiene` and `CMake build (Linux)`
   are required and up to date before merge.
6. Enable or confirm Discussions, Dependabot vulnerability alerts, private
   vulnerability reporting, and secret scanning/push protection where GitHub
   exposes them.
7. Flip public only after `scripts/check_github_launch_ready.sh` passes without
   `--allow-private`.
8. Announce with the same constraints used in the README: bring your own ROM, no
   copyrighted assets, experimental native port, matching target in progress,
   and SDK/libultra compatibility provenance still inventoried.

## Do Not Announce Yet If

- GitHub Actions is red or stuck before runner startup.
- `git ls-remote origin 'refs/pull/*'` exposes commits outside current public
  history.
- `scripts/check_github_launch_ready.sh` reports public text, workflow-history,
  or commit-reference contamination.
- A source archive smoke has not been run for the exact launch commit.
- Public docs imply signed binaries, a DMG, Homebrew package, full clean-room
  provenance, bundled assets, or byte-matching ROM rebuild support that is not
  actually wired.
