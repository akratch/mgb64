#!/bin/bash
#
# verify_all.sh -- the S-Tier Faithfulness Program's unified verify suite (the RATCHET).
#
# Reads docs/fidelity/verify_manifest.txt (an ordered, tier-partitioned list of gate
# invocations), runs each gate under the charter-rule-6 determinism envelope, captures a
# per-gate log, and emits a machine-readable verdict:
#
#   docs/fidelity/reports/verify_<git-sha>.json
#     { sha, started, gates: [{name, cmd, status: pass|fail|skip, seconds, log}],
#       verdict: green|degraded|red, skip_count, fail_count, strict, skipped_reason? }
#
# A gate may be SKIPPED only for a missing LICENSING-BOUND prerequisite (missing-ROM /
# missing-ares) -- charter rule 9 requires every skip be listed in the report; a skip alone
# never turns the verdict red (the gate could not run, it did not fail). BUT a skip is not
# a pass either (C4, 2026-07-10 review): if every gate in the requested tier range SKIPped,
# a ROM-less/ares-less CI run has silently degraded to whatever ran (often just tier-1
# static checks) while still claiming full coverage. So the verdict has three values:
#
#   green    -- fail_count == 0 and skip_count == 0 (every gate that was supposed to run,
#               ran, and passed).
#   degraded -- fail_count == 0 but skip_count > 0 (nothing failed, but N gate(s) could not
#               run -- coverage is incomplete). Printed as "degraded: N gate(s) skipped".
#   red      -- fail_count > 0 (something actually failed).
#
# Exit code: 0 for green; 0 for degraded UNLESS GE007_VERIFY_STRICT=1 is set, in which case
# degraded exits 1 too (the release gate sets this so a ROM-less/ares-less environment can
# never rubber-stamp a release); 1 for red always, strict or not.
#
# A missing/unconfigured build/ tree (no `cmake -B build` yet) is NOT a skip class (I1,
# 2026-07-10 review, superseding an earlier draft of this file that briefly treated it as
# one): tier 1 is defined ROM-free specifically so it always runs, and unlike ROM/ares --
# licensing-bound prerequisites nobody but the owner can produce -- every runner can
# `cmake -B build` for free. So a missing build/ is a hard FAIL in ALL modes, strict or
# not (see gate_hardfail_reason below), with a one-line actionable message telling the
# runner exactly which command to run.
#
# Usage:
#   tools/fidelity/verify_all.sh [--tier N] [--manifest PATH] [--report-dir DIR]
#
#   --tier N        run tiers <= N (default: all tiers present in the manifest).
#                   The loop uses --tier 1 for inner-iteration checks; the full suite
#                   runs before committing any sim-touching change.
#
#   GE007_VERIFY_STRICT=1   (env) treat a "degraded" (skip-only) verdict as a failure --
#                   for release-oriented callers where a silent coverage drop must hard-fail
#                   instead of passing through as green/degraded-but-exit-0.
#
# The manifest is APPEND-ONLY: adding a gate = appending a line; deleting a gate requires a
# waiver ledger entry (charter rule 4 -- gates only get stricter). This script executes the
# manifest verbatim, so the manifest is the single source of truth for what "green" means.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=tools/validation_common.sh
source "${SCRIPT_DIR}/../validation_common.sh"

REPO_ROOT="$(validation_repo_root)"
cd "$REPO_ROOT"

MANIFEST="docs/fidelity/verify_manifest.txt"
REPORT_DIR="docs/fidelity/reports"
MAX_TIER=0   # 0 => run every tier present in the manifest

usage() {
    sed -n '2,40p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --tier) MAX_TIER="$2"; shift 2 ;;
        --tier=*) MAX_TIER="${1#*=}"; shift ;;
        --manifest) MANIFEST="$2"; shift 2 ;;
        --report-dir) REPORT_DIR="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "verify_all: unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ ! -f "$MANIFEST" ]]; then
    echo "verify_all: manifest not found: $MANIFEST" >&2
    exit 2
fi
if [[ ! "$MAX_TIER" =~ ^[0-9]+$ ]]; then
    echo "verify_all: --tier must be a non-negative integer, got: $MAX_TIER" >&2
    exit 2
fi

mkdir -p "$REPORT_DIR"

# --- run identity -----------------------------------------------------------------------
SHA="$(git rev-parse --short=12 HEAD 2>/dev/null || echo unknown)"
if [[ -n "$(git status --porcelain 2>/dev/null || true)" ]]; then
    SHA="${SHA}-dirty"
fi
STARTED="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
REPORT_JSON="${REPORT_DIR}/verify_${SHA}.json"

ROM_PATH="$(validation_default_rom)"
TIMEOUT_BIN="$(validation_resolve_timeout_cmd)"

# Per-gate wall-clock ceiling (seconds). Tier 1 is cheap; tiers 2-3 exercise the full ROM
# smoke + regression + parity captures and legitimately run for many minutes.
gate_timeout_for_tier() {
    case "$1" in
        1) printf '%s\n' "${MGB64_VERIFY_TIER1_TIMEOUT:-900}" ;;
        *) printf '%s\n' "${MGB64_VERIFY_TIERN_TIMEOUT:-5400}" ;;
    esac
}

# Derive a stable, human-readable gate name from a command line.
gate_name_for() {
    local cmd="$1" first base
    # shellcheck disable=SC2086
    set -- $cmd
    first="$1"
    base="$(basename "$first")"
    base="${base%.sh}"
    if [[ "$base" == "ctest" ]]; then
        if [[ "$cmd" == *" -E "* ]]; then
            base="ctest_rom_free"
        elif [[ "$cmd" == *" -R "* ]]; then
            base="ctest_rom_gated"
        fi
    fi
    printf '%s\n' "$base" | tr -c 'A-Za-z0-9_' '_' | sed 's/_\{1,\}/_/g; s/^_//; s/_$//'
}

# Hard-fail prerequisite check for a gate. Prints "" if the gate is not gated on a
# not-yet-configured build/ tree, else a FAIL reason string. NOT a skip class (I1,
# 2026-07-10 review): tier 1 is ROM-free precisely so it always runs, and a build is
# producible by every runner (unlike the licensing-bound ROM/ares prerequisites below) --
# so a missing build/ must hard-stop the loop (charter Appendix B REPAIR trigger), not
# quietly degrade to exit 0.
gate_hardfail_reason() {
    local cmd="$1"
    # ctest-based gates (the bulk of tier 1) need a configured build/ tree --
    # ctest's own test list is generated at `cmake -B build` time, so a fresh,
    # build-less checkout would otherwise make every one of these report the same
    # generic FAIL for the same root cause. Likewise the D7 Release-config assert
    # (tools/fidelity/check_release_build.sh) reads build/CMakeCache.txt directly,
    # and check_sim_render_separation.sh nm's the built game objects (it already
    # exits non-zero with "build the native port first" for the same reason --
    # this just lets verify_all record ONE clear FAIL instead of a wall of identical
    # generic ones from every gate that shares the same root cause).
    # Keyed on the exact substrings the manifest actually uses, so this can't
    # accidentally swallow an unrelated command that merely mentions "build".
    if [[ "$cmd" == *"--test-dir build"* || "$cmd" == *check_release_build.sh* \
          || "$cmd" == *check_sim_render_separation.sh* ]]; then
        if [[ ! -f build/CMakeCache.txt ]]; then
            printf 'missing-build: build/ is not configured -- run: cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build   (tier 1 is ROM-free and must always be able to run; a missing build/ is a hard FAIL, not a degradable skip)\n'
            return
        fi
    fi
    printf ''
}

# Prerequisite check for a gate. Prints "" if runnable, else a skip reason string.
# Charter rule: skips allowed ONLY for missing-ROM / missing-ares prerequisites --
# licensing-bound assets the loop cannot produce for itself. A missing build/ tree is
# handled separately, above, as a hard FAIL (not a skip); by the time this function runs,
# gate_hardfail_reason has already ruled that case out for every gate it can fire on.
gate_skip_reason() {
    local tier="$1" cmd="$2"
    # M1 (2026-07-10 review): on a multi-config generator (Xcode / Ninja Multi-Config),
    # CMAKE_BUILD_TYPE is deliberately left blank in the cache -- this project's
    # CMakeLists.txt only forces CMAKE_BUILD_TYPE=Release (CACHE ... FORCE) for
    # single-config generators (the `_ge007_multi_config` guard), so a genuinely blank
    # value here can only happen on multi-config, never on the mainstream single-config
    # path. That's a real ambiguity check_release_build.sh itself can't resolve without a
    # --binary/--config hint (it already reports this as its own exit 2 "SKIP"), not a
    # missing prerequisite -- so pre-detect it here too, or the generic nonzero-rc-means-
    # FAIL runtime loop below would turn the script's own SKIP into a spurious FAIL.
    if [[ "$cmd" == *check_release_build.sh* && -f build/CMakeCache.txt ]]; then
        local build_type
        build_type="$(sed -n 's/^CMAKE_BUILD_TYPE:[A-Za-z]*=//p' build/CMakeCache.txt | head -1)"
        if [[ -z "$build_type" ]]; then
            printf 'ambiguous-build-type: CMAKE_BUILD_TYPE is blank in build/CMakeCache.txt (multi-config generator; run check_release_build.sh --binary PATH directly if you need this assert)\n'
            return
        fi
    fi
    # Tier >= 2 gates drive the built binary against the retail ROM.
    if [[ "$tier" -ge 2 && ! -e "$ROM_PATH" ]]; then
        printf 'missing-ROM: %s not present\n' "$ROM_PATH"
        return
    fi
    # ares-dependent lanes (none in the initial manifest; future sense lanes append here).
    if [[ "$cmd" == *ares* ]]; then
        local ares_bin="build/ares-movement-oracle"
        if [[ ! -d "$ares_bin" ]]; then
            printf 'missing-ares: instrumented oracle build not present (%s)\n' "$ares_bin"
            return
        fi
    fi
    printf ''
}

# --- run each manifest gate -------------------------------------------------------------
RECORDS="$(mktemp "${TMPDIR:-/tmp}/verify_records_XXXXXX")"
trap 'rm -f "$RECORDS"' EXIT

declare -i FAIL_COUNT=0
declare -i SKIP_COUNT=0
declare -a SKIP_REASONS=()
declare -a SEEN_NAMES=()

current_tier=0
gate_index=0

emit_record() {
    # name<TAB>cmd<TAB>status<TAB>seconds<TAB>log
    printf '%s\t%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" "$5" >>"$RECORDS"
}

echo "verify_all: sha=${SHA} tier<=$( [[ "$MAX_TIER" -eq 0 ]] && echo all || echo "$MAX_TIER") manifest=${MANIFEST}"

while IFS= read -r rawline || [[ -n "$rawline" ]]; do
    line="${rawline%$'\r'}"
    # tier header?  e.g.  "# tier 1 — static + unit ..."
    if [[ "$line" =~ ^#[[:space:]]*tier[[:space:]]+([0-9]+) ]]; then
        current_tier="${BASH_REMATCH[1]}"
        continue
    fi
    # blank or (whole-line) comment -> skip. This also drops the commented "appended by
    # later tasks" gate lines, which stay in the manifest as placeholders.
    trimmed="${line#"${line%%[![:space:]]*}"}"   # left-trim
    [[ -z "$trimmed" ]] && continue
    [[ "$trimmed" == \#* ]] && continue

    # strip any inline trailing comment (commands contain no '#')
    cmd="${trimmed%%#*}"
    cmd="${cmd%"${cmd##*[![:space:]]}"}"          # right-trim
    [[ -z "$cmd" ]] && continue

    # honor --tier: only run gates in tiers <= MAX_TIER (0 = all)
    if [[ "$MAX_TIER" -ne 0 && "$current_tier" -gt "$MAX_TIER" ]]; then
        continue
    fi

    gate_index=$((gate_index + 1))
    name="$(gate_name_for "$cmd")"
    [[ -z "$name" ]] && name="gate_${gate_index}"
    # de-duplicate names across the manifest
    n=1; base_name="$name"
    while printf '%s\n' "${SEEN_NAMES[@]:-}" | grep -qxF "$name"; do
        n=$((n + 1)); name="${base_name}_${n}"
    done
    SEEN_NAMES+=("$name")

    log_path="${REPORT_DIR}/verify_${SHA}_${name}.log"

    hardfail_reason="$(gate_hardfail_reason "$cmd")"
    if [[ -n "$hardfail_reason" ]]; then
        echo "  [tier ${current_tier}] FAIL ${name}: ${hardfail_reason}"
        {
            echo "FAILED (prerequisite missing -- not a degradable skip): ${hardfail_reason}"
            echo "cmd: ${cmd}"
        } >"$log_path"
        emit_record "$name" "$cmd" "fail" "0" "$log_path"
        FAIL_COUNT=$((FAIL_COUNT + 1))
        continue
    fi

    skip_reason="$(gate_skip_reason "$current_tier" "$cmd")"
    if [[ -n "$skip_reason" ]]; then
        echo "  [tier ${current_tier}] SKIP ${name}: ${skip_reason}"
        {
            echo "SKIPPED: ${skip_reason}"
            echo "cmd: ${cmd}"
        } >"$log_path"
        emit_record "$name" "$cmd" "skip" "0" "$log_path"
        SKIP_COUNT=$((SKIP_COUNT + 1))
        SKIP_REASONS+=("${name}: ${skip_reason}")
        continue
    fi

    to="$(gate_timeout_for_tier "$current_tier")"
    echo "  [tier ${current_tier}] RUN  ${name} (timeout ${to}s): ${cmd}"

    start=$(date +%s)
    # Run the gate under the determinism envelope (validation_automation_env) with a wall
    # clock ceiling (validation_run_with_timeout). Both are validation_common.sh helpers;
    # they exec real programs, so we bridge them through a sub-shell that re-sources the
    # helpers and applies the envelope to `bash -c "$cmd"`.
    set +e
    validation_run_with_timeout "$to" \
        bash -c 'source "$0"; validation_automation_env bash -c "$1"' \
        "${SCRIPT_DIR}/../validation_common.sh" "$cmd" \
        >"$log_path" 2>&1
    rc=$?
    set -e
    end=$(date +%s)
    seconds=$((end - start))

    if [[ "$rc" -eq 0 ]]; then
        echo "       PASS ${name} (${seconds}s)"
        emit_record "$name" "$cmd" "pass" "$seconds" "$log_path"
    else
        if [[ "$rc" -eq 124 || "$rc" -eq 137 ]]; then
            echo "       FAIL ${name} (${seconds}s) — TIMED OUT (rc=${rc}); log: ${log_path}"
        else
            echo "       FAIL ${name} (${seconds}s, rc=${rc}); log: ${log_path}"
        fi
        emit_record "$name" "$cmd" "fail" "$seconds" "$log_path"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
done <"$MANIFEST"

# --- verdict + JSON ---------------------------------------------------------------------
# C4 (2026-07-10 review): a SKIP is not a pass. green requires zero fails AND zero skips;
# any skip with zero fails is "degraded" (amber) -- ratchet coverage silently dropped, but
# nothing actually broke. A single fail is always red, skips or not.
if [[ "$FAIL_COUNT" -gt 0 ]]; then
    VERDICT="red"
elif [[ "$SKIP_COUNT" -gt 0 ]]; then
    VERDICT="degraded"
else
    VERDICT="green"
fi

# GE007_VERIFY_STRICT=1 (release gate) promotes a "degraded" verdict to a hard failure --
# see file header. Any non-empty value other than "0"/"" counts as strict, matching the
# GE007_* boolean-flag convention used throughout the codebase (validation_common.sh).
STRICT=0
case "${GE007_VERIFY_STRICT:-}" in
    ""|0) STRICT=0 ;;
    *) STRICT=1 ;;
esac

SKIPPED_REASON=""
if [[ "$SKIP_COUNT" -gt 0 ]]; then
    SKIPPED_REASON="$(IFS='; '; printf '%s' "${SKIP_REASONS[*]}")"
fi

VERIFY_SHA="$SHA" VERIFY_STARTED="$STARTED" VERIFY_VERDICT="$VERDICT" \
VERIFY_SKIPPED_REASON="$SKIPPED_REASON" VERIFY_RECORDS="$RECORDS" \
VERIFY_SKIP_COUNT="$SKIP_COUNT" VERIFY_FAIL_COUNT="$FAIL_COUNT" VERIFY_STRICT="$STRICT" \
VERIFY_TIER_LIMIT="$MAX_TIER" VERIFY_GATE_COUNT="$gate_index" \
python3 - "$REPORT_JSON" <<'PY'
import json, os, sys

out_path = sys.argv[1]
gates = []
with open(os.environ["VERIFY_RECORDS"], encoding="utf-8") as fh:
    for raw in fh:
        raw = raw.rstrip("\n")
        if not raw:
            continue
        name, cmd, status, seconds, log = raw.split("\t", 4)
        gates.append({
            "name": name,
            "cmd": cmd,
            "status": status,
            "seconds": int(seconds),
            "log": log,
        })

report = {
    "sha": os.environ["VERIFY_SHA"],
    "started": os.environ["VERIFY_STARTED"],
    "gates": gates,
    "verdict": os.environ["VERIFY_VERDICT"],
    "fail_count": int(os.environ["VERIFY_FAIL_COUNT"]),
    "skip_count": int(os.environ["VERIFY_SKIP_COUNT"]),
    "strict": bool(int(os.environ["VERIFY_STRICT"])),
    # tier_limit: 0 == full coverage (all tiers in the manifest ran); a positive
    # N means only tiers <= N ran (--tier N). The publish gate requires 0, so a
    # fast-lane partial run can never be laundered into a release-grade report.
    "tier_limit": int(os.environ["VERIFY_TIER_LIMIT"]),
    "gate_count": int(os.environ["VERIFY_GATE_COUNT"]),
}
skipped = os.environ.get("VERIFY_SKIPPED_REASON", "")
if skipped:
    report["skipped_reason"] = skipped

with open(out_path, "w", encoding="utf-8") as fh:
    json.dump(report, fh, indent=2)
    fh.write("\n")
print(out_path)
PY

echo "verify_all: verdict=${VERDICT} (pass=$((gate_index - FAIL_COUNT - SKIP_COUNT)) fail=${FAIL_COUNT} skip=${SKIP_COUNT})"
if [[ "$VERDICT" == "degraded" ]]; then
    echo "verify_all: degraded: ${SKIP_COUNT} gate(s) skipped -- ${SKIPPED_REASON}"
fi
echo "verify_all: report=${REPORT_JSON}"

# Exit status: red always fails. degraded fails only under GE007_VERIFY_STRICT=1 (the
# release gate). green always passes.
if [[ "$VERDICT" == "red" ]]; then
    exit 1
elif [[ "$VERDICT" == "degraded" && "$STRICT" -eq 1 ]]; then
    echo "verify_all: GE007_VERIFY_STRICT=1 -- degraded verdict treated as failure" >&2
    exit 1
fi
exit 0
