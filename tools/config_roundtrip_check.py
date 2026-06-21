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
    "Video.WindowX": "-1",
    "Video.WindowY": "-1",
    "Video.Display": "0",
    "Video.WindowMode": "windowed",
    "Video.VSync": "adaptive",
    "Video.FrameCap": "60",
    "Video.Gamma": "1",
    "Video.RenderScale": "1",
    "Video.MSAA": "0",
    "Video.RetroFilter": "auto",
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
WindowX=-1
WindowY=-1
Display=0
WindowMode=windowed
VSync=adaptive
FrameCap=60
Gamma=1
RenderScale=1
MSAA=0
RetroFilter=auto
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


def run_binary(
    binary: Path,
    savedir: Path,
    *args: str,
    env_extra: dict[str, str] | None = None,
) -> str:
    env = os.environ.copy()
    env.pop("GE007_DEBUG", None)
    if env_extra:
        env.update(env_extra)

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

        env_dump = parse_dump(
            run_binary(
                binary,
                savedir,
                "--dump-config",
                env_extra={
                    "GE007_WINDOW_WIDTH": "1333",
                    "GE007_WINDOW_X": "11",
                    "GE007_WINDOW_Y": "22",
                    "GE007_DISPLAY": "1",
                    "GE007_WINDOW_MODE": "borderless",
                    "GE007_VSYNC": "on",
                    "GE007_FRAME_CAP": "30",
                    "GE007_GAMMA": "1.2",
                    "GE007_RENDER_SCALE": "1.5",
                    "GE007_MSAA": "4",
                    "GE007_RETRO_FILTER": "on",
                },
            )
        )
        assert_values(
            env_dump,
            {
                "Video.WindowWidth": "1333",
                "Video.WindowX": "11",
                "Video.WindowY": "22",
                "Video.Display": "1",
                "Video.WindowMode": "borderless",
                "Video.VSync": "on",
                "Video.FrameCap": "30",
                "Video.Gamma": "1.2",
                "Video.RenderScale": "1.5",
                "Video.MSAA": "4",
                "Video.RetroFilter": "on",
            },
            "env override dump",
        )
        assert_file_contains(
            config_path,
            [
                "WindowWidth=1024",
                "WindowX=-1",
                "WindowY=-1",
                "Display=0",
                "WindowMode=windowed",
                "VSync=adaptive",
                "FrameCap=60",
                "Gamma=1",
                "RenderScale=1",
                "MSAA=0",
                "RetroFilter=auto",
            ],
            "env override is not persisted",
        )

        cli_dump = parse_dump(
            run_binary(
                binary,
                savedir,
                "--config-override",
                "Video.WindowHeight=720",
                "--config-override",
                "Video.WindowX=33",
                "--config-override",
                "Video.WindowY=44",
                "--config-override",
                "Video.Display=2",
                "--config-override",
                "Video.WindowMode=exclusive",
                "--config-override",
                "Video.VSync=off",
                "--config-override",
                "Video.FrameCap=display",
                "--config-override",
                "Video.Gamma=1.3",
                "--config-override",
                "Video.RenderScale=0.75",
                "--config-override",
                "Video.MSAA=2",
                "--config-override",
                "Video.RetroFilter=off",
                "--dump-config",
            )
        )
        assert_values(
            cli_dump,
            {
                "Video.WindowHeight": "720",
                "Video.WindowX": "33",
                "Video.WindowY": "44",
                "Video.Display": "2",
                "Video.WindowMode": "exclusive",
                "Video.VSync": "off",
                "Video.FrameCap": "display",
                "Video.Gamma": "1.3",
                "Video.RenderScale": "0.75",
                "Video.MSAA": "2",
                "Video.RetroFilter": "off",
            },
            "cli override dump",
        )
        assert_file_contains(
            config_path,
            [
                "WindowHeight=768",
                "WindowX=-1",
                "WindowY=-1",
                "Display=0",
                "WindowMode=windowed",
                "VSync=adaptive",
                "FrameCap=60",
                "Gamma=1",
                "RenderScale=1",
                "MSAA=0",
                "RetroFilter=auto",
            ],
            "cli override is not persisted",
        )

        precedence_dump = parse_dump(
            run_binary(
                binary,
                savedir,
                "--config-override",
                "Video.WindowWidth=1400",
                "--dump-config",
                env_extra={"GE007_WINDOW_WIDTH": "1333"},
            )
        )
        assert_values(precedence_dump, {"Video.WindowWidth": "1400"}, "cli over env precedence")

        run_binary(
            binary,
            savedir,
            "--config-set",
            "Input.InvertY=1",
            "--config-set",
            "Audio.MasterVolume=0.5",
            "--config-set",
            "Video.WindowWidth=1280",
            "--config-set",
            "Video.WindowX=50",
            "--config-set",
            "Video.WindowY=60",
            "--config-set",
            "Video.Display=1",
            "--config-set",
            "Video.WindowMode=borderless",
            "--config-set",
            "Video.VSync=off",
            "--config-set",
            "Video.FrameCap=30",
            "--config-set",
            "Video.Gamma=1.4",
            "--config-set",
            "Video.RenderScale=2",
            "--config-set",
            "Video.MSAA=8",
            "--config-set",
            "Video.RetroFilter=on",
        )
        assert_no_tmp(savedir)
        assert_file_contains(
            config_path,
            [
                "# Window width",
                "# type=int scope=restart default=1440 range=320..3840",
                "# Window X",
                "# type=int scope=restart default=-1 range=-1..32767",
                "WindowX=50",
                "# Window Y",
                "# type=int scope=restart default=-1 range=-1..32767",
                "WindowY=60",
                "# Display",
                "# type=int scope=restart default=0 range=0..31",
                "Display=1",
                "# Window mode",
                "# type=enum scope=live default=windowed range=windowed|borderless|exclusive",
                "WindowMode=borderless",
                "# VSync",
                "# type=enum scope=live default=adaptive range=off|on|adaptive",
                "VSync=off",
                "# Frame cap",
                "# type=enum scope=live default=60 range=30|60|display",
                "FrameCap=30",
                "# Gamma",
                "# type=float scope=live default=1 range=0.5..2.5",
                "Gamma=1.4",
                "# Render scale",
                "# type=float scope=restart default=1 range=0.5..2",
                "RenderScale=2",
                "# MSAA",
                "# type=enum scope=live default=0 range=0|2|4|8",
                "MSAA=8",
                "# Retro filter",
                "# type=enum scope=live default=auto range=auto|off|on",
                "RetroFilter=on",
                "# Master volume",
                "# type=float scope=live default=0.7 range=0..1",
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
                "Video.WindowX": "50",
                "Video.WindowY": "60",
                "Video.Display": "1",
                "Video.WindowMode": "borderless",
                "Video.VSync": "off",
                "Video.FrameCap": "30",
                "Video.Gamma": "1.4",
                "Video.RenderScale": "2",
                "Video.MSAA": "8",
                "Video.RetroFilter": "on",
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
