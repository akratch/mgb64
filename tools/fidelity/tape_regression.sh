#!/usr/bin/env bash
#
# tape_regression.sh — replay every committed .ge7tape input tape and assert the
# final sim-state hash matches its baseline (FID-0034, S-Tier Task 0.6).
#
# Tapes are input-only (never ROM-derived) and live in baselines/tapes/*.ge7tape,
# each paired with baselines/tapes/<name>.expected.json holding the level slug and
# the expected sim_state_hash. Each tape is replayed under the determinism
# envelope in an isolated CWD + savedir (so no ge007.ini / save-file drift can
# perturb the run — the exact contamination that makes a naive back-to-back
# replay diverge). The final sim-state hash is the canonical byte-exact
# invariant: it hashes the whole 8 MB pool + prop pool + sim timers, so it
# subsumes any per-field --trace-state comparison.
#
# Usage:
#   tools/fidelity/tape_regression.sh                 # gate: replay + compare all tapes
#   tools/fidelity/tape_regression.sh --record NAME LEVEL "ENVSCRIPT..."
#                                                     # (re)record a tape + refresh its baseline
#
# ROM is local-only (GE007_ROM or ./baserom.u.z64); the gate is ROM-gated tier 3.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
ROOT="$(pwd)"

BIN="${GE007_BIN:-$ROOT/build/ge007}"
ROM="${GE007_ROM:-$ROOT/baserom.u.z64}"
TAPES_DIR="$ROOT/baselines/tapes"

# Determinism envelope (charter rule 6). Must match the recording environment so
# record and replay reach byte-identical sim state.
ENVV=(SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1
      GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1)

[ -x "$BIN" ] || { echo "tape-regression: $BIN not built" >&2; exit 2; }
[ -e "$ROM" ] || { echo "tape-regression: ROM $ROM not found (local gate)" >&2; exit 2; }

json_get() {  # $1=file $2=key -> prints value (string or number, no quotes)
    python3 - "$1" "$2" <<'PY'
import json, sys
with open(sys.argv[1]) as f:
    d = json.load(f)
v = d.get(sys.argv[2], "")
print(v)
PY
}

hash_of() {  # $1=sim-state-hash json -> 16-hex sim hash
    grep -o '[0-9a-f]\{16\}' "$1" | head -1
}

# ---- Record mode: refresh a tape + its baseline ----
if [ "${1:-}" = "--record" ]; then
    NAME="${2:?usage: --record NAME LEVEL \"ENV=script ...\" [end_timer]}"
    LEVEL="${3:?level slug required}"
    SCRIPT_ENV="${4:-GE007_AUTO_FORWARD=60:1500}"
    END_TIMER="${5:-1800}"
    TAPE="$TAPES_DIR/$NAME.ge7tape"
    EXP="$TAPES_DIR/$NAME.expected.json"
    dir="$(mktemp -d)"; trap 'rm -rf "$dir"' EXIT
    # shellcheck disable=SC2086
    ( cd "$dir" && env "${ENVV[@]}" $SCRIPT_ENV "$BIN" --rom "$ROM" --savedir "$dir/sd" \
        --level "$LEVEL" --deterministic --record-tape "$TAPE" \
        --screenshot-game-timer "$END_TIMER" --screenshot-exit \
        --sim-state-hash-out "$dir/rec.json" >"$dir/log" 2>&1 ) || {
        echo "tape-regression: record run failed (see $dir/log)"; sed -n '$p' "$dir/log"; exit 1; }
    H="$(hash_of "$dir/rec.json")"
    echo "tape-regression: recorded $TAPE (hash=$H). Update $EXP manually with hash $H."
    exit 0
fi

# ---- Gate mode: replay every tape, compare ----
shopt -s nullglob
tapes=("$TAPES_DIR"/*.ge7tape)
if [ ${#tapes[@]} -eq 0 ]; then
    echo "tape-regression: no tapes in $TAPES_DIR (nothing to check)"
    exit 0
fi

fail=0
for tape in "${tapes[@]}"; do
    name="$(basename "$tape" .ge7tape)"
    exp="$TAPES_DIR/$name.expected.json"
    if [ ! -e "$exp" ]; then
        echo "tape-regression: FAIL $name — missing baseline $exp" >&2
        fail=1; continue
    fi
    level="$(json_get "$exp" level)"
    want="$(json_get "$exp" sim_state_hash)"
    if [ -z "$level" ] || [ -z "$want" ]; then
        echo "tape-regression: FAIL $name — baseline missing level/sim_state_hash" >&2
        fail=1; continue
    fi

    dir="$(mktemp -d)"
    # shellcheck disable=SC2086
    ( cd "$dir" && env "${ENVV[@]}" "$BIN" --rom "$ROM" --savedir "$dir/sd" \
        --level "$level" --deterministic --play-tape "$tape" \
        --sim-state-hash-out "$dir/rep.json" >"$dir/log" 2>&1 )
    rc=$?
    got="$(hash_of "$dir/rep.json" 2>/dev/null || true)"
    if [ $rc -ne 0 ]; then
        echo "tape-regression: FAIL $name — replay exited $rc (see below)" >&2
        grep -i "INPUT-TAPE\|misalign" "$dir/log" | tail -3 >&2
        fail=1; rm -rf "$dir"; continue
    fi
    if [ "$got" = "$want" ]; then
        echo "tape-regression: PASS $name (level=$level hash=$got)"
    else
        echo "tape-regression: FAIL $name — hash $got != expected $want" >&2
        grep -i "INPUT-TAPE\|misalign" "$dir/log" | tail -3 >&2
        fail=1
    fi
    rm -rf "$dir"
done

if [ $fail -ne 0 ]; then
    echo "tape-regression: FAIL — one or more tapes diverged" >&2
    exit 1
fi
echo "tape-regression: PASS — all ${#tapes[@]} tape(s) replay byte-exact."
