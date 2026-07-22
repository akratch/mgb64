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
# Exit 2: one of two "can't assert" shapes, both surfaced this way when the script runs
#         standalone -- inside verify_all.sh, each is pre-detected before this script is
#         even invoked, and handled differently (see below):
#           (a) build/ is not configured yet (no CMakeCache.txt). NOT a degradable skip
#               inside verify_all.sh (I1, 2026-07-10 review): its gate_hardfail_reason()
#               detects this ahead of running the command and records a hard FAIL --
#               tier 1 is ROM-free and must always be able to run, and every runner can
#               produce a build for free (unlike the licensing-bound ROM/ares
#               prerequisites, which remain degradable skips).
#           (b) build/ IS configured, but CMAKE_BUILD_TYPE is blank in the cache (a
#               multi-config generator -- Xcode / Ninja Multi-Config -- deliberately
#               leaves it blank; see the CMAKE_BUILD_TYPE read below). This genuinely
#               can't be resolved from the cache alone. verify_all.sh's gate_skip_reason()
#               pre-detects this specific case (M1, 2026-07-10 review) and records it as
#               a degradable SKIP, since it is a real ambiguity, not a missing build.
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
            sed -n '2,43p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
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
         "(multi-config generator: build type is chosen per \`cmake --build --config X\`," \
         " not recorded in the cache -- pass --binary to point at the configured binary" \
         " if you need this assert)" >&2
    exit 2
fi

if [[ "$BUILD_TYPE" != "Release" ]]; then
    echo "check_release_build: FAIL -- detected build type is ${BUILD_TYPE}, required is" \
         "Release -- sim-hash baselines are Release-only; reconfigure and rebuild with:" \
         "cmake -B ${BUILD_DIR} -DCMAKE_BUILD_TYPE=Release && cmake --build ${BUILD_DIR}" >&2
    exit 1
fi

if [[ ! -e "$BINARY" ]]; then
    echo "check_release_build: PASS -- build/ is configured Release; no binary at" \
         "${BINARY} yet (nothing to falsely redden)"
    exit 0
fi

# Is the binary a CURRENT LINK? The reliable staleness signal is the object
# files from ITS TARGET, NOT CMakeCache.txt's mtime: a `cmake -B build` RECONFIGURE
# touches the cache (and leaves a perpetual `cmake_check_build_system` phony
# dep, so `make -q` never reports up-to-date) without changing what the binary
# links -- so a cache-mtime or make-query heuristic FALSE-REDs on the most
# common workflow (reconfigure, then verify), which is exactly the false-RED
# footgun D7 exists to kill. Object files only change on a real (re)compile, so
# a binary newer than every .o it links is a current link that reflects the
# cache's CMAKE_BUILD_TYPE=Release confirmed above. A binary OLDER than some .o
# is genuinely stale (a real recompile happened without a relink) -> FAIL.
#
# FID-0144: never scan all of CMakeFiles here. Unit-test targets are independent
# executables and may legitimately recompile/relink after ge007; comparing their
# objects to ge007 falsely reports a stale game binary after a completely clean
# `cmake --build`. CMake's per-target object directory is stable across the
# Makefiles and Ninja generators. Strip the Windows .exe suffix when deriving it.
_target_name="$(basename "$BINARY")"
_target_name="${_target_name%.exe}"
_obj_root="${BUILD_DIR}/CMakeFiles/${_target_name}.dir"
if [[ -d "$_obj_root" ]]; then
    _stale_obj="$(find "$_obj_root" -name '*.o' -newer "$BINARY" 2>/dev/null | head -1)"
    if [[ -n "$_stale_obj" ]]; then
        echo "check_release_build: FAIL -- ${BINARY} is older than a compiled object" \
             "(${_stale_obj}) -- a source recompiled without relinking the binary;" \
             "rebuild with: cmake --build ${BUILD_DIR}" >&2
        exit 1
    fi
fi

echo "check_release_build: PASS -- ${BINARY} is Release (build/CMakeCache.txt" \
     "CMAKE_BUILD_TYPE=Release; binary is a current link, newer than every" \
     "${_target_name} target object)"
exit 0
