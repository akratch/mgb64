#!/usr/bin/env bash
#
# guard_fire_rate_symmetry_smoke.sh — permanent regression guard for FID-0066
# (NPC full-auto cadence symmetry; the port-half of the FID-0056 parity skew).
#
# FID-0056 tick-scaled the PLAYER's full-auto gate (gun.c field_88C % fire_rate)
# behind Input.FireRateAuthentic (default ON) so automatics fire at the true N64
# cadence at the port's locked 60Hz. FID-0066 does the SAME symmetrically for
# GUARDS: chrlvFireWeaponRelated (0x7F02D734) gates guard full-auto on
# self->firecount[hand]++ % bondwalkItemGetAutomaticFiringRate(item); the fix
# scales the divisor by Input.FireRateN64FrameCost (default 3) so guards fire
# ~1/3 as fast — the faithful outcome, since retail gated BOTH on the same
# per-rendered-frame counter.
#
# This lane exercises the REAL call site end-to-end (not a re-implementation of
# the gate — the FID-0056 unit test's known weakness). It replays the committed
# dam_forward_30s tape, on which guard chr=6 (KF7 Soviet, AutomaticFiringRate=3)
# sustains full-auto at Bond, and asserts, from the GE007_TRACE_GUARD_AUTOFIRE
# gate-fire stream:
#
#   1. CADENCE (the fix): the DEFAULT (fix-ON) within-burst inter-shot interval
#      is exactly FrameCost x the opt-out (fix-OFF) interval. A chrlv.c call-site
#      revert (dropping the fireRateEffectiveAutoRate divisor scaling) collapses
#      the ON interval to the OFF interval -> ratio 1x -> this lane FAILS.
#   2. BYTE-IDENTITY (the opt-out): with GE007_FIRE_RATE_AUTHENTIC=0 the sim-state
#      hash equals the recorded pre-fix baseline, proving the opt-out restores
#      legacy behavior byte-exact.
#
# Tier-3, ROM-gated (needs the native binary + ROM; ares-free). The baseline
# hash is build-config specific (canonical Release build).
#
# Usage:
#   tools/fidelity/guard_fire_rate_symmetry_smoke.sh [--build-dir DIR] [--rom PATH]
#                                                    [--binary PATH] [--timeout SECONDS]
set -euo pipefail
cd "$(dirname "$0")/../.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
TIMEOUT_SECONDS=180
TAPE_NAME="dam_forward_30s"
GUARD_CHRNUM=6              # the KF7 guard that sustains full-auto in this tape
FRAME_COST=3               # Input.FireRateN64FrameCost default (Dam ~20fps combat)
# Recorded legacy / opt-out sim-state hash for dam_forward_30s. This is the
# byte-identity anchor for GE007_FIRE_RATE_AUTHENTIC=0 — the pre-FID-0066
# fire-rate cadence on the CURRENT default baseline world. Re-based 2026-07-11
# (95944e2282a48178 -> 4dc07b71623b315c) for FID-0014: the faithful
# WAYMODE_MAGIC patrol semantics (default ON) shift offscreen patroller
# positions in every Dam replay, so the fire-rate opt-out hash moved with the
# world; the FID-0066 contract itself (ON interval == FRAME_COST x OFF
# interval, OFF == this anchor) is unchanged. See
# docs/fidelity/derivations/FID-0014-patrol-magic.md.
OPTOUT_HASH="4dc07b71623b315c"

usage() { sed -n '2,33p' "$0"; exit 0; }
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        --no-build|--build) shift ;;  # no-op: this lane never builds (add_port_validation_smoke passes --no-build)
        -h|--help) usage ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

[[ -z "$BINARY" ]] && BINARY="$(validation_binary_path "$BUILD_DIR")"
validation_require_binary "$BINARY"

TAPES_DIR="$(pwd)/baselines/tapes"
TAPE="$TAPES_DIR/$TAPE_NAME.ge7tape"
EXP="$TAPES_DIR/$TAPE_NAME.expected.json"
validation_require_file "$TAPE" "input tape"
validation_require_file "$EXP" "tape baseline"

LEVEL="$(python3 - "$EXP" <<'PY'
import json, sys
print(json.load(open(sys.argv[1]))["level"])
PY
)"

ENVV=(SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1
      GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1)

hash_of() { grep -o '[0-9a-f]\{16\}' "$1" 2>/dev/null | head -1 || true; }

# Most frequent frame-delta between consecutive GUARD_AUTOFIRE gate-fires,
# restricted to 2..50 frames — excludes the delta<=1 LOS-retry stutter and the
# large between-burst gaps, isolating the sustained-fire cadence. $1 = trace file.
burst_interval_mode() {
    local counts
    counts="$(grep '\[GUARD_AUTOFIRE\]' "$1" 2>/dev/null \
        | grep -o 'frame=[0-9]*' | sed 's/frame=//' \
        | awk 'NR>1{d=$1-p; if(d>=2 && d<=50) print d} {p=$1}' \
        | sort -n | uniq -c | sort -rn 2>/dev/null || true)"
    printf '%s\n' "$counts" | awk 'NR==1{print ($2==""?0:$2)} END{if(NR==0) print 0}'
}

# Replay the tape with the guard-autofire trace into $2 (trace) / $3 (hash json).
# $1 = extra env assignment (e.g. GE007_FIRE_RATE_AUTHENTIC=0), or empty string.
replay_into() {
    local extra="$1" trace="$2" hashjson="$3" dir
    dir="$(mktemp -d)"
    ( cd "$dir" && env "${ENVV[@]}" ${extra:+"$extra"} \
            GE007_TRACE_GUARD_AUTOFIRE=1 \
            GE007_TRACE_GUARD_AUTOFIRE_CHRNUM="$GUARD_CHRNUM" \
            "$BINARY" --rom "$ROM" --savedir "$dir/sd" \
            --level "$LEVEL" --deterministic --play-tape "$TAPE" \
            --sim-state-hash-out "$hashjson" >"$dir/log" 2>"$trace" ) || true
    rm -rf "$dir"
}

validation_acquire_runtime_lock
trap 'validation_release_runtime_lock; rm -rf "$WORK"' EXIT INT TERM
WORK="$(mktemp -d)"

replay_into "" "$WORK/on.trace" "$WORK/on.json"
ON_HASH="$(hash_of "$WORK/on.json")"; ON_HASH="${ON_HASH:-NONE}"
ON_MODE="$(burst_interval_mode "$WORK/on.trace")"

replay_into "GE007_FIRE_RATE_AUTHENTIC=0" "$WORK/off.trace" "$WORK/off.json"
OFF_HASH="$(hash_of "$WORK/off.json")"; OFF_HASH="${OFF_HASH:-NONE}"
OFF_MODE="$(burst_interval_mode "$WORK/off.trace")"

echo "guard-fire-rate-symmetry: tape=$TAPE_NAME guard=chr$GUARD_CHRNUM cost=$FRAME_COST"
echo "  fix-ON  (default) : hash=$ON_HASH  within-burst interval=$ON_MODE frames"
echo "  fix-OFF (opt-out) : hash=$OFF_HASH within-burst interval=$OFF_MODE frames"

fail=0

if [[ "$ON_MODE" -eq 0 || "$OFF_MODE" -eq 0 ]]; then
    echo "guard-fire-rate-symmetry: FAIL — no sustained guard full-auto captured (tape/guard drift?)" >&2
    fail=1
fi

# 1. CADENCE: default interval must be exactly FrameCost x the opt-out interval.
if [[ "$fail" -eq 0 && "$ON_MODE" -ne $((OFF_MODE * FRAME_COST)) ]]; then
    echo "guard-fire-rate-symmetry: FAIL CADENCE — ON interval $ON_MODE != $FRAME_COST x OFF interval $OFF_MODE." >&2
    echo "  The guard divisor scaling (fireRateEffectiveAutoRate) appears reverted/broken." >&2
    fail=1
elif [[ "$fail" -eq 0 ]]; then
    echo "guard-fire-rate-symmetry: OK CADENCE — ON $ON_MODE == ${FRAME_COST}x OFF $OFF_MODE (guards fire 1/${FRAME_COST} as fast)."
fi

# 2. BYTE-IDENTITY: the opt-out must reproduce the recorded pre-fix hash.
if [[ "$OFF_HASH" != "$OPTOUT_HASH" ]]; then
    echo "guard-fire-rate-symmetry: FAIL BYTE-IDENTITY — opt-out hash $OFF_HASH != recorded $OPTOUT_HASH." >&2
    echo "  GE007_FIRE_RATE_AUTHENTIC=0 no longer restores legacy guard cadence byte-exact." >&2
    fail=1
else
    echo "guard-fire-rate-symmetry: OK BYTE-IDENTITY — opt-out hash == recorded $OPTOUT_HASH."
fi

if [[ "$fail" -ne 0 ]]; then
    echo "guard-fire-rate-symmetry: FAIL" >&2
    exit 1
fi
echo "guard-fire-rate-symmetry: PASS"
exit 0
