#!/usr/bin/env bash
#
# gate_routes_smoke.sh — the ares-free hard gate for gate:true ROM-oracle routes
# (Phase B, FID-0031 / D4).
#
# A route in tools/rom_oracle_routes/*.json marked "gate": true is a committed
# parity checkpoint. Its ares-side trace comparison is a manual/local oracle step
# (sense_trace_sweep.sh --gate; ares is not in CI). Its DURABLE, CI-runnable half
# is a deterministic input tape: this lane enumerates every gate:true route,
# resolves its paired tape (baselines/tapes/<tape>.ge7tape + .expected.json, where
# <tape> is the route's "tape" field or its name), replays the tape under the
# determinism envelope (+ any replay_env the baseline declares), and asserts the
# whole-sim state hash matches the recorded baseline. This makes the gate:true flag
# mean something ares-free: a gate:true route with a missing/broken tape, or a sim
# regression on its route, FAILS here.
#
# Complements (does not replace):
#   - tools/fidelity/tape_regression.sh          (replays ALL committed tapes)
#   - tools/fidelity/sense_trace_sweep.sh --gate (ares-side trace comparison)
# The distinct guard here is the route<->tape LINKAGE: a route cannot claim
# gate:true without a passing determinism tape.
#
# ROM/binary-gated: SKIPs the whole lane cleanly (exit 0, noted) when the ROM or
# native binary is absent — like the other tier-3 lanes. Prints its coverage and
# any skipped routes (charter rule 9: no silent caps).
#
# Usage: tools/fidelity/gate_routes_smoke.sh [--build-dir DIR] [--rom PATH]
#                                            [--binary PATH] [--no-build]
#                                            [--timeout SECONDS]
set -euo pipefail
cd "$(dirname "$0")/../.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
TIMEOUT_SECONDS=180

usage() { sed -n '2,36p' "$0"; exit 0; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --no-build) shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
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

ROUTE_DIR="$(pwd)/tools/rom_oracle_routes"
TAPES_DIR="$(pwd)/baselines/tapes"

# Enumerate gate:true routes (name + tape). Emits "route_name<TAB>tape_name" lines.
gate_routes() {
    python3 - "$ROUTE_DIR" <<'PY'
import json, sys, pathlib
for p in sorted(pathlib.Path(sys.argv[1]).glob("*.json")):
    try:
        r = json.load(open(p))
    except (OSError, ValueError):
        continue
    if r.get("gate", False) is True:
        name = r.get("name", p.stem)
        print(f"{name}\t{r.get('tape', name)}")
PY
}

mapfile -t ROUTES < <(gate_routes)

if [[ "${#ROUTES[@]}" -eq 0 ]]; then
    echo "gate-routes: no gate:true routes found (nothing to gate yet)"
    echo "PASS: gate-routes (empty gate set)"
    exit 0
fi

echo "gate-routes: ${#ROUTES[@]} gate:true route(s): $(printf '%s ' "${ROUTES[@]%%$'\t'*}")"

# Prerequisite gate — SKIP whole lane cleanly if ROM/binary absent.
if [[ ! -x "$BINARY" || ! -e "$ROM" ]]; then
    echo "gate-routes: SKIP (native binary or ROM absent) — ${#ROUTES[@]} route(s) not replayed:"
    printf '  SKIP %s\n' "${ROUTES[@]%%$'\t'*}"
    echo "PASS: gate-routes (skipped, prerequisites absent)"
    exit 0
fi

ENVV=(SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1
      GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1)

json_field() {  # $1=expected.json $2=key
    python3 - "$1" "$2" <<'PY'
import json, sys
print(json.load(open(sys.argv[1])).get(sys.argv[2], ""))
PY
}
hash_of() { grep -o '[0-9a-f]\{16\}' "$1" 2>/dev/null | head -1; }

validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT INT TERM

fail=0
for entry in "${ROUTES[@]}"; do
    route="${entry%%$'\t'*}"
    tape_name="${entry##*$'\t'}"
    tape="$TAPES_DIR/$tape_name.ge7tape"
    exp="$TAPES_DIR/$tape_name.expected.json"
    if [[ ! -e "$tape" || ! -e "$exp" ]]; then
        echo "gate-routes: FAIL $route — gate:true but missing tape baseline ($tape_name.ge7tape/.expected.json)" >&2
        fail=1; continue
    fi
    level="$(json_field "$exp" level)"
    want="$(json_field "$exp" sim_state_hash)"
    replay_env="$(json_field "$exp" replay_env)"
    if [[ -z "$level" || -z "$want" ]]; then
        echo "gate-routes: FAIL $route — baseline missing level/sim_state_hash" >&2
        fail=1; continue
    fi
    # Replay with retry-on-misalignment. The FID-0075 boot-load race intermittently
    # desyncs tape playback ("TICK MISALIGNMENT DETECTED") — a HARNESS boot-timing
    # artifact, NOT a sim divergence — so a misaligned run is retried. A run that
    # completes WITHOUT misalignment is authoritative: a wrong hash then is a REAL
    # divergence and fails immediately (never retried away). Only an all-attempts-
    # misaligned route fails, with the race called out.
    got=""; clean_run=0; misaligns=0
    for attempt in 1 2 3 4; do
        dir="$(mktemp -d)"
        # shellcheck disable=SC2086
        if ( cd "$dir" && validation_run_with_timeout "$TIMEOUT_SECONDS" \
                env -u GE007_DEBUG "${ENVV[@]}" $replay_env "$BINARY" --rom "$ROM" \
                --savedir "$dir/sd" --level "$level" --deterministic --play-tape "$tape" \
                --sim-state-hash-out "$dir/rep.json" >"$dir/log" 2>&1 ); then
            run_rc=0
        else
            run_rc=$?
        fi
        if grep -qi "MISALIGN" "$dir/log"; then
            misaligns=$((misaligns + 1)); rm -rf "$dir"; continue   # boot-race: retry
        fi
        # A clean (non-misaligned) run is authoritative.
        [[ "$run_rc" -eq 0 ]] && got="$(hash_of "$dir/rep.json")" || got=""
        clean_run=1; rm -rf "$dir"; break
    done
    if [[ "$clean_run" -eq 0 ]]; then
        echo "gate-routes: FAIL $route — all 4 replays hit the FID-0075 boot-load tick-misalignment race (never got a clean run)" >&2
        fail=1; continue
    fi
    if [[ "$got" == "$want" ]]; then
        note=""; [[ "$misaligns" -gt 0 ]] && note=" (after $misaligns misaligned boot-race retr$([[ $misaligns -eq 1 ]] && echo y || echo ies))"
        echo "gate-routes: PASS $route (tape=$tape_name level=$level hash=$got)$note"
    else
        echo "gate-routes: FAIL $route — hash '${got:-<none>}' != expected $want (clean run, real divergence)" >&2
        fail=1
    fi
done

if [[ "$fail" -ne 0 ]]; then
    echo "gate-routes: FAIL — one or more gate:true routes diverged or lack a tape" >&2
    exit 1
fi
echo "PASS: gate-routes — all ${#ROUTES[@]} gate:true route(s) replay byte-exact."
