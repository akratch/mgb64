#!/usr/bin/env bash
#
# test_publish_public.sh -- ROM-free unit tests for scripts/publish_public.sh.
#
# Exercises every refusal path plus the happy-path dry-run and a real push,
# entirely inside a throwaway scratch repo with a LOCAL BARE repo standing in
# for the public remote. It never touches origin or any network remote: the
# guards are stubbed via PUBLISH_RELEASE_READY_CMD / PUBLISH_HISTORY_TEXT_CMD,
# the verify-report dir via PUBLISH_REPORTS_DIR, and the only push target is the
# local bare repo created in a tempdir.
#
# Wired as the ROM-free ctest `publish_public_guard` (see CMakeLists.txt).
#
set -euo pipefail

SCRIPT_UNDER_TEST="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/scripts/publish_public.sh"
[[ -f "$SCRIPT_UNDER_TEST" ]] || { echo "FATAL: not found: $SCRIPT_UNDER_TEST" >&2; exit 2; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/publish_public_test_XXXXXX")"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

pass=0; fail=0
ok()   { printf '  \033[32mPASS\033[0m %s\n' "$1"; pass=$((pass+1)); }
bad()  { printf '  \033[31mFAIL\033[0m %s\n' "$1"; fail=$((fail+1)); }

# Isolate git identity/config from the host.
export HOME="$WORK/home"; mkdir -p "$HOME"
export GIT_CONFIG_GLOBAL="$WORK/home/.gitconfig"
export GIT_CONFIG_SYSTEM=/dev/null
git config --global user.email "test@example.invalid"
git config --global user.name "Publish Test"
git config --global init.defaultBranch main
git config --global commit.gpgsign false
git config --global advice.detachedHead false

REPO="$WORK/repo"
BARE="$WORK/public.git"

# Guard stubs.
STUB_PASS="$WORK/stub_pass.sh"; printf '#!/usr/bin/env bash\nexit 0\n' > "$STUB_PASS"; chmod +x "$STUB_PASS"
STUB_FAIL="$WORK/stub_fail.sh"; printf '#!/usr/bin/env bash\necho "stub guard: RED" >&2\nexit 1\n' > "$STUB_FAIL"; chmod +x "$STUB_FAIL"

REPORTS="$WORK/reports"

# Rebuild a clean scratch repo + fresh bare remote before each test group.
setup_repo() {
  rm -rf "$REPO" "$BARE" "$REPORTS"
  mkdir -p "$REPO" "$REPORTS"
  git init -q "$REPO"
  ( cd "$REPO"
    echo "hello" > file.txt
    git add file.txt
    git commit -qm "initial commit"
  )
  git init -q --bare "$BARE"
  write_green_report
}

sha12() { ( cd "$REPO" && git rev-parse --short=12 HEAD ); }

write_green_report() {
  local s; s="$(sha12)"
  cat > "$REPORTS/verify_${s}.json" <<EOF
{ "sha": "${s}", "started": "now", "gates": [], "verdict": "green", "fail_count": 0, "skip_count": 0, "strict": true }
EOF
}

write_report_verdict() {  # verdict [sha-override]
  local s="${2:-$(sha12)}"
  cat > "$REPORTS/verify_$(sha12).json" <<EOF
{ "sha": "${s}", "started": "now", "gates": [], "verdict": "$1", "fail_count": 1, "skip_count": 0, "strict": true }
EOF
}

# Run the script under test from inside the scratch repo with stub env.
run() {
  ( cd "$REPO"
    PUBLISH_RELEASE_READY_CMD="${RR:-$STUB_PASS}" \
    PUBLISH_HISTORY_TEXT_CMD="${HT:-$STUB_PASS}" \
    PUBLISH_REPORTS_DIR="$REPORTS" \
    bash "$SCRIPT_UNDER_TEST" "$@"
  )
}

# assert_exit CODE "label" -- runs with $ARGS, captures rc+output.
OUT=""
expect() {  # expected_code label -- args...
  local want="$1" label="$2"; shift 2
  local rc; OUT="$( run "$@" 2>&1 )" && rc=0 || rc=$?
  if [[ "$rc" -eq "$want" ]]; then ok "$label (exit $rc)"; else
    bad "$label -- expected exit $want, got $rc"; printf '    --- output ---\n%s\n    --------------\n' "$OUT" | sed 's/^/    /'
  fi
}
expect_grep() {  # needle label
  local needle="$1" label="$2"
  if grep -qF -- "$needle" <<<"$OUT"; then ok "$label"; else bad "$label -- output missing: $needle"; fi
}

GAMEPLAY='macos=AK/2026-07-11,windows=AK/2026-07-11'

echo "== publish_public.sh refusal + happy-path tests =="

# --- Argument / force refusals (no repo needed, but set one up anyway) --------
setup_repo
expect 2 "unknown arg -> usage"                 --bogus
expect 2 "--force refused"                       --force --yes --dev-push
expect 2 "--force-with-lease refused"            --force-with-lease --dev-push

# --- (a) dirty tree -----------------------------------------------------------
setup_repo
echo "dirty" >> "$REPO/file.txt"
expect 3 "dirty working tree refused"            --dev-push
expect_grep "working tree is dirty" "dirty tree message"

# --- (b) hygiene guards -------------------------------------------------------
setup_repo
RR="$STUB_FAIL" expect 4 "release-ready guard red refused"   --dev-push
setup_repo
HT="$STUB_FAIL" expect 4 "history-text guard red refused"    --dev-push

# --- (c) verify-report gate ---------------------------------------------------
setup_repo
rm -f "$REPORTS"/verify_*.json
expect 5 "missing verify report refused"         --dev-push
expect_grep "no strict verify report" "missing-report message"

setup_repo
write_report_verdict red
expect 5 "non-green verify (no override) refused" --dev-push

setup_repo
write_report_verdict red
expect 5 "non-green override without red-note refused" --dev-push --verify-report "$REPORTS/verify_$(sha12).json"

setup_repo
write_green_report
# stale green report: sha field does not match HEAD
write_report_verdict green "deadbeefdead"
expect 5 "stale green report (sha mismatch) refused" --dev-push

# --- (d) gameplay gate --------------------------------------------------------
setup_repo
expect 6 "release push without gameplay refused"
expect_grep "requires --confirm-gameplay" "gameplay-required message"

setup_repo
expect 6 "gameplay missing windows refused"      --confirm-gameplay "macos=AK/2026-07-11"
setup_repo
expect 6 "gameplay missing macos refused"        --confirm-gameplay "windows=AK/2026-07-11"
setup_repo
expect 6 "gameplay empty macos value refused"    --confirm-gameplay "macos= ,windows=AK/2026-07-11"

# --- happy-path dry runs ------------------------------------------------------
setup_repo
expect 0 "dev-push dry run OK (skips gameplay)"  --dev-push
expect_grep "gameplay gate skipped" "dev-push skip message"
expect_grep "DRY RUN" "dev-push dry-run banner"

setup_repo
expect 0 "release dry run OK (with gameplay)"    --confirm-gameplay "$GAMEPLAY"
expect_grep "PLANNED PUSH" "release dry-run planned-push line"
expect_grep "DRY RUN" "release dry-run banner"

# adjudicated non-green override, dev-push dry run
setup_repo
write_report_verdict red
expect 0 "non-green override w/ red-note OK"      --dev-push --verify-report "$REPORTS/verify_$(sha12).json" --red-note "known-flaky gate X, ticket #99"
expect_grep "ADJUDICATED" "adjudication logged to output"

# --- rev-range preview against a populated remote -----------------------------
setup_repo
# Seed the bare remote's main with HEAD, then advance HEAD by one commit so the
# preview shows exactly the one new commit (fast-forward).
( cd "$REPO"
  git remote add pub "$BARE"
  git push -q pub HEAD:refs/heads/main
  echo "second" > second.txt && git add second.txt && git commit -qm "second commit"
  git fetch -q pub )
write_green_report   # HEAD moved, need a fresh green report
expect 0 "fast-forward preview OK"               --remote pub --dev-push
expect_grep "second commit" "preview lists the new commit"
expect_grep "PLANNED PUSH" "preview planned-push line"

# --- (e) non-fast-forward refusal ---------------------------------------------
setup_repo
( cd "$REPO"
  git remote add pub "$BARE"
  # Remote main gets a commit that HEAD does not contain.
  git push -q pub HEAD:refs/heads/main
  tmpc="$(git rev-parse HEAD)"
  git checkout -q -b _remote_only
  echo "remoteonly" > r.txt && git add r.txt && git commit -qm "remote-only commit"
  git push -q pub HEAD:refs/heads/main
  git checkout -q main
  git fetch -q pub )
write_green_report
expect 7 "non-fast-forward push refused"         --remote pub --dev-push
expect_grep "non-fast-forward" "non-ff message"

# --- (f) real push to the bare fake remote (--yes) ----------------------------
setup_repo
( cd "$REPO" && git remote add pub "$BARE" )
expect 0 "real push to fake remote (--yes)"      --remote pub --confirm-gameplay "$GAMEPLAY" --yes
remote_head="$( git --git-dir="$BARE" rev-parse main 2>/dev/null || echo MISSING )"
local_head="$( cd "$REPO" && git rev-parse HEAD )"
if [[ "$remote_head" == "$local_head" ]]; then ok "fake remote main advanced to HEAD"; else bad "fake remote main != HEAD ($remote_head vs $local_head)"; fi
if [[ -f "$REPORTS/publish_annotations.log" ]] && grep -q '"verify_verdict": "green"' "$REPORTS/publish_annotations.log"; then
  ok "push annotation recorded"
else bad "push annotation not recorded"; fi

# idempotent: second push finds nothing to do
( cd "$REPO" && git fetch -q pub )
expect 0 "second push is a no-op"                --remote pub --confirm-gameplay "$GAMEPLAY" --yes
expect_grep "nothing to push" "no-op message"

echo
echo "== publish_public.sh: ${pass} passed, ${fail} failed =="
[[ "$fail" -eq 0 ]] || exit 1
