#!/usr/bin/env python3
"""
hash_coverage_audit.py — sim-hash coverage audit (S-Tier Task 0.4, FID-0030).

Makes "what the sim-state invariance hash covers" a checked property instead of
tribal knowledge. Cross-references every writable (`.data`/`.bss`) symbol in the
decomp translation units (`src/game/*` + top-level `src/*`) against the set of
regions the runtime actually hashes, and dispositions each symbol:

  hashed    — the symbol's runtime address falls inside a registered SimHashRegion
              (emitted ROM-free by `<binary> --print-sim-hash-regions`).
  waived    — the symbol name matches a pattern in docs/fidelity/hash_waivers.txt
              (render-only counters, trace/debug state, immutable-after-load tables,
              port/debug TUs) — carries a one-line reason.
  UNCOVERED — neither hashed nor waived. Any UNCOVERED symbol fails the audit
              (exit 1): it is mutable decomp state the invariance gate cannot see.

ROM-free: only `nm` over the built objects + the linked binary, plus the region
table the binary prints without loading a ROM. Registered as ctest
`fidelity_hash_coverage` (verify_manifest tier 1).

macOS note: Mach-O symbol tables do not store symbol sizes, so coverage is
decided by START-address containment (a symbol whose start lands inside a hashed
region is hashed — the registered regions are whole arrays/scalars whose size is
reported exactly by --print-sim-hash-regions, so a symbol starting inside one is
wholly inside it). ASLR is handled by computing the load slide from a region
whose name matches a global symbol (g_ClockTimer / g_GlobalTimer) each run.
"""
import argparse
import fnmatch
import json
import os
import re
import subprocess
import sys

# Writable Mach-O sections that hold mutable data (exclude read-only __const).
WRITABLE_SECTION_RE = re.compile(
    r"\((?:common|__DATA,__data|__DATA,__bss|__DATA,__common|__DATA,__thread_data|__DATA,__thread_bss)\)"
)


def sh(cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=False)


def parse_regions(binary):
    """Run --print-sim-hash-regions -> [(name, base_int, size_int)]."""
    r = sh([binary, "--print-sim-hash-regions"])
    if r.returncode != 0:
        sys.exit("audit: '%s --print-sim-hash-regions' failed (rc=%d)\n%s"
                 % (binary, r.returncode, r.stderr))
    regions = []
    for line in r.stdout.splitlines():
        parts = line.split()
        if len(parts) != 3:
            continue
        name, base, size = parts
        try:
            regions.append((name, int(base, 16), int(size)))
        except ValueError:
            continue
    if not regions:
        sys.exit("audit: no regions parsed from --print-sim-hash-regions:\n" + r.stdout)
    return regions


def exe_symbol_addrs(binary):
    """name (no leading '_') -> list[int addr] for every defined symbol in the exe."""
    r = sh(["nm", "--defined-only", binary])
    addrs = {}
    for line in r.stdout.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        addr, typ, name = parts[0], parts[1], " ".join(parts[2:])
        try:
            a = int(addr, 16)
        except ValueError:
            continue
        n = name[1:] if name.startswith("_") else name
        addrs.setdefault(n, []).append(a)
    return addrs


def decomp_writable_symbols(objroot):
    """
    Collect writable data/bss symbols from the decomp objects (src/game/* and
    top-level src/*.c.o, excluding the src/platform and src/app port layers).
    Returns dict name(no '_') -> {"section": str, "objs": set(basename)}.
    """
    objs = []
    game_dir = os.path.join(objroot, "src", "game")
    src_dir = os.path.join(objroot, "src")
    if os.path.isdir(game_dir):
        for f in sorted(os.listdir(game_dir)):
            if f.endswith(".c.o"):
                objs.append(os.path.join(game_dir, f))
    if os.path.isdir(src_dir):
        for f in sorted(os.listdir(src_dir)):
            if f.endswith(".c.o"):
                objs.append(os.path.join(src_dir, f))
    if not objs:
        sys.exit("audit: no decomp .o objects under %s" % objroot)

    syms = {}
    for o in objs:
        base = os.path.basename(o)
        r = sh(["nm", "-m", "--defined-only", o])
        for line in r.stdout.splitlines():
            m = WRITABLE_SECTION_RE.search(line)
            if not m:
                continue
            name = line.split()[-1]
            n = name[1:] if name.startswith("_") else name
            e = syms.setdefault(n, {"section": m.group(0), "objs": set()})
            e["objs"].add(base)
    return syms


def load_waivers(path):
    """
    Return list of (name_glob, obj_glob, reason).

    Line grammar (fields separated by 2+ spaces or a tab; '#' comments):
      <name_glob>              <reason>   — waive symbols whose name matches.
      @<obj_glob>              <reason>   — waive any symbol defined by a matching
                                            object (e.g. '@speed_graph.c.o').
      <name_glob> @<obj_glob>  <reason>   — both must match (object-scoped name).

    A bare '*' name_glob with no object scope is rejected: a wildcard that waives
    everything would strip the audit of its teeth (a newly added mutable global
    must still surface as UNCOVERED).
    """
    out = []
    if not os.path.exists(path):
        return out
    with open(path) as f:
        for lineno, line in enumerate(f, 1):
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            parts = re.split(r"\s{2,}|\t", s, maxsplit=2)
            first = parts[0].strip()
            name_glob, obj_glob, rest = "*", None, parts[1:]
            if first.startswith("@"):
                obj_glob = first[1:]
            else:
                name_glob = first
                if len(parts) >= 2 and parts[1].strip().startswith("@"):
                    obj_glob = parts[1].strip()[1:]
                    rest = parts[2:]
            reason = (rest[0].strip() if rest else "")
            if name_glob == "*" and obj_glob is None:
                sys.exit("audit: waiver line %d is a bare '*' with no @object scope "
                         "(would waive everything and defeat the audit): %r"
                         % (lineno, s))
            out.append((name_glob, obj_glob, reason))
    return out


def waiver_match(name, objs, waivers):
    for name_glob, obj_glob, reason in waivers:
        if not fnmatch.fnmatchcase(name, name_glob):
            continue
        if obj_glob is not None and not any(
                fnmatch.fnmatchcase(o, obj_glob) for o in objs):
            continue
        label = name_glob + (" @" + obj_glob if obj_glob else "")
        return label, reason
    return None


def compute_slide(regions, exe_addrs):
    """slide = runtime region base - exe symbol addr, from a region whose name is
    an exe symbol (g_ClockTimer / g_GlobalTimer)."""
    slides = []
    for name, base, size in regions:
        if name in exe_addrs and len(exe_addrs[name]) == 1:
            slides.append((name, base - exe_addrs[name][0]))
    if not slides:
        sys.exit("audit: cannot compute ASLR slide — no region name matches a "
                 "unique exe symbol (need g_ClockTimer/g_GlobalTimer).")
    vals = {s for _, s in slides}
    if len(vals) != 1:
        sys.exit("audit: inconsistent ASLR slide across anchors: %r" % slides)
    return slides[0][1]


def in_region(runtime_addr, regions):
    for name, base, size in regions:
        if size > 0 and base <= runtime_addr < base + size:
            return name
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", required=True, help="path to built ge007")
    ap.add_argument("--objroot", required=True,
                    help="CMake object root (…/CMakeFiles/ge007.dir)")
    ap.add_argument("--waivers", required=True, help="hash_waivers.txt")
    ap.add_argument("--report", help="write JSON report to this path")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(args.binary):
        sys.exit("audit: binary not found: " + args.binary)

    regions = parse_regions(args.binary)
    exe_addrs = exe_symbol_addrs(args.binary)
    slide = compute_slide(regions, exe_addrs)
    decomp = decomp_writable_symbols(args.objroot)
    waivers = load_waivers(args.waivers)

    hashed, waived, uncovered, unresolved = [], [], [], []
    for name in sorted(decomp):
        addrs = exe_addrs.get(name)
        if not addrs:
            # Defined-in-object but absent from the linked exe symbol table
            # (dead-stripped or purely local and coalesced). Nothing to cover.
            unresolved.append(name)
            continue
        # A symbol is hashed if any of its exe occurrences maps into a region.
        covered_by = None
        for a in addrs:
            covered_by = in_region(a + slide, regions)
            if covered_by:
                break
        if covered_by:
            hashed.append({"sym": name, "region": covered_by})
            continue
        w = waiver_match(name, decomp[name]["objs"], waivers)
        if w:
            waived.append({"sym": name, "pattern": w[0], "reason": w[1]})
            continue
        uncovered.append({"sym": name,
                          "section": decomp[name]["section"],
                          "objs": sorted(decomp[name]["objs"])})

    report = {
        "regions": [{"name": n, "base": hex(b), "size": s} for n, b, s in regions],
        "slide": hex(slide),
        "counts": {
            "decomp_writable_symbols": len(decomp),
            "hashed": len(hashed),
            "waived": len(waived),
            "uncovered": len(uncovered),
            "unresolved_absent_from_exe": len(unresolved),
        },
        "hashed": hashed,
        "waived": waived,
        "uncovered": uncovered,
    }
    if args.report:
        with open(args.report, "w") as f:
            json.dump(report, f, indent=2)

    c = report["counts"]
    print("[hash-coverage] regions=%d slide=%s  writable=%d hashed=%d waived=%d "
          "uncovered=%d (absent-from-exe=%d)"
          % (len(regions), hex(slide), c["decomp_writable_symbols"], c["hashed"],
             c["waived"], c["uncovered"], c["unresolved_absent_from_exe"]))
    if uncovered:
        print("\nUNCOVERED (%d) — mutable decomp state neither hashed nor waived:"
              % len(uncovered))
        for u in uncovered[: (None if args.verbose else 60)]:
            print("  %-52s %-18s %s" % (u["sym"], u["section"], ",".join(u["objs"])))
        if not args.verbose and len(uncovered) > 60:
            print("  … %d more (use --verbose or read the JSON report)"
                  % (len(uncovered) - 60))
        return 1
    print("OK: every writable decomp symbol is hashed or waived.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
