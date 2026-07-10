#!/usr/bin/env python3
"""ASM-vs-C audit queue — drives every GLOBAL_ASM reimplementation to a verdict.

The S-tier exit criterion 4 requires that all ~388 `GLOBAL_ASM` reference bodies
in `src/` be driven to a recorded verdict via adversarial ASM-vs-C review (the
review contract in docs/fidelity/CHARTER.md #audit). This tool builds and
maintains that queue.

`GLOBAL_ASM(...)` expands to nothing in this port (see include/ultra64.h:30) —
the blocks are inert retail-assembly references. The *compiled* code is the
sibling native C body (usually under `#ifdef NONMATCHING`, with the GLOBAL_ASM
in the `#else`). Per CHARTER rule 1 the retail ASM is the authority; the `#else`
C reference bodies lie. This tool pairs each GLOBAL_ASM block with its live C
sibling so an audit agent can diff them.

python3 stdlib only. No ROM data, no build required (pure static scan).

Subcommands:
  build                 scan src/, (re)generate docs/fidelity/asm_audit_queue.json
                        (preserves recorded verdicts across rebuilds) + ASM_AUDIT.md
  next [--count N]      print the top-N ranked unreviewed entries + ASM/C locations
  record <symbol> --verdict V [--fid FID-NNNN] [--note ...]
                        record a verdict for an entry (validates transition)
  stats                 reviewed/total and per-verdict counts
  validate              schema + verdict-transition integrity check (ROM-free ctest)
"""
import argparse
import datetime
import json
import os
import re
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
SRC_DIR = os.path.join(REPO_ROOT, "src")
QUEUE_PATH = os.path.join(REPO_ROOT, "docs", "fidelity", "asm_audit_queue.json")
TABLE_PATH = os.path.join(REPO_ROOT, "docs", "fidelity", "ASM_AUDIT.md")
LEDGER_DIR = os.path.join(REPO_ROOT, "docs", "fidelity", "ledger")

# Files with prior confirmed defects — audited FIRST (CHARTER #audit rank (a)).
DEFECT_FILES = {"gun.c", "bondview.c", "chrobjhandler.c", "stan.c", "prop.c"}
# Bulk files audited LAST (CHARTER #audit rank (c)).
BULK_FILES = {"bg.c", "model.c"}

# Verdict grammar recorded per entry.
V_UNREVIEWED = "unreviewed"
V_VERIFIED = "verified-equivalent"
VERDICT_RE = re.compile(
    r"^(unreviewed|verified-equivalent|finding-filed:FID-\d{4}|waived:.+)$")

# An instruction line: /* romoff vram word */  mnemonic ...
INSTR_RE = re.compile(r"^\s*/\*\s*[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s*\*/")
GLABEL_RE = re.compile(r"^\s*glabel\s+(\S+)")
JPT_REF_RE = re.compile(r"jpt_[0-9A-Za-z_]+")
JR_RE = re.compile(r"\bjr\s+\$")
PP_RE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|else|elif|endif)\b(.*)$")


def iso_now():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def rel(path):
    return os.path.relpath(path, REPO_ROOT)


# --- scanning -------------------------------------------------------------
def iter_src_files():
    for root, _dirs, files in os.walk(SRC_DIR):
        for name in sorted(files):
            if name.endswith((".c", ".h")):
                yield os.path.join(root, name)


def find_c_definition(lines, symbol):
    """Return 1-based line of the live C definition of `symbol`, or None.

    A definition is a line where `symbol(` appears not inside a GLOBAL_ASM
    block, not as a `glabel`, not a bare call (ends with ';'), and not inside a
    block comment line. Prefer the one closest above an asm block, but any
    definition line works as the C anchor.
    """
    pat = re.compile(r"(^|[^0-9A-Za-z_])" + re.escape(symbol) + r"\s*\(")
    best = None
    for i, ln in enumerate(lines):
        s = ln.rstrip("\n")
        if "glabel" in s or s.lstrip().startswith("/*") or s.lstrip().startswith("*"):
            continue
        stripped = s.strip()
        if not pat.search(s):
            continue
        # Skip obvious call sites: line ends with ');' and does not open a body.
        if stripped.endswith(";"):
            continue
        # A definition line either ends in '{' or ')' (K&R/newline brace) and
        # begins with a type/qualifier or the symbol at low indent.
        if stripped.endswith("{") or stripped.endswith(")") or stripped.endswith(","):
            best = i + 1
            # earliest definition wins (the native body precedes the asm)
            return best
    return best


def guard_for_block(lines, asm_start_idx):
    """Given the 0-based index of the GLOBAL_ASM( line, find the enclosing
    `#else` sibling and its matching `#if*`. Returns (guard_text, if_line1,
    else_line1) all 1-based, or (None, None, None) if the block is not inside an
    #else (standalone GLOBAL_ASM). The live C body span is [if_line1+1 ..
    else_line1-1] but may contain nested guards.
    """
    # Walk backward to the enclosing #else. The GLOBAL_ASM reference always sits
    # in the #else branch of its matching guard (usually `#ifdef NONMATCHING`),
    # frequently wrapped by an inner `#ifdef VERSION_US` region guard. Step out
    # of region-only wrappers (VERSION_*) to reach the real enclosing #else.
    depth = 0
    else_idx = None
    for j in range(asm_start_idx - 1, -1, -1):
        m = PP_RE.match(lines[j])
        if not m:
            continue
        kw, rest = m.group(1), (m.group(2) or "")
        if kw == "endif":
            depth += 1
        elif kw in ("if", "ifdef", "ifndef"):
            if depth == 0:
                if any(r in rest for r in ("VERSION_US", "VERSION_JP", "VERSION_EU")):
                    continue  # region wrapper nests inside the #else — step out
                return (None, None, None)  # non-region enclosing if → standalone
            depth -= 1
        elif kw == "else":
            if depth == 0:
                else_idx = j
                break
    if else_idx is None:
        return (None, None, None)
    # find the matching #if for this #else
    depth = 0
    for j in range(else_idx - 1, -1, -1):
        m = PP_RE.match(lines[j])
        if not m:
            continue
        kw = m.group(1)
        if kw == "endif":
            depth += 1
        elif kw in ("if", "ifdef", "ifndef"):
            if depth == 0:
                guard = (m.group(2) or "").strip()
                return (guard, j + 1, else_idx + 1)
            depth -= 1
        elif kw == "else":
            depth += 1  # nested else belongs to a deeper if
    return (None, None, else_idx + 1)


def enclosing_region(lines, idx):
    """Nearest enclosing `#ifdef VERSION_XX` region guard for line index idx.
    Returns 'US' / 'JP' / 'EU' / None (default/no region guard)."""
    depth = 0
    for j in range(idx - 1, -1, -1):
        m = PP_RE.match(lines[j])
        if not m:
            continue
        kw, rest = m.group(1), (m.group(2) or "")
        if kw == "endif":
            depth += 1
        elif kw in ("if", "ifdef", "ifndef"):
            if depth == 0:
                for reg in ("VERSION_US", "VERSION_JP", "VERSION_EU"):
                    if reg in rest:
                        return reg.split("_")[1]
                # non-region guard (NONMATCHING/NATIVE_PORT/...) — keep looking up
                continue
            depth -= 1
    return None


def scan_file(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.readlines()
    entries = []
    i = 0
    n = len(lines)
    while i < n:
        if lines[i].lstrip().startswith("GLOBAL_ASM("):
            start = i
            # find closing ')' on its own line (may be indented for region variants)
            j = i + 1
            while j < n and not re.match(r"^\s*\)\s*;?\s*$", lines[j]):
                j += 1
            end = j if j < n else n - 1
            body = lines[start:end + 1]
            region = enclosing_region(lines, start)
            entries.append(parse_block(path, lines, start, end, body, region))
            i = end + 1
        else:
            i += 1
    return entries


def parse_block(path, all_lines, start, end, body, region=None):
    instr_count = 0
    text_glabels = []   # glabels immediately followed by an instruction line
    all_glabels = []
    jpts = set()
    has_jr = False
    for k, raw in enumerate(body):
        line = raw.rstrip("\n")
        if INSTR_RE.match(line):
            instr_count += 1
        gm = GLABEL_RE.match(line)
        if gm:
            name = gm.group(1)
            all_glabels.append(name)
            # is it a text (function) label? look at the next non-blank line
            nxt = None
            for look in range(k + 1, len(body)):
                cand = body[look].rstrip("\n")
                if cand.strip() == "":
                    continue
                nxt = cand
                break
            if nxt is not None and INSTR_RE.match(nxt):
                text_glabels.append(name)
        for jm in JPT_REF_RE.findall(line):
            jpts.add(jm)
        if JR_RE.search(line):
            has_jr = True

    # primary function symbol: first text glabel; else first glabel; else derive.
    if text_glabels:
        symbol = text_glabels[0]
    elif all_glabels:
        symbol = all_glabels[0]
    else:
        symbol = f"{os.path.basename(path)}:{start + 1}"

    # A dispatch block = references a jpt AND performs a computed jump (jr).
    has_jump_table = bool(jpts) and has_jr

    # live C sibling
    guard, if_l, else_l = guard_for_block(all_lines, start)
    c_def = find_c_definition(all_lines, symbol) if text_glabels or all_glabels else None
    live_c = None
    if if_l or c_def:
        live_c = {
            "file": rel(path),
            "def_line": c_def,
            "guard": guard,
            "body_start": (if_l + 1) if if_l else None,
            "body_end": (else_l - 1) if else_l else None,
        }

    return {
        "symbol": symbol,
        "symbols": text_glabels or all_glabels,
        "file": rel(path),
        "region": region or "US/default",
        "asm_start_line": start + 1,
        "asm_end_line": end + 1,
        "instr_count": instr_count,
        "byte_size": instr_count * 4,
        "jump_tables": sorted(jpts),
        "has_jump_table": has_jump_table,
        "live_c": live_c,
        "status": V_UNREVIEWED,
        "note": "",
        "reviewed_ts": None,
    }


# --- ranking --------------------------------------------------------------
def rank_class(entry):
    # VERSION_JP/EU region duplicates: the port is VERSION_US, so the US/default
    # variant is the authority (CHARTER rule 1). Audit those first; the region
    # duplicates rank dead last (they share the same C body / retail logic and
    # get resolved by reference to their US sibling).
    if entry.get("region") in ("JP", "EU"):
        return 3
    base = os.path.basename(entry["file"])
    if base in DEFECT_FILES:
        return 0
    if base in BULK_FILES:
        return 2
    return 1


def rank_score(entry):
    # dispatch semantics are the proven failure mode → jump-table blocks float up
    mult = 3 if entry["has_jump_table"] else 1
    return entry["instr_count"] * mult + (500 if entry["has_jump_table"] else 0)


def rank_reason(entry):
    base = os.path.basename(entry["file"])
    parts = []
    if entry.get("region") in ("JP", "EU"):
        parts.append(f"region-dup({entry['region']})")
    elif base in DEFECT_FILES:
        parts.append("prior-defect-file")
    elif base in BULK_FILES:
        parts.append("bulk-last")
    if entry["has_jump_table"]:
        parts.append(f"dispatch({','.join(entry['jump_tables'])})")
    parts.append(f"{entry['instr_count']}i")
    return " ".join(parts)


def assign_ranks(entries):
    entries.sort(key=lambda e: (
        rank_class(e), -rank_score(e), -e["instr_count"], e["file"], e["symbol"]))
    for idx, e in enumerate(entries, 1):
        e["rank"] = idx
        e["rank_score"] = rank_score(e)
        e["rank_reason"] = rank_reason(e)
    return entries


# --- queue io -------------------------------------------------------------
def load_queue():
    with open(QUEUE_PATH, encoding="utf-8") as f:
        return json.load(f)


def save_queue(doc):
    os.makedirs(os.path.dirname(QUEUE_PATH), exist_ok=True)
    with open(QUEUE_PATH, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=2)
        f.write("\n")


def entry_key(e):
    return (e["file"], e["symbol"], e["asm_start_line"])


# --- commands -------------------------------------------------------------
def cmd_build(args):
    entries = []
    for path in iter_src_files():
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        if "GLOBAL_ASM(" not in text:
            continue
        entries.extend(scan_file(path))

    # preserve verdicts from an existing queue (keyed by file+symbol+asm_start;
    # fall back to file+symbol so small line drifts don't lose a verdict).
    preserved = {}
    preserved_loose = {}
    if os.path.isfile(QUEUE_PATH):
        old = load_queue()
        for e in old.get("entries", []):
            if e.get("status", V_UNREVIEWED) != V_UNREVIEWED:
                preserved[entry_key(e)] = e
                preserved_loose[(e["file"], e["symbol"])] = e
    restored = 0
    for e in entries:
        src = preserved.get(entry_key(e)) or preserved_loose.get((e["file"], e["symbol"]))
        if src:
            e["status"] = src["status"]
            e["note"] = src.get("note", "")
            e["reviewed_ts"] = src.get("reviewed_ts")
            restored += 1

    assign_ranks(entries)
    doc = {
        "schema": "mgb64.asm_audit_queue.v1",
        "generated_ts": iso_now(),
        "source": "src/ GLOBAL_ASM scan (tools/fidelity/asm_audit.py build)",
        "authority": "retail ASM > oracle > decomp C; #else bodies lie (CHARTER rule 1)",
        "total": len(entries),
        "entries": entries,
    }
    save_queue(doc)
    render_table(doc)
    region_dups = sum(1 for e in entries if e.get("region") in ("JP", "EU"))
    print(f"built queue: {len(entries)} entries -> {rel(QUEUE_PATH)} "
          f"({restored} verdicts preserved)")
    print(f"  {len(entries)-region_dups} US/default (ranked by risk) + "
          f"{region_dups} VERSION_JP/EU region duplicates (ranked last; US port "
          f"is the authority per CHARTER rule 1)")
    return 0


def verdict_counts(entries):
    counts = {}
    for e in entries:
        s = e["status"]
        key = s.split(":")[0] if s != V_UNREVIEWED else s
        counts[key] = counts.get(key, 0) + 1
    return counts


def render_table(doc):
    entries = doc["entries"]
    counts = verdict_counts(entries)
    reviewed = sum(v for k, v in counts.items() if k != V_UNREVIEWED)
    total = len(entries)
    lines = [
        "# ASM-vs-C Audit Queue — progress",
        "",
        "> Generated by `tools/fidelity/asm_audit.py build`/`record`. Do not "
        "hand-edit; source of truth is `docs/fidelity/asm_audit_queue.json`.",
        "> Drives S-tier exit criterion 4 (FID-0042). Review contract: "
        "`docs/fidelity/CHARTER.md` #audit.",
        "",
        f"**{reviewed}/{total} reviewed** "
        f"({0.0 if not total else round(100.0*reviewed/total,1)}%). "
        "Verdict counts: " + ", ".join(f"{k}={counts[k]}" for k in sorted(counts)) + ".",
        "",
        "Risk rank: (a) prior-defect files (gun/bondview/chrobjhandler/stan/prop) "
        "first, (b) instruction-count x has-jump-table (dispatch = proven failure "
        "mode), (c) bg/model bulk last.",
        "",
        "## Top 30 ranked unreviewed",
        "",
        "| Rank | Symbol | File | Instrs | Jump tables | Reason |",
        "|-----:|--------|------|-------:|-------------|--------|",
    ]
    shown = 0
    for e in sorted(entries, key=lambda x: x["rank"]):
        if e["status"] != V_UNREVIEWED:
            continue
        jt = ", ".join(e["jump_tables"]) if e["jump_tables"] else ""
        lines.append(f"| {e['rank']} | `{e['symbol']}` | {e['file']} | "
                     f"{e['instr_count']} | {jt} | {e['rank_reason']} |")
        shown += 1
        if shown >= 30:
            break
    lines += ["", "## Recorded verdicts", "",
              "| Rank | Symbol | File | Verdict | Note |",
              "|-----:|--------|------|---------|------|"]
    any_rec = False
    for e in sorted(entries, key=lambda x: x["rank"]):
        if e["status"] == V_UNREVIEWED:
            continue
        any_rec = True
        note = (e.get("note") or "").replace("|", "\\|")
        lines.append(f"| {e['rank']} | `{e['symbol']}` | {e['file']} | "
                     f"`{e['status']}` | {note} |")
    if not any_rec:
        lines.append("| — | — | — | — | (none yet) |")
    lines.append("")
    with open(TABLE_PATH, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def cmd_next(args):
    doc = load_queue()
    todo = [e for e in doc["entries"] if e["status"] == V_UNREVIEWED]
    todo.sort(key=lambda x: x["rank"])
    for e in todo[:args.count]:
        lc = e["live_c"]
        reg = "" if e.get("region") == "US/default" else f"  [region {e['region']}]"
        print(f"#{e['rank']:>4}  {e['symbol']}{reg}")
        print(f"        ASM : {e['file']}:{e['asm_start_line']}-{e['asm_end_line']}"
              f"  ({e['instr_count']} instrs, {e['byte_size']} bytes)")
        if e["jump_tables"]:
            print(f"        JPT : {', '.join(e['jump_tables'])}"
                  f"  (dispatch={'yes' if e['has_jump_table'] else 'no'})")
        if lc and lc.get("def_line"):
            span = ""
            if lc.get("body_start"):
                g = f", guard {lc['guard']}" if lc.get("guard") else ""
                span = f" (live-body branch {lc['body_start']}-{lc['body_end']}{g})"
            print(f"        C   : {lc['file']}:{lc['def_line']}{span}")
        elif lc and lc.get("body_start"):
            print(f"        C   : {lc['file']} live-body branch "
                  f"{lc['body_start']}-{lc['body_end']} guard={lc.get('guard')}")
        else:
            print("        C   : (no live C sibling located — likely asm-only; "
                  "inspect manually)")
        print(f"        rank: {e['rank_reason']}")
        print()
    if not todo:
        print("queue exhausted: all entries reviewed.")
    return 0


def cmd_record(args):
    doc = load_queue()
    matches = [e for e in doc["entries"] if e["symbol"] == args.symbol]
    if not matches:
        print(f"no queue entry with symbol {args.symbol!r}", file=sys.stderr)
        return 1
    if args.file:
        matches = [e for e in matches
                   if os.path.basename(e["file"]) == os.path.basename(args.file)]
    if args.line:
        matches = [e for e in matches if e["asm_start_line"] == args.line]
    if len(matches) > 1:
        # prefer the US/default variant (the port authority) when a symbol has
        # region duplicates; only that is ambiguous-with-region.
        us = [e for e in matches if e.get("region") == "US/default"]
        if len(us) == 1:
            matches = us
    if len(matches) > 1:
        print(f"symbol {args.symbol!r} is ambiguous ({len(matches)} entries); "
              f"disambiguate with --file and/or --line", file=sys.stderr)
        for e in matches:
            print(f"  {e['file']}:{e['asm_start_line']} region={e.get('region')}",
                  file=sys.stderr)
        return 1
    entry = matches[0]

    verdict = args.verdict
    if verdict == "finding-filed":
        if not args.fid or not re.match(r"^FID-\d{4}$", args.fid):
            print("verdict finding-filed requires --fid FID-NNNN", file=sys.stderr)
            return 1
        if not os.path.isfile(os.path.join(LEDGER_DIR, f"{args.fid}.json")):
            print(f"--fid {args.fid} has no ledger entry "
                  f"(file a finding with ledger.py new first)", file=sys.stderr)
            return 1
        status = f"finding-filed:{args.fid}"
    elif verdict == "waived":
        if not args.note:
            print("verdict waived requires --note <reason>", file=sys.stderr)
            return 1
        status = f"waived:{args.note}"
    elif verdict == V_VERIFIED:
        if not args.note:
            print("verdict verified-equivalent requires --note "
                  "(2-line justification per CHARTER #audit)", file=sys.stderr)
            return 1
        status = V_VERIFIED
    elif verdict == V_UNREVIEWED:
        status = V_UNREVIEWED
    else:
        print(f"unknown verdict {verdict!r}", file=sys.stderr)
        return 1

    if not VERDICT_RE.match(status):
        print(f"resulting status {status!r} is not a valid verdict", file=sys.stderr)
        return 1

    prev = entry["status"]
    entry["status"] = status
    entry["note"] = args.note or ""
    entry["reviewed_ts"] = None if status == V_UNREVIEWED else iso_now()
    save_queue(doc)
    render_table(doc)
    print(f"{entry['symbol']} ({entry['file']}): {prev} -> {status}")
    return 0


def cmd_stats(args):
    doc = load_queue()
    entries = doc["entries"]
    counts = verdict_counts(entries)
    reviewed = sum(v for k, v in counts.items() if k != V_UNREVIEWED)
    total = len(entries)
    print(f"reviewed: {reviewed}/{total} "
          f"({0.0 if not total else round(100.0*reviewed/total,1)}%)")
    for k in sorted(counts):
        print(f"  {k}: {counts[k]}")
    return 0


def cmd_validate(args):
    if not os.path.isfile(QUEUE_PATH):
        print(f"queue missing: {rel(QUEUE_PATH)} (run asm_audit.py build)",
              file=sys.stderr)
        return 1
    doc = load_queue()
    v = []
    if doc.get("schema") != "mgb64.asm_audit_queue.v1":
        v.append(f"top-level schema tag wrong: {doc.get('schema')!r}")
    entries = doc.get("entries")
    if not isinstance(entries, list):
        print("VALIDATION FAILED: entries is not a list", file=sys.stderr)
        return 1
    if doc.get("total") != len(entries):
        v.append(f"total {doc.get('total')} != len(entries) {len(entries)}")

    ranks = []
    req_str = ["symbol", "file", "status", "note"]
    req_int = ["asm_start_line", "asm_end_line", "instr_count", "byte_size", "rank"]
    for idx, e in enumerate(entries):
        tag = f"entries[{idx}] {e.get('symbol','?')}"
        for k in req_str:
            if not isinstance(e.get(k), str):
                v.append(f"{tag}: field {k} missing/not string")
        for k in req_int:
            if not isinstance(e.get(k), int) or isinstance(e.get(k), bool):
                v.append(f"{tag}: field {k} missing/not int")
        if not isinstance(e.get("jump_tables"), list):
            v.append(f"{tag}: jump_tables not a list")
        if not isinstance(e.get("has_jump_table"), bool):
            v.append(f"{tag}: has_jump_table not bool")
        st = e.get("status", "")
        if not VERDICT_RE.match(st):
            v.append(f"{tag}: status {st!r} violates verdict grammar")
        # transition integrity: a finding-filed verdict must reference a real FID
        m = re.match(r"^finding-filed:(FID-\d{4})$", st)
        if m and not os.path.isfile(os.path.join(LEDGER_DIR, f"{m.group(1)}.json")):
            v.append(f"{tag}: finding-filed references missing {m.group(1)}")
        if st == V_VERIFIED and not (e.get("note") or "").strip():
            v.append(f"{tag}: verified-equivalent without justification note")
        if st.startswith("waived:") and len(st) <= len("waived:"):
            v.append(f"{tag}: waived without reason")
        # reviewed entries carry a timestamp; unreviewed do not
        if st == V_UNREVIEWED and e.get("reviewed_ts") is not None:
            v.append(f"{tag}: unreviewed but has reviewed_ts")
        if st != V_UNREVIEWED and not e.get("reviewed_ts"):
            v.append(f"{tag}: reviewed verdict without reviewed_ts")
        if isinstance(e.get("asm_start_line"), int) and isinstance(e.get("asm_end_line"), int):
            if e["asm_end_line"] < e["asm_start_line"]:
                v.append(f"{tag}: asm_end_line < asm_start_line")
        fpath = os.path.join(REPO_ROOT, e.get("file", ""))
        if not os.path.isfile(fpath):
            v.append(f"{tag}: file does not exist: {e.get('file')}")
        if isinstance(e.get("rank"), int):
            ranks.append(e["rank"])
    # ranks must be a contiguous 1..N permutation
    if sorted(ranks) != list(range(1, len(entries) + 1)):
        v.append("ranks are not a contiguous 1..N permutation")

    if v:
        print("VALIDATION FAILED:", file=sys.stderr)
        for x in v:
            print("  " + x, file=sys.stderr)
        return 1
    counts = verdict_counts(entries)
    reviewed = sum(val for k, val in counts.items() if k != V_UNREVIEWED)
    print(f"asm_audit_queue OK: {len(entries)} entries, "
          f"{reviewed} reviewed, ranks 1..{len(entries)} contiguous")
    return 0


def build_parser():
    p = argparse.ArgumentParser(description="ASM-vs-C audit queue")
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("build").set_defaults(func=cmd_build)
    nx = sub.add_parser("next")
    nx.add_argument("--count", type=int, default=5)
    nx.set_defaults(func=cmd_next)
    rc = sub.add_parser("record")
    rc.add_argument("symbol")
    rc.add_argument("--verdict", required=True,
                    choices=[V_VERIFIED, "finding-filed", "waived", V_UNREVIEWED])
    rc.add_argument("--fid", default=None)
    rc.add_argument("--note", default="")
    rc.add_argument("--file", default=None, help="disambiguate a duplicated symbol")
    rc.add_argument("--line", type=int, default=None,
                    help="disambiguate by asm_start_line")
    rc.set_defaults(func=cmd_record)
    sub.add_parser("stats").set_defaults(func=cmd_stats)
    sub.add_parser("validate").set_defaults(func=cmd_validate)
    return p


def main(argv=None):
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
