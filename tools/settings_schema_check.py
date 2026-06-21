#!/usr/bin/env python3
"""Validate native settings schema introspection without requiring a ROM."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path


EXPECTED_DEFAULTS = {
    "Video.WindowWidth": "1440",
    "Video.WindowHeight": "810",
    "Video.Fullscreen": "0",
    "Input.MouseSensitivity": "0.15",
    "Input.MouseSensitivityAim": "0.05",
    "Input.InvertY": "0",
    "Input.GamepadLookSpeed": "8",
    "Audio.MasterVolume": "0.7",
    "Audio.DeviceSamples": "512",
}

CUSTOM_CONFIG = """\
[Video]
WindowWidth=1024
WindowHeight=768
Fullscreen=1

[Input]
MouseSensitivity=0.25
MouseSensitivityAim=0.125
InvertY=1
GamepadLookSpeed=12

[Audio]
MasterVolume=0.5
DeviceSamples=1024
"""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--binary",
        default="build/ge007",
        help="native binary to inspect (default: build/ge007)",
    )
    return parser.parse_args()


def run_binary(binary: Path, savedir: Path, flag: str) -> str:
    env = os.environ.copy()
    env.pop("GE007_DEBUG", None)

    result = subprocess.run(
        [str(binary), "--savedir", str(savedir), flag],
        cwd=binary.parent.parent if binary.parent.name == "build" else Path.cwd(),
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )

    output = result.stdout
    if result.returncode != 0:
        raise SystemExit(
            f"FAIL: {flag} exited with {result.returncode}\n{output[-2000:]}"
        )

    forbidden = ("[ROM]", "No GoldenEye ROM", "[GE007-PC] Starting")
    for marker in forbidden:
        if marker in output:
            raise SystemExit(f"FAIL: {flag} touched runtime/ROM startup marker {marker!r}")

    return output


def parse_dump(output: str) -> dict[str, str]:
    values: dict[str, str] = {}
    section = ""

    for raw_line in output.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
            continue
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        full_key = f"{section}.{key}" if section else key
        values[full_key] = value

    return values


def assert_defaults_from_list(output: str) -> None:
    for key, default in EXPECTED_DEFAULTS.items():
        if key not in output:
            raise SystemExit(f"FAIL: --list-settings omitted {key}")
        needle = f"default={default}"
        if needle not in output:
            raise SystemExit(f"FAIL: --list-settings missing {needle} for {key}")


def assert_dump_values(values: dict[str, str], expected: dict[str, str], label: str) -> None:
    for key, expected_value in expected.items():
        actual = values.get(key)
        if actual != expected_value:
            raise SystemExit(
                f"FAIL: {label}: {key} expected {expected_value!r}, got {actual!r}"
            )


def main() -> int:
    args = parse_args()
    binary = Path(args.binary).resolve()
    if not binary.is_file():
        raise SystemExit(f"FAIL: native binary not found: {binary}")

    with tempfile.TemporaryDirectory(prefix="mgb64_settings_schema_") as temp:
        savedir = Path(temp)

        listing = run_binary(binary, savedir, "--list-settings")
        assert_defaults_from_list(listing)

        default_dump = parse_dump(run_binary(binary, savedir, "--dump-config"))
        assert_dump_values(default_dump, EXPECTED_DEFAULTS, "default dump")

        (savedir / "ge007.ini").write_text(CUSTOM_CONFIG, encoding="utf-8")
        custom_dump = parse_dump(run_binary(binary, savedir, "--dump-config"))
        assert_dump_values(
            custom_dump,
            {
                "Video.WindowWidth": "1024",
                "Video.WindowHeight": "768",
                "Video.Fullscreen": "1",
                "Input.MouseSensitivity": "0.25",
                "Input.MouseSensitivityAim": "0.125",
                "Input.InvertY": "1",
                "Input.GamepadLookSpeed": "12",
                "Audio.MasterVolume": "0.5",
                "Audio.DeviceSamples": "1024",
            },
            "custom dump",
        )

    print(f"PASS: settings schema introspection ({len(EXPECTED_DEFAULTS)} settings)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
