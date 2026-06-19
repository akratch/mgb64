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
  --strict-preflight-macos-app \
  --strict-preflight-macos-app-bundle-sdl2
```

Add `--strict-preflight-macos-app-strict-deployment-target` only when
`pkg-config` points at a controlled SDL2 build with the intended minimum macOS
version.

4. Before any destructive operation, verify the clean launch checkout and the
   current GitHub `main` still match the reviewed bundle:

```sh
repo=akratch/mgb64
clean_repo=/path/from/helper
expected_source_head="SOURCE_HEAD_FROM_PUBLIC_LAUNCH_BUNDLE"
expected_clean_head="CLEAN_LAUNCH_HEAD_FROM_PUBLIC_LAUNCH_BUNDLE"
expected_clean_tree="CLEAN_LAUNCH_TREE_FROM_PUBLIC_LAUNCH_BUNDLE"
test "$(git -C "$clean_repo" rev-list --all --count)" = "1"
test -z "$(git -C "$clean_repo" status --porcelain --untracked-files=all)"
test "$(gh api "repos/${repo}/git/ref/heads/main" --jq .object.sha)" = "$expected_source_head"
test "$(git -C "$clean_repo" rev-parse HEAD)" = "$expected_clean_head"
test "$(git -C "$clean_repo" rev-parse HEAD^{tree})" = "$expected_clean_tree"
```

The generated `PUBLIC_LAUNCH_BUNDLE.md` includes these concrete values and a
copy/pasteable guarded command block. Use those generated commands for the
actual replacement so a stale bundle cannot pass accidentally.

5. Rename the current private GitHub repository out of the way:

```sh
backup_name="mgb64-prepublic-$(date -u +%Y%m%d-%H%M%S)"
gh repo rename -R "$repo" "$backup_name" --yes
```

6. Create a fresh private repository with the launch name:

```sh
gh repo create "$repo" \
  --private \
  --disable-wiki \
  --description "A decompilation and native source port of a 1997 Nintendo 64 first-person shooter, for research & preservation. Bring your own ROM - no copyrighted assets included."
```

7. Push only the clean single-root public branch:

```sh
git -C "$clean_repo" remote add launch-clean "git@github.com:${repo}.git"
git -C "$clean_repo" push launch-clean HEAD:refs/heads/main
gh repo edit "$repo" --default-branch main
cd "$clean_repo"
```

8. Restore repository settings:

```sh
scripts/configure_github_launch_settings.sh --repo "$repo"
```

Review the dry-run output. Add `--yes` only after the fresh repository exists
and the planned settings match the local-CI launch policy:

```sh
scripts/configure_github_launch_settings.sh --repo "$repo" --yes
```

The helper configures repository settings, disables hosted GitHub Actions for
the local-CI launch policy, configures recommended security endpoints when
GitHub exposes them, and applies `main` branch protection without required
hosted status checks.

9. Recreate launch issues and labels from the scrubbed export:

```sh
python3 tools/export_github_launch_items.py apply \
  --repo "$repo" \
  --input-dir /tmp/mgb64-launch-items \
  --yes
```

Do not migrate closed PRs, old generated status comments, or pre-rewrite commit
references. Recreate any still-relevant launch blockers manually with fresh
current-repo evidence.

10. Verify the fresh repository:

```sh
git ls-remote launch-clean 'refs/heads/*' 'refs/tags/*' 'refs/pull/*'
./scripts/check_github_launch_ready.sh --repo akratch/mgb64 --allow-private
python3 tools/check_public_history_paths.py --repo-root .
```

The local history-provenance, pull-ref, workflow-history, public-text,
release-asset, Actions-artifact, local-CI/Actions-policy, and public
commit-reference sections must pass. The remaining expected dry-run failures
before launch are repository privacy and private/pro-only security settings that
GitHub does not expose until public/pro settings are available.
The launch-readiness check compares the local `HEAD` directly against the
GitHub repository's `main` ref, so it is safe to run from the generated clean
launch repository even if that checkout's local `origin` still points at a
temporary validation remote.

## Final Public Flip

After the fresh/purged repository has hosted Actions disabled and any
GitHub-exposed branch/security settings configured, run the final pre-public
local gate:

```sh
scripts/release_preflight.sh \
  --deep-runtime \
  --rom /path/outside/repo/baserom.u.z64 \
  --macos-app-bundle-sdl2 \
  --strict-ignored \
  --github \
  --allow-private
```

Add `--macos-app-strict-deployment-target` only when `pkg-config` points at a
controlled SDL2 build with the intended minimum macOS version.

Then run the launch checker one more time while the repository is still private:

```sh
NO_COLOR=1 scripts/check_github_launch_ready.sh --repo akratch/mgb64 --allow-private
```

Only after that dry run has no launch blockers other than private visibility
plus branch/security endpoints that GitHub only exposes after the public flip,
change visibility to public:

```sh
gh repo edit akratch/mgb64 \
  --visibility public \
  --accept-visibility-change-consequences
```

Immediately re-apply repository settings in case branch/security endpoints were
unavailable while private, then verify the public surface without the
private-repo allowance:

```sh
scripts/configure_github_launch_settings.sh --repo akratch/mgb64 --yes
NO_COLOR=1 scripts/check_github_launch_ready.sh --repo akratch/mgb64
```
