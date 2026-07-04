#!/usr/bin/env python3
"""Join glass shard pixel-oracle outliers to native draw/material trace rows."""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


EFFECT_PREFIX = "[EFFECT-TRI] "
MATERIAL_PREFIX = "[TEXGEN-MATERIAL] "
TOP_KEYS = (
    "top_abs_rgb_delta",
    "top_abs_luma_delta",
    "top_bright_or_white_delta",
    "top_overlap",
)
CYCLE_TYPES = {
    0: "1cycle",
    1: "2cycle",
    2: "copy",
    3: "fill",
}
BLEND_1A = ("pixel_color", "memory_color", "blend_color", "fog_color")
BLEND_1B = ("pixel_alpha", "fog_alpha", "shade_alpha", "zero")
BLEND_2A = ("pixel_color", "memory_color", "blend_color", "fog_color")
BLEND_2B = ("inv_pixel_alpha", "memory_alpha", "one", "zero")


def parse_int(value: str | None, default: int = 0) -> int:
    if value is None:
        return default
    try:
        return int(value, 0)
    except ValueError:
        return default


def parse_float(value: str | None, default: float = 0.0) -> float:
    if value is None:
        return default
    try:
        out = float(value)
    except ValueError:
        return default
    return out if math.isfinite(out) else default


def find_text(line: str, pattern: str, default: str | None = None) -> str | None:
    match = re.search(pattern, line)
    return match.group(1) if match else default


def find_int(line: str, pattern: str, default: int = 0) -> int:
    return parse_int(find_text(line, pattern), default)


def find_float(line: str, pattern: str, default: float = 0.0) -> float:
    return parse_float(find_text(line, pattern), default)


def int_tuple(text: str | None) -> list[int]:
    if not text:
        return []
    return [parse_int(part.strip()) for part in text.split(",") if part.strip()]


def float_tuple(text: str | None) -> list[float]:
    if not text:
        return []
    return [parse_float(part.strip()) for part in text.split(",") if part.strip()]


def hex_field(line: str, name: str) -> str | None:
    return find_text(line, rf"{re.escape(name)}=(0x[0-9A-Fa-f]+)")


def count_values(values: list[Any]) -> dict[str, int]:
    counts = Counter(str(value) for value in values)
    return {key: counts[key] for key in sorted(counts)}


def summarize_numbers(values: list[float]) -> dict[str, float]:
    if not values:
        return {"count": 0, "min": 0.0, "max": 0.0, "mean": 0.0}
    return {
        "count": len(values),
        "min": min(values),
        "max": max(values),
        "mean": sum(values) / len(values),
    }


def parse_mode_decode(text: str | None) -> dict[str, Any]:
    if not text:
        return {}
    mode: dict[str, Any] = {}
    for name in ("z", "cvg"):
        value = find_text(text, rf"\b{name}=([^\s]+)")
        if value is not None:
            mode[name] = value
    for name in (
        "aa",
        "imrd",
        "clr_on_cvg",
        "cvg_x_alpha",
        "alpha_cvg",
        "force_bl",
    ):
        mode[name] = find_int(text, rf"\b{name}=([-+]?\d+)")
    for name in ("b1", "b2"):
        mode[name] = int_tuple(find_text(text, rf"\b{name}=\(([^)]*)\)"))
    return mode


def cycle_type_name(other_mode_h: str | None) -> str:
    value = parse_int(other_mode_h, -1)
    if value < 0:
        return "unknown"
    return CYCLE_TYPES.get((value >> 20) & 3, "unknown")


def blend_field_name(fields: list[int], field: int) -> str:
    tables = (BLEND_1A, BLEND_1B, BLEND_2A, BLEND_2B)
    if field < 0 or field >= len(tables):
        return "unknown"
    value = fields[field] if field < len(fields) else -1
    table = tables[field]
    if value < 0 or value >= len(table):
        return "unknown"
    return table[value]


def describe_blend_cycle(fields: list[int]) -> dict[str, Any]:
    return {
        "raw": fields,
        "m1a": blend_field_name(fields, 0),
        "m1b": blend_field_name(fields, 1),
        "m2a": blend_field_name(fields, 2),
        "m2b": blend_field_name(fields, 3),
    }


def rdp_formula_parity(material: dict[str, Any] | None) -> dict[str, Any]:
    if not material:
        return {
            "status": "missing_material",
            "parallel_rdp_shader_equivalent": False,
            "diagnostic_shader_active": False,
            "failures": ["missing TEXGEN-MATERIAL row"],
        }

    mode = material.get("mode_decode", {})
    cycle_type = cycle_type_name(material.get("omh"))
    multi_cycle = cycle_type == "2cycle"
    cycle0 = mode.get("b1") if isinstance(mode.get("b1"), list) else []
    cycle1 = mode.get("b2") if isinstance(mode.get("b2"), list) else []
    final_cycle = cycle1 if multi_cycle else cycle0
    failures: list[str] = []
    notes: list[str] = []

    expected_flags = {
        "z": "xlu",
        "cvg": "wrap",
        "aa": 1,
        "imrd": 1,
        "clr_on_cvg": 1,
        "cvg_x_alpha": 0,
        "alpha_cvg": 0,
        "force_bl": 1,
    }
    for key, expected in expected_flags.items():
        actual = mode.get(key)
        if actual != expected:
            failures.append(f"{key}={actual!r}, expected {expected!r}")

    if cycle_type not in ("1cycle", "2cycle"):
        failures.append(f"cycle_type={cycle_type!r}, expected 1cycle or 2cycle")

    if multi_cycle:
        # Parallel-RDP runs cycle 0 with final_cycle=false. For this stock
        # shard mode b1=(pixel,zero,pixel,one), force-blend makes it a
        # pixel-color passthrough before the final memory blend.
        if cycle0 != [0, 3, 0, 2]:
            failures.append(f"cycle0={cycle0!r}, expected passthrough [0, 3, 0, 2]")
        else:
            notes.append("2cycle cycle0 is a force-blend pixel-color passthrough")

    if final_cycle != [0, 0, 1, 0]:
        failures.append(
            f"final_blend={final_cycle!r}, expected [0, 0, 1, 0] "
            "(pixel,pixel_alpha,memory,inv_pixel_alpha)"
        )

    parity = not failures
    return {
        "status": "pass" if parity else "fail",
        "parallel_rdp_shader_equivalent": parity,
        "diagnostic_shader_active": material.get("api_blend") == "alpha_rdp_cvg_memory",
        "cycle_type": cycle_type,
        "cycle0": describe_blend_cycle(cycle0),
        "cycle1": describe_blend_cycle(cycle1),
        "final_cycle": describe_blend_cycle(final_cycle),
        "coverage_rule": "(coverage_count + memory_coverage) & 7",
        "color_on_coverage_rule": "final cycle returns memory color unless coverage wraps",
        "blend_rule": "force-blend: ((pixel_rgb * (pixel_alpha >> 3)) + "
        "(memory_rgb * (((~pixel_alpha) >> 3) + 1))) >> 5",
        "shader_assumption": "coverage_wrap ? blended_rgb : memory_rgb; alpha stores coverage<<5",
        "notes": notes,
        "failures": failures,
    }


def parse_effect_tex_wh(line: str) -> list[int]:
    match = re.search(r"texwh=\((\d+)x(\d+),(\d+)x(\d+)\)", line)
    if not match:
        return []
    return [parse_int(match.group(index)) for index in range(1, 5)]


def parse_material_tex_wh(line: str) -> list[int]:
    match = re.search(r"tex_wh=\((\d+),(\d+)\)/\((\d+),(\d+)\)", line)
    if not match:
        return []
    return [parse_int(match.group(index)) for index in range(1, 5)]


def parse_pair_tuple(line: str, field: str) -> list[list[int]]:
    match = re.search(rf"{re.escape(field)}=\(([^)]*)\)/\(([^)]*)\)", line)
    if not match:
        return []
    return [int_tuple(match.group(1)), int_tuple(match.group(2))]


def parse_effect_row(line: str) -> dict[str, Any] | None:
    if not line.startswith(EFFECT_PREFIX) or " event=emit " not in line:
        return None

    shard_piece = int_tuple(find_text(line, r"shard_piece=\(([^)]*)\)"))
    shard_slot = int_tuple(find_text(line, r"shard_slot=\(([^)]*)\)"))
    pieces = sorted({piece for piece in shard_piece if piece >= 0})
    bbox_match = re.search(
        r"bbox=\[([-+0-9.eE]+),([-+0-9.eE]+)\]-\[([-+0-9.eE]+),([-+0-9.eE]+)\]",
        line,
    )

    row: dict[str, Any] = {
        "frame": find_int(line, r"\bframe=([-+]?\d+)"),
        "tri": find_int(line, r"\btri=([-+]?\d+)"),
        "cmd": find_text(line, r"\bcmd=([^\s]+)"),
        "label": find_text(line, r"\blabel=([^\s]+)"),
        "drawclass": find_text(line, r"\bdrawclass=([^\s]+)"),
        "src": [part.strip() for part in re.findall(r"src=\(([^)]*)\)", line)[0].split(",")]
        if "src=(" in line
        else [],
        "shard_piece": shard_piece,
        "shard_slot": shard_slot,
        "piece_indices": pieces,
        "blend": find_text(line, r"\bblend=([^\s]+)"),
        "alpha": find_int(line, r"\balpha=([-+]?\d+)"),
        "fog": find_int(line, r"\bfog=([-+]?\d+)"),
        "texedge": find_int(line, r"\btexedge=([-+]?\d+)"),
        "tex": find_int(line, r"\btex=([-+]?\d+)"),
        "used_textures": int_tuple(find_text(line, r"used=\(([^)]*)\)")),
        "tex_wh": parse_effect_tex_wh(line),
        "settex": find_int(line, r"\bsettex=([-+]?\d+)"),
        "gl_textures": int_tuple(find_text(line, r"gl=\(([^)]*)\)")),
        "raw": hex_field(line, "raw"),
        "eff": hex_field(line, "eff"),
        "omh": hex_field(line, "omh"),
        "cc": hex_field(line, "cc"),
        "geom": hex_field(line, "geom"),
        "depth": find_text(line, r"depth=\(([^)]*)\)"),
        "viewport": int_tuple(find_text(line, r"viewport=\(([^)]*)\)")),
        "scissor": int_tuple(find_text(line, r"scissor=\(([^)]*)\)")),
        "inputs": find_int(line, r"\binputs=([-+]?\d+)"),
        "cpuclip": find_int(line, r"\bcpuclip=([-+]?\d+)"),
        "reasons": hex_field(line, "reasons"),
        "ndc_ok": find_int(line, r"\bndc_ok=([-+]?\d+)"),
        "area2": find_float(line, r"\barea2=([-+0-9.eE]+)"),
        "shade0": int_tuple(find_text(line, r"shade0=\(([^)]*)\)")),
        "shadeA": int_tuple(find_text(line, r"shadeA=\(([^)]*)\)")),
        "uv0": float_tuple(find_text(line, r"uv0=\(([^)]*)\)")),
        "uv1": float_tuple(find_text(line, r"uv1=\(([^)]*)\)")),
        "uv2": float_tuple(find_text(line, r"uv2=\(([^)]*)\)")),
    }
    if bbox_match:
        row["bbox"] = [parse_float(bbox_match.group(index)) for index in range(1, 5)]
    else:
        row["bbox"] = []
    return row


def parse_material_row(line: str) -> dict[str, Any] | None:
    if not line.startswith(MATERIAL_PREFIX):
        return None

    mode_text = find_text(line, r"mode_decode=\{([^}]*)\}")
    row: dict[str, Any] = {
        "frame": find_int(line, r"\bframe=([-+]?\d+)"),
        "tri": find_int(line, r"\btri=([-+]?\d+)"),
        "timer": find_int(line, r"\btimer=([-+]?\d+)"),
        "cmd": find_text(line, r"\bcmd=([^\s]+)"),
        "class": find_text(line, r"\bclass=([^\s]+)"),
        "effect": find_text(line, r"\beffect=([^\s]+)"),
        "prop": find_int(line, r"\bprop=([-+]?\d+)"),
        "cc": hex_field(line, "cc"),
        "effcc": hex_field(line, "effcc"),
        "combcc": hex_field(line, "combcc"),
        "opts": hex_field(line, "opts"),
        "oml_raw": hex_field(line, "oml_raw"),
        "oml": hex_field(line, "oml"),
        "omh": hex_field(line, "omh"),
        "geom": hex_field(line, "geom"),
        "mode_decode": parse_mode_decode(mode_text),
        "inputs": find_int(line, r"\binputs=([-+]?\d+)"),
        "lodfrac": find_int(line, r"\blodfrac=([-+]?\d+)"),
        "texscale": int_tuple(find_text(line, r"texscale=\(([^)]*)\)")),
        "tex_used": int_tuple(find_text(line, r"tex_used=\(([^)]*)\)")),
        "tex_wh": parse_material_tex_wh(line),
        "load0_key": find_text(line, r"load0=\{[^}]*key=(0x[0-9A-Fa-f]+)"),
        "load1_key": find_text(line, r"load1=\{[^}]*key=(0x[0-9A-Fa-f]+)"),
        "sampler_linear": int_tuple(find_text(line, r"sampler_linear=\(([^)]*)\)")),
        "sampler_wrap": parse_pair_tuple(line, "sampler_wrap"),
        "bound": int_tuple(find_text(line, r"bound=\(([^)]*)\)")),
        "ambient": int_tuple(find_text(line, r"ambient=\(([^)]*)\)")),
        "prim": int_tuple(find_text(line, r"prim=\(([^)]*)\)")),
        "env": int_tuple(find_text(line, r"env=\(([^)]*)\)")),
        "fogc": int_tuple(find_text(line, r"fogc=\(([^)]*)\)")),
        "shade0": int_tuple(find_text(line, r"shade0=\(([^)]*)\)")),
        "shade1": int_tuple(find_text(line, r"shade1=\(([^)]*)\)")),
        "shade2": int_tuple(find_text(line, r"shade2=\(([^)]*)\)")),
        "ndc": find_text(line, r"ndc=\[([^]]*)\]"),
        "area": find_float(line, r"\barea=([-+0-9.eE]+)"),
        "mixedw": find_int(line, r"\bmixedw=([-+]?\d+)"),
        "blend": find_text(line, r"\bblend=([^\s]+)"),
        "api_blend": find_text(line, r"\bapi_blend=([^\s]+)"),
        "alpha": find_int(line, r"\balpha=([-+]?\d+)"),
        "fog": find_int(line, r"\bfog=([-+]?\d+)"),
        "texedge": find_int(line, r"\btexedge=([-+]?\d+)"),
        "noise": find_int(line, r"\bnoise=([-+]?\d+)"),
        "depth": find_text(line, r"depth=\(([^)]*)\)"),
        "mirror": int_tuple(find_text(line, r"mirror=\(([^)]*)\)")),
        "settex": find_int(line, r"\bsettex=([-+]?\d+)"),
    }
    return row


def load_trace(paths: list[Path]) -> tuple[list[dict[str, Any]], dict[tuple[int, int], list[dict[str, Any]]]]:
    effects: list[dict[str, Any]] = []
    materials: dict[tuple[int, int], list[dict[str, Any]]] = defaultdict(list)
    for path in paths:
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            effect = parse_effect_row(line)
            if effect is not None:
                effect["source_log"] = str(path)
                effects.append(effect)
                continue
            material = parse_material_row(line)
            if material is not None:
                material["source_log"] = str(path)
                materials[(material["frame"], material["tri"])].append(material)
    return effects, materials


def oracle_piece_map(oracle: dict[str, Any]) -> dict[int, dict[str, Any]]:
    pieces: dict[int, dict[str, Any]] = {}
    for item in oracle.get("pieces", []):
        index = item.get("index")
        if isinstance(index, int):
            pieces[index] = item
    return pieces


def collect_targets(oracle: dict[str, Any], explicit: list[int], top: int) -> tuple[list[int], dict[int, dict[str, int]]]:
    ranks: dict[int, dict[str, int]] = defaultdict(dict)
    targets: list[int] = []

    def add(index: int, source: str, rank: int) -> None:
        ranks[index][source] = rank
        if index not in targets:
            targets.append(index)

    for index in explicit:
        add(index, "explicit", len(targets) + 1)

    limit = max(0, top)
    for key in TOP_KEYS:
        for rank, item in enumerate(oracle.get(key, [])[:limit], start=1):
            index = item.get("index")
            if isinstance(index, int):
                add(index, key, rank)

    return targets, dict(ranks)


def compact_oracle_metrics(piece: dict[str, Any] | None) -> dict[str, Any]:
    if not piece:
        return {}
    return {
        "index": piece.get("index"),
        "pixels": piece.get("pixels"),
        "changed_pct": piece.get("changed_pct"),
        "abs_rgb_delta_mean": piece.get("abs_rgb_delta_mean"),
        "luma_delta_mean": piece.get("luma_delta_mean"),
        "saturation_delta_mean": piece.get("saturation_delta_mean"),
        "bright_or_white_delta": piece.get("bright_or_white_delta"),
        "overlap_pct": piece.get("overlap_pct"),
        "screen_bbox": piece.get("screen_bbox"),
        "timer": piece.get("timer"),
        "onscreen": piece.get("onscreen"),
        "baseline_buckets": piece.get("metrics", {}).get("baseline_buckets"),
        "test_buckets": piece.get("metrics", {}).get("test_buckets"),
    }


def material_signature(material: dict[str, Any] | None) -> str:
    if not material:
        return "missing"
    mode = material.get("mode_decode", {})
    return (
        f"cc={material.get('cc')} oml={material.get('oml_raw')} omh={material.get('omh')} "
        f"z={mode.get('z')} cvg={mode.get('cvg')} clr_on_cvg={mode.get('clr_on_cvg')} "
        f"imrd={mode.get('imrd')} sampler={material.get('sampler_linear')} "
        f"load=({material.get('load0_key')},{material.get('load1_key')})"
    )


def attach_materials(
    effects: list[dict[str, Any]],
    materials: dict[tuple[int, int], list[dict[str, Any]]],
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for effect in sorted(effects, key=lambda row: (row["frame"], row["tri"])):
        row = dict(effect)
        joined = materials.get((effect["frame"], effect["tri"]), [])
        row["material"] = joined[0] if joined else None
        row["material_count"] = len(joined)
        rows.append(row)
    return rows


def summarize_draws(draws: list[dict[str, Any]]) -> dict[str, Any]:
    materials = [row.get("material") for row in draws]
    mode_values = [material.get("mode_decode", {}).get("z") for material in materials if material]
    cvg_values = [material.get("mode_decode", {}).get("cvg") for material in materials if material]
    clr_on_cvg_values = [
        material.get("mode_decode", {}).get("clr_on_cvg") for material in materials if material
    ]
    parity = [rdp_formula_parity(material) for material in materials]
    return {
        "draw_rows": len(draws),
        "material_rows": sum(1 for row in draws if row.get("material")),
        "frames": sorted({row["frame"] for row in draws}),
        "tris": sorted(row["tri"] for row in draws),
        "blend": count_values([row.get("blend") for row in draws]),
        "api_blend": count_values([material.get("api_blend") for material in materials if material]),
        "zmode": count_values(mode_values),
        "coverage_mode": count_values(cvg_values),
        "clr_on_cvg": count_values(clr_on_cvg_values),
        "sampler_linear": count_values(
            [material.get("sampler_linear") for material in materials if material]
        ),
        "texture_keys": count_values(
            [
                (material.get("load0_key"), material.get("load1_key"))
                for material in materials
                if material
            ]
        ),
        "effect_material": count_values([material_signature(material) for material in materials]),
        "rdp_formula": count_values([item.get("status") for item in parity]),
        "rdp_formula_equivalent": count_values(
            [item.get("parallel_rdp_shader_equivalent") for item in parity]
        ),
        "diagnostic_shader_active": count_values(
            [item.get("diagnostic_shader_active") for item in parity]
        ),
        "cpuclip": count_values([row.get("cpuclip") for row in draws]),
        "ndc_ok": count_values([row.get("ndc_ok") for row in draws]),
        "area2": summarize_numbers([row.get("area2", 0.0) for row in draws]),
        "shade0": count_values([row.get("shade0") for row in draws]),
    }


def compact_draw(row: dict[str, Any]) -> dict[str, Any]:
    material = row.get("material")
    return {
        "frame": row.get("frame"),
        "tri": row.get("tri"),
        "cmd": row.get("cmd"),
        "piece_indices": row.get("piece_indices"),
        "shard_slot": row.get("shard_slot"),
        "blend": row.get("blend"),
        "alpha": row.get("alpha"),
        "fog": row.get("fog"),
        "texedge": row.get("texedge"),
        "tex": row.get("tex"),
        "used_textures": row.get("used_textures"),
        "tex_wh": row.get("tex_wh"),
        "raw": row.get("raw"),
        "eff": row.get("eff"),
        "omh": row.get("omh"),
        "cc": row.get("cc"),
        "geom": row.get("geom"),
        "depth": row.get("depth"),
        "cpuclip": row.get("cpuclip"),
        "reasons": row.get("reasons"),
        "ndc_ok": row.get("ndc_ok"),
        "bbox": row.get("bbox"),
        "area2": row.get("area2"),
        "shade0": row.get("shade0"),
        "uv0": row.get("uv0"),
        "uv1": row.get("uv1"),
        "uv2": row.get("uv2"),
        "material": {
            "present": material is not None,
            "cc": material.get("cc") if material else None,
            "opts": material.get("opts") if material else None,
            "oml_raw": material.get("oml_raw") if material else None,
            "omh": material.get("omh") if material else None,
            "geom": material.get("geom") if material else None,
            "mode_decode": material.get("mode_decode") if material else None,
            "tex_used": material.get("tex_used") if material else None,
            "tex_wh": material.get("tex_wh") if material else None,
            "load0_key": material.get("load0_key") if material else None,
            "load1_key": material.get("load1_key") if material else None,
            "sampler_linear": material.get("sampler_linear") if material else None,
            "bound": material.get("bound") if material else None,
            "ambient": material.get("ambient") if material else None,
            "prim": material.get("prim") if material else None,
            "env": material.get("env") if material else None,
            "fogc": material.get("fogc") if material else None,
            "shade0": material.get("shade0") if material else None,
            "shade1": material.get("shade1") if material else None,
            "shade2": material.get("shade2") if material else None,
            "lodfrac": material.get("lodfrac") if material else None,
            "blend": material.get("blend") if material else None,
            "api_blend": material.get("api_blend") if material else None,
            "depth": material.get("depth") if material else None,
            "rdp_formula_parity": rdp_formula_parity(material),
        },
    }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-json", type=Path, required=True)
    parser.add_argument("--native-log", type=Path, nargs="+", required=True)
    parser.add_argument("--piece", type=int, action="append", default=[])
    parser.add_argument("--top", type=int, default=8)
    parser.add_argument("--draw-limit", type=int, default=12)
    parser.add_argument("--require-materials", action="store_true")
    parser.add_argument(
        "--require-rdp-formula-parity",
        action="store_true",
        help="fail if target shard draws do not match the Parallel-RDP formula supported by the diagnostic shader",
    )
    parser.add_argument(
        "--require-rdp-cvg-api-blend",
        action="store_true",
        help="fail if target shard draws did not activate the alpha_rdp_cvg_memory API blend mode",
    )
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args(argv)

    failures: list[str] = []
    warnings: list[str] = []
    oracle = json.loads(args.oracle_json.read_text(encoding="utf-8"))
    piece_map = oracle_piece_map(oracle)
    targets, ranks = collect_targets(oracle, args.piece, args.top)

    if oracle.get("status") != "pass":
        warnings.append(f"oracle status is {oracle.get('status')}")
    if not targets:
        failures.append("no target shard pieces selected")

    effects, materials = load_trace(args.native_log)
    if not effects:
        failures.append("native log has no EFFECT-TRI emit rows")

    effects_by_piece: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for effect in attach_materials(effects, materials):
        for piece in effect.get("piece_indices", []):
            effects_by_piece[piece].append(effect)

    target_payload: list[dict[str, Any]] = []
    for piece in targets:
        draws = effects_by_piece.get(piece, [])
        if not draws:
            failures.append(f"target piece {piece} has no EFFECT-TRI draw rows")
        if args.require_materials and any(row.get("material") is None for row in draws):
            failures.append(f"target piece {piece} has EFFECT-TRI rows without TEXGEN-MATERIAL joins")
        if args.require_rdp_formula_parity:
            bad_formula = [
                row for row in draws
                if not rdp_formula_parity(row.get("material")).get("parallel_rdp_shader_equivalent")
            ]
            if bad_formula:
                failures.append(
                    f"target piece {piece} has {len(bad_formula)} draw rows without Parallel-RDP formula parity"
                )
        if args.require_rdp_cvg_api_blend:
            inactive = [
                row for row in draws
                if not rdp_formula_parity(row.get("material")).get("diagnostic_shader_active")
            ]
            if inactive:
                failures.append(
                    f"target piece {piece} has {len(inactive)} draw rows without alpha_rdp_cvg_memory API blend"
                )

        target_payload.append(
            {
                "piece": piece,
                "source_ranks": ranks.get(piece, {}),
                "oracle": compact_oracle_metrics(piece_map.get(piece)),
                "draw_summary": summarize_draws(draws),
                "draws": [compact_draw(row) for row in draws[: max(0, args.draw_limit)]],
            }
        )

    joined_count = sum(
        1 for effect in effects if materials.get((effect["frame"], effect["tri"]))
    )
    payload: dict[str, Any] = {
        "status": "fail" if failures else "pass",
        "failures": failures,
        "warnings": warnings,
        "inputs": {
            "oracle_json": str(args.oracle_json),
            "native_log": [str(path) for path in args.native_log],
            "top": args.top,
            "explicit_pieces": args.piece,
        },
        "oracle_summary": {
            "status": oracle.get("status"),
            "mask_mode": oracle.get("mask_mode"),
            "sample": oracle.get("sample"),
            "coverage": oracle.get("coverage"),
            "union_changed_pct": oracle.get("union_metrics", {}).get("changed_pct"),
            "union_luma_delta_mean": oracle.get("union_metrics", {})
            .get("luma_delta", {})
            .get("mean"),
            "union_abs_rgb_delta_mean": oracle.get("union_metrics", {})
            .get("abs_rgb_delta", {})
            .get("mean"),
        },
        "trace_counts": {
            "effect_rows": len(effects),
            "material_rows": sum(len(value) for value in materials.values()),
            "joined_effect_rows": joined_count,
            "pieces_seen": sorted(effects_by_piece),
            "rows_by_piece": {
                str(piece): len(rows) for piece, rows in sorted(effects_by_piece.items())
            },
        },
        "targets": target_payload,
    }

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print("=== glass shard draw trace join ===")
    print(
        "  trace: "
        f"effect_rows={len(effects)} material_rows={payload['trace_counts']['material_rows']} "
        f"joined={joined_count} pieces_seen={len(effects_by_piece)}"
    )
    if isinstance(payload["oracle_summary"]["union_changed_pct"], (float, int)):
        print(
            "  oracle: "
            f"status={oracle.get('status')} mask={oracle.get('mask_mode')} "
            f"changed={payload['oracle_summary']['union_changed_pct']:.3f}%"
        )
    else:
        print(f"  oracle: status={oracle.get('status')} mask={oracle.get('mask_mode')}")
    for target in target_payload[: max(0, args.top)]:
        summary = target["draw_summary"]
        oracle_metrics = target["oracle"]
        print(
            "  piece "
            f"{target['piece']}: draws={summary['draw_rows']} materials={summary['material_rows']} "
            f"abs_rgb={oracle_metrics.get('abs_rgb_delta_mean')} "
            f"bright_delta={oracle_metrics.get('bright_or_white_delta')} "
            f"sampler={summary['sampler_linear']} "
            f"cvg={summary['coverage_mode']} clr_on_cvg={summary['clr_on_cvg']} "
            f"rdp_formula={summary['rdp_formula']} "
            f"diag_api={summary['diagnostic_shader_active']}"
        )

    if warnings:
        print("WARN: " + "; ".join(warnings))
    if failures:
        print("FAIL: glass shard draw trace join failed")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print("PASS: glass shard draw trace join")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
