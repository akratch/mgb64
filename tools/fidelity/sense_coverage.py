#!/usr/bin/env python3
"""S6 — Coverage critic (Fidelity Flywheel sense lane).

The lane that keeps the loop honest. Pure-static, cheap, runs every iteration:
no ROM, no build, no engine execution — just cross-references the in-tree
artifacts (routes, trace emitter, comparators, env-flag doc, ledger, verify
manifest/reports, ASM audit queue) for gaps.

Checks (each prints what it skipped — charter rule 9):
  1. route-kinds     : stages missing intro/traverse/completion oracle routes
                       (vs the Task 1.2 20x3 goal state).
  2. trace-fields    : JSONL trace-schema fields emitted by port_trace.c that no
                       comparator ever compares (instrumentation gap).
  3. flag-hygiene    : GE007_* fix-gates (FIX|LEGACY|NO_) referenced by no
                       tools/ lane (violates S-tier criterion 6).
  4. waiver-retest   : ledger `waived` entries whose retest names a now-verified
                       FID (the waiver should re-open).
  5. asm-queue-stale : ASM-audit-queue entries unreviewed too long (skipped with
                       a note if the queue does not exist yet — Task 2.4).
  6. manifest-skips  : verify-manifest gates that were skipped in the last verify
                       report (charter rule 9 visibility).
  7. doc-staleness   : a closed (verified/documented) FID whose cited source
                       still says TODO/unimplemented/stub near the anchor.

Output: docs/fidelity/reports/sense_coverage_<ts>.json
  {lane, generated, inputs, candidates:[{title,class,surface,evidence,repro,
   suspect,priority}], skipped:[...]}

See docs/design/FAITHFULNESS_S_TIER_PLAN.md Task 2.6.
"""
import datetime
import glob
import json
import os
import re
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
ROUTES_DIR = os.path.join(REPO_ROOT, "tools", "rom_oracle_routes")
COMPARATOR_GLOB = os.path.join(REPO_ROOT, "tools", "compare_*.py")
PORT_TRACE = os.path.join(REPO_ROOT, "src", "platform", "port_trace.c")
ENV_FLAGS = os.path.join(REPO_ROOT, "docs", "ENV_FLAGS.md")
LEDGER_DIR = os.path.join(REPO_ROOT, "docs", "fidelity", "ledger")
MANIFEST = os.path.join(REPO_ROOT, "docs", "fidelity", "verify_manifest.txt")
REPORTS_DIR = os.path.join(REPO_ROOT, "docs", "fidelity", "reports")
ASM_QUEUE = os.path.join(REPO_ROOT, "tools", "fidelity", "asm_audit_queue.json")
TOOLS_DIR = os.path.join(REPO_ROOT, "tools")

GOAL_KINDS = ("intro", "traverse", "completion")
# The route filename token → canonical route kind. Everything else is "other"
# (glass/probe/visual etc.) and does not satisfy a goal kind.
KIND_TOKENS = {
    "intro": "intro",
    "traverse": "traverse", "forward": "traverse", "strafe": "traverse",
    "matrix": "traverse", "stop": "traverse", "movement": "traverse",
    "completion": "completion", "complete": "completion",
    "objective": "completion", "mission": "completion",
}

candidates = []
skipped = []
inputs = {}


def add(title, klass, surface, evidence, repro, suspect, priority="P2"):
    candidates.append({
        "title": title, "class": klass, "surface": surface,
        "evidence": evidence, "evidence_kind": "doc-anchor",
        "repro": repro, "suspect": suspect, "priority": priority,
    })


# --- check 1: route kinds -------------------------------------------------
def check_route_kinds():
    if not os.path.isdir(ROUTES_DIR):
        skipped.append("route-kinds: tools/rom_oracle_routes/ missing")
        return
    files = [os.path.basename(p) for p in glob.glob(os.path.join(ROUTES_DIR, "*.json"))]
    inputs["route_files"] = len(files)
    # Derive the stage universe from any *_intro_* route (one per solo stage).
    stages = set()
    for f in files:
        m = re.match(r"^([a-z0-9]+)_intro_", f)
        if m:
            stages.add(m.group(1))
    inputs["stages_detected"] = sorted(stages)
    # Map present kinds per stage.
    present = {s: set() for s in stages}
    for f in files:
        name = f[:-5] if f.endswith(".json") else f
        parts = name.split("_")
        stage = parts[0]
        if stage not in present:
            continue
        for tok in parts[1:]:
            if tok in KIND_TOKENS:
                present[stage].add(KIND_TOKENS[tok])
    total_missing = 0
    for stage in sorted(stages):
        missing = [k for k in GOAL_KINDS if k not in present[stage]]
        if not missing:
            continue
        total_missing += len(missing)
        add(
            title=f"Stage '{stage}' missing oracle route kind(s): {', '.join(missing)}",
            klass="coverage-gap", surface="infra",
            evidence="tools/rom_oracle_routes/ (S6 route-kind census)",
            repro="python3 tools/fidelity/sense_coverage.py",
            suspect=[f"tools/rom_oracle_routes/{stage}_*"],
            priority="P1" if "traverse" in missing or "completion" in missing else "P2",
        )
    inputs["route_kind_missing_total"] = total_missing


# --- check 2: trace-schema fields never compared --------------------------
def check_trace_fields():
    if not os.path.isfile(PORT_TRACE):
        skipped.append("trace-fields: src/platform/port_trace.c missing")
        return
    with open(PORT_TRACE, encoding="utf-8", errors="replace") as f:
        src = f.read()
    # Scope to the primary per-frame state emitter(s): the block that starts
    # `{"f":%d,"p":%d,` and ends `"nan":%d}` — that is the movement/state trace
    # schema the movement/combat/intro comparators consume. Scanning the whole
    # file would also pull in the guard-oracle / mission-gate / stage-dump
    # sub-emitters, which are separate schemas (over-reporting).
    blocks = []
    for m in re.finditer(r'\{\\"f\\":%d,\\"p\\":%d,', src):
        end = src.find(r'\"nan\":%d}', m.start())
        if end != -1:
            blocks.append(src[m.start():end + 24])
    key_text = "".join(blocks) if blocks else src
    if not blocks:
        skipped.append("trace-fields: primary emitter block not located; "
                       "scanned whole file (over-broad)")
    # Keys appear in C format strings as \"key\": — capture the identifiers.
    keys = set(re.findall(r'\\"([a-z_][a-z0-9_]*)\\":', key_text))
    inputs["trace_schema_fields"] = len(keys)
    if not keys:
        skipped.append("trace-fields: no keys parsed from emitter (format changed?)")
        return
    comparator_text = ""
    comps = glob.glob(COMPARATOR_GLOB)
    inputs["comparators"] = len(comps)
    for c in comps:
        with open(c, encoding="utf-8", errors="replace") as f:
            comparator_text += f.read()
    uncompared = sorted(
        k for k in keys
        if not re.search(r'["\']' + re.escape(k) + r'["\']', comparator_text)
    )
    # Structural/wrapper keys that are containers, not comparable leaf values.
    NOISE = {"f", "p"}
    uncompared = [k for k in uncompared if k not in NOISE]
    inputs["trace_fields_uncompared"] = len(uncompared)
    if uncompared:
        preview = ", ".join(uncompared[:24])
        more = f" (+{len(uncompared) - 24} more)" if len(uncompared) > 24 else ""
        add(
            title=f"{len(uncompared)} trace-schema field(s) emitted by port_trace.c "
                  f"are compared by no comparator: {preview}{more}",
            klass="instrumentation-gap", surface="infra",
            evidence="src/platform/port_trace.c vs tools/compare_*.py (S6 field census)",
            repro="python3 tools/fidelity/sense_coverage.py",
            suspect=["src/platform/port_trace.c:7693"],
            priority="P2",
        )


# --- check 3: env-flag hygiene --------------------------------------------
def check_flag_hygiene():
    if not os.path.isfile(ENV_FLAGS):
        skipped.append("flag-hygiene: docs/ENV_FLAGS.md missing")
        return
    with open(ENV_FLAGS, encoding="utf-8", errors="replace") as f:
        flags = set(re.findall(r"GE007_[A-Z0-9_]+", f.read()))
    fix_gates = sorted(
        fl for fl in flags
        if re.search(r"(FIX|LEGACY|NO_)", fl)
    )
    inputs["fix_gate_flags"] = len(fix_gates)
    # Concatenate every tools/ script's text (the "lane references" universe).
    tool_text = ""
    for root, _dirs, fnames in os.walk(TOOLS_DIR):
        # skip the routes corpus (data, not lanes) and caches
        if os.sep + "rom_oracle_routes" in root:
            continue
        for fn in fnames:
            if fn.endswith((".sh", ".py")):
                try:
                    with open(os.path.join(root, fn), encoding="utf-8",
                              errors="replace") as f:
                        tool_text += f.read()
                except OSError:
                    pass
    unref = [fl for fl in fix_gates if fl not in tool_text]
    inputs["fix_gate_flags_unreferenced"] = len(unref)
    if unref:
        preview = ", ".join(unref[:20])
        more = f" (+{len(unref) - 20} more)" if len(unref) > 20 else ""
        add(
            title=f"{len(unref)} GE007 fix-gate flag(s) referenced by no tools/ lane "
                  f"(no A/B proof lane, S-tier crit.6): {preview}{more}",
            klass="instrumentation-gap", surface="infra",
            evidence="docs/ENV_FLAGS.md vs tools/*.sh,*.py (S6 flag census)",
            repro="python3 tools/fidelity/sense_coverage.py",
            suspect=["docs/ENV_FLAGS.md"],
            priority="P2",
        )


# --- ledger helpers -------------------------------------------------------
def load_ledger():
    entries = {}
    if not os.path.isdir(LEDGER_DIR):
        return entries
    for p in sorted(glob.glob(os.path.join(LEDGER_DIR, "FID-*.json"))):
        try:
            with open(p, encoding="utf-8") as f:
                o = json.load(f)
            entries[o["id"]] = o
        except (OSError, ValueError, KeyError):
            pass
    return entries


# --- check 4: waiver retest now satisfied ---------------------------------
def check_waiver_retest(entries):
    verified = {fid for fid, o in entries.items()
                if o.get("status") in ("verified", "documented")}
    n = 0
    for fid, o in entries.items():
        if o.get("status") != "waived":
            continue
        n += 1
        retest = (o.get("waiver") or {}).get("retest", "")
        for dep in set(re.findall(r"FID-\d{4}", retest)):
            if dep in verified:
                add(
                    title=f"{fid} waiver retest condition satisfied "
                          f"({dep} now {entries[dep]['status']}) — re-open for retest",
                    klass="coverage-gap", surface=o.get("surface", "infra"),
                    evidence=f"docs/fidelity/ledger/{fid}.json (waiver.retest)",
                    repro="python3 tools/fidelity/sense_coverage.py",
                    suspect=[f"docs/fidelity/ledger/{fid}.json"],
                    priority=o.get("priority", "P2"),
                )
                break
    inputs["waived_entries"] = n


# --- check 5: ASM audit queue staleness -----------------------------------
def check_asm_queue():
    if not os.path.isfile(ASM_QUEUE):
        skipped.append("asm-queue-stale: tools/fidelity/asm_audit_queue.json "
                       "absent (Task 2.4 not built yet)")
        return
    try:
        with open(ASM_QUEUE, encoding="utf-8") as f:
            q = json.load(f)
    except (OSError, ValueError):
        skipped.append("asm-queue-stale: queue unparseable")
        return
    entries = q if isinstance(q, list) else q.get("entries", [])
    # Rank <= 50 unreviewed entries are the staleness surface (high-risk head).
    stale = [e for i, e in enumerate(entries)
             if i < 50 and e.get("status", "unreviewed") == "unreviewed"]
    inputs["asm_queue_entries"] = len(entries)
    inputs["asm_queue_stale_head"] = len(stale)
    if stale:
        add(
            title=f"{len(stale)} top-ranked ASM-audit entries still unreviewed "
                  f"(dispatch-risk head of queue)",
            klass="coverage-gap", surface="sim",
            evidence="tools/fidelity/asm_audit_queue.json (S6 staleness scan)",
            repro="python3 tools/fidelity/asm_audit.py next",
            suspect=["tools/fidelity/asm_audit_queue.json"],
            priority="P1",
        )


# --- check 6: verify-manifest gates skipped last run ----------------------
def check_manifest_skips():
    reports = glob.glob(os.path.join(REPORTS_DIR, "verify_*.json"))
    if not reports:
        skipped.append("manifest-skips: no verify_<sha>.json report yet")
        return
    newest = max(reports, key=os.path.getmtime)
    try:
        with open(newest, encoding="utf-8") as f:
            rep = json.load(f)
    except (OSError, ValueError):
        skipped.append("manifest-skips: newest verify report unparseable")
        return
    skips = [g.get("name", "?") for g in rep.get("gates", [])
             if g.get("status") == "skip"]
    inputs["verify_report"] = os.path.basename(newest)
    inputs["verify_gates_skipped"] = len(skips)
    if skips:
        add(
            title=f"{len(skips)} verify gate(s) skipped in last run "
                  f"({os.path.basename(newest)}): {', '.join(skips[:8])}",
            klass="coverage-gap", surface="infra",
            evidence=os.path.relpath(newest, REPO_ROOT),
            repro="tools/fidelity/verify_all.sh",
            suspect=["docs/fidelity/verify_manifest.txt"],
            priority="P2",
        )


# --- check 7: doc staleness on closed findings ----------------------------
STALE_RE = re.compile(r"\b(TODO|FIXME|XXX|unimplemented|not implemented|"
                      r"PORT_FIXME|stubbed?)\b", re.IGNORECASE)


def check_doc_staleness(entries):
    checked = 0
    for fid, o in entries.items():
        if o.get("status") not in ("verified", "documented"):
            continue
        for sus in o.get("suspect", []):
            m = re.match(r"^([^\s:]+):(\d+)", sus)
            if not m:
                continue
            rel, line = m.group(1), int(m.group(2))
            path = os.path.join(REPO_ROOT, rel)
            if not os.path.isfile(path):
                continue
            checked += 1
            try:
                with open(path, encoding="utf-8", errors="replace") as f:
                    src_lines = f.readlines()
            except OSError:
                continue
            lo, hi = max(0, line - 6), min(len(src_lines), line + 5)
            window = "".join(src_lines[lo:hi])
            if STALE_RE.search(window):
                add(
                    title=f"{fid} is {o['status']} but its anchor {sus} still reads "
                          f"TODO/unimplemented/stub — doc-vs-code drift",
                    klass="coverage-gap", surface=o.get("surface", "infra"),
                    evidence=f"{rel}:{line} (S6 doc-staleness scan)",
                    repro="python3 tools/fidelity/sense_coverage.py",
                    suspect=[sus],
                    priority="P3",
                )
    inputs["doc_staleness_anchors_checked"] = checked


def main():
    entries = load_ledger()
    check_route_kinds()
    check_trace_fields()
    check_flag_hygiene()
    check_waiver_retest(entries)
    check_asm_queue()
    check_manifest_skips()
    check_doc_staleness(entries)

    ts = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    report = {
        "lane": "S6",
        "generated": datetime.datetime.now(datetime.timezone.utc)
            .strftime("%Y-%m-%dT%H:%M:%SZ"),
        "inputs": inputs,
        "candidates": candidates,
        "skipped": skipped,
    }
    os.makedirs(REPORTS_DIR, exist_ok=True)
    out = os.path.join(REPORTS_DIR, f"sense_coverage_{ts}.json")
    with open(out, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)
        f.write("\n")
    print(f"S6 coverage critic: {len(candidates)} candidate(s), "
          f"{len(skipped)} skip(s) -> {os.path.relpath(out, REPO_ROOT)}")
    for c in candidates:
        print(f"  [{c['class']}/{c['priority']}] {c['title']}")
    for s in skipped:
        print(f"  (skip) {s}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
