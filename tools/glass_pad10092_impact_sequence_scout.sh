#!/bin/bash
#
# glass_pad10092_impact_sequence_scout.sh -- native-only route cleanup scout for
# the Dam pad-10092 impact fixture.
#
# It reuses an existing stock-backed pad10092 case, generates native-only route
# variants, and ranks each capture by full sampled world-impact sequence parity.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

ROUTE="dam_regular_glass_shatter_pad10092_impact_visual_probe"
BASE_ROUTE="tools/rom_oracle_routes/${ROUTE}.json"
BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
BASE_CASE_DIR="/tmp/mgb64_glass_pad10092_impact_framing_gate/pad10092_impact"
OUT_DIR="/tmp/mgb64_glass_pad10092_impact_sequence_scout_$$"
DO_BUILD=1
TIMEOUT_SECONDS=180
STOCK_FRAME=2541
NATIVE_FRAMES=127
X_LIST="159.0,159.4,160.0"
Y_LIST="121.5,123.0,124.5"
FIRE_STARTS="70"
FIRE_LENS="170"
MAX_CASES=0

usage() {
    cat <<'USAGE'
Usage: tools/glass_pad10092_impact_sequence_scout.sh [options]

Options:
  --base-case-dir DIR  existing stock-backed pad10092 impact case dir
                       (default: /tmp/mgb64_glass_pad10092_impact_framing_gate/pad10092_impact)
  --base-route PATH    base route JSON (default: checked-in pad10092 route)
  --out-dir DIR        output directory (default: /tmp/...)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary path (default: build/ge007)
  --build-dir DIR      CMake build directory (default: build)
  --no-build           reuse an existing native binary
  --timeout SECONDS    per native capture timeout (default: 180)
  --stock-frame N      stock frame to compare (default: 2541)
  --native-frames N    native capture length for generated routes (default: 127)
  --x-list LIST        comma-separated crosshair X values
  --y-list LIST        comma-separated crosshair Y values
  --fire-starts LIST   comma-separated native gameplay fire start frames
  --fire-lens LIST     comma-separated native fire hold lengths
  --max-cases N        stop after N generated cases (0 = all)

Set GLASS_PAD10092_SEQUENCE_SCOUT_CANDIDATES to override the generated matrix.
Format:
  name:x:y:fire_start:fire_len:native_frames[,name:...]

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, emulator output, or generated summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --base-case-dir) BASE_CASE_DIR="$2"; shift 2 ;;
        --base-route) BASE_ROUTE="$2"; shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        --stock-frame) STOCK_FRAME="$2"; shift 2 ;;
        --native-frames) NATIVE_FRAMES="$2"; shift 2 ;;
        --x-list) X_LIST="$2"; shift 2 ;;
        --y-list) Y_LIST="$2"; shift 2 ;;
        --fire-starts) FIRE_STARTS="$2"; shift 2 ;;
        --fire-lens) FIRE_LENS="$2"; shift 2 ;;
        --max-cases) MAX_CASES="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

for value_name in TIMEOUT_SECONDS STOCK_FRAME NATIVE_FRAMES MAX_CASES; do
    value="${!value_name}"
    if [[ ! "$value" =~ ^[0-9]+$ ]]; then
        echo "FAIL: $value_name must be a non-negative integer: $value" >&2
        exit 2
    fi
done
if [[ "$TIMEOUT_SECONDS" -eq 0 || "$STOCK_FRAME" -eq 0 || "$NATIVE_FRAMES" -eq 0 ]]; then
    echo "FAIL: --timeout, --stock-frame, and --native-frames must be positive" >&2
    exit 2
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
validation_require_file "$BASE_ROUTE" "base route"
validation_require_file "$BASE_CASE_DIR/stock_${ROUTE}.jsonl" "base stock trace"

mkdir -p "$OUT_DIR/routes"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

echo "=== Glass Pad10092 Impact Sequence Scout ==="
echo "  out-dir:       $OUT_DIR"
echo "  base-route:    $BASE_ROUTE"
echo "  base-case:     $BASE_CASE_DIR"
echo "  stock-frame:   $STOCK_FRAME"
echo "  native:        $BINARY"
echo "  ROM:           $ROM"
echo "  crosshair x:   $X_LIST"
echo "  crosshair y:   $Y_LIST"
echo "  fire starts:   $FIRE_STARTS"
echo "  fire lengths:  $FIRE_LENS"

SUMMARY_JSON="$OUT_DIR/glass_pad10092_impact_sequence_scout_summary.json"
CASES_FILE="$OUT_DIR/cases.tsv"

python3 - \
    "$BASE_ROUTE" \
    "$OUT_DIR/routes" \
    "$CASES_FILE" \
    "$X_LIST" \
    "$Y_LIST" \
    "$FIRE_STARTS" \
    "$FIRE_LENS" \
    "$NATIVE_FRAMES" \
    "$MAX_CASES" <<'PY'
import json
import re
import sys
from pathlib import Path

(
    base_route_path,
    routes_dir,
    cases_path,
    x_list,
    y_list,
    fire_starts,
    fire_lens,
    native_frames,
    max_cases,
) = sys.argv[1:10]

native_frames = int(native_frames)
max_cases = int(max_cases)


def parse_numbers(raw):
    out = []
    for item in raw.split(","):
        item = item.strip()
        if item:
            out.append(float(item))
    if not out:
        raise SystemExit("FAIL: generated route lists must not be empty")
    return out


def parse_ints(raw):
    out = []
    for item in raw.split(","):
        item = item.strip()
        if item:
            out.append(int(item))
    if not out:
        raise SystemExit("FAIL: generated fire lists must not be empty")
    return out


def safe_float(value):
    text = f"{value:.2f}".rstrip("0").rstrip(".")
    return re.sub(r"[^0-9A-Za-z]+", "p", text)


base = json.loads(Path(base_route_path).read_text(encoding="utf-8"))
routes = []
override = None
import os
if os.environ.get("GLASS_PAD10092_SEQUENCE_SCOUT_CANDIDATES"):
    override = os.environ["GLASS_PAD10092_SEQUENCE_SCOUT_CANDIDATES"]

if override:
    for raw in override.split(","):
        name, x, y, fire_start, fire_len, frames = raw.split(":", 5)
        routes.append((name, float(x), float(y), int(fire_start), int(fire_len), int(frames)))
else:
    for x in parse_numbers(x_list):
        for y in parse_numbers(y_list):
            for fire_start in parse_ints(fire_starts):
                for fire_len in parse_ints(fire_lens):
                    name = f"x{safe_float(x)}_y{safe_float(y)}_s{fire_start}_l{fire_len}_f{native_frames}"
                    routes.append((name, x, y, fire_start, fire_len, native_frames))

if max_cases > 0:
    routes = routes[:max_cases]

rows = []
for name, x, y, fire_start, fire_len, frames in routes:
    route_name = f"dam_pad10092_seq_{name}"
    route_path = Path(routes_dir) / f"{route_name}.json"
    force_end = max(260, frames + 140, fire_start + fire_len + 20)
    route = json.loads(json.dumps(base))
    route["name"] = route_name
    route["native_frames"] = frames
    route["native_env"]["GE007_AUTO_FORCE_PLAYER_SCRIPT"] = (
        f"45-{force_end}:16356.85:518.28:19066.15:315.0000:-6.4210:167.31:10092"
    )
    route["native_env"]["GE007_AUTO_CROSSHAIR_SCRIPT"] = f"45-{force_end}:{x:.2f}:{y:.2f}"
    route["native_env"]["GE007_AUTO_AUTOAIM_SCRIPT"] = f"41:0,{max(1, fire_start - 10)}:0,{max(1, fire_start - 1)}:0"
    route["native_events"] = [{"start": fire_start, "len": fire_len, "buttons": ["fire"]}]
    route["stock_events"] = []
    route["events"] = []
    route["description"] = (
        "Temporary native-only pad10092 impact-sequence scout generated by "
        "tools/glass_pad10092_impact_sequence_scout.sh."
    )
    route_path.write_text(json.dumps(route, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    rows.append((name, route_name, route_path, x, y, fire_start, fire_len, frames))

with Path(cases_path).open("w", encoding="utf-8") as handle:
    for row in rows:
        handle.write("\t".join(str(item) for item in row) + "\n")

print(f"generated {len(rows)} route variants")
PY

CASE_RESULTS=()
while IFS=$'\t' read -r case_name route_name route_path crosshair_x crosshair_y fire_start fire_len frames; do
    case_dir="$OUT_DIR/$case_name"
    mkdir -p "$case_dir"
    echo ""
    echo "=== candidate: $case_name ==="
    echo "  route:      $route_path"
    echo "  crosshair:  $crosshair_x,$crosshair_y"
    echo "  fire:       start=$fire_start len=$fire_len frames=$frames"

    set +e
    tools/movement_oracle_capture.sh \
        --route "$route_path" \
        --out-dir "$case_dir" \
        --rom "$ROM" \
        --binary "$BINARY" \
        --native-only \
        --no-build \
        --no-compare \
        --timeout "$TIMEOUT_SECONDS"
    capture_status=$?
    set -e

    native_trace="$case_dir/native_${route_name}.jsonl"
    score_json="$case_dir/impact_sequence_candidates.json"
    score_txt="$case_dir/impact_sequence_candidates.txt"
    if [[ "$capture_status" -eq 0 && -s "$native_trace" ]]; then
        tools/score_bullet_impact_sequence_candidates.py \
            --baseline-frame "$STOCK_FRAME" \
            --require-active \
            --json-out "$score_json" \
            "$BASE_CASE_DIR/stock_${ROUTE}.jsonl" \
            "$native_trace" | tee "$score_txt"
    else
        cat >"$score_json" <<JSON
{
  "status": "capture_failed",
  "capture_status": $capture_status,
  "native_trace": "$native_trace"
}
JSON
        echo "FAIL: candidate $case_name capture_status=$capture_status"
    fi
    CASE_RESULTS+=("$case_name|$route_name|$route_path|$case_dir|$score_json|$capture_status|$crosshair_x|$crosshair_y|$fire_start|$fire_len|$frames")
done < "$CASES_FILE"

python3 - "$SUMMARY_JSON" "${CASE_RESULTS[@]}" <<'PY'
import json
import sys
from pathlib import Path

summary_path = Path(sys.argv[1])
cases = []
for raw in sys.argv[2:]:
    (
        name,
        route_name,
        route_path,
        case_dir,
        score_json,
        capture_status,
        crosshair_x,
        crosshair_y,
        fire_start,
        fire_len,
        frames,
    ) = raw.split("|", 10)
    score_path = Path(score_json)
    data = {}
    if score_path.exists():
        data = json.loads(score_path.read_text(encoding="utf-8"))
    best_items = data.get("best") if isinstance(data, dict) else None
    best = best_items[0] if isinstance(best_items, list) and best_items else None
    strict_items = data.get("best_strict") if isinstance(data, dict) else []
    strict_count = len(strict_items) if isinstance(strict_items, list) else 0
    impact_sequence = None
    if isinstance(best, dict):
        impact_sequence = (best.get("sequence") or {}).get("impact")
    cases.append({
        "name": name,
        "route_name": route_name,
        "route": route_path,
        "case_dir": case_dir,
        "score_json": score_json,
        "capture_status": int(capture_status),
        "crosshair": [float(crosshair_x), float(crosshair_y)],
        "fire_start": int(fire_start),
        "fire_len": int(fire_len),
        "native_frames": int(frames),
        "strict_count": strict_count,
        "best_score": best.get("score") if isinstance(best, dict) else None,
        "best_frame": best.get("frame") if isinstance(best, dict) else None,
        "best_global": best.get("global") if isinstance(best, dict) else None,
        "best_impact_sequence": impact_sequence,
        "best": best,
    })

cases.sort(key=lambda item: (item["best_score"] is None, item["best_score"] or 0))
payload = {
    "status": "strict_found" if any(item["strict_count"] for item in cases) else "no_strict_candidate",
    "cases": cases,
}
summary_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

print("")
print("=== sequence scout summary ===")
print(f"status={payload['status']} cases={len(cases)}")
for item in cases[:10]:
    seq = item.get("best_impact_sequence") or {}
    print(
        "{name}: score={score} strict={strict} frame={frame} crosshair={crosshair} fire={start}:{length} impact={impact}".format(
            name=item["name"],
            score=item["best_score"],
            strict=item["strict_count"],
            frame=item["best_frame"],
            crosshair=item["crosshair"],
            start=item["fire_start"],
            length=item["fire_len"],
            impact=seq.get("test"),
        )
    )
print(f"summary={summary_path}")
PY

echo "artifacts: $OUT_DIR"
