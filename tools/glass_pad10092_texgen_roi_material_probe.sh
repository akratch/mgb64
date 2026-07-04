#!/bin/bash
#
# glass_pad10092_texgen_roi_material_probe.sh -- identify which texgen material
# rows cover the Dam pad10092 impact ROI, with a shard-off control.
#
# This is a diagnostic ownership lane. It does not require stock ares: it asks
# the native renderer which material rows overlap the failing projected-impact
# region, then verifies whether GE007_GLASS_SHARDS=0 suppresses those rows under
# the same route and checkpoint.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=300
OUT_DIR="/tmp/mgb64_glass_pad10092_texgen_roi_material_$$"

ROUTE="dam_regular_glass_shatter_pad10092_impact_visual_probe"
ROUTE_JSON="tools/rom_oracle_routes/${ROUTE}.json"
TRACE_AFTER_FRAME=120
TRACE_BUDGET=3000
PRIMARY_REGION="projected_impact"

usage() {
    cat <<'USAGE'
Usage: tools/glass_pad10092_texgen_roi_material_probe.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary path (default: build/ge007)
  --build-dir DIR      CMake build directory (default: build)
  --no-build           reuse an existing native binary
  --timeout SECONDS    route timeout (default: 300)
  --after-frame N      first frame for TEXGEN-MATERIAL rows (default: 120)
  --budget N           TEXGEN-MATERIAL row budget per capture (default: 3000)

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

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"
validation_require_file "$ROUTE_JSON" "route JSON"

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

echo "=== Glass Pad10092 Texgen ROI Material Probe ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  route:   $ROUTE"
echo "  primary: $PRIMARY_REGION"
echo "  after:   $TRACE_AFTER_FRAME"
echo "  budget:  $TRACE_BUDGET"

capture_case() {
    local name="$1"
    shift

    local case_dir="$OUT_DIR/$name"
    rm -rf "$case_dir"
    mkdir -p "$case_dir"

    echo "=== capture: $name ==="
    env \
        GE007_TRACE_TEXGEN_MATERIALS=1 \
        GE007_TRACE_TEXGEN_MATERIALS_AFTER_FRAME="$TRACE_AFTER_FRAME" \
        GE007_TRACE_TEXGEN_MATERIALS_BUDGET="$TRACE_BUDGET" \
        GE007_TRACE_GLASS_SHARD_COVERAGE=1 \
        GE007_TRACE_GLASS_SHARD_COVERAGE_AFTER_FRAME="$TRACE_AFTER_FRAME" \
        GE007_TRACE_GLASS_SHARD_COVERAGE_BUDGET=40 \
        GE007_TRACE_GLASS_PROJECTION_ALL=1 \
        "$@" \
        tools/movement_oracle_capture.sh \
            --route "$ROUTE" \
            --native-only \
            --no-compare \
            --out-dir "$case_dir" \
            --rom "$ROM" \
            --binary "$BINARY" \
            --no-build \
            --timeout "$TIMEOUT_SECONDS"

    local gfxdl_matches="$case_dir/gfxdl_matches.txt"
    if grep -H -F "[GFX-DL]" "$case_dir"/*.log > "$gfxdl_matches"; then
        echo "FAIL: [GFX-DL] warning rows found for $name" >&2
        head -20 "$gfxdl_matches" | sed 's/^/  /' >&2
        exit 1
    fi
    rm -f "$gfxdl_matches"

    validation_require_file "$case_dir/native_${ROUTE}.log" "$name native log"
    validation_require_file "$case_dir/native_${ROUTE}.bmp" "$name native screenshot"

    local expect_rows=1
    if [[ "$name" == "shards_off" ]]; then
        expect_rows=0
    fi

    python3 tools/summarize_texgen_roi_materials.py \
        "$case_dir/native_${ROUTE}.log" \
        --route-json "$ROUTE_JSON" \
        --primary-region "$PRIMARY_REGION" \
        --expect-min-primary-rows "$expect_rows" \
        --top 20 \
        --top-groups 20 \
        --json-out "$case_dir/texgen_roi_material_summary.json" \
        > "$case_dir/texgen_roi_material_summary.txt"

    for region in tower_pane impact_side; do
        local region_expect=0
        if [[ "$region" == "tower_pane" ]]; then
            region_expect=1
        fi
        python3 tools/summarize_texgen_roi_materials.py \
            "$case_dir/native_${ROUTE}.log" \
            --route-json "$ROUTE_JSON" \
            --primary-region "$region" \
            --expect-min-primary-rows "$region_expect" \
            --top 20 \
            --top-groups 20 \
            --json-out "$case_dir/texgen_roi_material_${region}_summary.json" \
            > "$case_dir/texgen_roi_material_${region}_summary.txt"
    done
}

capture_case "default"
capture_case "shards_off" GE007_GLASS_SHARDS=0

ROUTE_REGION_ARGS=()
while IFS= read -r line; do
    [[ -n "$line" ]] || continue
    ROUTE_REGION_ARGS+=(--region "$line")
done < <(python3 tools/rom_oracle_route.py visual-regions "$ROUTE_JSON")

python3 tools/compare_screenshots.py \
    "$OUT_DIR/default/native_${ROUTE}.bmp" \
    "$OUT_DIR/shards_off/native_${ROUTE}.bmp" \
    --logical-size 320,240 \
    --logical-viewport 0,10,320,220 \
    --baseline-logical-frame full \
    --test-logical-frame full \
    "${ROUTE_REGION_ARGS[@]}" \
    --json-out "$OUT_DIR/default_vs_shards_off_visual.json" \
    > "$OUT_DIR/default_vs_shards_off_visual.txt"

python3 tools/compare_glass_shard_pixel_oracle.py \
    --baseline-trace "$OUT_DIR/default/native_${ROUTE}.jsonl" \
    --test-trace "$OUT_DIR/shards_off/native_${ROUTE}.jsonl" \
    --baseline-image "$OUT_DIR/default/native_${ROUTE}.bmp" \
    --test-image "$OUT_DIR/shards_off/native_${ROUTE}.bmp" \
    --logical-size 320,240 \
    --logical-viewport 0,10,320,220 \
    --baseline-logical-frame full \
    --test-logical-frame full \
    --mask-mode triangle \
    --mask-padding 1 \
    --require-full-sample \
    --json-out "$OUT_DIR/default_vs_shards_off_shard_pixel_oracle.json" \
    > "$OUT_DIR/default_vs_shards_off_shard_pixel_oracle.txt"

python3 - "$OUT_DIR" "$ROUTE" "$PRIMARY_REGION" <<'PY'
import json
import sys
from pathlib import Path
from typing import Any

root = Path(sys.argv[1])
route = sys.argv[2]
primary_region = sys.argv[3]
failures: list[str] = []


def load_json(path: Path) -> dict[str, Any]:
    if not path.exists():
        failures.append(f"missing JSON artifact: {path}")
        return {}
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def region_map(compare: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {
        item.get("name"): item
        for item in compare.get("regions", [])
        if isinstance(item, dict) and item.get("name")
    }


def changed_pct(region: dict[str, Any] | None) -> float | None:
    if not region:
        return None
    value = region.get("changed_pct")
    if not isinstance(value, (int, float)):
        value = (region.get("full") or {}).get("changed_pct")
    return float(value) if isinstance(value, (int, float)) else None


default = load_json(root / "default" / "texgen_roi_material_summary.json")
shards_off = load_json(root / "shards_off" / "texgen_roi_material_summary.json")
visual = load_json(root / "default_vs_shards_off_visual.json")
shard_oracle = load_json(root / "default_vs_shards_off_shard_pixel_oracle.json")
region_materials: dict[str, Any] = {}
for case_name in ("default", "shards_off"):
    region_materials[case_name] = {}
    for region_name, filename in (
        (primary_region, "texgen_roi_material_summary.json"),
        ("tower_pane", "texgen_roi_material_tower_pane_summary.json"),
        ("impact_side", "texgen_roi_material_impact_side_summary.json"),
    ):
        data = load_json(root / case_name / filename)
        region_materials[case_name][region_name] = {
            "summary": str(root / case_name / filename),
            "status": data.get("status"),
            "line_counts": data.get("line_counts"),
            "primary_effect_counts": data.get("primary_effect_counts"),
            "primary_class_counts": data.get("primary_class_counts"),
        }

default_rows = int((default.get("line_counts") or {}).get("texgen_rows_in_primary_region") or 0)
off_rows = int((shards_off.get("line_counts") or {}).get("texgen_rows_in_primary_region") or 0)
default_effects = default.get("primary_effect_counts") or {}
off_effects = shards_off.get("primary_effect_counts") or {}
default_shard_rows = int(default_effects.get("glass_shards") or 0)
off_shard_rows = int(off_effects.get("glass_shards") or 0)

if default.get("status") != "pass":
    failures.append("default texgen ROI summary failed")
if default_rows <= 0:
    failures.append("default capture produced no texgen rows in the primary ROI")
if default_shard_rows > 0 and off_shard_rows != 0:
    failures.append(
        "GE007_GLASS_SHARDS=0 did not suppress primary-ROI glass_shards material rows "
        f"({off_shard_rows} remained)"
    )
if shard_oracle.get("status") != "pass":
    failures.append("default-vs-shards_off shard pixel oracle failed")

regions = region_map(visual)
primary_changed = changed_pct(regions.get(primary_region))
full_changed = changed_pct(visual)
oracle_union = shard_oracle.get("union_metrics") or {}
oracle_coverage = shard_oracle.get("coverage") or {}
oracle_changed = oracle_union.get("changed_pct")
oracle_pixels = oracle_coverage.get("pixels")

interpretation = [
    f"default primary ROI rows: {default_rows} total, effects={default_effects}",
    f"shards_off primary ROI rows: {off_rows} total, effects={off_effects}",
]
if primary_changed is not None:
    interpretation.append(
        f"default-vs-shards_off {primary_region} changed_pct={primary_changed:.3f}"
    )
if full_changed is not None:
    interpretation.append(f"default-vs-shards_off full changed_pct={full_changed:.3f}")
if isinstance(oracle_changed, (int, float)):
    interpretation.append(
        f"default-vs-shards_off shard-mask changed_pct={float(oracle_changed):.3f} "
        f"over {int(oracle_pixels or 0)} rasterized pixels"
    )
default_tower = region_materials["default"].get("tower_pane", {})
default_impact_side = region_materials["default"].get("impact_side", {})
tower_counts = default_tower.get("line_counts") or {}
impact_side_counts = default_impact_side.get("line_counts") or {}
if tower_counts:
    interpretation.append(
        "default tower_pane texgen rows: "
        f"{int(tower_counts.get('texgen_rows_in_primary_region') or 0)} "
        f"{default_tower.get('primary_effect_counts') or {}}"
    )
if impact_side_counts:
    interpretation.append(
        "default impact_side texgen rows: "
        f"{int(impact_side_counts.get('texgen_rows_in_primary_region') or 0)}"
    )
if default_shard_rows > 0 and off_shard_rows == 0:
    interpretation.append(
        "GE007_GLASS_SHARDS=0 is a valid control for the traced glass_shards path on this route"
    )
elif default_shard_rows == 0:
    interpretation.append(
        "corrected aligned-crop ROI ownership shows the primary ROI is not owned by "
        "falling-shard texgen rows at this checkpoint"
    )
if (
    default_shard_rows > 0
    and off_shard_rows == 0
    and primary_changed is not None
    and abs(primary_changed) <= 0.000001
    and isinstance(oracle_changed, (int, float))
    and abs(float(oracle_changed)) <= 0.000001
):
    interpretation.append(
        "the material rows are removed but neither the logical ROI nor the shard-mask "
        "pixels move; material bbox overlap is not final framebuffer ownership"
    )
elif default_shard_rows > 0 and off_shard_rows == 0 and primary_changed is not None:
    interpretation.append(
        "the traced glass_shards rows are pixel-visible in the primary ROI and remain the "
        "best next target"
    )

payload = {
    "status": "fail" if failures else "pass",
    "failures": failures,
    "route": route,
    "primary_region": primary_region,
    "default": {
        "summary": str(root / "default" / "texgen_roi_material_summary.json"),
        "primary_rows": default_rows,
        "primary_effect_counts": default_effects,
    },
    "shards_off": {
        "summary": str(root / "shards_off" / "texgen_roi_material_summary.json"),
        "primary_rows": off_rows,
        "primary_effect_counts": off_effects,
    },
    "visual": {
        "compare": str(root / "default_vs_shards_off_visual.json"),
        "full_changed_pct": full_changed,
        "primary_changed_pct": primary_changed,
    },
    "shard_pixel_oracle": {
        "compare": str(root / "default_vs_shards_off_shard_pixel_oracle.json"),
        "status": shard_oracle.get("status"),
        "coverage_pixels": oracle_pixels,
        "union_changed_pct": oracle_changed,
    },
    "region_materials": region_materials,
    "interpretation": interpretation,
}

out = root / "glass_pad10092_texgen_roi_material_audit.json"
out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
txt = root / "glass_pad10092_texgen_roi_material_audit.txt"
with txt.open("w", encoding="utf-8") as handle:
    print(f"status: {payload['status']}", file=handle)
    for item in interpretation:
        print(f"- {item}", file=handle)
    if failures:
        print("failures:", file=handle)
        for failure in failures:
            print(f"- {failure}", file=handle)

print(txt.read_text(encoding="utf-8"), end="")
if failures:
    raise SystemExit(1)
PY

echo ""
echo "Summary: $OUT_DIR/glass_pad10092_texgen_roi_material_audit.json"
