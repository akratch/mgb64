#!/bin/bash
#
# movement_oracle_capture.sh -- Capture and compare ROM-oracle traces.
#
# Native captures are self-contained. Stock-ROM captures require an instrumented
# ares binary prepared with tools/prepare_ares_movement_oracle_build.sh.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
ROUTE="dam_forward_stop"
OUT_DIR="/tmp/mgb64_movement_oracle_$$"
DO_BUILD=1
TIMEOUT_SECONDS=60
ARES_BIN=""
STOCK_TRACE=""
NATIVE_ONLY=0
NO_COMPARE=0
COMPARE_ALIGN=""

usage() {
    cat <<'USAGE'
Usage: tools/movement_oracle_capture.sh [options]

Options:
  --route NAME|PATH     route spec (default: dam_forward_stop)
  --out-dir DIR         output directory (default: /tmp/...)
  --rom PATH            ROM path (default: ./baserom.u.z64)
  --binary PATH         native binary path (default: build/ge007)
  --build-dir DIR       CMake build directory (default: build)
  --no-build            reuse an existing native binary
  --ares-bin PATH       instrumented ares binary for stock-ROM capture
  --stock-trace PATH    compare against an existing stock/emulator JSONL trace
  --native-only         only capture native trace
  --no-compare          capture traces but do not run the route comparator
  --align MODE          comparator alignment (default: route compare_align)
  --timeout SECONDS     per-process timeout (default: 60)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, saves, or emulator output.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --route) ROUTE="$2"; shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --ares-bin) ARES_BIN="$2"; shift 2 ;;
        --stock-trace) STOCK_TRACE="$2"; shift 2 ;;
        --native-only) NATIVE_ONLY=1; shift ;;
        --no-compare) NO_COMPARE=1; shift ;;
        --align) COMPARE_ALIGN="$2"; shift 2 ;;
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
if [[ -n "$ARES_BIN" ]]; then
    validation_require_binary "$ARES_BIN"
fi
if [[ -n "$STOCK_TRACE" ]]; then
    validation_require_file "$STOCK_TRACE" "stock trace"
fi

ROUTE_PATH="$(python3 tools/rom_oracle_route.py resolve "$ROUTE")"
python3 tools/rom_oracle_route.py validate "$ROUTE_PATH"
ROUTE_NAME="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" name)"
LEVEL="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" level)"
NATIVE_LEVEL="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_level)"
STOCK_LEVEL="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_level)"
NATIVE_FRAMES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_frames)"
STOCK_FRAMES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_frames)"
NATIVE_SPEEDFRAMES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_speedframes)"
STOCK_SPEEDFRAMES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_speedframes)"
STOCK_GAMEPLAY_START_GLOBAL="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_gameplay_start_global)"
STOCK_MENU_CLOSE_ON_PLAYER="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_menu_close_on_player)"
NATIVE_RENDER_AUDIT="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_render_audit)"
NATIVE_MIN_MOVING_RECORDS="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_min_moving_records)"
STOCK_MIN_MOVING_RECORDS="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_min_moving_records)"
STOCK_MIN_GAMEPLAY_INPUT_RECORDS="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_min_gameplay_input_records)"
STOCK_MAX_SUPPRESSED_MENU_RECORDS="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_max_suppressed_menu_records)"
STOCK_MIN_MENU_TO_GAMEPLAY_GAP="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" stock_min_menu_to_gameplay_gap)"
if [[ -z "$COMPARE_ALIGN" ]]; then
    COMPARE_ALIGN="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_align)"
fi
COMPARE_PROFILE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_profile)"
COMPARE_KIND="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_kind)"
COMPARE_MAX_ALIGNED="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_max_aligned)"
COMPARE_MIN_ALIGNED="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_min_aligned)"
COMPARE_NORMALIZE_POSITION="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_normalize_position)"
COMPARE_GAMEPLAY_WINDOWS="$(python3 tools/rom_oracle_route.py gameplay-windows "$ROUTE_PATH")"
COMPARE_CAMERA_MODES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_camera_modes)"
COMPARE_START_ACTIVE_FRAME="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_start_active_frame)"
COMPARE_START_INTRO_TIMER="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_start_intro_timer)"
COMPARE_END_INTRO_TIMER="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_end_intro_timer)"
COMPARE_SAMPLE_STEP="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_sample_step)"
COMPARE_VECTOR_TOLERANCE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_vector_tolerance)"
COMPARE_DIRECTION_TOLERANCE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_direction_tolerance)"
COMPARE_SCALAR_TOLERANCE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_scalar_tolerance)"
COMPARE_ANIM_TOLERANCE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_anim_tolerance)"
COMPARE_STATE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_state)"
COMPARE_SELECTED_CAMERA="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_selected_camera)"
COMPARE_INTRO_SETUP="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_intro_setup)"
COMPARE_BOND_ANIM="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_bond_anim)"
COMPARE_EXCLUDE_FIELDS="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_exclude_fields)"
COMPARE_REQUIRE_FROZEN="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" compare_require_frozen)"
NATIVE_INTRO_AUDIT="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_audit)"
NATIVE_INTRO_CAMERA_MODES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_camera_modes)"
NATIVE_INTRO_REQUIRE_FROZEN="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_require_frozen)"
NATIVE_INTRO_REQUIRE_PLAYER="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_require_player)"
NATIVE_INTRO_REQUIRE_BOND_PRESENT="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_require_bond_present)"
NATIVE_INTRO_REQUIRE_BOND_ONSCREEN="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_require_bond_onscreen)"
NATIVE_INTRO_REQUIRE_BOND_MODEL_MTX="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_require_bond_model_mtx)"
NATIVE_INTRO_REQUIRE_BOND_RENDERED="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_require_bond_rendered)"
NATIVE_INTRO_REQUIRE_BOND_ANIM="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_require_bond_anim)"
NATIVE_INTRO_REQUIRE_BOND_ANIM_HASH="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_require_bond_anim_hash)"
NATIVE_INTRO_REQUIRE_BOND_RIGHT_ITEM="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_require_bond_right_item)"
NATIVE_INTRO_MIN_ACTIVE_RECORDS="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_min_active_records)"
NATIVE_INTRO_MIN_PRESENT_FRAMES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_min_present_frames)"
NATIVE_INTRO_MIN_ONSCREEN_FRAMES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_min_onscreen_frames)"
NATIVE_INTRO_MIN_MODEL_MTX_FRAMES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_min_model_mtx_frames)"
NATIVE_INTRO_MIN_RENDERED_FRAMES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_min_rendered_frames)"
NATIVE_INTRO_MIN_RENDER_COUNT="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_min_render_count)"
NATIVE_INTRO_MIN_ANIM_FRAMES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_min_anim_frames)"
NATIVE_INTRO_MIN_ANIM_HASH_FRAMES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_min_anim_hash_frames)"
NATIVE_INTRO_MIN_ANIM_ADVANCE="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_min_anim_advance)"
NATIVE_INTRO_MIN_RIGHT_ITEM_FRAMES="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_min_right_item_frames)"
NATIVE_INTRO_MAX_FIRST_PRESENT_FRAME="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_max_first_present_frame)"
NATIVE_INTRO_MAX_FIRST_RENDER_FRAME="$(python3 tools/rom_oracle_route.py field "$ROUTE_PATH" native_intro_max_first_render_frame)"

for value_name in LEVEL NATIVE_LEVEL STOCK_LEVEL; do
    value="${!value_name}"
    if [[ ! "$value" =~ ^-?[0-9]+$ ]]; then
        echo "FAIL: route ${value_name} must be an integer LEVELID: $value" >&2
        exit 2
    fi
done
if [[ ! "$NATIVE_FRAMES" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: route native_frames must be a positive integer: $NATIVE_FRAMES" >&2
    exit 2
fi
if [[ ! "$STOCK_FRAMES" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: route stock_frames must be a positive integer: $STOCK_FRAMES" >&2
    exit 2
fi
if [[ -n "$NATIVE_SPEEDFRAMES" && ! "$NATIVE_SPEEDFRAMES" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: route native_speedframes must be a positive integer when set: $NATIVE_SPEEDFRAMES" >&2
    exit 2
fi
if [[ -n "$STOCK_SPEEDFRAMES" && ! "$STOCK_SPEEDFRAMES" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: route stock_speedframes must be a positive integer when set: $STOCK_SPEEDFRAMES" >&2
    exit 2
fi
if [[ -n "$STOCK_GAMEPLAY_START_GLOBAL" && ! "$STOCK_GAMEPLAY_START_GLOBAL" =~ ^-?[0-9]+$ ]]; then
    echo "FAIL: route stock_gameplay_start_global must be an integer when set: $STOCK_GAMEPLAY_START_GLOBAL" >&2
    exit 2
fi
for value_name in \
    NATIVE_MIN_MOVING_RECORDS \
    STOCK_MIN_MOVING_RECORDS \
    STOCK_MIN_GAMEPLAY_INPUT_RECORDS \
    STOCK_MAX_SUPPRESSED_MENU_RECORDS \
    STOCK_MIN_MENU_TO_GAMEPLAY_GAP
do
    value="${!value_name}"
    if [[ -n "$value" && ! "$value" =~ ^[0-9]+$ ]]; then
        echo "FAIL: route ${value_name} must be a non-negative integer when set: $value" >&2
        exit 2
    fi
done
for value_name in \
    NATIVE_MIN_MOVING_RECORDS \
    STOCK_MIN_MOVING_RECORDS \
    STOCK_MIN_GAMEPLAY_INPUT_RECORDS
do
    value="${!value_name}"
    if [[ "$value" == "0" ]]; then
        echo "FAIL: route ${value_name} must be positive when set: $value" >&2
        exit 2
    fi
done
case "$STOCK_MENU_CLOSE_ON_PLAYER" in
    1|true|True|TRUE|yes|YES|on|ON) STOCK_MENU_CLOSE_ON_PLAYER=1 ;;
    0|false|False|FALSE|no|NO|off|OFF) STOCK_MENU_CLOSE_ON_PLAYER=0 ;;
    *)
        echo "FAIL: route stock_menu_close_on_player must be boolean: $STOCK_MENU_CLOSE_ON_PLAYER" >&2
        exit 2
        ;;
esac
case "$COMPARE_KIND" in
    movement|intro) ;;
    *)
        echo "FAIL: route compare_kind must be movement or intro: $COMPARE_KIND" >&2
        exit 2
        ;;
esac
if [[ "$COMPARE_KIND" == "movement" ]]; then
    case "$COMPARE_ALIGN" in
        global|frame|index|move|gameplay-frame) ;;
        *)
            echo "FAIL: movement --align must be global, frame, index, move, or gameplay-frame: $COMPARE_ALIGN" >&2
            exit 2
            ;;
    esac
    case "$COMPARE_PROFILE" in
        full|dynamics|scalar-speed|timing) ;;
        *)
            echo "FAIL: movement compare_profile must be full, dynamics, scalar-speed, or timing: $COMPARE_PROFILE" >&2
            exit 2
            ;;
    esac
else
    case "$COMPARE_ALIGN" in
    active-index|global|frame|intro-timer) ;;
        *)
        echo "FAIL: intro --align must be active-index, global, frame, or intro-timer: $COMPARE_ALIGN" >&2
        exit 2
        ;;
esac
    case "$COMPARE_PROFILE" in
        path|scalar|state|full) ;;
        *)
            echo "FAIL: intro compare_profile must be path, scalar, state, or full: $COMPARE_PROFILE" >&2
            exit 2
            ;;
    esac
fi
if [[ -n "$COMPARE_MAX_ALIGNED" && ! "$COMPARE_MAX_ALIGNED" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: route compare_max_aligned must be a positive integer when set: $COMPARE_MAX_ALIGNED" >&2
    exit 2
fi
if [[ -n "$COMPARE_MIN_ALIGNED" && ! "$COMPARE_MIN_ALIGNED" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: route compare_min_aligned must be a positive integer when set: $COMPARE_MIN_ALIGNED" >&2
    exit 2
fi
if [[ -n "$COMPARE_START_ACTIVE_FRAME" && ! "$COMPARE_START_ACTIVE_FRAME" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: route compare_start_active_frame must be a positive integer when set: $COMPARE_START_ACTIVE_FRAME" >&2
    exit 2
fi
if [[ -n "$COMPARE_SAMPLE_STEP" && ! "$COMPARE_SAMPLE_STEP" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: route compare_sample_step must be a positive integer when set: $COMPARE_SAMPLE_STEP" >&2
    exit 2
fi
for value_name in \
    NATIVE_INTRO_MIN_ACTIVE_RECORDS \
    NATIVE_INTRO_MIN_PRESENT_FRAMES \
    NATIVE_INTRO_MIN_ONSCREEN_FRAMES \
    NATIVE_INTRO_MIN_MODEL_MTX_FRAMES \
    NATIVE_INTRO_MIN_RENDERED_FRAMES \
    NATIVE_INTRO_MIN_RENDER_COUNT \
    NATIVE_INTRO_MIN_ANIM_FRAMES \
    NATIVE_INTRO_MIN_ANIM_HASH_FRAMES \
    NATIVE_INTRO_MIN_RIGHT_ITEM_FRAMES \
    NATIVE_INTRO_MAX_FIRST_PRESENT_FRAME \
    NATIVE_INTRO_MAX_FIRST_RENDER_FRAME
do
    value="${!value_name}"
    if [[ -n "$value" && ! "$value" =~ ^[1-9][0-9]*$ ]]; then
        echo "FAIL: route ${value_name} must be a positive integer when set: $value" >&2
        exit 2
    fi
done
if [[ -n "$NATIVE_INTRO_REQUIRE_BOND_RIGHT_ITEM" && ! "$NATIVE_INTRO_REQUIRE_BOND_RIGHT_ITEM" =~ ^[0-9]+$ ]]; then
    echo "FAIL: route native_intro_require_bond_right_item must be a non-negative integer when set: $NATIVE_INTRO_REQUIRE_BOND_RIGHT_ITEM" >&2
    exit 2
fi
if [[ -n "$NATIVE_INTRO_MIN_ANIM_ADVANCE" && ! "$NATIVE_INTRO_MIN_ANIM_ADVANCE" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "FAIL: route native_intro_min_anim_advance must be a positive number when set: $NATIVE_INTRO_MIN_ANIM_ADVANCE" >&2
    exit 2
fi

if [[ "$STOCK_FRAMES" -lt "$NATIVE_FRAMES" ]]; then
    echo "FAIL: route stock_frames must be >= native_frames" >&2
    exit 2
fi

mkdir -p "$OUT_DIR"
OUT_DIR="$(python3 - "$OUT_DIR" <<'PY'
import os
import sys
print(os.path.abspath(sys.argv[1]))
PY
)"

NATIVE_TRACE="$OUT_DIR/native_${ROUTE_NAME}.jsonl"
NATIVE_LOG="$OUT_DIR/native_${ROUTE_NAME}.log"
COMPARE_JSON="$OUT_DIR/compare_${ROUTE_NAME}.json"
SUMMARY_COMPARE_JSON="$OUT_DIR/summary_compare_${ROUTE_NAME}.json"
CAPTURE_SUMMARY_JSON="$OUT_DIR/summary_${ROUTE_NAME}.json"
NATIVE_SCREENSHOT_JSON="$OUT_DIR/native_${ROUTE_NAME}.screenshot.json"
NATIVE_RENDER_JSON="$OUT_DIR/native_${ROUTE_NAME}.render.json"
NATIVE_MOVEMENT_JSON="$OUT_DIR/native_${ROUTE_NAME}.movement.json"
NATIVE_INTRO_SUMMARY_JSON="$OUT_DIR/native_${ROUTE_NAME}.intro-summary.json"
NATIVE_INTRO_AUDIT_JSON="$OUT_DIR/native_${ROUTE_NAME}.intro-audit.json"
STOCK_AUDIT_JSON="$OUT_DIR/stock_${ROUTE_NAME}.audit.json"
STOCK_OUT_TRACE="${STOCK_TRACE:-$OUT_DIR/stock_${ROUTE_NAME}.jsonl}"
STOCK_LOG="$OUT_DIR/stock_${ROUTE_NAME}.log"
ARES_INPUT="$OUT_DIR/${ROUTE_NAME}.ares-input"
SETTINGS_FILE="$OUT_DIR/ares_settings.bml"
SAVES_DIR="$OUT_DIR/ares_saves"
SCREENSHOT_LABEL="rom_oracle_${ROUTE_NAME}_$$"
SCREENSHOT_SRC="screenshot_${SCREENSHOT_LABEL}.bmp"
SCREENSHOT_DST="$OUT_DIR/native_${ROUTE_NAME}.bmp"

validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT INT TERM

run_native_capture() {
    local env_cmd=()
    local native_env=()
    local native_timing_env=()

    while IFS= read -r line; do
        native_env+=("$line")
    done < <(python3 tools/rom_oracle_route.py native-env "$ROUTE_PATH")
    if [[ -n "$NATIVE_SPEEDFRAMES" ]]; then
        native_timing_env+=(GE007_DETERMINISTIC_SPEEDFRAMES="$NATIVE_SPEEDFRAMES")
    fi
    rm -f "$NATIVE_TRACE" "$NATIVE_LOG" "$SCREENSHOT_SRC" "$SCREENSHOT_DST"
    rm -f "$NATIVE_SCREENSHOT_JSON" "$NATIVE_RENDER_JSON" "$NATIVE_MOVEMENT_JSON"
    rm -f "$NATIVE_INTRO_SUMMARY_JSON" "$NATIVE_INTRO_AUDIT_JSON" "$CAPTURE_SUMMARY_JSON"

    env_cmd=(env -u GE007_DEBUG
        SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}"
        GE007_MUTE=1
        GE007_DETERMINISTIC_STABLE_COUNT=1
        GE007_NO_VSYNC=1
        GE007_BACKGROUND=1
        GE007_NO_INPUT_GRAB=1)
    if [[ "$COMPARE_KIND" == "movement" ]]; then
        env_cmd+=(GE007_TRACE_FLOW_ONLY=1)
    fi
    if [[ "${#native_timing_env[@]}" -gt 0 ]]; then
        env_cmd+=("${native_timing_env[@]}")
    fi
    if [[ "${#native_env[@]}" -gt 0 ]]; then
        env_cmd+=("${native_env[@]}")
    fi
    env_cmd+=(
        "$BINARY"
        --rom "$ROM"
        --level "$NATIVE_LEVEL"
        --deterministic
        --trace-state "$NATIVE_TRACE"
        --screenshot-frame "$NATIVE_FRAMES"
        --screenshot-label "$SCREENSHOT_LABEL"
        --screenshot-exit)

    echo "  native: route=${ROUTE_NAME} level=${NATIVE_LEVEL} frames=${NATIVE_FRAMES}"
    if ! validation_run_with_timeout "$TIMEOUT_SECONDS" "${env_cmd[@]}" >"$NATIVE_LOG" 2>&1; then
        echo "FAIL: native oracle capture failed" >&2
        tail -40 "$NATIVE_LOG" | sed 's/^/  /' >&2
        exit 1
    fi
    if [[ ! -s "$NATIVE_TRACE" ]]; then
        echo "FAIL: native oracle trace was not written: $NATIVE_TRACE" >&2
        tail -40 "$NATIVE_LOG" | sed 's/^/  /' >&2
        exit 1
    fi
    if [[ ! -s "$SCREENSHOT_SRC" ]]; then
        echo "FAIL: native oracle screenshot was not written: $SCREENSHOT_SRC" >&2
        tail -40 "$NATIVE_LOG" | sed 's/^/  /' >&2
        exit 1
    fi
    mv "$SCREENSHOT_SRC" "$SCREENSHOT_DST"
    python3 tools/audit_screenshot_health.py \
        --label "native ${ROUTE_NAME} screenshot" \
        --expect-size 640x480 \
        --json-out "$NATIVE_SCREENSHOT_JSON" \
        "$SCREENSHOT_DST"
}

run_stock_capture() {
    local ares_pid=""
    local elapsed=0
    local trace_lines=0
    local stock_timing_env=()
    local stock_cmd=()

    python3 tools/rom_oracle_route.py ares-input "$ROUTE_PATH" >"$ARES_INPUT"
    mkdir -p "$SAVES_DIR"
    rm -f "$STOCK_OUT_TRACE" "$STOCK_LOG"

    if [[ -n "$STOCK_SPEEDFRAMES" ]]; then
        stock_timing_env+=(MGB64_ARES_GAMEPLAY_SPEEDFRAMES="$STOCK_SPEEDFRAMES")
    fi
    if [[ -n "$STOCK_GAMEPLAY_START_GLOBAL" ]]; then
        stock_timing_env+=(MGB64_ARES_GAMEPLAY_START_GLOBAL="$STOCK_GAMEPLAY_START_GLOBAL")
    fi
    stock_timing_env+=(MGB64_ARES_CLOSE_MENU_ON_PLAYER="$STOCK_MENU_CLOSE_ON_PLAYER")

    echo "  stock:  route=${ROUTE_NAME} level=${STOCK_LEVEL} frames=${STOCK_FRAMES}"
    stock_cmd=(env
        MGB64_ARES_ORACLE_TRACE="$STOCK_OUT_TRACE" \
        MGB64_ARES_MOVEMENT_TRACE="$STOCK_OUT_TRACE" \
        MGB64_ARES_INPUT_SCRIPT="$ARES_INPUT" \
        MGB64_ARES_FRAME_LIMIT="$STOCK_FRAMES" \
        MGB64_ARES_TARGET_STAGE="$STOCK_LEVEL")
    if [[ "${#stock_timing_env[@]}" -gt 0 ]]; then
        stock_cmd+=("${stock_timing_env[@]}")
    fi
    stock_cmd+=(
        "$ARES_BIN" \
        --kiosk \
        --settings-file "$SETTINGS_FILE" \
        --setting Audio/Mute=true \
        --setting Audio/Blocking="${MGB64_ARES_AUDIO_BLOCKING:-false}" \
        --setting Audio/Dynamic="${MGB64_ARES_AUDIO_DYNAMIC:-false}" \
        --setting Input/Defocus=Allow \
        --setting General/AutoSaveMemory=false \
        --setting Paths/Saves="$SAVES_DIR" \
        --setting Boot/Fast=true \
        --setting Video/Blocking="${MGB64_ARES_VIDEO_BLOCKING:-false}" \
        --system "Nintendo 64" \
        --no-file-prompt \
        "$ROM")
    "${stock_cmd[@]}" >"$STOCK_LOG" 2>&1 &
    ares_pid="$!"

    while kill -0 "$ares_pid" 2>/dev/null; do
        if [[ -f "$STOCK_OUT_TRACE" ]]; then
            trace_lines="$(wc -l < "$STOCK_OUT_TRACE" | tr -d '[:space:]')"
            if [[ "$trace_lines" =~ ^[0-9]+$ && "$trace_lines" -ge "$STOCK_FRAMES" ]]; then
                break
            fi
        fi
        if [[ "$elapsed" -ge "$TIMEOUT_SECONDS" ]]; then
            break
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done

    if kill -0 "$ares_pid" 2>/dev/null; then
        kill "$ares_pid" 2>/dev/null || true
        sleep 1
        if kill -0 "$ares_pid" 2>/dev/null; then
            kill -9 "$ares_pid" 2>/dev/null || true
        fi
        wait "$ares_pid" 2>/dev/null || true
    else
        wait "$ares_pid" 2>/dev/null || true
    fi

    if [[ ! -s "$STOCK_OUT_TRACE" ]]; then
        echo "FAIL: stock oracle trace was not written: $STOCK_OUT_TRACE" >&2
        echo "This usually means the selected ares binary lacks MGB64 oracle instrumentation." >&2
        tail -60 "$STOCK_LOG" | sed 's/^/  /' >&2
        exit 1
    fi
    trace_lines="$(wc -l < "$STOCK_OUT_TRACE" | tr -d '[:space:]')"
    if [[ ! "$trace_lines" =~ ^[0-9]+$ || "$trace_lines" -lt "$STOCK_FRAMES" ]]; then
        echo "FAIL: stock oracle trace stopped at ${trace_lines:-0}/${STOCK_FRAMES} frame(s)" >&2
        tail -60 "$STOCK_LOG" | sed 's/^/  /' >&2
        exit 1
    fi
}

echo "=== ROM Oracle Capture ==="
echo "  out-dir: $OUT_DIR"
echo "  route:   $ROUTE_PATH"
echo "  native:  $BINARY"
echo "  ROM:     $ROM"

run_native_capture

    case "$NATIVE_RENDER_AUDIT" in
    1|true|True|TRUE|yes|YES|on|ON)
        echo ""
        python3 tools/audit_render_trace.py \
            --label "native ${ROUTE_NAME}" \
            --json-out "$NATIVE_RENDER_JSON" \
            "$NATIVE_TRACE"
        ;;
esac

if [[ "$COMPARE_KIND" == "movement" && -n "$NATIVE_MIN_MOVING_RECORDS" ]]; then
    echo ""
    native_audit_args=(--kind movement --label "native ${ROUTE_NAME}" --require-stage "$NATIVE_LEVEL" --require-target-player)
    native_audit_args+=(--min-moving-records "$NATIVE_MIN_MOVING_RECORDS")
    native_audit_args+=(--json-out "$NATIVE_MOVEMENT_JSON")
    python3 tools/audit_oracle_trace.py "${native_audit_args[@]}" "$NATIVE_TRACE"
fi

intro_summary_common_args=()
intro_summary_profile=""
if [[ "$COMPARE_KIND" == "intro" ]]; then
    intro_summary_common_args=()
    if [[ -n "$COMPARE_CAMERA_MODES" ]]; then
        intro_summary_common_args+=(--camera-modes "$COMPARE_CAMERA_MODES")
    fi
    if [[ -n "$COMPARE_START_INTRO_TIMER" ]]; then
        intro_summary_common_args+=(--start-intro-timer "$COMPARE_START_INTRO_TIMER")
    fi
    if [[ -n "$COMPARE_END_INTRO_TIMER" ]]; then
        intro_summary_common_args+=(--end-intro-timer "$COMPARE_END_INTRO_TIMER")
    fi
    case "$COMPARE_REQUIRE_FROZEN" in
        1|true|True|TRUE|yes|YES)
            intro_summary_common_args+=(--require-frozen)
            ;;
    esac

    case "$COMPARE_INTRO_SETUP" in
        1|true|True|TRUE|yes|YES)
            intro_summary_profile="${intro_summary_profile},setup"
            ;;
    esac
    case "$COMPARE_SELECTED_CAMERA" in
        1|true|True|TRUE|yes|YES)
            intro_summary_profile="${intro_summary_profile},selected-camera"
            ;;
    esac
    case "$COMPARE_BOND_ANIM" in
        1|true|True|TRUE|yes|YES)
            intro_summary_profile="${intro_summary_profile},bond-anim"
            ;;
    esac
    intro_summary_profile="${intro_summary_profile#,}"
    if [[ -z "$intro_summary_profile" ]]; then
        intro_summary_profile="setup,selected-camera"
    fi

    echo ""
    python3 tools/intro_trace_summary.py \
        "${intro_summary_common_args[@]}" \
        --json-out "$NATIVE_INTRO_SUMMARY_JSON" \
        "$NATIVE_TRACE"
fi

case "$NATIVE_INTRO_AUDIT" in
    1|true|True|TRUE|yes|YES|on|ON)
        echo ""
        intro_audit_args=(--label "native ${ROUTE_NAME}")
        if [[ -n "$NATIVE_INTRO_CAMERA_MODES" ]]; then
            intro_audit_args+=(--camera-modes "$NATIVE_INTRO_CAMERA_MODES")
        elif [[ -n "$COMPARE_CAMERA_MODES" ]]; then
            intro_audit_args+=(--camera-modes "$COMPARE_CAMERA_MODES")
        fi
        case "${NATIVE_INTRO_REQUIRE_FROZEN:-$COMPARE_REQUIRE_FROZEN}" in
            1|true|True|TRUE|yes|YES|on|ON)
                intro_audit_args+=(--require-frozen)
                ;;
        esac
        case "$NATIVE_INTRO_REQUIRE_PLAYER" in
            1|true|True|TRUE|yes|YES|on|ON)
                intro_audit_args+=(--require-player)
                ;;
        esac
        case "$NATIVE_INTRO_REQUIRE_BOND_PRESENT" in
            1|true|True|TRUE|yes|YES|on|ON)
                intro_audit_args+=(--require-bond-present)
                ;;
        esac
        case "$NATIVE_INTRO_REQUIRE_BOND_ONSCREEN" in
            1|true|True|TRUE|yes|YES|on|ON)
                intro_audit_args+=(--require-bond-onscreen)
                ;;
        esac
        case "$NATIVE_INTRO_REQUIRE_BOND_MODEL_MTX" in
            1|true|True|TRUE|yes|YES|on|ON)
                intro_audit_args+=(--require-bond-model-mtx)
                ;;
        esac
        case "$NATIVE_INTRO_REQUIRE_BOND_RENDERED" in
            1|true|True|TRUE|yes|YES|on|ON)
                intro_audit_args+=(--require-bond-rendered)
                ;;
        esac
        case "$NATIVE_INTRO_REQUIRE_BOND_ANIM" in
            1|true|True|TRUE|yes|YES|on|ON)
                intro_audit_args+=(--require-bond-anim)
                ;;
        esac
        case "$NATIVE_INTRO_REQUIRE_BOND_ANIM_HASH" in
            1|true|True|TRUE|yes|YES|on|ON)
                intro_audit_args+=(--require-bond-anim-hash)
                ;;
        esac
        if [[ -n "$NATIVE_INTRO_REQUIRE_BOND_RIGHT_ITEM" ]]; then
            intro_audit_args+=(--require-right-item "$NATIVE_INTRO_REQUIRE_BOND_RIGHT_ITEM")
        fi
        if [[ -n "$NATIVE_INTRO_MIN_ACTIVE_RECORDS" ]]; then
            intro_audit_args+=(--min-active-records "$NATIVE_INTRO_MIN_ACTIVE_RECORDS")
        fi
        if [[ -n "$NATIVE_INTRO_MIN_PRESENT_FRAMES" ]]; then
            intro_audit_args+=(--min-present-frames "$NATIVE_INTRO_MIN_PRESENT_FRAMES")
        fi
        if [[ -n "$NATIVE_INTRO_MIN_ONSCREEN_FRAMES" ]]; then
            intro_audit_args+=(--min-onscreen-frames "$NATIVE_INTRO_MIN_ONSCREEN_FRAMES")
        fi
        if [[ -n "$NATIVE_INTRO_MIN_MODEL_MTX_FRAMES" ]]; then
            intro_audit_args+=(--min-model-mtx-frames "$NATIVE_INTRO_MIN_MODEL_MTX_FRAMES")
        fi
        if [[ -n "$NATIVE_INTRO_MIN_RENDERED_FRAMES" ]]; then
            intro_audit_args+=(--min-rendered-frames "$NATIVE_INTRO_MIN_RENDERED_FRAMES")
        fi
        if [[ -n "$NATIVE_INTRO_MIN_RENDER_COUNT" ]]; then
            intro_audit_args+=(--min-render-count "$NATIVE_INTRO_MIN_RENDER_COUNT")
        fi
        if [[ -n "$NATIVE_INTRO_MIN_ANIM_FRAMES" ]]; then
            intro_audit_args+=(--min-anim-frames "$NATIVE_INTRO_MIN_ANIM_FRAMES")
        fi
        if [[ -n "$NATIVE_INTRO_MIN_ANIM_HASH_FRAMES" ]]; then
            intro_audit_args+=(--min-anim-hash-frames "$NATIVE_INTRO_MIN_ANIM_HASH_FRAMES")
        fi
        if [[ -n "$NATIVE_INTRO_MIN_ANIM_ADVANCE" ]]; then
            intro_audit_args+=(--min-anim-advance "$NATIVE_INTRO_MIN_ANIM_ADVANCE")
        fi
        if [[ -n "$NATIVE_INTRO_MIN_RIGHT_ITEM_FRAMES" ]]; then
            intro_audit_args+=(--min-right-item-frames "$NATIVE_INTRO_MIN_RIGHT_ITEM_FRAMES")
        fi
        if [[ -n "$NATIVE_INTRO_MAX_FIRST_PRESENT_FRAME" ]]; then
            intro_audit_args+=(--max-first-present-frame "$NATIVE_INTRO_MAX_FIRST_PRESENT_FRAME")
        fi
        if [[ -n "$NATIVE_INTRO_MAX_FIRST_RENDER_FRAME" ]]; then
            intro_audit_args+=(--max-first-render-frame "$NATIVE_INTRO_MAX_FIRST_RENDER_FRAME")
        fi
        intro_audit_args+=(--json-out "$NATIVE_INTRO_AUDIT_JSON")
        python3 tools/audit_intro_trace.py "${intro_audit_args[@]}" "$NATIVE_TRACE"
        ;;
esac

if [[ "$NATIVE_ONLY" -eq 0 && -n "$ARES_BIN" && -z "$STOCK_TRACE" ]]; then
    run_stock_capture
fi

echo ""
echo "artifacts:"
echo "  native trace: $NATIVE_TRACE"
if [[ -n "$STOCK_TRACE" || -n "$ARES_BIN" ]]; then
    echo "  stock trace:  $STOCK_OUT_TRACE"
fi

if [[ "$NATIVE_ONLY" -eq 0 && ( -n "$STOCK_TRACE" || -n "$ARES_BIN" ) ]]; then
    echo ""
    audit_args=(--kind "$COMPARE_KIND" --label "stock ${ROUTE_NAME}" --require-stage "$STOCK_LEVEL" --require-target-player)
    if [[ "$COMPARE_KIND" == "movement" ]]; then
        audit_args+=(--require-gameplay-input)
        if [[ -n "$STOCK_MIN_GAMEPLAY_INPUT_RECORDS" ]]; then
            audit_args+=(--min-gameplay-input-records "$STOCK_MIN_GAMEPLAY_INPUT_RECORDS")
        fi
        if [[ -n "$STOCK_MIN_MOVING_RECORDS" ]]; then
            audit_args+=(--min-moving-records "$STOCK_MIN_MOVING_RECORDS")
        fi
    fi
    if [[ -n "$STOCK_MAX_SUPPRESSED_MENU_RECORDS" ]]; then
        audit_args+=(--max-suppressed-menu-records "$STOCK_MAX_SUPPRESSED_MENU_RECORDS")
    fi
    if [[ -n "$STOCK_MIN_MENU_TO_GAMEPLAY_GAP" ]]; then
        audit_args+=(--min-menu-to-gameplay-gap "$STOCK_MIN_MENU_TO_GAMEPLAY_GAP")
    fi
    audit_args+=(--json-out "$STOCK_AUDIT_JSON")
    python3 tools/audit_oracle_trace.py "${audit_args[@]}" "$STOCK_OUT_TRACE"
fi

if [[ "$NO_COMPARE" -eq 0 && "$NATIVE_ONLY" -eq 0 && ( -n "$STOCK_TRACE" || -n "$ARES_BIN" ) ]]; then
    echo ""
    compare_args=(--align "$COMPARE_ALIGN" --profile "$COMPARE_PROFILE")
    if [[ -n "$COMPARE_MAX_ALIGNED" ]]; then
        compare_args+=(--max-aligned "$COMPARE_MAX_ALIGNED")
    fi
    if [[ "$COMPARE_KIND" == "movement" ]]; then
        compare_args+=(--baseline-stage "$STOCK_LEVEL" --test-stage "$NATIVE_LEVEL")
        if [[ -n "$COMPARE_GAMEPLAY_WINDOWS" ]]; then
            while IFS= read -r window; do
                if [[ -n "$window" ]]; then
                    compare_args+=(--gameplay-window "$window")
                fi
            done <<< "$COMPARE_GAMEPLAY_WINDOWS"
        fi
        if [[ -n "$COMPARE_MIN_ALIGNED" ]]; then
            compare_args+=(--min-aligned "$COMPARE_MIN_ALIGNED")
        fi
        case "$COMPARE_NORMALIZE_POSITION" in
            1|true|True|TRUE|yes|YES)
                compare_args+=(--normalize-position)
                ;;
        esac
        compare_args+=(--json-out "$COMPARE_JSON")
        python3 tools/compare_movement_trace.py "${compare_args[@]}" "$STOCK_OUT_TRACE" "$NATIVE_TRACE"
    else
        if [[ -n "$COMPARE_CAMERA_MODES" ]]; then
            compare_args+=(--camera-modes "$COMPARE_CAMERA_MODES")
        fi
        if [[ -n "$COMPARE_START_ACTIVE_FRAME" ]]; then
            compare_args+=(--start-active-frame "$COMPARE_START_ACTIVE_FRAME")
        fi
        if [[ -n "$COMPARE_START_INTRO_TIMER" ]]; then
            compare_args+=(--start-intro-timer "$COMPARE_START_INTRO_TIMER")
        fi
        if [[ -n "$COMPARE_END_INTRO_TIMER" ]]; then
            compare_args+=(--end-intro-timer "$COMPARE_END_INTRO_TIMER")
        fi
        if [[ -n "$COMPARE_SAMPLE_STEP" ]]; then
            compare_args+=(--sample-step "$COMPARE_SAMPLE_STEP")
        fi
        if [[ -n "$COMPARE_MIN_ALIGNED" ]]; then
            compare_args+=(--min-aligned "$COMPARE_MIN_ALIGNED")
        fi
        if [[ -n "$COMPARE_VECTOR_TOLERANCE" ]]; then
            compare_args+=(--vector-tolerance "$COMPARE_VECTOR_TOLERANCE")
        fi
        if [[ -n "$COMPARE_DIRECTION_TOLERANCE" ]]; then
            compare_args+=(--direction-tolerance "$COMPARE_DIRECTION_TOLERANCE")
        fi
        if [[ -n "$COMPARE_SCALAR_TOLERANCE" ]]; then
            compare_args+=(--scalar-tolerance "$COMPARE_SCALAR_TOLERANCE")
        fi
        if [[ -n "$COMPARE_ANIM_TOLERANCE" ]]; then
            compare_args+=(--anim-tolerance "$COMPARE_ANIM_TOLERANCE")
        fi
        if [[ -n "$COMPARE_EXCLUDE_FIELDS" ]]; then
            compare_args+=(--exclude-fields "$COMPARE_EXCLUDE_FIELDS")
        fi
        case "$COMPARE_STATE" in
            1|true|True|TRUE|yes|YES)
                compare_args+=(--compare-state)
                ;;
        esac
        case "$COMPARE_SELECTED_CAMERA" in
            1|true|True|TRUE|yes|YES)
                compare_args+=(--compare-selected-camera)
                ;;
        esac
        case "$COMPARE_INTRO_SETUP" in
            1|true|True|TRUE|yes|YES)
                compare_args+=(--compare-setup)
                ;;
        esac
        case "$COMPARE_BOND_ANIM" in
            1|true|True|TRUE|yes|YES)
                compare_args+=(--compare-bond-anim)
                ;;
        esac
        case "$COMPARE_REQUIRE_FROZEN" in
            1|true|True|TRUE|yes|YES)
                compare_args+=(--require-frozen)
                ;;
        esac
        compare_args+=(--json-out "$COMPARE_JSON")
        python3 tools/compare_intro_trace.py "${compare_args[@]}" "$STOCK_OUT_TRACE" "$NATIVE_TRACE"

        if [[ "$COMPARE_ALIGN" == "intro-timer" ]]; then
            summary_compare_args=(
                --baseline "$STOCK_OUT_TRACE"
                --test "$NATIVE_TRACE"
                --baseline-label "stock ${ROUTE_NAME}"
                --test-label "native ${ROUTE_NAME}"
                --compare-profile "$intro_summary_profile"
                --json-out "$SUMMARY_COMPARE_JSON"
            )
            summary_compare_args+=("${intro_summary_common_args[@]}")
            if [[ -n "$COMPARE_MIN_ALIGNED" ]]; then
                summary_compare_args+=(--min-matched-timers "$COMPARE_MIN_ALIGNED")
            fi
            if [[ -n "$COMPARE_VECTOR_TOLERANCE" ]]; then
                summary_compare_args+=(--vector-tolerance "$COMPARE_VECTOR_TOLERANCE")
            fi
            if [[ -n "$COMPARE_DIRECTION_TOLERANCE" ]]; then
                summary_compare_args+=(--direction-tolerance "$COMPARE_DIRECTION_TOLERANCE")
            fi
            if [[ -n "$COMPARE_SCALAR_TOLERANCE" ]]; then
                summary_compare_args+=(--scalar-tolerance "$COMPARE_SCALAR_TOLERANCE")
            fi
            if [[ -n "$COMPARE_ANIM_TOLERANCE" ]]; then
                summary_compare_args+=(--anim-tolerance "$COMPARE_ANIM_TOLERANCE")
            fi
            summary_compare_args+=(--integer-timer-keys)
            python3 tools/intro_trace_summary.py "${summary_compare_args[@]}"
        else
            echo "Skipping timer-summary comparison for ${COMPARE_ALIGN} intro alignment; strict intro comparator already ran."
        fi
    fi
else
    echo ""
    echo "Native capture complete. Add --stock-trace or --ares-bin to run ROM-vs-native comparison."
fi

python3 - \
    "$CAPTURE_SUMMARY_JSON" \
    "$ROUTE_NAME" \
    "$COMPARE_KIND" \
    "$ROUTE_PATH" \
    "$NATIVE_LEVEL" \
    "$STOCK_LEVEL" \
    "$NATIVE_TRACE" \
    "$STOCK_OUT_TRACE" \
    "$SCREENSHOT_DST" \
    "$NATIVE_SCREENSHOT_JSON" \
    "$NATIVE_RENDER_JSON" \
    "$NATIVE_MOVEMENT_JSON" \
    "$NATIVE_INTRO_SUMMARY_JSON" \
    "$NATIVE_INTRO_AUDIT_JSON" \
    "$STOCK_AUDIT_JSON" \
    "$COMPARE_JSON" \
    "$SUMMARY_COMPARE_JSON" <<'PY'
import json
import sys
from pathlib import Path

(
    summary_path,
    route_name,
    compare_kind,
    route_path,
    native_level,
    stock_level,
    native_trace,
    stock_trace,
    native_screenshot,
    native_screenshot_json,
    native_render_json,
    native_movement_json,
    native_intro_summary_json,
    native_intro_audit_json,
    stock_audit_json,
    compare_json,
    summary_compare_json,
) = sys.argv[1:18]


def existing(path: str) -> str | None:
    return path if path and Path(path).exists() else None


def load_json(path: str) -> dict | None:
    if not path or not Path(path).is_file():
        return None
    with open(path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    return data if isinstance(data, dict) else {"value": data}


artifacts = {
    "native_trace": existing(native_trace),
    "stock_trace": existing(stock_trace),
    "native_screenshot": existing(native_screenshot),
    "native_screenshot_health_json": existing(native_screenshot_json),
    "native_render_json": existing(native_render_json),
    "native_movement_json": existing(native_movement_json),
    "native_intro_summary_json": existing(native_intro_summary_json),
    "native_intro_audit_json": existing(native_intro_audit_json),
    "stock_audit_json": existing(stock_audit_json),
    "compare_json": existing(compare_json),
    "summary_compare_json": existing(summary_compare_json),
}
payload = {
    "status": "pass",
    "route": route_name,
    "route_path": route_path,
    "compare_kind": compare_kind,
    "native_level": int(native_level),
    "stock_level": int(stock_level),
    "artifacts": artifacts,
    "native_screenshot_health": load_json(native_screenshot_json),
    "native_render": load_json(native_render_json),
    "native_movement": load_json(native_movement_json),
    "native_intro_summary": load_json(native_intro_summary_json),
    "native_intro_audit": load_json(native_intro_audit_json),
    "stock_audit": load_json(stock_audit_json),
    "comparison": load_json(compare_json),
    "summary_comparison": load_json(summary_compare_json),
}
with open(summary_path, "w", encoding="utf-8") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
print(f"summary_json: {summary_path}")
PY

echo ""
echo "=== ROM Oracle Capture: PASS ==="
