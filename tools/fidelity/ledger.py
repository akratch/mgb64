#!/usr/bin/env python3
"""Fidelity findings ledger — the loop's persistent, evidence-gated memory.

One JSON file per finding in docs/fidelity/ledger/FID-NNNN.json; a generated
human index docs/fidelity/LEDGER.md; a ctest guard (validate) that the ledger
is schema-valid and its transition history is internally consistent.

python3 stdlib only. See docs/design/FAITHFULNESS_S_TIER_PLAN.md Task 0.2 and
Appendix A for the contract and schema this implements.
"""
import argparse
import datetime
import difflib
import json
import os
import re
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
SCHEMA_PATH = os.path.join(SCRIPT_DIR, "ledger_schema.json")

# State machine (Task 0.2 Step 2). Linear forward chain + terminals.
FORWARD = ["discovered", "triaged", "root-caused", "fix-in-progress", "landed", "verified"]
TERMINALS = {"refuted", "waived", "documented"}
OPEN_STATUSES = {"discovered", "triaged", "root-caused", "fix-in-progress", "landed"}
EVIDENCE_KINDS = ["trace-diff", "pixel-diff", "rdp-diff", "asm-citation", "gate-log",
                  "doc-anchor", "counter-log", "audio-diff"]


def iso_now():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


# --- minimal JSON-schema-subset validator (draft 2020-12 subset used by our schema) ---
def _validate_node(schema, value, path, errors):
    t = schema.get("type")
    types = t if isinstance(t, list) else ([t] if t else [])
    if "null" in types and value is None:
        return
    if types and value is not None:
        ok = False
        for tt in types:
            if tt == "object" and isinstance(value, dict):
                ok = True
            elif tt == "array" and isinstance(value, list):
                ok = True
            elif tt == "string" and isinstance(value, str):
                ok = True
            elif tt == "integer" and isinstance(value, int) and not isinstance(value, bool):
                ok = True
            elif tt == "number" and isinstance(value, (int, float)) and not isinstance(value, bool):
                ok = True
            elif tt == "boolean" and isinstance(value, bool):
                ok = True
            elif tt == "null" and value is None:
                ok = True
        if not ok:
            errors.append(f"{path}: expected type {types}, got {type(value).__name__}")
            return
    if "enum" in schema and value not in schema["enum"]:
        errors.append(f"{path}: {value!r} not in enum {schema['enum']}")
    if "pattern" in schema and isinstance(value, str) and not re.search(schema["pattern"], value):
        errors.append(f"{path}: {value!r} does not match /{schema['pattern']}/")
    if "maxLength" in schema and isinstance(value, str) and len(value) > schema["maxLength"]:
        errors.append(f"{path}: length {len(value)} > maxLength {schema['maxLength']}")
    if isinstance(value, dict):
        for req in schema.get("required", []):
            if req not in value:
                errors.append(f"{path}: missing required property '{req}'")
        props = schema.get("properties", {})
        for k, sub in props.items():
            if k in value:
                _validate_node(sub, value[k], f"{path}.{k}", errors)
    if isinstance(value, list):
        if "minItems" in schema and len(value) < schema["minItems"]:
            errors.append(f"{path}: {len(value)} items < minItems {schema['minItems']}")
        item_schema = schema.get("items")
        if item_schema:
            for i, item in enumerate(value):
                _validate_node(item_schema, item, f"{path}[{i}]", errors)


def load_schema():
    with open(SCHEMA_PATH, encoding="utf-8") as f:
        return json.load(f)


def schema_errors(obj):
    errors = []
    _validate_node(load_schema(), obj, obj.get("id", "<entry>"), errors)
    return errors


# --- storage ---
def ledger_dir(args):
    return os.path.abspath(args.ledger_dir) if args.ledger_dir \
        else os.path.join(REPO_ROOT, "docs", "fidelity", "ledger")


def entry_path(ld, fid):
    return os.path.join(ld, f"{fid}.json")


def load_entry(ld, fid):
    with open(entry_path(ld, fid), encoding="utf-8") as f:
        return json.load(f)


def save_entry(ld, obj):
    os.makedirs(ld, exist_ok=True)
    with open(entry_path(ld, obj["id"]), "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=2)
        f.write("\n")


def load_all(ld):
    out = {}
    if not os.path.isdir(ld):
        return out
    for name in sorted(os.listdir(ld)):
        m = re.match(r"^(FID-\d{4})\.json$", name)
        if m:
            out[m.group(1)] = load_entry(ld, m.group(1))
    return out


def next_id(ld):
    existing = load_all(ld)
    n = 0
    for fid in existing:
        n = max(n, int(fid.split("-")[1]))
    return f"FID-{n + 1:04d}"


def escalated_fids(ld):
    esc = os.path.join(os.path.dirname(os.path.abspath(ld)), "ESCALATIONS.md")
    if not os.path.isfile(esc):
        return set()
    with open(esc, encoding="utf-8") as f:
        return set(re.findall(r"FID-\d{4}", f.read()))


def is_actionable(obj, all_by_id, escalated):
    if obj["status"] in ("waived", "verified", "documented", "refuted"):
        return False
    if obj["id"] in escalated:
        return False
    for dep in obj.get("blocked_on", []):
        d = all_by_id.get(dep)
        if d is None or d["status"] != "verified":
            return False
    return True


# --- commands ---
def cmd_new(args):
    ld = ledger_dir(args)
    fid = args.id or next_id(ld)
    status = args.status
    obj = {
        "id": fid, "title": args.title, "class": getattr(args, "class"),
        "surface": args.surface, "priority": args.priority, "status": status,
        "suspect": args.suspect or [], "repro": args.repro or "",
        "evidence": [{"kind": args.evidence_kind, "path": args.evidence}],
        "history": [{"ts": args.ts or iso_now(), "from": "", "to": status,
                     "evidence": args.evidence, "note": args.note or ""}],
    }
    if args.blocked_on:
        obj["blocked_on"] = args.blocked_on
    errs = schema_errors(obj)
    if errs:
        print("schema errors:\n  " + "\n  ".join(errs), file=sys.stderr)
        return 1
    save_entry(ld, obj)
    print(fid)
    return 0


def cmd_transition(args):
    ld = ledger_dir(args)
    obj = load_entry(ld, args.fid)
    src, dst = obj["status"], args.to
    if dst not in FORWARD and dst not in TERMINALS:
        print(f"invalid target status {dst!r}", file=sys.stderr)
        return 1
    if dst == "documented" and obj["class"] != "parity-divergence":
        print("documented is only valid for parity-divergence findings", file=sys.stderr)
        return 1
    promotion = dst not in ("refuted", "waived")
    if promotion and not args.evidence:
        print(f"transition to {dst} requires --evidence (evidence monopoly)", file=sys.stderr)
        return 1
    if dst in ("refuted", "waived") and not args.note:
        print(f"transition to {dst} requires --note", file=sys.stderr)
        return 1
    if dst == "waived" and not args.retest:
        print("transition to waived requires --retest (waiver.retest condition)", file=sys.stderr)
        return 1
    if args.evidence:
        obj.setdefault("evidence", []).append({"kind": args.evidence_kind, "path": args.evidence})
    if dst == "waived":
        obj["waiver"] = {"reason": args.note, "retest": args.retest}
    obj["status"] = dst
    obj["history"].append({"ts": iso_now(), "from": src, "to": dst,
                           "evidence": args.evidence or "", "note": args.note or ""})
    errs = schema_errors(obj)
    if errs:
        print("schema errors after transition:\n  " + "\n  ".join(errs), file=sys.stderr)
        return 1
    save_entry(ld, obj)
    print(f"{args.fid}: {src} -> {dst}")
    return 0


def cmd_list(args):
    ld = ledger_dir(args)
    entries = load_all(ld)
    escalated = escalated_fids(ld)
    rows = []
    for fid in sorted(entries):
        o = entries[fid]
        if args.status and o["status"] != args.status:
            continue
        if getattr(args, "class") and o["class"] != getattr(args, "class"):
            continue
        if args.priority and o["priority"] != args.priority:
            continue
        if args.actionable and not is_actionable(o, entries, escalated):
            continue
        rows.append(o)
    for o in rows:
        blocked = (" blocked_on=" + ",".join(o["blocked_on"])) if o.get("blocked_on") else ""
        print(f"{o['id']}  {o['priority']}  {o['status']:<16}  {o['class']:<20}  {o['title']}{blocked}")
    return 0


def cmd_dedupe_check(args):
    ld = ledger_dir(args)
    entries = load_all(ld)
    want_file = args.suspect.split(":")[0] if args.suspect else None
    cands = []
    for fid in sorted(entries):
        o = entries[fid]
        title_ratio = difflib.SequenceMatcher(None, args.title.lower(), o["title"].lower()).ratio()
        same_file = bool(want_file) and any(
            s.split(":")[0] == want_file for s in o.get("suspect", []))
        if title_ratio >= 0.6 or same_file:
            cands.append((fid, round(title_ratio, 2), same_file))
    for fid, ratio, same_file in cands:
        print(f"{fid}  title_ratio={ratio}  same_suspect_file={same_file}")
    if not cands:
        print("(no candidate duplicates)")
    return 0


def cmd_render(args):
    ld = ledger_dir(args)
    entries = load_all(ld)
    escalated = escalated_fids(ld)
    counts = {}
    for o in entries.values():
        counts[o["status"]] = counts.get(o["status"], 0) + 1
    lines = ["# Fidelity Ledger — generated index",
             "",
             "> Generated by `tools/fidelity/ledger.py render`. Do not hand-edit; "
             "the source of truth is `docs/fidelity/ledger/FID-NNNN.json`.",
             "",
             f"**{len(entries)} findings.** Status counts: "
             + ", ".join(f"{k}={counts[k]}" for k in sorted(counts)) + ".",
             "",
             "| ID | Pri | Status | Class | Surface | Actionable | Blocked-on | Title |",
             "|----|-----|--------|-------|---------|:---------:|-----------|-------|"]
    for fid in sorted(entries):
        o = entries[fid]
        act = "yes" if is_actionable(o, entries, escalated) else ""
        blocked = ", ".join(o.get("blocked_on", []))
        title = o["title"].replace("|", "\\|")
        lines.append(f"| {o['id']} | {o['priority']} | {o['status']} | {o['class']} | "
                     f"{o['surface']} | {act} | {blocked} | {title} |")
    lines.append("")
    out = os.path.join(os.path.dirname(os.path.abspath(ld)), "LEDGER.md")
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"wrote {out} ({len(entries)} findings)")
    return 0


def cmd_validate(args):
    ld = ledger_dir(args)
    entries = load_all(ld)
    violations = []
    for fid in sorted(entries):
        o = entries[fid]
        for e in schema_errors(o):
            violations.append(f"{fid}: schema: {e}")
        if fid != o.get("id"):
            violations.append(f"{fid}: id field {o.get('id')!r} != filename")
        hist = o.get("history", [])
        if not hist:
            violations.append(f"{fid}: empty history")
            continue
        if hist[0]["from"] != "":
            violations.append(f"{fid}: genesis history 'from' must be empty, got {hist[0]['from']!r}")
        for i in range(1, len(hist)):
            if hist[i]["from"] != hist[i - 1]["to"]:
                violations.append(
                    f"{fid}: history[{i}] from={hist[i]['from']!r} != prior to={hist[i-1]['to']!r}")
        if hist[-1]["to"] != o["status"]:
            violations.append(
                f"{fid}: status {o['status']!r} != last history.to {hist[-1]['to']!r}")
        for h in hist:
            try:
                datetime.datetime.strptime(h["ts"], "%Y-%m-%dT%H:%M:%SZ")
            except (ValueError, KeyError):
                violations.append(f"{fid}: bad ISO-8601 UTC ts {h.get('ts')!r}")
        for dep in o.get("blocked_on", []):
            if dep not in entries:
                violations.append(f"{fid}: blocked_on {dep} does not exist")
        if o["status"] == "waived" and not (o.get("waiver") and o["waiver"].get("retest")):
            violations.append(f"{fid}: waived without waiver.retest")
    if violations:
        print("LEDGER INVALID:", file=sys.stderr)
        for v in violations:
            print("  " + v, file=sys.stderr)
        return 1
    print(f"ledger OK: {len(entries)} findings valid")
    return 0


def cmd_stats(args):
    ld = ledger_dir(args)
    entries = load_all(ld)
    counts = {}
    for o in entries.values():
        counts[o["status"]] = counts.get(o["status"], 0) + 1
    print(f"total: {len(entries)}")
    for s in FORWARD + sorted(TERMINALS):
        if counts.get(s):
            print(f"  {s}: {counts[s]}")
    if args.assert_closed:
        open_n = sum(counts.get(s, 0) for s in OPEN_STATUSES)
        if open_n:
            print(f"NOT CLOSED: {open_n} finding(s) still in "
                  f"{sorted(OPEN_STATUSES)}", file=sys.stderr)
            return 1
        print("CLOSED: no open findings (S-tier criterion 8 holds)")
    return 0


def build_parser():
    p = argparse.ArgumentParser(description="Fidelity findings ledger")
    p.add_argument("--ledger-dir", default=None, help="override ledger dir (for testing)")
    sub = p.add_subparsers(dest="cmd", required=True)

    n = sub.add_parser("new")
    n.add_argument("--title", required=True)
    n.add_argument("--class", required=True, dest="class")
    n.add_argument("--surface", required=True)
    n.add_argument("--priority", required=True)
    n.add_argument("--evidence", required=True)
    n.add_argument("--evidence-kind", default="gate-log", choices=EVIDENCE_KINDS)
    n.add_argument("--repro", default="")
    n.add_argument("--suspect", action="append", default=[])
    n.add_argument("--blocked-on", action="append", default=[])
    n.add_argument("--status", default="discovered", choices=FORWARD + sorted(TERMINALS))
    n.add_argument("--note", default="")
    n.add_argument("--id", default=None, help="explicit id (seeding only)")
    n.add_argument("--ts", default=None, help="explicit genesis ts (seeding only)")
    n.set_defaults(func=cmd_new)

    t = sub.add_parser("transition")
    t.add_argument("fid")
    t.add_argument("--to", required=True)
    t.add_argument("--evidence", default=None)
    t.add_argument("--evidence-kind", default="gate-log", choices=EVIDENCE_KINDS)
    t.add_argument("--note", default="")
    t.add_argument("--retest", default="")
    t.set_defaults(func=cmd_transition)

    ls = sub.add_parser("list")
    ls.add_argument("--status", default=None)
    ls.add_argument("--class", default=None, dest="class")
    ls.add_argument("--priority", default=None)
    ls.add_argument("--actionable", action="store_true")
    ls.set_defaults(func=cmd_list)

    d = sub.add_parser("dedupe-check")
    d.add_argument("--title", required=True)
    d.add_argument("--suspect", default=None)
    d.set_defaults(func=cmd_dedupe_check)

    sub.add_parser("render").set_defaults(func=cmd_render)
    sub.add_parser("validate").set_defaults(func=cmd_validate)

    st = sub.add_parser("stats")
    st.add_argument("--assert-closed", action="store_true")
    st.set_defaults(func=cmd_stats)
    return p


def main(argv=None):
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
