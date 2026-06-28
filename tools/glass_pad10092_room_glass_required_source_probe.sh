#!/usr/bin/env bash
#
# glass_pad10092_room_glass_required_source_probe.sh -- invert fixed-alpha
# texnum-654 room-glass composition against stock/native screenshots.
#
set -euo pipefail

cd "$(dirname "$0")/.."

source tools/validation_common.sh

ROUTE="dam_regular_glass_shatter_pad10092_impact_visual_probe"
ROUTE_JSON="tools/rom_oracle_routes/${ROUTE}.json"
BASE_CASE_DIR="/tmp/mgb64_glass_pad10092_impact_visual_sequence_clean/pad10092_impact"
UNDERLAY_IMAGE="/tmp/mgb64_glass_pad10092_room_glass_skip_tex654/skip_room_glass_tex654/native_${ROUTE}.bmp"
SETTEX_SAMPLE_JSON="/tmp/mgb64_glass_pad10092_room_glass_settex_sample_current_v2/pad10092_room_glass_settex_sample.json"
OUT_DIR="/tmp/mgb64_glass_pad10092_room_glass_required_source_$$"
ALPHA_U8=102
SHADER_FIELD="shaderL_frag"

usage() {
    cat <<'USAGE'
Usage: tools/glass_pad10092_room_glass_required_source_probe.sh [options]

Options:
  --out-dir DIR             output directory (default: /tmp/...)
  --base-case-dir DIR       existing stock-backed pad10092 impact case directory
  --underlay-image IMG      native image with texnum 654 skipped
  --settex-sample-json FILE focused settex material sample summary
  --route-json FILE         route JSON
  --alpha-u8 N              source alpha in 0..255 terms (default: 102)
  --shader-field NAME       settex sample field for expected source band

This is a read-only composition oracle. It consumes:
  stock image:   BASE/stock_dam_regular_glass_shatter_pad10092_impact_visual_probe.ppm
  default image: BASE/native_dam_regular_glass_shatter_pad10092_impact_visual_probe.bmp
  underlay:      native screenshot from skip_room_glass_tex654 (GE007_SKIP_TEX=654)

It inverts output = source * alpha + underlay * (1 - alpha) per pixel.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --base-case-dir) BASE_CASE_DIR="$2"; shift 2 ;;
        --underlay-image) UNDERLAY_IMAGE="$2"; shift 2 ;;
        --settex-sample-json) SETTEX_SAMPLE_JSON="$2"; shift 2 ;;
        --route-json) ROUTE_JSON="$2"; shift 2 ;;
        --alpha-u8) ALPHA_U8="$2"; shift 2 ;;
        --shader-field) SHADER_FIELD="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

BASE_CASE_DIR="$(validation_resolve_path "$BASE_CASE_DIR")"
UNDERLAY_IMAGE="$(validation_resolve_path "$UNDERLAY_IMAGE")"
SETTEX_SAMPLE_JSON="$(validation_resolve_path "$SETTEX_SAMPLE_JSON")"
ROUTE_JSON="$(validation_resolve_path "$ROUTE_JSON")"

validation_require_file "$ROUTE_JSON" "route JSON"
validation_require_file "$BASE_CASE_DIR/stock_${ROUTE}.ppm" "base stock screenshot"
validation_require_file "$BASE_CASE_DIR/native_${ROUTE}.bmp" "base native screenshot"
validation_require_file "$UNDERLAY_IMAGE" "skip-tex654 underlay screenshot"
validation_require_file "$SETTEX_SAMPLE_JSON" "settex sample JSON"

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

JSON_OUT="$OUT_DIR/pad10092_room_glass_required_source.json"
TXT_OUT="$OUT_DIR/pad10092_room_glass_required_source.txt"

python3 tools/analyze_room_glass_required_source.py \
    --route-json "$ROUTE_JSON" \
    --base-case-dir "$BASE_CASE_DIR" \
    --underlay-image "$UNDERLAY_IMAGE" \
    --settex-sample-json "$SETTEX_SAMPLE_JSON" \
    --alpha-u8 "$ALPHA_U8" \
    --shader-field "$SHADER_FIELD" \
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

echo "[ROOM-GLASS-REQUIRED-SOURCE] wrote $JSON_OUT"
