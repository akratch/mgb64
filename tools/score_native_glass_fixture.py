#!/usr/bin/env python3
"""Score a native glass fixture trace for active-shard visual cleanliness."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any


def load_records(path: Path) -> list[dict[str, Any]]:
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


def as_int(value: Any) -> int | None:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float) and math.isfinite(value):
        return int(value)
    return None


def as_float(value: Any) -> float | None:
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        value = float(value)
        return value if math.isfinite(value) else None
    return None


def triplet(value: Any) -> list[float] | None:
    if (
        isinstance(value, list)
        and len(value) == 3
        and all(isinstance(item, (int, float)) and not isinstance(item, bool) for item in value)
    ):
        return [float(value[0]), float(value[1]), float(value[2])]
    return None


def distance(lhs: list[float], rhs: list[float]) -> float:
    return math.sqrt(sum((a - b) * (a - b) for a, b in zip(lhs, rhs)))


def actor_summary(actor: dict[str, Any], viewer: list[float] | None) -> dict[str, Any]:
    pos = triplet(actor.get("pos"))
    return {
        "slot": as_int(actor.get("slot")),
        "chrnum": as_int(actor.get("chrnum")),
        "action": as_int(actor.get("action")),
        "alert": as_int(actor.get("alert")),
        "sleep": as_int(actor.get("sleep")),
        "hidden": as_int(actor.get("hidden")),
        "hidden_bits": as_int(actor.get("hidden_bits")),
        "alive": as_int(actor.get("alive")),
        "onscreen": as_int(actor.get("onscreen")),
        "rendered": as_int(actor.get("rendered")),
        "room": as_int(actor.get("room")),
        "dist": as_float(actor.get("dist")),
        "viewer_dist": distance(pos, viewer) if pos is not None and viewer is not None else None,
        "pos": pos,
    }


def actor_samples(record: dict[str, Any]) -> list[dict[str, Any]]:
    actors = record.get("actors")
    if not isinstance(actors, dict):
        return []
    sample = actors.get("sample")
    if not isinstance(sample, list):
        return []
    return [actor for actor in sample if isinstance(actor, dict)]


def visible_actors(record: dict[str, Any], viewer: list[float] | None) -> list[dict[str, Any]]:
    actors = []
    for actor in actor_samples(record):
        if as_int(actor.get("onscreen")) or as_int(actor.get("rendered")):
            actors.append(actor_summary(actor, viewer))
    actors.sort(
        key=lambda item: (
            0 if item.get("onscreen") else 1,
            item.get("viewer_dist") if item.get("viewer_dist") is not None else 1e9,
            item.get("chrnum") if item.get("chrnum") is not None else 1e9,
        )
    )
    return actors


def health_summary(record: dict[str, Any]) -> dict[str, Any]:
    health = record.get("combat", {}).get("health")
    if not isinstance(health, dict):
        health = {}
    return {
        "bond": as_float(health.get("bond")),
        "armor": as_float(health.get("armor")),
        "damage_show": as_int(health.get("damage_show")),
        "health_show": as_int(health.get("health_show")),
    }


def pane_records(record: dict[str, Any]) -> list[dict[str, Any]]:
    props = record.get("glass_props")
    if not isinstance(props, dict):
        return []
    sample = props.get("sample")
    if not isinstance(sample, list):
        return []
    return [pane for pane in sample if isinstance(pane, dict)]


def pane_summary(pane: dict[str, Any]) -> dict[str, Any]:
    return {
        "index": as_int(pane.get("index")),
        "type": as_int(pane.get("type")),
        "obj": as_int(pane.get("obj")),
        "pad": as_int(pane.get("pad")),
        "state": as_int(pane.get("state")),
        "remove": as_int(pane.get("remove")),
        "destroyed_level": as_int(pane.get("destroyed_level")),
        "shots": as_int(pane.get("shots")),
        "damage": as_float(pane.get("damage")),
        "maxdamage": as_float(pane.get("maxdamage")),
        "pos": triplet(pane.get("pos")),
    }


def target_pane(record: dict[str, Any], target_pad: int | None) -> dict[str, Any] | None:
    if target_pad is None:
        return None
    for pane in pane_records(record):
        if as_int(pane.get("pad")) == target_pad:
            return pane_summary(pane)
    return None


def destroyed_pads(record: dict[str, Any]) -> list[int]:
    pads: set[int] = set()
    props = record.get("glass_props")
    if not isinstance(props, dict):
        return []
    for key in ("first_destroyed", "first_removed"):
        item = props.get(key)
        if isinstance(item, dict):
            pad = as_int(item.get("pad"))
            if pad is not None and pad >= 0:
                pads.add(pad)
    for pane in pane_records(record):
        pad = as_int(pane.get("pad"))
        if pad is None:
            continue
        if as_int(pane.get("remove")) or (as_int(pane.get("destroyed_level")) or 0) > 0:
            pads.add(pad)
    return sorted(pads)


def glass_summary(record: dict[str, Any]) -> dict[str, Any]:
    glass = record.get("glass")
    if not isinstance(glass, dict):
        return {"active": 0, "first_timer": None, "first_piece": None}
    first = glass.get("first")
    if not isinstance(first, dict):
        first = {}
    return {
        "active": as_int(glass.get("active")) or 0,
        "first_timer": as_int(first.get("timer")),
        "first_piece": as_int(first.get("piece")),
        "first_pos": triplet(first.get("pos")),
        "hash": glass.get("hash"),
    }


def find_first_active(records: list[dict[str, Any]]) -> dict[str, Any] | None:
    for record in records:
        if record.get("p") != 1:
            continue
        if (record.get("glass") or {}).get("active", 0):
            return record
    return None


def max_active(records: list[dict[str, Any]]) -> int:
    value = 0
    for record in records:
        glass = record.get("glass")
        if not isinstance(glass, dict):
            continue
        active = as_int(glass.get("active")) or 0
        value = max(value, active)
    return value


def score_payload(records: list[dict[str, Any]], trace: Path, target_pad: int | None) -> dict[str, Any]:
    first = find_first_active(records)
    last_record = next((record for record in reversed(records) if record.get("p") == 1), records[-1] if records else {})
    if first is None:
        first = last_record
    viewer = triplet(first.get("pos"))
    move = first.get("move")
    if not isinstance(move, dict):
        move = {}

    target = target_pane(first, target_pad)
    destroyed = destroyed_pads(first)
    target_destroyed = target_pad in destroyed if target_pad is not None else False
    glass = glass_summary(first)
    visible = visible_actors(first, viewer)
    onscreen_count = sum(1 for actor in visible if actor.get("onscreen"))
    rendered_count = sum(1 for actor in visible if actor.get("rendered"))
    nearest_visible = min(
        (actor.get("viewer_dist") for actor in visible if actor.get("viewer_dist") is not None),
        default=None,
    )
    health = health_summary(first)
    hud_active = int((health.get("damage_show") or -1) >= 0) + int((health.get("health_show") or -1) >= 0)
    score = 0.0
    if glass["active"] <= 0:
        score += 100000.0
    if target_pad is not None and not target_destroyed:
        score += 50000.0
    score += onscreen_count * 2000.0
    score += rendered_count * 500.0
    score += hud_active * 1000.0
    if nearest_visible is not None:
        score += max(0.0, 1200.0 - nearest_visible)
    score += max(0, 90 - (max_active(records) or 0)) * 100.0

    return {
        "trace": str(trace),
        "target_pad": target_pad,
        "score": score,
        "first_active_present": glass["active"] > 0,
        "frame": as_int(first.get("f")),
        "global": as_int(move.get("global")),
        "clock": as_int(move.get("clock")),
        "max_active": max_active(records),
        "glass": glass,
        "destroyed_pads": destroyed,
        "target_destroyed": target_destroyed,
        "target_pane": target,
        "health": health,
        "visible_count": len(visible),
        "onscreen_count": onscreen_count,
        "rendered_count": rendered_count,
        "nearest_visible_dist": nearest_visible,
        "visible_actors": visible,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--target-pad", type=int)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    payload = score_payload(load_records(args.trace), args.trace, args.target_pad)
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(
        f"score={payload['score']:.3f} target={payload['target_pad']} "
        f"target_destroyed={payload['target_destroyed']} active={payload['glass']['active']} "
        f"max_active={payload['max_active']} frame={payload['frame']} timer={payload['glass']['first_timer']} "
        f"visible={payload['visible_count']} onscreen={payload['onscreen_count']} "
        f"rendered={payload['rendered_count']} nearest_visible={payload['nearest_visible_dist']}"
    )
    if payload["visible_actors"]:
        print(
            "visible_actors="
            + json.dumps(payload["visible_actors"][:8], sort_keys=True)
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
