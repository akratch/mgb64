<h1 align="center">MGB64</h1>
<p align="center"><em>The Man with the Golden Build</em></p>

<p align="center">
A <strong>decompilation</strong> and <strong>native source port</strong>
of the 1997 Nintendo&nbsp;64 first-person shooter developed by Rare,
<em>GoldenEye&nbsp;007</em> — reimplemented in portable C so it can be studied,
preserved, and run natively on modern machines.
</p>

<p align="center">
<a href="LICENSE"><img alt="License: MIT (first-party)" src="https://img.shields.io/badge/license-MIT%20(first--party)-blue.svg"></a>
<img alt="Status: experimental" src="https://img.shields.io/badge/status-experimental-orange.svg">
<img alt="Bring your own ROM" src="https://img.shields.io/badge/assets-bring%20your%20own%20ROM-red.svg">
</p>

---

> [!IMPORTANT]
> **This project ships no game.** It contains decompiled code and an original
> port layer — **no ROM and no copyrighted assets** (graphics, audio, models,
> fonts, level data). To build or run anything you must supply your **own**
> legally-dumped copy of the game. Please read **[DISCLAIMER.md](DISCLAIMER.md)**.

## What is this?

MGB64 is two things in one repository, in the tradition of the
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

The project name is a deliberately trademark-safe codename — a play on the Bond
film *The Man with the Golden Gun*, reframed around the *build* process, with
"64" nodding to the SM64 decomp lineage.

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

Full instructions — toolchain, dependencies, asset extraction, and the native
port build — are in **[docs/BUILDING.md](docs/BUILDING.md)**. In brief:

1. Install the build dependencies for your platform (see the build doc).
2. Place a ROM **you legally own** at the repository root (e.g. `baserom.u.z64`).
3. The build extracts the assets it needs from that ROM into git-ignored paths.
4. Build the native port with CMake; matching-target contributors can use the
   Makefile path described in the build docs.

```sh
# Native port (PC/macOS), once your ROM is in place:
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Running

You must already own and supply the ROM — the port reads it at runtime; no game
data is bundled.

```sh
./build/ge007 --rom "/path/to/baserom.u.z64"
```

If you don't pass `--rom`, the port auto-detects a GoldenEye ROM in the working
directory and common locations (`~/Downloads`, `~/Documents`, `~/Desktop`, `~`)
by size + N64 header + internal cartridge name. See
[docs/BUILDING.md](docs/BUILDING.md#running) for the save directory and options.

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

Mouse sensitivity, fullscreen, and window size are configurable in `ge007.ini`
(written next to the executable on first run). Pressing `H` also prints the full
control list to the console.

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

## Acknowledgements

This project stands on the shoulders of the N64 decompilation community. Thanks
to the SM64, Ocarina of Time, and Perfect Dark decomp/port projects for the
tooling, conventions, and trail-blazing that make work like this possible, and
to the authors of [`asm-processor`](tools/asm-processor) and the other vendored
tools.

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

Third-party components (including N64 SDK compatibility headers/source and
vendored tools/libraries) and their provenance are inventoried in
**[THIRD_PARTY.md](THIRD_PARTY.md)**.

Public release hygiene checks are documented in
**[docs/RELEASE_CHECKLIST.md](docs/RELEASE_CHECKLIST.md)**, with the current
pre-public launch decision matrix in
**[docs/PUBLIC_LAUNCH_READINESS.md](docs/PUBLIC_LAUNCH_READINESS.md)**.
