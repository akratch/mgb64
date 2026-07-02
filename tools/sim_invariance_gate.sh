#!/usr/bin/env bash
#
# Remaster P0.2 — RAMROM sim-invariance gate (LOCAL preflight).
#
# Runs the same deterministic RAMROM replay to the same frame twice, holding the
# internal render resolution FIXED and toggling only the remaster's screen-space
# pipeline (post-FX + SSAO) OFF vs ON. The whole-simulation hash (see
# --sim-state-hash-out; src/platform/sim_state_hash.c) must be IDENTICAL, proving
# the screen-space renderer touches no simulation byte.
#
# NOTE on scope: RenderScale is held fixed on purpose. Changing the internal
# resolution alters view-frustum *culling* (rooms_drawn/tris), which is a
# gameplay-NEUTRAL render difference but does perturb render bookkeeping that
# lives in the game arena — so the whole-arena hash would (correctly, but
# unhelpfully) flag it. Gameplay-invariance across RenderScale is covered
# separately by the gameplay-field trace comparison (tools/compare_state.py).
#
# A mismatch here is a real screen-space -> sim leak. Localize with:
#   tools/compare_state.py <off.jsonl> <on.jsonl>   (add --trace-state to both)
#
# Requires a ROM, so this is a LOCAL gate; CI runs the ROM-free hash unit test
# (ctest -R sim_state_hash) instead.
#
# Usage: tools/sim_invariance_gate.sh [replay=dam1] [frame=600] [render_scale=2]
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
ROOT="$(pwd)"

BIN="${GE007_BIN:-$ROOT/build/ge007}"
ROM="${GE007_ROM:-$ROOT/baserom.u.z64}"
REPLAY="${1:-dam1}"
FRAME="${2:-600}"
SCALE="${3:-2}"

[ -x "$BIN" ] || { echo "sim-invariance-gate: $BIN not built" >&2; exit 2; }
[ -e "$ROM" ] || { echo "sim-invariance-gate: ROM $ROM not found (local gate)" >&2; exit 2; }

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
ENVV=(GE007_DETERMINISTIC_STABLE_COUNT=1 SDL_AUDIODRIVER=dummy GE007_MUTE=1
      GE007_BACKGROUND=1 GE007_NO_VSYNC=1 GE007_NO_INPUT_GRAB=1 GE007_DISABLE_LEVEL_INTRO=1)

run() {  # $1=label ; remaining args = --config-override ...
    local label="$1"; shift
    local dir="$TMP/$label"; mkdir -p "$dir"        # isolated CWD: no ge007.ini drift
    ( cd "$dir" && env "${ENVV[@]}" "$BIN" --rom "$ROM" --ramrom "$REPLAY" \
        --deterministic --screenshot-frame "$FRAME" --screenshot-label "$label" \
        --screenshot-exit "$@" --sim-state-hash-out "$dir/hash.json" \
        >"$dir/log" 2>&1 )
    grep -o '[0-9a-f]\{16\}' "$dir/hash.json" | head -1
}

echo "sim-invariance-gate: replay=$REPLAY frame=$FRAME render_scale=$SCALE"
OFF="$(run off --config-override Video.RemasterFX=0 --config-override Video.RenderScale=$SCALE \
               --config-override Video.Ssao=0)"
ON="$(run on  --config-override Video.RemasterFX=1 --config-override Video.RenderScale=$SCALE \
              --config-override Video.Bloom=1 --config-override Video.Fxaa=1 \
              --config-override Video.Ssao=1)"
echo "  OFF (screen-space off): $OFF"
echo "  ON  (screen-space on):  $ON"

if [ -z "$OFF" ] || [ -z "$ON" ]; then
    echo "sim-invariance-gate: FAIL — a run produced no hash (see $TMP/*/log)" >&2
    exit 1
fi
if [ "$OFF" = "$ON" ]; then
    echo "sim-invariance-gate: PASS — screen-space rendering perturbs no simulation byte."
else
    echo "sim-invariance-gate: FAIL — screen-space -> sim leak; localize with tools/compare_state.py" >&2
    exit 1
fi
