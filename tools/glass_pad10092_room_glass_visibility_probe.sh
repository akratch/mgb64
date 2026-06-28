#!/bin/bash
#
# glass_pad10092_room_glass_visibility_probe.sh -- native-only room-glass
# material A/Bs for the Dam pad10092 impact route.
#
# This diagnostic reuses an existing stock-backed pad10092 impact case. It asks
# whether room-glass material controls move the same broad dirty visual regions,
# and whether any movement is stock-directed. It is not a renderer fix.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=300
OUT_DIR="/tmp/mgb64_glass_pad10092_room_glass_visibility_$$"
BASE_CASE_DIR="/tmp/mgb64_glass_pad10092_impact_visual_sequence_clean/pad10092_impact"
VARIANTS=()
ROOM_GLASS_CC="0x00738e4f020a2d12"

ROUTE="dam_regular_glass_shatter_pad10092_impact_visual_probe"
ROUTE_JSON="tools/rom_oracle_routes/${ROUTE}.json"

usage() {
    cat <<'USAGE'
Usage: tools/glass_pad10092_room_glass_visibility_probe.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --base-case-dir DIR  existing stock-backed pad10092 impact case directory
                       (default: /tmp/mgb64_glass_pad10092_impact_visual_sequence_clean/pad10092_impact)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary path (default: build/ge007)
  --build-dir DIR      CMake build directory (default: build)
  --no-build           reuse an existing native binary
  --timeout SECONDS    route timeout (default: 300)
  --variant NAME       run one variant; may be repeated

Known variants:
  room_alpha_env_half       GE007_DIAG_ROOM_ALPHA_ENV_SCALE=0.5
  room_alpha_env_onehalf    GE007_DIAG_ROOM_ALPHA_ENV_SCALE=1.5
  room_alpha_texedge        GE007_ROOM_ALPHA_AS_TEXEDGE=1
  room_xlu_opaque           GE007_ROOM_XLU_AS_OPAQUE=1
  room_point_filter         GE007_FORCE_ROOM_POINT_FILTER=1
  disable_n64_filter        GE007_DISABLE_N64_FILTER=1
  no_fog                    GE007_NO_FOG=1
  alpha_blend_premult       GE007_DIAG_ALPHA_BLEND=premult
  alpha_blend_add           GE007_DIAG_ALPHA_BLEND=add
  alpha_blend_copy          GE007_DIAG_ALPHA_BLEND=copy
  alpha_blend_inv_alpha     GE007_DIAG_ALPHA_BLEND=inv_alpha
  noperspective_settex_texcoords
                            noperspective texcoords for the room-glass combiner
  noperspective_settex_inputs
                            noperspective shader inputs for the room-glass combiner
  noperspective_settex_fog  noperspective fog for the room-glass combiner
  settex_disable_n64_filter disable shader N64 filtering for the room-glass combiner
  settex_always_3point      force shader 3-point filtering for the room-glass combiner
  settex_disable_clamped_3point
                            disable the default clamped settex 3-point policy
  settex_color_scale_095    scale room-glass combiner RGB by 0.95
  settex_color_scale_105    scale room-glass combiner RGB by 1.05
  settex_alpha_scale_081    scale room-glass combiner alpha by 0.81
  settex_alpha_scale_125    scale room-glass combiner alpha by 1.25
  convert_k4k5              convert K4/K5 combiner constants for target combiner A/B
  skip_room_glass_tex654     skip G_SETTEX texture 654 to expose the framebuffer behind the target room glass
  xlu_rdp_memory_blend      shader-side RDP memory blend for room-glass combiner
  xlu_rdp_cvg_memory_blend  shader-side RDP coverage+memory blend for room-glass combiner

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, emulator output, or generated summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --base-case-dir) BASE_CASE_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
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
    VARIANTS=(
        room_alpha_env_half
        room_alpha_env_onehalf
        room_alpha_texedge
        room_xlu_opaque
        room_point_filter
        disable_n64_filter
    )
fi

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
else
    BINARY="$(validation_resolve_path "$BINARY")"
fi
ROM="$(validation_resolve_path "$ROM")"
BASE_CASE_DIR="$(validation_resolve_path "$BASE_CASE_DIR")"

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"
validation_require_file "$ROUTE_JSON" "route JSON"
validation_require_file "$BASE_CASE_DIR/stock_${ROUTE}.ppm" "base stock screenshot"
validation_require_file "$BASE_CASE_DIR/native_${ROUTE}.bmp" "base native screenshot"

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

ROUTE_REGION_ARGS=()
while IFS= read -r line; do
    [[ -n "$line" ]] || continue
    ROUTE_REGION_ARGS+=(--region "$line")
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

DEFAULT_NATIVE_IMAGE="$BASE_CASE_DIR/native_${ROUTE}.bmp"
STOCK_IMAGE="$BASE_CASE_DIR/stock_${ROUTE}.ppm"

variant_env() {
    local variant="$1"
    case "$variant" in
        room_alpha_env_half) printf '%s\n' "GE007_DIAG_ROOM_ALPHA_ENV_SCALE=0.5" ;;
        room_alpha_env_onehalf) printf '%s\n' "GE007_DIAG_ROOM_ALPHA_ENV_SCALE=1.5" ;;
        room_alpha_texedge) printf '%s\n' "GE007_ROOM_ALPHA_AS_TEXEDGE=1" ;;
        room_xlu_opaque) printf '%s\n' "GE007_ROOM_XLU_AS_OPAQUE=1" ;;
        room_point_filter) printf '%s\n' "GE007_FORCE_ROOM_POINT_FILTER=1" ;;
        disable_n64_filter) printf '%s\n' "GE007_DISABLE_N64_FILTER=1" ;;
        no_fog) printf '%s\n' "GE007_NO_FOG=1" ;;
        alpha_blend_premult) printf '%s\n' "GE007_DIAG_ALPHA_BLEND=premult" ;;
        alpha_blend_add) printf '%s\n' "GE007_DIAG_ALPHA_BLEND=add" ;;
        alpha_blend_copy) printf '%s\n' "GE007_DIAG_ALPHA_BLEND=copy" ;;
        alpha_blend_inv_alpha) printf '%s\n' "GE007_DIAG_ALPHA_BLEND=inv_alpha" ;;
        noperspective_settex_texcoords)
            printf '%s\n' "GE007_DIAG_NOPERSPECTIVE_SETTEX_CC=$ROOM_GLASS_CC"
            ;;
        noperspective_settex_inputs)
            printf '%s\n' "GE007_DIAG_NOPERSPECTIVE_SETTEX_CC_INPUTS=$ROOM_GLASS_CC"
            ;;
        noperspective_settex_fog)
            printf '%s\n' "GE007_DIAG_NOPERSPECTIVE_SETTEX_CC_FOG=$ROOM_GLASS_CC"
            ;;
        settex_disable_n64_filter)
            printf '%s\n' "GE007_DIAG_SETTEX_CC_DISABLE_N64_FILTER=$ROOM_GLASS_CC"
            ;;
        settex_always_3point)
            printf '%s\n' "GE007_DIAG_SETTEX_CC_N64_FILTER_ALWAYS_3POINT=$ROOM_GLASS_CC"
            ;;
        settex_disable_clamped_3point)
            printf '%s\n' "GE007_DIAG_SETTEX_CLAMPED_NON_TEXEDGE_N64_FILTER_ALWAYS_3POINT=0"
            ;;
        settex_color_scale_095)
            printf '%s\n' "GE007_DIAG_SETTEX_CC_COLOR_SCALE=$ROOM_GLASS_CC"
            printf '%s\n' "GE007_DIAG_SETTEX_CC_COLOR_SCALE_TEXSIZE=54x54"
            printf '%s\n' "GE007_DIAG_SETTEX_CC_COLOR_SCALE_VALUE=0.95"
            ;;
        settex_color_scale_105)
            printf '%s\n' "GE007_DIAG_SETTEX_CC_COLOR_SCALE=$ROOM_GLASS_CC"
            printf '%s\n' "GE007_DIAG_SETTEX_CC_COLOR_SCALE_TEXSIZE=54x54"
            printf '%s\n' "GE007_DIAG_SETTEX_CC_COLOR_SCALE_VALUE=1.05"
            ;;
        settex_alpha_scale_081)
            printf '%s\n' "GE007_DIAG_SETTEX_CC_ALPHA_SCALE=$ROOM_GLASS_CC"
            printf '%s\n' "GE007_DIAG_SETTEX_CC_ALPHA_SCALE_TEXSIZE=54x54"
            printf '%s\n' "GE007_DIAG_SETTEX_CC_ALPHA_SCALE_TEXNUM=654"
            printf '%s\n' "GE007_DIAG_SETTEX_CC_ALPHA_SCALE_VALUE=0.81"
            ;;
        settex_alpha_scale_125)
            printf '%s\n' "GE007_DIAG_SETTEX_CC_ALPHA_SCALE=$ROOM_GLASS_CC"
            printf '%s\n' "GE007_DIAG_SETTEX_CC_ALPHA_SCALE_TEXSIZE=54x54"
            printf '%s\n' "GE007_DIAG_SETTEX_CC_ALPHA_SCALE_TEXNUM=654"
            printf '%s\n' "GE007_DIAG_SETTEX_CC_ALPHA_SCALE_VALUE=1.25"
            ;;
        convert_k4k5)
            printf '%s\n' "GE007_DIAG_CONVERT_K4K5=1"
            ;;
        skip_room_glass_tex654)
            printf '%s\n' "GE007_SKIP_TEX=654"
            ;;
        xlu_rdp_memory_blend)
            printf '%s\n' "GE007_DIAG_XLU_RDP_MEMORY_BLEND_CC=$ROOM_GLASS_CC"
            ;;
        xlu_rdp_cvg_memory_blend)
            printf '%s\n' "GE007_DIAG_XLU_RDP_CVG_MEMORY_BLEND_CC=$ROOM_GLASS_CC"
            ;;
        *)
            echo "FAIL: unknown variant: $variant" >&2
            exit 2
            ;;
    esac
}

echo "=== Glass Pad10092 Room Glass Visibility Probe ==="
echo "  out-dir: $OUT_DIR"
echo "  base:    $BASE_CASE_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  route:   $ROUTE"
echo "  variants: ${VARIANTS[*]}"

mkdir -p "$OUT_DIR/default"
python3 tools/compare_screenshots.py \
    "${STOCK_COMPARE_ARGS[@]}" \
    "$STOCK_IMAGE" \
    "$DEFAULT_NATIVE_IMAGE" \
    --json-out "$OUT_DIR/default/stock_vs_default_visual.json" \
    > "$OUT_DIR/default/stock_vs_default_visual.txt"

python3 tools/compare_roi_pixel_semantics.py \
    --route-json "$ROUTE_JSON" \
    --base-case-dir "$BASE_CASE_DIR" \
    --stock-image "$STOCK_IMAGE" \
    --native-image "$DEFAULT_NATIVE_IMAGE" \
    --json-out "$OUT_DIR/default/roi_pixel_semantics.json" \
    > "$OUT_DIR/default/roi_pixel_semantics.txt"

for variant in "${VARIANTS[@]}"; do
    CASE_DIR="$OUT_DIR/$variant"
    rm -rf "$CASE_DIR"
    mkdir -p "$CASE_DIR"

    EXTRA_ENV=()
    while IFS= read -r env_line; do
        EXTRA_ENV+=("$env_line")
    done < <(variant_env "$variant")
    printf '%s\n' "${EXTRA_ENV[@]}" > "$CASE_DIR/env.txt"

    echo "=== variant: $variant (${EXTRA_ENV[*]}) ==="
    env \
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
        echo "FAIL: [GFX-DL] warning rows found for $variant" >&2
        head -20 "$GFXDL_MATCHES" | sed 's/^/  /' >&2
        exit 1
    fi
    rm -f "$GFXDL_MATCHES"

    VARIANT_IMAGE="$CASE_DIR/native_${ROUTE}.bmp"
    validation_require_file "$VARIANT_IMAGE" "$variant native screenshot"

    python3 tools/compare_screenshots.py \
        "$DEFAULT_NATIVE_IMAGE" \
        "$VARIANT_IMAGE" \
        --logical-size 320,240 \
        --logical-viewport 0,10,320,220 \
        --baseline-logical-frame full \
        --test-logical-frame full \
        "${ROUTE_REGION_ARGS[@]}" \
        --json-out "$CASE_DIR/default_vs_${variant}_visual.json" \
        > "$CASE_DIR/default_vs_${variant}_visual.txt"

    python3 tools/compare_screenshots.py \
        "${STOCK_COMPARE_ARGS[@]}" \
        "$STOCK_IMAGE" \
        "$VARIANT_IMAGE" \
        --json-out "$CASE_DIR/stock_vs_${variant}_visual.json" \
        > "$CASE_DIR/stock_vs_${variant}_visual.txt"

    python3 tools/compare_control_footprint_visual.py \
        --route-json "$ROUTE_JSON" \
        --base-case-dir "$BASE_CASE_DIR" \
        --variant-image "$VARIANT_IMAGE" \
        --variant-label "$variant" \
        --heatmap "$CASE_DIR/control_footprint_heatmap.png" \
        --json-out "$CASE_DIR/control_footprint_visual.json" \
        > "$CASE_DIR/control_footprint_visual.txt"

    python3 tools/compare_roi_pixel_semantics.py \
        --route-json "$ROUTE_JSON" \
        --base-case-dir "$BASE_CASE_DIR" \
        --stock-image "$STOCK_IMAGE" \
        --native-image "$VARIANT_IMAGE" \
        --json-out "$CASE_DIR/roi_pixel_semantics.json" \
        > "$CASE_DIR/roi_pixel_semantics.txt"
done

python3 - "$OUT_DIR" "$ROUTE" "$ROOM_GLASS_CC" "${VARIANTS[@]}" <<'PY'
import json
import sys
from pathlib import Path
from typing import Any

root = Path(sys.argv[1])
route = sys.argv[2]
room_glass_cc = sys.argv[3]
variants = sys.argv[4:]
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


def changed_pct(region_or_compare: dict[str, Any] | None) -> float | None:
    if not region_or_compare:
        return None
    value = region_or_compare.get("changed_pct")
    if not isinstance(value, (int, float)):
        value = (region_or_compare.get("full") or {}).get("changed_pct")
    return float(value) if isinstance(value, (int, float)) else None


def region_changed(compare: dict[str, Any], name: str) -> float | None:
    return changed_pct(region_map(compare).get(name))


def roi_region_metrics(semantics: dict[str, Any], name: str) -> dict[str, Any]:
    region = (semantics.get("regions") or {}).get(name) or {}
    unmasked = region.get("unmasked") or {}
    route_masked = region.get("route_masked") or {}
    delta = unmasked.get("changed_delta") or {}
    luma = delta.get("luma") or {}
    return {
        "unmasked_changed_pct": unmasked.get("changed_pct"),
        "route_masked_changed_pct": route_masked.get("changed_pct"),
        "luma_delta_mean": luma.get("mean"),
        "native_bluer_pct": delta.get("native_bluer_pct"),
        "native_brighter_pct": delta.get("native_brighter_pct"),
        "native_darker_pct": delta.get("native_darker_pct"),
    }


default_stock = load_json(root / "default" / "stock_vs_default_visual.json")
default_semantics = load_json(root / "default" / "roi_pixel_semantics.json")
default_full = changed_pct(default_stock)
default_regions = {
    name: region_changed(default_stock, name)
    for name in ("tower_pane", "projected_impact", "impact_side", "lower_actor_cluster")
}
default_roi = {
    name: roi_region_metrics(default_semantics, name)
    for name in ("tower_pane", "projected_impact", "impact_side")
}

variant_payloads: dict[str, Any] = {}
for variant in variants:
    variant_dir = root / variant
    native_compare = load_json(variant_dir / f"default_vs_{variant}_visual.json")
    stock_compare = load_json(variant_dir / f"stock_vs_{variant}_visual.json")
    footprint = load_json(variant_dir / "control_footprint_visual.json")
    semantics = load_json(variant_dir / "roi_pixel_semantics.json")
    native_regions = {
        name: region_changed(native_compare, name)
        for name in ("tower_pane", "projected_impact", "impact_side", "lower_actor_cluster")
    }
    stock_regions = {
        name: region_changed(stock_compare, name)
        for name in ("tower_pane", "projected_impact", "impact_side", "lower_actor_cluster")
    }
    variant_payloads[variant] = {
        "env": (variant_dir / "env.txt").read_text(encoding="utf-8").splitlines(),
        "default_vs_variant": {
            "compare": str(variant_dir / f"default_vs_{variant}_visual.json"),
            "full_changed_pct": changed_pct(native_compare),
            "regions": native_regions,
        },
        "stock_vs_variant": {
            "compare": str(variant_dir / f"stock_vs_{variant}_visual.json"),
            "full_changed_pct": changed_pct(stock_compare),
            "regions": stock_regions,
        },
        "roi_pixel_semantics": {
            "compare": str(variant_dir / "roi_pixel_semantics.json"),
            "regions": {
                name: roi_region_metrics(semantics, name)
                for name in ("tower_pane", "projected_impact", "impact_side")
            },
        },
        "stock_delta_vs_default": {
            "full": (
                changed_pct(stock_compare) - default_full
                if changed_pct(stock_compare) is not None and default_full is not None
                else None
            ),
            "regions": {
                name: (
                    stock_regions[name] - default_regions[name]
                    if stock_regions.get(name) is not None and default_regions.get(name) is not None
                    else None
                )
                for name in stock_regions
            },
        },
        "control_footprint": {
            "compare": str(variant_dir / "control_footprint_visual.json"),
            "status": footprint.get("status"),
            "regions": {
                name: {
                    "control_changed_pct": (footprint.get("regions") or {}).get(name, {}).get("control_changed_pct"),
                    "stock_mismatch_covered_pct": (footprint.get("regions") or {}).get(name, {}).get("stock_mismatch_covered_pct"),
                    "outside_control_stock_changed_pct": (footprint.get("regions") or {}).get(name, {}).get("outside_control_stock_changed_pct"),
                }
                for name in ("tower_pane", "projected_impact", "impact_side")
            },
        },
    }
    if footprint.get("status") != "pass":
        failures.append(f"{variant}: control footprint scorer status {footprint.get('status')}")

room_visible = any(
    (payload["default_vs_variant"]["regions"].get("tower_pane") or 0.0) > 0.5
    for payload in variant_payloads.values()
)
stock_improvements = [
    (variant, payload["stock_delta_vs_default"]["regions"].get("tower_pane"))
    for variant, payload in variant_payloads.items()
    if isinstance(payload["stock_delta_vs_default"]["regions"].get("tower_pane"), (int, float))
    and payload["stock_delta_vs_default"]["regions"]["tower_pane"] < -0.1
]
impact_side_movers = [
    variant
    for variant, payload in variant_payloads.items()
    if (payload["default_vs_variant"]["regions"].get("impact_side") or 0.0) > 0.5
]

interpretation = []
if room_visible:
    interpretation.append("room-glass controls are pixel-visible in tower_pane")
else:
    interpretation.append("room-glass controls barely move tower_pane in native A/Bs")

if stock_improvements:
    best = sorted(stock_improvements, key=lambda item: item[1])[0]
    if best[1] <= -1.0:
        interpretation.append(
            f"best stock-directed tower_pane clue is {best[0]} ({best[1]:+.3f} pct points)"
        )
    else:
        interpretation.append(
            f"only tiny stock-directed tower_pane movement was {best[0]} ({best[1]:+.3f} pct points)"
        )
else:
    interpretation.append("no tested room-glass control materially improves tower_pane stock parity")

if impact_side_movers:
    interpretation.append(
        "impact_side moves under broad room/filter controls, but pair this with "
        "texgen ROI material tracing before assigning ownership: "
        + ", ".join(sorted(impact_side_movers))
    )
else:
    interpretation.append("impact_side does not move under tested room-glass controls")

footprint_coverages = [
    (
        variant,
        payload["control_footprint"]["regions"].get("tower_pane", {}).get("stock_mismatch_covered_pct"),
    )
    for variant, payload in variant_payloads.items()
    if isinstance(
        payload["control_footprint"]["regions"].get("tower_pane", {}).get("stock_mismatch_covered_pct"),
        (int, float),
    )
]
if footprint_coverages:
    best_variant, best_coverage = sorted(footprint_coverages, key=lambda item: item[1], reverse=True)[0]
    interpretation.append(
        f"largest tower_pane stock/native mismatch coverage by any control footprint is "
        f"{best_coverage:.3f}% ({best_variant})"
    )

projected_stock_deltas = [
    (
        variant,
        payload["stock_delta_vs_default"]["regions"].get("projected_impact"),
        payload["roi_pixel_semantics"]["regions"].get("projected_impact", {}).get("luma_delta_mean"),
    )
    for variant, payload in variant_payloads.items()
    if isinstance(payload["stock_delta_vs_default"]["regions"].get("projected_impact"), (int, float))
]
if projected_stock_deltas:
    best_projected = sorted(projected_stock_deltas, key=lambda item: item[1])[0]
    if best_projected[1] < -0.1:
        interpretation.append(
            "best stock-directed projected_impact movement is "
            f"{best_projected[0]} ({best_projected[1]:+.3f} pct points, "
            f"luma_delta={best_projected[2]})"
        )
    elif best_projected[1] <= 0.1:
        interpretation.append(
            "projected_impact has no material stock-parity movement; nearest "
            f"neutral variant is {best_projected[0]} ({best_projected[1]:+.3f} "
            f"pct points, luma_delta={best_projected[2]})"
        )
    else:
        interpretation.append(
            "no tested variant improves projected_impact stock parity; least "
            f"bad delta is {best_projected[0]} ({best_projected[1]:+.3f} "
            f"pct points, luma_delta={best_projected[2]})"
        )

payload = {
    "status": "fail" if failures else "pass",
    "failures": failures,
    "route": route,
    "target_room_glass_cc": room_glass_cc,
    "default_stock": {
        "compare": str(root / "default" / "stock_vs_default_visual.json"),
        "full_changed_pct": default_full,
        "regions": default_regions,
        "roi_pixel_semantics": default_roi,
    },
    "variants": variant_payloads,
    "interpretation": interpretation,
}

out = root / "glass_pad10092_room_glass_visibility_summary.json"
out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
txt = root / "glass_pad10092_room_glass_visibility_summary.txt"
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
echo "Summary: $OUT_DIR/glass_pad10092_room_glass_visibility_summary.json"
