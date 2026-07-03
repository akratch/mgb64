#!/usr/bin/env bash
#
# check_public_history_text.sh -- scan reachable git history for high-risk text.
#
# The current tree can be clean while older reachable commits still contain
# private paths, handoff notes, credentials, or proprietary notice text. Public
# launch exposes branch history, so this guard checks every reachable commit.
#
set -euo pipefail

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "SKIP: not a git checkout; no reachable history to scan."
  exit 0
fi

cd "$(git rev-parse --show-toplevel)"

private_text='(/Users/[^[:space:]]+|Desktop/dev|/home/adam|github\.com/akratch/007|private dev repo|contaminated private|Claude|session handoff|agent memory|must never go public)'
secret_text='(AKIA[0-9A-Z]{16}|ghp_[0-9A-Za-z_]{36,}|github_pat_[0-9A-Za-z_]+|xox[baprs]-[0-9A-Za-z-]+|sk-[A-Za-z0-9]{20,}|BEGIN (RSA|OPENSSH|EC|DSA) PRIVATE KEY)'
sdk_notice_text='(UNPUBLISHED[[:space:]]+PROPRI''ETARY|may not be disclo''sed|without the prior written (permission|cons''ent)|RESTRICTED[[:space:]]+RIG''HTS|subparagraph \(c\)\(1\)\(ii\) of the Rig''hts)'
pattern="(${private_text}|${secret_text}|${sdk_notice_text})"

# Path scope. The three guard scripts embed the detection patterns themselves, so
# they are excluded. Internal documentation is excluded too: the public/internal
# boundary is declared once in .gitattributes via `export-ignore`, and those docs
# never enter the public source archive (`git archive`) or the fresh launch
# repository (scripts/create_public_launch_repo.sh). Deriving the same exclusion
# set here means this guard scans exactly the text that can ever become public.
pathspecs=(
  .
  ':!scripts/ci/check_public_history_text.sh'
  ':!scripts/ci/check_no_rom_data.sh'
  ':!scripts/ci/check_release_ready.sh'
)
if [ -f .gitattributes ]; then
  while IFS= read -r pat; do
    [ -n "$pat" ] && pathspecs+=( ":(glob,exclude)${pat}" )
  done < <(awk '/^[[:space:]]*#/ { next } /(^|[[:space:]])export-ignore([[:space:]]|$)/ { print $1 }' .gitattributes)
fi

limit="${GE007_HISTORY_TEXT_HIT_LIMIT:-80}"
if ! history_output="$(git log --all -G"$pattern" \
  --pretty='format:commit %H %s' --name-only -- \
  "${pathspecs[@]}" 2>&1)"; then
  printf '%s\n' "$history_output" >&2
  exit 1
fi

hits="$(printf '%s\n' "$history_output" | awk 'NF' | sed -n "1,${limit}p")"

if [ -n "$hits" ]; then
  printf '%s\n' "$hits"
  echo "FAIL: high-risk text found in reachable git history." >&2
  echo "Rewrite or remove the reachable history before public launch." >&2
  exit 1
fi

echo "PASS: no high-risk private/proprietary text found in reachable git history"
