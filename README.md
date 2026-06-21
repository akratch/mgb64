<h1 align="center">MGB64</h1>
<p align="center"><em>Man with the Golden Build</em></p>

<p align="center">
A <strong>decompilation</strong> and <strong>native source port</strong>
of a 1997 Nintendo&nbsp;64 first-person shooter, reimplemented in portable C so
it can be studied, preserved, and run natively on modern machines.
</p>

<p align="center">
<a href="LICENSE"><img alt="License: MIT (first-party)" src="https://img.shields.io/badge/license-MIT%20(first--party)-blue.svg"></a>
<img alt="Status: experimental" src="https://img.shields.io/badge/status-experimental-orange.svg">
<img alt="Bring your own ROM" src="https://img.shields.io/badge/assets-bring%20your%20own%20ROM-red.svg">
</p>

> [!IMPORTANT]
> **This project ships no game.** It contains decompiled code and an original
> port layer — **no ROM and no copyrighted assets** (graphics, audio, models,
> fonts, level data). To run the port you must supply your **own**
> legally-dumped copy of the game. Please read **[DISCLAIMER.md](DISCLAIMER.md)**.

## Gameplay Examples

| Runway | Cradle |
| --- | --- |
| [![Runway gameplay example](https://img.youtube.com/vi/Jh3GOirvobI/hqdefault.jpg)](https://youtu.be/Jh3GOirvobI) | [![Cradle gameplay example](https://img.youtube.com/vi/Pob6Itc7rCQ/hqdefault.jpg)](https://youtu.be/Pob6Itc7rCQ) |
| [Watch on YouTube](https://youtu.be/Jh3GOirvobI) | [Watch on YouTube](https://youtu.be/Pob6Itc7rCQ) |

## What is this?

MGB64 combines the decompiled game code with a native PC/macOS port. It builds
on the same N64 decompilation ecosystem as
[Super Mario 64](https://github.com/n64decomp/sm64) and
[Ocarina of Time](https://github.com/zeldaret/oot) decompilations and the
[Perfect Dark](https://github.com/fgsfdsfgs/perfect_dark) port:

- **A decompilation** — the original N64 game's logic, rewritten as readable,
  buildable C and MIPS assembly. The native port is the supported build today;
  byte-identical N64 ROM rebuilding is still in progress.
- **A native port** — a platform layer (`src/platform/`) that runs the same game
  code on PC/macOS: it loads *your* ROM at runtime, translates the N64 display
  lists to a modern GPU, and provides audio, input, and file I/O. No copyrighted
  data is compiled into the binary ("bring your own ROM").

## Status

> **Experimental.** This is a research/preservation project, not a polished
> consumer release, and it is **not** a 1:1 replacement for original hardware.

- ✅ The decompilation covers the bulk of the game's systems (rendering, AI,
  combat, level logic, menus, audio).
- ✅ **The native port builds and runs from a clean checkout + your own ROM**
  (`cmake -B build && cmake --build build`), with native rendering, audio, and
  keyboard/mouse + gamepad input.
- ✅ **Asset-free:** no ROM media is compiled into the binary — textures, audio,
  animation, fonts, and logos are read from *your* ROM at runtime.
- 🚧 Rebuilding the original **N64 ROM** (byte-matching), SDK provenance cleanup,
  and renderer/audio parity are the main open areas. See
  **[docs/STATUS.md](docs/STATUS.md)** and **[ROADMAP.md](ROADMAP.md)** for the
  precise state and good first issues.
- ⚠️ The tree still contains inventoried N64 SDK/libultra compatibility
  material that is **not** MIT-licensed first-party code. See
  **[THIRD_PARTY.md](THIRD_PARTY.md)** before describing or redistributing the
  project.

There are known graphical and gameplay issues and occasional instability. Bug
reports and PRs are very welcome.

## Building

Full instructions — toolchain, dependencies, native-port setup, and the
matching-target asset-extraction path — are in
**[docs/BUILDING.md](docs/BUILDING.md)**. In brief:

1. Install the build dependencies for your platform (see the build doc).
2. Build the native port with CMake; the public checkout compiles without ROM
   media or extracted assets.
3. Run the port with a ROM **you legally own** using `--rom /path/to/rom.z64`,
   or place it at the repository root for auto-detection.
4. Matching-target contributors can use the asset-extraction and
   Makefile path described in the build docs.

```sh
# Native port (PC/macOS):
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Running

You must already own and supply the ROM — the port reads it at runtime; no game
data is bundled. The default command boots normally into the game frontend:

```sh
./build/ge007 --rom "/path/to/baserom.u.z64"
```

Common startup examples:

```sh
# Boot normally with an explicit ROM path:
./build/ge007 --rom "/path/to/baserom.u.z64"

# Direct-boot a solo stage by name:
./build/ge007 --rom "/path/to/baserom.u.z64" --level dam

# Direct-boot mission 2 on Secret Agent:
./build/ge007 --rom "/path/to/baserom.u.z64" --mission 2 --difficulty secret

# Direct-boot by raw internal LEVELID, useful for validation:
./build/ge007 --rom "/path/to/baserom.u.z64" --level 33

# Keep saves/config in a dedicated directory:
./build/ge007 --rom "/path/to/baserom.u.z64" --savedir "$HOME/.mgb64"
```

If you don't pass `--rom`, the port auto-detects a compatible ROM in the working
directory and common locations (`~/Downloads`, `~/Documents`, `~/Desktop`, `~`)
by size, N64 header, and internal cartridge name.

Useful runtime options:

| Option | Purpose |
| --- | --- |
| `--rom PATH` | Load the ROM from `PATH`. A positional ROM path also works. |
| `--level NAME` | Direct-boot a supported solo stage by slug, for example `dam`, `facility`, `runway`, `surface1`, `bunker1`, or `egypt`. |
| `--level N` | Direct-boot a raw internal `LEVELID`. Example: `33` is Dam. These numbers are not mission-order numbers. |
| `--mission N` | Direct-boot solo mission order `1` through `20`. Use this when you mean "mission 2", "mission 3", etc. |
| `--difficulty VALUE` | Select the direct-boot difficulty. Values: `agent`, `secret`, `00`, `007`, or numeric `0` through `3`. Defaults to Agent. |
| `--savedir PATH` | Store `ge007.ini` and save data in `PATH`. Without this, the port uses the current directory when writable, then falls back to a per-user directory. |
| `--list-settings` | Print registered persistent settings and exit. Does not require a ROM. |
| `--dump-config` | Print the current config view after loading `ge007.ini` and exit. Does not require a ROM. |
| `--config-override KEY=VALUE` | Override a persistent setting for this run without saving it. Repeatable. |
| `--config-set KEY=VALUE` | Set a persistent config value, save `ge007.ini`, and exit. Repeatable. Does not require a ROM. |
| `--reset-config` | Reset registered persistent settings to defaults, save `ge007.ini`, and exit. Does not require a ROM. |
| `--no-input-grab` | Do not capture the mouse. Useful while testing windowed startup. |
| `--background` | Run without grabbing input and with background-friendly settings. Useful for smoke tests. |

If you pass no direct-boot option, the game starts normally. `--level` accepts
stage slugs or raw internal `LEVELID` values; `--mission` accepts campaign order.
For example, use `--mission 2` or `--level facility` for mission 2, not
`--level 2`. See [docs/BUILDING.md](docs/BUILDING.md#running) for more platform
setup detail.

### Controls (default)

| Action | Keyboard & mouse | Gamepad |
| --- | --- | --- |
| Move | `W` `A` `S` `D` | Left stick |
| Look | Mouse | Right stick |
| Fire | Left click | RT |
| Aim (hold) | Right click | LT |
| Cycle weapon | Mouse wheel | `Y` (next) / Back (prev) |
| Reload | `R` | — |
| Interact | `F` | — |
| Crouch (toggle) | `C` / `LCtrl` | L3 |
| Lean L/R (in aim mode) | `Q` / `E` | — |
| Watch menu | `Tab` | — |
| Pause | `Esc` | — |
| Mute audio | `M` | — |
| Show controls | `H` | — |

Mouse sensitivity, window size, display mode, and VSync are configurable in
`ge007.ini` (written next to the executable on first run). The display mode key
is `Video.WindowMode` with `windowed`, `borderless`, or `exclusive`; for example:
`--config-set Video.WindowMode=borderless`. VSync is `Video.VSync` with `off`,
`on`, or `adaptive`. `Video.Display` selects the zero-based SDL display index
and falls back to display 0 if the saved index is unavailable. Pressing `H` also
prints the full control list to the console.

## Known limitations

This is an experimental, work-in-progress port — **not** a 1:1 replacement for
original hardware. It runs the game's decompiled code natively; it is not an
emulator and is not cycle-exact. Rendering and audio use some documented
compatibility approximations, and a few areas are still converging toward exact
N64 behavior. **[PORT.md](PORT.md)** has the full, honest breakdown: what is and
isn't faithful, the known divergences, and the validation coverage. Please read
it before filing parity bugs.

## Repository layout

| Path | Contents |
| --- | --- |
| `src/` | Decompiled game code (`src/game/`), libultra, and the native port layer (`src/platform/`) |
| `include/` | Shared headers |
| `assets/` | Asset **build glue** only (extraction config, linker scripts, `.incbin` wrappers). Extracted ROM data lands here at build time and is git-ignored. |
| `tools/` | Build/extraction tooling (extractor, texture conversion, checksum, `asm-processor`, …) |
| `scripts/` | Extraction and build helper scripts |
| `macos/` | Native macOS app shell sources (Swift/AppKit), local unsigned `.app` packaging, and signing/notarization scripts — see [`macos/README.md`](macos/README.md) |
| `ld/`, `*.ld` | Linker scripts |
| `docs/` | Build, status, and design documentation |

## Contributing

Contributions are welcome — see **[CONTRIBUTING.md](CONTRIBUTING.md)** for the
workflow, coding style, and the one rule that matters most: **never commit ROMs
or any ROM-derived assets.** Project conduct is covered in
**[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)**, and vulnerability / repository
hygiene reports are covered in **[SECURITY.md](SECURITY.md)**.

Maintainers should enable the tracked safety hooks once per checkout:

```sh
scripts/install_git_hooks.sh
```

The hooks run the ROM/data contamination guard before commits and pushes.

## Acknowledgements

This project builds directly on the
[**n64decomp/007**](https://github.com/n64decomp/007) decompilation, which we
started from and continue to work off of. Our thanks to its authors and
contributors for the foundational reverse-engineering effort that this port
extends.

It also stands on the shoulders of the wider N64 decompilation community. Thanks
to the SM64, Ocarina of Time, and Perfect Dark decomp/port projects for the
tooling, conventions, and trail-blazing that make work like this possible, and
to the authors of [`asm-processor`](tools/asm-processor) and the other bundled
or locally referenced tools.

## License & legal

First-party code in this repository (the port layer, build system, tooling, and
documentation) is released under the **[MIT License](LICENSE)**. The MIT license
does **not** grant any rights in the original game. See **[NOTICE.md](NOTICE.md)**
for the short license/provenance summary.

The decompiled game code is a transformative work provided for research,
preservation, and educational purposes. **No copyrighted ROM or assets are
distributed.** All trademarks are the property of their respective owners, and
this project is not affiliated with or endorsed by any rights holder. Please
read **[DISCLAIMER.md](DISCLAIMER.md)** in full before using this project.

Third-party components (including N64 SDK compatibility headers/source, bundled
tools/libraries, and local-only matching-target tool placeholders) and their
provenance are inventoried in
**[THIRD_PARTY.md](THIRD_PARTY.md)**.

Maintainer release hygiene checks are documented in
**[docs/RELEASE_CHECKLIST.md](docs/RELEASE_CHECKLIST.md)**.
