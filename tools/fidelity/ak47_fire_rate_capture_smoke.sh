#!/usr/bin/env bash
#
# ak47_fire_rate_capture_smoke.sh — end-to-end gate for the FID-0056 full-auto
# fire-cadence fix's gun.c CALL-SITE (P1d).
#
# The ROM-free unit test (tests/test_fire_rate_authentic.c) exercises the pure
# arithmetic (fireRateCounterAdvance / fireRateEffectiveAutoRate) but RE-IMPLEMENTS
# the gate — so a revert of the call-site in src/game/gun.c (~line 18359-18372,
# where the full-auto machinegun case calls bondwalkItemGetAutomaticFiringRate()
# then fireRateEffectiveAutoRate()) stays green. The canonical combat tape
# (dam_combat_guard6) fires the PP7, which is NOT an automatic and never reaches
# that case, so it can't guard it either.
#
# This lane replays a committed AK47 sustained-fire tape (baselines/tapes/
# dam_ak47_sustained.ge7tape) that DOES hold the full-auto machinegun case for
# 400 ticks, under the determinism envelope + the deterministic weapon-injection
# env (replay_env in the baseline: give+equip ITEM_AK47=8 with rifle ammo). Since
# Input.FireRateAuthentic ships ON (faithful N64 cadence) by default, the gate
# asserts the faithful hash. A call-site revert makes the default behave like the
# legacy locked-60Hz cadence -> the replay hash diverges -> this lane FAILS.
#
#   --gate     (default) replay the tape under the default (authentic) cadence;
#              the sim-state hash must match the committed baseline byte-exact.
#   --prove    self-test: replay authentic (flag ON, default) AND legacy (flag OFF,
#              GE007_FIRE_RATE_AUTHENTIC=0, which exactly reproduces the pre-FID-0056
#              call-site) and assert (a) ON == baseline, (b) OFF ==
#              fire_rate_authentic_off_hash, (c) ON != OFF. (c) is the proof that
#              the tape distinguishes the call-site: a revert reddens the gate.
#
# ROM is local-only (GE007_ROM or ./baserom.u.z64); tier-3 ROM-gated lane. The
# baseline hash is build-config specific (canonical Release build). Regenerate the
# tape only via scratchpad/record_ak47_final.sh (keep the recipe in sync with the
# WEAPON_ENV documented in the baseline's replay_env).
set -euo pipefail
cd "$(dirname "$0")/../.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
TIMEOUT_SECONDS=180
MODE="gate"
TAPE_NAME="dam_ak47_sustained"

usage() { sed -n '2,40p' "$0"; exit 0; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --no-build) shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        --prove) MODE="prove"; shift ;;
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

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"

TAPES_DIR="$(pwd)/baselines/tapes"
TAPE="$TAPES_DIR/$TAPE_NAME.ge7tape"
EXP="$TAPES_DIR/$TAPE_NAME.expected.json"
validation_require_file "$TAPE" "AK47 tape"
validation_require_file "$EXP" "AK47 tape baseline"

json_field() {  # $1=key
    python3 - "$EXP" "$1" <<'PY'
import json, sys
print(json.load(open(sys.argv[1])).get(sys.argv[2], ""))
PY
}

LEVEL="$(json_field level)"
WANT_ON="$(json_field sim_state_hash)"
WANT_OFF="$(json_field fire_rate_authentic_off_hash)"
REPLAY_ENV="$(json_field replay_env)"

if [[ -z "$LEVEL" || -z "$WANT_ON" || -z "$REPLAY_ENV" ]]; then
    echo "FAIL: AK47 baseline missing level/sim_state_hash/replay_env" >&2
    exit 2
fi

ENVV=(SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1
      GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1)

hash_of() { grep -o '[0-9a-f]\{16\}' "$1" 2>/dev/null | head -1; }

# Replay the tape with an optional extra env override; echo the sim-state hash.
replay_hash() {  # $@ = extra KEY=VALUE overrides applied AFTER replay_env
    local dir got
    dir="$(mktemp -d)"
    # shellcheck disable=SC2086
    if ( cd "$dir" && env -u GE007_DEBUG "${ENVV[@]}" $REPLAY_ENV "$@" \
            "$BINARY" --rom "$ROM" --savedir "$dir/sd" \
            --level "$LEVEL" --deterministic --play-tape "$TAPE" \
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

if [[ "$MODE" == "prove" ]]; then
    ON="$(replay_hash)"                                   # default: FireRateAuthentic=1
    OFF="$(replay_hash GE007_FIRE_RATE_AUTHENTIC=0)"      # pre-FID-0056 call-site
    echo "ak47-fire-rate: authentic(ON)=$ON  legacy(OFF)=$OFF  baseline_on=$WANT_ON baseline_off=$WANT_OFF"
    rc=0
    if [[ -z "$ON" || -z "$OFF" ]]; then
        echo "FAIL(prove): a replay produced no sim-state hash" >&2; exit 1
    fi
    if [[ "$ON" != "$WANT_ON" ]]; then
        echo "FAIL(prove): authentic hash $ON != baseline $WANT_ON" >&2; rc=1
    fi
    if [[ -n "$WANT_OFF" && "$OFF" != "$WANT_OFF" ]]; then
        echo "FAIL(prove): legacy hash $OFF != recorded off-hash $WANT_OFF" >&2; rc=1
    fi
    if [[ "$ON" == "$OFF" ]]; then
        echo "FAIL(prove): authentic == legacy — the tape does NOT exercise the fire-rate call-site (a revert would stay green)" >&2
        rc=1
    fi
    if [[ "$rc" -eq 0 ]]; then
        echo "PASS(prove): authentic != legacy AND both match recorded hashes — a gun.c fire-rate call-site revert reddens this gate."
    fi
    exit "$rc"
fi

# Gate mode: default (authentic) replay must match the committed baseline.
GOT="$(replay_hash)"
if [[ -z "$GOT" ]]; then
    echo "FAIL: AK47 tape replay produced no sim-state hash (run failed)" >&2
    exit 1
fi
if [[ "$GOT" == "$WANT_ON" ]]; then
    echo "ak47-fire-rate: PASS $TAPE_NAME (level=$LEVEL hash=$GOT, authentic cadence)"
    echo "PASS: AK47 full-auto fire-rate call-site guarded — replay hash matches baseline"
    exit 0
fi
echo "ak47-fire-rate: FAIL $TAPE_NAME — hash $GOT != expected $WANT_ON" >&2
if [[ "$GOT" == "$WANT_OFF" ]]; then
    echo "  (hash == legacy off-hash: the gun.c fire-rate call-site looks REVERTED, or Input.FireRateAuthentic default flipped to 0)" >&2
else
    echo "  (combat sim diverged, or the binary is not the canonical Release build)" >&2
fi
exit 1
