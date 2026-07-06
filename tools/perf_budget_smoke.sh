#!/bin/bash
#
# perf_budget_smoke.sh -- CTest-compatible wrapper that runs the per-level
# frame-time census (tools/perf_census.sh) and enforces budgets
# (tools/perf_budget_check.py). This is the M0 regression gate described in
# docs/design/PERFORMANCE_PLAN.md §6.
#
# HARD FAIL if any level drops below the 60 fps floor (16.6 ms). Target is
# 120 fps (8.3 ms), reported as WARN unless --strict.
#
# Reference-hardware sensitive: absolute frame times depend on the GPU/driver.
# Intended for local/manual gating on a known machine, not shared CI. Registered
# opt-in under PORT_VALIDATION_TESTS.
#
# Usage:
#   ./tools/perf_budget_smoke.sh                       # all levels, default budgets
#   ./tools/perf_budget_smoke.sh --levels "jungle dam" # subset
#   ./tools/perf_budget_smoke.sh --hard-ms 16.6 --target-ms 8.3 --strict
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
LEVELS=""
HARD_MS="16.6"
TARGET_MS="8.3"
STRICT=0
ALLOW_MISSING=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --levels) LEVELS="$2"; shift 2 ;;
        --hard-ms) HARD_MS="$2"; shift 2 ;;
        --target-ms) TARGET_MS="$2"; shift 2 ;;
        --strict) STRICT=1; shift ;;
        --allow-missing) ALLOW_MISSING="$2"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 2 ;;
    esac
done

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
else
    BINARY="$(validation_resolve_path "$BINARY")"
fi
ROM="$(validation_resolve_path "$ROM")"

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"
validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT

CSV="$(mktemp -t perf_census.XXXXXX.csv)"
echo "[perf-budget] running census (binary=$BINARY)..."
# perf_census.sh honors BIN + CENSUS_OUT; ROM is auto-discovered next to the repo.
BIN="$BINARY" CENSUS_OUT="$CSV" bash tools/perf_census.sh ${LEVELS:+$LEVELS}

CHECK_ARGS=(--hard-ms "$HARD_MS" --target-ms "$TARGET_MS" --baseline baselines/perf_census_baseline.csv)
[[ "$STRICT" -eq 1 ]] && CHECK_ARGS+=(--strict)
[[ -n "$ALLOW_MISSING" ]] && CHECK_ARGS+=(--allow-missing "$ALLOW_MISSING")

echo "[perf-budget] checking budgets..."
python3 tools/perf_budget_check.py "$CSV" "${CHECK_ARGS[@]}"
