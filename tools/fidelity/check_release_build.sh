#!/usr/bin/env bash
#
# check_release_build.sh -- D7 rail: assert the configured build is Release BEFORE
# any sim-hash lane runs.
#
# The sim-state hash baselines (docs/fidelity/README.md "Verifying") are recorded
# under a Release build and are FP-optimization sensitive. A Debug (or stale,
# reconfigured-since-last-build) binary reddens every sim-hash lane with a
# misleading "sim diverged" -- the sim didn't diverge, the binary just isn't the
# build the baseline was recorded under. This is a cheap, early tier-1 gate so
# that footgun surfaces as ONE clear message instead of a wall of false-positive
# sim-hash diffs; registered in docs/fidelity/verify_manifest.txt BEFORE the
# sim-hash / regression lanes run.
#
# Mechanism (zero code-change, per the D7 plan item): read CMAKE_BUILD_TYPE
# straight out of build/CMakeCache.txt -- no need to run the binary or add a
# --print-build-config flag to it. Also compare the binary's mtime against the
# cache's: a binary older than the cache predates the last `cmake` (re)configure,
# so it may not reflect the CMAKE_BUILD_TYPE the cache currently records (e.g.
# reconfigured Debug -> Release but never rebuilt) -- flagged as its own failure
# rather than silently trusting a stale artifact.
#
# Usage: tools/fidelity/check_release_build.sh [--build-dir DIR] [--binary PATH]
#
# Exit 0: build is configured Release and (if built) the binary is newer than the
#         configure, or no binary exists yet (nothing to falsely redden).
# Exit 2: build/ is not configured yet (no CMakeCache.txt) -- a legitimate
#         prerequisite-missing case; verify_all.sh's gate_skip_reason() detects
#         this ahead of running the command and records it as SKIP, not FAIL (see
#         its "missing-build" prerequisite class). Running this script directly
#         (outside verify_all) surfaces the same condition as exit 2.
# Exit 1: build IS configured, but not Release, or the binary predates the
#         configure -- a real, actionable failure with a one-line fix.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=tools/validation_common.sh
source "${SCRIPT_DIR}/../validation_common.sh"

REPO_ROOT="$(validation_repo_root)"
cd "$REPO_ROOT"

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --build-dir=*) BUILD_DIR="${1#*=}"; shift ;;
        --binary) BINARY="$2"; shift 2 ;;
        --binary=*) BINARY="${1#*=}"; shift ;;
        -h|--help)
            sed -n '2,33p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "check_release_build: unknown arg: $1" >&2; exit 2 ;;
    esac
done

CACHE="${BUILD_DIR}/CMakeCache.txt"
if [[ ! -f "$CACHE" ]]; then
    echo "check_release_build: SKIP -- ${CACHE} not present (build/ not configured;" \
         "run: cmake -B ${BUILD_DIR} -DCMAKE_BUILD_TYPE=Release)" >&2
    exit 2
fi

[[ -n "$BINARY" ]] || BINARY="$(validation_binary_path "$BUILD_DIR")"

# CMAKE_BUILD_TYPE:STRING=Release / :UNINITIALIZED=Release / etc. -- the cache-entry
# TYPE token varies, the VALUE after '=' is what we want.
BUILD_TYPE="$(sed -n 's/^CMAKE_BUILD_TYPE:[A-Za-z]*=//p' "$CACHE" | head -1)"

if [[ -z "$BUILD_TYPE" ]]; then
    # Multi-config generator (Xcode / Ninja Multi-Config): CMAKE_BUILD_TYPE is
    # deliberately blank in the cache -- the config is chosen per `cmake --build
    # --config X`, not recorded here. This project's CMakeLists.txt only forces
    # CMAKE_BUILD_TYPE=Release for single-config generators (see the
    # `_ge007_multi_config` guard), so a blank value is a real ambiguity, not a
    # defect -- report it plainly rather than guess.
    echo "check_release_build: SKIP -- CMAKE_BUILD_TYPE is blank in ${CACHE}" \
         "(multi-config generator: build type is chosen per \`cmake --build ---config\`," \
         " not recorded in the cache -- pass --binary to point at the configured binary" \
         " if you need this assert)" >&2
    exit 2
fi

if [[ "$BUILD_TYPE" != "Release" ]]; then
    echo "check_release_build: FAIL -- binary is ${BUILD_TYPE} -- sim-hash baselines" \
         "are Release-only; rebuild with -DCMAKE_BUILD_TYPE=Release" >&2
    exit 1
fi

if [[ ! -e "$BINARY" ]]; then
    echo "check_release_build: PASS -- build/ is configured Release; no binary at" \
         "${BINARY} yet (nothing to falsely redden)"
    exit 0
fi

# Binary predates the current configure -> it may have been built under a PRIOR
# (e.g. Debug) config that was since reconfigured to Release without a rebuild.
if [[ "$CACHE" -nt "$BINARY" ]]; then
    echo "check_release_build: FAIL -- ${BINARY} predates the current cmake configure" \
         "(${CACHE} is newer) -- it may not reflect CMAKE_BUILD_TYPE=Release; rebuild" \
         "with: cmake --build ${BUILD_DIR}" >&2
    exit 1
fi

echo "check_release_build: PASS -- ${BINARY} is Release (build/CMakeCache.txt" \
     "CMAKE_BUILD_TYPE=Release, binary newer than the configure)"
exit 0
