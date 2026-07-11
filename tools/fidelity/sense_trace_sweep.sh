#!/bin/bash
#
# sense_trace_sweep.sh -- S1 trace-comparator sense lane (Fidelity Flywheel).
#
# For every oracle route in tools/rom_oracle_routes/ that has a cached stock
# capture, capture the native trace under the determinism envelope, run the
# comparator that matches the route's compare_kind (movement/combat/intro/glass),
# and turn any divergent-field report into a sense candidate. This is the "S1"
# lane of the S-Tier plan (Task 2.1).
#
#   default   : SENSE mode. Sweep every applicable route, harvest candidates
#               into docs/fidelity/reports/sense_trace_<ts>.json. Report-only
#               (a divergence is a finding, never a lane failure); exit 0.
#   --gate     : RATCHET mode. Sweep only routes marked "gate": true in their
#               route JSON (the known-good set) and exit 1 on ANY divergence.
#               This is what joins verify_manifest tier 3.
#   --self-test: feed a synthetic comparator report to the candidate emitter and
#               prove a candidate is produced. ROM/ares-free; exit 0.
#
# Route -> comparator mapping (by route JSON `compare_kind`):
#   movement -> tools/compare_movement_trace.py   (surface: movement)
#   intro    -> tools/compare_intro_trace.py       (surface: intro)
#   glass    -> tools/compare_glass_trace.py        (surface: glass)
#   visual   -> (S2 pixel sweep's domain; skipped here with a note)
# A route additionally carrying `"compare_combat": true` also runs
#   tools/compare_combat_trace.py                   (surface: combat).
#
# Stock captures are cached (git-ignored) in
#   build/oracle_cache/<route>/<ares-hash>/stock_trace.jsonl
# ROM/ares-gated: a route with no cached stock capture SKIPs cleanly and is noted
# in the report's "skipped" list (charter rule 9). With no ROM or no native
# binary the whole lane SKIPs cleanly (exit 0, empty candidate set).
#
# Report-only SENSE lane: this never files into the ledger (the loop's job).
#
set -uo pipefail
cd "$(dirname "$0")/../.."
ROOT="$(pwd)"
source tools/validation_common.sh

ROUTES_DIR="tools/rom_oracle_routes"
REPORTS_DIR="docs/fidelity/reports"
CACHE_DIR="build/oracle_cache"
TS="$(date -u +%Y%m%dT%H%M%SZ)"
OUT_DIR="/tmp/mgb64_sense_trace_$$"
REPORT="${REPORTS_DIR}/sense_trace_${TS}.json"

ROM="$(validation_default_rom)"
BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ARES_BIN=""
GATE=0
SELF_TEST=0
ONLY_ROUTE=""

usage() {
    cat <<'USAGE'
Usage: tools/fidelity/sense_trace_sweep.sh [options]
  --gate            ratchet mode: only "gate":true routes, exit 1 on divergence
  --route NAME      restrict the sweep to a single route
  --self-test       prove candidate emission on a synthetic report (no ROM/ares)
  --binary PATH     native binary (default: build/ge007)
  --rom PATH        ROM (default: ./baserom.u.z64)
  --ares-bin PATH   instrumented ares binary for stock captures
  --build-dir DIR   build dir (default: build)
  -h, --help        this help

ROM/ares-gated: SKIPs cleanly (exit 0) when the ROM, native binary, ares binary,
or a route's cached stock capture is absent. Emits sense_trace_<ts>.json.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --gate) GATE=1; shift ;;
        --route) ONLY_ROUTE="$2"; shift 2 ;;
        --self-test) SELF_TEST=1; shift ;;
        --binary) BINARY="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --ares-bin) ARES_BIN="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown arg: $1" >&2; usage; exit 2 ;;
    esac
done

[[ -n "$BINARY" ]] || BINARY="$(validation_binary_path "$BUILD_DIR")"
mkdir -p "$OUT_DIR" "$REPORTS_DIR"
trap 'rm -rf "$OUT_DIR"' EXIT

CAND_DIR="${OUT_DIR}/candidates"
mkdir -p "$CAND_DIR"

# ---------------------------------------------------------------------------
# Assemble candidate JSONs in $CAND_DIR into the lane report.
#   $1 = mode label (sense|gate|self-test)
#   $2 = ";;"-joined skip notes
# ---------------------------------------------------------------------------
harvest_and_report() {
    local mode="$1"
    local skipped_meta="$2"
    python3 - "$CAND_DIR" "$REPORT" "$mode" "$TS" "$skipped_meta" <<'PY'
import glob, json, os, sys, datetime
cand_dir, report_path, mode, ts, skipped_meta = sys.argv[1:6]
candidates = []
for p in sorted(glob.glob(os.path.join(cand_dir, "*.json"))):
    try:
        with open(p) as fh:
            c = json.load(fh)
        if c:
            candidates.append(c)
    except (OSError, ValueError):
        pass
report = {
    "lane": "S1",
    "mode": mode,
    "generated": ts,
    "inputs": {
        "routes_swept": len(glob.glob(os.path.join(cand_dir, "*.json"))),
        "candidates": len(candidates),
    },
    "candidates": candidates,
    "skipped": skipped_meta.split(";;") if skipped_meta else [],
}
os.makedirs(os.path.dirname(report_path), exist_ok=True)
with open(report_path, "w") as fh:
    json.dump(report, fh, indent=2)
    fh.write("\n")
print("S1 trace lane (%s): %d candidate(s) -> %s" % (mode, len(candidates), report_path))
for c in candidates:
    print("  [%s] %s" % (c.get("surface", "?"), c.get("title", "?")))
if skipped_meta:
    for s in skipped_meta.split(";;"):
        print("  SKIP:", s)
PY
}

# ---------------------------------------------------------------------------
# Self-test: prove candidate emission without a ROM/ares.
# ---------------------------------------------------------------------------
if [[ "$SELF_TEST" == "1" ]]; then
    syn="${OUT_DIR}/synthetic_report.json"
    cat > "$syn" <<'JSON'
{
  "status": "fail",
  "failure_kind": "divergence",
  "divergence_count": 2,
  "divergences": [
    {"key": 120, "baseline_frame": 120, "test_frame": 120,
     "diffs": ["pos.x baseline=14051.5 test=14060.2", "speed baseline=1.0 test=1.4"]}
  ]
}
JSON
    python3 tools/fidelity/sense_trace_candidates.py \
        --report "$syn" --route "selftest_route" \
        --comparator "compare_movement_trace.py" --surface "movement" \
        --out "${CAND_DIR}/selftest_route.json"
    harvest_and_report "self-test" ""
    # Assert a candidate really landed (belt-and-braces for the ctest lane).
    if ! python3 - "$REPORT" <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
assert r["inputs"]["candidates"] >= 1, "self-test emitted no candidate"
c = r["candidates"][0]
assert "pos.x" in c["divergent_fields"], c["divergent_fields"]
print("self-test OK: candidate with fields", c["divergent_fields"])
PY
    then
        echo "sense_trace_sweep: SELF-TEST FAILED" >&2
        exit 1
    fi
    exit 0
fi

# ---------------------------------------------------------------------------
# Route -> comparator mapping helpers.
# ---------------------------------------------------------------------------
route_json_field() {  # $1=route-json $2=key  -> prints value or empty
    python3 - "$1" "$2" <<'PY'
import json, sys
try:
    r = json.load(open(sys.argv[1]))
except (OSError, ValueError):
    sys.exit(0)
v = r.get(sys.argv[2], "")
if isinstance(v, bool):
    print("true" if v else "false")
elif v is not None:
    print(v)
PY
}

comparator_for_kind() {  # $1=compare_kind -> "script surface" or empty
    case "$1" in
        movement) echo "tools/compare_movement_trace.py movement" ;;
        intro)    echo "tools/compare_intro_trace.py intro" ;;
        glass)    echo "tools/compare_glass_trace.py glass" ;;
        *)        echo "" ;;   # visual handled by S2; unknown -> skip
    esac
}

# ---------------------------------------------------------------------------
# Prerequisite gate — SKIP whole lane cleanly if ROM/binary absent.
# ---------------------------------------------------------------------------
SKIPS=""
if [[ ! -f "$ROM" ]]; then
    SKIPS="ROM absent ($ROM) — S1 trace sweep requires the user's ROM"
fi
if [[ ! -x "$BINARY" ]]; then
    [[ -n "$SKIPS" ]] && SKIPS="${SKIPS};;"
    SKIPS="${SKIPS}native binary absent/not-executable ($BINARY)"
fi
if [[ -z "$ARES_BIN" ]]; then
    default_ares="build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares"
    [[ -x "$default_ares" ]] && ARES_BIN="$default_ares"
fi
if [[ -z "$ARES_BIN" || ! -x "$ARES_BIN" ]]; then
    [[ -n "$SKIPS" ]] && SKIPS="${SKIPS};;"
    SKIPS="${SKIPS}instrumented ares binary absent — stock captures unavailable"
fi
if [[ -n "$SKIPS" ]]; then
    echo "sense_trace_sweep: SKIP — $SKIPS" >&2
    harvest_and_report "$([[ $GATE == 1 ]] && echo gate || echo sense)" "$SKIPS"
    # Gate mode with nothing to check is a clean pass (no divergence observed).
    exit 0
fi

# ---------------------------------------------------------------------------
# Sweep.
# ---------------------------------------------------------------------------
DETERMINISM_ENV=(env -u GE007_DEBUG SDL_AUDIODRIVER=dummy GE007_MUTE=1
    GE007_DETERMINISTIC_STABLE_COUNT=1 GE007_NO_VSYNC=1 GE007_BACKGROUND=1
    GE007_NO_INPUT_GRAB=1)

DIVERGED=0
ROUTE_SKIPS=""
note_route_skip() { [[ -n "$ROUTE_SKIPS" ]] && ROUTE_SKIPS="${ROUTE_SKIPS};;"; ROUTE_SKIPS="${ROUTE_SKIPS}$1"; }

# capture_native <route> <native_trace_out> : returns 0 on success.
# (ROM+ares+binary already confirmed present above.)
capture_native() {
    local route="$1" out="$2"
    local env_lines
    env_lines="$(python3 tools/rom_oracle_route.py native-env "$route" 2>/dev/null)" || return 1
    local extra=()
    while IFS= read -r line; do [[ -n "$line" ]] && extra+=("$line"); done <<<"$env_lines"
    "${DETERMINISM_ENV[@]}" "${extra[@]}" "$BINARY" \
        --rom "$ROM" --deterministic \
        --oracle-trace "$out" >"${OUT_DIR}/${route}_native.log" 2>&1
}

run_route() {
    local rj="$1"
    local route; route="$(basename "$rj" .json)"
    [[ -n "$ONLY_ROUTE" && "$route" != "$ONLY_ROUTE" ]] && return 0

    if [[ "$GATE" == "1" ]]; then
        [[ "$(route_json_field "$rj" gate)" == "true" ]] || return 0
    fi

    local kind; kind="$(route_json_field "$rj" compare_kind)"
    [[ -z "$kind" ]] && kind="movement"

    # Latest cached stock capture for this route (git-ignored). The `|| true`
    # is load-bearing: when the route has NO cache dir at all, the unexpanded
    # glob makes `ls` fail; under macOS /bin/bash 3.2 + pipefail that failing
    # substitution silently aborted the whole sweep at this assignment (exit 1,
    # empty log — 2026-07-11 red verify on the first stock-less gate route),
    # so the rule-9 skip below was never reached.
    local stock
    stock="$(ls -1t "${CACHE_DIR}/${route}"/*/stock_trace.jsonl 2>/dev/null | head -1 || true)"
    if [[ -z "$stock" || ! -f "$stock" ]]; then
        note_route_skip "${route}: no cached stock capture (${CACHE_DIR}/${route}/<hash>/stock_trace.jsonl)"
        return 0
    fi

    local native="${OUT_DIR}/${route}_native.jsonl"
    if ! capture_native "$route" "$native"; then
        note_route_skip "${route}: native capture failed (see ${route}_native.log)"
        return 0
    fi

    # Primary comparator by kind, plus optional combat comparator.
    local specs=()
    local mapped; mapped="$(comparator_for_kind "$kind")"
    if [[ -n "$mapped" ]]; then
        specs+=("$mapped")
    else
        note_route_skip "${route}: compare_kind=${kind} not a trace surface (S2 pixel domain)"
    fi
    if [[ "$(route_json_field "$rj" compare_combat)" == "true" ]]; then
        specs+=("tools/compare_combat_trace.py combat")
    fi

    local spec script surface report cand
    for spec in "${specs[@]}"; do
        script="${spec%% *}"; surface="${spec##* }"
        report="${OUT_DIR}/${route}_${surface}_report.json"
        # P1h (Lane C, 2026-07-10 review): with no --align, every comparator
        # here defaults to `global` alignment. compare_combat_trace.py's own
        # --align help text calls `global`/`move` untrustworthy for combat
        # fields (FID-0062 -- native emits 1 record/game-frame, ares emits
        # ~2/advancing-tick, so index/global-keyed pairing skews the
        # timelines ~2x and invents/misattributes guard divergences); `tick`
        # is its one alignment mode designed to be trustworthy for the
        # combat_oracle surface. Scoped to the combat comparator only --
        # compare_movement_trace.py's --align choices don't even include
        # `tick` (would be an argparse error), and this task doesn't touch
        # the movement/intro/glass surfaces' own alignment default.
        local -a align_args=()
        [[ "$surface" == "combat" ]] && align_args=(--align tick)
        python3 "$script" --baseline "$stock" --test "$native" \
            "${align_args[@]}" --json-out "$report" \
            >"${OUT_DIR}/${route}_${surface}_cmp.log" 2>&1 || true
        [[ -f "$report" ]] || { note_route_skip "${route}/${surface}: comparator produced no report"; continue; }
        cand="${CAND_DIR}/${route}_${surface}.json"
        python3 tools/fidelity/sense_trace_candidates.py \
            --report "$report" --route "$route" --comparator "$(basename "$script")" \
            --surface "$surface" --out "$cand"
        if [[ -s "$cand" ]]; then
            DIVERGED=1
        fi
    done
}

for rj in "${ROUTES_DIR}"/*.json; do
    run_route "$rj"
done

harvest_and_report "$([[ $GATE == 1 ]] && echo gate || echo sense)" "$ROUTE_SKIPS"

if [[ "$GATE" == "1" && "$DIVERGED" == "1" ]]; then
    echo "sense_trace_sweep: GATE FAIL — gate route(s) diverged" >&2
    exit 1
fi
exit 0
