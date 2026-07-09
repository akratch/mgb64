#!/usr/bin/env python3
"""gen_struct_asserts.py — one-time generator for tests/test_struct_layout.c.

S-Tier Faithfulness Program, Phase 0 Task 0.7 (FID-0035 / first half of FID-0036).

The port relies on N64 struct layouts (StandTile navmesh, PROPDEF records) that
are held together ONLY by compiler flags (-mno-ms-bitfields + -fms-extensions).
If a compiler or flag ever drifts (the exact v0.3.2 Windows-crash class: MinGW
defaulting to -mms-bitfields repacked StandTile and OOB'd the stan navmesh), the
layout silently changes and corrupts navmesh/prop reads at RUNTIME.

This generator emits `_Static_assert(sizeof(T) == N, ...)` for every PROPDEF
record type in the two hand-maintained size tables, plus `offsetof` asserts for
the union / bitfield-adjacent fields flagged in FID-0036 and the whole StandTile
family, with expected values taken from the CURRENT known-good build. Any future
compiler/flag drift then fails the BUILD (the _Static_asserts fire at compile
time) instead of corrupting memory at runtime.

Usage:
    python3 tools/fidelity/gen_struct_asserts.py            # print asserts to stdout
    python3 tools/fidelity/gen_struct_asserts.py --cc clang # override compiler

The output block is pasted into tests/test_struct_layout.c ONCE, then that file
is hand-maintained. Re-run this generator if the size tables change to refresh
the expected values (and review the diff — a changed value is either an
intentional layout change or a drift finding).
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

# --- Game build configuration (must mirror CMakeLists.txt) --------------------
# add_definitions(...) at CMakeLists.txt:536 + the layout-critical per-target
# compile options at :707 (-fms-extensions, -mno-ms-bitfields).
GAME_DEFINES = [
    "NONMATCHING", "NATIVE_PORT", "PORT_FIXME_STUBS", "_LANGUAGE_C",
    "VERSION_US", "LANG_US", "REFRESH_NTSC", "LEFTOVERDEBUG",
    "LEFTOVERSPECTRUM", "BUGFIX_R0", "BYTEMATCH", "SDL_MAIN_HANDLED",
]
GAME_INCLUDES = [
    "include", "include/PR", "src", "src/game", "src/platform",
    "src/platform/fast3d", "src/libultra/audio", "assets",
    "lib/glad/include", "lib/stb", ".",
]
# The two flags that LOCK the N64 layout. -fms-extensions is required to parse
# bondtypes.h's `inherits` (anonymous struct member) idiom; -mno-ms-bitfields
# forces the SysV/Itanium bit-field packing on every platform.
LAYOUT_FLAGS = ["-fms-extensions", "-mno-ms-bitfields"]

# --- offsetof spec: union / bitfield-adjacent fields (FID-0036) + StandTile ---
# (C type, member) pairs. Bitfield members cannot be offsetof'd, so we lock
# their neighbours (and the total sizeof) instead.
OFFSET_SPEC = [
    # FID-0036 converter risk cases: union / pair-stride fields.
    ("KeyRecord", "keyID"),               # anonymous union {s8 keyID; u32 keyflags}
    ("MultiAmmoCrateRecord", "unk80"),    # overlay view (2-byte stride)
    ("MultiAmmoCrateRecord", "slots"),    # authored setup layout (4-byte pair stride)
    ("TankRecord", "rect"),               # struct rect4f, after collision_data* (8B on PC)
    # StandTile navmesh family — the v0.3.2 -mms-bitfields crash surface.
    ("StandTile", "room"),
    ("StandTile", "mid"),
    ("StandTile", "tail"),
    ("StandTile", "points"),
    ("StandFileTile", "room"),
    ("StandFileTile", "mid"),
    ("StandFileTile", "tail"),
]

# --- sizeof spec for the StandTile family (bondtypes.h:387-472) ---------------
STANDTILE_SIZEOF = [
    "StandTilePoint", "StandTileHeaderMid", "StandTileHeaderTail",
    "StandTile", "StandFilePoint", "StandFileTile",
]


def parse_prop_c(path):
    """Parse sizepropdef_pc_bytes: `case PROPDEF_X: return (s32)sizeof(TYPE);`."""
    text = open(path, encoding="utf-8").read()
    start = text.index("static s32 sizepropdef_pc_bytes")
    end = text.index("}", text.index("switch", start))
    body = text[start:end]
    pat = re.compile(r"case\s+(PROPDEF_\w+):\s*return\s+\(s32\)sizeof\(([^)]+)\)")
    return [(m.group(1), m.group(2).strip()) for m in pat.finditer(body)]


def parse_loadobjectmodel_c(path):
    """Parse the NATIVE_PORT sizepropdef switch: `sizeof(TYPE) / 4`."""
    text = open(path, encoding="utf-8").read()
    start = text.index("s32 sizepropdef(")
    end = text.index("#else", start)  # only the #ifdef NATIVE_PORT arm
    body = text[start:end]
    pat = re.compile(r"case\s+(PROPDEF_\w+):\s*return\s+sizeof\(([^)]+)\)\s*/\s*4")
    return [(m.group(1), m.group(2).strip()) for m in pat.finditer(body)]


def probe(cc, exprs):
    """Compile+run a probe that prints sizeof/offsetof for each request.

    exprs: list of ("SIZEOF", type_expr) or ("OFFSET", type_expr, member).
    Returns {index: int}.
    """
    lines = []
    for i, e in enumerate(exprs):
        if e[0] == "SIZEOF":
            lines.append(f'    printf("%d %zu\\n", {i}, sizeof({e[1]}));')
        else:
            lines.append(f'    printf("%d %zu\\n", {i}, offsetof({e[1]}, {e[2]}));')
    src = (
        "#include <stdio.h>\n#include <stddef.h>\n"
        '#include "bondtypes.h"\n'
        "int main(void){\n" + "\n".join(lines) + "\n    return 0;\n}\n"
    )
    with tempfile.TemporaryDirectory() as td:
        cpath = os.path.join(td, "probe.c")
        bpath = os.path.join(td, "probe")
        open(cpath, "w").write(src)
        cmd = [cc, "-w"]
        cmd += [f"-D{d}" for d in GAME_DEFINES]
        cmd += LAYOUT_FLAGS
        cmd += [f"-I{os.path.join(REPO_ROOT, inc)}" for inc in GAME_INCLUDES]
        cmd += [cpath, "-o", bpath]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            sys.stderr.write(r.stderr)
            raise SystemExit("probe compile failed")
        out = subprocess.run([bpath], capture_output=True, text=True, check=True)
    vals = {}
    for ln in out.stdout.strip().splitlines():
        idx, val = ln.split()
        vals[int(idx)] = int(val)
    return vals


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = ap.parse_args()

    prop = parse_prop_c(os.path.join(REPO_ROOT, "src/game/prop.c"))
    load = dict(parse_loadobjectmodel_c(
        os.path.join(REPO_ROOT, "src/game/loadobjectmodel.c")))

    # Cross-check the two hand-maintained tables agree on struct mapping.
    mismatches = []
    for ptype, pexpr in prop:
        if ptype in load and load[ptype] != pexpr:
            mismatches.append((ptype, pexpr, load[ptype]))

    # Unique struct-backed sizeof targets (table order), plus StandTile family.
    sizeof_types = []
    seen = set()
    for _, expr in prop:
        if expr not in seen:
            seen.add(expr)
            sizeof_types.append(expr)
    for t in STANDTILE_SIZEOF:
        if t not in seen:
            seen.add(t)
            sizeof_types.append(t)

    reqs = [("SIZEOF", t) for t in sizeof_types]
    reqs += [("OFFSET", t, m) for (t, m) in OFFSET_SPEC]
    vals = probe(args.cc, reqs)

    out = []
    out.append("/* ==== BEGIN generated by tools/fidelity/gen_struct_asserts.py ==== */")
    out.append("/* PROPDEF record sizes — sizepropdef_pc_bytes (prop.c) and the")
    out.append(" * NATIVE_PORT arm of sizepropdef (loadobjectmodel.c). */")
    for i, t in enumerate(sizeof_types):
        n = vals[i]
        out.append(
            f'_Static_assert(sizeof({t}) == {n}, '
            f'"sizeof({t}) drifted from locked {n} (0x{n:x})");')
    out.append("")
    out.append("/* Union / bitfield-adjacent field offsets (FID-0036) + StandTile"
               " navmesh family. */")
    base = len(sizeof_types)
    for j, (t, m) in enumerate(OFFSET_SPEC):
        n = vals[base + j]
        out.append(
            f'_Static_assert(offsetof({t}, {m}) == {n}, '
            f'"offsetof({t}, {m}) drifted from locked {n} (0x{n:x})");')
    out.append("/* ==== END generated ==== */")

    if mismatches:
        sys.stderr.write("WARNING: prop.c vs loadobjectmodel.c table mismatch:\n")
        for pt, a, b in mismatches:
            sys.stderr.write(f"  {pt}: prop.c={a} loadobjectmodel.c={b}\n")

    print("\n".join(out))


if __name__ == "__main__":
    main()
