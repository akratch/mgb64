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
```

7. Restore repository settings:

```sh
gh repo edit akratch/mgb64 \
  --enable-issues \
  --enable-discussions \
  --enable-wiki=false \
  --delete-branch-on-merge \
  --allow-update-branch \
  --enable-squash-merge \
  --enable-merge-commit \
  --enable-rebase-merge \
  --add-topic bring-your-own-rom,decompilation,game-preservation,n64,native-port,nintendo-64,opengl,reverse-engineering,sdl2,source-port
```

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
python3 tools/check_public_history_paths.py --repo-root /path/from/helper
```

The local history-provenance, pull-ref, workflow-history, public-text,
release-asset, and public commit-reference sections must pass. The remaining
expected dry-run failures before launch are repository privacy, hosted Actions
startup if billing/settings are still blocked, and private/pro-only security
settings that GitHub does not expose until public/pro settings are available.

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

Only then change visibility to public.
