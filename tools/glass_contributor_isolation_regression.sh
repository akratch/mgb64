#!/bin/bash
#
# glass_contributor_isolation_regression.sh -- Native visual ownership A/Bs for
# Dam regular-glass fixtures.
#
# This is a diagnostic ownership harness, not a final stock/native pixel gate.
# It first runs a stock-backed visual isolation gate, then captures native-only
# variants against the same route and compares each variant to the default native
# screenshot. The goal is to separate falling shards from impact, weapon/HUD,
# fog, and presentation contributors before changing renderer code.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
ARES_BIN=""
DO_BUILD=1
TIMEOUT_SECONDS=240
OUT_DIR="/tmp/mgb64_glass_contributor_isolation_$$"
VARIANTS=()
FIXTURE="active"
BASE_CASE_DIR=""
WORLD_IMPACT_CC=""

ROUTE="dam_regular_glass_shatter_rng_visual_probe"

usage() {
    cat <<'USAGE'
Usage: tools/glass_contributor_isolation_regression.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary path (default: build/ge007)
  --build-dir DIR      CMake build directory (default: build)
  --ares-bin PATH      instrumented ares binary
  --no-build           reuse an existing native binary
  --timeout SECONDS    route timeout (default: 240)
  --fixture NAME       fixture to probe: active, impact, or pad10092-impact
                       (default: active)
  --base-case-dir DIR  reuse an existing passing stock-backed case directory
  --world-impact-cc CC combiner id used by world-impact material diagnostics
                       (default: fixture-specific)
  --variant NAME       run one variant; may be repeated

Known variants:
  shards_off           GE007_GLASS_SHARDS=0
  bullet_impacts_off   GE007_DISABLE_BULLET_IMPACTS=1
  prop_impacts_off     GE007_DISABLE_PROP_BULLET_IMPACTS=1
  weapon_render_off    GE007_SKIP_FP_WEAPON_RENDER=1
  weapon_projection_off GE007_SKIP_FP_WEAPON_PROJECTION=1
  no_fog               GE007_NO_FOG=1
  flat_bullet_impacts  GE007_FLAT_BULLET_IMPACTS=1
  flat_prop_impacts    GE007_FLAT_PROP_BULLET_IMPACTS=1
  zmode_dec_less       GE007_DIAG_ZMODE_DEC_LESS=1
  zmode_dec_no_offset  GE007_DIAG_ZMODE_DEC_NO_POLY_OFFSET=1
  alpha_blend_premult  GE007_DIAG_ALPHA_BLEND=premult
  alpha_blend_add      GE007_DIAG_ALPHA_BLEND=add
  alpha_blend_inv      GE007_DIAG_ALPHA_BLEND=inv_alpha
  alpha_blend_copy     GE007_DIAG_ALPHA_BLEND=copy
  bullet_impact_inv_vis_scale_off
                       GE007_BULLET_IMPACT_INV_VIS_SCALE=0
  fixed_room_mtx       GE007_FIXED_ROOM_MTX=1
  world_impact_loaded_tile_2tex_filter
                       GE007_DIAG_LOADED_TILE_2TEX_N64_FILTER=<world impact CC>
  world_impact_alpha_from_intensity
                       GE007_DIAG_ALPHA_FROM_TEX_INTENSITY_CC=<world impact CC>
  world_impact_alpha_from_intensity_mix50
                       GE007_DIAG_ALPHA_FROM_TEX_INTENSITY_CC=<world impact CC>
                       GE007_DIAG_ALPHA_FROM_TEX_INTENSITY_MIX=0.5
  world_impact_xlu_coverage_wrap_thin
                       GE007_DIAG_XLU_COVERAGE_WRAP_THIN_CC=<world impact CC>
  world_impact_xlu_coverage_stencil
                       GE007_DIAG_XLU_COVERAGE_STENCIL_CC=<world impact CC>
  world_impact_rdp_memory
                       GE007_DIAG_XLU_RDP_MEMORY_BLEND_CC=<world impact CC>
  world_impact_rdp_cvg_memory
                       GE007_DIAG_XLU_RDP_CVG_MEMORY_BLEND_CC=<world impact CC>

With no --variant arguments, the default diagnostic set is:
  shards_off, bullet_impacts_off, weapon_render_off, no_fog, flat_bullet_impacts

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
        --ares-bin) ARES_BIN="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        --fixture) FIXTURE="$2"; shift 2 ;;
        --base-case-dir) BASE_CASE_DIR="$2"; shift 2 ;;
        --world-impact-cc) WORLD_IMPACT_CC="$2"; shift 2 ;;
        --variant) VARIANTS+=("$2"); shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ ! "$TIMEOUT_SECONDS" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --timeout must be a positive integer: $TIMEOUT_SECONDS" >&2
    exit 2
fi

if [[ "${#VARIANTS[@]}" -eq 0 ]]; then
    VARIANTS=(shards_off bullet_impacts_off weapon_render_off no_fog flat_bullet_impacts)
fi

case "$FIXTURE" in
    active)
        ROUTE="dam_regular_glass_shatter_rng_visual_probe"
        BASELINE_SCRIPT="tools/glass_active_visual_isolation_regression.sh"
        DEFAULT_SUBDIR="active_rng_visual"
        DEFAULT_SUMMARY_NAME="glass_active_visual_isolation_summary.json"
        DEFAULT_STOCK_COMPARE_NAME="visual_compare_${ROUTE}.json"
        DEFAULT_WORLD_IMPACT_CC="0x00f39e4f1f39e4f1"
        ;;
    impact)
        ROUTE="dam_regular_glass_shatter_rng_impact_visual_probe"
        BASELINE_SCRIPT="tools/glass_impact_visual_isolation_regression.sh"
        DEFAULT_SUBDIR="impact_visual"
        DEFAULT_SUMMARY_NAME="glass_impact_visual_isolation_summary.json"
        DEFAULT_STOCK_COMPARE_NAME="visual_compare_${ROUTE}.json"
        DEFAULT_WORLD_IMPACT_CC="0x00f39e4f1f39e4f1"
        ;;
    pad10092-impact)
        ROUTE="dam_regular_glass_shatter_pad10092_impact_visual_probe"
        BASELINE_SCRIPT="tools/glass_pad10092_impact_visual_regression.sh"
        DEFAULT_SUBDIR="pad10092_impact"
        DEFAULT_SUMMARY_NAME="glass_pad10092_impact_visual_summary.json"
        DEFAULT_STOCK_COMPARE_NAME="actor_masked_visual_compare_${ROUTE}.json"
        DEFAULT_WORLD_IMPACT_CC="0x00f38e4f020a2d12"
        ;;
    *)
        echo "FAIL: --fixture must be active, impact, or pad10092-impact: $FIXTURE" >&2
        exit 2
        ;;
esac
if [[ -z "$WORLD_IMPACT_CC" ]]; then
    WORLD_IMPACT_CC="$DEFAULT_WORLD_IMPACT_CC"
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
validation_require_binary "$ARES_BIN"

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
    if [[ -s "$dir/$DEFAULT_SUBDIR/stock_${ROUTE}.jsonl" ]]; then
        printf '%s\n' "$dir/$DEFAULT_SUBDIR"
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

DEFAULT_ROOT="$OUT_DIR/default"
if [[ -n "$BASE_CASE_DIR" ]]; then
    if ! DEFAULT_CASE="$(resolve_base_case_dir "$BASE_CASE_DIR")"; then
        echo "FAIL: --base-case-dir does not contain stock/native traces for $ROUTE: $BASE_CASE_DIR" >&2
        exit 2
    fi
else
    DEFAULT_CASE="$DEFAULT_ROOT/$DEFAULT_SUBDIR"
fi
ROUTE_JSON="tools/rom_oracle_routes/${ROUTE}.json"

echo "=== Glass Contributor Isolation Regression ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  ares:    $ARES_BIN"
echo "  fixture: $FIXTURE"
echo "  route:   $ROUTE"
echo "  base:    $DEFAULT_CASE"
echo "  world impact CC: $WORLD_IMPACT_CC"
echo "  variants: ${VARIANTS[*]}"

DEFAULT_SUMMARY="$DEFAULT_CASE/$DEFAULT_SUMMARY_NAME"
if [[ -n "$BASE_CASE_DIR" ]]; then
    validation_require_file "$DEFAULT_SUMMARY" "base case summary"
    python3 - "$DEFAULT_SUMMARY" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
payload = json.loads(path.read_text(encoding="utf-8"))
if payload.get("status") != "pass":
    raise SystemExit(f"FAIL: base case summary did not pass: {path}")
PY
elif [[ -f "$DEFAULT_SUMMARY" ]] && python3 - "$DEFAULT_SUMMARY" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
try:
    payload = json.loads(path.read_text(encoding="utf-8"))
except Exception:
    raise SystemExit(1)
raise SystemExit(0 if payload.get("status") == "pass" else 1)
PY
then
    echo "=== default: reusing passing $FIXTURE visual isolation capture ==="
    echo "  summary: $DEFAULT_SUMMARY"
else
    rm -rf "$DEFAULT_ROOT"
    env \
        GE007_TRACE_GLASS_PROJECTION_ALL=1 \
        MGB64_ARES_TRACE_GLASS_PROJECTION_ALL=1 \
        "$BASELINE_SCRIPT" \
            --out-dir "$DEFAULT_ROOT" \
            --rom "$ROM" \
            --binary "$BINARY" \
            --ares-bin "$ARES_BIN" \
            --no-build \
            --timeout "$TIMEOUT_SECONDS"
fi

DEFAULT_NATIVE_IMAGE="$DEFAULT_CASE/native_${ROUTE}.bmp"
DEFAULT_NATIVE_TRACE="$DEFAULT_CASE/native_${ROUTE}.jsonl"
DEFAULT_STOCK_IMAGE="$DEFAULT_CASE/stock_${ROUTE}.ppm"
DEFAULT_STOCK_COMPARE="$DEFAULT_CASE/$DEFAULT_STOCK_COMPARE_NAME"
validation_require_file "$DEFAULT_NATIVE_IMAGE" "default native screenshot"
validation_require_file "$DEFAULT_NATIVE_TRACE" "default native trace"
validation_require_file "$DEFAULT_STOCK_IMAGE" "default stock screenshot"
validation_require_file "$DEFAULT_STOCK_COMPARE" "default stock/native visual comparison"

ROUTE_REGION_ARGS=()
ROUTE_REGION_NAMES=()
while IFS= read -r line; do
    [[ -n "$line" ]] || continue
    ROUTE_REGION_ARGS+=(--region "$line")
    ROUTE_REGION_NAMES+=("${line%%:*}")
done < <(python3 tools/rom_oracle_route.py visual-regions "$ROUTE_JSON")

STOCK_COMPARE_ARGS=()
while IFS= read -r line; do
    [[ -n "$line" ]] || continue
    STOCK_COMPARE_ARGS+=("$line")
done < <(python3 tools/rom_oracle_route.py visual-logical-args "$ROUTE_JSON")
for line in "${ROUTE_REGION_ARGS[@]}"; do
    STOCK_COMPARE_ARGS+=("$line")
done
while IFS= read -r line; do
    [[ -n "$line" ]] || continue
    STOCK_COMPARE_ARGS+=(--exclude-region "$line")
done < <(python3 tools/rom_oracle_route.py visual-exclude-regions "$ROUTE_JSON")

variant_env() {
    local variant="$1"
    case "$variant" in
        shards_off) printf '%s\n' "GE007_GLASS_SHARDS=0" ;;
        bullet_impacts_off) printf '%s\n' "GE007_DISABLE_BULLET_IMPACTS=1" ;;
        prop_impacts_off) printf '%s\n' "GE007_DISABLE_PROP_BULLET_IMPACTS=1" ;;
        weapon_render_off) printf '%s\n' "GE007_SKIP_FP_WEAPON_RENDER=1" ;;
        weapon_projection_off) printf '%s\n' "GE007_SKIP_FP_WEAPON_PROJECTION=1" ;;
        no_fog) printf '%s\n' "GE007_NO_FOG=1" ;;
        flat_bullet_impacts) printf '%s\n' "GE007_FLAT_BULLET_IMPACTS=1" ;;
        flat_prop_impacts) printf '%s\n' "GE007_FLAT_PROP_BULLET_IMPACTS=1" ;;
        zmode_dec_less) printf '%s\n' "GE007_DIAG_ZMODE_DEC_LESS=1" ;;
        zmode_dec_no_offset) printf '%s\n' "GE007_DIAG_ZMODE_DEC_NO_POLY_OFFSET=1" ;;
        alpha_blend_premult) printf '%s\n' "GE007_DIAG_ALPHA_BLEND=premult" ;;
        alpha_blend_add) printf '%s\n' "GE007_DIAG_ALPHA_BLEND=add" ;;
        alpha_blend_inv) printf '%s\n' "GE007_DIAG_ALPHA_BLEND=inv_alpha" ;;
        alpha_blend_copy) printf '%s\n' "GE007_DIAG_ALPHA_BLEND=copy" ;;
        bullet_impact_inv_vis_scale_off) printf '%s\n' "GE007_BULLET_IMPACT_INV_VIS_SCALE=0" ;;
        fixed_room_mtx) printf '%s\n' "GE007_FIXED_ROOM_MTX=1" ;;
        world_impact_loaded_tile_2tex_filter)
            printf '%s\n' "GE007_DIAG_LOADED_TILE_2TEX_N64_FILTER=$WORLD_IMPACT_CC"
            ;;
        world_impact_alpha_from_intensity)
            printf '%s\n' "GE007_DIAG_ALPHA_FROM_TEX_INTENSITY_CC=$WORLD_IMPACT_CC"
            ;;
        world_impact_alpha_from_intensity_mix50)
            printf '%s\n' "GE007_DIAG_ALPHA_FROM_TEX_INTENSITY_CC=$WORLD_IMPACT_CC"
            printf '%s\n' "GE007_DIAG_ALPHA_FROM_TEX_INTENSITY_MIX=0.5"
            ;;
        world_impact_xlu_coverage_wrap_thin)
            printf '%s\n' "GE007_DIAG_XLU_COVERAGE_WRAP_THIN_CC=$WORLD_IMPACT_CC"
            ;;
        world_impact_xlu_coverage_stencil)
            printf '%s\n' "GE007_DIAG_XLU_COVERAGE_STENCIL_CC=$WORLD_IMPACT_CC"
            ;;
        world_impact_rdp_memory)
            printf '%s\n' "GE007_DIAG_XLU_RDP_MEMORY_BLEND_CC=$WORLD_IMPACT_CC"
            ;;
        world_impact_rdp_cvg_memory)
            printf '%s\n' "GE007_DIAG_XLU_RDP_CVG_MEMORY_BLEND_CC=$WORLD_IMPACT_CC"
            ;;
        *)
            echo "FAIL: unknown variant: $variant" >&2
            exit 2
            ;;
    esac
}

VARIANT_DIRS=()
for variant in "${VARIANTS[@]}"; do
    CASE_DIR="$OUT_DIR/$variant"
    rm -rf "$CASE_DIR"
    mkdir -p "$CASE_DIR"
    VARIANT_DIRS+=("$CASE_DIR")

    EXTRA_ENV=()
    while IFS= read -r env_line; do
        EXTRA_ENV+=("$env_line")
    done < <(variant_env "$variant")
    printf '%s\n' "${EXTRA_ENV[@]}" > "$CASE_DIR/env.txt"

    echo "=== variant: $variant (${EXTRA_ENV[*]}) ==="
    env \
        GE007_TRACE_GLASS_PROJECTION_ALL=1 \
        "${EXTRA_ENV[@]}" \
        tools/movement_oracle_capture.sh \
            --route "$ROUTE" \
            --native-only \
            --no-compare \
            --out-dir "$CASE_DIR" \
            --rom "$ROM" \
            --binary "$BINARY" \
            --no-build \
            --timeout "$TIMEOUT_SECONDS"

    GFXDL_MATCHES="$CASE_DIR/gfxdl_matches.txt"
    if grep -H -F "[GFX-DL]" "$CASE_DIR"/*.log > "$GFXDL_MATCHES"; then
        echo "FAIL: [GFX-DL] warning rows found for variant $variant" >&2
        head -20 "$GFXDL_MATCHES" | sed 's/^/  /' >&2
        exit 1
    fi
    rm -f "$GFXDL_MATCHES"

    VARIANT_NATIVE_IMAGE="$CASE_DIR/native_${ROUTE}.bmp"
    VARIANT_NATIVE_TRACE="$CASE_DIR/native_${ROUTE}.jsonl"
    validation_require_file "$VARIANT_NATIVE_IMAGE" "$variant native screenshot"
    validation_require_file "$VARIANT_NATIVE_TRACE" "$variant native trace"

    python3 tools/compare_screenshots.py \
        "$DEFAULT_NATIVE_IMAGE" \
        "$VARIANT_NATIVE_IMAGE" \
        --logical-size 320,240 \
        --logical-viewport 0,10,320,220 \
        --baseline-logical-frame full \
        --test-logical-frame full \
        "${ROUTE_REGION_ARGS[@]}" \
        --json-out "$CASE_DIR/default_vs_${variant}_visual.json"

    python3 tools/compare_screenshots.py \
        "${STOCK_COMPARE_ARGS[@]}" \
        "$DEFAULT_STOCK_IMAGE" \
        "$VARIANT_NATIVE_IMAGE" \
        --json-out "$CASE_DIR/stock_vs_${variant}_visual.json" \
        >"$CASE_DIR/stock_vs_${variant}_visual.txt"

    if [[ "$variant" == "shards_off" ]]; then
        python3 tools/compare_glass_shard_pixel_oracle.py \
            --baseline-trace "$DEFAULT_NATIVE_TRACE" \
            --test-trace "$VARIANT_NATIVE_TRACE" \
            --baseline-image "$DEFAULT_NATIVE_IMAGE" \
            --test-image "$VARIANT_NATIVE_IMAGE" \
            --logical-size 320,240 \
            --logical-viewport 0,10,320,220 \
            --baseline-logical-frame full \
            --test-logical-frame full \
            --json-out "$CASE_DIR/default_vs_${variant}_shard_pixel_oracle.json"
    fi
done

python3 - "$OUT_DIR" "$ROUTE" "$WORLD_IMPACT_CC" "$DEFAULT_SUMMARY" "$DEFAULT_STOCK_COMPARE" "${VARIANTS[@]}" <<'PY'
import json
import sys
from pathlib import Path
from typing import Any

root = Path(sys.argv[1])
route = sys.argv[2]
world_impact_cc = sys.argv[3]
default_summary_path = Path(sys.argv[4])
default_stock_compare_path = Path(sys.argv[5])
variants = sys.argv[6:]
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


def bright_pair(region: dict[str, Any] | None) -> list[int | None]:
    if not region:
        return [None, None]
    features = region.get("features")
    if not isinstance(features, dict):
        features = (region.get("full") or {}).get("features", {})
    baseline = features.get("baseline", {}).get("bright_pixels")
    test = features.get("test", {}).get("bright_pixels")
    return [baseline, test]


def compare_changed_pct(compare: dict[str, Any]) -> float | None:
    value = compare.get("changed_pct")
    if not isinstance(value, (int, float)):
        value = (compare.get("full") or {}).get("changed_pct")
    return float(value) if isinstance(value, (int, float)) else None


def numeric(value: Any) -> float | None:
    return float(value) if isinstance(value, (int, float)) else None


def delta(default_value: float | None, variant_value: float | None) -> float | None:
    if default_value is None or variant_value is None:
        return None
    return variant_value - default_value


def improvement(default_value: float | None, variant_value: float | None) -> float | None:
    change = delta(default_value, variant_value)
    return None if change is None else -change


default_summary = load_json(default_summary_path)
if default_summary.get("status") != "pass":
    failures.append("default stock-backed visual isolation did not pass")

default_stock_compare = load_json(default_stock_compare_path)
default_stock_regions = region_map(default_stock_compare)
preferred_regions = (
    "glass_burst",
    "tower_pane",
    "projected_impact",
    "impact_side",
    "damage_arc",
    "hud_viewmodel",
)
primary_region = next(
    (name for name in preferred_regions if name in default_stock_regions),
    next(iter(default_stock_regions), None),
)
if default_stock_compare.get("status") != "pass":
    failures.append("default stock/native visual comparison did not pass")

variant_payloads: dict[str, Any] = {}
for variant in variants:
    variant_dir = root / variant
    capture = load_json(variant_dir / f"summary_{route}.json")
    compare = load_json(variant_dir / f"default_vs_{variant}_visual.json")
    stock_compare = load_json(variant_dir / f"stock_vs_{variant}_visual.json")
    regions = region_map(compare)
    stock_regions = region_map(stock_compare)
    region_names = sorted(set(default_stock_regions) | set(regions) | set(stock_regions))
    shard_oracle = {}
    if variant == "shards_off":
        shard_oracle = load_json(variant_dir / f"default_vs_{variant}_shard_pixel_oracle.json")

    if capture.get("status") != "pass":
        failures.append(f"{variant}: native capture status is not pass")
    if compare.get("status") != "pass":
        failures.append(f"{variant}: visual comparison status is not pass")
    if stock_compare.get("status") != "pass":
        failures.append(f"{variant}: stock comparison status is not pass")
    if shard_oracle and shard_oracle.get("status") != "pass":
        failures.append(f"{variant}: shard pixel oracle status is not pass")

    variant_payloads[variant] = {
        "env": (variant_dir / "env.txt").read_text(encoding="utf-8").splitlines()
        if (variant_dir / "env.txt").exists()
        else [],
        "capture_summary": str(variant_dir / f"summary_{route}.json"),
        "visual_compare": str(variant_dir / f"default_vs_{variant}_visual.json"),
        "stock_visual_compare": str(variant_dir / f"stock_vs_{variant}_visual.json"),
        "changed_pixels": compare.get("changed_pixels"),
        "changed_pct": compare.get("changed_pct"),
        "identical_pct": compare.get("identical_pct"),
        "mean_rgb": compare.get("mean_rgb"),
        "features": compare.get("features"),
        "stock": {
            "default_changed_pct": compare_changed_pct(default_stock_compare),
            "variant_changed_pct": compare_changed_pct(stock_compare),
            "changed_delta_pct": delta(
                compare_changed_pct(default_stock_compare),
                compare_changed_pct(stock_compare),
            ),
            "improvement_pct": improvement(
                compare_changed_pct(default_stock_compare),
                compare_changed_pct(stock_compare),
            ),
            "regions": {
                name: {
                    "default_changed_pct": changed_pct(default_stock_regions.get(name)),
                    "variant_changed_pct": changed_pct(stock_regions.get(name)),
                    "changed_delta_pct": delta(
                        changed_pct(default_stock_regions.get(name)),
                        changed_pct(stock_regions.get(name)),
                    ),
                    "improvement_pct": improvement(
                        changed_pct(default_stock_regions.get(name)),
                        changed_pct(stock_regions.get(name)),
                    ),
                    "default_bright": bright_pair(default_stock_regions.get(name)),
                    "variant_bright": bright_pair(stock_regions.get(name)),
                }
                for name in region_names
            },
        },
        "regions": {
            name: {
                "changed_pct": changed_pct(regions.get(name)),
                "bright": bright_pair(regions.get(name)),
                "warm": [
                    regions.get(name, {})
                    .get("features", {})
                    .get("baseline", {})
                    .get("warm_pixels"),
                    regions.get(name, {})
                    .get("features", {})
                    .get("test", {})
                    .get("warm_pixels"),
                ],
            }
            for name in region_names
        },
        "shard_pixel_oracle": {
            "path": str(variant_dir / f"default_vs_{variant}_shard_pixel_oracle.json")
            if shard_oracle
            else None,
            "sample": shard_oracle.get("sample") if shard_oracle else None,
            "coverage": shard_oracle.get("coverage") if shard_oracle else None,
            "union_changed_pct": shard_oracle.get("union_metrics", {}).get("changed_pct")
            if shard_oracle
            else None,
            "union_abs_rgb_delta_mean": shard_oracle.get("union_metrics", {})
            .get("abs_rgb_delta", {})
            .get("mean")
            if shard_oracle
            else None,
        },
    }

ranked = sorted(
    variant_payloads,
    key=lambda name: float(variant_payloads[name].get("changed_pct") or 0.0),
    reverse=True,
)

payload = {
    "status": "fail" if failures else "pass",
    "failures": failures,
    "route": route,
    "world_impact_cc": world_impact_cc,
    "default_summary": str(default_summary_path),
    "default_stock_compare": str(default_stock_compare_path),
    "primary_region": primary_region,
    "variants_ranked_by_changed_pct": ranked,
    "variants": variant_payloads,
    "interpretation": {
        "shards_off": (
            "If changed_pct and shard_pixel_oracle union_changed_pct are zero, "
            "the projected shard masks are not causal proof of falling-shard framebuffer ownership."
        ),
        "bullet_impacts_off": (
            "Large movement here proves bullet-impact ownership somewhere in the frame; "
            "stock.improvement_pct decides whether the variant is an improvement versus stock."
        ),
        "weapon_render_off": (
            "Movement concentrated in hud_viewmodel marks first-person weapon/HUD ownership."
        ),
    },
}

summary_path = root / "glass_contributor_isolation_summary.json"
summary_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

print("=== glass contributor isolation summary ===")
print(f"  status={payload['status']}")
for name in ranked:
    item = variant_payloads[name]
    stock = item["stock"]
    primary_delta = None
    if primary_region is not None:
        primary_delta = stock["regions"].get(primary_region, {}).get("changed_delta_pct")
    region_text = " ".join(
        f"{region}={item['regions'].get(region, {}).get('changed_pct')}"
        for region in sorted(item["regions"])
    )
    print(
        "  {name}: changed={changed:.3f}% "
        "stock_delta={stock_delta:+.3f}% stock_{primary}_delta={primary_delta:+.3f}% "
        "{regions}".format(
            name=name,
            changed=float(item.get("changed_pct") or 0.0),
            stock_delta=float(stock.get("changed_delta_pct") or 0.0),
            primary=primary_region or "region",
            primary_delta=float(primary_delta or 0.0),
            regions=region_text,
        )
    )
if failures:
    for failure in failures:
        print(f"  failure: {failure}", file=sys.stderr)
    raise SystemExit(1)
print(f"  summary={summary_path}")
PY

echo "PASS: glass contributor isolation diagnostics completed"
echo "artifacts: $OUT_DIR"
