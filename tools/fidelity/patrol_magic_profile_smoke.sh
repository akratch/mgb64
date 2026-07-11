#!/usr/bin/env bash
#
# patrol_magic_profile_smoke.sh — permanent guard for the FID-0014 faithful
# WAYMODE_MAGIC patrol semantics.
#
# Retail keeps unseen patrolling guards FROZEN at their pad in WAYMODE_MAGIC
# (chrTickBeams US 0x7F020EF0 magic dispatch .L7F0211C4: no anim tick / root
# motion), walking them virtually and teleporting pad-to-pad on virtual-walk
# completion (chrlvTravelTickMagic US 0x7F028600). The legacy NATIVE_PORT
# workarounds made every Dam patroller creep-walk continuously instead
# (0% paused, no warps) — FID-0054's Layer B patrol divergence.
#
# This lane captures the native-only dam_combat_guard6_agealign route twice and
# asserts, via tools/fidelity/patrol_profile.py over the pre-onset window
# (ticks 0..1386):
#   fix ON (default):   chr45 (fully-unseen patroller) paused >= 90% with a
#                       >= 1000u pad warp; chr41/42/43 paused >= 50% — the
#                       stock pause/warp profile class
#                       (docs/fidelity/derivations/FID-0014-patrol-magic.md §4;
#                       chr45's warp steps are bit-identical to the pinned
#                       stock capture).
#   fix OFF (GE007_NO_PATROL_MAGIC_FIX=1, negative control): chr45 paused <=
#                       5% with NO >= 1000u warp — proves the lane detects a
#                       workaround revert / flag-polarity bug (fail-on-revert).
#
# ROM-gated, ares-free (native binary + ROM only). The runtime lock is taken by
# movement_oracle_capture.sh per capture — do not take it here (mkdir mutex is
# not reentrant).
#
# Usage:
#   tools/fidelity/patrol_magic_profile_smoke.sh [--build-dir DIR] [--rom PATH]
#                                                [--binary PATH] [--no-build]
#                                                [--timeout SECONDS]
set -euo pipefail
cd "$(dirname "$0")/../.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=0
TIMEOUT_SECONDS=400
ROUTE="dam_combat_guard6_agealign"
TICK_HI=1386

usage() {
    sed -n '2,36p' "$0"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --build) DO_BUILD=1; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        -h|--help) usage ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

if [[ ! -f "$ROM" ]]; then
    echo "SKIP: ROM not found at $ROM (local-only lane)" >&2
    exit 77
fi

if [[ "$DO_BUILD" == "1" ]]; then
    validation_configure_build
    validation_build
fi

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
fi
if [[ ! -x "$BINARY" ]]; then
    echo "FAIL: native binary not found/executable: $BINARY" >&2
    echo "  (build first: cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build)" >&2
    exit 1
fi

OUT_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/mgb64_patrol_magic_XXXXXX")"
trap 'rm -rf "$OUT_ROOT"' EXIT INT TERM

capture() {  # $1 = out subdir; extra env comes from the caller's exports
    local out="$OUT_ROOT/$1"
    tools/movement_oracle_capture.sh --route "$ROUTE" --native-only \
        --native-full-trace --no-compare --no-build --binary "$BINARY" \
        --rom "$ROM" --out-dir "$out" --timeout "$TIMEOUT_SECONDS" \
        > "$out.log" 2>&1 || {
        echo "FAIL: native capture failed ($1); tail of log:" >&2
        tail -20 "$out.log" >&2
        exit 1
    }
    echo "$out/native_${ROUTE}.jsonl"
}

assert_profile() {  # $1 = json path, $2 = side label ("on"|"off")
    python3 - "$1" "$2" <<'PYEOF'
import json, sys
prof = json.load(open(sys.argv[1]))["guards"]
side = sys.argv[2]
def g(cn):
    d = prof.get(str(cn))
    if d is None:
        print(f"FAIL: chr {cn} absent from profile", file=sys.stderr)
        sys.exit(1)
    return d
rc = 0
def check(cond, msg):
    global rc
    if not cond:
        print("FAIL: " + msg, file=sys.stderr)
        rc = 1
c45 = g(45)
big_warps = [w for w in c45["warp_sizes"] if w >= 1000.0]
if side == "on":
    check(c45["paused_pct"] >= 90.0,
          f"fix-ON chr45 paused_pct {c45['paused_pct']} < 90 (magic freeze regressed)")
    check(len(big_warps) >= 1,
          f"fix-ON chr45 has no >=1000u pad warp (warp_sizes={c45['warp_sizes']})")
    for cn in (41, 42, 43):
        d = g(cn)
        check(d["paused_pct"] >= 50.0,
              f"fix-ON chr{cn} paused_pct {d['paused_pct']} < 50 (magic freeze regressed)")
else:
    check(c45["paused_pct"] <= 5.0,
          f"fix-OFF chr45 paused_pct {c45['paused_pct']} > 5 (legacy path not restored; "
          "negative control broken)")
    check(len(big_warps) == 0,
          f"fix-OFF chr45 shows >=1000u warps {big_warps} (legacy path not restored)")
print(f"profile[{side}]: chr45 paused={c45['paused_pct']}% warps>=1000u={len(big_warps)}")
sys.exit(rc)
PYEOF
}

echo "== patrol-magic profile: fix ON (default) =="
unset GE007_NO_PATROL_MAGIC_FIX 2>/dev/null || true  # ON side must not inherit the flag
TRACE_ON="$(capture on)"
python3 tools/fidelity/patrol_profile.py "$TRACE_ON" --tick-hi "$TICK_HI" \
    --json-out "$OUT_ROOT/profile_on.json" | sed 's/^/  /'
assert_profile "$OUT_ROOT/profile_on.json" on

echo "== patrol-magic profile: fix OFF (GE007_NO_PATROL_MAGIC_FIX=1, negative control) =="
export GE007_NO_PATROL_MAGIC_FIX=1
TRACE_OFF="$(capture off)"
unset GE007_NO_PATROL_MAGIC_FIX
python3 tools/fidelity/patrol_profile.py "$TRACE_OFF" --tick-hi "$TICK_HI" \
    --json-out "$OUT_ROOT/profile_off.json" | sed 's/^/  /'
assert_profile "$OUT_ROOT/profile_off.json" off

echo "PASS: FID-0014 patrol WAYMODE_MAGIC pause/warp profile guarded (fix ON = stock-shaped freeze+warp; opt-out = legacy creep-walk)"
