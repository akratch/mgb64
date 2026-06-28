#!/bin/bash
#
# Scout Dam active-glass visual candidates for actor-composition cleanliness.
#
# This is intentionally a scouting helper, not a pass/fail regression gate. It
# generates temporary native-only route variants from the checked-in pad10001
# visual candidate, runs them, and ranks each native trace against a supplied
# stock active-glass trace with score_actor_composition_checkpoints.py.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
BASE_ROUTE="tools/rom_oracle_routes/dam_regular_glass_shatter_pad10001_visual_probe.json"
STOCK_TRACE=""
OUT_DIR="/tmp/mgb64_glass_actor_clean_scout_$$"
DO_BUILD=1
TIMEOUT_SECONDS=180
TARGET_PAD=10001
POSE="13659.14:-474.70:2192.46:308.9732:-10.7187:167.31:10001"
ACTOR_CHRNUMS="10,12"

usage() {
    cat <<'USAGE'
Usage: tools/glass_actor_clean_candidate_scout.sh --stock-trace TRACE [options]

Options:
  --stock-trace PATH    stock JSONL trace to compare against
  --base-route PATH     base route JSON (default: pad10001 visual candidate)
  --target-pad PAD      glass object pad to trace/warp (default: 10001)
  --pose SPEC           X:Y:Z:YAW_DEG:PITCH_DEG:EYE_OFFSET:PAD forced pose
  --actor-chrnums LIST  comma-separated chrnums required in composition score
  --out-dir DIR         output directory (default: /tmp/...)
  --rom PATH            ROM path (default: ./baserom.u.z64)
  --binary PATH         native binary path (default: build/ge007)
  --build-dir DIR       CMake build directory (default: build)
  --no-build            reuse an existing native binary
  --timeout SECONDS     per native capture timeout (default: 180)

Set GLASS_ACTOR_SCOUT_CANDIDATES to override the candidate matrix. Format:
  name:warp_frame:force_start:fire_start:native_frames[,name:...]

The generated route holds the configured camera pose until native_frames and
fires for 170 frames. Artifacts are ROM-derived local validation data.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --stock-trace) STOCK_TRACE="$2"; shift 2 ;;
        --base-route) BASE_ROUTE="$2"; shift 2 ;;
        --target-pad) TARGET_PAD="$2"; shift 2 ;;
        --pose) POSE="$2"; shift 2 ;;
        --actor-chrnums) ACTOR_CHRNUMS="$2"; shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ -z "$STOCK_TRACE" ]]; then
    echo "FAIL: --stock-trace is required" >&2
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
validation_require_file "$STOCK_TRACE" "stock trace"
if [[ ! "$TARGET_PAD" =~ ^[0-9]+$ ]]; then
    echo "FAIL: --target-pad must be an integer" >&2
    exit 2
fi
if [[ ! "$ACTOR_CHRNUMS" =~ ^[0-9]+(,[0-9]+)*$ ]]; then
    echo "FAIL: --actor-chrnums must be a comma-separated integer list" >&2
    exit 2
fi
if [[ ! "$POSE" =~ ^[-0-9.]+:[-0-9.]+:[-0-9.]+:[-0-9.]+:[-0-9.]+:[-0-9.]+:[0-9]+$ ]]; then
    echo "FAIL: --pose must be X:Y:Z:YAW_DEG:PITCH_DEG:EYE_OFFSET:PAD" >&2
    exit 2
fi

mkdir -p "$OUT_DIR/routes"

if [[ -n "${GLASS_ACTOR_SCOUT_CANDIDATES:-}" ]]; then
    IFS=',' read -r -a CANDIDATES <<<"$GLASS_ACTOR_SCOUT_CANDIDATES"
else
    CANDIDATES=(
        "latewarp1240_fire1393:1240:1245:1393:1469"
        "latewarp1300_fire1393:1300:1305:1393:1469"
        "latewarp1330_fire1393:1330:1335:1393:1469"
        "latewarp1360_fire1393:1360:1365:1393:1469"
        "latewarp1375_fire1393:1375:1380:1393:1469"
        "latewarp1385_fire1398:1385:1390:1398:1474"
    )
fi

echo "=== Dam Glass Actor-Clean Candidate Scout ==="
echo "  out-dir:     $OUT_DIR"
echo "  base-route:  $BASE_ROUTE"
echo "  stock-trace: $STOCK_TRACE"
echo "  native:      $BINARY"
echo "  target-pad:  $TARGET_PAD"
echo "  pose:        $POSE"
echo "  actors:      $ACTOR_CHRNUMS"
echo "  candidates:  ${#CANDIDATES[@]}"

SUMMARY_JSON="$OUT_DIR/actor_clean_scout_summary.json"
CASE_JSONS=()

for spec in "${CANDIDATES[@]}"; do
    IFS=':' read -r case_name warp_frame force_start fire_start native_frames extra <<<"$spec"
    if [[ -z "${case_name:-}" || -z "${warp_frame:-}" || -z "${force_start:-}" || -z "${fire_start:-}" || -z "${native_frames:-}" || -n "${extra:-}" ]]; then
        echo "FAIL: invalid candidate spec: $spec" >&2
        exit 2
    fi
    if [[ ! "$warp_frame" =~ ^[0-9]+$ || ! "$force_start" =~ ^[0-9]+$ || ! "$fire_start" =~ ^[0-9]+$ || ! "$native_frames" =~ ^[0-9]+$ ]]; then
        echo "FAIL: candidate spec has non-integer frame: $spec" >&2
        exit 2
    fi

    route_name="dam_glass_actor_clean_pad${TARGET_PAD}_${case_name}"
    route_path="$OUT_DIR/routes/${route_name}.json"
    case_dir="$OUT_DIR/$case_name"
    mkdir -p "$case_dir"

    python3 - "$BASE_ROUTE" "$route_path" "$route_name" "$warp_frame" "$force_start" "$fire_start" "$native_frames" "$TARGET_PAD" "$POSE" <<'PY'
import json
import sys
from pathlib import Path

base_route, route_path, route_name, warp_frame, force_start, fire_start, native_frames, target_pad, pose = sys.argv[1:10]
warp_frame = int(warp_frame)
force_start = int(force_start)
fire_start = int(fire_start)
native_frames = int(native_frames)
target_pad = int(target_pad)
force_end = max(native_frames + 80, fire_start + 180)

route = json.loads(Path(base_route).read_text(encoding="utf-8"))
route["name"] = route_name
route["native_frames"] = native_frames
route["native_env"]["GE007_TRACE_GLASS_BUDGET"] = str(max(2200, native_frames + 500))
route["native_env"]["GE007_OBJECT_TRACE_BUDGET"] = str(max(2200, native_frames + 500))
route["native_env"]["GE007_OBJECT_TRACE_PAD"] = target_pad
route["native_env"]["GE007_AUTO_WARP_FRAME"] = warp_frame
route["native_env"]["GE007_AUTO_WARP_PAD"] = target_pad
route["native_env"]["GE007_AUTO_FORCE_PLAYER_SCRIPT"] = f"{force_start}-{force_end}:{pose}"
route["native_env"]["GE007_AUTO_AUTOAIM_SCRIPT"] = (
    f"{max(1, warp_frame + 1)}:0,{max(1, fire_start - 10)}:0,{max(1, fire_start - 1)}:0"
)
route["native_env"]["GE007_AUTO_CROSSHAIR_SCRIPT"] = f"{force_start}-{force_end}:160:120"
route["native_events"] = [{"start": fire_start, "len": 170, "buttons": ["fire"]}]
route["description"] = (
    "Temporary native-only glass actor-composition timing scout generated by "
    "tools/glass_actor_clean_candidate_scout.sh."
)
Path(route_path).write_text(json.dumps(route, indent=2, sort_keys=True) + "\n", encoding="utf-8")
PY

    echo ""
    echo "=== candidate: $case_name ==="
    echo "  route: $route_path"

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

    native_trace="$case_dir/native_${route_name}.jsonl"
    score_json="$case_dir/composition_score.json"
    score_txt="$case_dir/composition_score.txt"
    if [[ "$capture_status" -eq 0 && -s "$native_trace" ]]; then
        score_actor_args=()
        IFS=',' read -r -a score_actor_chrnums <<<"$ACTOR_CHRNUMS"
        for chrnum in "${score_actor_chrnums[@]}"; do
            score_actor_args+=(--actor-chrnum "$chrnum")
        done
        python3 tools/score_actor_composition_checkpoints.py \
            --require-active \
            "${score_actor_args[@]}" \
            --top 10 \
            --json-out "$score_json" \
            "$STOCK_TRACE" \
            "$native_trace" | tee "$score_txt"
    else
        cat >"$score_json" <<JSON
{
  "capture_status": $capture_status,
  "error": "native capture failed or trace missing",
  "native_trace": "$native_trace"
}
JSON
        echo "FAIL: candidate $case_name capture_status=$capture_status"
    fi
    CASE_JSONS+=("$case_name:$route_path:$case_dir:$score_json:$capture_status")
done

python3 - "$SUMMARY_JSON" "${CASE_JSONS[@]}" <<'PY'
import json
import sys
from pathlib import Path

summary_path = Path(sys.argv[1])
cases = []
for raw in sys.argv[2:]:
    name, route_path, case_dir, score_json, capture_status = raw.split(":", 4)
    score_path = Path(score_json)
    data = {}
    if score_path.is_file():
        data = json.loads(score_path.read_text(encoding="utf-8"))
    best = None
    best_items = data.get("best") if isinstance(data, dict) else None
    if isinstance(best_items, list) and best_items:
        best = best_items[0]
    strict_count = 0
    strict_items = data.get("best_strict") if isinstance(data, dict) else None
    if isinstance(strict_items, list):
        strict_count = len(strict_items)
    cases.append({
        "name": name,
        "route": route_path,
        "case_dir": case_dir,
        "score_json": score_json,
        "capture_status": int(capture_status),
        "best_score": best.get("score") if isinstance(best, dict) else None,
        "best": best,
        "strict_count": strict_count,
    })

cases.sort(key=lambda item: (item["best_score"] is None, item["best_score"] or 0))
payload = {"cases": cases}
summary_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

print("")
print("=== scout summary ===")
for item in cases:
    score = item["best_score"]
    score_text = "none" if score is None else f"{score:.3f}"
    strict = item["strict_count"]
    best = item.get("best") or {}
    print(
        f"{item['name']}: status={item['capture_status']} score={score_text} "
        f"strict={strict} score_json={item['score_json']}"
    )
    if isinstance(best, dict) and best:
        print(
            "  best: "
            f"stock_frame={best['baseline']['frame']} native_frame={best['test']['frame']} "
            f"visible_delta={best['visible_set_delta']} "
            f"field_mismatches={len(best['field_mismatches'])} "
            f"max_pos={best['max_position_delta']:.3f} "
            f"timer_delta={best['timer_delta']}"
        )
print(f"summary_json: {summary_path}")
PY

echo ""
echo "=== Dam Glass Actor-Clean Candidate Scout: DONE ==="
