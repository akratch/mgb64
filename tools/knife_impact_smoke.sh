#!/bin/bash
#
# knife_impact_smoke.sh -- regression for the throwing-knife-vs-guard crash.
#
# object_interaction's throwknife->on-screen-guard branch (chrobjhandler.c
# ~9529-9547) transforms the hit position through a stack Mtxf (sp58C). The
# decomp had mistyped sp58C as a NULL Mtxf*, so every knife-vs-guard impact
# dereferenced NULL -> SIGSEGV. This smoke warps a guard point-blank in front of
# Bond, equips the throwing knife, and throws it; the PRIMARY assertion is that
# the process survives (exit 0). A secondary audit confirms a Bond knife hit was
# actually registered, so a "no crash because the knife missed" false pass fails.
#
# ROM-derived captures stay local; do not commit them.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=120
OUT_DIR="/tmp/mgb64_knife_impact_$$"
LEVEL=33
CHRNUM=0
WARP_FRAME=60
# FID-0066: 120 -> 90. Under the faithful default guard fire cadence
# (Input.FireRateAuthentic=1) the warped guard's fire/anim schedule shifts and
# the 120-unit throw deterministically missed (0 hit events -> false-red). At 90
# the knife lands on chr 0 under BOTH flag states (default ON: hit frame 258;
# legacy GE007_FIRE_RATE_AUTHENTIC=0: hit frame 192), keeping crash-branch coverage.
WARP_DISTANCE=90
GIVE_FRAME=70
EQUIP_FRAME=85
FIRE_SPEC="110:240"
FRAMES=300

usage() {
    # Unquoted heredoc so option defaults interpolate from the real variables
    # (e.g. --warp-distance) rather than drifting from a hardcoded literal [AUDIT-0021].
    cat <<USAGE
Usage: tools/knife_impact_smoke.sh [options]

Options:
  --level N            raw LEVELID (default: 33 = Dam)
  --chrnum C           guard to warp in front of Bond (default: 0)
  --warp-frame N       frame to warp the guard (default: 60)
  --warp-distance D    warp distance in front of Bond (default: ${WARP_DISTANCE})
  --give-frame N       frame to add knife + ammo (default: 70)
  --equip-frame N      frame to equip the throwing knife (default: 85)
  --fire-spec F:L      AUTO_FIRE window frame:len (default: 110:240)
  --frames N           capture/exit frame (default: 300)
  --out-dir DIR        output dir (default: /tmp/...)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary (default: build/ge007)
  --build-dir DIR      CMake build dir (default: build)
  --no-build           reuse an existing native binary
  --timeout SECONDS    process timeout (default: 120)

Artifacts are ROM-derived local validation data; do not commit them.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --level) LEVEL="$2"; shift 2 ;;
        --chrnum) CHRNUM="$2"; shift 2 ;;
        --warp-frame) WARP_FRAME="$2"; shift 2 ;;
        --warp-distance) WARP_DISTANCE="$2"; shift 2 ;;
        --give-frame) GIVE_FRAME="$2"; shift 2 ;;
        --equip-frame) EQUIP_FRAME="$2"; shift 2 ;;
        --fire-spec) FIRE_SPEC="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
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

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT INT TERM

TRACE="$OUT_DIR/trace.jsonl"
LOG="$OUT_DIR/run.log"

echo "== knife impact: level $LEVEL, guard $CHRNUM warped d=$WARP_DISTANCE, equip knife @$EQUIP_FRAME, fire $FIRE_SPEC =="

GAME_EXIT=0
(
    cd "$OUT_DIR"
    validation_run_with_timeout "$TIMEOUT_SECONDS" \
        env -u GE007_DEBUG \
            SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
            GE007_MUTE=1 \
            GE007_DETERMINISTIC_STABLE_COUNT=1 \
            GE007_NO_VSYNC=1 \
            GE007_BACKGROUND=1 \
            GE007_NO_INPUT_GRAB=1 \
            GE007_DISABLE_LEVEL_INTRO=1 \
            GE007_TRACE_CHRNUM="$CHRNUM" \
            GE007_AUTO_WARP_CHRNUM="$CHRNUM" \
            GE007_AUTO_WARP_CHR_FRAME="$WARP_FRAME" \
            GE007_AUTO_WARP_CHR_DISTANCE="$WARP_DISTANCE" \
            GE007_AUTO_WARP_CHR_ANGLE=0 \
            GE007_AUTO_ADD_ITEM=3 \
            GE007_AUTO_ADD_ITEM_FRAME="$GIVE_FRAME" \
            GE007_AUTO_ADD_WEAPON_AMMO=3 \
            GE007_AUTO_ADD_WEAPON_AMMO_AMOUNT=50 \
            GE007_AUTO_ADD_WEAPON_AMMO_FRAME="$GIVE_FRAME" \
            GE007_AUTO_EQUIP_ITEM=3 \
            GE007_AUTO_EQUIP_ITEM_FRAME="$EQUIP_FRAME" \
            GE007_AUTO_FIRE="$FIRE_SPEC" \
            "$BINARY" \
            --rom "$ROM" \
            --level "$LEVEL" \
            --deterministic \
            --trace-state "$TRACE" \
            --screenshot-frame "$FRAMES" \
            --screenshot-label "knife_impact_$$" \
            --screenshot-exit
) >"$LOG" 2>&1 || GAME_EXIT=$?

# PRIMARY: process survival. A SIGSEGV here is the sp58C regression.
if [[ "$GAME_EXIT" -eq 124 ]]; then
    echo "  process: FAIL (timeout)"; tail -20 "$LOG" | sed 's/^/    /'; exit 1
elif [[ "$GAME_EXIT" -ne 0 ]]; then
    echo "  process: FAIL (exit $GAME_EXIT -- knife-vs-guard crash regressed?)"
    grep -iE 'crash|signal|segmentation|fault|abort' "$LOG" | head -5 | sed 's/^/    /'
    tail -10 "$LOG" | sed 's/^/    /'
    exit 1
fi
echo "  process: PASS (no SIGSEGV on knife-vs-guard impact)"

ASSERTS="$(grep -cF "[GEASSERT]" "$LOG" 2>/dev/null || true)"
if [[ "${ASSERTS:-0}" -ne 0 ]]; then
    echo "  assertions: FAIL ($ASSERTS)"; grep -F "[GEASSERT]" "$LOG" | head -5 | sed 's/^/    /'; exit 1
fi
echo "  assertions: PASS"

# SECONDARY: prove the crashy branch actually ran (knife hit registered).
if python3 tools/audit_knife_impact.py --trace "$TRACE"; then
    echo "knife impact smoke: PASS"
    echo "artifacts: $OUT_DIR"
    exit 0
else
    echo "knife impact smoke: FAIL"
    echo "artifacts: $OUT_DIR"
    exit 1
fi
