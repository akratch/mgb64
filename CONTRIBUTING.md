# Contributing to MGB64

Thanks for your interest in contributing! This project is a decompilation and
native port built by and for the preservation community. Contributions of all
kinds are welcome: decompilation/matching work, port and renderer fixes,
tooling, documentation, and bug reports.

Please read this whole document before opening a pull request — especially the
first section.

## ⚠️ The one rule that matters most: no copyrighted data, ever

This repository must **never** contain the game ROM or anything derived from it.
This is what keeps the project legal and publishable. Before every commit, make
sure you are not adding any of the following:

- a ROM in any form (`*.z64`, `*.n64`, `*.v64`, `baserom*`, compressed/`cdata`);
- **extracted assets**: textures/images, audio/music/sequences, models, fonts,
  level/scene/setup data, image or animation tables, logos — in **any** form,
  including raw `.bin` *and* inline `u8[]`/`u16[]` data arrays pasted into `.c`
  files;
- save data (`*.eeprom`), Ghidra/IDA project databases of the ROM, or
  decompiled-from-ROM data dumps;
- proprietary SDK/header/source material or vendor binaries. Existing
  SDK-derived compatibility files are inventoried in `THIRD_PARTY.md` as a
  migration target; do not add more.

The decompilation legitimately includes small, **code-coupled** data tables
(AI scripts, collision tables, level/room index tables, display lists) as
decompiled source — the same line the SM64 and OoT projects draw. If you are
unsure whether something is a "code-coupled table" (OK) or a "standalone asset"
(not OK), ask in your PR rather than committing it.

The `.gitignore` is set up to block the common cases, and CI checks for
contamination, but **the human reviewer and you are the real safeguard.** When
in doubt, leave it out.

By contributing, you affirm that your contribution is your own original work (or
properly attributed), and that you are not adding any third party's copyrighted
material.

## Getting set up

See **[docs/BUILDING.md](docs/BUILDING.md)** for toolchain, dependencies, asset
extraction (you supply your own ROM), the native port build, and the N64 ROM
matching target.

Before you push, run the quick validation lane:

```sh
./tools/validate_quick.sh
```

It runs a static source guard (no ROM needed) and, if you have a build + ROM, a
boot/spawn smoke. See **[docs/INSTRUMENTATION.md](docs/INSTRUMENTATION.md)** for
the full validation tooling (save/pixel/state/audio lanes, the trace schema, and
the diagnostic environment variables).

## Coding style

- C and assembly style is enforced by [`.clang-format`](.clang-format) and
  [`.editorconfig`](.editorconfig). Run `clang-format` on files you touch.
- Match the conventions of the surrounding code: naming, indentation, and
  comment density. Decompiled code mirrors the original structure — don't
  refactor it into a different shape just for taste.
- Keep port-layer changes inside `src/platform/` where possible, behind the
  existing platform seams, so the decompiled game code stays clean.
- See **[docs/CODING_STYLE.md](docs/CODING_STYLE.md)** for more detailed
  naming, header, scope, and platform-boundary guidance.

## Decompilation / matching work

- New or changed decompiled functions should aim to **match** the original. Use
  the in-tree tooling (`tools/asm-processor`, `tools/diff.py`,
  `scripts/asmdiff.sh`) to compare against the target.
- Prefer matching code. If a function can't be matched yet, document why.
- Don't change behavior of game code to "fix" a port issue — fix it in the
  platform layer instead, so the decompilation stays faithful.

## Port / renderer / audio work

- Prove visual and audio changes where you can; small, reviewable changes are
  preferred over large speculative ones.
- The native port must remain **asset-free**: no game data compiled into the
  binary. If your change touches asset loading, keep data coming from the user's
  ROM at runtime, and keep `macos/Scripts/verify_asset_free.sh` passing.

## Pull requests

1. Fork and branch from `main`.
2. Keep PRs focused; write a clear description of what changed and why.
3. Confirm the build still configures/compiles, and run any relevant checks.
4. **Re-read your diff for accidental ROM-derived data** before pushing
   (`git diff --stat` and skim added files).
5. Be responsive to review. Be kind; assume good faith.

## Reporting bugs

Open an issue with: your platform, the ROM region you used, exact steps to
reproduce, what you expected, and what happened (logs/stack traces help). Please
do **not** attach ROMs, save files, or screenshots that contain copyrighted game
imagery.

## Code of conduct

Be respectful and constructive. Harassment or discrimination of any kind is not
welcome. Maintainers may moderate to keep the project healthy.

---

Questions about whether something is safe to commit, or where to start? Open a
discussion or a draft PR and ask. Welcome aboard.
