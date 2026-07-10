#!/usr/bin/env python3
"""Compare the combat/floor oracle block (``combat_oracle``) between a native and an
ares (or any two) JSONL traces.

Emitted by both the instrumented-ares tracer and the native ``--trace-state`` path
(FID-0032). Schema parity is the contract; this tool aligns matched frames and reports
per-field divergences. Per S-Tier plan Task 1.1 step 3, *alignment succeeding* is the
pass criterion — divergences are the product (candidate findings), not a failure. Use
``--strict`` to also fail on any divergence.

Modelled on ``tools/compare_movement_trace.py`` (alignment + per-field tolerance table).
See ``docs/fidelity/combat_oracle_fields.md`` for the field/offset dossier.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

# --- tolerance table (dossier §5) ------------------------------------------------

# Integer fields: compared exactly (mismatch = candidate finding).
COMBAT_INT_FIELDS = ["shots_fired_total", "hits_landed_total"]
COMBAT_HEX_FIELDS = ["rng_seed"]
COMBAT_FLOAT_FIELDS = ["player_health", "player_armor"]

FLOOR_INT_FIELDS = ["stan_id", "stan_room", "stan_flags"]
FLOOR_FLOAT_FIELDS = ["height"]

GUARD_INT_FIELDS = ["actiontype", "aimode", "flags_onscreen", "target_visible", "room"]
GUARD_HEX_FIELDS = ["anim_hash"]
GUARD_FLOAT_FIELDS = ["health", "shotbondsum"]

PROJ_INT_FIELDS = ["kind", "owner_chrnum"]


def load_trace(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    skipped = 0
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line or not line.startswith("{"):
                continue
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError:
                skipped += 1
    if skipped:
        print(f"WARNING: skipped {skipped} corrupted JSONL line(s) in {path}", file=sys.stderr)
    return records


def parse_int(value: Any) -> int | None:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            return None
    return None


def parse_float(value: Any) -> float | None:
    if isinstance(value, bool):
        return float(value)
    if isinstance(value, (int, float)):
        return float(value)
    if isinstance(value, str):
        try:
            return float(value)
        except ValueError:
            return None
    return None


def key_for(record: dict[str, Any], mode: str) -> Any:
    move = record.get("move", {})
    if mode == "global":
        return move.get("global") if isinstance(move, dict) else None
    if mode == "frame":
        return record.get("f")
    if mode == "clock":
        return move.get("clock") if isinstance(move, dict) else None
    raise AssertionError(mode)


def first_moving_index(records: list[dict[str, Any]], threshold: float) -> int:
    """First record whose player move.speed/raw exceeds ``threshold`` (motion onset).

    Mirrors ``compare_movement_trace.first_moving_index`` so a native direct-boot
    trace (g_GlobalTimer starts at 0 in gameplay) can be anchored against an ares
    menu-boot trace (gameplay begins well after the menu) by motion onset rather
    than absolute timer, which otherwise pairs native-gameplay with ares-menu frames.
    """
    for index, record in enumerate(records):
        move = record.get("move", {})
        if not isinstance(move, dict):
            continue
        speed = move.get("speed") or []
        raw = move.get("raw") or []
        for seq in (speed, raw):
            if isinstance(seq, list):
                for value in seq:
                    if isinstance(value, (int, float)) and abs(float(value)) > threshold:
                        return index
    return 0


def align_records(
    baseline: list[dict[str, Any]],
    test: list[dict[str, Any]],
    mode: str,
    motion_threshold: float = 0.01,
) -> list[tuple[Any, dict[str, Any], dict[str, Any]]]:
    if mode == "index":
        count = min(len(baseline), len(test))
        return [(i, baseline[i], test[i]) for i in range(count)]
    if mode == "move":
        baseline_start = first_moving_index(baseline, motion_threshold)
        test_start = first_moving_index(test, motion_threshold)
        count = min(len(baseline) - baseline_start, len(test) - test_start)
        return [
            (i, baseline[baseline_start + i], test[test_start + i])
            for i in range(max(0, count))
        ]

    def by_key(records: list[dict[str, Any]]) -> dict[Any, dict[str, Any]]:
        out: dict[Any, dict[str, Any]] = {}
        for record in records:
            k = key_for(record, mode)
            if k is None:
                continue
            out.setdefault(k, record)
        return out

    b = by_key(baseline)
    t = by_key(test)
    keys = sorted(set(b) & set(t), key=lambda x: (isinstance(x, str), x))
    return [(k, b[k], t[k]) for k in keys]


def combat_records(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [r for r in records if isinstance(r.get("combat_oracle"), dict)]


class Divergence:
    __slots__ = ("frame", "path", "baseline", "test")

    def __init__(self, frame: Any, path: str, baseline: Any, test: Any) -> None:
        self.frame = frame
        self.path = path
        self.baseline = baseline
        self.test = test

    def as_dict(self) -> dict[str, Any]:
        return {"frame": self.frame, "field": self.path, "baseline": self.baseline, "test": self.test}


def cmp_int(frame, prefix, key, bval, tval, out: list[Divergence]) -> None:
    bi, ti = parse_int(bval), parse_int(tval)
    if bi != ti:
        out.append(Divergence(frame, f"{prefix}.{key}", bval, tval))


def cmp_float(frame, prefix, key, bval, tval, tol, out: list[Divergence]) -> None:
    bf, tf = parse_float(bval), parse_float(tval)
    if bf is None or tf is None:
        if bf is not tf:
            out.append(Divergence(frame, f"{prefix}.{key}", bval, tval))
        return
    if abs(bf - tf) > tol:
        out.append(Divergence(frame, f"{prefix}.{key}", bval, tval))


def compare_combat_block(frame, base, test, tols, out: list[Divergence]) -> None:
    b = base.get("combat", {}) if isinstance(base.get("combat"), dict) else {}
    t = test.get("combat", {}) if isinstance(test.get("combat"), dict) else {}
    for key in COMBAT_INT_FIELDS:
        cmp_int(frame, "combat", key, b.get(key), t.get(key), out)
    for key in COMBAT_HEX_FIELDS:
        cmp_int(frame, "combat", key, b.get(key), t.get(key), out)
    for key in COMBAT_FLOAT_FIELDS:
        cmp_float(frame, "combat", key, b.get(key), t.get(key), tols["health"], out)


def compare_floor_block(frame, base, test, tols, out: list[Divergence]) -> None:
    b = base.get("floor", {}) if isinstance(base.get("floor"), dict) else {}
    t = test.get("floor", {}) if isinstance(test.get("floor"), dict) else {}
    for key in FLOOR_INT_FIELDS:
        cmp_int(frame, "floor", key, b.get(key), t.get(key), out)
    for key in FLOOR_FLOAT_FIELDS:
        cmp_float(frame, "floor", key, b.get(key), t.get(key), tols["position"], out)


def guards_by_chrnum(block: dict[str, Any]) -> dict[int, dict[str, Any]]:
    out: dict[int, dict[str, Any]] = {}
    for g in block.get("guards", []) or []:
        if not isinstance(g, dict):
            continue
        cn = parse_int(g.get("chrnum"))
        if cn is None:
            continue
        out.setdefault(cn, g)
    return out


def compare_guards(frame, base, test, tols, out: list[Divergence]) -> None:
    b = guards_by_chrnum(base)
    t = guards_by_chrnum(test)
    for cn in sorted(set(b) | set(t)):
        if cn not in b or cn not in t:
            out.append(Divergence(frame, f"guards[chr={cn}].present",
                                   cn in b, cn in t))
            continue
        gb, gt = b[cn], t[cn]
        prefix = f"guards[chr={cn}]"
        for key in GUARD_INT_FIELDS:
            cmp_int(frame, prefix, key, gb.get(key), gt.get(key), out)
        for key in GUARD_HEX_FIELDS:
            cmp_int(frame, prefix, key, gb.get(key), gt.get(key), out)
        for key in GUARD_FLOAT_FIELDS:
            cmp_float(frame, prefix, key, gb.get(key), gt.get(key), tols["health"], out)
        pb, pt = gb.get("pos"), gt.get("pos")
        if isinstance(pb, list) and isinstance(pt, list) and len(pb) == 3 and len(pt) == 3:
            for i in range(3):
                cmp_float(frame, f"{prefix}.pos", str(i), pb[i], pt[i], tols["position"], out)


def compare_projectiles(frame, base, test, tols, out: list[Divergence]) -> None:
    pb = base.get("projectiles", []) or []
    pt = test.get("projectiles", []) or []
    if len(pb) != len(pt):
        out.append(Divergence(frame, "projectiles.count", len(pb), len(pt)))
    for i in range(min(len(pb), len(pt))):
        a, c = pb[i], pt[i]
        if not (isinstance(a, dict) and isinstance(c, dict)):
            continue
        prefix = f"projectiles[{i}]"
        for key in PROJ_INT_FIELDS:
            cmp_int(frame, prefix, key, a.get(key), c.get(key), out)
        qa, qc = a.get("pos"), c.get("pos")
        if isinstance(qa, list) and isinstance(qc, list) and len(qa) == 3 and len(qc) == 3:
            for k in range(3):
                cmp_float(frame, f"{prefix}.pos", str(k), qa[k], qc[k], tols["position"], out)


def compare(args: argparse.Namespace) -> int:
    baseline_path = Path(args.baseline)
    test_path = Path(args.test)
    baseline_all = load_trace(baseline_path)
    test_all = load_trace(test_path)

    baseline = combat_records(baseline_all)
    test = combat_records(test_all)

    tols = {
        "position": args.position_tolerance,
        "health": args.health_tolerance,
    }

    aligned = align_records(baseline, test, args.align, args.motion_threshold)

    divergences: list[Divergence] = []
    field_counts: dict[str, int] = {}
    for frame, brec, trec in aligned:
        base = brec["combat_oracle"]
        tst = trec["combat_oracle"]
        before = len(divergences)
        compare_combat_block(frame, base, tst, tols, divergences)
        compare_floor_block(frame, base, tst, tols, divergences)
        compare_guards(frame, base, tst, tols, divergences)
        compare_projectiles(frame, base, tst, tols, divergences)
        for d in divergences[before:]:
            # collapse per-field-family for the summary
            fam = d.path.split("[")[0].split(".")[0] + "." + d.path.rsplit(".", 1)[-1]
            field_counts[fam] = field_counts.get(fam, 0) + 1

    metrics: dict[str, Any] = {
        "baseline": str(baseline_path),
        "test": str(test_path),
        "align": args.align,
        "tolerances": tols,
        "record_counts": {
            "baseline_all": len(baseline_all),
            "test_all": len(test_all),
            "baseline_combat": len(baseline),
            "test_combat": len(test),
        },
        "aligned_frames": len(aligned),
        "divergences_total": len(divergences),
        "divergences_by_field": dict(sorted(field_counts.items(), key=lambda kv: -kv[1])),
        "sample_divergences": [d.as_dict() for d in divergences[: args.max_report]],
    }

    if args.json_out:
        Path(args.json_out).write_text(json.dumps(metrics, indent=2, sort_keys=True) + "\n",
                                       encoding="utf-8")

    print(json.dumps(metrics["divergences_by_field"], indent=2))
    print(f"aligned_frames={len(aligned)} divergences_total={len(divergences)}")

    if len(aligned) < args.min_aligned:
        print(f"FAIL: only {len(aligned)} aligned frames (< --min-aligned {args.min_aligned}); "
              f"alignment did not succeed", file=sys.stderr)
        return 2

    if args.strict and divergences:
        print(f"FAIL(strict): {len(divergences)} field divergence(s)", file=sys.stderr)
        return 1

    print(f"OK: alignment succeeded ({len(aligned)} frames); "
          f"{len(divergences)} divergence(s) reported as findings")
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--baseline", required=True, help="ares (or reference) JSONL trace")
    p.add_argument("--test", required=True, help="native (or candidate) JSONL trace")
    p.add_argument("--align", default="global",
                   choices=["global", "frame", "clock", "index", "move"],
                   help="frame alignment key (default: global = g_GlobalTimer). "
                        "'move' anchors both sides on motion onset (native direct-boot "
                        "vs ares menu-boot) — the correct mode for combat routes.")
    p.add_argument("--motion-threshold", type=float, default=0.01,
                   help="min |move.speed/raw| to count as motion onset for --align move")
    p.add_argument("--position-tolerance", type=float, default=0.5,
                   help="float position/height epsilon (world units)")
    p.add_argument("--health-tolerance", type=float, default=1.0,
                   help="float health/shotbondsum epsilon (raw units)")
    p.add_argument("--min-aligned", type=int, default=1,
                   help="minimum aligned frames for alignment to count as success")
    p.add_argument("--max-report", type=int, default=50,
                   help="max sample divergences embedded in the metrics JSON")
    p.add_argument("--strict", action="store_true",
                   help="also exit non-zero if any field diverges (default: divergences are findings)")
    p.add_argument("--json-out", help="write full metrics JSON to this path")
    return p


def main(argv: list[str]) -> int:
    return compare(build_parser().parse_args(argv))


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
