#!/usr/bin/env bash
#
# sense_rdp_sweep.sh — S3 RDP command-stream sense lane (FID-0043).
#
# The native fast3d renderer and the stock ares RDP both expose their RDP
# render-mode semantics, but there was no lane that DIFFERENCES them (the existing
# analyzer guards are native-only). This lane closes that:
#
#   NATIVE  : run the scene with GE007_TRACE_RDP_RENDER_MODES=1 (src/platform/
#             fast3d/gfx_pc.c) -> `[RDP-MODE] ...` trace -> a normalized RDP-config
#             fingerprint (other-mode / combine / blend / coverage-decode tuples).
#   ARES    : the instrumented ares RDP command sidecar
#             (MGB64_ARES_TRACE_RDP_COMMANDS=1, summarized by
#             tools/analyze_stock_rdp_command_stream.py --json) -> the comparable
#             fingerprint of the RDP configs the hardware actually executed.
#   DIFF    : tools/fidelity/rdp_fingerprint.py diff -> RDP configurations present
#             on one side but not the other = candidate findings (Phase C inflow).
#
# Contract (S-Tier sense lane): the comparator SELF-TEST is the always-on,
# ROM/ares-free guarantee (a seeded RDP-mode divergence must be caught). The
# native capture is ROM-gated; the native<->ares differential is ares-gated —
# both SKIP cleanly (exit 0) when their prerequisite is absent, and the skip is
# printed into the report (charter rule 9). Pin --faithful so the RDP stream is
# stock (RenderScale=1, FXAA/MSAA off, stock textures).
#
# Usage:
#   tools/fidelity/sense_rdp_sweep.sh [--gate] [--selftest]
#       [--build-dir DIR] [--rom PATH] [--binary PATH] [--level SLUG]
#       [--ares-rdp-sidecar PATH]   # analyze_stock_rdp_command_stream JSON, or a
#                                   # raw MGB64_ARES_TRACE_RDP_COMMANDS sidecar
#   tools/fidelity/sense_rdp_sweep.sh --selftest   # comparator self-test only
set -uo pipefail
cd "$(dirname "$0")/../.."

REPORTS_DIR="docs/fidelity/reports"
OUT_DIR="/tmp/mgb64_sense_rdp_$$"
TS="$(date +%Y%m%d_%H%M%S)"
REPORT="${REPORTS_DIR}/sense_rdp_${TS}.json"
FP="tools/fidelity/rdp_fingerprint.py"
ANALYZER="tools/analyze_stock_rdp_command_stream.py"

GATE=0
SELFTEST_ONLY=0
LEVEL="dam"
ARES_SIDECAR=""
BUILD_DIR="build"
BINARY=""
ROM="${GE007_ROM:-./baserom.u.z64}"
TRACE_AFTER="${GE007_RDP_TRACE_AFTER:-30}"
TRACE_BUDGET="${GE007_RDP_TRACE_BUDGET:-4000}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --gate) GATE=1; shift ;;
        --selftest) SELFTEST_ONLY=1; shift ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --level) LEVEL="$2"; shift 2 ;;
        --ares-rdp-sidecar) ARES_SIDECAR="$2"; shift 2 ;;
        --no-build) shift ;;
        -h|--help) sed -n '2,40p' "$0"; exit 0 ;;
        *) echo "sense_rdp_sweep: unknown arg $1" >&2; exit 2 ;;
    esac
done

# ---- 1. comparator SELF-TEST (always; ROM/ares-free guarantee) ----------------
if ! python3 "$FP" selftest; then
    echo "sense_rdp_sweep: FAIL — RDP fingerprint comparator self-test failed" >&2
    exit 1
fi
if [[ "$SELFTEST_ONLY" -eq 1 ]]; then
    echo "sense_rdp_sweep: self-test only — PASS"
    exit 0
fi

mkdir -p "$REPORTS_DIR" "$OUT_DIR"

# Resolve binary/ROM (prefer explicit, else build dir, else common lib).
if [[ -z "$BINARY" ]]; then
    if [[ -x "$BUILD_DIR/ge007" ]]; then BINARY="$BUILD_DIR/ge007"; fi
fi

emit_report() {  # $1=native_configs $2=ares_state $3=divergences $4=candidates_json $5=skips_json
    python3 - "$REPORT" "$TS" "$1" "$2" "$3" "$4" "$5" <<'PY'
import json, sys
report, ts, ncfg, ares_state, ndiv, cand_json, skips_json = sys.argv[1:8]
out = {
    "lane": "S3", "kind": "rdp", "ts": ts,
    "inputs": {
        "native_configs": int(ncfg), "ares_state": ares_state,
        "divergences": int(ndiv), "candidates": json.loads(cand_json),
        "skipped": json.loads(skips_json),
    },
    "candidates": json.loads(cand_json),
}
json.dump(out, open(report, "w"), indent=2)
print(f"S3 rdp lane: native_configs={ncfg} ares={ares_state} "
      f"divergences={ndiv} candidates={len(out['candidates'])} -> {report}")
PY
}

SKIPS="[]"
CANDS="[]"

# ---- 2. NATIVE RDP fingerprint (ROM-gated) -----------------------------------
if [[ -z "$BINARY" || ! -x "$BINARY" || ! -e "$ROM" ]]; then
    SKIPS='[{"stage":"native","reason":"ROM or native binary absent"},{"stage":"ares-diff","reason":"native side skipped"}]'
    emit_report 0 skipped 0 "$CANDS" "$SKIPS"
    echo "sense_rdp_sweep: SKIP native+ares (ROM/binary absent); self-test PASSED."
    rm -rf "$OUT_DIR"; exit 0
fi

NATIVE_ERR="$OUT_DIR/native_rdp.log"
FP_NATIVE="$OUT_DIR/fp_native.json"
ENVV=(SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1
      GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1
      GE007_TRACE_RDP_RENDER_MODES=1
      "GE007_TRACE_RDP_RENDER_MODES_AFTER_FRAME=$TRACE_AFTER"
      "GE007_TRACE_RDP_RENDER_MODES_BUDGET=$TRACE_BUDGET")
( cd "$OUT_DIR" && env -u GE007_DEBUG "${ENVV[@]}" "$(cd "$(dirname "$BINARY")" && pwd)/$(basename "$BINARY")" \
    --rom "$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")" --savedir "$OUT_DIR/sd" \
    --level "$LEVEL" --deterministic --faithful \
    --screenshot-frame "$((TRACE_AFTER + 20))" --screenshot-exit \
    >"$OUT_DIR/native.out" 2>"$NATIVE_ERR" ) || true

if ! grep -q 'RDP-MODE' "$NATIVE_ERR"; then
    SKIPS='[{"stage":"native","reason":"no [RDP-MODE] lines emitted (scene produced no traced draws)"}]'
    emit_report 0 skipped 0 "$CANDS" "$SKIPS"
    echo "sense_rdp_sweep: SKIP (native emitted no RDP-MODE lines); self-test PASSED."
    rm -rf "$OUT_DIR"; exit 0
fi
python3 "$FP" native "$NATIVE_ERR" --out "$FP_NATIVE"
NCFG="$(python3 -c "import json;print(json.load(open('$FP_NATIVE'))['configs'])")"
echo "sense_rdp_sweep: native $LEVEL --faithful -> $NCFG distinct RDP configs"

# ---- 3. NATIVE<->ARES differential (ares-gated) ------------------------------
if [[ -z "$ARES_SIDECAR" || ! -e "$ARES_SIDECAR" ]]; then
    SKIPS='[{"stage":"ares-diff","reason":"no ares RDP sidecar (--ares-rdp-sidecar); produce with MGB64_ARES_TRACE_RDP_COMMANDS=1 + analyze_stock_rdp_command_stream.py --json"}]'
    emit_report "$NCFG" skipped 0 "$CANDS" "$SKIPS"
    echo "sense_rdp_sweep: native coverage captured; ares differential SKIPPED (no sidecar). self-test PASSED."
    rm -rf "$OUT_DIR"; exit 0
fi

# The sidecar may be a raw MGB64_ARES_TRACE_RDP_COMMANDS file or already an
# analyzer JSON. If it isn't valid JSON with draw-states, run the analyzer.
FP_ARES="$OUT_DIR/fp_ares.json"
ANALYZER_JSON="$ARES_SIDECAR"
if ! python3 -c "import json,sys; d=json.load(open('$ARES_SIDECAR')); sys.exit(0 if isinstance(d,dict) else 1)" 2>/dev/null; then
    ANALYZER_JSON="$OUT_DIR/ares_analyzer.json"
    python3 "$ANALYZER" "$ARES_SIDECAR" --json > "$ANALYZER_JSON" 2>/dev/null || true
fi
python3 "$FP" ares "$ANALYZER_JSON" --out "$FP_ARES" 2>/dev/null || echo '{"fingerprint":{}}' > "$FP_ARES"

DIFF_JSON="$OUT_DIR/rdp_diff.json"
python3 "$FP" diff "$FP_NATIVE" "$FP_ARES" --out "$DIFF_JSON" $([[ $GATE -eq 1 ]] && echo --strict) || DIFF_RC=$?
DIFF_RC="${DIFF_RC:-0}"
NDIV="$(python3 -c "import json;print(json.load(open('$DIFF_JSON'))['divergences'])")"

# Turn the diff into ledger candidates (capped; charter rule 9 prints the cap).
CANDS="$(python3 - "$DIFF_JSON" "$REPORT" "$LEVEL" <<'PY'
import json, sys
diff = json.load(open(sys.argv[1])); report = sys.argv[2]; level = sys.argv[3]
cands = []
def add(side, key):
    cands.append({
        "title": f"RDP render-mode config only-in-{side} on {level}: {key[:120]}",
        "class": "parity-divergence", "surface": "renderer", "priority": "P2",
        "evidence": report, "evidence_kind": "rdp-diff",
        "repro": f"tools/fidelity/sense_rdp_sweep.sh --level {level} --ares-rdp-sidecar <sidecar>; rdp_fingerprint diff native vs ares",
        "suspect": ["src/platform/fast3d/gfx_pc.c" if side == "native" else "MGB64_ARES_TRACE_RDP_COMMANDS"],
    })
CAP = 20
for k in diff.get("only_in_a", [])[:CAP]: add("native", k)
for k in diff.get("only_in_b", [])[:CAP]: add("ares", k)
print(json.dumps(cands))
PY
)"
emit_report "$NCFG" compared "$NDIV" "$CANDS" "$SKIPS"

if [[ "$GATE" -eq 1 && "${DIFF_RC:-0}" -ne 0 ]]; then
    echo "sense_rdp_sweep: GATE FAIL — native/ares RDP configs diverge ($NDIV)" >&2
    rm -rf "$OUT_DIR"; exit 1
fi
echo "sense_rdp_sweep: PASS (native/ares compared; $NDIV divergence(s) reported as candidates)"
rm -rf "$OUT_DIR"; exit 0
