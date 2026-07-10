#!/usr/bin/env python3
"""rdp_fingerprint.py — normalize + diff RDP render-mode semantics for the S3
native/ares RDP command-stream differential lane (FID-0043).

Two sources of RDP semantics, one comparable axis:

* NATIVE — the fast3d renderer's `[RDP-MODE] ...` stderr trace, emitted under
  GE007_TRACE_RDP_RENDER_MODES=1 (src/platform/fast3d/gfx_pc.c). Each line carries
  the effective RDP other-mode word (eff / omh), combine mode (effcc), the blend
  class, and the decoded coverage/blend micro-state (decode={... cvg zcmp zupd aa
  cvg_x_alpha alpha_cvg force_bl b1 b2}).
* ARES (stock) — the instrumented ares RDP command sidecar, produced under
  MGB64_ARES_TRACE_RDP_COMMANDS=1 and summarized by
  tools/analyze_stock_rdp_command_stream.py --json. Its draw-states expose the
  same RDP other-mode / combine / coverage configuration the hardware executed.

A "fingerprint" is a Counter over normalized RDP-configuration keys (the distinct
render-mode/blend/coverage tuples actually used), so the two streams can be
compared by which RDP configurations each side emits and how often. Per the
S-Tier sense-lane contract, alignment/coverage succeeding is the pass criterion;
config divergences are the PRODUCT (candidate findings), not a hard failure —
`diff --strict` flips that for a gate.

Subcommands:
  native  TRACE            -> fingerprint JSON on stdout (or --out)
  ares    ANALYZER_JSON    -> fingerprint JSON on stdout (or --out)
  diff    FP_A FP_B        -> report config keys only-in-A / only-in-B / count
                             deltas; exit 0 (report) or 1 (--strict + divergent)
  selftest                 -> ROM/ares-free proof the diff catches a seeded
                             divergence (the lane's always-on guarantee)
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path

# RDP-semantic fields lifted from a `[RDP-MODE]` line. These, not the tri index or
# pointers, define the render-mode/blend/coverage configuration.
_KV = re.compile(r"(\w+)=(0x[0-9A-Fa-f]+|[-\w.]+)")
_DECODE = re.compile(r"decode=\{([^}]*)\}")
_SEMANTIC_KEYS = (
    "class", "eff", "omh", "effcc", "blend", "api_blend", "rdp_mem",
)
_DECODE_KEYS = (
    "z", "cvg", "zcmp", "zupd", "aa", "cvg_x_alpha", "alpha_cvg", "force_bl",
    "b1", "b2",
)


def _config_key(fields: dict) -> str:
    """A stable, order-independent key over the RDP-semantic subset."""
    parts = [f"{k}={fields.get(k, '?')}" for k in _SEMANTIC_KEYS]
    parts += [f"{k}={fields.get(k, '?')}" for k in _DECODE_KEYS]
    return "|".join(parts)


def parse_native_line(line: str) -> dict | None:
    if "[RDP-MODE]" not in line:
        return None
    fields = {k: v for k, v in _KV.findall(line)}
    dm = _DECODE.search(line)
    if dm:
        # decode uses "b1=(0,3,0,2)" tuples that the flat _KV misses.
        for k, v in re.findall(r"(\w+)=(\([^)]*\)|[-\w]+)", dm.group(1)):
            fields[k] = v
    return fields


def fingerprint_native(trace_path: str) -> Counter:
    fp: Counter = Counter()
    with open(trace_path, "r", errors="replace") as fh:
        for line in fh:
            fields = parse_native_line(line)
            if fields is not None:
                fp[_config_key(fields)] += 1
    return fp


def _ares_fields_from_drawstate(ds: dict) -> dict:
    """Map an analyze_stock_rdp_command_stream draw-state onto the native key
    space. The analyzer names vary by version; accept several spellings and fall
    back to '?' so the common axis (whatever both expose) still compares."""
    def g(*names, default="?"):
        for n in names:
            if n in ds and ds[n] not in (None, ""):
                return ds[n]
        return default
    return {
        "class": g("class", "draw_class", "kind", default="ares"),
        "eff": g("othermode_l", "omode_l", "other_mode_lo", "eff"),
        "omh": g("othermode_h", "omode_h", "other_mode_hi", "omh"),
        "effcc": g("combine", "cc", "combine_mode", "effcc"),
        "blend": g("blend", "blender", default="?"),
        "api_blend": g("api_blend", default="?"),
        "rdp_mem": g("rdp_mem", "cvg_dest", default="?"),
        "z": g("z", "z_mode", default="?"),
        "cvg": g("cvg", "cvg_dest", default="?"),
        "zcmp": g("zcmp", "z_compare", default="?"),
        "zupd": g("zupd", "z_update", default="?"),
        "aa": g("aa", "aa_en", default="?"),
        "cvg_x_alpha": g("cvg_x_alpha", default="?"),
        "alpha_cvg": g("alpha_cvg", "alpha_cvg_sel", default="?"),
        "force_bl": g("force_bl", "force_blend", default="?"),
        "b1": g("b1", "blend1", default="?"),
        "b2": g("b2", "blend2", default="?"),
    }


def fingerprint_ares(analyzer_json_path: str) -> Counter:
    data = json.load(open(analyzer_json_path))
    fp: Counter = Counter()
    # Accept several shapes: {"draw_states":[...]} or {"top_draw_states":[...]}.
    states = (
        data.get("draw_states")
        or data.get("top_draw_states")
        or data.get("drawStates")
        or []
    )
    for ds in states:
        if not isinstance(ds, dict):
            continue
        count = ds.get("count") or ds.get("draws") or ds.get("n") or 1
        try:
            count = int(count)
        except (TypeError, ValueError):
            count = 1
        fp[_config_key(_ares_fields_from_drawstate(ds))] += count
    return fp


def diff_fingerprints(fp_a: Counter, fp_b: Counter) -> dict:
    keys = set(fp_a) | set(fp_b)
    only_a = sorted(k for k in keys if k in fp_a and k not in fp_b)
    only_b = sorted(k for k in keys if k in fp_b and k not in fp_a)
    shared_delta = {
        k: [fp_a[k], fp_b[k]] for k in sorted(keys)
        if k in fp_a and k in fp_b and fp_a[k] != fp_b[k]
    }
    return {
        "configs_a": len(fp_a),
        "configs_b": len(fp_b),
        "shared_configs": len(set(fp_a) & set(fp_b)),
        "only_in_a": only_a,
        "only_in_b": only_b,
        "shared_count_deltas": shared_delta,
        "divergences": len(only_a) + len(only_b) + len(shared_delta),
    }


def _load_fp(path: str) -> Counter:
    obj = json.load(open(path))
    return Counter(obj.get("fingerprint", obj))


def _emit_fp(fp: Counter, out: str | None) -> None:
    payload = {"configs": len(fp), "total": sum(fp.values()), "fingerprint": dict(fp)}
    text = json.dumps(payload, indent=2, sort_keys=True)
    if out:
        Path(out).write_text(text + "\n")
    else:
        print(text)


def cmd_selftest() -> int:
    base = (
        "[RDP-MODE] frame=20 tri=0 class=room eff=0x0F0A4000 omh=0x00B82C20 "
        "effcc=0x0080080008008000 blend=disabled api_blend=disabled rdp_mem=none "
        "decode={z=opa cvg=clamp zcmp=0 zupd=0 aa=0 imrd=0 clr_on_cvg=0 "
        "cvg_x_alpha=0 alpha_cvg=0 force_bl=1 b1=(0,3,0,2) b2=(0,3,0,2)}"
    )
    # A perturbed line: force_bl flips and the blend words change -> a genuine,
    # different RDP render-mode configuration the differ MUST surface.
    perturbed = base.replace("force_bl=1", "force_bl=0").replace(
        "b1=(0,3,0,2) b2=(0,3,0,2)", "b1=(0,3,1,0) b2=(0,3,1,0)"
    )
    fp_ref = Counter()
    fp_ref[_config_key(parse_native_line(base))] += 3
    # identical fingerprint -> zero divergences
    same = diff_fingerprints(fp_ref, fp_ref.copy())
    # perturbed fingerprint -> must report a divergence
    fp_bad = Counter()
    fp_bad[_config_key(parse_native_line(base))] += 2
    fp_bad[_config_key(parse_native_line(perturbed))] += 1
    changed = diff_fingerprints(fp_ref, fp_bad)
    ok = (
        same["divergences"] == 0
        and changed["divergences"] >= 1
        and len(changed["only_in_b"]) == 1
        and changed["shared_count_deltas"]  # the shared key's count dropped 3->2
    )
    if ok:
        print("rdp-fingerprint selftest: PASS "
              f"(identical={same['divergences']} divergent={changed['divergences']}: "
              "diff catches a seeded RDP render-mode divergence)")
        return 0
    print("rdp-fingerprint selftest: FAIL", json.dumps({"same": same, "changed": changed}),
          file=sys.stderr)
    return 1


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    pn = sub.add_parser("native"); pn.add_argument("trace"); pn.add_argument("--out")
    pa = sub.add_parser("ares"); pa.add_argument("analyzer_json"); pa.add_argument("--out")
    pd = sub.add_parser("diff"); pd.add_argument("fp_a"); pd.add_argument("fp_b")
    pd.add_argument("--out"); pd.add_argument("--strict", action="store_true")
    pd.add_argument("--max-report", type=int, default=25)
    sub.add_parser("selftest")
    args = p.parse_args()

    if args.cmd == "native":
        _emit_fp(fingerprint_native(args.trace), args.out); return 0
    if args.cmd == "ares":
        _emit_fp(fingerprint_ares(args.analyzer_json), args.out); return 0
    if args.cmd == "diff":
        report = diff_fingerprints(_load_fp(args.fp_a), _load_fp(args.fp_b))
        text = json.dumps(report, indent=2)
        if args.out:
            Path(args.out).write_text(text + "\n")
        # console summary (capped)
        print(f"rdp-diff: A={report['configs_a']} configs, B={report['configs_b']} "
              f"configs, shared={report['shared_configs']}, "
              f"divergences={report['divergences']}")
        for k in report["only_in_a"][:args.max_report]:
            print(f"  only-in-A: {k}")
        for k in report["only_in_b"][:args.max_report]:
            print(f"  only-in-B: {k}")
        if args.strict and report["divergences"] > 0:
            print("rdp-diff: STRICT FAIL — RDP render-mode configurations diverge",
                  file=sys.stderr)
            return 1
        return 0
    if args.cmd == "selftest":
        return cmd_selftest()
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
