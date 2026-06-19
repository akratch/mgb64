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
smoke_archive=0
jobs="${GE007_BUILD_JOBS:-4}"
max_warnings=0
report=""

usage() {
  cat <<'USAGE'
Usage: scripts/create_public_launch_repo.sh [--out DIR] [--message MESSAGE] [--smoke-archive]

Creates a fresh local git repository with a single root commit containing the
current HEAD tree, then runs public release/history guards inside that repo.

The output repository is intended for replacement dry runs. It does not include
old commits, hidden pull-request refs, local ignored files, or generated assets.

Options:
  --out DIR             Output repository directory. Must be empty or absent.
  --message MESSAGE     Root commit message.
  --smoke-archive       Build and test the source archive from the clean repo.
  --jobs N              Parallel jobs for archive smoke (default: GE007_BUILD_JOBS or 4).
  --max-warnings N      Archive warning threshold (default: 0).
  --report PATH         Write a Markdown evidence report to PATH.
USAGE
}

require_non_negative_int() {
  local name="$1"
  local value="$2"
  case "$value" in
    ''|*[!0-9]*)
      echo "${name} must be a non-negative integer, got: ${value}" >&2
      exit 2
      ;;
  esac
}

require_positive_int() {
  local name="$1"
  local value="$2"
  require_non_negative_int "$name" "$value"
  if [ "$value" -eq 0 ]; then
    echo "${name} must be a positive integer, got: ${value}" >&2
    exit 2
  fi
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
    --smoke-archive)
      smoke_archive=1
      shift
      ;;
    --jobs)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      jobs="$2"
      shift 2
      ;;
    --max-warnings)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      max_warnings="$2"
      shift 2
      ;;
    --report)
      [ "$#" -ge 2 ] || { usage >&2; exit 2; }
      report="$2"
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

require_positive_int "--jobs" "$jobs"
require_non_negative_int "--max-warnings" "$max_warnings"

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
archive_info="$tmp/archive-info.tsv"
git init --bare "$bare" >/dev/null
launch_commit="$(
  GIT_AUTHOR_NAME="MGB64 Launch Builder" \
  GIT_AUTHOR_EMAIL="mgb64-launch@example.invalid" \
  GIT_COMMITTER_NAME="MGB64 Launch Builder" \
  GIT_COMMITTER_EMAIL="mgb64-launch@example.invalid" \
  git commit-tree HEAD^{tree} -m "$message"
)"
git push --quiet "$bare" "${launch_commit}:refs/heads/main"
git --git-dir="$bare" symbolic-ref HEAD refs/heads/main
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

  if [ "$smoke_archive" -eq 1 ]; then
    echo
    echo "== Clean launch source archive smoke =="
    ./scripts/make_public_source_archive.sh --force
    archive="dist/mgb64-$(git rev-parse --short=12 HEAD).tar.gz"
    ./scripts/smoke_public_source_archive.sh "$archive" --jobs "$jobs" --max-warnings "$max_warnings"
    archive_sha="$(shasum -a 256 "$archive" | awk '{ print $1 }')"
    printf '%s\t%s\n' "$archive" "$archive_sha" > "$archive_info"
  fi
)

if [ -z "$report" ]; then
  report="$(dirname "$out")/mgb64-clean-launch-report.md"
fi
mkdir -p "$(dirname "$report")"

archive_path=""
archive_sha=""
if [ -s "$archive_info" ]; then
  IFS=$'\t' read -r archive_path archive_sha < "$archive_info"
fi

{
  printf '# MGB64 Clean Launch Repository Evidence\n\n'
  printf '%s\n' "- Generated: \`$(date -u '+%Y-%m-%dT%H:%M:%SZ')\`"
  printf '%s\n' "- Source repository: \`$(pwd)\`"
  printf '%s\n' "- Source HEAD: \`${source_head}\`"
  printf '%s\n' "- Source tree: \`${source_tree}\`"
  printf '%s\n' "- Launch repository: \`${out}\`"
  printf '%s\n' "- Launch HEAD: \`$(git -C "$out" rev-parse HEAD)\`"
  printf '%s\n' "- Launch tree: \`$(git -C "$out" rev-parse HEAD^{tree})\`"
  printf '%s\n' "- Launch reachable commits: \`$(git -C "$out" rev-list --all --count)\`"
  printf '%s\n' "- Launch HEAD parent count: \`$(git -C "$out" rev-list --parents -n 1 HEAD | awk '{ print NF - 1 }')\`"
  printf '%s\n' "- Launch working tree status: \`clean\`"
  if [ -n "$archive_path" ]; then
    printf '%s\n' "- Source archive: \`${out}/${archive_path}\`"
    printf '%s\n' "- Source archive SHA-256: \`${archive_sha}\`"
  else
    printf '%s\n' "- Source archive smoke: not requested"
  fi
  printf '\n## Required Follow-Up\n\n'
  printf '%s\n' "Before publishing, push only this launch repository to a fresh GitHub repository and verify:"
  printf '\n```sh\n'
  printf '%s\n' "git -C \"$out\" ls-remote <new-remote> 'refs/heads/*' 'refs/tags/*' 'refs/pull/*'"
  printf '%s\n' "cd \"$out\""
  printf '%s\n' "scripts/check_github_launch_ready.sh --repo akratch/mgb64 --allow-private"
  printf '```\n'
} > "$report"

cat <<EOF

Created clean launch repository:
  $out

Launch HEAD:
  $(git -C "$out" rev-parse HEAD)

Source HEAD used:
  $source_head

Evidence report:
  $report

Verify remote refs before publishing from this repository:
  git -C "$out" ls-remote <new-remote> 'refs/heads/*' 'refs/tags/*' 'refs/pull/*'
EOF
