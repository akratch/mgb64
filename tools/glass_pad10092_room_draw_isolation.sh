#!/bin/bash
#
# glass_pad10092_room_draw_isolation.sh -- Measure whether the pad10092
# native-only extra room-124 draw-list entry owns any pixels at the impact route.
#
# This is a diagnostic guard, not a production rendering fix. It reuses the
# stock/default pad10092 impact fixture, then runs native-only variants that
# suppress room 124 through metadata and draw hooks. The useful result is whether
# those variants change the native screenshot or the stock/native state gates.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

ROUTE="dam_regular_glass_shatter_pad10092_impact_visual_probe"
BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
ARES_BIN=""
DO_BUILD=1
TIMEOUT_SECONDS=300
OUT_DIR="/tmp/mgb64_glass_pad10092_room_draw_isolation_$$"
BASE_CASE_DIR=""

usage() {
    cat <<'USAGE'
Usage: tools/glass_pad10092_room_draw_isolation.sh [options]

Options:
  --base-case-dir DIR  existing pad10092 impact case dir, or a parent containing
                       pad10092_impact; if omitted, the script captures one
  --out-dir DIR        output directory (default: /tmp/...)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary path (default: build/ge007)
  --build-dir DIR      CMake build directory (default: build)
  --ares-bin PATH      instrumented ares binary, only needed if capturing base
  --no-build           reuse an existing native binary
  --timeout SECONDS    route timeout (default: 300)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, emulator output, or generated summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --base-case-dir) BASE_CASE_DIR="$2"; shift 2 ;;
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

resolve_base_case_dir() {
    local dir="$1"
    if [[ -z "$dir" ]]; then
        return 1
    fi
    dir="$(validation_resolve_path "$dir")"
    if [[ -s "$dir/stock_${ROUTE}.jsonl" && -s "$dir/native_${ROUTE}.jsonl" ]]; then
        printf '%s\n' "$dir"
        return 0
    fi
    if [[ -s "$dir/pad10092_impact/stock_${ROUTE}.jsonl" ]]; then
        printf '%s\n' "$dir/pad10092_impact"
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

if [[ -n "$BASE_CASE_DIR" ]]; then
    if ! BASE_CASE_DIR="$(resolve_base_case_dir "$BASE_CASE_DIR")"; then
        echo "FAIL: --base-case-dir does not contain pad10092 impact traces: $BASE_CASE_DIR" >&2
        exit 2
    fi
else
    validation_require_binary "$ARES_BIN"
    tools/glass_pad10092_impact_visual_regression.sh \
        --no-build \
        --binary "$BINARY" \
        --rom "$ROM" \
        --ares-bin "$ARES_BIN" \
        --out-dir "$OUT_DIR/base" \
        --timeout "$TIMEOUT_SECONDS"
    BASE_CASE_DIR="$OUT_DIR/base/pad10092_impact"
fi

STOCK_TRACE="$BASE_CASE_DIR/stock_${ROUTE}.jsonl"
NATIVE_TRACE="$BASE_CASE_DIR/native_${ROUTE}.jsonl"
STOCK_SCREENSHOT="$BASE_CASE_DIR/stock_${ROUTE}.ppm"
NATIVE_SCREENSHOT="$BASE_CASE_DIR/native_${ROUTE}.bmp"
SUMMARY_JSON="$OUT_DIR/glass_pad10092_room_draw_isolation_summary.json"
INDEX_TSV="$OUT_DIR/variants.tsv"

validation_require_file "$STOCK_TRACE" "stock trace"
validation_require_file "$NATIVE_TRACE" "native default trace"
validation_require_file "$STOCK_SCREENSHOT" "stock screenshot"
validation_require_file "$NATIVE_SCREENSHOT" "native default screenshot"

echo "=== Glass Pad10092 Room Draw Isolation ==="
echo "  out-dir:       $OUT_DIR"
echo "  base-case-dir: $BASE_CASE_DIR"
echo "  route:         $ROUTE"
echo "  binary:        $BINARY"
echo "  ROM:           $ROM"

printf 'name\tenv\tcase_dir\tframing_json\tglass_json\tprojection_json\tnative_delta_json\tvisual_json\n' >"$INDEX_TSV"

run_variant() {
    local name="$1"
    shift
    local env_pairs=("$@")
    local case_dir="$OUT_DIR/variants/$name"
    local native_trace="$case_dir/native_${ROUTE}.jsonl"
    local native_screenshot="$case_dir/native_${ROUTE}.bmp"
    local log="$case_dir/native_${ROUTE}.log"
    local framing_json="$case_dir/framing.json"
    local glass_json="$case_dir/glass.json"
    local projection_json="$case_dir/projection.json"
    local native_delta_json="$case_dir/native_default_vs_${name}.json"
    local visual_json="$case_dir/stock_vs_${name}_visual.json"
    local heatmap="$case_dir/stock_vs_${name}_visual.png"
    local env_display

    mkdir -p "$case_dir"
    env_display="$(IFS=,; printf '%s' "${env_pairs[*]}")"

    echo ""
    echo "=== variant: $name ==="
    printf '  env:'
    printf ' %s' "${env_pairs[@]}"
    printf '\n'

    env "${env_pairs[@]}" \
        tools/movement_oracle_capture.sh \
        --route "$ROUTE" \
        --native-only \
        --no-compare \
        --out-dir "$case_dir" \
        --rom "$ROM" \
        --binary "$BINARY" \
        --no-build \
        --timeout "$TIMEOUT_SECONDS"

    if grep -H -F "[GFX-DL]" "$log" >"$case_dir/gfxdl_matches.txt"; then
        echo "FAIL: [GFX-DL] warning rows found in variant $name" >&2
        head -20 "$case_dir/gfxdl_matches.txt" | sed 's/^/  /' >&2
        exit 1
    fi
    rm -f "$case_dir/gfxdl_matches.txt"

    python3 tools/compare_visual_framing_trace.py \
        --baseline-label "stock ${ROUTE}" \
        --test-label "native ${ROUTE} ${name}" \
        --baseline-frame 2541 \
        --test-frame 126 \
        --max-camera-pos-delta 0.25 \
        --max-camera-target-delta 0.25 \
        --max-render-camera-pos-delta 0.25 \
        --max-render-camera-target-delta 0.25 \
        --max-cam-up-delta 0.01 \
        --max-facing-delta 0.01 \
        --max-room-basis-delta 0.01 \
        --max-view-delta 0.0 \
        --max-vv-verta-delta 0.001 \
        --json-out "$framing_json" \
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

    python3 tools/compare_glass_projection_trace.py \
        --require-present \
        --baseline-frame 2541 \
        --test-frame 126 \
        --max-active-delta 0 \
        --max-projected-delta 0 \
        --max-onscreen-delta 0 \
        --max-behind-delta 0 \
        --max-max-area-pct-delta 0.10 \
        --max-union-area-pct-delta 1.00 \
        --json-out "$projection_json" \
        "$STOCK_TRACE" \
        "$native_trace"

    python3 tools/compare_screenshots.py \
        "$NATIVE_SCREENSHOT" \
        "$native_screenshot" \
        --max-changed-pct 0.01 \
        --json-out "$native_delta_json" \
        >"$case_dir/native_default_vs_${name}.txt"

    python3 tools/compare_actor_masked_visual.py \
        --logical-size 320,240 \
        --logical-viewport 0,10,320,220 \
        --baseline-logical-frame active \
        --test-logical-frame full \
        --region tower_pane:80,115,320,180 \
        --region impact_side:255,145,120,95 \
        --region projected_impact:173,156,20,19 \
        --region lower_actor_cluster:145,160,215,125 \
        --region hud_viewmodel:360,300,255,130 \
        --exclude-region lower_actor_cluster:145,160,215,125 \
        --exclude-region hud_viewmodel:360,300,255,130 \
        --heatmap "$heatmap" \
        --json-out "$visual_json" \
        "$STOCK_SCREENSHOT" \
        "$native_screenshot"

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$name" "$env_display" "$case_dir" "$framing_json" "$glass_json" "$projection_json" "$native_delta_json" "$visual_json" \
        >>"$INDEX_TSV"
}

run_variant "force_unrender_room124" "GE007_FORCE_UNRENDERED_ROOMS=124"
run_variant "skip_bg_room124" "GE007_SKIP_BG_ROOM=124"

python3 - "$INDEX_TSV" "$SUMMARY_JSON" "$BASE_CASE_DIR" "$ROUTE" <<'PY'
import csv
import json
import sys
from pathlib import Path
from typing import Any

index_path = Path(sys.argv[1])
summary_path = Path(sys.argv[2])
base_case_dir = Path(sys.argv[3])
route = sys.argv[4]

def load(path: str | Path) -> dict[str, Any]:
    return json.loads(Path(path).read_text(encoding="utf-8"))

def frame_record(path: Path, frame: int) -> dict[str, Any]:
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            record = json.loads(line)
            if record.get("f") == frame:
                return record
    raise SystemExit(f"FAIL: missing frame {frame}: {path}")

def rooms(record: dict[str, Any]) -> dict[str, Any]:
    value = record.get("rooms", {})
    vis = value.get("vis", {}) if isinstance(value, dict) else {}
    return {
        "rendered": vis.get("rendered"),
        "sample": vis.get("sample"),
        "draw_sample": vis.get("draw_sample"),
        "rooms_drawn": record.get("rooms_drawn"),
        "tris": record.get("tris"),
    }

def region_metric(visual: dict[str, Any], name: str, branch: str, key: str) -> Any:
    for region in visual.get("regions", []):
        if region.get("name") == name:
            return region.get(branch, {}).get(key)
    return None

default_record = frame_record(base_case_dir / f"native_{route}.jsonl", 126)
rows: list[dict[str, Any]] = []
failures: list[str] = []

with index_path.open("r", encoding="utf-8") as handle:
    for row in csv.DictReader(handle, delimiter="\t"):
        framing = load(row["framing_json"])
        glass = load(row["glass_json"])
        projection = load(row["projection_json"])
        native_delta = load(row["native_delta_json"])
        visual = load(row["visual_json"])
        variant_record = frame_record(Path(row["case_dir"]) / f"native_{route}.jsonl", 126)

        changed_pct = float(native_delta.get("changed_pct", 100.0))
        if changed_pct > 0.01:
            failures.append(f"{row['name']}: native screenshot changed {changed_pct:.3f}% > 0.01%")
        for label, payload in (
            ("framing", framing),
            ("glass", glass),
            ("projection", projection),
            ("native_delta", native_delta),
            ("visual", visual),
        ):
            if payload.get("status") != "pass":
                failures.append(f"{row['name']}: {label} status is {payload.get('status')}")

        rows.append(
            {
                "name": row["name"],
                "env": row["env"],
                "case_dir": row["case_dir"],
                "native_default_vs_variant_changed_pct": changed_pct,
                "default_rooms": rooms(default_record),
                "variant_rooms": rooms(variant_record),
                "framing_room_delta": (framing.get("deltas") or {}).get("room_set"),
                "stock_vs_variant_masked_changed_pct": (visual.get("masked") or {}).get("changed_pct"),
                "stock_vs_variant_projected_impact_changed_pct": region_metric(
                    visual, "projected_impact", "masked", "changed_pct"
                ),
            }
        )

summary = {
    "status": "fail" if failures else "pass",
    "failures": failures,
    "route": route,
    "base_case_dir": str(base_case_dir),
    "note": (
        "Room 124 is an extra native draw-list/room-set entry on the pad10092 "
        "impact checkpoint, but both metadata suppression and room-124 draw-skip "
        "requests are gated here as native-pixel-neutral. If this passes, room "
        "124 should not be treated as the current pad10092 pixel-parity blocker."
    ),
    "variants": rows,
}
summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")

if failures:
    print("FAIL: pad10092 room draw isolation failed")
    for failure in failures:
        print(f"  - {failure}")
    raise SystemExit(1)

print("PASS: pad10092 room draw isolation")
for row in rows:
    print(
        "  {name}: native_changed={changed:.3f}% rooms={rooms} draw={draw}".format(
            name=row["name"],
            changed=row["native_default_vs_variant_changed_pct"],
            rooms=row["variant_rooms"].get("sample"),
            draw=row["variant_rooms"].get("draw_sample"),
        )
    )
print(f"  summary={summary_path}")
PY

echo "PASS: Dam pad10092 room draw isolation is guarded"
echo "artifacts: $OUT_DIR"
