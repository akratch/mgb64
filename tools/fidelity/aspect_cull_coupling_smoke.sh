#!/usr/bin/env bash
#
# aspect_cull_coupling_smoke.sh — FID-0058 regression lane.
#
# Verdict (this task): the widescreen cull-window widening (widenCullHorizontal,
# src/game/bondview.c:2064) makes gameplay ASPECT-RATIO-DEPENDENT. The widened
# horizontal frustum planes built in sub_GAME_7F0785DC are read by the SIM's
# visibility tests (camIsPosInScreen/camIsPosInScreenBox/sub_GAME_7F054D6C ->
# chrIsPosOffScreen chrlv.c:12719, object detection chrobjhandler.c:11045), so a
# guard/object near the horizontal screen edge is treated as on-screen earlier at
# 16:9 than at 4:3 -> aspect-dependent guard AI/targeting -> divergent sim state.
#
# This lane replays ONE input tape at 4:3 vs 16:9 (everything else identical) and
# asserts two properties on the final sim-state hash:
#
#   INVARIANT (hard, forward guard): with the widen DISABLED
#     (GE007_NO_CULL_ASPECT_FIX=1) the two aspects are BYTE-IDENTICAL. This proves
#     widenCullHorizontal is the SOLE aspect->sim coupling; a NEW aspect-dependent
#     sim path introduced anywhere else flips this RED.
#
#   CHARACTERIZATION (teeth check): with the widen ON (default) the two aspects
#     DIVERGE. This both documents the live FID-0058 divergence and proves the
#     INVARIANT check has teeth (it can tell converged from diverged). When the
#     eventual decouple fix lands (sim visibility reads the faithful 4:3 frustum
#     while the render cull stays widened), this DIVERGE assertion is expected to
#     flip to CONVERGE — update this block and transition FID-0058 at that point.
#
# Determinism envelope per CHARTER.md rule 6. ROM-gated tier 3 (skips clean when
# the binary or ROM is absent — CI runs the ROM-free ctest lanes instead).
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"
ROOT="$(pwd)"

BIN="${GE007_BIN:-$ROOT/build/ge007}"
ROM="${GE007_ROM:-$ROOT/baserom.u.z64}"
TAPE="${TAPE:-$ROOT/baselines/tapes/dam_forward_30s.ge7tape}"
LEVEL="${LEVEL:-dam}"

# 4:3 vs 16:9 at a fixed HEIGHT (720): only the aspect factor (and thus the
# horizontal widen) differs. 960x720 -> factor 1.0 (widen no-op); 1280x720 ->
# factor 0.75 (widen active).
W43=960; H43=720; W169=1280; H169=720

[ -x "$BIN" ] || { echo "aspect-cull-smoke: SKIP ($BIN not built)"; exit 0; }
[ -e "$ROM" ] || { echo "aspect-cull-smoke: SKIP (ROM $ROM not found — local gate)"; exit 0; }
[ -e "$TAPE" ] || { echo "aspect-cull-smoke: SKIP (tape $TAPE missing)"; exit 0; }

ENVV=(SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1
      GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1)

hash_of() { grep -o '[0-9a-f]\{16\}' "$1" 2>/dev/null | head -1; }

replay() { # $1=W $2=H  [extra env...]; echoes the final sim hash
    local W="$1" H="$2"; shift 2
    local d; d="$(mktemp -d)"
    ( cd "$d" && env "${ENVV[@]}" "$@" GE007_WINDOW_WIDTH="$W" GE007_WINDOW_HEIGHT="$H" \
        "$BIN" --rom "$ROM" --savedir "$d/sd" --level "$LEVEL" --deterministic \
        --play-tape "$TAPE" --sim-state-hash-out "$d/h.json" >"$d/log" 2>&1 )
    hash_of "$d/h.json"
    rm -rf "$d"
}

echo "aspect-cull-smoke: replaying $(basename "$TAPE") at 4:3 vs 16:9 ..."

# INVARIANT: widen OFF -> aspects converge.
off43="$(replay $W43 $H43 GE007_NO_CULL_ASPECT_FIX=1)"
off169="$(replay $W169 $H169 GE007_NO_CULL_ASPECT_FIX=1)"
# CHARACTERIZATION: widen ON (default) -> aspects diverge.
on43="$(replay $W43 $H43)"
on169="$(replay $W169 $H169)"

echo "  widen OFF : 4:3=$off43  16:9=$off169"
echo "  widen ON  : 4:3=$on43  16:9=$on169"

fail=0
if [ -z "$off43" ] || [ -z "$off169" ] || [ -z "$on43" ] || [ -z "$on169" ]; then
    echo "aspect-cull-smoke: FAIL — a replay produced no hash (see run logs)"; exit 1
fi
if [ "$off43" != "$off169" ]; then
    echo "aspect-cull-smoke: FAIL INVARIANT — widen-OFF 4:3 != 16:9 ($off43 != $off169)."
    echo "  A NEW aspect-dependent sim path exists OUTSIDE widenCullHorizontal."
    fail=1
else
    echo "aspect-cull-smoke: PASS INVARIANT — widen-OFF is aspect-invariant ($off43)."
fi
if [ "$on43" = "$on169" ]; then
    echo "aspect-cull-smoke: CHARACTERIZATION FLIPPED — widen-ON 4:3 == 16:9 ($on43)."
    echo "  FID-0058 appears DECOUPLED/FIXED. Update this lane + transition FID-0058."
    fail=1
else
    echo "aspect-cull-smoke: OK CHARACTERIZATION — widen-ON diverges (FID-0058 live): 4:3=$on43 16:9=$on169."
fi

[ "$fail" -eq 0 ] && echo "aspect-cull-smoke: PASS" || echo "aspect-cull-smoke: FAIL"
exit $fail
