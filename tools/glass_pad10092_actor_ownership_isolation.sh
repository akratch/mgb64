#!/bin/bash
#
# glass_pad10092_actor_ownership_isolation.sh -- Measure whether chr7/chr44
# own the remaining pad10092 stock/native glass screenshot mismatch.
#
# This is a diagnostic guard, not a production rendering fix. It reuses the
# stock/default pad10092 impact fixture, then runs native-only variants with
# selected chr draw calls skipped. Route state must remain clean; only native
# pixels should move.
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
OUT_DIR="/tmp/mgb64_glass_pad10092_actor_ownership_isolation_$$"
BASE_CASE_DIR=""

usage() {
    cat <<'USAGE'
Usage: tools/glass_pad10092_actor_ownership_isolation.sh [options]

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
SUMMARY_JSON="$OUT_DIR/glass_pad10092_actor_ownership_isolation_summary.json"
INDEX_TSV="$OUT_DIR/variants.tsv"
DEFAULT_VISUAL_JSON="$OUT_DIR/default_stock_vs_native_visual.json"
DEFAULT_VISUAL_HEATMAP="$OUT_DIR/default_stock_vs_native_visual.png"

validation_require_file "$STOCK_TRACE" "stock trace"
validation_require_file "$NATIVE_TRACE" "native default trace"
validation_require_file "$STOCK_SCREENSHOT" "stock screenshot"
validation_require_file "$NATIVE_SCREENSHOT" "native default screenshot"

echo "=== Glass Pad10092 Actor Ownership Isolation ==="
echo "  out-dir:       $OUT_DIR"
echo "  base-case-dir: $BASE_CASE_DIR"
echo "  route:         $ROUTE"
echo "  binary:        $BINARY"
echo "  ROM:           $ROM"

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
    --heatmap "$DEFAULT_VISUAL_HEATMAP" \
    --json-out "$DEFAULT_VISUAL_JSON" \
    "$STOCK_SCREENSHOT" \
    "$NATIVE_SCREENSHOT" >/dev/null

printf 'name\tenv\tcase_dir\thealth_json\tframing_json\tglass_json\tprojection_json\tnative_delta_json\tvisual_json\n' >"$INDEX_TSV"

run_variant() {
    local name="$1"
    shift
    local env_pairs=("$@")
    local case_dir="$OUT_DIR/variants/$name"
    local native_trace="$case_dir/native_${ROUTE}.jsonl"
    local native_screenshot="$case_dir/native_${ROUTE}.bmp"
    local log="$case_dir/native_${ROUTE}.log"
    local health_json="$case_dir/health.json"
    local framing_json="$case_dir/framing.json"
    local glass_json="$case_dir/glass.json"
    local projection_json="$case_dir/projection.json"
    local native_delta_json="$case_dir/native_default_vs_${name}.json"
    local native_delta_heatmap="$case_dir/native_default_vs_${name}.png"
    local visual_json="$case_dir/stock_vs_${name}_visual.json"
    local visual_heatmap="$case_dir/stock_vs_${name}_visual.png"
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
        --region tower_pane:80,115,320,180 \
        --region impact_side:255,145,120,95 \
        --region projected_impact:173,156,20,19 \
        --region lower_actor_cluster:145,160,215,125 \
        --region hud_viewmodel:360,300,255,130 \
        --heatmap "$native_delta_heatmap" \
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
        --heatmap "$visual_heatmap" \
        --json-out "$visual_json" \
        "$STOCK_SCREENSHOT" \
        "$native_screenshot"

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$name" "$env_display" "$case_dir" "$health_json" "$framing_json" "$glass_json" "$projection_json" "$native_delta_json" "$visual_json" \
        >>"$INDEX_TSV"
}

run_variant "skip_chr7" "GE007_SKIP_RENDER_CHRNUMS=7"
run_variant "skip_chr44" "GE007_SKIP_RENDER_CHRNUMS=44"
run_variant "skip_chr7_44" "GE007_SKIP_RENDER_CHRNUMS=7,44"

python3 - "$INDEX_TSV" "$SUMMARY_JSON" "$BASE_CASE_DIR" "$ROUTE" "$DEFAULT_VISUAL_JSON" <<'PY'
import csv
import json
import sys
from pathlib import Path
from typing import Any

index_path = Path(sys.argv[1])
summary_path = Path(sys.argv[2])
base_case_dir = Path(sys.argv[3])
route = sys.argv[4]
default_visual_path = Path(sys.argv[5])

def load(path: str | Path) -> dict[str, Any]:
    return json.loads(Path(path).read_text(encoding="utf-8"))

def region(payload: dict[str, Any], name: str) -> dict[str, Any]:
    for item in payload.get("regions", []):
        if item.get("name") == name:
            return item
    return {}

def region_value(payload: dict[str, Any], name: str, key: str) -> Any:
    return region(payload, name).get(key)

def visual_region_value(payload: dict[str, Any], name: str, branch: str, key: str) -> Any:
    return (region(payload, name).get(branch) or {}).get(key)

def changed_pct(payload: dict[str, Any]) -> float:
    value = payload.get("changed_pct")
    return float(value) if isinstance(value, (int, float)) else 100.0

def status_ok(payload: dict[str, Any]) -> bool:
    return payload.get("status") == "pass"

default_visual = load(default_visual_path)
default_masked = (default_visual.get("masked") or {}).get("changed_pct")
default_full = default_visual.get("full", {}).get("changed_pct")
default_projected = visual_region_value(default_visual, "projected_impact", "masked", "changed_pct")

rows: list[dict[str, Any]] = []
failures: list[str] = []

with index_path.open("r", encoding="utf-8") as handle:
    for row in csv.DictReader(handle, delimiter="\t"):
        health = load(row["health_json"])
        framing = load(row["framing_json"])
        glass = load(row["glass_json"])
        projection = load(row["projection_json"])
        native_delta = load(row["native_delta_json"])
        visual = load(row["visual_json"])

        for label, payload in (
            ("health", health),
            ("framing", framing),
            ("glass", glass),
            ("projection", projection),
            ("native_delta", native_delta),
            ("visual", visual),
        ):
            if not status_ok(payload):
                failures.append(f"{row['name']}: {label} status is {payload.get('status')}")

        stock_masked = (visual.get("masked") or {}).get("changed_pct")
        stock_full = visual.get("full", {}).get("changed_pct")
        stock_projected = visual_region_value(visual, "projected_impact", "masked", "changed_pct")

        entry = {
            "name": row["name"],
            "env": row["env"],
            "case_dir": row["case_dir"],
            "native_default_vs_variant_changed_pct": changed_pct(native_delta),
            "native_default_vs_variant_regions": {
                "tower_pane": region_value(native_delta, "tower_pane", "changed_pct"),
                "impact_side": region_value(native_delta, "impact_side", "changed_pct"),
                "projected_impact": region_value(native_delta, "projected_impact", "changed_pct"),
                "lower_actor_cluster": region_value(native_delta, "lower_actor_cluster", "changed_pct"),
                "hud_viewmodel": region_value(native_delta, "hud_viewmodel", "changed_pct"),
            },
            "stock_vs_variant_full_changed_pct": stock_full,
            "stock_vs_variant_masked_changed_pct": stock_masked,
            "stock_vs_variant_projected_impact_changed_pct": stock_projected,
            "stock_vs_default_full_changed_pct": default_full,
            "stock_vs_default_masked_changed_pct": default_masked,
            "stock_vs_default_projected_impact_changed_pct": default_projected,
            "masked_delta_vs_default": (
                float(stock_masked) - float(default_masked)
                if isinstance(stock_masked, (int, float)) and isinstance(default_masked, (int, float))
                else None
            ),
            "projected_impact_delta_vs_default": (
                float(stock_projected) - float(default_projected)
                if isinstance(stock_projected, (int, float)) and isinstance(default_projected, (int, float))
                else None
            ),
            "framing_room_delta": (framing.get("deltas") or {}).get("room_set"),
            "glass_active_delta": glass.get("max_active_delta"),
            "projection_deltas": projection.get("deltas"),
        }
        rows.append(entry)

max_signal = max((row["native_default_vs_variant_changed_pct"] for row in rows), default=0.0)
if max_signal <= 0.01:
    failures.append("skip variants did not change native pixels; actor ownership probe produced no signal")

summary = {
    "status": "fail" if failures else "pass",
    "failures": failures,
    "route": route,
    "base_case_dir": str(base_case_dir),
    "default_visual": str(default_visual_path),
    "note": (
        "GE007_SKIP_RENDER_CHRNUMS is a native-only diagnostic draw skip. "
        "Passing route-state gates here means actor draw ownership can be "
        "interpreted without mixing in camera, health, glass lifecycle, or "
        "projection drift."
    ),
    "variants": rows,
    "strongest_native_pixel_signal": max_signal,
    "best_masked_vs_stock": min(
        rows,
        key=lambda item: (
            float(item["stock_vs_variant_masked_changed_pct"])
            if isinstance(item.get("stock_vs_variant_masked_changed_pct"), (int, float))
            else 1000.0
        ),
    ) if rows else None,
}
summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")

if failures:
    print("FAIL: pad10092 actor ownership isolation failed")
    for failure in failures:
        print(f"  - {failure}")
    raise SystemExit(1)

print("PASS: pad10092 actor ownership isolation")
for row in rows:
    print(
        "  {name}: native_changed={native:.3f}% masked_delta={masked_delta:+.3f}% "
        "projected_delta={projected_delta:+.3f}% lower_actor={actor}".format(
            name=row["name"],
            native=row["native_default_vs_variant_changed_pct"],
            masked_delta=float(row["masked_delta_vs_default"] or 0.0),
            projected_delta=float(row["projected_impact_delta_vs_default"] or 0.0),
            actor=row["native_default_vs_variant_regions"].get("lower_actor_cluster"),
        )
    )
print(f"  summary={summary_path}")
PY

echo "PASS: Dam pad10092 actor ownership isolation is guarded"
echo "artifacts: $OUT_DIR"
