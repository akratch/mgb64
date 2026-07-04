#!/bin/bash
#
# glass_actor_masked_variant_sweep.sh -- A/B native shard renderer variants
# against one stock pad10092 actor-masked fixture.
#
# The stock capture is reused so this sweep measures renderer/presentation
# toggles, not ares startup variance. If --stock-case-dir is omitted, the script
# first runs glass_actor_masked_visual_regression.sh to create one.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

ROUTE="dam_regular_glass_shatter_pad10092_actor_masked_visual_probe"
BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
ARES_BIN=""
DO_BUILD=1
TIMEOUT_SECONDS=240
OUT_DIR="/tmp/mgb64_glass_actor_masked_variant_sweep_$$"
STOCK_CASE_DIR=""
VARIANT_SPECS=()

usage() {
    cat <<'USAGE'
Usage: tools/glass_actor_masked_variant_sweep.sh [options]

Options:
  --stock-case-dir DIR Reuse a prior pad10092_actor_masked case directory.
                       If omitted, the script creates one via the full
                       glass_actor_masked_visual_regression.sh gate.
  --variant SPEC       Add variant NAME:ENV=VALUE[,ENV=VALUE]. Repeatable.
                       If omitted, a default shard-renderer matrix is used.
  --out-dir DIR        output directory (default: /tmp/...)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary path (default: build/ge007)
  --build-dir DIR      CMake build directory (default: build)
  --ares-bin PATH      instrumented ares binary, only needed if no stock case
  --no-build           reuse an existing native binary
  --timeout SECONDS    per native capture timeout (default: 240)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, emulator output, or generated summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --stock-case-dir) STOCK_CASE_DIR="$2"; shift 2 ;;
        --variant) VARIANT_SPECS+=("$2"); shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --ares-bin) ARES_BIN="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
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

if [[ -z "$ARES_BIN" ]]; then
    if [[ -x "build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares" ]]; then
        ARES_BIN="build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares"
    else
        ARES_BIN="build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares"
    fi
fi
ARES_BIN="$(validation_resolve_path "$ARES_BIN")"

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

if [[ "${#VARIANT_SPECS[@]}" -eq 0 ]]; then
    VARIANT_SPECS+=("default:")
    VARIANT_SPECS+=("shards_off:GE007_GLASS_SHARDS=0")
    VARIANT_SPECS+=("fixed_mtx:GE007_GLASS_SHARD_FIXED_MTX=1")
    VARIANT_SPECS+=("compress:GE007_GLASS_SHARD_COMPRESS=1")
    VARIANT_SPECS+=("legacy_basis_scale:GE007_GLASS_SHARD_BASIS_SCALE=1")
    VARIANT_SPECS+=("legacy_field_10e0_scaled:GE007_FIELD_10E0_SCALED=1")
    VARIANT_SPECS+=("loaded_tile_2tex_filter:GE007_DIAG_LOADED_TILE_2TEX_N64_FILTER=0x00f38e4f020a2d12")
    VARIANT_SPECS+=("nopersp_inputs:GE007_DIAG_NOPERSPECTIVE_CC_INPUTS=0x00f38e4f020a2d12")
    VARIANT_SPECS+=("nopersp_cc:GE007_DIAG_NOPERSPECTIVE_CC=0x00f38e4f020a2d12")
fi

resolve_stock_case_dir() {
    local dir="$1"
    if [[ -z "$dir" ]]; then
        return 1
    fi
    dir="$(validation_resolve_path "$dir")"
    if [[ -s "$dir/stock_${ROUTE}.jsonl" && -s "$dir/stock_${ROUTE}.ppm" ]]; then
        printf '%s\n' "$dir"
        return 0
    fi
    if [[ -s "$dir/pad10092_actor_masked/stock_${ROUTE}.jsonl" ]]; then
        printf '%s\n' "$dir/pad10092_actor_masked"
        return 0
    fi
    if [[ -s "$dir/glass_actor_masked/pad10092_actor_masked/stock_${ROUTE}.jsonl" ]]; then
        printf '%s\n' "$dir/glass_actor_masked/pad10092_actor_masked"
        return 0
    fi
    local found
    found="$(find "$dir" -maxdepth 4 -type f -name "stock_${ROUTE}.jsonl" -print -quit 2>/dev/null || true)"
    if [[ -n "$found" ]]; then
        dirname "$found"
        return 0
    fi
    return 1
}

if [[ -n "$STOCK_CASE_DIR" ]]; then
    if ! STOCK_CASE_DIR="$(resolve_stock_case_dir "$STOCK_CASE_DIR")"; then
        echo "FAIL: --stock-case-dir does not contain stock_${ROUTE}.jsonl: $STOCK_CASE_DIR" >&2
        exit 2
    fi
else
    validation_require_binary "$ARES_BIN"
    BASELINE_DIR="$OUT_DIR/stock_baseline"
    tools/glass_actor_masked_visual_regression.sh \
        --no-build \
        --binary "$BINARY" \
        --rom "$ROM" \
        --ares-bin "$ARES_BIN" \
        --out-dir "$BASELINE_DIR" \
        --timeout "$TIMEOUT_SECONDS"
    STOCK_CASE_DIR="$BASELINE_DIR/pad10092_actor_masked"
fi

STOCK_TRACE="$STOCK_CASE_DIR/stock_${ROUTE}.jsonl"
STOCK_SCREENSHOT="$STOCK_CASE_DIR/stock_${ROUTE}.ppm"
validation_require_file "$STOCK_TRACE" "stock trace"
validation_require_file "$STOCK_SCREENSHOT" "stock screenshot"

VARIANT_INDEX="$OUT_DIR/variants.tsv"
SUMMARY_JSON="$OUT_DIR/variant_sweep_summary.json"
printf 'name\tenv\tcase_dir\thealth_json\tglass_json\tactor_json\tvisual_json\n' >"$VARIANT_INDEX"

echo "=== Glass Actor-Masked Variant Sweep ==="
echo "  out-dir:        $OUT_DIR"
echo "  stock-case-dir: $STOCK_CASE_DIR"
echo "  binary:         $BINARY"
echo "  ROM:            $ROM"
echo "  variants:       ${#VARIANT_SPECS[@]}"

sanitize_name() {
    printf '%s' "$1" | tr -c 'A-Za-z0-9_.-' '_'
}

run_variant() {
    local spec="$1"
    local name="${spec%%:*}"
    local env_text=""
    if [[ "$spec" == *:* ]]; then
        env_text="${spec#*:}"
    fi
    if [[ -z "$name" ]]; then
        echo "FAIL: variant name must not be empty: $spec" >&2
        exit 2
    fi

    local safe_name
    safe_name="$(sanitize_name "$name")"
    local case_dir="$OUT_DIR/variants/$safe_name"
    local env_pairs=()
    local env_display="$env_text"
    mkdir -p "$case_dir"

    if [[ -n "$env_text" ]]; then
        IFS=',' read -r -a env_pairs <<< "$env_text"
        for pair in "${env_pairs[@]}"; do
            if [[ ! "$pair" =~ ^[A-Za-z_][A-Za-z0-9_]*=.+$ ]]; then
                echo "FAIL: malformed env assignment in variant $name: $pair" >&2
                exit 2
            fi
        done
    fi

    echo ""
    echo "=== variant: $name ==="
    if [[ "${#env_pairs[@]}" -gt 0 ]]; then
        printf '  env:'
        printf ' %s' "${env_pairs[@]}"
        printf '\n'
    fi

    local capture_cmd=(
        tools/movement_oracle_capture.sh
        --route "$ROUTE"
        --native-only
        --no-compare
        --out-dir "$case_dir"
        --rom "$ROM"
        --binary "$BINARY"
        --no-build
        --timeout "$TIMEOUT_SECONDS"
    )
    if [[ "${#env_pairs[@]}" -gt 0 ]]; then
        env "${env_pairs[@]}" "${capture_cmd[@]}"
    else
        "${capture_cmd[@]}"
    fi

    local log="$case_dir/native_${ROUTE}.log"
    local native_trace="$case_dir/native_${ROUTE}.jsonl"
    local native_screenshot="$case_dir/native_${ROUTE}.bmp"
    local health_json="$case_dir/health.json"
    local glass_json="$case_dir/glass.json"
    local actor_json="$case_dir/actor_composition.json"
    local visual_json="$case_dir/visual_actor_masked.json"
    local heatmap="$case_dir/visual_actor_masked.png"

    if grep -H -F "[GFX-DL]" "$log" > "$case_dir/gfxdl_matches.txt"; then
        echo "FAIL: [GFX-DL] warning rows found in variant $name" >&2
        head -20 "$case_dir/gfxdl_matches.txt" | sed 's/^/  /' >&2
        exit 1
    fi
    rm -f "$case_dir/gfxdl_matches.txt"

    python3 tools/compare_combat_health_trace.py \
        --baseline-label "stock ${ROUTE}" \
        --test-label "native ${ROUTE} ${name}" \
        --baseline-frame 2541 \
        --test-frame 126 \
        --health-tolerance 0.001 \
        --damage-show-tolerance 1 \
        --require-match \
        --json-out "$health_json" \
        "$STOCK_TRACE" \
        "$native_trace"

    python3 tools/compare_glass_trace.py \
        --require-active \
        --max-active-tolerance 0 \
        --first-position-tolerance 1.0 \
        --require-prop-destroyed \
        --prop-position-tolerance 1.0 \
        --max-buffer-len 200 \
        --json-out "$glass_json" \
        "$STOCK_TRACE" \
        "$native_trace"

    python3 tools/score_actor_composition_checkpoints.py \
        --require-active \
        --actor-chrnum 44 \
        --actor-chrnum 7 \
        --fields alive,hidden,onscreen,rendered,action \
        --position-tolerance 25.0 \
        --top 5 \
        --json-out "$actor_json" \
        "$STOCK_TRACE" \
        "$native_trace"

    python3 tools/compare_actor_masked_visual.py \
        --logical-size 320,240 \
        --logical-viewport 0,10,320,220 \
        --baseline-logical-frame active \
        --test-logical-frame full \
        --region tower_pane:80,115,320,180 \
        --region impact_side:255,145,120,95 \
        --region lower_actor_cluster:145,160,215,125 \
        --region hud_viewmodel:360,300,255,130 \
        --exclude-region lower_actor_cluster:145,160,215,125 \
        --exclude-region hud_viewmodel:360,300,255,130 \
        --heatmap "$heatmap" \
        --json-out "$visual_json" \
        "$STOCK_SCREENSHOT" \
        "$native_screenshot"

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$name" "$env_display" "$case_dir" "$health_json" "$glass_json" "$actor_json" "$visual_json" \
        >>"$VARIANT_INDEX"
}

for spec in "${VARIANT_SPECS[@]}"; do
    run_variant "$spec"
done

python3 - "$VARIANT_INDEX" "$SUMMARY_JSON" "$STOCK_CASE_DIR" "$ROUTE" <<'PY'
import csv
import json
import sys
from pathlib import Path
from typing import Any

from PIL import Image

sys.path.insert(0, str(Path("tools").resolve()))
import compare_screenshots as screenshots

index_path = Path(sys.argv[1])
summary_path = Path(sys.argv[2])
stock_case_dir = Path(sys.argv[3])
route = sys.argv[4]

def load(path: str) -> dict[str, Any]:
    return json.loads(Path(path).read_text(encoding="utf-8"))

def region_metric(visual: dict[str, Any], name: str, branch: str, key: str) -> float | None:
    for region in visual.get("regions", []):
        if region.get("name") == name:
            value = region.get(branch, {}).get(key)
            return float(value) if isinstance(value, (int, float)) else None
    return None


def native_aligned_crop(path: Path, target_size: tuple[int, int]) -> Image.Image:
    image = Image.open(path).convert("RGB")
    crop = screenshots.logical_crop_bbox(
        image,
        0,
        (320, 240),
        (0, 10, 320, 220),
        "full",
    )
    cropped = screenshots.crop_bbox(image, crop)
    if cropped.size != target_size:
        resampling = getattr(Image, "Resampling", Image).BILINEAR
        cropped = cropped.resize(target_size, resampling)
    return cropped


def stock_crop_size() -> tuple[int, int]:
    stock_image = Image.open(stock_case_dir / f"stock_{route}.ppm").convert("RGB")
    crop = screenshots.logical_crop_bbox(
        stock_image,
        0,
        (320, 240),
        (0, 10, 320, 220),
        "active",
    )
    return crop[2], crop[3]


def direct_metrics(default_image: Image.Image, variant_image: Image.Image) -> dict[str, Any]:
    full = screenshots.diff_metrics(
        screenshots.rgb_pixels(default_image),
        screenshots.rgb_pixels(variant_image),
    )
    regions: dict[str, Any] = {}
    for name, roi in {
        "tower_pane": (80, 115, 320, 180),
        "impact_side": (255, 145, 120, 95),
    }.items():
        x, y, w, h = roi
        base = default_image.crop((x, y, x + w, y + h))
        test = variant_image.crop((x, y, x + w, y + h))
        regions[name] = screenshots.diff_metrics(
            screenshots.rgb_pixels(base),
            screenshots.rgb_pixels(test),
        )
    return {
        "full_changed_pct": full["changed_pct"],
        "full_changed_pixels": full["changed_pixels"],
        "tower_pane_changed_pct": regions["tower_pane"]["changed_pct"],
        "tower_pane_changed_pixels": regions["tower_pane"]["changed_pixels"],
        "impact_side_changed_pct": regions["impact_side"]["changed_pct"],
        "impact_side_changed_pixels": regions["impact_side"]["changed_pixels"],
    }

rows = []
with index_path.open("r", encoding="utf-8") as handle:
    for row in csv.DictReader(handle, delimiter="\t"):
        visual = load(row["visual_json"])
        glass = load(row["glass_json"])
        actor = load(row["actor_json"])
        best_actor = (actor.get("best") or [{}])[0]
        entry = {
            **row,
            "visual_status": visual.get("status"),
            "full_changed_pct": visual.get("full", {}).get("changed_pct"),
            "masked_changed_pct": visual.get("masked", {}).get("changed_pct"),
            "masked_excluded_pct": visual.get("masked", {}).get("excluded_pct"),
            "tower_pane_full_changed_pct": region_metric(visual, "tower_pane", "full", "changed_pct"),
            "tower_pane_masked_changed_pct": region_metric(visual, "tower_pane", "masked", "changed_pct"),
            "impact_side_full_changed_pct": region_metric(visual, "impact_side", "full", "changed_pct"),
            "impact_side_masked_changed_pct": region_metric(visual, "impact_side", "masked", "changed_pct"),
            "tower_pane_bright_baseline": region_metric(visual, "tower_pane", "masked", "features") if False else None,
            "max_active": glass.get("test", {}).get("max_active"),
            "first_position_delta": glass.get("first_position_delta"),
            "prop_position_delta": glass.get("prop_position_delta"),
            "actor_best_score": best_actor.get("score"),
            "actor_strict_matches": len(actor.get("best_strict") or []),
            "actor_visible_set_delta": best_actor.get("visible_set_delta"),
        }

        tower = next(
            (region for region in visual.get("regions", []) if region.get("name") == "tower_pane"),
            {},
        )
        impact = next(
            (region for region in visual.get("regions", []) if region.get("name") == "impact_side"),
            {},
        )
        entry["tower_pane_masked_bright"] = {
            side: tower.get("masked", {}).get("features", {}).get(side, {}).get("bright_pixels")
            for side in ("baseline", "test")
        }
        entry["impact_side_masked_bright"] = {
            side: impact.get("masked", {}).get("features", {}).get(side, {}).get("bright_pixels")
            for side in ("baseline", "test")
        }
        rows.append(entry)

default = next((row for row in rows if row["name"] == "default"), None)
target_size = stock_crop_size()
default_native = None
if default:
    default_native = native_aligned_crop(
        Path(default["case_dir"]) / f"native_{route}.bmp",
        target_size,
    )

if default:
    for row in rows:
        for key in (
            "full_changed_pct",
            "masked_changed_pct",
            "tower_pane_masked_changed_pct",
            "impact_side_masked_changed_pct",
        ):
            lhs = row.get(key)
            rhs = default.get(key)
            row[f"{key}_delta_vs_default"] = (
                float(lhs) - float(rhs)
                if isinstance(lhs, (int, float)) and isinstance(rhs, (int, float))
                else None
            )
        if default_native is not None:
            variant_native = native_aligned_crop(
                Path(row["case_dir"]) / f"native_{route}.bmp",
                target_size,
            )
            row["native_delta_vs_default"] = direct_metrics(default_native, variant_native)

ranked = sorted(
    rows,
    key=lambda row: (
        float(row.get("tower_pane_masked_changed_pct") or 1000.0),
        float(row.get("masked_changed_pct") or 1000.0),
        row["name"],
    ),
)

payload = {
    "status": "pass",
    "stock_case_dir": str(stock_case_dir),
    "variant_index": str(index_path),
    "variants": rows,
    "ranked_by_tower_pane_masked": ranked,
    "best": ranked[0] if ranked else None,
}
summary_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

print("=== variant ranking: tower_pane_masked_changed_pct ===")
for row in ranked:
    print(
        "{name}: tower={tower:.3f}% masked={masked:.3f}% "
        "impact={impact:.3f}% delta_tower={delta:+.3f} "
        "native_delta={native_delta:.4f}%".format(
            name=row["name"],
            tower=float(row.get("tower_pane_masked_changed_pct") or 0.0),
            masked=float(row.get("masked_changed_pct") or 0.0),
            impact=float(row.get("impact_side_masked_changed_pct") or 0.0),
            delta=float(row.get("tower_pane_masked_changed_pct_delta_vs_default") or 0.0),
            native_delta=float(
                (row.get("native_delta_vs_default") or {}).get("full_changed_pct") or 0.0
            ),
        )
    )
PY

echo ""
echo "PASS: glass actor-masked variant sweep"
echo "summary_json: $SUMMARY_JSON"
echo "index: $VARIANT_INDEX"
echo "artifacts: $OUT_DIR"
