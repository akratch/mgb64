#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

ROUTE_JSON="$ROOT_DIR/tools/rom_oracle_routes/dam_regular_glass_shatter_pad10092_impact_visual_probe.json"
BASE_CASE_DIR="/tmp/mgb64_glass_pad10092_impact_visual_sequence_clean/pad10092_impact"
OUT_DIR=""
REGIONS=()

usage() {
    cat <<USAGE
Usage: $0 [--base-case-dir DIR] [--out-dir DIR] [--route-json FILE] [--region NAME]

Read-only stock/native ROI pixel semantics probe for the Dam pad-10092 impact
fixture. The default base case is:
  $BASE_CASE_DIR
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --base-case-dir)
            BASE_CASE_DIR="$2"
            shift 2
            ;;
        --out-dir)
            OUT_DIR="$2"
            shift 2
            ;;
        --route-json)
            ROUTE_JSON="$2"
            shift 2
            ;;
        --region)
            REGIONS+=("--region" "$2")
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ -z "$OUT_DIR" ]]; then
    OUT_DIR="$(mktemp -d /tmp/mgb64_glass_pad10092_roi_pixel_semantics.XXXXXX)"
fi

mkdir -p "$OUT_DIR"

python3 "$ROOT_DIR/tools/compare_roi_pixel_semantics.py" \
    --route-json "$ROUTE_JSON" \
    --base-case-dir "$BASE_CASE_DIR" \
    "${REGIONS[@]}" \
    --json-out "$OUT_DIR/pad10092_roi_pixel_semantics.json" \
    > "$OUT_DIR/pad10092_roi_pixel_semantics.txt"

echo "[ROI-PIXEL] wrote $OUT_DIR/pad10092_roi_pixel_semantics.json"
