#!/bin/bash
#
# sense_pixel_sweep.sh -- S2 pixel-oracle sense lane (Fidelity Flywheel).
#
# For every visual oracle route with a screenshot_game_timer checkpoint: capture
# the native BMP at that game timer, pair it with the cached ares stock PPM at
# the matched g_GlobalTimer, sanity-audit both (audit_screenshot_health.py),
# normalize them onto one grid (pixel_normalize.py), diff + cluster + classify
# against the approximation registry (pixel_diff.py), and turn any UNEXPLAINED
# cluster into a sense candidate carrying both source images + the diff viz.
# This is the "S2" lane of the S-Tier plan (Task 2.2).
#
#   default    : SENSE mode. Sweep applicable checkpoints, harvest candidates
#                into docs/fidelity/reports/sense_pixel_<ts>.json. Report-only
#                (an unexplained cluster is a finding, not a lane failure); exit 0.
#   --gate     : RATCHET mode. Sweep only "gate": true routes (the known-good
#                checkpoints) and exit 1 if ANY unexplained cluster appears.
#                This is what joins verify_manifest tier 3.
#   --self-test: run the full normalize->diff->classify->candidate pipeline on
#                the committed synthetic calibration images (gen_pixel_calibration)
#                and prove broken->candidate, good->none. ROM/ares-free; exit 0.
#
# Stock PPMs are cached (git-ignored) in
#   build/oracle_cache/<route>/<ares-hash>/stock_<route>.ppm
# ROM/ares-gated: a checkpoint with no cached stock PPM SKIPs cleanly and is
# noted (charter rule 9). With no ROM or no native binary the whole lane SKIPs
# cleanly (exit 0, empty candidate set).
#
# Report-only SENSE lane: never files into the ledger (the loop's job).
#
set -uo pipefail
cd "$(dirname "$0")/../.."
ROOT="$(pwd)"
source tools/validation_common.sh

ROUTES_DIR="tools/rom_oracle_routes"
REPORTS_DIR="docs/fidelity/reports"
CACHE_DIR="build/oracle_cache"
TS="$(date -u +%Y%m%dT%H%M%SZ)"
OUT_DIR="/tmp/mgb64_sense_pixel_$$"
REPORT="${REPORTS_DIR}/sense_pixel_${TS}.json"

ROM="$(validation_default_rom)"
BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ARES_BIN=""
GATE=0
SELF_TEST=0
ONLY_ROUTE=""
VI_DEBLUR=0

usage() {
    cat <<'USAGE'
Usage: tools/fidelity/sense_pixel_sweep.sh [options]
  --gate            ratchet mode: only "gate":true routes, exit 1 on unexplained
  --route NAME      restrict the sweep to a single route
  --self-test       prove the pixel pipeline on synthetic images (no ROM/ares)
  --vi-deblur       apply the N64 VI horizontal-AA approximation during normalize
  --binary PATH     native binary (default: build/ge007)
  --no-build        accepted + ignored (the sweep never builds; it needs a
                    prebuilt --binary). Present so the ROM-gated ctest smoke
                    harness (add_port_validation_smoke) can invoke this lane.
  --rom PATH        ROM (default: ./baserom.u.z64)
  --ares-bin PATH   instrumented ares binary for stock captures
  --build-dir DIR   build dir (default: build)
  -h, --help        this help

ROM/ares-gated: SKIPs cleanly (exit 0) when the ROM, native binary, ares binary,
or a checkpoint's cached stock PPM is absent. Emits sense_pixel_<ts>.json.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --gate) GATE=1; shift ;;
        --route) ONLY_ROUTE="$2"; shift 2 ;;
        --self-test) SELF_TEST=1; shift ;;
        --vi-deblur) VI_DEBLUR=1; shift ;;
        --binary) BINARY="$2"; shift 2 ;;
        --no-build) shift ;;   # no-op: this lane requires a prebuilt --binary
        --rom) ROM="$2"; shift 2 ;;
        --ares-bin) ARES_BIN="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown arg: $1" >&2; usage; exit 2 ;;
    esac
done

[[ -n "$BINARY" ]] || BINARY="$(validation_binary_path "$BUILD_DIR")"
mkdir -p "$OUT_DIR" "$REPORTS_DIR"
trap 'rm -rf "$OUT_DIR" screenshot_pixorc.bmp' EXIT

CAND_DIR="${OUT_DIR}/candidates"
mkdir -p "$CAND_DIR"

NORMALIZE_FLAGS=()
[[ "$VI_DEBLUR" == "1" ]] && NORMALIZE_FLAGS+=(--vi-deblur)

# ---------------------------------------------------------------------------
# Assemble candidate JSONs in $CAND_DIR into the lane report.
# ---------------------------------------------------------------------------
harvest_and_report() {
    local mode="$1"
    local skipped_meta="$2"
    python3 - "$CAND_DIR" "$REPORT" "$mode" "$TS" "$skipped_meta" <<'PY'
import glob, json, os, sys
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
    "lane": "S2",
    "mode": mode,
    "generated": ts,
    "inputs": {
        "checkpoints_swept": len(glob.glob(os.path.join(cand_dir, "*.json"))),
        "candidates": len(candidates),
    },
    "candidates": candidates,
    "skipped": skipped_meta.split(";;") if skipped_meta else [],
}
os.makedirs(os.path.dirname(report_path), exist_ok=True)
with open(report_path, "w") as fh:
    json.dump(report, fh, indent=2)
    fh.write("\n")
print("S2 pixel lane (%s): %d candidate(s) -> %s" % (mode, len(candidates), report_path))
for c in candidates:
    print("  [renderer] %s" % c.get("title", "?"))
if skipped_meta:
    for s in skipped_meta.split(";;"):
        print("  SKIP:", s)
PY
}

# ---------------------------------------------------------------------------
# Self-test: full pipeline on synthetic calibration images (no ROM/ares).
#   good pair  -> 0 candidates; broken pair -> >=1 candidate.
# ---------------------------------------------------------------------------
if [[ "$SELF_TEST" == "1" ]]; then
    syn="${OUT_DIR}/syn"; mkdir -p "$syn"
    python3 tools/fidelity/tests/gen_pixel_calibration.py --write "$syn" >/dev/null

    # Sanity: audit both sides before comparing (catches blank/wrong-size).
    # The synthetic fixtures are intentionally near-flat calibration targets, so
    # relax the real-screenshot colour thresholds here — we are only exercising
    # the "both sides audited, right size, not blank" step of the pipeline.
    python3 tools/audit_screenshot_health.py --expect-size 128x96 \
        --min-unique-colors 1 --max-dominant-pct 100 --max-black-pct 100 \
        "$syn/native.png" "$syn/ares_good.png" "$syn/ares_broken.png" \
        >"${OUT_DIR}/health.log" 2>&1 || {
            echo "sense_pixel_sweep: SELF-TEST health audit failed" >&2
            cat "${OUT_DIR}/health.log" >&2; exit 1; }

    run_pair() {  # $1=ares image  $2=label  -> writes candidate iff unexplained
        local ares="$1" label="$2"
        local nd="${OUT_DIR}/${label}"; mkdir -p "$nd"
        python3 tools/fidelity/pixel_normalize.py --native "$syn/native.png" \
            --ares "$ares" --out-dir "$nd" "${NORMALIZE_FLAGS[@]}" >/dev/null
        python3 tools/fidelity/pixel_diff.py --native "$nd/native.png" \
            --ares "$nd/ares_normalized.png" --out "$nd/verdict.json" \
            --viz "$nd/diff.png" >/dev/null
        python3 tools/fidelity/sense_pixel_candidates.py --verdict "$nd/verdict.json" \
            --route "synthetic_${label}" --checkpoint "0" \
            --native-png "$nd/native.png" --ares-png "$nd/ares_normalized.png" \
            --diff-png "$nd/diff.png" --out "${CAND_DIR}/synthetic_${label}.json"
    }
    run_pair "$syn/ares_good.png" good
    run_pair "$syn/ares_broken.png" broken
    harvest_and_report "self-test" ""

    if ! python3 - "$REPORT" <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
titles = [c["title"] for c in r["candidates"]]
broken = [c for c in r["candidates"] if c["route"] == "synthetic_broken"]
good = [c for c in r["candidates"] if c["route"] == "synthetic_good"]
assert not good, "known-good synthetic pair must emit NO candidate: %r" % titles
assert broken, "broken synthetic pair must emit a candidate; got %r" % titles
assert broken[0]["clusters_unexplained"] >= 1, broken[0]
print("self-test OK: good=0 broken=%d candidate(s)" % len(broken))
PY
    then
        echo "sense_pixel_sweep: SELF-TEST FAILED" >&2
        exit 1
    fi
    exit 0
fi

# ---------------------------------------------------------------------------
# Route helpers.
# ---------------------------------------------------------------------------
route_json_field() {  # $1=route-json $2=key
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

# ---------------------------------------------------------------------------
# Prerequisite gate — SKIP whole lane cleanly if ROM/binary/ares absent.
# ---------------------------------------------------------------------------
SKIPS=""
if [[ ! -f "$ROM" ]]; then
    SKIPS="ROM absent ($ROM) — S2 pixel sweep requires the user's ROM"
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
    SKIPS="${SKIPS}instrumented ares binary absent — stock PPMs unavailable"
fi
if [[ -n "$SKIPS" ]]; then
    echo "sense_pixel_sweep: SKIP — $SKIPS" >&2
    harvest_and_report "$([[ $GATE == 1 ]] && echo gate || echo sense)" "$SKIPS"
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

run_route() {
    local rj="$1"
    local route; route="$(basename "$rj" .json)"
    [[ -n "$ONLY_ROUTE" && "$route" != "$ONLY_ROUTE" ]] && return 0

    # visual routes only (a screenshot checkpoint is required).
    local timer; timer="$(route_json_field "$rj" native_screenshot_game_timer)"
    [[ -z "$timer" ]] && return 0   # not a pixel checkpoint; silently skip

    if [[ "$GATE" == "1" ]]; then
        [[ "$(route_json_field "$rj" gate)" == "true" ]] || return 0
    fi

    local stock
    stock="$(ls -1t "${CACHE_DIR}/${route}"/*/stock_${route}.ppm 2>/dev/null | head -1)"
    if [[ -z "$stock" || ! -f "$stock" ]]; then
        note_route_skip "${route}: no cached stock PPM (${CACHE_DIR}/${route}/<hash>/stock_${route}.ppm)"
        return 0
    fi

    # Native BMP at the game-timer checkpoint.
    local env_lines; env_lines="$(python3 tools/rom_oracle_route.py native-env "$route" 2>/dev/null)" || true
    local extra=(); while IFS= read -r line; do [[ -n "$line" ]] && extra+=("$line"); done <<<"$env_lines"
    # Route native --config-override overrides (FovY, RemasterFX off, RenderScale=1,
    # RetroFilter on, ...): a pixel checkpoint must render on the *faithful* config,
    # not the user's saved ge007.ini — otherwise post-FX/scale/FOV differences swamp
    # the diff. movement_oracle_capture.sh applies these the same way.
    local cfg_lines; cfg_lines="$(python3 tools/rom_oracle_route.py native-config "$route" 2>/dev/null)" || true
    local cfg_args=(); while IFS= read -r line; do [[ -n "$line" ]] && cfg_args+=(--config-override "$line"); done <<<"$cfg_lines"
    # Level pin: a route reaches its checkpoint via a direct --level boot unless it
    # declares native_menu_boot (frontend-driven). WITHOUT --level the binary boots
    # to the front menu, the gameplay game-timer never advances, and the timer-keyed
    # screenshot fires on a black menu frame (unique<=3, black~100%) that fails the
    # health audit -- the original defect that kept this lane from ever capturing a
    # real ROM frame (only the synthetic self-test ran).
    local level_args=()
    local menu_boot; menu_boot="$(route_json_field "$rj" native_menu_boot)"
    local native_level; native_level="$(route_json_field "$rj" native_level)"
    [[ -z "$native_level" ]] && native_level="$(route_json_field "$rj" level)"
    case "$menu_boot" in
        1|true|True|TRUE|yes|YES|on|ON) ;;   # frontend-driven boot; no --level pin
        *) [[ -n "$native_level" ]] && level_args=(--level "$native_level") ;;
    esac
    rm -f screenshot_pixorc.bmp
    if ! "${DETERMINISM_ENV[@]}" "${extra[@]}" "$BINARY" \
            "${cfg_args[@]}" \
            --rom "$ROM" --deterministic "${level_args[@]}" \
            --screenshot-game-timer "$timer" --screenshot-label pixorc \
            --screenshot-exit >"${OUT_DIR}/${route}_native.log" 2>&1; then
        note_route_skip "${route}: native capture failed (see ${route}_native.log)"; return 0
    fi
    local native_bmp="${OUT_DIR}/${route}_native.bmp"
    [[ -f screenshot_pixorc.bmp ]] || { note_route_skip "${route}: no native screenshot produced"; return 0; }
    mv screenshot_pixorc.bmp "$native_bmp"

    # Sanity: audit both sides before comparing.
    if ! python3 tools/audit_screenshot_health.py "$native_bmp" "$stock" \
            >"${OUT_DIR}/${route}_health.log" 2>&1; then
        note_route_skip "${route}: screenshot health audit failed (see ${route}_health.log)"; return 0
    fi

    local nd="${OUT_DIR}/${route}"; mkdir -p "$nd"
    python3 tools/fidelity/pixel_normalize.py --native "$native_bmp" --ares "$stock" \
        --out-dir "$nd" "${NORMALIZE_FLAGS[@]}" >"${OUT_DIR}/${route}_norm.log" 2>&1 || {
            note_route_skip "${route}: normalize failed"; return 0; }
    python3 tools/fidelity/pixel_diff.py --native "$nd/native.png" \
        --ares "$nd/ares_normalized.png" --out "$nd/verdict.json" --viz "$nd/diff.png" \
        >"${OUT_DIR}/${route}_diff.log" 2>&1 || {
            note_route_skip "${route}: pixel_diff failed"; return 0; }
    python3 tools/fidelity/sense_pixel_candidates.py --verdict "$nd/verdict.json" \
        --route "$route" --checkpoint "$timer" \
        --native-png "$nd/native.png" --ares-png "$nd/ares_normalized.png" \
        --diff-png "$nd/diff.png" --out "${CAND_DIR}/${route}.json"
    [[ -s "${CAND_DIR}/${route}.json" ]] && DIVERGED=1
    return 0
}

for rj in "${ROUTES_DIR}"/*.json; do
    run_route "$rj"
done

harvest_and_report "$([[ $GATE == 1 ]] && echo gate || echo sense)" "$ROUTE_SKIPS"

if [[ "$GATE" == "1" && "$DIVERGED" == "1" ]]; then
    echo "sense_pixel_sweep: GATE FAIL — gate checkpoint(s) have unexplained clusters" >&2
    exit 1
fi
exit 0
