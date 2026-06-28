#!/usr/bin/env bash
#
# glass_pad10092_room_glass_pixel_oracle_probe.sh -- split stock/native ROI
# pixels by the native room-glass TEXGEN material bbox mask.
#
set -euo pipefail

cd "$(dirname "$0")/.."

source tools/validation_common.sh

ROUTE="dam_regular_glass_shatter_pad10092_impact_visual_probe"
ROUTE_JSON="tools/rom_oracle_routes/${ROUTE}.json"
BASE_CASE_DIR="/tmp/mgb64_glass_pad10092_impact_visual_sequence_clean/pad10092_impact"
LOG="/tmp/mgb64_glass_pad10092_texgen_roi_material_probe/default/native_${ROUTE}.log"
OUT_DIR="/tmp/mgb64_glass_pad10092_room_glass_pixel_oracle_$$"
FRAME="latest"
ROOM_GLASS_CC="0x00738e4f020a2d12"

usage() {
    cat <<'USAGE'
Usage: tools/glass_pad10092_room_glass_pixel_oracle_probe.sh [options]

Options:
  --out-dir DIR         output directory (default: /tmp/...)
  --base-case-dir DIR   stock-backed pad10092 case directory
  --log FILE            native log containing TEXGEN-MATERIAL rows
  --route-json FILE     route JSON
  --frame VALUE         latest, all, or native frame number (default: latest)

This is a read-only pixel-output oracle. It does not capture new ROM data by
default; it consumes an existing stock/native base screenshot pair and an
existing native TEXGEN material trace.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --base-case-dir) BASE_CASE_DIR="$2"; shift 2 ;;
        --log) LOG="$2"; shift 2 ;;
        --route-json) ROUTE_JSON="$2"; shift 2 ;;
        --frame) FRAME="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

BASE_CASE_DIR="$(validation_resolve_path "$BASE_CASE_DIR")"
LOG="$(validation_resolve_path "$LOG")"
ROUTE_JSON="$(validation_resolve_path "$ROUTE_JSON")"

validation_require_file "$ROUTE_JSON" "route JSON"
validation_require_file "$BASE_CASE_DIR/stock_${ROUTE}.ppm" "base stock screenshot"
validation_require_file "$BASE_CASE_DIR/native_${ROUTE}.bmp" "base native screenshot"
validation_require_file "$LOG" "native TEXGEN material log"

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

JSON_OUT="$OUT_DIR/pad10092_room_glass_pixel_oracle.json"
TXT_OUT="$OUT_DIR/pad10092_room_glass_pixel_oracle.txt"

python3 tools/compare_texgen_material_mask_pixel_semantics.py \
    "$LOG" \
    --route-json "$ROUTE_JSON" \
    --base-case-dir "$BASE_CASE_DIR" \
    --frame "$FRAME" \
    --material-class room \
    --effect glass \
    --settex 1 \
    --effcc "$ROOM_GLASS_CC" \
    --json-out "$JSON_OUT" \
    > "$TXT_OUT"

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

echo "[ROOM-GLASS-PIXEL] wrote $JSON_OUT"
