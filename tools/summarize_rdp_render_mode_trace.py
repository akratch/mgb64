#!/usr/bin/env python3
"""Summarize native [RDP-MODE] render-mode audit rows."""

from __future__ import annotations

import argparse
import collections
import re
import sys
from pathlib import Path


RDP_MODE_RE = re.compile(r"\[RDP-MODE\]\s+(?P<body>.*)")


def value_for(body: str, name: str) -> str | None:
    match = re.search(rf"\b{name}=([^\s]+)", body)
    return match.group(1) if match else None


def decode_value(body: str, name: str) -> str | None:
    match = re.search(rf"\b{name}=([^,\s}}]+)", body)
    return match.group(1) if match else None


def parse_row(body: str) -> dict[str, str]:
    decode_match = re.search(r"decode=\{(?P<decode>[^}]*)\}", body)
    decode = decode_match.group("decode") if decode_match else ""
    row = {
        "class": value_for(body, "class") or "?",
        "effect": value_for(body, "effect") or "-",
        "raw": value_for(body, "raw") or "0x00000000",
        "eff": value_for(body, "eff") or "0x00000000",
        "omh": value_for(body, "omh") or "0x00000000",
        "geom": value_for(body, "geom") or "0x00000000",
        "cc": value_for(body, "cc") or "0x0000000000000000",
        "opts": value_for(body, "opts") or "0x00000000",
        "settex": value_for(body, "settex") or "0",
        "texnum": value_for(body, "texnum") or "-1",
        "tex_used": value_for(body, "tex_used") or "(0,0)",
        "blend": value_for(body, "blend") or "?",
        "api_blend": value_for(body, "api_blend") or "?",
        "rdp_mem": value_for(body, "rdp_mem") or "none",
        "room_cvg": value_for(body, "room_cvg") or "0",
        "water_suppress": value_for(body, "water_suppress") or "0",
        "room_sort": value_for(body, "room_sort") or "0",
        "alpha": value_for(body, "alpha") or "0",
        "fog": value_for(body, "fog") or "0",
        "texedge": value_for(body, "texedge") or "0",
        "z": decode_value(decode, "z") or "?",
        "cvg": decode_value(decode, "cvg") or "?",
        "zcmp": decode_value(decode, "zcmp") or "0",
        "zupd": decode_value(decode, "zupd") or "0",
        "aa": decode_value(decode, "aa") or "0",
        "imrd": decode_value(decode, "imrd") or "0",
        "clr_on_cvg": decode_value(decode, "clr_on_cvg") or "0",
        "cvg_x_alpha": decode_value(decode, "cvg_x_alpha") or "0",
        "alpha_cvg": decode_value(decode, "alpha_cvg") or "0",
        "force_bl": decode_value(decode, "force_bl") or "0",
    }
    return row


def load_rows(paths: list[Path]) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for path in paths:
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            for line_no, line in enumerate(handle, 1):
                match = RDP_MODE_RE.search(line)
                if not match:
                    continue
                row = parse_row(match.group("body"))
                row["source"] = f"{path}:{line_no}"
                rows.append(row)
    return rows


def signature(row: dict[str, str]) -> tuple[str, ...]:
    return (
        row["class"],
        row["effect"],
        row["raw"],
        row["blend"],
        row["api_blend"],
        row["rdp_mem"],
        row["settex"],
        row["texnum"],
        row["tex_used"],
        row["z"],
        row["cvg"],
        row["zcmp"],
        row["zupd"],
        row["imrd"],
        row["clr_on_cvg"],
        row["cvg_x_alpha"],
        row["alpha_cvg"],
        row["force_bl"],
        row["fog"],
        row["texedge"],
    )


def is_unpromoted_coverage_candidate(row: dict[str, str]) -> bool:
    return (
        row["blend"] == "alpha"
        and row["api_blend"] == "alpha"
        and row["z"] == "xlu"
        and row["cvg"] == "wrap"
        and row["imrd"] == "1"
        and row["clr_on_cvg"] == "1"
        and row["force_bl"] == "1"
        and row["cvg_x_alpha"] == "0"
        and row["alpha_cvg"] == "0"
    )


def print_bucket(title: str, counter: collections.Counter[tuple[str, ...]], top: int) -> None:
    print(title)
    if not counter:
        print("  none")
        return
    for sig, count in counter.most_common(top):
        print(f"  {count:5d}  " + " ".join(sig))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument("--top", type=int, default=20)
    args = parser.parse_args(argv)

    rows = load_rows(args.logs)
    if not rows:
        print("no RDP-MODE rows found", file=sys.stderr)
        return 1

    by_signature = collections.Counter(signature(row) for row in rows)
    by_raw_api = collections.Counter(
        (
            row["raw"],
            row["class"],
            row["blend"],
            row["api_blend"],
            row["rdp_mem"],
            f"z={row['z']}",
            f"cvg={row['cvg']}",
            f"fog={row['fog']}",
            f"texedge={row['texedge']}",
        )
        for row in rows
    )
    by_depth = collections.Counter(
        (
            row["z"],
            f"zcmp={row['zcmp']}",
            f"zupd={row['zupd']}",
            row["blend"],
            row["api_blend"],
        )
        for row in rows
    )
    candidates = [row for row in rows if is_unpromoted_coverage_candidate(row)]
    by_candidate = collections.Counter(signature(row) for row in candidates)

    print(f"rows: {len(rows)}")
    print(f"unique signatures: {len(by_signature)}")
    print_bucket("top raw/api buckets:", by_raw_api, args.top)
    print_bucket("top depth buckets:", by_depth, args.top)
    print_bucket("unpromoted XLU coverage-wrap candidates:", by_candidate, args.top)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
