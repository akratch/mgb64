#!/bin/bash
#
# sense_soak.sh -- S4 soak/fuzz/sanitizer sense lane (Fidelity Flywheel).
#
# Wraps the existing endurance tools into ONE candidate-emitting lane:
#   * tools/soak_stability.sh      (crash/NaN/DL-resolve, audited --max-crashes 0)
#   * dyn-allocator stress         (GE007_DYN_STRESS_LIMIT sweep across a few
#                                   levels, watching g_dyn_overflow_count +
#                                   [RENDER-HEALTH] lines)
#   * tools/asan_smoke.sh          (ASan/UBSan pass, if present)
#   * tools/uncap_purity_gate.sh   (0-tick purity fuzz, --quick)
#
# Harvest rule: any new [RENDER-HEALTH] / watchdog / ASan / counter anomaly
# signature (deduped by a normalized signature hash) becomes a candidate with
# the captured log + a repro command.
#
# ROM-gated: SKIPs cleanly (exit 0, empty candidate set, skip noted) when the
# ROM or the native binary is absent — same contract as the wrapped tools.
#
# Output: docs/fidelity/reports/sense_soak_<ts>.json
#   {lane, generated, inputs, candidates:[...], skipped:[...]}
#
# See docs/design/FAITHFULNESS_S_TIER_PLAN.md Task 2.5.
set -uo pipefail       # NOTE: no -e; sub-lanes are expected to fail on anomaly
                       # and we capture that instead of aborting.
cd "$(dirname "$0")/../.."
ROOT="$(pwd)"
source tools/validation_common.sh

REPORTS_DIR="docs/fidelity/reports"
TS="$(date -u +%Y%m%dT%H%M%SZ)"
OUT_DIR="/tmp/mgb64_sense_soak_$$"
REPORT="${REPORTS_DIR}/sense_soak_${TS}.json"

ROM="$(validation_default_rom)"
BINARY=""
BUILD_DIR="$(validation_default_build_dir)"
QUICK=0
NO_BUILD=0
FRAMES=1800
LEVELS="33 41 27"
DYN_LEVELS="33 41 27 22 26"
DYN_LIMITS="64 256 1024"
TIMEOUT_SECONDS=600
SELF_TEST=0

usage() {
    cat <<'USAGE'
Usage: tools/fidelity/sense_soak.sh [options]

Options:
  --quick             reduced matrix (2 levels, fewer frames, skip ASan rebuild)
  --no-build          reuse existing binaries (pass through to sub-lanes)
  --level LIST        soak level list, quoted (default: "33 41 27")
  --frames N          soak length in frames (default: 1800)
  --rom PATH          ROM path (default: ./baserom.u.z64)
  --binary PATH       native binary (default: build/ge007)
  --timeout SECONDS   per-sub-lane timeout (default: 600)
  --self-test         harvest a synthetic anomaly log (no ROM) to prove the
                      candidate emitter; writes a report and exits
  -h, --help

Emits docs/fidelity/reports/sense_soak_<ts>.json. ROM-gated: SKIPs cleanly
(exit 0) when the ROM or binary is absent. Report-only: this is a SENSE lane,
not a gate — it never fails the loop; anomalies become ledger candidates.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --quick) QUICK=1; shift ;;
        --no-build) NO_BUILD=1; shift ;;
        --level) LEVELS="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        --self-test) SELF_TEST=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

[[ -n "$BINARY" ]] || BINARY="$(validation_binary_path "$BUILD_DIR")"
mkdir -p "$OUT_DIR" "$REPORTS_DIR"

if [[ "$QUICK" == "1" ]]; then
    LEVELS="33 41"
    DYN_LEVELS="33 41"
    DYN_LIMITS="64 1024"
    FRAMES=600
fi

# ---------------------------------------------------------------------------
# Harvest logs in $OUT_DIR into a candidate report (python: signature dedupe).
# $1 = space-separated list of "lane:ran|skipped" tokens for the inputs block.
# ---------------------------------------------------------------------------
harvest_and_report() {
    local lanes_meta="$1"
    local skipped_meta="$2"
    python3 - "$OUT_DIR" "$REPORT" "$lanes_meta" "$skipped_meta" <<'PY'
import glob, hashlib, json, os, re, sys, datetime

out_dir, report_path, lanes_meta, skipped_meta = \
    sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]

# Anomaly signature patterns: (regex, class-guess, surface, priority, kind-tag)
PATTERNS = [
    (re.compile(r"\[RENDER-HEALTH\]"),                 "port-defect", "renderer", "P2", "render-health"),
    (re.compile(r"watchdog", re.I),                    "port-defect", "renderer", "P1", "watchdog"),
    (re.compile(r"AddressSanitizer|SUMMARY:\s*AddressSanitizer"), "port-defect", "infra", "P1", "asan"),
    (re.compile(r"runtime error:|UndefinedBehaviorSanitizer"),    "port-defect", "infra", "P1", "ubsan"),
    (re.compile(r"g_dyn_overflow|dyn[_ ]overflow|allocator (overflow|alias)", re.I),
                                                       "port-defect", "renderer", "P1", "dyn-overflow"),
    (re.compile(r"\bFAIL\b.*(crash|recover|NaN|bad command|DL-resolve|resolve)", re.I),
                                                       "port-defect", "sim", "P1", "soak-audit-fail"),
    (re.compile(r"purity gate .*RED|SIM-HASH MISMATCH|purity.*mismatch", re.I),
                                                       "port-defect", "sim", "P0", "purity-mismatch"),
]

# Signature normalization: drop volatile tokens so the same defect dedupes.
def signature(line, tag):
    s = line.strip()
    s = re.sub(r"0x[0-9a-fA-F]+", "0xADDR", s)
    s = re.sub(r"/tmp/[^\s:]+", "/tmp/PATH", s)
    s = re.sub(r"\b\d+\b", "N", s)
    return tag + "|" + hashlib.sha1(s.encode("utf-8", "replace")).hexdigest()[:12]

seen = {}
for log in sorted(glob.glob(os.path.join(out_dir, "*.log"))):
    lane = os.path.basename(log)[:-4]
    try:
        with open(log, encoding="utf-8", errors="replace") as f:
            for ln in f:
                for rx, klass, surface, prio, tag in PATTERNS:
                    if rx.search(ln):
                        sig = signature(ln, tag)
                        if sig not in seen:
                            seen[sig] = {"tag": tag, "class": klass, "surface": surface,
                                         "priority": prio, "sample": ln.strip()[:240],
                                         "lane": lane, "log": log, "count": 0}
                        seen[sig]["count"] += 1
                        break
    except OSError:
        pass

candidates = []
for sig, a in sorted(seen.items()):
    candidates.append({
        "title": f"S4 soak anomaly [{a['tag']}] in {a['lane']}: {a['sample'][:120]}",
        "class": a["class"], "surface": a["surface"], "priority": a["priority"],
        "evidence": os.path.relpath(a["log"], os.getcwd())
                    if a["log"].startswith(os.getcwd()) else a["log"],
        "evidence_kind": "counter-log",
        "repro": f"tools/fidelity/sense_soak.sh  (signature {sig}, {a['count']} hit(s))",
        "suspect": [],
        "signature": sig,
    })

report = {
    "lane": "S4",
    "generated": datetime.datetime.now(datetime.timezone.utc)
        .strftime("%Y-%m-%dT%H:%M:%SZ"),
    "inputs": {
        "sub_lanes": lanes_meta.split() if lanes_meta else [],
        "logs_scanned": len(glob.glob(os.path.join(out_dir, "*.log"))),
        "unique_anomaly_signatures": len(candidates),
    },
    "candidates": candidates,
    "skipped": skipped_meta.split(";;") if skipped_meta else [],
}
os.makedirs(os.path.dirname(report_path), exist_ok=True)
with open(report_path, "w", encoding="utf-8") as f:
    json.dump(report, f, indent=2)
    f.write("\n")
print(f"S4 soak lane: {len(candidates)} anomaly candidate(s) -> {report_path}")
for c in candidates:
    print(f"  [{c['class']}/{c['priority']}] {c['title']}")
PY
}

# ---------------------------------------------------------------------------
# Self-test: prove the harvester emits candidates without a ROM.
# ---------------------------------------------------------------------------
if [[ "$SELF_TEST" == "1" ]]; then
    cat > "${OUT_DIR}/selftest.log" <<'LOG'
[RENDER-HEALTH] room 51 draw-only walk admitted 0 rooms (frame 812)
watchdog: frame stall exceeded 2000ms on level 27
==12345==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x602000000010
runtime error: signed integer overflow: 2147483647 + 1 cannot be represented in type 'int'
[TRACE-AUDIT] FAIL crash recovery observed (g_crashRecoveryCount=1)
g_dyn_overflow_count=3 at frame 1400 (GE007_DYN_STRESS_LIMIT=64)
LOG
    harvest_and_report "self-test" ""
    rm -rf "$OUT_DIR"
    exit 0
fi

# ---------------------------------------------------------------------------
# ROM/binary gate — SKIP cleanly if prerequisites absent.
# ---------------------------------------------------------------------------
SKIPS=""
if [[ ! -f "$ROM" ]]; then
    SKIPS="ROM absent ($ROM) — soak lane requires the user's ROM"
fi
if [[ ! -x "$BINARY" ]]; then
    [[ -n "$SKIPS" ]] && SKIPS="${SKIPS};;"
    SKIPS="${SKIPS}native binary absent/not-executable ($BINARY)"
fi
if [[ -n "$SKIPS" ]]; then
    echo "sense_soak: SKIP — $SKIPS" >&2
    harvest_and_report "" "$SKIPS"
    rm -rf "$OUT_DIR"
    exit 0
fi

# ---------------------------------------------------------------------------
# Run the wrapped endurance lanes, capturing output for harvest.
# ---------------------------------------------------------------------------
LANES_META=""
SKIP_META=""
NB_ARG=(); [[ "$NO_BUILD" == "1" || -x "$BINARY" ]] && NB_ARG=(--no-build)

echo "sense_soak: soak_stability baseline (levels: $LEVELS, frames: $FRAMES)" >&2
tools/soak_stability.sh --level "$LEVELS" --frames "$FRAMES" \
    --binary "$BINARY" --rom "$ROM" --timeout "$TIMEOUT_SECONDS" "${NB_ARG[@]}" \
    > "${OUT_DIR}/soak_stability.log" 2>&1
LANES_META="${LANES_META}soak_stability(rc=$?) "

echo "sense_soak: dyn-allocator stress sweep (limits: $DYN_LIMITS)" >&2
for lim in $DYN_LIMITS; do
    GE007_DYN_STRESS_LIMIT="$lim" \
    tools/soak_stability.sh --level "$DYN_LEVELS" --frames "$FRAMES" \
        --binary "$BINARY" --rom "$ROM" --timeout "$TIMEOUT_SECONDS" --no-build \
        > "${OUT_DIR}/dyn_stress_lim${lim}.log" 2>&1
    LANES_META="${LANES_META}dyn_stress_lim${lim}(rc=$?) "
done

if [[ "$QUICK" != "1" && -f tools/asan_smoke.sh ]]; then
    echo "sense_soak: ASan/UBSan smoke" >&2
    tools/asan_smoke.sh --rom "$ROM" --timeout "$TIMEOUT_SECONDS" \
        > "${OUT_DIR}/asan_smoke.log" 2>&1
    LANES_META="${LANES_META}asan_smoke(rc=$?) "
else
    SKIP_META="asan_smoke skipped (${QUICK:+--quick}${QUICK:-not-present})"
fi

if [[ -f tools/uncap_purity_gate.sh ]]; then
    echo "sense_soak: 0-tick purity fuzz (--quick)" >&2
    BIN="$BINARY" ROM="$ROM" tools/uncap_purity_gate.sh --quick \
        > "${OUT_DIR}/uncap_purity.log" 2>&1
    LANES_META="${LANES_META}uncap_purity(rc=$?) "
else
    [[ -n "$SKIP_META" ]] && SKIP_META="${SKIP_META};;"
    SKIP_META="${SKIP_META}uncap_purity_gate.sh not present"
fi

harvest_and_report "$LANES_META" "$SKIP_META"
rm -rf "$OUT_DIR"
exit 0
