#!/usr/bin/env python3
"""Compile ROM-oracle route specs for native and emulator runners."""

from __future__ import annotations

import argparse
import copy
import json
import sys
from pathlib import Path
from typing import Any


SCHEMA = "mgb64.rom_oracle.route.v1"
ROUTE_DIR = Path(__file__).resolve().parent / "rom_oracle_routes"

BUTTON_MASKS = {
    "a": 0x8000,
    "b": 0x4000,
    "fire": 0x2000,
    "z": 0x2000,
    "start": 0x1000,
    "dpad_up": 0x0800,
    "dpad_down": 0x0400,
    "dpad_left": 0x0200,
    "dpad_right": 0x0100,
    "l": 0x0020,
    "aim": 0x0010,
    "r": 0x0010,
    "cup": 0x0008,
    "c_up": 0x0008,
    "cdown": 0x0004,
    "c_down": 0x0004,
    "cleft": 0x0002,
    "c_left": 0x0002,
    "cright": 0x0001,
    "c_right": 0x0001,
}

BUTTON_NATIVE_ENV = {
    "a": "GE007_AUTO_A",
    "b": "GE007_AUTO_B",
    "fire": "GE007_AUTO_FIRE",
    "z": "GE007_AUTO_FIRE",
    "start": "GE007_AUTO_START",
    "l": "GE007_AUTO_L",
    "aim": "GE007_AUTO_AIM",
    "r": "GE007_AUTO_R",
    "cup": "GE007_AUTO_CUP",
    "c_up": "GE007_AUTO_CUP",
    "cdown": "GE007_AUTO_CDOWN",
    "c_down": "GE007_AUTO_CDOWN",
    "cleft": "GE007_AUTO_CLEFT",
    "c_left": "GE007_AUTO_CLEFT",
    "cright": "GE007_AUTO_CRIGHT",
    "c_right": "GE007_AUTO_CRIGHT",
    "dpad_up": "GE007_AUTO_DPAD_UP",
    "dpad_down": "GE007_AUTO_DPAD_DOWN",
    "dpad_left": "GE007_AUTO_DPAD_LEFT",
    "dpad_right": "GE007_AUTO_DPAD_RIGHT",
    "crouch": "GE007_AUTO_CROUCH",
}


def route_path(name_or_path: str) -> Path:
    candidate = Path(name_or_path)
    if candidate.exists():
        return candidate.resolve()
    if candidate.suffix != ".json":
        candidate = ROUTE_DIR / f"{name_or_path}.json"
    else:
        candidate = ROUTE_DIR / candidate.name
    if candidate.exists():
        return candidate.resolve()
    raise SystemExit(f"FAIL: route not found: {name_or_path}")


def load_route(name_or_path: str) -> dict[str, Any]:
    path = route_path(name_or_path)
    with path.open("r", encoding="utf-8") as handle:
        route = json.load(handle)
    if route.get("schema") != SCHEMA:
        raise SystemExit(f"FAIL: unsupported route schema in {path}: {route.get('schema')!r}")
    if "level" not in route:
        raise SystemExit(f"FAIL: route missing level: {path}")
    for key in ("events", "native_events", "stock_events"):
        if key in route and not isinstance(route.get(key), list):
            raise SystemExit(f"FAIL: route {key} must be a list: {path}")
    if not isinstance(route.get("events"), list) and not isinstance(route.get("native_events"), list):
        raise SystemExit(f"FAIL: route must define events or native_events: {path}")
    route["_path"] = str(path)
    return route


def route_field(route: dict[str, Any], field: str) -> Any:
    if field == "native_frames":
        return route.get("native_frames", route.get("frames"))
    if field == "stock_frames":
        return route.get("stock_frames", route.get("frames"))
    if field == "native_level":
        return route.get("native_level", route.get("level"))
    if field == "stock_level":
        return route.get("stock_level", route.get("level"))
    if field == "compare_align":
        return route.get("compare_align", "global")
    if field == "compare_kind":
        return route.get("compare_kind", "movement")
    if field == "compare_profile":
        return route.get("compare_profile", "full")
    if field == "compare_max_aligned":
        return route.get("compare_max_aligned", "")
    if field == "compare_min_aligned":
        return route.get("compare_min_aligned", "")
    if field == "compare_camera_modes":
        return route.get("compare_camera_modes", "")
    if field == "compare_start_active_frame":
        return route.get("compare_start_active_frame", "")
    if field == "compare_start_intro_timer":
        return route.get("compare_start_intro_timer", "")
    if field == "compare_end_intro_timer":
        return route.get("compare_end_intro_timer", "")
    if field == "compare_sample_step":
        return route.get("compare_sample_step", "")
    if field == "compare_vector_tolerance":
        return route.get("compare_vector_tolerance", "")
    if field == "compare_direction_tolerance":
        return route.get("compare_direction_tolerance", "")
    if field == "compare_scalar_tolerance":
        return route.get("compare_scalar_tolerance", "")
    if field == "compare_anim_tolerance":
        return route.get("compare_anim_tolerance", "")
    if field == "compare_state":
        return route.get("compare_state", False)
    if field == "compare_selected_camera":
        return route.get("compare_selected_camera", False)
    if field == "compare_intro_setup":
        return route.get("compare_intro_setup", False)
    if field == "compare_bond_anim":
        return route.get("compare_bond_anim", False)
    if field == "compare_exclude_fields":
        return route.get("compare_exclude_fields", "")
    if field == "compare_require_frozen":
        return route.get("compare_require_frozen", False)
    if field == "compare_gameplay_windows":
        return route.get("compare_gameplay_windows", [])
    if field == "compare_normalize_position":
        return route.get("compare_normalize_position", False)
    if field == "native_speedframes":
        return route.get("native_speedframes", "")
    if field == "native_render_audit":
        return route.get("native_render_audit", False)
    if field == "stock_speedframes":
        return route.get("stock_speedframes", route.get("native_speedframes", ""))
    if field == "stock_gameplay_start_global":
        return route.get("stock_gameplay_start_global", "")
    if field == "stock_menu_close_on_player":
        return route.get("stock_menu_close_on_player", True)
    if field == "native_min_moving_records":
        return route.get("native_min_moving_records", "")
    if field == "stock_min_moving_records":
        return route.get("stock_min_moving_records", "")
    if field == "stock_min_gameplay_input_records":
        return route.get("stock_min_gameplay_input_records", "")
    if field == "stock_max_suppressed_menu_records":
        return route.get("stock_max_suppressed_menu_records", "")
    if field == "stock_min_menu_to_gameplay_gap":
        return route.get("stock_min_menu_to_gameplay_gap", "")
    if field == "native_intro_audit":
        return route.get("native_intro_audit", False)
    if field == "native_intro_camera_modes":
        return route.get("native_intro_camera_modes", "")
    if field == "native_intro_require_frozen":
        return route.get("native_intro_require_frozen", "")
    if field == "native_intro_require_player":
        return route.get("native_intro_require_player", True)
    if field == "native_intro_require_bond_present":
        return route.get("native_intro_require_bond_present", False)
    if field == "native_intro_require_bond_onscreen":
        return route.get("native_intro_require_bond_onscreen", False)
    if field == "native_intro_require_bond_model_mtx":
        return route.get("native_intro_require_bond_model_mtx", False)
    if field == "native_intro_require_bond_rendered":
        return route.get("native_intro_require_bond_rendered", False)
    if field == "native_intro_require_bond_anim":
        return route.get("native_intro_require_bond_anim", False)
    if field == "native_intro_require_bond_anim_hash":
        return route.get("native_intro_require_bond_anim_hash", False)
    if field == "native_intro_require_bond_right_item":
        return route.get("native_intro_require_bond_right_item", "")
    if field == "native_intro_min_active_records":
        return route.get("native_intro_min_active_records", "")
    if field == "native_intro_min_present_frames":
        return route.get("native_intro_min_present_frames", "")
    if field == "native_intro_min_onscreen_frames":
        return route.get("native_intro_min_onscreen_frames", "")
    if field == "native_intro_min_model_mtx_frames":
        return route.get("native_intro_min_model_mtx_frames", "")
    if field == "native_intro_min_rendered_frames":
        return route.get("native_intro_min_rendered_frames", "")
    if field == "native_intro_min_render_count":
        return route.get("native_intro_min_render_count", "")
    if field == "native_intro_min_anim_frames":
        return route.get("native_intro_min_anim_frames", "")
    if field == "native_intro_min_anim_hash_frames":
        return route.get("native_intro_min_anim_hash_frames", "")
    if field == "native_intro_min_anim_advance":
        return route.get("native_intro_min_anim_advance", "")
    if field == "native_intro_min_right_item_frames":
        return route.get("native_intro_min_right_item_frames", "")
    if field == "native_intro_max_first_present_frame":
        return route.get("native_intro_max_first_present_frame", "")
    if field == "native_intro_max_first_render_frame":
        return route.get("native_intro_max_first_render_frame", "")
    if field not in route:
        raise SystemExit(f"FAIL: route has no field: {field}")
    return route[field]


def events_for(route: dict[str, Any], provider: str) -> list[dict[str, Any]]:
    if provider == "native":
        events = route.get("native_events", route.get("events", []))
    elif provider == "stock":
        events = route.get("stock_events", route.get("events", []))
    else:
        raise AssertionError(provider)
    return expand_events(events)


def expand_events(events: list[dict[str, Any]]) -> list[dict[str, Any]]:
    expanded: list[dict[str, Any]] = []
    for event in events:
        if not isinstance(event, dict):
            raise SystemExit(f"FAIL: route event must be an object: {event!r}")
        repeat = event.get("repeat")
        if repeat is None:
            expanded.append(copy.deepcopy(event))
            continue
        if not isinstance(repeat, dict):
            raise SystemExit(f"FAIL: route event repeat must be an object: {event!r}")
        try:
            every = int(repeat["every"])
        except (KeyError, TypeError, ValueError):
            raise SystemExit(f"FAIL: route repeat has invalid every: {event!r}") from None
        try:
            first_start = int(event["start"])
        except (KeyError, TypeError, ValueError):
            raise SystemExit(f"FAIL: route repeat has invalid start: {event!r}") from None
        if every < 1:
            raise SystemExit(f"FAIL: route repeat every must be positive: {event!r}")
        if "count" in repeat:
            try:
                count = int(repeat["count"])
            except (TypeError, ValueError):
                raise SystemExit(f"FAIL: route repeat has invalid count: {event!r}") from None
        elif "until" in repeat:
            try:
                until = int(repeat["until"])
            except (TypeError, ValueError):
                raise SystemExit(f"FAIL: route repeat has invalid until: {event!r}") from None
            count = ((until - first_start) // every) + 1
        else:
            raise SystemExit(f"FAIL: route repeat must define count or until: {event!r}")
        if count < 1:
            raise SystemExit(f"FAIL: route repeat count must be positive: {event!r}")
        for index in range(count):
            instance = copy.deepcopy(event)
            instance.pop("repeat", None)
            instance["start"] = first_start + every * index
            expanded.append(instance)
    expanded.sort(key=lambda item: int(item.get("start", 0)))
    return expanded


def clamp_axis(value: int) -> int:
    return max(-80, min(80, value))


def event_window(event: dict[str, Any]) -> tuple[int, int]:
    try:
        start = int(event["start"])
    except (KeyError, TypeError, ValueError):
        raise SystemExit(f"FAIL: route event has invalid start: {event!r}") from None
    duration_value = event.get("len", event.get("duration", 1))
    try:
        duration = int(duration_value)
    except (TypeError, ValueError):
        raise SystemExit(f"FAIL: route event has invalid duration: {event!r}") from None
    if start < 1 or duration < 1:
        raise SystemExit(f"FAIL: route event start/duration must be positive: {event!r}")
    return start, duration


def event_axes(event: dict[str, Any]) -> tuple[int, int]:
    stick_x = int(event.get("stick_x", 0))
    stick_y = int(event.get("stick_y", 0))

    if event.get("left"):
        stick_x -= 80
    if event.get("right"):
        stick_x += 80
    if event.get("forward"):
        stick_y += 80
    if event.get("back"):
        stick_y -= 80

    return clamp_axis(stick_x), clamp_axis(stick_y)


def event_buttons(event: dict[str, Any]) -> list[str]:
    buttons = event.get("buttons", [])
    if isinstance(buttons, str):
        buttons = [buttons]
    if not isinstance(buttons, list):
        raise SystemExit(f"FAIL: route event buttons must be a string or list: {event!r}")
    normalized = []
    for button in buttons:
        key = str(button).strip().lower().replace("-", "_")
        if key not in BUTTON_MASKS and key not in BUTTON_NATIVE_ENV:
            raise SystemExit(f"FAIL: unknown route button {button!r} in event {event!r}")
        normalized.append(key)
    return normalized


def emit_native_env(route: dict[str, Any]) -> None:
    env: dict[str, list[str]] = {}
    direct_env = route.get("native_env", {})
    if direct_env in (None, ""):
        direct_env = {}
    if not isinstance(direct_env, dict):
        raise SystemExit("FAIL: route native_env must be an object when set")

    def env_value(value: Any) -> str:
        if isinstance(value, bool):
            return "1" if value else "0"
        if value is None:
            return ""
        return str(value)

    for event in events_for(route, "native"):
        start, duration = event_window(event)
        window = f"{start}:{duration}"
        stick_x, stick_y = event_axes(event)

        if stick_y > 0:
            if stick_y != 80:
                raise SystemExit("FAIL: native GE007_AUTO_FORWARD only supports full-scale +80")
            env.setdefault("GE007_AUTO_FORWARD", []).append(window)
        elif stick_y < 0:
            if stick_y != -80:
                raise SystemExit("FAIL: native GE007_AUTO_BACK only supports full-scale -80")
            env.setdefault("GE007_AUTO_BACK", []).append(window)

        if stick_x > 0:
            if stick_x != 80:
                raise SystemExit("FAIL: native GE007_AUTO_RIGHT only supports full-scale +80")
            env.setdefault("GE007_AUTO_RIGHT", []).append(window)
        elif stick_x < 0:
            if stick_x != -80:
                raise SystemExit("FAIL: native GE007_AUTO_LEFT only supports full-scale -80")
            env.setdefault("GE007_AUTO_LEFT", []).append(window)

        for button in event_buttons(event):
            native_env = BUTTON_NATIVE_ENV.get(button)
            if native_env:
                env.setdefault(native_env, []).append(window)

    for key in sorted(direct_env):
        if not isinstance(key, str) or not key:
            raise SystemExit(f"FAIL: native_env key must be a non-empty string: {key!r}")
        print(f"{key}={env_value(direct_env[key])}")

    for key in sorted(env):
        print(f"{key}={','.join(env[key])}")


def event_phase(event: dict[str, Any]) -> str:
    phase = str(event.get("phase", "gameplay")).strip().lower().replace("-", "_")
    if phase not in ("gameplay", "menu", "boot", "frontend"):
        raise SystemExit(f"FAIL: route event has unsupported phase {phase!r}: {event!r}")
    if phase in ("boot", "frontend"):
        return "menu"
    return phase


def emit_ares_input(route: dict[str, Any]) -> None:
    print("# mgb64 ares input script v1")
    print("# start len buttons_hex stick_x stick_y phase")
    for event in events_for(route, "stock"):
        start, duration = event_window(event)
        stick_x, stick_y = event_axes(event)
        mask = 0
        for button in event_buttons(event):
            mask |= BUTTON_MASKS.get(button, 0)
        print(f"{start} {duration} 0x{mask:04x} {stick_x} {stick_y} {event_phase(event)}")


def emit_gameplay_windows(route: dict[str, Any]) -> None:
    windows = route.get("compare_gameplay_windows", [])
    if windows in (None, ""):
        return
    if not isinstance(windows, list):
        raise SystemExit("FAIL: compare_gameplay_windows must be a list")
    for window in windows:
        if not isinstance(window, dict):
            raise SystemExit(f"FAIL: compare_gameplay_windows entries must be objects: {window!r}")
        start, duration = event_window(window)
        print(f"{start}:{duration}")


def route_bool(route: dict[str, Any], field: str, default: bool) -> bool:
    value = route.get(field, default)
    if isinstance(value, bool):
        return value
    if isinstance(value, int):
        return value != 0
    if isinstance(value, str):
        key = value.strip().lower()
        if key in ("1", "true", "yes", "on"):
            return True
        if key in ("0", "false", "no", "off"):
            return False
    raise SystemExit(f"FAIL: route {field} must be boolean when set")


def route_positive_int(route: dict[str, Any], field: str) -> None:
    value = route.get(field, "")
    if value in ("", None):
        return
    try:
        parsed = int(value)
    except (TypeError, ValueError):
        raise SystemExit(f"FAIL: route {field} must be a positive integer when set") from None
    if parsed < 1:
        raise SystemExit(f"FAIL: route {field} must be a positive integer when set")


def route_nonnegative_int(route: dict[str, Any], field: str) -> None:
    value = route.get(field, "")
    if value in ("", None):
        return
    try:
        parsed = int(value)
    except (TypeError, ValueError):
        raise SystemExit(f"FAIL: route {field} must be a non-negative integer when set") from None
    if parsed < 0:
        raise SystemExit(f"FAIL: route {field} must be a non-negative integer when set")


def route_requires_present(route: dict[str, Any], field: str, errors: list[str]) -> bool:
    value = route.get(field, "")
    if value in ("", None):
        errors.append(f"route must set {field}")
        return False
    return True


def route_positive_float(route: dict[str, Any], field: str) -> None:
    value = route.get(field, "")
    if value in ("", None):
        return
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        raise SystemExit(f"FAIL: route {field} must be a positive number when set") from None
    if parsed <= 0.0:
        raise SystemExit(f"FAIL: route {field} must be a positive number when set")


def validate_route(route: dict[str, Any]) -> None:
    compare_kind = str(route_field(route, "compare_kind"))
    stock_events = events_for(route, "stock")
    errors: list[str] = []

    stock_has_gameplay_events = any(event_phase(event) == "gameplay" for event in stock_events)
    stock_has_menu_events = any(event_phase(event) == "menu" for event in stock_events)
    if compare_kind == "movement" and stock_has_gameplay_events:
        if route_field(route, "stock_gameplay_start_global") in ("", None):
            errors.append("movement stock gameplay routes must set stock_gameplay_start_global")
        if not route_bool(route, "stock_menu_close_on_player", True):
            errors.append(
                "movement stock gameplay routes must close menu input on target-player entry"
            )
        for field in (
            "native_min_moving_records",
            "stock_min_moving_records",
            "stock_min_gameplay_input_records",
        ):
            if route_requires_present(route, field, errors):
                route_positive_int(route, field)
        if stock_has_menu_events:
            if route_requires_present(route, "stock_max_suppressed_menu_records", errors):
                route_nonnegative_int(route, "stock_max_suppressed_menu_records")
            if route_requires_present(route, "stock_min_menu_to_gameplay_gap", errors):
                route_nonnegative_int(route, "stock_min_menu_to_gameplay_gap")

    if route_bool(route, "native_intro_audit", False):
        for field in (
            "native_intro_require_player",
            "native_intro_require_bond_present",
            "native_intro_require_bond_onscreen",
            "native_intro_require_bond_model_mtx",
            "native_intro_require_bond_rendered",
            "native_intro_require_bond_anim",
            "native_intro_require_bond_anim_hash",
        ):
            route_bool(route, field, bool(route_field(route, field)))
        for field in (
            "native_intro_min_active_records",
            "native_intro_min_present_frames",
            "native_intro_min_onscreen_frames",
            "native_intro_min_model_mtx_frames",
            "native_intro_min_rendered_frames",
            "native_intro_min_render_count",
            "native_intro_min_anim_frames",
            "native_intro_min_anim_hash_frames",
            "native_intro_min_right_item_frames",
            "native_intro_max_first_present_frame",
            "native_intro_max_first_render_frame",
        ):
            route_positive_int(route, field)
        route_nonnegative_int(route, "native_intro_require_bond_right_item")
        route_positive_float(route, "native_intro_min_anim_advance")

    route_bool(route, "compare_selected_camera", False)
    route_bool(route, "compare_intro_setup", False)
    route_bool(route, "compare_bond_anim", False)
    route_bool(route, "native_render_audit", False)
    route_positive_int(route, "native_level")
    route_positive_int(route, "stock_level")
    route_nonnegative_int(route, "stock_max_suppressed_menu_records")
    route_nonnegative_int(route, "stock_min_menu_to_gameplay_gap")
    route_positive_float(route, "compare_anim_tolerance")
    route_positive_int(route, "compare_min_aligned")
    route_positive_float(route, "compare_start_intro_timer")
    route_positive_float(route, "compare_end_intro_timer")

    for provider in ("native", "stock"):
        for event in events_for(route, provider):
            phase = event_phase(event) if provider == "stock" else "gameplay"
            buttons = set(event_buttons(event))
            if phase == "gameplay" and "start" in buttons:
                errors.append(f"{provider} gameplay event injects START: {event!r}")

    if errors:
        print(f"FAIL: invalid route {route.get('name', route.get('_path', '<unknown>'))}")
        for error in errors:
            print(f"  {error}")
        raise SystemExit(2)


def list_routes() -> None:
    for path in sorted(ROUTE_DIR.glob("*.json")):
        try:
            route = load_route(str(path))
        except SystemExit:
            continue
        native_frames = route_field(route, "native_frames")
        stock_frames = route_field(route, "stock_frames")
        native_level = route_field(route, "native_level")
        stock_level = route_field(route, "stock_level")
        print(
            f"{path.stem}\tlevel={route.get('level')}"
            f"\tnative_level={native_level}\tstock_level={stock_level}"
            f"\tnative_frames={native_frames}\tstock_frames={stock_frames}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("resolve", help="print the resolved route path")
    p.add_argument("route")

    p = sub.add_parser("field", help="print one route metadata field")
    p.add_argument("route")
    p.add_argument("field")

    p = sub.add_parser("native-env", help="emit KEY=VALUE lines for native GE007_AUTO_* input")
    p.add_argument("route")

    p = sub.add_parser("ares-input", help="emit ares controller route script")
    p.add_argument("route")

    p = sub.add_parser("gameplay-windows", help="emit comparator gameplay windows as START:LEN lines")
    p.add_argument("route")

    p = sub.add_parser("summary", help="print route metadata")
    p.add_argument("route")

    p = sub.add_parser("validate", help="validate route safety contracts")
    p.add_argument("route")

    sub.add_parser("list", help="list built-in route specs")

    args = parser.parse_args()

    if args.command == "list":
        list_routes()
        return 0

    route = load_route(args.route)
    if args.command == "resolve":
        print(route["_path"])
    elif args.command == "field":
        print(route_field(route, args.field))
    elif args.command == "native-env":
        emit_native_env(route)
    elif args.command == "ares-input":
        emit_ares_input(route)
    elif args.command == "gameplay-windows":
        emit_gameplay_windows(route)
    elif args.command == "summary":
        print(json.dumps({k: v for k, v in route.items() if not k.startswith("_")}, indent=2))
    elif args.command == "validate":
        validate_route(route)
        print(f"PASS: route validated: {route.get('name', route['_path'])}")
    else:
        raise AssertionError(args.command)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
