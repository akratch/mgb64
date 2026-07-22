#!/usr/bin/env bash
# ROM-free fail-on-revert coverage for the FID-0144 Release-build guard.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
CHECK="$ROOT/tools/fidelity/check_release_build.sh"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/mgb64_release_guard_XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

BUILD="$WORK/build"
GAME="$BUILD/ge007"
GAME_OBJ="$BUILD/CMakeFiles/ge007.dir/src/game/lvl.c.o"
OTHER_OBJ="$BUILD/CMakeFiles/test_unrelated.dir/tests/test_unrelated.c.o"
mkdir -p "$(dirname "$GAME_OBJ")" "$(dirname "$OTHER_OBJ")"
printf 'CMAKE_BUILD_TYPE:STRING=Release\n' > "$BUILD/CMakeCache.txt"
printf 'game\n' > "$GAME"
printf 'game object\n' > "$GAME_OBJ"
printf 'unrelated test object\n' > "$OTHER_OBJ"

set_mtime() {
    python3 - "$1" "$2" <<'PY'
import os
import sys
os.utime(sys.argv[1], (int(sys.argv[2]), int(sys.argv[2])))
PY
}

expect_rc() {
    local want="$1" label="$2"
    shift 2
    local rc
    "$@" >"$WORK/out.log" 2>&1 && rc=0 || rc=$?
    if [[ "$rc" -ne "$want" ]]; then
        echo "FAIL: $label (expected rc=$want, got rc=$rc)" >&2
        sed -n '1,20p' "$WORK/out.log" >&2
        exit 1
    fi
    echo "PASS: $label"
}

# ge007 is newer than its own object. A newer object belonging to an unrelated
# unit-test executable must not make the Release guard false-red.
set_mtime "$GAME_OBJ" 100
set_mtime "$GAME" 200
set_mtime "$OTHER_OBJ" 300
expect_rc 0 "newer unrelated target object is ignored" \
    bash "$CHECK" --build-dir "$BUILD" --binary "$GAME"

# The guard must still catch the real stale-link shape: a ge007 target object
# newer than the ge007 executable.
set_mtime "$GAME_OBJ" 400
expect_rc 1 "newer ge007 target object fails closed" \
    bash "$CHECK" --build-dir "$BUILD" --binary "$GAME"

# Preserve the primary configuration contract too.
printf 'CMAKE_BUILD_TYPE:STRING=Debug\n' > "$BUILD/CMakeCache.txt"
set_mtime "$GAME" 500
expect_rc 1 "Debug build is rejected" \
    bash "$CHECK" --build-dir "$BUILD" --binary "$GAME"

echo "check_release_build fixture: PASS"
