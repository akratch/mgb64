# GitHub Repository Replacement Runbook

Use this when GitHub-side metadata or reachable git history exposes pre-public
or local-only material that should not become public. Known cases are:

- closed PR refs under `refs/pull/*`: GitHub keeps these refs read-only, and
  they can continue to point at commits outside the current public branch after
  a history rewrite;
- removed local-only matching-tool source in reachable history, such as the old
  `tools/ido5.3_recomp/*` source files.

Do not flip the repository public while
`scripts/check_github_launch_ready.sh --allow-private` reports stale pull refs
or `tools/check_public_history_paths.py` reports launch-blocking paths.

## Why This Exists

`git push origin :refs/pull/8/head` and the equivalent GitHub API ref deletion
fail because GitHub treats pull-request refs as hidden/read-only refs. If those
refs point at pre-public commits, a public repository can still reveal old commit
objects and PR diffs even when `main` itself is clean. Likewise, deleting a
local-only tool from `HEAD` does not remove it from reachable git history.

There are two acceptable fixes for stale hidden pull-request refs:

- ask GitHub Support to purge the stale hidden PR refs and associated unreachable
  objects; or
- replace the GitHub repository with a fresh repository populated from a
  single-root launch commit created from the current clean tree.

If reachable branch history itself contains launch-blocking provenance paths,
GitHub Support purge is not enough. In that case, publish from the fresh
single-root launch repository or complete an approved history rewrite, then
verify hidden refs separately.

## Verify The Problem

```sh
git fetch origin main
git status --short --branch
./scripts/check_github_launch_ready.sh --allow-private
git ls-remote origin 'refs/heads/*' 'refs/tags/*' 'refs/pull/*'
```

The launch check must fail if any advertised branch, tag, or `refs/pull/*` ref
points at a commit outside the current public history, or if public GitHub text
links to resolvable commits outside that history. The local history check must
also pass:

```sh
python3 tools/check_public_history_paths.py --repo-root .
```

## Option A: GitHub Support Purge

Generate a local evidence report:

```sh
scripts/prepare_github_launch_evidence.sh --repo akratch/mgb64
```

The generated report is written under `/tmp` by default and can include stale
hidden-ref SHAs, so do not commit it.

Send GitHub Support:

- repository: `akratch/mgb64`;
- current public branch head: `git rev-parse HEAD`;
- stale refs from the `Pull request ref surface` section of
  `scripts/check_github_launch_ready.sh --allow-private`;
- request: purge the listed hidden `refs/pull/*` refs, closed PR diff caches, and
  unreachable commit objects that are not reachable from current `main`.

After support confirms the purge:

```sh
git ls-remote origin 'refs/pull/*'
./scripts/check_github_launch_ready.sh --allow-private
```

The pull-ref section must pass before launch.

If the local reachable-history provenance section still fails after support
purges hidden refs, do not publish that repository history. Use Option B or an
approved history rewrite.

## Option B: Replace The Repository

This is destructive to the GitHub repository identity. Use it only after deciding
that preserving current GitHub issues/PR numbers is less important than removing
all pre-public GitHub metadata.

Do not use `git push --mirror` from the old GitHub repository. Push only the
clean branch you intend to publish.

1. Final local source validation:

```sh
./scripts/ci/check_release_ready.sh
ctest --test-dir build --output-on-failure
```

2. Export launch issues and labels that should be recreated:

```sh
python3 tools/export_github_launch_items.py export \
  --repo akratch/mgb64 \
  --out-dir /tmp/mgb64-launch-items \
  --exclude-number 7 \
  --exclude-number 30
```

Issues #7 and #30 are old-repository launch blockers. Recreate them manually in
the fresh repository only if the same blocker still exists after replacement.
The exporter copies only labels and open issue bodies; it does not copy closed
PRs, comments, Actions runs, or hidden refs. It fails if exported text contains
private paths, token-shaped strings, proprietary notice fragments, or
non-current commit-like references.

Preview the import before modifying a fresh repository:

```sh
python3 tools/export_github_launch_items.py apply \
  --repo akratch/mgb64 \
  --input-dir /tmp/mgb64-launch-items
```

Add `--yes` only after the fresh repository exists and the preview is correct.

3. Create and verify a local clean launch repository:

```sh
scripts/create_public_launch_repo.sh --smoke-archive
```

The helper prints a fresh local repository path. It creates a single root commit
from the current committed tree, verifies tree equality and one-commit history,
runs the release and history-path guards inside that repository, and writes a
local evidence report. With `--smoke-archive`, it also creates and smoke-tests a
source archive from the clean repository. The resulting repository must have
exactly one commit:

```sh
git -C /path/from/helper rev-list --all --count
```

For the least error-prone replacement dry run, prepare the whole local launch
bundle instead:

```sh
scripts/prepare_public_launch_bundle.sh --repo akratch/mgb64
```

That helper is non-destructive. It creates the clean single-root repository,
smoke-tests its source archive, exports scrubbed launch labels/issues, captures
GitHub-side blocker evidence, and writes a `PUBLIC_LAUNCH_BUNDLE.md` manifest
with the exact commands to review before any destructive repository operation.
When a local ROM is available outside the generated launch checkout, use the
stronger form to also run strict ignored-artifact, deep runtime, archive, Docker
context, and macOS app asset-free validation from inside the generated clean
repository:

```sh
scripts/prepare_public_launch_bundle.sh \
  --repo akratch/mgb64 \
  --strict-preflight-rom /path/outside/clean-launch-repo/baserom.u.z64 \
  --strict-preflight-macos-app
```

4. Rename the current private GitHub repository out of the way:

```sh
gh repo rename -R akratch/mgb64 "mgb64-prepublic-$(date +%Y%m%d)" --yes
```

5. Create a fresh private repository with the launch name:

```sh
gh repo create akratch/mgb64 \
  --private \
  --disable-wiki \
  --description "A decompilation and native source port of a 1997 Nintendo 64 first-person shooter, for research & preservation. Bring your own ROM - no copyrighted assets included."
```

6. Push only the clean single-root public branch:

```sh
git -C /path/from/helper remote add launch-clean git@github.com:akratch/mgb64.git
git -C /path/from/helper push launch-clean HEAD:refs/heads/main
gh repo edit akratch/mgb64 --default-branch main
cd /path/from/helper
```

7. Restore repository settings:

```sh
scripts/configure_github_launch_settings.sh --repo akratch/mgb64
```

Review the dry-run output. Add `--yes` only after the fresh repository exists,
hosted CI can start, and the planned settings match the launch policy:

```sh
scripts/configure_github_launch_settings.sh --repo akratch/mgb64 --yes
```

The helper configures repository settings, repository Actions permissions
including read-only default workflow tokens, full-SHA action pinning, and
14-day artifact/log retention, recommended security endpoints when GitHub
exposes them, and `main` branch protection with `Release hygiene` and
`CMake build (Linux)` required and up to date before merge.

8. Recreate launch issues and labels from the scrubbed export:

```sh
python3 tools/export_github_launch_items.py apply \
  --repo akratch/mgb64 \
  --input-dir /tmp/mgb64-launch-items \
  --yes
```

Do not migrate closed PRs, old generated status comments, or pre-rewrite commit
references. Recreate any still-relevant launch blockers manually with fresh
current-repo evidence.

9. Verify the fresh repository:

```sh
git ls-remote launch-clean 'refs/heads/*' 'refs/tags/*' 'refs/pull/*'
./scripts/check_github_launch_ready.sh --repo akratch/mgb64 --allow-private
python3 tools/check_public_history_paths.py --repo-root .
```

The local history-provenance, pull-ref, workflow-history, public-text,
release-asset, Actions-artifact, Actions-permission, and public
commit-reference sections must pass. The remaining expected dry-run failures
before launch are repository privacy, hosted Actions startup if billing/settings
are still blocked, and private/pro-only security settings that GitHub does not
expose until public/pro settings are available.
The launch-readiness check compares the local `HEAD` directly against the
GitHub repository's `main` ref, so it is safe to run from the generated clean
launch repository even if that checkout's local `origin` still points at a
temporary validation remote.

## Final Public Flip

After the fresh/purged repository has green current-head CI and branch/security
settings are configured:

```sh
scripts/release_preflight.sh \
  --deep-runtime \
  --rom /path/outside/repo/baserom.u.z64 \
  --macos-app \
  --strict-ignored \
  --github
```

Then run the launch checker one more time while the repository is still private:

```sh
NO_COLOR=1 scripts/check_github_launch_ready.sh --repo akratch/mgb64 --allow-private
```

Only after that dry run has no launch blockers other than private visibility,
change visibility to public:

```sh
gh repo edit akratch/mgb64 \
  --visibility public \
  --accept-visibility-change-consequences
```

Finally verify the public surface without the private-repo allowance:

```sh
NO_COLOR=1 scripts/check_github_launch_ready.sh --repo akratch/mgb64
```
