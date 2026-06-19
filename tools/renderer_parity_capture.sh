#!/bin/bash
#
# renderer_parity_capture.sh -- Capture local renderer parity scenes.
#
# This script creates screenshots and state traces for known renderer
# compatibility defaults. Captures are generated from the user's ROM and must
# stay local; do not commit or attach the resulting BMP/JSONL artifacts.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=45
TIMEOUT_BIN="$(validation_resolve_timeout_cmd)"
OUT_DIR="/tmp/mgb64_renderer_parity_$$"
SCENE="all"

usage() {
    cat <<'USAGE'
Usage: tools/renderer_parity_capture.sh [options]

Options:
  --scene NAME       all, facility_scissor, surface_sky_fog (default: all)
  --out-dir DIR      output directory for local captures (default: /tmp/...)
  --rom PATH         ROM path (default: ./baserom.u.z64)
  --binary PATH      native binary path (default: build/ge007)
  --build-dir DIR    CMake build directory (default: build)
  --no-build         reuse an existing binary
  --timeout SECONDS  per-capture timeout (default: 45)

Artifacts are ROM-derived local validation data. Do not commit or redistribute
captured screenshots or traces.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --scene) SCENE="$2"; shift 2 ;;
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
fi

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"
validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT INT TERM

mkdir -p "$OUT_DIR"

run_capture() {
    local scene_name="$1"
    local variant="$2"
    local level="$3"
    local frame="$4"
    local env_key="$5"
    local env_value="$6"
    local scene_dir="$OUT_DIR/$scene_name"
    local label="${scene_name}_${variant}_$$"
    local screenshot_src="screenshot_${label}.bmp"
    local screenshot_dst="$scene_dir/${variant}.bmp"
    local trace="$scene_dir/${variant}.jsonl"
    local log="$scene_dir/${variant}.log"
    local env_cmd=()

    mkdir -p "$scene_dir"
    rm -f "$screenshot_src" "$screenshot_dst" "$trace" "$log"

    echo "  capture: ${scene_name}/${variant} level=${level} frame=${frame}${env_key:+ ${env_key}=${env_value}}"

    env_cmd=(env -u GE007_DEBUG \
        SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
        GE007_MUTE=1 \
        GE007_DETERMINISTIC_STABLE_COUNT=1 \
        GE007_NO_VSYNC=1 \
        GE007_BACKGROUND=1 \
        GE007_NO_INPUT_GRAB=1)
    if [[ -n "$env_key" ]]; then
        env_cmd+=("${env_key}=${env_value}")
    fi
    env_cmd+=("$BINARY"
        --rom "$ROM"
        --level "$level"
        --deterministic
        --trace-state "$trace"
        --screenshot-frame "$frame"
        --screenshot-label "$label"
        --screenshot-exit)

    if [[ -n "$TIMEOUT_BIN" ]]; then
        if ! "$TIMEOUT_BIN" --kill-after=5 "$TIMEOUT_SECONDS" "${env_cmd[@]}" >"$log" 2>&1; then
            echo "FAIL: capture failed for ${scene_name}/${variant}"
            tail -40 "$log" | sed 's/^/  /'
            exit 1
        fi
    elif ! "${env_cmd[@]}" >"$log" 2>&1; then
        echo "FAIL: capture failed for ${scene_name}/${variant}"
        tail -40 "$log" | sed 's/^/  /'
        exit 1
    fi

    if [[ ! -s "$screenshot_src" ]]; then
        echo "FAIL: missing screenshot for ${scene_name}/${variant}: $screenshot_src"
        tail -40 "$log" | sed 's/^/  /'
        exit 1
    fi
    mv "$screenshot_src" "$screenshot_dst"

    if [[ ! -s "$trace" ]]; then
        echo "FAIL: missing state trace for ${scene_name}/${variant}: $trace"
        exit 1
    fi

    if grep -qF "[GEASSERT]" "$log"; then
        echo "FAIL: GEASSERT fired during ${scene_name}/${variant}"
        grep -F "[GEASSERT]" "$log" | head -5 | sed 's/^/  /'
        exit 1
    fi

    python3 - "$trace" <<'PY'
import json
import sys

last = None
with open(sys.argv[1], "r", encoding="utf-8", errors="replace") as handle:
    for line in handle:
        if line.startswith("{"):
            last = json.loads(line)
if not last:
    raise SystemExit("FAIL: no JSON state frames")
summary = {
    "frame": last.get("f"),
    "tris": last.get("tris"),
    "fog": last.get("fog"),
    "fog_mul": last.get("fog_mul"),
    "fog_off": last.get("fog_off"),
    "rooms": last.get("rooms"),
}
print("    trace:", json.dumps(summary, sort_keys=True))
PY
}

print_compare_commands() {
    local scene_name="$1"
    local a="$OUT_DIR/$scene_name/compat"
    local b="$OUT_DIR/$scene_name/diagnostic"

    echo ""
    echo "  compare ${scene_name}:"
    echo "    python3 tools/compare_screenshots.py \\"
    echo "      ${a}.bmp \\"
    echo "      ${b}.bmp"
    echo "    python3 tools/compare_state.py \\"
    echo "      ${a}.jsonl \\"
    echo "      ${b}.jsonl"
}

run_facility_scissor() {
    echo ""
    echo "=== Renderer Scene: facility_scissor ==="
    echo "Expected: compat keeps room scissor disabled; diagnostic enables exact N64 room scissor."
    echo "Use this to inspect interior seam/under-cover regressions."
    run_capture "facility_scissor" "compat" "34" "180" "" ""
    run_capture "facility_scissor" "diagnostic" "34" "180" "GE007_EXACT_ROOM_SCISSOR" "1"
    print_compare_commands "facility_scissor"
}

run_surface_sky_fog() {
    echo ""
    echo "=== Renderer Scene: surface_sky_fog ==="
    echo "Expected: compat uses portal-BFS room admission; diagnostic disables it as a broad fallback."
    echo "Use this to inspect outdoor sky/fog state and room-admission side effects."
    run_capture "surface_sky_fog" "compat" "36" "180" "" ""
    run_capture "surface_sky_fog" "diagnostic" "36" "180" "GE007_PORTAL_BFS" "0"
    print_compare_commands "surface_sky_fog"
}

echo "=== Renderer Parity Capture ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"

case "$SCENE" in
    all)
        run_facility_scissor
        run_surface_sky_fog
        ;;
    facility_scissor)
        run_facility_scissor
        ;;
    surface_sky_fog)
        run_surface_sky_fog
        ;;
    *)
        echo "Unknown scene: $SCENE" >&2
        usage >&2
        exit 2
        ;;
esac

echo ""
echo "=== Renderer Parity Capture: PASS ==="
echo "Artifacts are local ROM-derived validation data. Keep them out of git."
