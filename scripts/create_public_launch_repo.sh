#!/usr/bin/env bash
#
# create_public_launch_repo.sh -- build a fresh single-root launch repository
# from the current committed tree.
#
# This is non-destructive: it writes a separate repository under /tmp unless
# --out is provided. Use it to validate the repository-replacement path before
# changing any GitHub repository state.
#
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

out=""
message="Initial public source release"

usage() {
  cat <<'USAGE'
Usage: scripts/create_public_launch_repo.sh [--out DIR] [--message MESSAGE]

Creates a fresh local git repository with a single root commit containing the
current HEAD tree, then runs public release/history guards inside that repo.

The output repository is intended for replacement dry runs. It does not include
old commits, hidden pull-request refs, local ignored files, or generated assets.
USAGE
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --out)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      out="$2"
      shift 2
      ;;
    --message)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      message="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      usage >&2
      exit 2
      ;;
  esac
done

dirty="$(git status --porcelain --untracked-files=all)"
if [ -n "$dirty" ]; then
  echo "Refusing to create a launch repo from a dirty checkout:" >&2
  printf '%s\n' "$dirty" >&2
  exit 1
fi
source_head="$(git rev-parse HEAD)"
source_tree="$(git rev-parse HEAD^{tree})"

if [ -z "$out" ]; then
  out="$(mktemp -d "${TMPDIR:-/tmp}/mgb64-public-launch.XXXXXX")/repo"
fi

if [ -e "$out" ] && [ -n "$(find "$out" -mindepth 1 -maxdepth 1 2>/dev/null | sed -n '1p')" ]; then
  echo "Output directory already exists and is not empty: $out" >&2
  exit 1
fi

tmp="$(mktemp -d "${TMPDIR:-/tmp}/mgb64-public-launch-objects.XXXXXX")"
bare="$tmp/remote.git"
git init --bare "$bare" >/dev/null
launch_commit="$(
  GIT_AUTHOR_NAME="MGB64 Launch Builder" \
  GIT_AUTHOR_EMAIL="mgb64-launch@example.invalid" \
  GIT_COMMITTER_NAME="MGB64 Launch Builder" \
  GIT_COMMITTER_EMAIL="mgb64-launch@example.invalid" \
  git commit-tree HEAD^{tree} -m "$message"
)"
git push --quiet "$bare" "${launch_commit}:refs/heads/main"
if [ -n "$(git ls-remote "$bare" 'refs/pull/*' 'refs/tags/*')" ]; then
  echo "Temporary launch remote unexpectedly advertises pull-request or tag refs." >&2
  git ls-remote "$bare" 'refs/pull/*' 'refs/tags/*' >&2
  exit 1
fi
git clone --quiet "$bare" "$out"

(
  cd "$out"
  launch_head="$(git rev-parse HEAD)"
  launch_tree="$(git rev-parse HEAD^{tree})"
  commit_count="$(git rev-list --all --count)"
  parent_count="$(git rev-list --parents -n 1 HEAD | awk '{ print NF - 1 }')"
  launch_status="$(git status --porcelain --untracked-files=all)"

  if [ "$launch_head" != "$launch_commit" ]; then
    echo "Launch repository HEAD mismatch: got $launch_head, expected $launch_commit" >&2
    exit 1
  fi
  if [ "$launch_tree" != "$source_tree" ]; then
    echo "Launch repository tree mismatch: got $launch_tree, expected $source_tree from $source_head" >&2
    exit 1
  fi
  if [ "$commit_count" -ne 1 ]; then
    echo "Launch repository must contain exactly one reachable commit, got $commit_count" >&2
    exit 1
  fi
  if [ "$parent_count" -ne 0 ]; then
    echo "Launch repository HEAD must be a root commit, got $parent_count parent(s)" >&2
    exit 1
  fi
  if [ -n "$launch_status" ]; then
    echo "Launch repository checkout is dirty:" >&2
    printf '%s\n' "$launch_status" >&2
    exit 1
  fi

  echo "== Clean launch repository invariants =="
  echo "  OK -- launch tree matches source HEAD tree ($source_tree)."
  echo "  OK -- launch history contains exactly one root commit."
  echo "  OK -- launch checkout is clean."

  ./scripts/ci/check_release_ready.sh
  python3 tools/check_public_history_paths.py --repo-root .
)

cat <<EOF

Created clean launch repository:
  $out

Launch HEAD:
  $(git -C "$out" rev-parse HEAD)

Source HEAD used:
  $source_head

Verify remote refs before publishing from this repository:
  git -C "$out" ls-remote <new-remote> 'refs/heads/*' 'refs/tags/*' 'refs/pull/*'
EOF
