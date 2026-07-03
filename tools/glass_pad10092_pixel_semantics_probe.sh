#!/bin/bash
#
# glass_pad10092_pixel_semantics_probe.sh -- native-only pixel-output semantics
# probe for the Dam pad-10092 impact fixture.
#
# This is a diagnostic capture lane. It does not change renderer behavior and
# it does not require stock ares unless the caller separately refreshes the base
# stock/native visual case.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=300
OUT_DIR="/tmp/mgb64_glass_pad10092_pixel_semantics_$$"
BASE_CASE_DIR="/tmp/mgb64_glass_pad10092_impact_visual_sequence_clean/pad10092_impact"
USE_BASE_CASE=1

ROUTE="dam_regular_glass_shatter_pad10092_impact_visual_probe"
ROUTE_JSON="tools/rom_oracle_routes/${ROUTE}.json"
EFFECT_LABEL="bullet_impact_world"
TRACE_AFTER_FRAME=120
TRACE_BUDGET=160
TRACE_DRAWCLASS="room"
STOCK_FRAME=2541
NATIVE_FRAME=124

usage() {
    cat <<'USAGE'
Usage: tools/glass_pad10092_pixel_semantics_probe.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary path (default: build/ge007)
  --build-dir DIR      CMake build directory (default: build)
  --no-build           reuse an existing native binary
  --timeout SECONDS    route timeout (default: 300)
  --base-case-dir DIR  existing pad10092 impact case dir for stock/native
                       pixel evidence (default: /tmp/...)
  --no-base-case       skip stock/native pixel evidence lookup
  --after-frame N      first frame for material/triangle trace rows
                       (default: 120)
  --budget N           material/triangle trace row budget (default: 160)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, emulator output, or generated summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        --base-case-dir) BASE_CASE_DIR="$2"; USE_BASE_CASE=1; shift 2 ;;
        --no-base-case) USE_BASE_CASE=0; shift ;;
        --after-frame) TRACE_AFTER_FRAME="$2"; shift 2 ;;
        --budget) TRACE_BUDGET="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

for value_name in TIMEOUT_SECONDS TRACE_AFTER_FRAME TRACE_BUDGET; do
    value="${!value_name}"
    if [[ ! "$value" =~ ^[1-9][0-9]*$ ]]; then
        case "$value_name" in
            TIMEOUT_SECONDS) option_label="--timeout" ;;
            TRACE_AFTER_FRAME) option_label="--after-frame" ;;
            TRACE_BUDGET) option_label="--budget" ;;
            *) option_label="$value_name" ;;
        esac
        echo "FAIL: $option_label must be a positive integer: $value" >&2
        exit 2
    fi
done

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
else
    BINARY="$(validation_resolve_path "$BINARY")"
fi
ROM="$(validation_resolve_path "$ROM")"
if [[ "$USE_BASE_CASE" -eq 1 ]]; then
    BASE_CASE_DIR="$(validation_resolve_path "$BASE_CASE_DIR")"
fi

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"
validation_require_file "$ROUTE_JSON" "route JSON"
if [[ "$USE_BASE_CASE" -eq 1 ]]; then
    validation_require_file "$BASE_CASE_DIR/glass_pad10092_impact_visual_summary.json" "base case summary"
    validation_require_file "$BASE_CASE_DIR/stock_${ROUTE}.jsonl" "base stock trace"
    validation_require_file "$BASE_CASE_DIR/native_${ROUTE}.jsonl" "base native trace"
fi

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

echo "=== Glass Pad10092 Pixel Semantics Probe ==="
echo "  out-dir:    $OUT_DIR"
echo "  binary:     $BINARY"
echo "  ROM:        $ROM"
echo "  route:      $ROUTE"
echo "  effect:     $EFFECT_LABEL"
echo "  drawclass:  $TRACE_DRAWCLASS"
echo "  after:      $TRACE_AFTER_FRAME"
echo "  budget:     $TRACE_BUDGET"
if [[ "$USE_BASE_CASE" -eq 1 ]]; then
    echo "  base-case:  $BASE_CASE_DIR"
fi

env \
    GE007_TRACE_BULLET_IMPACTS=1 \
    GE007_EFFECT_RANGE_TRACE=1 \
    GE007_EFFECT_TRI_TRACE=1 \
    GE007_EFFECT_TRI_TRACE_LABEL="$EFFECT_LABEL" \
    GE007_EFFECT_TRI_TRACE_AFTER_FRAME="$TRACE_AFTER_FRAME" \
    GE007_EFFECT_TRI_TRACE_BUDGET="$TRACE_BUDGET" \
    GE007_EFFECT_TRI_TRACE_DRAWCLASS="$TRACE_DRAWCLASS" \
    GE007_EFFECT_TRI_TRACE_EMITS_ONLY=1 \
    GE007_TRACE_BULLET_IMPACT_MATERIALS=1 \
    GE007_TRACE_BULLET_IMPACT_MATERIALS_EFFECT="$EFFECT_LABEL" \
    GE007_TRACE_BULLET_IMPACT_MATERIALS_AFTER_FRAME="$TRACE_AFTER_FRAME" \
    GE007_TRACE_BULLET_IMPACT_MATERIALS_BUDGET="$TRACE_BUDGET" \
    tools/movement_oracle_capture.sh \
        --route "$ROUTE" \
        --native-only \
        --no-compare \
        --out-dir "$OUT_DIR" \
        --rom "$ROM" \
        --binary "$BINARY" \
        --no-build \
        --timeout "$TIMEOUT_SECONDS"

LOG="$OUT_DIR/native_${ROUTE}.log"
validation_require_file "$LOG" "native log"
validation_require_file "$OUT_DIR/native_${ROUTE}.jsonl" "native trace"
validation_require_file "$OUT_DIR/native_${ROUTE}.bmp" "native screenshot"

GFXDL_MATCHES="$OUT_DIR/gfxdl_matches.txt"
if grep -H -F "[GFX-DL]" "$OUT_DIR"/*.log > "$GFXDL_MATCHES"; then
    echo "FAIL: [GFX-DL] warning rows found during pixel semantics probe" >&2
    head -20 "$GFXDL_MATCHES" | sed 's/^/  /' >&2
    exit 1
fi
rm -f "$GFXDL_MATCHES"

SUMMARY="$OUT_DIR/glass_pad10092_pixel_semantics_summary.json"
FOOTPRINT_JSON="$OUT_DIR/glass_pad10092_effect_footprint_visual.json"
FOOTPRINT_HEATMAP="$OUT_DIR/glass_pad10092_effect_footprint_visual.png"
FOOTPRINT_TXT="$OUT_DIR/glass_pad10092_effect_footprint_visual.txt"
IMPACT_SEQUENCE_JSON=""
if [[ "$USE_BASE_CASE" -eq 1 ]]; then
    IMPACT_SEQUENCE_JSON="$OUT_DIR/glass_pad10092_impact_sequence_compare.json"
    tools/compare_bullet_impact_sequence.py \
        --baseline-label "stock ${ROUTE}" \
        --test-label "native ${ROUTE}" \
        --baseline-frame "$STOCK_FRAME" \
        --test-frame "$NATIVE_FRAME" \
        --json-out "$IMPACT_SEQUENCE_JSON" \
        "$BASE_CASE_DIR/stock_${ROUTE}.jsonl" \
        "$BASE_CASE_DIR/native_${ROUTE}.jsonl" \
        > "$OUT_DIR/glass_pad10092_impact_sequence_compare.txt"
fi
SUMMARY_ARGS=(
    --route-json "$ROUTE_JSON"
    --effect "$EFFECT_LABEL"
    --primary-region projected_impact
    --expect-min-material-rows 8
    --expect-min-effect-tri-rows 8
    --expect-min-signatures 1
    --json-out "$SUMMARY"
)
if [[ "$USE_BASE_CASE" -eq 1 ]]; then
    SUMMARY_ARGS+=(--base-case-dir "$BASE_CASE_DIR")
    SUMMARY_ARGS+=(--impact-sequence-json "$IMPACT_SEQUENCE_JSON")
fi

tools/summarize_effect_pixel_semantics.py "${SUMMARY_ARGS[@]}" "$LOG" > "$OUT_DIR/glass_pad10092_pixel_semantics_summary.txt"

if [[ "$USE_BASE_CASE" -eq 1 ]]; then
    python3 tools/compare_effect_footprint_visual.py \
        "$SUMMARY" \
        --base-case-dir "$BASE_CASE_DIR" \
        --group-source effect_triangles \
        --primary-region projected_impact \
        --padding-logical 0,2,8 \
        --heatmap "$FOOTPRINT_HEATMAP" \
        --json-out "$FOOTPRINT_JSON" \
        > "$FOOTPRINT_TXT"
fi

cat "$OUT_DIR/glass_pad10092_pixel_semantics_summary.txt"
if [[ -n "$IMPACT_SEQUENCE_JSON" ]]; then
    echo ""
    python3 - "$IMPACT_SEQUENCE_JSON" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
data = json.loads(path.read_text(encoding="utf-8"))
sequence = data.get("sequence", {})
impact = sequence.get("impact", {})
print("Impact sequence evidence:")
print(f"  match: {data.get('match')} (status={data.get('status')})")
print(f"  first sample identity match: {data.get('first_pair_identity_match')}")
print(f"  stock impact types: {impact.get('baseline')}")
print(f"  native impact types: {impact.get('test')}")
for item in data.get("interpretation", []):
    print(f"  - {item}")
print(f"  report: {path}")
PY
fi
if [[ "$USE_BASE_CASE" -eq 1 ]]; then
    echo ""
    python3 - "$FOOTPRINT_JSON" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
data = json.loads(path.read_text(encoding="utf-8"))
print("Effect footprint visual evidence:")
print(f"  status: {data.get('status')}")
for item in data.get("interpretation", []):
    print(f"  - {item}")
print(f"  report: {path}")
PY
fi
echo ""
echo "Summary: $SUMMARY"
