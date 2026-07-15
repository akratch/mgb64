# Native Port Audit Issues

This directory contains standalone defects found while auditing the native port.
The fidelity ledger remains authoritative for retail-versus-port parity findings;
these records cover native backend behavior, robustness, and correctness defects
that do not already have an equivalent standalone ledger entry.

For a recommended fix order across all findings, see
[PRIORITIZATION.md](PRIORITIZATION.md) (ranked by priority, severity, and
evidence, with theme clusters and a full ranked table).

## Issue Format

Every issue uses the same fields and sections:

- `Status`: lifecycle state. New reports start `Open`.
- `Severity`: user or runtime impact, independent of scheduling.
- `Priority`: recommended repair order.
- `Evidence level`: the strongest completed validation.
- `Origin`: whether this audit newly confirmed the issue or standardized a prior
  monolithic finding.
- `Affected configurations`: the configurations to which the evidence applies.
- `Summary`, `Evidence`, `Reproduction`, `Root Cause`, `Required End State`,
  `Acceptance Criteria`, `Verification Plan`, and `Related Work`.

Severity levels:

| Level | Meaning |
| --- | --- |
| S1 | Broadly unusable, destructive, or security-critical. |
| S2 | Crash, major gameplay fault, or a promised feature entirely unavailable. |
| S3 | Narrow or latent correctness fault, undefined behavior, or material degradation. |
| S4 | Minor behavior, diagnostics, or maintainability problem. |

Priority levels:

| Level | Meaning |
| --- | --- |
| P0 | Stop-ship; repair immediately. |
| P1 | Repair before the next release candidate. |
| P2 | Schedule in the active correctness backlog. |
| P3 | Repair after higher-risk defects or when touching the area. |

## Issue Index

All 73 audit findings, regardless of lifecycle state. Status is extracted from each issue file's `| Status |` row; the per-issue files under `issues/` remain authoritative if this column and the source ever disagree.

| ID | Status | Severity | Priority | Evidence | Title |
| --- | --- | --- | --- | --- | --- |
| [AUDIT-0001](issues/AUDIT-0001-opengl-scene-decor-skipped.md) | Deferred | S2 | P2 | Runtime reproduced | OpenGL silently skips all scene-decor meshes |
| [AUDIT-0002](issues/AUDIT-0002-linkless-stan-oob-modulo-zero.md) | Fixed | S2 | P2 | Shipped data reachable | OOB recovery divides by zero on linkless STAN tiles |
| [AUDIT-0003](issues/AUDIT-0003-metal-screenshot-series-crash.md) | Fixed (crash) | S2 | P2 | Runtime reproduced | Screenshot series calls OpenGL under Metal and crashes |
| [AUDIT-0004](issues/AUDIT-0004-mp-awards-uninitialized-metrics.md) | Fixed | S3 | P3 | Analyzer and source proven | Multiplayer awards evaluate uninitialized inactive metrics |
| [AUDIT-0005](issues/AUDIT-0005-cli-rom-validation-crash.md) | Fixed | S2 | P2 | Runtime reproduced | CLI accepts a wrong 12 MB ROM and crashes during boot |
| [AUDIT-0006](issues/AUDIT-0006-supported-objects-null-stan.md) | Fixed | S2 | P2 | Analyzer and source proven | Supported-object destruction dereferences nullable STAN pointers |
| [AUDIT-0007](issues/AUDIT-0007-texture-lookup-uninitialized-pointer.md) | Fixed | S3 | P2 | Shipped data reachable | Texture lookup decode advances an uninitialized source pointer |
| [AUDIT-0008](issues/AUDIT-0008-vertical-autoaim-uninitialized-state.md) | Fixed | S3 | P3 | Analyzer and source proven | Vertical-only auto-aim uses uninitialized horizontal state |
| [AUDIT-0009](issues/AUDIT-0009-pointer-registry-silent-eviction.md) | Fixed | S3 | P3 | Source proven | Low-32 pointer registry silently evicts live mappings |
| [AUDIT-0010](issues/AUDIT-0010-faithful-preset-fps-overlay.md) | Fixed | S4 | P2 | Source proven | Faithful presets retain the non-original FPS overlay |
| [AUDIT-0011](issues/AUDIT-0011-malformed-numeric-settings.md) | Fixed | S4 | P2 | Runtime reproduced | Malformed numeric settings are silently accepted and coerced |
| [AUDIT-0012](issues/AUDIT-0012-sanitizer-gate-ignores-process-failure.md) | Fixed | S3 | P1 | Fault injected | Sanitizer gate certifies nonzero process exits as clean |
| [AUDIT-0013](issues/AUDIT-0013-fidelity-ledger-index-stale.md) | Fixed | S3 | P1 | Test reproduced | Fidelity ledger index omits nine authoritative records |
| [AUDIT-0014](issues/AUDIT-0014-nuke-cleanup-quotes-asset-globs.md) | Fixed | S4 | P2 | Runtime reproduced | Nuke cleanup quotes asset globs and leaves generated binaries |
| [AUDIT-0015](issues/AUDIT-0015-hashtable-generator-false-success.md) | Open | S3 | P2 | Fault injected | Hashtable generator emits invalid output and exits zero on failure |
| [AUDIT-0016](issues/AUDIT-0016-f10-setting-help-wrong-keycode.md) | Fixed | S4 | P3 | Runtime reproduced | FPS-toggle help publishes the wrong SDL keycode for F10 |
| [AUDIT-0017](issues/AUDIT-0017-campaign-routes-stale-after-root-motion-fix.md) | Open | S3 | P1 | A/B reproduced | Four campaign routes still encode pre-FID-0117 root motion |
| [AUDIT-0018](issues/AUDIT-0018-knife-impact-fixture-stale-after-root-motion-fix.md) | Fixed | S3 | P1 | A/B reproduced | Knife-impact fixture misses after the retail root-motion fix |
| [AUDIT-0019](issues/AUDIT-0019-perf-gate-unqualified-host-baseline.md) | Fixed | S3 | P1 | Test reproduced | Performance gate hard-fails an unqualified host-specific baseline |
| [AUDIT-0020](issues/AUDIT-0020-guard-fire-rate-stale-optout-hash.md) | Fixed | S3 | P1 | Test reproduced | Guard-fire regression lane hardcodes a stale opt-out hash |
| [AUDIT-0021](issues/AUDIT-0021-knife-impact-help-stale-warp-default.md) | Fixed | S4 | P3 | Source proven | Knife-impact help advertises the wrong default warp distance |
| [AUDIT-0022](issues/AUDIT-0022-launcher-mode-overrides-survive-reexec.md) | Fixed (core) | S3 | P2 | Source proven | Launcher mode overrides survive Return-to-Launcher re-exec |
| [AUDIT-0023](issues/AUDIT-0023-minimap-objectives-falsely-unimplemented.md) | Fixed | S4 | P2 | Runtime and test proven | Implemented minimap objectives are hidden as having no effect |
| [AUDIT-0024](issues/AUDIT-0024-system-hotkeys-use-raw-numeric-sliders.md) | Open | S3 | P2 | Source and UI proven | Rebindable system hotkeys use impractical raw numeric sliders |
| [AUDIT-0025](issues/AUDIT-0025-menu-button-cross-domain-conflict.md) | Fixed (core) | S3 | P2 | Source proven | Menu-button rebinding can create a reserved-button double role |
| [AUDIT-0026](issues/AUDIT-0026-staged-string-settings-truncate-paths.md) | Fixed | S3 | P2 | Source proven | Staged string settings silently truncate paths at 63 bytes |
| [AUDIT-0027](issues/AUDIT-0027-pi-dma-bounds-check-wraps.md) | Fixed | S3 | P2 | Source proven | PI DMA bounds checks wrap and permit out-of-bounds ROM reads |
| [AUDIT-0028](issues/AUDIT-0028-weapon-sound-switches-diverge-from-tables.md) | Fixed | S4 | P2 | Source and retail-table proven | Weapon equip and reload sounds diverge from the retail tables |
| [AUDIT-0029](issues/AUDIT-0029-metal-half-res-ssao-bilinear-upsample.md) | Deferred | S4 | P3 | Source and mechanism proven | Metal half-resolution SSAO uses a non-depth-aware upsample |
| [AUDIT-0030](issues/AUDIT-0030-metal-textures-lack-private-storage-path.md) | Deferred | S4 | P3 | Source proven; impact unmeasured | Metal texture uploads lack a discrete-GPU private-storage path |
| [AUDIT-0031](issues/AUDIT-0031-metal-combiner-diagnostics-ignore-controls.md) | Deferred | S4 | P3 | Source proven | Metal combiner diagnostics ignore OpenGL runtime controls |
| [AUDIT-0032](issues/AUDIT-0032-display-frame-cap-is-60hz-alias.md) | Fixed | S4 | P3 | Source and design-plan proven | Display frame cap is a 60 Hz alias without render interpolation |
| [AUDIT-0033](issues/AUDIT-0033-portmaster-save-directory-env-ignored.md) | Fixed | S3 | P1 | Runtime reproduced | PortMaster save directory export is ignored by the shipped target |
| [AUDIT-0034](issues/AUDIT-0034-app-shell-save-override-initialized-too-late.md) | Fixed | S3 | P1 | Runtime reproduced | App-shell save overrides arrive after the save directory is frozen |
| [AUDIT-0035](issues/AUDIT-0035-app-shell-discards-engine-exit-status.md) | Fixed | S3 | P1 | Fault injected | App shell converts engine boot failures into exit status zero |
| [AUDIT-0036](issues/AUDIT-0036-settings-ui-cannot-report-save-failure.md) | Deferred | S4 | P2 | Source proven | Settings UI cannot report failed or suppressed persistence |
| [AUDIT-0037](issues/AUDIT-0037-linux-packager-runs-unpinned-download.md) | Fixed | S1 | P1 | Source proven | Linux release packaging executes an unpinned mutable download |
| [AUDIT-0038](issues/AUDIT-0038-portmaster-launcher-masks-boot-failure.md) | Fixed | S3 | P2 | Source proven | PortMaster launcher masks game boot failures behind cleanup |
| [AUDIT-0039](issues/AUDIT-0039-app-preferences-save-is-nonatomic-and-silent.md) | Fixed | S4 | P2 | Source proven | App preferences save is non-atomic and silently fallible |
| [AUDIT-0040](issues/AUDIT-0040-gles-screenshot-reads-stale-back-buffer.md) | Open | S3 | P2 | Source proven | GLES screenshots read the stale back buffer |
| [AUDIT-0041](issues/AUDIT-0041-truncated-eeprom-prefix-remains-live.md) | Fixed | S3 | P1 | Source proven | Truncated EEPROM loads a partial prefix despite claiming blank state |
| [AUDIT-0042](issues/AUDIT-0042-eeprom-long-transfer-range-overflow.md) | Fixed | S3 | P2 | Source proven | EEPROM long transfers use overflow-prone range arithmetic |
| [AUDIT-0043](issues/AUDIT-0043-auto-screenshot-write-failure-exits-zero.md) | Fixed | S3 | P1 | Fault injected | Auto-screenshot write failure still exits successfully |
| [AUDIT-0044](issues/AUDIT-0044-update-thread-failure-never-completes.md) | Fixed | S4 | P3 | Source proven | Update thread creation failure never completes the UI state |
| [AUDIT-0045](issues/AUDIT-0045-rom-picker-accepts-engine-rejected-sizes.md) | Fixed | S3 | P1 | Source proven | ROM picker marks engine-rejected ROM sizes Ready to play |
| [AUDIT-0046](issues/AUDIT-0046-app-smoke-capture-failure-exits-zero.md) | Fixed | S4 | P2 | Fault injected | App smoke capture failure still exits successfully |
| [AUDIT-0047](issues/AUDIT-0047-diagnostics-export-misses-resolved-config.md) | Fixed | S4 | P2 | Source proven | Diagnostics export ignores the resolved engine config path |
| [AUDIT-0048](issues/AUDIT-0048-windows-diagnostics-leak-rom-path.md) | Fixed | S3 | P1 | Source proven | Windows diagnostics export leaks the full ROM path |
| [AUDIT-0049](issues/AUDIT-0049-binding-files-save-nonatomically.md) | Fixed | S4 | P2 | Source proven | Keyboard and gamepad binding files save non-atomically |
| [AUDIT-0050](issues/AUDIT-0050-duplicate-bindings-trigger-both-actions.md) | Fixed | S3 | P2 | Source proven | Duplicate bindings trigger both actions despite last-writer message |
| [AUDIT-0051](issues/AUDIT-0051-fixed-alternates-absent-from-conflict-model.md) | Fixed | S3 | P2 | Source proven | Fixed alternate controls are absent from conflict detection |
| [AUDIT-0052](issues/AUDIT-0052-release-assets-not-bound-to-verified-commit.md) | Fixed (attestation owner-gated) | S1 | P1 | Source proven | Release assets are not bound to the verified source commit |
| [AUDIT-0053](issues/AUDIT-0053-release-version-not-threaded-to-macos-linux.md) | Fixed | S3 | P1 | Source and built-artifact proven | macOS and Linux release builds do not receive the release version |
| [AUDIT-0054](issues/AUDIT-0054-invalid-savedir-accepted.md) | Fixed | S3 | P1 | Fault injected | Invalid explicit save directory is accepted as usable |
| [AUDIT-0055](issues/AUDIT-0055-env-override-erases-persisted-setting.md) | Fixed | S3 | P1 | Runtime reproduced | Transient environment override erases the persisted setting |
| [AUDIT-0056](issues/AUDIT-0056-unknown-config-preservation-is-lossy.md) | Fixed | S4 | P3 | Source proven | Unknown config preservation silently drops forward-compatible data |
| [AUDIT-0057](issues/AUDIT-0057-metal-shadow-bias-copies-gl-depth-units.md) | Deferred | S4 | P3 | Source and mechanism proven; impact unmeasured | Metal shadow bias copies OpenGL constants across depth formats |
| [AUDIT-0058](issues/AUDIT-0058-linux-release-succeeds-without-appimage.md) | Fixed | S3 | P1 | Source proven | Linux release job succeeds without producing an AppImage |
| [AUDIT-0059](issues/AUDIT-0059-packagers-allow-missing-sdl-runtime.md) | Fixed | S3 | P1 | Source proven | Linux and macOS packagers allow missing SDL runtime bundles |
| [AUDIT-0060](issues/AUDIT-0060-launcher-discards-interactive-cli-flags.md) | Open | S3 | P1 | Runtime and UI reproduced | Launcher discards interactive CLI launch flags |
| [AUDIT-0061](issues/AUDIT-0061-input-tape-recording-late-failure-exits-zero.md) | Fixed | S3 | P1 | Fault injected | Input-tape recording reports success after late output failure |
| [AUDIT-0062](issues/AUDIT-0062-input-tape-overwrites-fixture-in-place.md) | Fixed | S3 | P2 | Fault injected | Failed input-tape finalization destroys the previous fixture |
| [AUDIT-0063](issues/AUDIT-0063-input-tape-accepts-incompatible-metadata.md) | Fixed | S3 | P2 | Runtime reproduced | Input-tape playback accepts incompatible session metadata |
| [AUDIT-0064](issues/AUDIT-0064-invalid-input-tape-falls-back-to-live-input.md) | Fixed | S3 | P1 | Fault injected | Invalid playback tape falls back to ordinary input |
| [AUDIT-0065](issues/AUDIT-0065-input-tape-unverified-allocation.md) | Fixed | S3 | P2 | Source and mechanism proven | Tape reader allocates from an unverified header count |
| [AUDIT-0066](issues/AUDIT-0066-debug-dump-hardcodes-posix-tmp.md) | Fixed | S4 | P2 | Source proven | Guard debug dumps hard-code the POSIX /tmp directory |
| [AUDIT-0067](issues/AUDIT-0067-debug-dump-falsely-confirms-short-write.md) | Fixed | S4 | P2 | Fault injected | Guard debug dump falsely confirms a truncated file |
| [AUDIT-0068](issues/AUDIT-0068-audio-queue-failures-counted-as-accepted.md) | Fixed | S3 | P2 | Source and API-contract proven | Audio queue failures are counted as accepted output |
| [AUDIT-0069](issues/AUDIT-0069-audio-capture-falsely-reports-complete.md) | Fixed | S4 | P2 | Fault injected | PCM capture reports complete after a short write |
| [AUDIT-0070](issues/AUDIT-0070-gatekeeper-assessment-is-nonfatal.md) | Fixed | S3 | P1 | Source proven | Gatekeeper rejection is nonfatal to the signing pipeline |
| [AUDIT-0071](issues/AUDIT-0071-music-decompression-failure-ignored.md) | Fixed | S3 | P2 | Fault injected | Music track decompression failure is ignored before sequence parsing |
| [AUDIT-0072](issues/AUDIT-0072-music-seq-table-fixed-bound-overread.md) | Fixed | S3 | P2 | Fault injected | Music sequence table is read past its allocation with a fixed bound |
| [AUDIT-0073](issues/AUDIT-0073-native-mission-music-init-bypassed.md) | Deferred | S3 | P2 | Source proven | Native mission-music initializer is bypassed by a stale guard |

## Evidence Handling

ROM-derived screenshots, traces, and extracted data remain local and are not
committed. Reports retain the aggregate measurements, hashes, source anchors,
and reproduction parameters needed to repeat an observation with a legally
provided ROM. A source-only report must state when a natural gameplay trigger
has not yet been reproduced.
