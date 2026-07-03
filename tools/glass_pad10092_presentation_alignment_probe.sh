#!/bin/bash
#
# glass_pad10092_presentation_alignment_probe.sh -- read-only presentation/crop
# scorer for the Dam pad-10092 impact fixture.
#
# This probe reuses an existing glass_pad10092_impact_visual_regression.sh case
# directory. It does not launch the game or emulator. It asks whether alternate
# logical crop/presentation choices can explain the current stock/native pixel
# mismatch before another renderer experiment.
#
set -euo pipefail
cd "$(dirname "$0")/.."

BASE_CASE_DIR="/tmp/mgb64_glass_pad10092_impact_framing_gate/pad10092_impact"
OUT_DIR="/tmp/mgb64_glass_pad10092_presentation_alignment_$$"
ROUTE="dam_regular_glass_shatter_pad10092_impact_visual_probe"

usage() {
    cat <<'USAGE'
Usage: tools/glass_pad10092_presentation_alignment_probe.sh [options]

Options:
  --base-case-dir DIR  existing pad10092 impact case dir
                       (default: /tmp/mgb64_glass_pad10092_impact_framing_gate/pad10092_impact)
  --out-dir DIR        output directory (default: /tmp/...)

Artifacts are ROM-derived local validation data. Do not commit captured
screenshots, heatmaps, logs, or generated summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --base-case-dir) BASE_CASE_DIR="$2"; shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

BASE_CASE_DIR="$(cd "$BASE_CASE_DIR" && pwd)"
mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

STOCK_SCREENSHOT="$BASE_CASE_DIR/stock_${ROUTE}.ppm"
NATIVE_SCREENSHOT="$BASE_CASE_DIR/native_${ROUTE}.bmp"

for file in "$STOCK_SCREENSHOT" "$NATIVE_SCREENSHOT"; do
    if [[ ! -f "$file" ]]; then
        echo "FAIL: required base-case screenshot not found: $file" >&2
        exit 2
    fi
done

echo "=== Glass Pad10092 Presentation Alignment Probe ==="
echo "  base-case: $BASE_CASE_DIR"
echo "  out-dir:   $OUT_DIR"

COMMON_ARGS=(
    --logical-size 320,240
    --logical-viewport 0,10,320,220
    --region tower_pane:80,115,320,180
    --region impact_side:255,145,120,95
    --region projected_impact:173,156,20,19
    --region lower_actor_cluster:145,160,215,125
    --region hud_viewmodel:360,300,255,130
    --exclude-region lower_actor_cluster:145,160,215,125
    --exclude-region hud_viewmodel:360,300,255,130
    --max-offset-px 8
    --offset-step 2
    --max-size-delta-px 32
    --size-step 8
    --top 16
)

for region in tower_pane projected_impact impact_side; do
    echo ""
    echo "Scoring region: $region"
    tools/score_visual_presentation_alignment.py \
        --json-out "$OUT_DIR/presentation_alignment_${region}.json" \
        "${COMMON_ARGS[@]}" \
        --primary-region "$region" \
        "$STOCK_SCREENSHOT" \
        "$NATIVE_SCREENSHOT" \
        > "$OUT_DIR/presentation_alignment_${region}.log"
done

python3 - "$OUT_DIR" <<'PY'
import json
import sys
from pathlib import Path

out_dir = Path(sys.argv[1])
regions = {}
failures = []
for name in ("tower_pane", "projected_impact", "impact_side"):
    path = out_dir / f"presentation_alignment_{name}.json"
    try:
        data = json.loads(path.read_text())
    except Exception as exc:
        failures.append(f"{name}: {exc}")
        continue
    if data.get("status") != "pass":
        failures.append(f"{name}: scorer status {data.get('status')}")
    default = data.get("default") or {}
    best = data.get("best") or {}
    regions[name] = {
        "json": str(path),
        "default_changed_pct": default.get("changed_pct"),
        "best_changed_pct": best.get("changed_pct"),
        "improvement_pct_points": data.get("improvement_pct_points"),
        "best_baseline_logical_frame": best.get("baseline_logical_frame"),
        "best_test_logical_frame": best.get("test_logical_frame"),
        "best_adjustment": best.get("adjustment"),
        "candidate_count": data.get("search", {}).get("candidate_count"),
        "source_active_bbox": data.get("source_presentation", {}),
    }

tower_best = regions.get("tower_pane", {}).get("best_changed_pct")
impact_best = regions.get("impact_side", {}).get("best_changed_pct")
projected_best = regions.get("projected_impact", {}).get("best_changed_pct")
projected_improvement = regions.get("projected_impact", {}).get("improvement_pct_points")

interpretation = []
if tower_best is not None and impact_best is not None:
    if tower_best > 90.0 and impact_best > 85.0:
        interpretation.append(
            "presentation search does not explain the broad tower/impact-side mismatch"
        )
if projected_best is not None and projected_improvement is not None:
    if projected_improvement > 10.0 and projected_best > 50.0:
        interpretation.append(
            "projected-impact ROI is alignment-sensitive but remains heavily mismatched"
        )
if not interpretation:
    interpretation.append("inspect region JSON before drawing a presentation conclusion")

summary = {
    "status": "fail" if failures else "pass",
    "failures": failures,
    "regions": regions,
    "interpretation": interpretation,
}
(out_dir / "glass_pad10092_presentation_alignment_summary.json").write_text(
    json.dumps(summary, indent=2, sort_keys=True) + "\n"
)
print(json.dumps(summary, indent=2, sort_keys=True))
if failures:
    sys.exit(1)
PY

echo ""
echo "Summary: $OUT_DIR/glass_pad10092_presentation_alignment_summary.json"
