#!/bin/bash
#
# Broad native-only scout for Dam active-glass fixture candidates.
#
# This script is for discovery. It generates temporary routes around ranked Dam
# glass panes, captures each route native-only, and scores whether the target
# pane shatters with few/no visible actors at the first active shard frame.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
BASE_ROUTE="tools/rom_oracle_routes/dam_regular_glass_shatter_pad10001_visual_probe.json"
STAGE_PADS=""
STAGE_CHRS=""
OUT_DIR="/tmp/mgb64_glass_native_fixture_scout_$$"
DO_BUILD=1
TIMEOUT_SECONDS=120
TOP_PANES=6
MAX_CANDIDATES=18
ANGLES="0,45,90,180,270,315"
DISTANCES="420"
HEIGHT_OFFSET="73.15"
FIRE_START=70
NATIVE_FRAMES=170

usage() {
    cat <<'USAGE'
Usage: tools/glass_native_fixture_scout.sh --stage-pads PADS --stage-chrs CHRS [options]

Options:
  --stage-pads PATH     GE007_DUMP_STAGE_PADS JSONL
  --stage-chrs PATH     GE007_DUMP_STAGE_CHRS JSONL
  --base-route PATH     base route JSON (default: pad10001 visual candidate)
  --out-dir DIR         output directory (default: /tmp/...)
  --rom PATH            ROM path (default: ./baserom.u.z64)
  --binary PATH         native binary path (default: build/ge007)
  --build-dir DIR       CMake build directory (default: build)
  --no-build            reuse an existing native binary
  --timeout SECONDS     per native capture timeout (default: 120)
  --top-panes N         ranked regular-glass panes to scout (default: 6)
  --max-candidates N    maximum generated routes to run (default: 18)
  --angles LIST         comma-separated yaw angles in degrees (default: 0,45,90,180,270,315)
  --distances LIST      comma-separated horizontal distances (default: 420)
  --height-offset N     viewer Y minus pane Y (default: 73.15)
  --fire-start N        native gameplay frame to start firing (default: 70)
  --native-frames N     native capture frames (default: 170)

Artifacts are ROM-derived local validation data. Do not commit captures.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --stage-pads) STAGE_PADS="$2"; shift 2 ;;
        --stage-chrs) STAGE_CHRS="$2"; shift 2 ;;
        --base-route) BASE_ROUTE="$2"; shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        --top-panes) TOP_PANES="$2"; shift 2 ;;
        --max-candidates) MAX_CANDIDATES="$2"; shift 2 ;;
        --angles) ANGLES="$2"; shift 2 ;;
        --distances) DISTANCES="$2"; shift 2 ;;
        --height-offset) HEIGHT_OFFSET="$2"; shift 2 ;;
        --fire-start) FIRE_START="$2"; shift 2 ;;
        --native-frames) NATIVE_FRAMES="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ -z "$STAGE_PADS" || -z "$STAGE_CHRS" ]]; then
    echo "FAIL: --stage-pads and --stage-chrs are required" >&2
    usage >&2
    exit 2
fi
if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
fi
if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"
validation_require_file "$BASE_ROUTE" "base route"
validation_require_file "$STAGE_PADS" "stage pads"
validation_require_file "$STAGE_CHRS" "stage chrs"

for int_value in "$TOP_PANES" "$MAX_CANDIDATES" "$FIRE_START" "$NATIVE_FRAMES"; do
    if [[ ! "$int_value" =~ ^[0-9]+$ ]]; then
        echo "FAIL: expected integer option, got '$int_value'" >&2
        exit 2
    fi
done

mkdir -p "$OUT_DIR/routes"
ROUTE_INDEX_JSON="$OUT_DIR/generated_routes.json"
SUMMARY_JSON="$OUT_DIR/native_fixture_scout_summary.json"

echo "=== Dam Native Glass Fixture Scout ==="
echo "  out-dir:        $OUT_DIR"
echo "  base-route:     $BASE_ROUTE"
echo "  stage-pads:     $STAGE_PADS"
echo "  stage-chrs:     $STAGE_CHRS"
echo "  native:         $BINARY"
echo "  top-panes:      $TOP_PANES"
echo "  max-candidates: $MAX_CANDIDATES"
echo "  angles:         $ANGLES"
echo "  distances:      $DISTANCES"

python3 - \
    "$BASE_ROUTE" \
    "$STAGE_PADS" \
    "$STAGE_CHRS" \
    "$OUT_DIR/routes" \
    "$ROUTE_INDEX_JSON" \
    "$TOP_PANES" \
    "$MAX_CANDIDATES" \
    "$ANGLES" \
    "$DISTANCES" \
    "$HEIGHT_OFFSET" \
    "$FIRE_START" \
    "$NATIVE_FRAMES" <<'PY'
import json
import math
import re
import sys
from pathlib import Path

(
    base_route_path,
    stage_pads_path,
    stage_chrs_path,
    routes_dir,
    route_index_path,
    top_panes,
    max_candidates,
    angles_raw,
    distances_raw,
    height_offset,
    fire_start,
    native_frames,
) = sys.argv[1:13]

top_panes = int(top_panes)
max_candidates = int(max_candidates)
height_offset = float(height_offset)
fire_start = int(fire_start)
native_frames = int(native_frames)
angles = [float(item) for item in angles_raw.split(",") if item.strip()]
distances = [float(item) for item in distances_raw.split(",") if item.strip()]
if not angles or not distances:
    raise SystemExit("FAIL: --angles and --distances must not be empty")


def load_jsonl(path):
    records = []
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            raw = raw.strip()
            if raw:
                records.append(json.loads(raw))
    return records


def triplet(value):
    if isinstance(value, list) and len(value) == 3:
        return [float(value[0]), float(value[1]), float(value[2])]
    return None


def distance(lhs, rhs):
    return math.sqrt(sum((a - b) * (a - b) for a, b in zip(lhs, rhs)))


panes = []
for record in load_jsonl(stage_pads_path):
    if record.get("scope") != "setup" or record.get("kind") != "object":
        continue
    if record.get("type_name") != "glass":
        continue
    pos = triplet(record.get("pos"))
    if pos is None:
        continue
    panes.append({
        "index": record.get("index"),
        "pad": record.get("pad"),
        "room": record.get("room"),
        "type": record.get("type"),
        "type_name": record.get("type_name"),
        "pos": pos,
    })

chrs = []
for record in load_jsonl(stage_chrs_path):
    if record.get("scope") != "stage" or record.get("kind") != "chr":
        continue
    prop = record.get("prop")
    if not isinstance(prop, dict) or not prop.get("present"):
        continue
    pos = triplet([prop.get("x"), prop.get("y"), prop.get("z")])
    if pos is None:
        continue
    chrs.append({
        "slot": record.get("slot"),
        "chrnum": record.get("chrnum"),
        "alive": record.get("alive"),
        "hidden": record.get("hidden"),
        "action": record.get("action"),
        "room": prop.get("room"),
        "pos": pos,
    })


def pane_sort_key(pane):
    distances_to_chrs = [
        distance(pane["pos"], chr_["pos"])
        for chr_ in chrs
        if chr_.get("alive") != 0
    ]
    distances_to_chrs.sort()
    nearest = distances_to_chrs[0] if distances_to_chrs else 100000.0
    near_250 = sum(1 for item in distances_to_chrs if item <= 250.0)
    near_500 = sum(1 for item in distances_to_chrs if item <= 500.0)
    near_1000 = sum(1 for item in distances_to_chrs if item <= 1000.0)
    return (near_250, near_500, near_1000, -nearest)


ranked_panes = sorted(panes, key=pane_sort_key)[:top_panes]
base = json.loads(Path(base_route_path).read_text(encoding="utf-8"))
routes_dir = Path(routes_dir)
routes_dir.mkdir(parents=True, exist_ok=True)

generated = []
for dist in distances:
    for angle in angles:
        for pane in ranked_panes:
            if len(generated) >= max_candidates:
                break
            target = pane["pos"]
            theta = math.radians(angle)
            facing_x = -math.sin(theta)
            facing_z = math.cos(theta)
            x = target[0] - (facing_x * dist)
            y = target[1] + height_offset
            z = target[2] - (facing_z * dist)
            pitch = math.degrees(math.atan2(target[1] - y, dist))
            yaw = angle % 360.0
            force_end = max(native_frames + 90, fire_start + 180)
            route_name = (
                f"dam_glass_native_fixture_pad{pane['pad']}"
                f"_yaw{int(round(yaw))}_d{int(round(dist))}"
            )
            route_name = re.sub(r"[^A-Za-z0-9_]+", "_", route_name)
            route = json.loads(json.dumps(base))
            route["name"] = route_name
            route["native_frames"] = native_frames
            route["stock_frames"] = max(int(route.get("stock_frames", 2700)), 2700)
            route["stock_screenshot_frame"] = int(route.get("stock_screenshot_frame", 2616))
            route["description"] = (
                "Temporary native-only broad glass fixture scout generated by "
                "tools/glass_native_fixture_scout.sh."
            )
            route["native_env"]["GE007_TRACE_GLASS"] = "1"
            route["native_env"]["GE007_TRACE_GLASS_BUDGET"] = str(max(1200, native_frames + 400))
            route["native_env"]["GE007_OBJECT_TRACE"] = "1"
            route["native_env"]["GE007_OBJECT_TRACE_PAD"] = int(pane["pad"])
            route["native_env"]["GE007_OBJECT_TRACE_BUDGET"] = str(max(1200, native_frames + 400))
            route["native_env"]["GE007_AUTO_WARP_FRAME"] = 40
            route["native_env"]["GE007_AUTO_WARP_PAD"] = int(pane["pad"])
            route["native_env"]["GE007_AUTO_FORCE_PLAYER_SCRIPT"] = (
                f"45-{force_end}:{x:.2f}:{y:.2f}:{z:.2f}:{yaw:.4f}:{pitch:.4f}:167.31:{int(pane['pad'])}"
            )
            route["native_env"]["GE007_AUTO_AUTOAIM_SCRIPT"] = f"41:0,{max(1, fire_start - 10)}:0,{max(1, fire_start - 1)}:0"
            route["native_env"]["GE007_AUTO_CROSSHAIR_SCRIPT"] = f"45-{force_end}:160:120"
            route["native_events"] = [{"start": fire_start, "len": 170, "buttons": ["fire"]}]
            route_path = routes_dir / f"{route_name}.json"
            route_path.write_text(json.dumps(route, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            generated.append({
                "name": route_name,
                "route": str(route_path),
                "target_pad": int(pane["pad"]),
                "pane": pane,
                "yaw": yaw,
                "distance": dist,
                "pose": {
                    "x": x,
                    "y": y,
                    "z": z,
                    "yaw": yaw,
                    "pitch": pitch,
                },
            })
        if len(generated) >= max_candidates:
            break
    if len(generated) >= max_candidates:
        break

Path(route_index_path).write_text(json.dumps({"routes": generated}, indent=2, sort_keys=True) + "\n", encoding="utf-8")
print(f"generated_routes={len(generated)} route_index={route_index_path}")
for item in generated:
    print(
        f"  {item['name']} target={item['target_pad']} "
        f"yaw={item['yaw']:.1f} dist={item['distance']:.1f} pose={item['pose']}"
    )
PY

CASE_JSONS=()
route_count="$(python3 - "$ROUTE_INDEX_JSON" <<'PY'
import json
import sys
print(len(json.load(open(sys.argv[1]))["routes"]))
PY
)"

for ((i = 0; i < route_count; i++)); do
    case_fields="$(python3 - "$ROUTE_INDEX_JSON" "$i" <<'PY'
import json
import sys
item = json.load(open(sys.argv[1]))["routes"][int(sys.argv[2])]
print(item["name"])
print(item["route"])
print(item["target_pad"])
PY
)"
    case_name="$(printf '%s\n' "$case_fields" | sed -n '1p')"
    route_path="$(printf '%s\n' "$case_fields" | sed -n '2p')"
    target_pad="$(printf '%s\n' "$case_fields" | sed -n '3p')"
    case_dir="$OUT_DIR/$case_name"
    mkdir -p "$case_dir"

    echo ""
    echo "=== native fixture candidate: $case_name target=$target_pad ==="
    set +e
    tools/movement_oracle_capture.sh \
        --route "$route_path" \
        --out-dir "$case_dir" \
        --rom "$ROM" \
        --binary "$BINARY" \
        --native-only \
        --no-build \
        --timeout "$TIMEOUT_SECONDS"
    capture_status=$?
    set -e

    native_trace="$case_dir/native_${case_name}.jsonl"
    score_json="$case_dir/native_fixture_score.json"
    score_txt="$case_dir/native_fixture_score.txt"
    if [[ "$capture_status" -eq 0 && -s "$native_trace" ]]; then
        python3 tools/score_native_glass_fixture.py \
            --target-pad "$target_pad" \
            --json-out "$score_json" \
            "$native_trace" | tee "$score_txt"
    else
        cat >"$score_json" <<JSON
{
  "capture_status": $capture_status,
  "error": "native capture failed or trace missing",
  "native_trace": "$native_trace",
  "target_pad": $target_pad
}
JSON
        echo "FAIL: candidate $case_name capture_status=$capture_status"
    fi
    CASE_JSONS+=("$case_name:$route_path:$case_dir:$score_json:$capture_status:$target_pad")
done

python3 - "$SUMMARY_JSON" "$ROUTE_INDEX_JSON" "${CASE_JSONS[@]}" <<'PY'
import json
import sys
from pathlib import Path

summary_path = Path(sys.argv[1])
route_index = json.load(open(sys.argv[2], "r", encoding="utf-8"))
route_meta = {item["name"]: item for item in route_index["routes"]}
cases = []
for raw in sys.argv[3:]:
    name, route_path, case_dir, score_json, capture_status, target_pad = raw.split(":", 5)
    score = {}
    path = Path(score_json)
    if path.is_file():
        score = json.loads(path.read_text(encoding="utf-8"))
    cases.append({
        "name": name,
        "route": route_path,
        "case_dir": case_dir,
        "score_json": score_json,
        "capture_status": int(capture_status),
        "target_pad": int(target_pad),
        "meta": route_meta.get(name),
        "score": score,
        "best_score": score.get("score") if isinstance(score, dict) else None,
        "target_destroyed": score.get("target_destroyed") if isinstance(score, dict) else None,
        "first_active_present": score.get("first_active_present") if isinstance(score, dict) else None,
        "visible_count": score.get("visible_count") if isinstance(score, dict) else None,
        "onscreen_count": score.get("onscreen_count") if isinstance(score, dict) else None,
    })

cases.sort(
    key=lambda item: (
        item["capture_status"] != 0,
        not bool(item["first_active_present"]),
        not bool(item["target_destroyed"]),
        item["best_score"] is None,
        item["best_score"] or 0,
    )
)
summary_path.write_text(json.dumps({"cases": cases}, indent=2, sort_keys=True) + "\n", encoding="utf-8")

print("")
print("=== native fixture scout summary ===")
for item in cases:
    score = item["score"] if isinstance(item["score"], dict) else {}
    score_text = "none" if item["best_score"] is None else f"{item['best_score']:.3f}"
    print(
        f"{item['name']}: status={item['capture_status']} score={score_text} "
        f"target={item['target_pad']} target_destroyed={score.get('target_destroyed')} "
        f"active={score.get('first_active_present')} max_active={score.get('max_active')} "
        f"visible={score.get('visible_count')} onscreen={score.get('onscreen_count')} "
        f"frame={score.get('frame')} score_json={item['score_json']}"
    )
print(f"summary_json: {summary_path}")
PY

echo ""
echo "=== Dam Native Glass Fixture Scout: DONE ==="
