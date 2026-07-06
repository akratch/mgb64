# Changelog

All notable changes to MGB64 are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/); this project uses
[Semantic Versioning](https://semver.org/).

Detailed, narrative notes for each released version live in
[RELEASE_NOTES.md](RELEASE_NOTES.md); this file is the concise, scannable index.

## [Unreleased]

## [0.3.2] - 2026-07-06

Repository-quality, maintainability, and licensing-correctness pass (no gameplay
or rendering change; every runtime edit is verified against the ROM-free CTest
suite and is byte-identical with features off).

### Added
- `docs/README.md` and `tools/README.md` — navigable indexes for the docs and
  tooling trees.
- Registering `GE007_*` flag accessors (`src/platform/port_env.*`) plus a
  generated, drift-gated flag reference (`docs/ENV_FLAGS.md`,
  `tools/gen_env_reference.py`).
- `scripts/ci/ci_local.sh` — one-command local runner for the ROM-free CI gates.
- `src/platform/fast3d/gfx_uniforms.h` — one shared declaration of the render/
  post-FX uniforms consumed by the GL and Metal backends.
- `src/platform/fast3d/PROVENANCE.md` recording the vendored Fast3D renderer's
  origin and reproducing its upstream license notice.
- CTest guards: `port_validation_smoke_registry`, `env_reference_current`,
  `port_env`.

### Changed
- `docs/` reorganized: a curated public reference at the top level and internal
  design/roadmap notes under `docs/design/` (export-ignored). 32 transient
  session/planning artifacts removed.
- The public/internal doc boundary in `.gitattributes` is now structural
  (`docs/design/**`).
- CONTRIBUTING documents a commit-message convention and the env-flag convention.
- `tools/check_third_party_notices.py` now validates the Fast3D license notice
  (copyright line, license identification, and binary clause) so it cannot
  silently regress.

### Fixed
- **Windows: immediate crash on game load.** MinGW/GCC defaults to
  `-mms-bitfields`, which repacks the game's mixed bit-field structs and desyncs
  them from the N64-matching ROM/model/setup data read through them. The engine
  now builds with `-mno-ms-bitfields` on every platform (the macOS/Linux default,
  byte-identical there), so the Windows layout matches and the data parses
  correctly.
- **Fast3D upstream license was mis-identified as MIT.** It is actually a
  modified BSD-2-Clause license (© 2020 Emill, MaikelChan) whose clause 2
  restricts binary redistribution to binaries containing no assets the
  distributor lacks the right to distribute. The reproduced notice was reconciled
  verbatim against the authoritative upstream `LICENSE.txt`, and the
  identification was corrected in the Fast3D source-file headers, `THIRD_PARTY.md`,
  and `PROVENANCE.md`. MGB64's prebuilt binaries are asset-free (verified by
  `check_no_rom_data.sh` / `verify_asset_free.sh`) and so satisfy the binary
  clause.
- Integer-overflow in the ROM DMA bounds check (`src/ramrom.c`) that a crafted
  ROM image could use to read past the loaded buffer.
- Removed 38 phantom validation-smoke registrations that pointed at scripts which
  were never committed (advertised coverage now matches what ships).
- Removed a dead, build-excluded copy of `gfx_pc.c` with a misleading header.
- De-duplicated shared render-uniform `extern` declarations onto the new
  `gfx_uniforms.h`, and repaired stale documentation references to the removed
  legacy `gfx_pc.c`.

## Released

See [RELEASE_NOTES.md](RELEASE_NOTES.md) for full notes.

- **v0.3.2** — repository-quality pass + Fast3D license correction
- **v0.3.1** — Remote Mine detonator fix
- **v0.3.0** — MGB64 becomes a real in-process app (launcher + in-game overlay)
- **v0.2.1** — pre-ship hardening
- **v0.2.0** — visual remaster + native Metal backend
- **v0.1.0** — first public release
