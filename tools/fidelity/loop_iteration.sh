#!/bin/bash
#
# loop_iteration.sh -- mechanical driver for one Fidelity Flywheel iteration.
#
# The *decisions* (which phase to run) stay with the agent per the scheduling
# policy in docs/fidelity/CHARTER.md §policy. This script provides the four
# mechanical primitives the agent shells out to:
#
#   status               emit docs/fidelity/reports/loop_status.json (the
#                        one-read snapshot the agent uses to pick a phase)
#   sense <lane>         run a sense lane, auto-file its deduped candidates
#   verify [--tier N]    wrapper over tools/fidelity/verify_all.sh
#   checkpoint "<msg>"   assert clean tier-1 gates, commit, regenerate LEDGER.md
#
# Cross-session cursor: docs/fidelity/reports/loop_state.json (COMMITTED) holds
# the iteration counter and per-lane last-run iteration so the loop resumes
# across sessions/machines.
#
# See docs/design/FAITHFULNESS_S_TIER_PLAN.md Task 3.1.
set -euo pipefail
cd "$(dirname "$0")/../.."
REPO_ROOT="$(pwd)"

FID_DIR="tools/fidelity"
REPORTS_DIR="docs/fidelity/reports"
STATE_JSON="${REPORTS_DIR}/loop_state.json"
STATUS_JSON="${REPORTS_DIR}/loop_status.json"
LEDGER_PY="python3 ${FID_DIR}/ledger.py"

# Lane registry: S<N> -> lane script (relative to repo root).
# Scripts not yet landed (built by sibling Phase-1 tasks) are handled
# gracefully by `sense` (it notes the missing dependency and no-ops).
lane_script() {
    case "$1" in
        S1|trace)    echo "tools/fidelity/sense_trace_sweep.sh" ;;
        S2|pixel)    echo "tools/fidelity/sense_pixel_sweep.sh" ;;
        S3|rdp)      echo "tools/fidelity/sense_rdp_sweep.sh" ;;
        S4|soak)     echo "tools/fidelity/sense_soak.sh" ;;
        S5|asm)      echo "tools/fidelity/asm_audit.py" ;;
        S6|coverage) echo "tools/fidelity/sense_coverage.py" ;;
        *)           echo "" ;;
    esac
}

lane_id() {
    case "$1" in
        S1|trace)    echo "S1" ;;
        S2|pixel)    echo "S2" ;;
        S3|rdp)      echo "S3" ;;
        S4|soak)     echo "S4" ;;
        S5|asm)      echo "S5" ;;
        S6|coverage) echo "S6" ;;
        *)           echo "" ;;
    esac
}

# lane report basename token, so we can find the newest sense_<token>_<ts>.json
lane_report_token() {
    case "$1" in
        S1) echo "trace" ;;
        S2) echo "pixel" ;;
        S3) echo "rdp" ;;
        S4) echo "soak" ;;
        S5) echo "asm" ;;
        S6) echo "coverage" ;;
    esac
}

usage() {
    cat <<'USAGE'
Usage: tools/fidelity/loop_iteration.sh <command> [args]

Commands:
  status                     emit docs/fidelity/reports/loop_status.json + print it
  sense <S1..S6|name>        run a sense lane, auto-file deduped candidates
  verify [--tier N]          run tools/fidelity/verify_all.sh (pass-through args)
  checkpoint "<message>"     tier-1 gate, commit changed surface, regen LEDGER.md

Lanes: S1/trace S2/pixel S3/rdp S4/soak S5/asm S6/coverage
USAGE
}

# ---------------------------------------------------------------------------
# status
# ---------------------------------------------------------------------------
cmd_status() {
    mkdir -p "${REPORTS_DIR}"
    python3 - "$REPO_ROOT" "$STATE_JSON" "$STATUS_JSON" <<'PY'
import datetime, json, os, subprocess, sys

repo, state_path, status_path = sys.argv[1], sys.argv[2], sys.argv[3]
os.chdir(repo)
sys.path.insert(0, os.path.join(repo, "tools", "fidelity"))
import ledger  # reuse the ledger state machine + actionable logic

LANES = ["S1", "S2", "S3", "S4", "S5", "S6"]
PRIO_RANK = {"P0": 0, "P1": 1, "P2": 2, "P3": 3}


def head_sha():
    try:
        return subprocess.check_output(["git", "rev-parse", "HEAD"],
                                       text=True).strip()
    except Exception:
        return "unknown"


def ledger_stats():
    """Shell out to ledger.py stats and parse into a dict (charter: reuse tool)."""
    out = subprocess.run([sys.executable, "tools/fidelity/ledger.py", "stats"],
                         capture_output=True, text=True)
    counts = {}
    total = 0
    for line in out.stdout.splitlines():
        line = line.strip()
        if line.startswith("total:"):
            total = int(line.split(":", 1)[1])
        elif ":" in line:
            k, v = line.split(":", 1)
            try:
                counts[k.strip()] = int(v)
            except ValueError:
                pass
    return {"total": total, "by_status": counts}


def latest_verify():
    rdir = "docs/fidelity/reports"
    reports = []
    if os.path.isdir(rdir):
        for n in os.listdir(rdir):
            if n.startswith("verify_") and n.endswith(".json"):
                reports.append(os.path.join(rdir, n))
    if not reports:
        return {"verdict": "never"}
    newest = max(reports, key=lambda p: os.path.getmtime(p))
    try:
        with open(newest, encoding="utf-8") as f:
            rep = json.load(f)
    except Exception:
        return {"verdict": "never"}
    started = rep.get("started")
    age_s = None
    if started:
        try:
            t0 = datetime.datetime.strptime(started, "%Y-%m-%dT%H:%M:%SZ") \
                .replace(tzinfo=datetime.timezone.utc)
            age_s = int((datetime.datetime.now(datetime.timezone.utc) - t0)
                        .total_seconds())
        except Exception:
            age_s = None
    return {"verdict": rep.get("verdict", "unknown"),
            "sha": rep.get("sha"),
            "report": newest,
            "started": started,
            "age_seconds": age_s,
            "skipped": [g["name"] for g in rep.get("gates", [])
                        if g.get("status") == "skip"]}


def load_state():
    if os.path.isfile(state_path):
        with open(state_path, encoding="utf-8") as f:
            return json.load(f)
    return {"iteration": 0, "lanes": {L: 0 for L in LANES}}


def actionable_top(n=10):
    ld = os.path.join("docs", "fidelity", "ledger")
    entries = ledger.load_all(ld)
    escalated = ledger.escalated_fids(ld)
    rows = []
    for fid in sorted(entries):
        o = entries[fid]
        if ledger.is_actionable(o, entries, escalated):
            hist = o.get("history", [])
            genesis = hist[0]["ts"] if hist else "9999"
            rows.append((PRIO_RANK.get(o["priority"], 9), genesis, o))
    rows.sort(key=lambda r: (r[0], r[1]))
    return [{"id": o["id"], "priority": o["priority"], "status": o["status"],
             "class": o["class"], "surface": o["surface"], "title": o["title"],
             "blocked_on": o.get("blocked_on", [])}
            for _, _, o in rows[:n]]


def escalations_open():
    esc = os.path.join("docs", "fidelity", "ESCALATIONS.md")
    if not os.path.isfile(esc):
        return 0
    with open(esc, encoding="utf-8") as f:
        text = f.read()
    # entries are markdown headings; fall back to unique FID mentions
    n = sum(1 for ln in text.splitlines() if ln.startswith("## "))
    if n == 0:
        import re
        n = len(set(re.findall(r"FID-\d{4}", text)))
    return n


state = load_state()
it = state.get("iteration", 0)
lanes_last = state.get("lanes", {})
staleness = {L: it - int(lanes_last.get(L, 0)) for L in LANES}

status = {
    "generated": datetime.datetime.now(datetime.timezone.utc)
        .strftime("%Y-%m-%dT%H:%M:%SZ"),
    "iteration": it,
    "head_sha": head_sha(),
    "verify": latest_verify(),
    "ledger_stats": ledger_stats(),
    "actionable": actionable_top(10),
    "lane_staleness": staleness,
    "escalations_open": escalations_open(),
}

os.makedirs(os.path.dirname(status_path), exist_ok=True)
with open(status_path, "w", encoding="utf-8") as f:
    json.dump(status, f, indent=2)
    f.write("\n")
print(json.dumps(status, indent=2))
PY
}

# ---------------------------------------------------------------------------
# sense <lane> -- run lane, auto-file deduped candidates
# ---------------------------------------------------------------------------
cmd_sense() {
    local lane_arg="${1:-}"
    [[ -n "$lane_arg" ]] || { echo "sense: missing lane" >&2; usage >&2; exit 2; }
    local lid script token
    lid="$(lane_id "$lane_arg")"
    script="$(lane_script "$lane_arg")"
    [[ -n "$lid" ]] || { echo "sense: unknown lane '$lane_arg'" >&2; exit 2; }
    token="$(lane_report_token "$lid")"

    if [[ ! -f "$script" ]]; then
        echo "sense[$lid]: lane script '$script' not present yet " \
             "(built by a sibling Phase-1 task) — dependency noted, no-op." >&2
        _bump_lane "$lid"
        return 0
    fi

    echo "sense[$lid]: running $script ..." >&2
    # Lanes self-skip cleanly (exit 0) when a prerequisite (ROM/ares) is absent.
    if [[ "$script" == *.py ]]; then
        python3 "$script" || true
    else
        bash "$script" || true
    fi

    # Newest report for this lane.
    local report
    report="$(ls -t ${REPORTS_DIR}/sense_${token}_*.json 2>/dev/null | head -1 || true)"
    if [[ -z "$report" ]]; then
        echo "sense[$lid]: no report emitted (lane skipped or produced nothing)." >&2
        _bump_lane "$lid"
        return 0
    fi
    echo "sense[$lid]: report $report" >&2

    # For each candidate: dedupe-check, file if new.
    python3 - "$report" <<'PY'
import json, subprocess, sys
report = sys.argv[1]
with open(report, encoding="utf-8") as f:
    data = json.load(f)
cands = data.get("candidates", [])
filed = dup = 0
for c in cands:
    title = c.get("title", "")
    suspects = c.get("suspect", []) or []
    suspect0 = suspects[0] if suspects else None
    args = ["python3", "tools/fidelity/ledger.py", "dedupe-check", "--title", title]
    if suspect0:
        args += ["--suspect", suspect0]
    out = subprocess.run(args, capture_output=True, text=True).stdout
    if "(no candidate duplicates)" not in out:
        dup += 1
        continue
    new = ["python3", "tools/fidelity/ledger.py", "new",
           "--title", title,
           "--class", c.get("class", "coverage-gap"),
           "--surface", c.get("surface", "infra"),
           "--priority", c.get("priority", "P2"),
           "--evidence", c.get("evidence", report),
           "--evidence-kind", c.get("evidence_kind", "gate-log"),
           "--repro", c.get("repro", "")]
    for s in suspects:
        new += ["--suspect", s]
    r = subprocess.run(new, capture_output=True, text=True)
    if r.returncode == 0:
        filed += 1
        print(f"filed {r.stdout.strip()}  {title}")
    else:
        print(f"FAILED to file: {title}\n{r.stderr}", file=sys.stderr)
print(f"sense: filed={filed} dup={dup} total_candidates={len(cands)}")
PY
    _bump_lane "$lid"
}

# record that lane <lid> ran at the current iteration (cross-session cursor)
_bump_lane() {
    local lid="$1"
    python3 - "$STATE_JSON" "$lid" <<'PY'
import datetime, json, os, sys
path, lid = sys.argv[1], sys.argv[2]
state = {"iteration": 0, "lanes": {L: 0 for L in ["S1","S2","S3","S4","S5","S6"]}}
if os.path.isfile(path):
    with open(path, encoding="utf-8") as f:
        state = json.load(f)
state.setdefault("lanes", {})[lid] = state.get("iteration", 0)
state["updated"] = datetime.datetime.now(datetime.timezone.utc)\
    .strftime("%Y-%m-%dT%H:%M:%SZ")
os.makedirs(os.path.dirname(path), exist_ok=True)
with open(path, "w", encoding="utf-8") as f:
    json.dump(state, f, indent=2)
    f.write("\n")
PY
}

# ---------------------------------------------------------------------------
# verify -- wrapper over verify_all.sh (built by Task 0.3, sibling agent)
# ---------------------------------------------------------------------------
cmd_verify() {
    local va="tools/fidelity/verify_all.sh"
    if [[ ! -x "$va" && ! -f "$va" ]]; then
        echo "verify: ${va} not present yet (Task 0.3 dependency)." >&2
        echo "verify: falling back to the tier-1 static gates available in-tree." >&2
        _fallback_tier1
        return $?
    fi
    bash "$va" "$@"
}

# Minimal tier-1 fallback when verify_all.sh has not merged yet.
_fallback_tier1() {
    local rc=0
    echo "[fallback] ledger validate" >&2
    ${LEDGER_PY} validate || rc=1
    if [[ -x scripts/ci/check_sim_render_separation.sh ]]; then
        echo "[fallback] check_sim_render_separation.sh" >&2
        scripts/ci/check_sim_render_separation.sh || rc=1
    fi
    if [[ -x scripts/ci/check_timing_lock.sh ]]; then
        echo "[fallback] check_timing_lock.sh" >&2
        scripts/ci/check_timing_lock.sh || rc=1
    fi
    return $rc
}

# ---------------------------------------------------------------------------
# checkpoint "<msg>" -- gate, commit changed surface, regen LEDGER.md, bump it
# ---------------------------------------------------------------------------
cmd_checkpoint() {
    local msg="${1:-}"
    [[ -n "$msg" ]] || { echo "checkpoint: missing commit message" >&2; exit 2; }

    echo "checkpoint: asserting tier-1 gates ..." >&2
    if [[ -f tools/fidelity/verify_all.sh ]]; then
        bash tools/fidelity/verify_all.sh --tier 1 \
            || { echo "checkpoint: tier-1 verify RED — refusing to commit." >&2; exit 1; }
    else
        _fallback_tier1 \
            || { echo "checkpoint: tier-1 fallback gate RED — refusing to commit." >&2; exit 1; }
    fi

    # Regenerate the human index so it never drifts from the JSON source.
    ${LEDGER_PY} render >/dev/null

    # Bump the iteration counter (one checkpoint == one completed iteration).
    python3 - "$STATE_JSON" <<'PY'
import datetime, json, os, sys
path = sys.argv[1]
state = {"iteration": 0, "lanes": {L: 0 for L in ["S1","S2","S3","S4","S5","S6"]}}
if os.path.isfile(path):
    with open(path, encoding="utf-8") as f:
        state = json.load(f)
state["iteration"] = state.get("iteration", 0) + 1
state["updated"] = datetime.datetime.now(datetime.timezone.utc)\
    .strftime("%Y-%m-%dT%H:%M:%SZ")
os.makedirs(os.path.dirname(path), exist_ok=True)
with open(path, "w", encoding="utf-8") as f:
    json.dump(state, f, indent=2)
    f.write("\n")
PY

    git add -A
    if git diff --cached --quiet; then
        echo "checkpoint: nothing staged to commit." >&2
        return 0
    fi
    git commit -m "$msg" >&2
    echo "checkpoint: committed \"$msg\"" >&2
}

# ---------------------------------------------------------------------------
main() {
    local cmd="${1:-}"
    shift || true
    case "$cmd" in
        status)     cmd_status ;;
        sense)      cmd_sense "$@" ;;
        verify)     cmd_verify "$@" ;;
        checkpoint) cmd_checkpoint "$@" ;;
        -h|--help|"") usage ;;
        *) echo "unknown command: $cmd" >&2; usage >&2; exit 2 ;;
    esac
}

main "$@"
