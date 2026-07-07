# Development Workflow — single source of truth

This repository has **one** source of truth: the public repo
[`akratch/mgb64`](https://github.com/akratch/mgb64). All development happens here,
in the open. There is no private "staging" fork, and there is no second `main`.

## Why this doc exists

For a while the project carried two disjoint repos:

- a **private** `mgb64-prepublic-*` staging repo (the original full-history dev tree), and
- the **public** `akratch/mgb64` repo (a curated snapshot that the releases were cut from).

The two shared **no common git ancestor**, so nothing could flow between them except
manual cherry-picks. In practice that meant fixes landed on one side and were silently
missing from the other (e.g. the multi-ammo-crate collect fix shipped on public but never
reached private, so local builds cut from private re-exhibited a bug that was "already
fixed"). The private tree was also perpetually *behind* public on shipped code while
carrying ~11k lines of history and curated-out artifacts nobody wanted.

We collapsed to a single lineage: **public is the source of truth**; the private repo was
archived read-only. This document is the guardrail that keeps us from re-introducing a
second lineage.

## The rules

1. **One remote, one `main`.** `origin` points at `akratch/mgb64`. Local `main` tracks
   `origin/main`. Never add a second long-lived "staging" remote that develops in parallel.
2. **Branch → PR → merge.** Never commit directly to `main`. Cut a topic branch
   (`fix/…`, `feat/…`, `docs/…`), push it, open a PR, and merge it into `main`. This is the
   "develop in the open" model the v0.3.x releases already used.
3. **Every commit passes the contamination guard.** The `.githooks` pre-commit guard
   rejects ROM/ROM-derived data in tracked files. Keep it installed
   (`git config core.hooksPath .githooks`). No ROM bytes, extracted assets, or captured
   traces/screenshots are ever committed — see `.gitignore` and
   `docs/RENDERING_REGRESSION_NOTES.md` ("must stay local").
4. **Releases are tags on `main`.** `vX.Y.Z` tags live on `public/main` only. Cut releases
   from `main`, never from a fork.
5. **Keep `main` releasable.** Merge only branches that build and pass the smoke/ctest
   gates. Land focused PRs; don't stack unrelated work on one branch (the local `main`
   briefly accumulated eight unrelated fixes before this cleanup — that's the anti-pattern).

## Day-to-day

```sh
git switch main && git pull                 # main == origin/main == akratch/mgb64
git switch -c fix/whatever                   # topic branch
# … edit, build (cmake --build build --target ge007), test …
git commit                                   # contamination guard runs here
git push -u origin fix/whatever
gh pr create --base main                     # open the PR
# review, then squash-or-merge into main; delete the topic branch
```

## The archived private repo

The old private `mgb64-prepublic-*` repo is **archived (read-only)** for historical
reference only. It is NOT a development target. If you ever need something from it, cherry-
pick the specific commit onto a public topic branch and run it through the normal PR + guard
path — never merge its history back in (the histories are disjoint by design and a merge
would drag the curated-out artifacts back into public).

## If you find code "missing" from public

That should no longer happen, because there is only one lineage. If some old private-only
change is ever wanted, treat it like any external patch: extract just that change, apply it
on a topic branch off `main`, verify it's contamination-clean and builds, and PR it.
