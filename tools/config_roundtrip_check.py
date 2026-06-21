#!/usr/bin/env python3
"""Validate no-ROM config set/save/reset round-trips through the native binary."""

from __future__ import annotations

import argparse
import os
import subprocess
import tempfile
from pathlib import Path


DEFAULTS = {
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

SEED_CONFIG = """\
# User-owned comments are not guaranteed to round-trip yet, but unknown keys are.
[Video]
WindowWidth=1024
WindowHeight=768
FutureVideo=keep-me

[Input]
MouseSensitivity=0.25
InvertY=0

[Future]
Token=hello
Number=42
"""


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--binary",
        default=str(repo_root / "build" / "ge007"),
        help="native binary to inspect (default: build/ge007)",
    )
    return parser.parse_args()


def run_binary(binary: Path, savedir: Path, *args: str) -> str:
    env = os.environ.copy()
    env.pop("GE007_DEBUG", None)

    result = subprocess.run(
        [str(binary), "--savedir", str(savedir), *args],
        cwd=Path(__file__).resolve().parents[1],
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    output = result.stdout
    if result.returncode != 0:
        raise SystemExit(
            f"FAIL: {' '.join(args)} exited with {result.returncode}\n{output[-2000:]}"
        )

    forbidden = ("[ROM]", "No GoldenEye ROM", "[GE007-PC] Starting")
    for marker in forbidden:
        if marker in output:
            raise SystemExit(f"FAIL: {' '.join(args)} touched runtime/ROM startup marker {marker!r}")

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
        full_key = f"{section}.{key.strip()}" if section else key.strip()
        values[full_key] = value.strip()

    return values


def assert_values(values: dict[str, str], expected: dict[str, str], label: str) -> None:
    for key, expected_value in expected.items():
        actual = values.get(key)
        if actual != expected_value:
            raise SystemExit(
                f"FAIL: {label}: {key} expected {expected_value!r}, got {actual!r}"
            )


def assert_file_contains(path: Path, needles: list[str], label: str) -> None:
    text = path.read_text(encoding="utf-8")
    for needle in needles:
        if needle not in text:
            raise SystemExit(f"FAIL: {label}: missing {needle!r} in {path}")


def assert_no_tmp(savedir: Path) -> None:
    leftovers = sorted(savedir.glob("*.tmp"))
    if leftovers:
        raise SystemExit(f"FAIL: config save left temp file(s): {leftovers}")


def main() -> int:
    args = parse_args()
    binary = Path(args.binary).resolve()
    if not binary.is_file():
        raise SystemExit(f"FAIL: native binary not found: {binary}")

    with tempfile.TemporaryDirectory(prefix="mgb64_config_roundtrip_") as temp:
        savedir = Path(temp)
        config_path = savedir / "ge007.ini"
        config_path.write_text(SEED_CONFIG, encoding="utf-8")

        run_binary(
            binary,
            savedir,
            "--config-set",
            "Input.InvertY=1",
            "--config-set",
            "Audio.MasterVolume=0.5",
            "--config-set",
            "Video.WindowWidth=1280",
        )
        assert_no_tmp(savedir)
        assert_file_contains(
            config_path,
            [
                "FutureVideo=keep-me",
                "[Future]",
                "Token=hello",
                "Number=42",
            ],
            "unknown-key passthrough",
        )

        updated = parse_dump(run_binary(binary, savedir, "--dump-config"))
        assert_values(
            updated,
            {
                "Video.WindowWidth": "1280",
                "Video.WindowHeight": "768",
                "Input.MouseSensitivity": "0.25",
                "Input.InvertY": "1",
                "Audio.MasterVolume": "0.5",
            },
            "updated dump",
        )

        run_binary(binary, savedir, "--reset-config")
        assert_no_tmp(savedir)
        assert_file_contains(
            config_path,
            [
                "FutureVideo=keep-me",
                "[Future]",
                "Token=hello",
                "Number=42",
            ],
            "unknown-key passthrough after reset",
        )

        reset = parse_dump(run_binary(binary, savedir, "--dump-config"))
        assert_values(reset, DEFAULTS, "reset dump")

    print("PASS: config round-trip, reset, unknown-key preservation")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
