#!/bin/bash
#
# startup_music_reference_check.sh -- Capture and compare startup music audio.
#
# Captures MGB64's pre-SFX startup music PCM dump and optionally compares it
# against a local emulator/hardware reference capture. All artifacts are local
# ROM-derived validation data; do not commit or redistribute them.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=180
TIMEOUT_BIN="$(validation_resolve_timeout_cmd)"
OUT_DIR="/tmp/mgb64_startup_music_$$"
FRAMES=2400
REFERENCE=""
REFERENCE_FORMAT="auto"
REFERENCE_RAW_RATE=44100
REFERENCE_RAW_CHANNELS=2
TEST_RAW_RATE=22050
TARGET_RATE=22050
REFERENCE_START=0
TEST_START=0
DURATION=""
MAX_OFFSET_SECONDS=20
SEGMENT_SECONDS=10
SEGMENT_HOP_SECONDS=5

usage() {
    cat <<'USAGE'
Usage: tools/startup_music_reference_check.sh [options]

Options:
  --reference PATH              local reference WAV/raw capture to compare
  --reference-format FORMAT     auto, wav, raw (default: auto)
  --reference-raw-rate HZ       raw reference sample rate (default: 44100)
  --reference-raw-channels N    raw reference channel count (default: 2)
  --reference-start SECONDS     skip reference prefix before comparing
  --test-start SECONDS          skip MGB64 dump prefix before comparing
  --duration SECONDS            compare only this many seconds
  --max-offset-seconds SECONDS  envelope alignment search window (default: 20)
  --frames N                    MGB64 music dump frames to capture (default: 2400)
  --out-dir DIR                 local artifact directory (default: /tmp/...)
  --rom PATH                    ROM path (default: ./baserom.u.z64)
  --binary PATH                 native binary path (default: build/ge007)
  --build-dir DIR               CMake build directory (default: build)
  --no-build                    reuse an existing binary
  --timeout SECONDS             capture timeout (default: 180)

Artifacts are ROM-derived local validation data. Do not commit, attach, or
redistribute the raw/WAV captures, screenshots, logs, or JSON metrics if they
contain derived game audio.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --reference) REFERENCE="$2"; shift 2 ;;
        --reference-format) REFERENCE_FORMAT="$2"; shift 2 ;;
        --reference-raw-rate) REFERENCE_RAW_RATE="$2"; shift 2 ;;
        --reference-raw-channels) REFERENCE_RAW_CHANNELS="$2"; shift 2 ;;
        --reference-start) REFERENCE_START="$2"; shift 2 ;;
        --test-start) TEST_START="$2"; shift 2 ;;
        --duration) DURATION="$2"; shift 2 ;;
        --max-offset-seconds) MAX_OFFSET_SECONDS="$2"; shift 2 ;;
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

case "$REFERENCE_FORMAT" in
    auto|wav|raw) ;;
    *) echo "FAIL: --reference-format must be auto, wav, or raw" >&2; exit 2 ;;
esac

if [[ ! "$FRAMES" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --frames must be a positive integer" >&2
    exit 2
fi

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
else
    BINARY="$(validation_resolve_path "$BINARY")"
fi
ROM="$(validation_resolve_path "$ROM")"
OUT_DIR="$(validation_resolve_path "$OUT_DIR")"
if [[ -n "$REFERENCE" ]]; then
    REFERENCE="$(validation_resolve_path "$REFERENCE")"
fi

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"
if [[ -n "$REFERENCE" ]]; then
    validation_require_file "$REFERENCE" "reference capture"
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "FAIL: python3 is required for audio comparison tooling" >&2
    exit 2
fi

validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT INT TERM

mkdir -p "$OUT_DIR/savedir"

MGB64_DUMP="$OUT_DIR/mgb64_boot_music.raw"
COMPARE_JSON="$OUT_DIR/boot_audio_compare.json"
CAPTURE_LOG="$OUT_DIR/mgb64_capture.log"
rm -f "$MGB64_DUMP" "$COMPARE_JSON" "$CAPTURE_LOG"

echo "=== Startup Music Reference Check ==="
echo "  out-dir:   $OUT_DIR"
echo "  binary:    $BINARY"
echo "  ROM:       $ROM"
echo "  frames:    $FRAMES"
if [[ -n "$REFERENCE" ]]; then
    echo "  reference: $REFERENCE"
else
    echo "  reference: (none; capture-only)"
fi

CAPTURE_CMD=(env -u GE007_DEBUG
    SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}"
    GE007_MUSIC_AUDIO_DUMP="$MGB64_DUMP"
    GE007_MUSIC_AUDIO_DUMP_FRAMES="$FRAMES"
    GE007_NO_VSYNC=1
    GE007_BACKGROUND=1
    GE007_NO_INPUT_GRAB=1
    "$BINARY"
    --rom "$ROM"
    --deterministic
    --savedir "$OUT_DIR/savedir"
    --screenshot-frame "$FRAMES"
    --screenshot-label "startup_music_$$"
    --screenshot-exit)

echo ""
echo "== Capturing MGB64 startup music dump =="
if [[ -n "$TIMEOUT_BIN" ]]; then
    (
        cd "$OUT_DIR"
        "$TIMEOUT_BIN" --kill-after=5 "$TIMEOUT_SECONDS" "${CAPTURE_CMD[@]}"
    ) >"$CAPTURE_LOG" 2>&1 || {
        echo "FAIL: MGB64 startup capture failed"
        tail -60 "$CAPTURE_LOG" | sed 's/^/  /'
        exit 1
    }
else
    (
        cd "$OUT_DIR"
        "${CAPTURE_CMD[@]}"
    ) >"$CAPTURE_LOG" 2>&1 || {
        echo "FAIL: MGB64 startup capture failed"
        tail -60 "$CAPTURE_LOG" | sed 's/^/  /'
        exit 1
    }
fi

if [[ ! -s "$MGB64_DUMP" ]]; then
    echo "FAIL: missing MGB64 music dump: $MGB64_DUMP"
    tail -60 "$CAPTURE_LOG" | sed 's/^/  /'
    exit 1
fi

echo "PASS: captured $(wc -c < "$MGB64_DUMP" | tr -d '[:space:]') bytes -> $MGB64_DUMP"

if [[ -z "$REFERENCE" ]]; then
    echo ""
    echo "Capture-only mode complete. To compare later:"
    echo "  tools/startup_music_reference_check.sh \\"
    echo "    --no-build --rom \"$ROM\" --binary \"$BINARY\" \\"
    echo "    --out-dir \"$OUT_DIR\" --reference /tmp/mgb64_audio_ref/reference_boot.wav"
    exit 0
fi

COMPARE_CMD=(python3 tools/compare_audio_reference.py
    "$REFERENCE"
    "$MGB64_DUMP"
    --reference-format "$REFERENCE_FORMAT"
    --reference-raw-rate "$REFERENCE_RAW_RATE"
    --reference-raw-channels "$REFERENCE_RAW_CHANNELS"
    --test-format raw
    --test-raw-rate "$TEST_RAW_RATE"
    --target-rate "$TARGET_RATE"
    --reference-start "$REFERENCE_START"
    --test-start "$TEST_START"
    --max-offset-seconds "$MAX_OFFSET_SECONDS"
    --segment-seconds "$SEGMENT_SECONDS"
    --segment-hop-seconds "$SEGMENT_HOP_SECONDS"
    --print-bands
    --print-segments
    --json-out "$COMPARE_JSON")
if [[ -n "$DURATION" ]]; then
    COMPARE_CMD+=(--duration "$DURATION")
fi

echo ""
echo "== Comparing against reference =="
"${COMPARE_CMD[@]}"
echo ""
echo "Metrics JSON: $COMPARE_JSON"
