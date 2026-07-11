#!/usr/bin/env bash
#
# publish_public.sh -- THE one guarded entrypoint for any push to the public
# remote (akratch/mgb64). Nothing else pushes public: not a raw `git push
# origin main`, not release.sh, not CI.
#
# Why this exists (owner ruling, 2026-07-11, docs/fidelity/ESCALATIONS.md,
# resolved C1/D1): the already-public history is accepted as-is; the boundary is
# FORWARD-ONLY and MECHANIZED. `.gitattributes export-ignore` is the single
# source of truth for internal trees; every public push flows through this
# script, which composes the release-ready + public-history-text guards, a
# strict verify-report gate, and (for releases) an owner gameplay attestation on
# macOS AND Windows. It NEVER rewrites history and NEVER filters content -- it
# accepts the tree as-is and lets the guards catch the never-leak classes
# (private paths, personal email, credentials, ROM/media, proprietary notices).
#
# It NEVER force-pushes. Without --yes it is a non-destructive dry run that
# prints exactly what would be pushed.
#
# See docs/RELEASING.md ("The publish gate") and docs/WORKFLOW.md for the
# operational doctrine.
#
# Usage:
#   scripts/publish_public.sh [options]
#
# Modes:
#   (default)            RELEASE push -- requires --confirm-gameplay (macOS AND
#                        Windows owner gameplay verification).
#   --dev-push           Non-release push to main. Skips ONLY the gameplay gate;
#                        every other guard (clean tree, release-ready,
#                        history-text, strict verify green) still runs.
#
# Push target:
#   --remote NAME        Remote to push to. Default: origin.
#   --branch NAME        Branch to update on the remote. Default: main.
#   --tag NAME           Also push this EXISTING local tag (implies RELEASE).
#
# Verify gate:
#   (automatic)          Requires docs/fidelity/reports/verify_<sha>.json for the
#                        current HEAD, verdict "green".
#   --verify-report PATH Adjudicate with a specific report instead of the
#                        auto-located one. If its verdict is not "green",
#                        --red-note is REQUIRED and is logged into the push
#                        annotation.
#   --red-note "TEXT"    Owner adjudication note for a non-green verify override.
#
# Gameplay gate (RELEASE only):
#   --confirm-gameplay "macos=<initials/date>,windows=<initials/date>"
#
# Execution:
#   --yes                Actually perform the push. Without it, dry-run only.
#   -h | --help          This help.
#
# Test seams (do not use for real publishes):
#   PUBLISH_RELEASE_READY_CMD   Override the release-ready guard command.
#   PUBLISH_HISTORY_TEXT_CMD    Override the public-history-text guard command.
#   PUBLISH_REPORTS_DIR         Override the verify-report / annotation directory.
#
# Exit codes:
#   0  success (dry run OK, or push completed)
#   2  usage / bad arguments
#   3  dirty working tree
#   4  a hygiene guard failed (release-ready or history-text)
#   5  verify-report missing or not green (and not adjudicated)
#   6  gameplay attestation missing or malformed
#   7  push would be non-fast-forward (would need a force -- refused)
#
set -euo pipefail

prog="publish_public"
die() { printf '%s: ERROR: %s\n' "$prog" "$1" >&2; exit "${2:-1}"; }
info() { printf '%s: %s\n' "$prog" "$1"; }

usage() { sed -n '2,66p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

# --- Args --------------------------------------------------------------------
mode="release"          # release | dev
remote="origin"
branch="main"
tag=""
verify_report=""
red_note=""
gameplay=""
do_push=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dev-push)          mode="dev"; shift ;;
    --remote)            remote="${2:?--remote needs a value}"; shift 2 ;;
    --branch)            branch="${2:?--branch needs a value}"; shift 2 ;;
    --tag)               tag="${2:?--tag needs a value}"; shift 2 ;;
    --verify-report)     verify_report="${2:?--verify-report needs a path}"; shift 2 ;;
    --red-note)          red_note="${2:?--red-note needs text}"; shift 2 ;;
    --confirm-gameplay)  gameplay="${2:?--confirm-gameplay needs a value}"; shift 2 ;;
    --yes)               do_push=1; shift ;;
    -h|--help)           usage; exit 0 ;;
    # Force pushing is categorically refused -- there is no flag for it.
    --force|-f|--force-with-lease|--force-with-lease=*|--mirror)
      die "force pushing is never allowed through this path (ruling: forward-only, no rewrite)" 2 ;;
    *) die "unknown argument: $1 (see --help)" 2 ;;
  esac
done

# A tag push is by definition a release.
[[ -n "$tag" ]] && mode="release"

# --- Repo context ------------------------------------------------------------
root="$(git rev-parse --show-toplevel 2>/dev/null)" || die "not inside a git worktree" 2
cd "$root"

sha_full="$(git rev-parse HEAD)"
sha="$(git rev-parse --short=12 HEAD)"   # matches tools/fidelity/verify_all.sh keying
reports_dir="${PUBLISH_REPORTS_DIR:-$root/docs/fidelity/reports}"

release_ready_cmd="${PUBLISH_RELEASE_READY_CMD:-$root/scripts/ci/check_release_ready.sh}"
history_text_cmd="${PUBLISH_HISTORY_TEXT_CMD:-$root/scripts/ci/check_public_history_text.sh}"

info "HEAD ${sha} (${sha_full})"
info "mode=${mode} target=${remote}/${branch}${tag:+ tag=${tag}}"

# --- (a) Refuse a dirty tree -------------------------------------------------
# Publishing an untracked/modified tree means the push would not match what the
# guards and verify report were computed against.
if [[ -n "$(git status --porcelain)" ]]; then
  git status --short >&2
  die "working tree is dirty -- commit or stash before publishing" 3
fi

# --- (b) Hygiene guards (both modes; never skipped) --------------------------
info "running release-ready guard: ${release_ready_cmd}"
if ! bash "$release_ready_cmd"; then
  die "release-ready guard failed -- refusing to publish" 4
fi
info "running public-history-text guard: ${history_text_cmd}"
if ! bash "$history_text_cmd"; then
  die "public-history-text guard failed -- refusing to publish" 4
fi

# --- (c) Strict verify-report gate -------------------------------------------
# Default: docs/fidelity/reports/verify_<sha>.json must exist and be "green".
# Override: --verify-report PATH; if that report is not green, --red-note is
# mandatory and is recorded in the push annotation.
default_report="${reports_dir}/verify_${sha}.json"
report_path="${verify_report:-$default_report}"

if [[ ! -f "$report_path" ]]; then
  if [[ -n "$verify_report" ]]; then
    die "verify report not found: ${report_path}" 5
  fi
  die "no strict verify report for HEAD ${sha}: expected ${default_report}
       run 'GE007_VERIFY_STRICT=1 tools/fidelity/verify_all.sh' at this commit,
       or adjudicate with --verify-report <path> --red-note \"...\"" 5
fi

# Parse verdict + embedded sha with python3 (the repo's JSON tool everywhere).
read -r report_verdict report_sha < <(python3 - "$report_path" <<'PY'
import json, sys
try:
    with open(sys.argv[1], encoding="utf-8") as fh:
        d = json.load(fh)
    print(d.get("verdict", "MISSING"), d.get("sha", "MISSING"))
except Exception as e:
    print("PARSE_ERROR", "PARSE_ERROR")
PY
)
[[ "$report_verdict" == "PARSE_ERROR" ]] && die "verify report is not valid JSON: ${report_path}" 5
info "verify report: ${report_path} verdict=${report_verdict} sha=${report_sha}"

adjudicated=0
if [[ "$report_verdict" == "green" ]]; then
  # A green report must be for THIS commit, or it is not evidence about HEAD.
  if [[ "$report_sha" != "$sha" && "$report_sha" != "MISSING" ]]; then
    die "verify report sha (${report_sha}) does not match HEAD (${sha}) -- stale report" 5
  fi
else
  # Non-green: only a deliberate --verify-report adjudication with a written
  # red-note may proceed. The note is logged into the push annotation.
  if [[ -z "$verify_report" ]]; then
    die "verify verdict is '${report_verdict}', not green -- fix verify, or adjudicate
       with --verify-report ${report_path} --red-note \"why this is safe to ship\"" 5
  fi
  [[ -n "$red_note" ]] || die "non-green verify override requires --red-note \"...\"" 5
  adjudicated=1
  info "ADJUDICATED non-green verify (${report_verdict}): ${red_note}"
fi

# --- (d) Gameplay attestation (RELEASE only) ---------------------------------
if [[ "$mode" == "release" ]]; then
  [[ -n "$gameplay" ]] || die "release push requires --confirm-gameplay \"macos=<initials/date>,windows=<initials/date>\"
       (owner gameplay verification on macOS AND Windows -- ruling C1/D1)" 6
  # Require both platforms present with non-empty attestations.
  gp_macos="" ; gp_windows=""
  IFS=',' read -ra _pairs <<< "$gameplay"
  for pair in "${_pairs[@]}"; do
    key="${pair%%=*}"; val="${pair#*=}"
    key="${key// /}"
    case "$key" in
      macos)   gp_macos="$val" ;;
      windows) gp_windows="$val" ;;
    esac
  done
  [[ -n "${gp_macos// /}" ]]   || die "--confirm-gameplay missing a non-empty macos=<initials/date>" 6
  [[ -n "${gp_windows// /}" ]] || die "--confirm-gameplay missing a non-empty windows=<initials/date>" 6
  info "gameplay attested: macos=${gp_macos} windows=${gp_windows}"
else
  info "dev push: gameplay gate skipped (guards + strict verify still enforced)"
fi

# --- (e/f) Compute + print the exact rev range; refuse non-fast-forward -------
remote_ref="refs/remotes/${remote}/${branch}"
range_desc=""
if git rev-parse --verify --quiet "$remote_ref" >/dev/null; then
  base="$(git rev-parse "$remote_ref")"
  if [[ "$base" == "$sha_full" ]]; then
    info "nothing to push: ${remote}/${branch} already at ${sha}"
    exit 0
  fi
  if ! git merge-base --is-ancestor "$base" HEAD; then
    die "push would be non-fast-forward (${remote}/${branch} has commits not in HEAD).
       This path never force-pushes; rebase onto ${remote}/${branch} first." 7
  fi
  range_desc="${remote}/${branch} (${base:0:12}) .. HEAD (${sha})"
  echo
  info "commits that would be pushed to ${remote}/${branch}:"
  git --no-pager log --oneline "${base}..HEAD"
else
  range_desc="new branch ${remote}/${branch} <- HEAD (${sha})"
  echo
  info "no local remote-tracking ref for ${remote}/${branch}."
  info "  ('git fetch ${remote}' to preview the exact delta; git push still refuses non-fast-forward)"
  info "recent commits at HEAD:"
  git --no-pager log --oneline -n 20 HEAD
fi
[[ -n "$tag" ]] && { git rev-parse --verify --quiet "refs/tags/${tag}" >/dev/null || die "tag not found locally: ${tag} (create it with 'git tag -a ${tag}' first)" 2; }

echo
info "PLANNED PUSH: ${range_desc}${tag:+  + tag ${tag}}"
info "  git push ${remote} HEAD:refs/heads/${branch}${tag:+  &&  git push ${remote} refs/tags/${tag}}"

# --- Execute (only with --yes) -----------------------------------------------
if [[ "$do_push" -ne 1 ]]; then
  echo
  info "DRY RUN -- nothing pushed. Re-run with --yes to publish."
  exit 0
fi

# Record the push annotation (durable, local, internal -- reports_dir is
# gitignored + export-ignored). The red-note adjudication lands here.
mkdir -p "$reports_dir"
annotation_log="${reports_dir}/publish_annotations.log"
python3 - "$annotation_log" <<PY
import json, os, sys, time
rec = {
    "ts": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
    "sha": "${sha}",
    "sha_full": "${sha_full}",
    "mode": "${mode}",
    "remote": "${remote}",
    "branch": "${branch}",
    "tag": "${tag}",
    "verify_report": os.path.basename("${report_path}"),
    "verify_verdict": "${report_verdict}",
    "verify_adjudicated": bool(${adjudicated}),
    "red_note": """${red_note}""",
    "gameplay": """${gameplay}""",
}
with open(sys.argv[1], "a", encoding="utf-8") as fh:
    fh.write(json.dumps(rec) + "\n")
print("annotation ->", sys.argv[1])
PY

echo
info "pushing HEAD -> ${remote}/${branch} (no force)"
git push "$remote" "HEAD:refs/heads/${branch}"
if [[ -n "$tag" ]]; then
  info "pushing tag ${tag} -> ${remote} (no force)"
  git push "$remote" "refs/tags/${tag}"
fi
echo
info "published. annotation recorded in ${annotation_log}"
