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
import subprocess
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

# C5 (2026-07-10 review): the legal transition edge-set. "verified" plus the 3 branch
# terminals have NO outgoing edges (a finding is done) except through --reopen, which
# records why the flywheel is walking a closed/completed finding back. Everything else
# (discovered..landed) is "open": it may step exactly one state forward in FORWARD, or
# branch sideways into documented/refuted/waived, without --reopen. A *backward* move
# among open states, or reopening a closed one, requires --reopen + --note. Skip-ahead
# (more than one forward step) is illegal outright -- no override, ever.
CLOSED_STATUSES = TERMINALS | {"verified"}


def legal_edges_from(src):
    """Statuses reachable from `src` without --reopen. Empty for closed (terminal)
    statuses -- see CLOSED_STATUSES."""
    if src in CLOSED_STATUSES:
        return set()
    edges = set(TERMINALS)
    edges.add(FORWARD[FORWARD.index(src) + 1])
    return edges


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
    if dst == src:
        print(f"transition {src!r} -> {dst!r}: not a legal edge (self-transition; "
              "nothing would change)", file=sys.stderr)
        return 1

    # C5: enforce the legal transition edge-set (module docstring / CLOSED_STATUSES).
    if dst not in legal_edges_from(src):
        # Only two shapes of "illegal" edge are ever recoverable, and only with
        # --reopen: (a) src is closed (verified/documented/refuted/waived) with no
        # ordinary outgoing edge, or (b) dst sits strictly earlier than src in the
        # open (non-terminal) FORWARD chain. Anything else -- skipping ahead past
        # the very next state -- is illegal outright; the flywheel walks findings
        # back when evidence contradicts, it never fast-forwards them.
        backward = (src not in CLOSED_STATUSES and dst in FORWARD
                    and FORWARD.index(dst) < FORWARD.index(src))
        reopenable = (src in CLOSED_STATUSES) or backward
        if not reopenable:
            print(f"transition {src!r} -> {dst!r} is not a legal edge "
                  f"(lifecycle: {' -> '.join(FORWARD)}; documented/refuted/waived "
                  "branch from any open state)", file=sys.stderr)
            return 1
        if not args.reopen:
            reason = ("a terminal status with no outgoing edges" if src in CLOSED_STATUSES
                      else "a backward move among open lifecycle states")
            print(f"transition {src!r} -> {dst!r}: {src!r} is {reason} -- retry with "
                  "--reopen --note '<why>' to record the flywheel walking this finding "
                  "back (only when evidence contradicts the prior transition)",
                  file=sys.stderr)
            return 1
        if dst in CLOSED_STATUSES:
            print(f"--reopen must land on an open lifecycle state (one of "
                  f"{FORWARD[:-1]}), not a terminal/closed status; got {dst!r}",
                  file=sys.stderr)
            return 1
        if not args.note:
            print("transition with --reopen requires --note (the reopen rationale)",
                  file=sys.stderr)
            return 1

    if dst == "documented" and obj["class"] != "parity-divergence":
        print("documented is only valid for parity-divergence findings", file=sys.stderr)
        return 1
    promotion = dst not in ("refuted", "waived")
    if promotion and not args.evidence:
        print(f"transition to {dst} requires --evidence (evidence monopoly)", file=sys.stderr)
        return 1
    if args.evidence:
        # M6 (2026-07-10 review): validate at transition time, not just at the next
        # `validate`/ctest run -- a bad --evidence string used to leave the ledger
        # invalid in the gap between the transition and whoever next runs `validate`.
        ok, detail = evidence_target_ok(args.evidence, REPO_ROOT, _known_ctest_names(REPO_ROOT),
                                         _basename_index(REPO_ROOT))
        if not ok:
            print(f"--evidence {args.evidence!r} invalid: {detail}", file=sys.stderr)
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
    hist_entry = {"ts": iso_now(), "from": src, "to": dst,
                  "evidence": args.evidence or "", "note": args.note or ""}
    if args.reopen:
        hist_entry["reopen"] = True
    obj["history"].append(hist_entry)
    errs = schema_errors(obj)
    if errs:
        print("schema errors after transition:\n  " + "\n  ".join(errs), file=sys.stderr)
        return 1
    save_entry(ld, obj)
    print(f"{args.fid}: {src} -> {dst}" + (" [reopen]" if args.reopen else ""))
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


def render_ledger_text(ld):
    """Build the LEDGER.md index text from the JSON source of truth.

    Pure (no I/O) so both `render` (write) and `render --check` (drift guard)
    share one generator — the rendered index can never disagree with the check.
    """
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
    return "\n".join(lines), len(entries)


# --- C5: evidence-path existence check (validate) ---
# docs/fidelity/reports/ is entirely .gitignore'd (see its own .gitignore: "*" plus two
# tracked exceptions) -- per-run gate captures live there and are never committed, so a
# citation into it can never be resolved from a fresh checkout. Treat the prefix itself as
# the recognized marker instead of requiring the (deliberately untracked) file to exist.
REPORTS_PREFIX = "docs/fidelity/reports/"


def _known_ctest_names(repo_root):
    """Test names registered in CMakeLists.txt, for validating 'ctest:<name>' evidence
    shorthand. Regex over the CMake source rather than invoking cmake/ctest, so `validate`
    stays usable from a bare checkout with no configured build/ directory."""
    path = os.path.join(repo_root, "CMakeLists.txt")
    names = set()
    if not os.path.isfile(path):
        return names
    with open(path, encoding="utf-8") as f:
        text = f.read()
    names.update(re.findall(r"add_test\(\s*NAME\s+([A-Za-z0-9_]+)", text))
    names.update(re.findall(r"add_port_validation_smoke\(\s*([A-Za-z0-9_]+)", text))
    return names


def _basename_index(repo_root):
    """basename -> [repo-relative paths] for every TRACKED file (git ls-files), so a
    bare-filename asm-citation shorthand (e.g. 'unk_0A1DA0.c: <prose>', matching existing
    ledger convention) resolves unambiguously. Evidence must point at tracked artifacts;
    indexing the raw filesystem instead broke whenever agent worktrees existed under
    .claude/worktrees/ (every src basename became "ambiguous" -- 2026-07-10 red main).
    Falls back to a filesystem walk only when git is unavailable (e.g. an extracted
    archive), skipping the known untracked working-dir roots."""
    index = {}
    try:
        out = subprocess.run(
            ["git", "-C", repo_root, "ls-files", "-z"],
            capture_output=True, check=True)
        for rel in out.stdout.decode("utf-8", "replace").split("\0"):
            if rel:
                index.setdefault(os.path.basename(rel), []).append(rel)
        return index
    except (OSError, subprocess.CalledProcessError):
        pass
    skip_dirs = {".git", "build", ".claude", "dist", ".superpowers"}
    for dirpath, dirnames, filenames in os.walk(repo_root):
        dirnames[:] = [d for d in dirnames if d not in skip_dirs]
        for name in filenames:
            rel = os.path.relpath(os.path.join(dirpath, name), repo_root)
            index.setdefault(name, []).append(rel)
    return index


def _leading_path_token(text):
    """Pull the leading path-like token off a free-form evidence string, so citations
    like 'src/game/gun.c:19579', 'file.c:12-34 (note)', or 'file.c: prose...' (existing
    asm-citation house style) resolve to just the file component."""
    text = text.strip()
    m = re.match(r"^([A-Za-z0-9_./-]+\.[A-Za-z0-9]+)", text)
    if m:
        return m.group(1)
    parts = text.split()
    return parts[0] if parts else text


def evidence_target_ok(raw, repo_root, ctest_names, basename_index):
    """Return (ok, detail) for one evidence 'path' string (C5b). Accepts: a real
    repo-relative file, optionally with a '#section' anchor or trailing ':line'/prose
    (only the file part must exist); 'ctest:<name>' shorthand for a name registered in
    CMakeLists.txt; a bare filename that resolves unambiguously somewhere in the tree
    (existing asm-citation shorthand); or anything under docs/fidelity/reports/ (ephemeral,
    untracked by design)."""
    raw = (raw or "").strip()
    if not raw:
        return False, "empty evidence path"
    if raw.startswith("ctest:"):
        name = raw[len("ctest:"):].strip()
        if name in ctest_names:
            return True, ""
        return False, (f"ctest:{name} matches no add_test/add_port_validation_smoke "
                        "name in CMakeLists.txt")
    file_part = raw.split("#", 1)[0].strip()
    token = _leading_path_token(file_part)
    if not token:
        return False, f"could not extract a path from evidence {raw!r}"
    # docs/fidelity/reports/ is exempt ONLY if the path actually resolves inside that
    # directory after normalization -- a naive prefix-string match would rubber-stamp
    # exactly the ".." path-traversal bug this check exists to catch (the original
    # FID-0046 evidence was 'docs/fidelity/reports/../../../private/tmp ...').
    if token.startswith(REPORTS_PREFIX):
        reports_root = os.path.normpath(os.path.join(repo_root, "docs/fidelity/reports"))
        resolved = os.path.normpath(os.path.join(repo_root, token))
        if resolved == reports_root or resolved.startswith(reports_root + os.sep):
            return True, ""
        return False, (f"{token!r} escapes docs/fidelity/reports/ via path traversal "
                        f"(resolves to {resolved!r})")
    if os.path.isfile(os.path.join(repo_root, token)):
        return True, ""
    if "/" not in token:
        matches = basename_index.get(token, [])
        if len(matches) == 1:
            return True, ""
        if len(matches) > 1:
            return False, f"{token!r} basename is ambiguous ({len(matches)} matches: {matches})"
    return False, f"no file at {token!r} (from evidence {raw!r})"


def cmd_render(args):
    ld = ledger_dir(args)
    text, n = render_ledger_text(ld)
    out = os.path.join(os.path.dirname(os.path.abspath(ld)), "LEDGER.md")
    if getattr(args, "check", False):
        current = ""
        if os.path.exists(out):
            with open(out, "r", encoding="utf-8") as f:
                current = f.read()
        if current != text:
            print("LEDGER.md is STALE — run `tools/fidelity/ledger.py render` "
                  f"(index has drifted from the {n} JSON findings)")
            return 1
        print(f"LEDGER.md current ({n} findings)")
        return 0
    with open(out, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"wrote {out} ({n} findings)")
    return 0


def cmd_validate(args):
    ld = ledger_dir(args)
    entries = load_all(ld)
    violations = []
    # C5b: evidence paths must resolve to something real. Built once for the whole run.
    ctest_names = _known_ctest_names(REPO_ROOT)
    basename_index = _basename_index(REPO_ROOT)
    for fid in sorted(entries):
        o = entries[fid]
        for e in schema_errors(o):
            violations.append(f"{fid}: schema: {e}")
        if fid != o.get("id"):
            violations.append(f"{fid}: id field {o.get('id')!r} != filename")
        for e in o.get("evidence", []):
            ok, detail = evidence_target_ok(e.get("path"), REPO_ROOT, ctest_names, basename_index)
            if not ok:
                violations.append(f"{fid}: evidence path invalid: {detail}")
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
    # M3 (2026-07-10 review): `new` mints a finding with a synthetic genesis history
    # entry (from="" -> status) rather than walking cmd_transition's edge-set, so an
    # unrestricted --status let a finding be birthed directly into a terminal state
    # (e.g. `new --status verified`) with no chain and no evidence-progression at all --
    # the cheapest way to fake closure. The only legitimate callers (loop_iteration.sh's
    # auto-filer, and test fixtures seeding an already-triaged finding for blocked_on
    # coverage) ever pass "discovered" (the default) or "triaged"; restrict to those.
    n.add_argument("--status", default="discovered", choices=["discovered", "triaged"])
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
    t.add_argument("--reopen", action="store_true",
                    help="authorize a backward move among open states, or walking a "
                         "closed (verified/documented/refuted/waived) finding back to an "
                         "earlier non-terminal state; requires --note")
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

    p_render = sub.add_parser("render")
    p_render.add_argument("--check", action="store_true",
                          help="exit non-zero if LEDGER.md is out of date instead of writing it")
    p_render.set_defaults(func=cmd_render)
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
