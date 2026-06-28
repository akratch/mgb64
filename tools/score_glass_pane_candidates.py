#!/usr/bin/env python3
"""Rank glass panes for actor-clean visual-route scouting."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line_no, raw in enumerate(handle, 1):
            line = raw.strip()
            if not line:
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as exc:
                raise SystemExit(f"FAIL: {path}:{line_no}: invalid JSONL: {exc}") from exc
            if isinstance(record, dict):
                records.append(record)
    return records


def numeric_triplet(value: Any) -> list[float] | None:
    if (
        isinstance(value, list)
        and len(value) == 3
        and all(isinstance(item, (int, float)) and not isinstance(item, bool) for item in value)
    ):
        return [float(value[0]), float(value[1]), float(value[2])]
    return None


def distance(a: list[float], b: list[float]) -> float:
    return math.sqrt(sum((lhs - rhs) * (lhs - rhs) for lhs, rhs in zip(a, b)))


def parse_viewer(value: str | None) -> list[float] | None:
    if not value:
        return None
    parts = value.replace(",", ":").split(":")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("--viewer must be X:Y:Z")
    try:
        return [float(part) for part in parts]
    except ValueError as exc:
        raise argparse.ArgumentTypeError("--viewer must contain numeric X:Y:Z") from exc


def pane_from_stage_record(record: dict[str, Any]) -> dict[str, Any] | None:
    if record.get("scope") != "setup" or record.get("kind") != "object":
        return None
    type_name = record.get("type_name")
    if type_name not in ("glass", "tinted_glass"):
        return None
    pos = numeric_triplet(record.get("pos"))
    if pos is None:
        return None
    return {
        "index": record.get("index"),
        "type": record.get("type"),
        "type_name": type_name,
        "obj": record.get("obj"),
        "pad": record.get("pad"),
        "room": record.get("room"),
        "damage": record.get("damage"),
        "maxdamage": record.get("maxdamage"),
        "pos": pos,
    }


def pane_from_trace_sample(item: dict[str, Any]) -> dict[str, Any] | None:
    type_id = item.get("type")
    if type_id not in (42, 47):
        return None
    pos = numeric_triplet(item.get("pos"))
    if pos is None:
        return None
    return {
        "index": item.get("index"),
        "type": type_id,
        "type_name": "glass" if type_id == 42 else "tinted_glass",
        "obj": item.get("obj"),
        "pad": item.get("pad"),
        "room": item.get("room"),
        "damage": item.get("damage"),
        "maxdamage": item.get("maxdamage"),
        "pos": pos,
    }


def panes_from_trace(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    for record in records:
        props = record.get("glass_props")
        if not isinstance(props, dict):
            continue
        sample = props.get("sample")
        if not isinstance(sample, list):
            continue
        panes = [pane for item in sample if isinstance(item, dict) for pane in [pane_from_trace_sample(item)] if pane]
        if panes:
            return panes
    return []


def chr_from_stage_record(record: dict[str, Any]) -> dict[str, Any] | None:
    if record.get("scope") != "stage" or record.get("kind") != "chr":
        return None
    prop = record.get("prop")
    if not isinstance(prop, dict) or not prop.get("present"):
        return None
    pos = numeric_triplet([prop.get("x"), prop.get("y"), prop.get("z")])
    if pos is None:
        return None
    return {
        "slot": record.get("slot"),
        "chrnum": record.get("chrnum"),
        "hidden": record.get("hidden"),
        "alive": record.get("alive"),
        "action": record.get("action"),
        "alert": record.get("alert"),
        "sleep": record.get("sleep"),
        "padpreset1": record.get("padpreset1"),
        "room": prop.get("room"),
        "pos": pos,
    }


def chr_from_trace_actor(actor: dict[str, Any]) -> dict[str, Any] | None:
    pos = numeric_triplet(actor.get("pos"))
    if pos is None:
        return None
    return {
        "slot": actor.get("slot"),
        "chrnum": actor.get("chrnum"),
        "hidden": actor.get("hidden"),
        "alive": actor.get("alive"),
        "action": actor.get("action"),
        "alert": actor.get("alert"),
        "sleep": actor.get("sleep"),
        "padpreset1": actor.get("padpreset1"),
        "room": actor.get("room"),
        "onscreen": actor.get("onscreen"),
        "rendered": actor.get("rendered"),
        "pos": pos,
    }


def chrs_from_trace(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    best: list[dict[str, Any]] = []
    for record in records:
        actors = record.get("actors")
        if not isinstance(actors, dict):
            continue
        sample = actors.get("sample")
        if not isinstance(sample, list):
            continue
        current = [chr_ for actor in sample if isinstance(actor, dict) for chr_ in [chr_from_trace_actor(actor)] if chr_]
        if len(current) > len(best):
            best = current
    return best


def load_panes(stage_pads: Path | None, trace: Path | None) -> list[dict[str, Any]]:
    if stage_pads:
        panes = [pane for record in load_jsonl(stage_pads) for pane in [pane_from_stage_record(record)] if pane]
        if panes:
            return panes
    if trace:
        return panes_from_trace(load_jsonl(trace))
    return []


def load_chrs(stage_chrs: Path | None, trace: Path | None) -> list[dict[str, Any]]:
    if stage_chrs:
        chrs = [chr_ for record in load_jsonl(stage_chrs) for chr_ in [chr_from_stage_record(record)] if chr_]
        if chrs:
            return chrs
    if trace:
        return chrs_from_trace(load_jsonl(trace))
    return []


def nearest_chrs(pane: dict[str, Any], chrs: list[dict[str, Any]], limit: int) -> list[dict[str, Any]]:
    pane_pos = pane["pos"]
    ranked: list[dict[str, Any]] = []
    for chr_ in chrs:
        if chr_.get("alive") == 0:
            continue
        chr_pos = chr_.get("pos")
        if not isinstance(chr_pos, list):
            continue
        ranked.append({
            "chrnum": chr_.get("chrnum"),
            "slot": chr_.get("slot"),
            "distance": distance(pane_pos, chr_pos),
            "hidden": chr_.get("hidden"),
            "action": chr_.get("action"),
            "room": chr_.get("room"),
            "pos": chr_pos,
        })
    ranked.sort(key=lambda item: item["distance"])
    return ranked[:limit]


def rank_panes(
    panes: list[dict[str, Any]],
    chrs: list[dict[str, Any]],
    *,
    viewer: list[float] | None,
    include_tinted: bool,
    nearest_limit: int,
) -> list[dict[str, Any]]:
    ranked: list[dict[str, Any]] = []
    for pane in panes:
        if not include_tinted and pane.get("type_name") != "glass":
            continue
        nearest = nearest_chrs(pane, chrs, nearest_limit)
        nearest_distance = nearest[0]["distance"] if nearest else None
        near_250 = sum(1 for item in nearest if item["distance"] <= 250.0)
        near_500 = sum(1 for item in nearest if item["distance"] <= 500.0)
        near_1000 = sum(1 for item in nearest if item["distance"] <= 1000.0)
        viewer_distance = distance(pane["pos"], viewer) if viewer is not None else None
        score = (nearest_distance if nearest_distance is not None else 100000.0)
        score -= near_250 * 2000.0
        score -= near_500 * 750.0
        score -= near_1000 * 100.0
        if viewer_distance is not None:
            score -= max(0.0, 3000.0 - viewer_distance) * 0.05
        ranked.append({
            "score": score,
            "index": pane.get("index"),
            "pad": pane.get("pad"),
            "type": pane.get("type"),
            "type_name": pane.get("type_name"),
            "obj": pane.get("obj"),
            "room": pane.get("room"),
            "pos": pane.get("pos"),
            "damage": pane.get("damage"),
            "maxdamage": pane.get("maxdamage"),
            "nearest_chr_distance": nearest_distance,
            "near_chr_counts": {"250": near_250, "500": near_500, "1000": near_1000},
            "viewer_distance": viewer_distance,
            "nearest_chrs": nearest,
        })
    ranked.sort(
        key=lambda item: (
            item["near_chr_counts"]["250"],
            item["near_chr_counts"]["500"],
            item["near_chr_counts"]["1000"],
            -(item["nearest_chr_distance"] or 0.0),
            item["viewer_distance"] if item["viewer_distance"] is not None else 0.0,
        )
    )
    return ranked


def print_rows(ranked: list[dict[str, Any]], top: int) -> None:
    for item in ranked[:top]:
        nearest = item["nearest_chrs"][0] if item["nearest_chrs"] else {}
        nearest_desc = (
            f"chr={nearest.get('chrnum')} dist={nearest.get('distance'):.1f}"
            if nearest
            else "none"
        )
        viewer = item.get("viewer_distance")
        viewer_desc = f" viewer={viewer:.1f}" if viewer is not None else ""
        counts = item["near_chr_counts"]
        print(
            f"pad={item['pad']} index={item['index']} type={item['type_name']} "
            f"nearest={nearest_desc} near250={counts['250']} near500={counts['500']} "
            f"near1000={counts['1000']}{viewer_desc} pos={item['pos']}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage-pads", type=Path, help="GE007_DUMP_STAGE_PADS JSONL")
    parser.add_argument("--stage-chrs", type=Path, help="GE007_DUMP_STAGE_CHRS JSONL")
    parser.add_argument("--trace", type=Path, help="fallback route JSONL with glass_props/actors samples")
    parser.add_argument("--viewer", type=parse_viewer, help="optional X:Y:Z viewer position")
    parser.add_argument("--include-tinted", action="store_true")
    parser.add_argument("--top", type=int, default=20)
    parser.add_argument("--nearest", type=int, default=6)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    if args.top < 1:
        parser.error("--top must be positive")
    if args.nearest < 1:
        parser.error("--nearest must be positive")
    if not args.stage_pads and not args.trace:
        parser.error("provide --stage-pads or --trace")
    if args.stage_chrs is None and args.trace is None:
        parser.error("provide --stage-chrs or --trace")

    panes = load_panes(args.stage_pads, args.trace)
    chrs = load_chrs(args.stage_chrs, args.trace)
    ranked = rank_panes(
        panes,
        chrs,
        viewer=args.viewer,
        include_tinted=args.include_tinted,
        nearest_limit=args.nearest,
    )

    payload = {
        "stage_pads": str(args.stage_pads) if args.stage_pads else None,
        "stage_chrs": str(args.stage_chrs) if args.stage_chrs else None,
        "trace": str(args.trace) if args.trace else None,
        "viewer": args.viewer,
        "include_tinted": args.include_tinted,
        "pane_count": len(panes),
        "chr_count": len(chrs),
        "ranked": ranked,
    }

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(f"panes={len(panes)} chrs={len(chrs)} ranked={len(ranked)}")
    print_rows(ranked, args.top)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
