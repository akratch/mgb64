#!/usr/bin/env bash
#
# glass_pad10092_room_glass_settex_sample_probe.sh -- summarize room-glass
# SETTEX material samples from an existing pad10092 trace.
#
set -euo pipefail

cd "$(dirname "$0")/.."

source tools/validation_common.sh

ROUTE="dam_regular_glass_shatter_pad10092_impact_visual_probe"
ROUTE_JSON="tools/rom_oracle_routes/${ROUTE}.json"
LOG="/tmp/mgb64_glass_pad10092_room_glass_settex_sample_trace/native_${ROUTE}.log"
OUT_DIR="/tmp/mgb64_glass_pad10092_room_glass_settex_sample_$$"
FRAME="latest"
ROOM_GLASS_CC="0x00738e4f020a2d12"

usage() {
    cat <<'USAGE'
Usage: tools/glass_pad10092_room_glass_settex_sample_probe.sh [options]

Options:
  --out-dir DIR       output directory (default: /tmp/...)
  --log FILE          native log containing SETTEX-MATERIAL-CC rows
  --route-json FILE   route JSON
  --frame VALUE       latest, all, or native frame number after filtering

This read-only probe consumes a native trace captured with:
  GE007_TRACE_SETTEX_MATERIAL_CC=0x00738e4f020a2d12
  GE007_TRACE_SETTEX_MATERIAL_CC_AFTER_FRAME=120
  GE007_TRACE_SETTEX_MATERIAL_CC_BUDGET=120
  GE007_TRACE_SETTEX_MATERIAL_CC_SAMPLES=1
  GE007_TRACE_SETTEX_MATERIAL_CC_VERTS=1
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --log) LOG="$2"; shift 2 ;;
        --route-json) ROUTE_JSON="$2"; shift 2 ;;
        --frame) FRAME="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

LOG="$(validation_resolve_path "$LOG")"
ROUTE_JSON="$(validation_resolve_path "$ROUTE_JSON")"

validation_require_file "$LOG" "native SETTEX material log"
validation_require_file "$ROUTE_JSON" "route JSON"

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

JSON_OUT="$OUT_DIR/pad10092_room_glass_settex_sample.json"
TXT_OUT="$OUT_DIR/pad10092_room_glass_settex_sample.txt"

python3 tools/summarize_settex_material_sample_trace.py \
    "$LOG" \
    --route-json "$ROUTE_JSON" \
    --frame "$FRAME" \
    --primary-region projected_impact \
    --material-class room \
    --texnum 654 \
    --wh 54x54 \
    --rgba-wh 54x54 \
    --blend alpha \
    --alpha 1 \
    --fog 1 \
    --effcc "$ROOM_GLASS_CC" \
    --opts 0x00043C13 \
    --oml-raw 0xC41049D8 \
    --expect-min-filtered-rows 1 \
    --expect-min-primary-rows 1 \
    --expect-min-primary-coverage-pct 99.0 \
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

echo "[ROOM-GLASS-SETTEX] wrote $JSON_OUT"
