#!/usr/bin/env bash
#
# combat_route_capture_smoke.sh — permanent determinism guard for the Dam combat
# route (FID-0054, S-Tier combat-parity cluster).
#
# Replays the committed combat input tape (baselines/tapes/dam_combat_guard6.ge7tape)
# under the determinism envelope and asserts the whole-sim state hash matches the
# recorded baseline (baselines/tapes/dam_combat_guard6.expected.json). Unlike the
# no-combat dam_forward tape, this route FIRES the PP7 (GE007_AUTO_FIRE baked into
# the captured OSContPad stream), so the guard reads the sim through a live weapon
# discharge + projectile spawn + guard-AI reactions — the exact state the both-sides
# ares oracle (compare_combat_trace.py) frame-locks FID-0054 against. If combat ever
# becomes nondeterministic (RNG spread / auto-aim schedule drift), or a code change
# perturbs the combat sim, the replay hash diverges and this lane FAILS.
#
# This is the durable, ROM-gated-but-ares-free asset: it needs only the native
# binary + ROM (the both-sides ares comparison stays a manual oracle step, ares is
# not in CI). It is a sibling of tools/fidelity/tape_regression.sh scoped to the
# single combat tape, plus a --perturb self-test that proves the hash assertion
# actually catches a divergence (a one-byte tape mutation must change the hash).
#
# Usage:
#   tools/fidelity/combat_route_capture_smoke.sh [--build-dir DIR] [--rom PATH]
#                                                [--binary PATH] [--no-build]
#                                                [--timeout SECONDS]
#   tools/fidelity/combat_route_capture_smoke.sh --perturb   # self-test: assert a
#                                                            # mutated tape FAILS
#
# ROM is local-only (GE007_ROM or ./baserom.u.z64); tier-3 ROM-gated lane. The
# baseline hash is build-config specific (canonical Release build), exactly like
# the dam_forward_30s baseline — regenerate only via tape_regression.sh --record.
set -euo pipefail
cd "$(dirname "$0")/../.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=0
TIMEOUT_SECONDS=180
MODE="gate"
TAPE_NAME="dam_combat_guard6"

usage() {
    sed -n '2,32p' "$0"
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
        --perturb) MODE="perturb"; shift ;;
        --gate) MODE="gate"; shift ;;
        -h|--help) usage ;;
        *) echo "Unknown arg: $1" >&2; exit 2 ;;
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

TAPES_DIR="$(pwd)/baselines/tapes"
TAPE="$TAPES_DIR/$TAPE_NAME.ge7tape"
EXP="$TAPES_DIR/$TAPE_NAME.expected.json"
validation_require_file "$TAPE" "combat tape"
validation_require_file "$EXP" "combat tape baseline"

LEVEL="$(python3 - "$EXP" <<'PY'
import json, sys
print(json.load(open(sys.argv[1]))["level"])
PY
)"
WANT="$(python3 - "$EXP" <<'PY'
import json, sys
print(json.load(open(sys.argv[1]))["sim_state_hash"])
PY
)"

ENVV=(SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1
      GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1)

hash_of() { grep -o '[0-9a-f]\{16\}' "$1" 2>/dev/null | head -1; }

# Replay $1 (tape path); echo its final sim-state hash (empty on run failure).
replay_hash() {
    local tape="$1" dir got
    dir="$(mktemp -d)"
    if ( cd "$dir" && env "${ENVV[@]}" "$BINARY" --rom "$ROM" --savedir "$dir/sd" \
            --level "$LEVEL" --deterministic --play-tape "$tape" \
            --sim-state-hash-out "$dir/rep.json" >"$dir/log" 2>&1 ); then
        got="$(hash_of "$dir/rep.json")"
    else
        got=""
    fi
    rm -rf "$dir"
    printf '%s' "$got"
}

validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT INT TERM

if [[ "$MODE" == "perturb" ]]; then
    # Self-test: flip one pad byte in a copy of the tape; its replay hash MUST
    # differ from the baseline (proving the hash assertion catches a divergence).
    WORK="$(mktemp -d)"
    BAD="$WORK/perturbed.ge7tape"
    python3 - "$TAPE" "$BAD" <<'PY'
import struct, sys
data = bytearray(open(sys.argv[1], "rb").read())
# Header is 48 bytes; each per-tick record is u32 tick + num_players*(u16 button
# + s8 stick_x + s8 stick_y). Edit the player-0 stick_x of a mid-stream record
# (leaving the u32 tick fields intact so replay still ALIGNS, but the sim sees a
# different input -> a different, non-failing hash). This is a genuine input
# edit, the exact class of change the guard must catch.
npl = struct.unpack_from("<I", data, 36)[0]          # header.num_players
reclen = 4 + npl * 4
nrec = (len(data) - 48) // reclen
rec = nrec // 2                                       # a record mid-stream
off = 48 + rec * reclen + 4 + 2                       # +tick(4) +button(2) -> stick_x
data[off] = (data[off] + 40) & 0xFF                   # nudge stick_x
open(sys.argv[2], "wb").write(data)
print(f"perturbed player-0 stick_x of record {rec} at offset {off}")
PY
    GOT="$(replay_hash "$BAD")"
    rm -rf "$WORK"
    echo "perturb self-test: baseline=$WANT perturbed=$GOT"
    if [[ -z "$GOT" ]]; then
        # A perturbed tape may also fail to replay cleanly (tick misalignment);
        # that is still a detected divergence.
        echo "PASS(perturb): mutated tape did not reproduce the baseline (replay diverged/failed)"
        exit 0
    fi
    if [[ "$GOT" == "$WANT" ]]; then
        echo "FAIL(perturb): mutated tape still produced the baseline hash — guard is blind" >&2
        exit 1
    fi
    echo "PASS(perturb): mutated tape produced $GOT != baseline $WANT (guard detects divergence)"
    exit 0
fi

# Gate mode: the committed tape must replay to the baseline hash byte-exact.
GOT="$(replay_hash "$TAPE")"
if [[ -z "$GOT" ]]; then
    echo "FAIL: combat tape replay produced no sim-state hash (run failed)" >&2
    exit 1
fi
if [[ "$GOT" == "$WANT" ]]; then
    echo "combat-route-capture: PASS $TAPE_NAME (level=$LEVEL hash=$GOT)"
    echo "PASS: combat route deterministic — replay hash matches recorded baseline"
    exit 0
fi
echo "combat-route-capture: FAIL $TAPE_NAME — hash $GOT != expected $WANT" >&2
echo "  (combat sim diverged, or the binary is not the canonical Release build)" >&2
exit 1
