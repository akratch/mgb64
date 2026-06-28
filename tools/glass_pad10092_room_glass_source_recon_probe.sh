#!/usr/bin/env bash
#
# glass_pad10092_room_glass_source_recon_probe.sh -- capture texnum-654
# room-glass source payload and reconstruct projected_impact pixels.
#
set -euo pipefail

cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=300
OUT_DIR="/tmp/mgb64_glass_pad10092_room_glass_source_recon_$$"
BASE_CASE_DIR="/tmp/mgb64_glass_pad10092_impact_visual_sequence_clean/pad10092_impact"
UNDERLAY_IMAGE=""
NATIVE_CONFIG_OVERRIDES=()

ROUTE="dam_regular_glass_shatter_pad10092_impact_visual_probe"
ROUTE_JSON="tools/rom_oracle_routes/${ROUTE}.json"
ROOM_GLASS_CC="0x00738e4f020a2d12"

usage() {
    cat <<'USAGE'
Usage: tools/glass_pad10092_room_glass_source_recon_probe.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --base-case-dir DIR  existing stock-backed pad10092 impact case directory
  --underlay-image IMG reuse a native image captured with GE007_SKIP_TEX=654
                       (default: capture a fresh same-run underlay)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary path (default: build/ge007)
  --build-dir DIR      CMake build directory (default: build)
  --no-build           reuse an existing native binary
  --timeout SECONDS    route timeout (default: 300)
  --native-config-override KEY=VALUE
                       append a native config override to both captures

This is read-only instrumentation. It captures the Dam pad10092 impact route
with:
  GE007_TRACE_SETTEX_MATERIAL_CC=0x00738e4f020a2d12
  GE007_DUMP_SETTEX_TEXTURES=654

Then it reconstructs projected_impact source pixels from the dumped tex654
payload and SETTEX-MATERIAL-CC triangles.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --base-case-dir) BASE_CASE_DIR="$2"; shift 2 ;;
        --underlay-image) UNDERLAY_IMAGE="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        --native-config-override) NATIVE_CONFIG_OVERRIDES+=("$2"); shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ ! "$TIMEOUT_SECONDS" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --timeout must be a positive integer: $TIMEOUT_SECONDS" >&2
    exit 2
fi

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
else
    BINARY="$(validation_resolve_path "$BINARY")"
fi
ROM="$(validation_resolve_path "$ROM")"
BASE_CASE_DIR="$(validation_resolve_path "$BASE_CASE_DIR")"
if [[ -n "$UNDERLAY_IMAGE" ]]; then
    UNDERLAY_IMAGE="$(validation_resolve_path "$UNDERLAY_IMAGE")"
fi
ROUTE_JSON="$(validation_resolve_path "$ROUTE_JSON")"

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"
validation_require_file "$ROUTE_JSON" "route JSON"
validation_require_file "$BASE_CASE_DIR/stock_${ROUTE}.ppm" "base stock screenshot"
if [[ -n "$UNDERLAY_IMAGE" ]]; then
    validation_require_file "$UNDERLAY_IMAGE" "skip-tex654 underlay screenshot"
fi

mkdir -p "$OUT_DIR/texture_dump"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"
CAPTURE_DIR="$OUT_DIR/capture"
SKIP_CAPTURE_DIR="$OUT_DIR/skip_tex654"
TEXTURE_DIR="$OUT_DIR/texture_dump"
SAMPLE_DIR="$OUT_DIR/settex_sample"
FB_CAPTURE_DIR="$OUT_DIR/framebuffer_capture"

echo "=== Glass Pad10092 Room Glass Source Reconstruction ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  route:   $ROUTE"
if [[ "${#NATIVE_CONFIG_OVERRIDES[@]}" -gt 0 ]]; then
    echo "  native-config-overrides: ${NATIVE_CONFIG_OVERRIDES[*]}"
fi

rm -rf "$CAPTURE_DIR" "$SKIP_CAPTURE_DIR" "$SAMPLE_DIR" "$FB_CAPTURE_DIR"
mkdir -p "$TEXTURE_DIR" "$FB_CAPTURE_DIR"

NATIVE_CONFIG_ARGS=()
if [[ "${#NATIVE_CONFIG_OVERRIDES[@]}" -gt 0 ]]; then
    for override in "${NATIVE_CONFIG_OVERRIDES[@]}"; do
        NATIVE_CONFIG_ARGS+=(--native-config-override "$override")
    done
fi

env \
    GE007_TRACE_SETTEX_MATERIAL_CC="$ROOM_GLASS_CC" \
    GE007_TRACE_SETTEX_MATERIAL_CC_AFTER_FRAME=120 \
    GE007_TRACE_SETTEX_MATERIAL_CC_BUDGET=240 \
    GE007_TRACE_SETTEX_MATERIAL_CC_SAMPLES=1 \
    GE007_TRACE_SETTEX_MATERIAL_CC_VERTS=1 \
    GE007_TRACE_SETTEX_FB_CAPTURE="$ROOM_GLASS_CC" \
    GE007_TRACE_SETTEX_FB_CAPTURE_AFTER_FRAME=120 \
    GE007_TRACE_SETTEX_FB_CAPTURE_BUDGET=240 \
    GE007_TRACE_SETTEX_FB_CAPTURE_TEXNUM=654 \
    GE007_TRACE_SETTEX_FB_CAPTURE_TEXSIZE=54x54 \
    GE007_TRACE_SETTEX_FB_CAPTURE_DIR="$FB_CAPTURE_DIR" \
    GE007_DUMP_SETTEX_TEXTURES=654 \
    GE007_DUMP_SETTEX_DIR="$TEXTURE_DIR" \
    tools/movement_oracle_capture.sh \
        --route "$ROUTE" \
        --native-only \
        --no-compare \
        --out-dir "$CAPTURE_DIR" \
        --rom "$ROM" \
        --binary "$BINARY" \
        --no-build \
        --timeout "$TIMEOUT_SECONDS" \
        "${NATIVE_CONFIG_ARGS[@]}"

if [[ -z "$UNDERLAY_IMAGE" ]]; then
    echo "=== same-run skip underlay: GE007_SKIP_TEX=654 ==="
    env \
        GE007_SKIP_TEX=654 \
        tools/movement_oracle_capture.sh \
            --route "$ROUTE" \
            --native-only \
            --no-compare \
            --out-dir "$SKIP_CAPTURE_DIR" \
            --rom "$ROM" \
            --binary "$BINARY" \
            --no-build \
            --timeout "$TIMEOUT_SECONDS" \
            "${NATIVE_CONFIG_ARGS[@]}"
    UNDERLAY_IMAGE="$SKIP_CAPTURE_DIR/native_${ROUTE}.bmp"
fi

LOG="$CAPTURE_DIR/native_${ROUTE}.log"
NATIVE_IMAGE="$CAPTURE_DIR/native_${ROUTE}.bmp"
TEXTURE_RGBA="$TEXTURE_DIR/ge007_settex_0654.rgba.ppm"
TEXTURE_ALPHA="$TEXTURE_DIR/ge007_settex_0654.alpha.pgm"

validation_require_file "$LOG" "native SETTEX material log"
validation_require_file "$NATIVE_IMAGE" "native route screenshot"
validation_require_file "$UNDERLAY_IMAGE" "skip-tex654 underlay screenshot"
validation_require_file "$TEXTURE_RGBA" "tex654 RGBA dump"
validation_require_file "$TEXTURE_ALPHA" "tex654 alpha dump"

tools/glass_pad10092_room_glass_settex_sample_probe.sh \
    --log "$LOG" \
    --out-dir "$SAMPLE_DIR" \
    --frame latest

JSON_OUT="$OUT_DIR/pad10092_room_glass_source_recon.json"
TXT_OUT="$OUT_DIR/pad10092_room_glass_source_recon.txt"

python3 tools/analyze_room_glass_source_reconstruction.py \
    --log "$LOG" \
    --route-json "$ROUTE_JSON" \
    --texture-rgba "$TEXTURE_RGBA" \
    --texture-alpha "$TEXTURE_ALPHA" \
    --base-case-dir "$BASE_CASE_DIR" \
    --default-image "$NATIVE_IMAGE" \
    --underlay-image "$UNDERLAY_IMAGE" \
    --region projected_impact \
    --frame latest \
    --require-fb-capture \
    --json-out "$JSON_OUT" \
    > /dev/null

python3 - "$JSON_OUT" "$TXT_OUT" <<'PY'
import json
import sys
from pathlib import Path

json_path = Path(sys.argv[1])
txt_path = Path(sys.argv[2])
payload = json.loads(json_path.read_text(encoding="utf-8"))
lines = [f"status: {payload.get('status')}"]
lines.extend(f"- {item}" for item in payload.get("interpretation", []))
if payload.get("failures"):
    lines.append("failures:")
    lines.extend(f"- {item}" for item in payload["failures"])
txt_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
print(txt_path.read_text(encoding="utf-8"), end="")
if payload.get("status") != "pass":
    raise SystemExit(1)
PY

echo "[ROOM-GLASS-SOURCE-RECON] wrote $JSON_OUT"
