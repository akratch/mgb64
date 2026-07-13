# Native-Port Audit: Prioritized Findings Report

Scope: the full native-port defect ledger produced by the Codex audit,
`docs/audit/issues/AUDIT-0001` through `AUDIT-0073` (73 findings). This report
ranks them into a recommended fix order and clusters them by theme. It is a
reading of the ledger metadata (severity, priority, evidence) plus the source
anchors in each report; the individual reports remain authoritative.

Internal document (docs/audit is export-ignored). No ROM-derived data appears
here or in the reports it summarizes.

## Ranking method

Each finding is ordered by three keys, most significant first:

1. **Priority** (the maintainer's intended repair order): P0 < P1 < P2 < P3.
2. **Severity** (impact): S1 < S2 < S3 < S4.
3. **Evidence strength** (confidence it is real and reproducible): runtime /
   fault-injected / test / A-B reproduced, then source-plus-mechanism, then
   source-only.

Lower sorts earlier = fix earlier. The tiers below follow that order.

## Distribution at a glance

| | P1 | P2 | P3 | Total |
| --- | --- | --- | --- | --- |
| S1 | 2 | 0 | 0 | 2 |
| S2 | 0 | 5 | 0 | 5 |
| S3 | 22 | 19 | 3 | 44 |
| S4 | 0 | 13 | 9 | 22 |
| **Total** | **24** | **37** | **12** | **73** |

Read-through: only **2** findings are top-severity (S1), and both are release
supply-chain integrity. There are **no S1/S2 defects marked P1** other than those
two, so the ledger has no "crash-on-every-boot" class open. The bulk (44) is S3
correctness/robustness, split roughly evenly between P1 (clear before a release
candidate) and P2 (correctness backlog). The 22 S4 items are polish.

## The single biggest risk: the release/CI pipeline

The largest and highest-severity cluster is not gameplay -- it is the machinery
that builds, certifies, and signs releases. Both S1s live here, plus a long P1
tail. If a release is cut before these are fixed, the release itself can be
wrong or unverifiable even when the game is fine:

- **AUDIT-0037 (S1)** Linux packaging runs an unpinned mutable download (supply
  chain).
- **AUDIT-0052 (S1)** Release assets are not bound to the verified source commit
  (provenance).
- **AUDIT-0053 / 0058 / 0059 / 0070 (P1/P2)** macOS/Linux builds miss the release
  version; a Linux job "succeeds" without producing an AppImage; packagers allow a
  missing SDL runtime; Gatekeeper rejection is nonfatal to signing.
- **AUDIT-0012 / 0019 / 0020 / 0035 / 0043 / 0046 (P1)** the gates that are
  supposed to catch all of the above certify nonzero exits as clean, hard-fail on
  host variance, hardcode a stale hash, or swallow failure into exit 0.

Fixing the gates (0012/0035/0043/0046) first is high-leverage: they are why other
pipeline defects can ship undetected.

---

## Tier 0 -- Ship blockers (fix first)

| Rank | ID | Sev/Pri | Evidence | Finding |
| --- | --- | --- | --- | --- |
| 1 | AUDIT-0037 | S1/P1 | Source proven | Linux release packaging executes an unpinned mutable download |
| 2 | AUDIT-0052 | S1/P1 | Source proven | Release assets are not bound to the verified source commit |

Both are release-integrity/supply-chain. Neither affects gameplay, but each makes
a published release untrustworthy. These gate any real publish.

## Tier 1 -- Clear before the next release candidate (P1)

In fix order. These are all S3 (except the two S1s above), and most are
runtime/fault-injected -- high confidence, and each undermines a release,
save, or automation contract.

**Release / CI gates that hide failure**
- AUDIT-0012 Sanitizer gate certifies nonzero process exits as clean
- AUDIT-0035 App shell converts engine boot failures into exit status zero
- AUDIT-0043 Auto-screenshot write failure still exits successfully
- AUDIT-0019 Performance gate hard-fails an unqualified host-specific baseline
- AUDIT-0020 Guard-fire regression lane hardcodes a stale opt-out hash
- AUDIT-0013 Fidelity ledger index omits nine authoritative records
- AUDIT-0053 macOS/Linux release builds do not receive the release version
- AUDIT-0058 Linux release job succeeds without producing an AppImage
- AUDIT-0059 Linux/macOS packagers allow missing SDL runtime bundles
- AUDIT-0070 Gatekeeper rejection is nonfatal to the signing pipeline

**Save / config durability**
- AUDIT-0033 PortMaster save directory export is ignored by the shipped target
- AUDIT-0034 App-shell save overrides arrive after the save directory is frozen
- AUDIT-0054 Invalid explicit save directory is accepted as usable
- AUDIT-0055 Transient environment override erases the persisted setting
- AUDIT-0041 Truncated EEPROM loads a partial prefix while claiming blank state

**Input / launcher correctness**
- AUDIT-0060 Launcher discards interactive CLI launch flags
- AUDIT-0061 Input-tape recording reports success after a late output failure
- AUDIT-0064 Invalid playback tape falls back to ordinary input

**Trust boundary**
- AUDIT-0045 ROM picker marks engine-rejected ROM sizes Ready to play
- AUDIT-0048 Windows diagnostics export leaks the full ROM path (privacy)
- AUDIT-0017 Four campaign routes still encode pre-FID-0117 root motion
- AUDIT-0018 Knife-impact fixture misses after the retail root-motion fix

## Tier 2 -- Correctness / robustness backlog (P2)

Grouped by theme; each is a real correctness or robustness fault, but narrower or
already contained.

**Malformed-input crashes and UB (engine/assets)**
- AUDIT-0001 OpenGL silently skips all scene-decor meshes (S2)
- AUDIT-0003 Screenshot series calls OpenGL under Metal and crashes (S2)
- AUDIT-0005 CLI accepts a wrong 12 MB ROM and crashes during boot (S2)
- AUDIT-0002 OOB recovery divides by zero on linkless STAN tiles (S2)
- AUDIT-0006 Supported-object destruction dereferences nullable STAN pointers (S2)
- AUDIT-0007 Texture lookup decode advances an uninitialized source pointer
- AUDIT-0027 PI DMA bounds checks wrap and permit out-of-bounds ROM reads
- AUDIT-0042 EEPROM long transfers use overflow-prone range arithmetic
- AUDIT-0071 Music track decompression failure is ignored before sequence parsing
- AUDIT-0072 Music sequence table is read past its allocation with a fixed bound
- AUDIT-0073 Native mission-music initializer is bypassed by a stale guard

**Input / tape / bindings**
- AUDIT-0062 Failed input-tape finalization destroys the previous fixture
- AUDIT-0063 Input-tape playback accepts incompatible session metadata
- AUDIT-0065 Tape reader allocates from an unverified header count
- AUDIT-0024 Rebindable system hotkeys use impractical raw numeric sliders
- AUDIT-0025 Menu-button rebinding can create a reserved-button double role
- AUDIT-0050 Duplicate bindings trigger both actions despite last-writer message
- AUDIT-0051 Fixed alternate controls are absent from conflict detection

**Audio / diagnostics I/O**
- AUDIT-0068 Audio queue failures are counted as accepted output
- AUDIT-0069 PCM capture reports complete after a short write
- AUDIT-0067 Guard debug dump falsely confirms a truncated file
- AUDIT-0066 Guard debug dumps hard-code the POSIX /tmp directory
- AUDIT-0028 Weapon equip and reload sounds diverge from the retail tables

**Save / config**
- AUDIT-0026 Staged string settings silently truncate paths at 63 bytes
- AUDIT-0011 Malformed numeric settings are silently accepted and coerced
- AUDIT-0036 Settings UI cannot report failed or suppressed persistence
- AUDIT-0039 App preferences save is non-atomic and silently fallible
- AUDIT-0049 Keyboard and gamepad binding files save non-atomically
- AUDIT-0056 Unknown config preservation silently drops forward-compatible data (P3)

**App shell / rendering / build hygiene**
- AUDIT-0038 PortMaster launcher masks game boot failures behind cleanup
- AUDIT-0022 Launcher mode overrides survive Return-to-Launcher re-exec
- AUDIT-0023 Implemented minimap objectives are hidden as having no effect
- AUDIT-0040 GLES screenshots read the stale back buffer
- AUDIT-0047 Diagnostics export ignores the resolved engine config path
- AUDIT-0010 Faithful presets retain the non-original FPS overlay
- AUDIT-0046 App smoke capture failure still exits successfully
- AUDIT-0015 Hashtable generator emits invalid output and exits zero on failure
- AUDIT-0014 Nuke cleanup quotes asset globs and leaves generated binaries

## Tier 3 -- Polish and latent (P3)

- AUDIT-0008 Vertical-only auto-aim uses uninitialized horizontal state (S3)
- AUDIT-0004 Multiplayer awards evaluate uninitialized inactive metrics (S3)
- AUDIT-0009 Low-32 pointer registry silently evicts live mappings (S3)
- AUDIT-0016 FPS-toggle help publishes the wrong SDL keycode for F10
- AUDIT-0057 Metal shadow bias copies OpenGL constants across depth formats
- AUDIT-0029 Metal half-resolution SSAO uses a non-depth-aware upsample
- AUDIT-0030 Metal texture uploads lack a discrete-GPU private-storage path
- AUDIT-0031 Metal combiner diagnostics ignore OpenGL runtime controls
- AUDIT-0021 Knife-impact help advertises the wrong default warp distance
- AUDIT-0044 Update thread creation failure never completes the UI state
- AUDIT-0032 Display frame cap is a 60 Hz alias without render interpolation

---

## The recent Codex batch (this session): AUDIT-0061 to AUDIT-0073

Thirteen findings, freshest and highest-confidence (all runtime/fault-injected or
directly source-proven). They cluster into three areas:

- **Input-tape robustness (0061-0065)**: late-failure false success (P1),
  fixture-destroying finalization, incompatible metadata accepted, invalid tape
  silently falling back to live input (P1), unverified allocation.
- **Diagnostics / audio I/O integrity (0066-0069)**: hardcoded /tmp, truncated
  files reported complete, audio queue/PCM failures counted as success.
- **macOS signing (0070)** and **native music subsystem (0071-0073)**: Gatekeeper
  rejection nonfatal; decompression failure ignored into the sequence parser
  (ASan crash); sequence-table over-read (ASan crash); the stale native
  mission-music initializer bypass.

0061, 0064, and 0070 are the P1 members of this batch; 0071-0073 are the
music-subsystem crashes/fidelity defect rescued from the halted run.

## Recommended fix sequence

1. **Trust the gates** -- fix the CI/exit-status swallowers first (0012, 0035,
   0043, 0046, 0015). Until these are honest, you cannot tell whether any other
   pipeline fix actually took.
2. **Release integrity** -- 0037, 0052, then 0053/0058/0059/0070.
3. **Save durability** -- 0033/0034/0054/0055/0041 (a player losing progress is
   worse than most gameplay bugs).
4. **Malformed-input crashes** -- 0005/0002/0006/0027/0042/0071/0072 (harden the
   ROM/asset trust boundary; several are one guarded branch each).
5. **The rest of P1**, then the P2 correctness backlog by theme, then P3 polish.

## Full ranked table

Ordered by the ranking method above. Within a tier (same Severity and
Priority) the order is not significant -- only the tier boundaries carry
meaning. Rank 65 in the earlier draft's duplicate has been removed; this
table is the authoritative 73-row list.

| # | ID | Sev | Pri | Evidence | Title |
| --- | --- | --- | --- | --- | --- |
| 1 | AUDIT-0037 | S1 | P1 | Source proven | Linux release packaging executes an unpinned mutable download |
| 2 | AUDIT-0052 | S1 | P1 | Source proven | Release assets are not bound to the verified source commit |
| 3 | AUDIT-0017 | S3 | P1 | A/B reproduced | Four campaign routes still encode pre-FID-0117 root motion |
| 4 | AUDIT-0018 | S3 | P1 | A/B reproduced | Knife-impact fixture misses after the retail root-motion fix |
| 5 | AUDIT-0035 | S3 | P1 | Fault injected | App shell converts engine boot failures into exit status zero |
| 6 | AUDIT-0012 | S3 | P1 | Fault injected | Sanitizer gate certifies nonzero process exits as clean |
| 7 | AUDIT-0043 | S3 | P1 | Fault injected | Auto-screenshot write failure still exits successfully |
| 8 | AUDIT-0054 | S3 | P1 | Fault injected | Invalid explicit save directory is accepted as usable |
| 9 | AUDIT-0061 | S3 | P1 | Fault injected | Input-tape recording reports success after late output failure |
| 10 | AUDIT-0064 | S3 | P1 | Fault injected | Invalid playback tape falls back to ordinary input |
| 11 | AUDIT-0060 | S3 | P1 | Runtime and UI reproduced | Launcher discards interactive CLI launch flags |
| 12 | AUDIT-0034 | S3 | P1 | Runtime reproduced | App-shell save overrides arrive after the save directory is frozen |
| 13 | AUDIT-0033 | S3 | P1 | Runtime reproduced | PortMaster save directory export is ignored by the shipped target |
| 14 | AUDIT-0055 | S3 | P1 | Runtime reproduced | Transient environment override erases the persisted setting |
| 15 | AUDIT-0053 | S3 | P1 | Source and built-artifact proven | macOS and Linux release builds do not receive the release version |
| 16 | AUDIT-0013 | S3 | P1 | Test reproduced | Fidelity ledger index omits nine authoritative records |
| 17 | AUDIT-0019 | S3 | P1 | Test reproduced | Performance gate hard-fails an unqualified host-specific baseline |
| 18 | AUDIT-0020 | S3 | P1 | Test reproduced | Guard-fire regression lane hardcodes a stale opt-out hash |
| 19 | AUDIT-0041 | S3 | P1 | Source proven | Truncated EEPROM loads a partial prefix despite claiming blank state |
| 20 | AUDIT-0045 | S3 | P1 | Source proven | ROM picker marks engine-rejected ROM sizes Ready to play |
| 21 | AUDIT-0058 | S3 | P1 | Source proven | Linux release job succeeds without producing an AppImage |
| 22 | AUDIT-0059 | S3 | P1 | Source proven | Linux and macOS packagers allow missing SDL runtime bundles |
| 23 | AUDIT-0070 | S3 | P1 | Source proven | Gatekeeper rejection is nonfatal to the signing pipeline |
| 24 | AUDIT-0048 | S3 | P1 | Source proven | Windows diagnostics export leaks the full ROM path |
| 25 | AUDIT-0005 | S2 | P2 | Runtime reproduced | CLI accepts a wrong 12 MB ROM and crashes during boot |
| 26 | AUDIT-0001 | S2 | P2 | Runtime reproduced | OpenGL silently skips all scene-decor meshes |
| 27 | AUDIT-0003 | S2 | P2 | Runtime reproduced | Screenshot series calls OpenGL under Metal and crashes |
| 28 | AUDIT-0006 | S2 | P2 | Analyzer and source proven | Supported-object destruction dereferences nullable STAN pointers |
| 29 | AUDIT-0002 | S2 | P2 | Shipped data reachable | OOB recovery divides by zero on linkless STAN tiles |
| 30 | AUDIT-0071 | S3 | P2 | Fault injected | Music track decompression failure is ignored before sequence parsing |
| 31 | AUDIT-0072 | S3 | P2 | Fault injected | Music sequence table is read past its allocation with a fixed bound |
| 32 | AUDIT-0015 | S3 | P2 | Fault injected | Hashtable generator emits invalid output and exits zero on failure |
| 33 | AUDIT-0062 | S3 | P2 | Fault injected | Failed input-tape finalization destroys the previous fixture |
| 34 | AUDIT-0063 | S3 | P2 | Runtime reproduced | Input-tape playback accepts incompatible session metadata |
| 35 | AUDIT-0068 | S3 | P2 | Source and API-contract proven | Audio queue failures are counted as accepted output |
| 36 | AUDIT-0007 | S3 | P2 | Shipped data reachable | Texture lookup decode advances an uninitialized source pointer |
| 37 | AUDIT-0065 | S3 | P2 | Source and mechanism proven | Tape reader allocates from an unverified header count |
| 38 | AUDIT-0024 | S3 | P2 | Source and UI proven | Rebindable system hotkeys use impractical raw numeric sliders |
| 39 | AUDIT-0022 | S3 | P2 | Source proven | Launcher mode overrides survive Return-to-Launcher re-exec |
| 40 | AUDIT-0038 | S3 | P2 | Source proven | PortMaster launcher masks game boot failures behind cleanup |
| 41 | AUDIT-0027 | S3 | P2 | Source proven | PI DMA bounds checks wrap and permit out-of-bounds ROM reads |
| 42 | AUDIT-0042 | S3 | P2 | Source proven | EEPROM long transfers use overflow-prone range arithmetic |
| 43 | AUDIT-0073 | S3 | P2 | Source proven | Native mission-music initializer is bypassed by a stale guard |
| 44 | AUDIT-0051 | S3 | P2 | Source proven | Fixed alternate controls are absent from conflict detection |
| 45 | AUDIT-0040 | S3 | P2 | Source proven | GLES screenshots read the stale back buffer |
| 46 | AUDIT-0025 | S3 | P2 | Source proven | Menu-button rebinding can create a reserved-button double role |
| 47 | AUDIT-0026 | S3 | P2 | Source proven | Staged string settings silently truncate paths at 63 bytes |
| 48 | AUDIT-0050 | S3 | P2 | Source proven | Duplicate bindings trigger both actions despite last-writer message |
| 49 | AUDIT-0046 | S4 | P2 | Fault injected | App smoke capture failure still exits successfully |
| 50 | AUDIT-0067 | S4 | P2 | Fault injected | Guard debug dump falsely confirms a truncated file |
| 51 | AUDIT-0069 | S4 | P2 | Fault injected | PCM capture reports complete after a short write |
| 52 | AUDIT-0023 | S4 | P2 | Runtime and test proven | Implemented minimap objectives are hidden as having no effect |
| 53 | AUDIT-0014 | S4 | P2 | Runtime reproduced | Nuke cleanup quotes asset globs and leaves generated binaries |
| 54 | AUDIT-0011 | S4 | P2 | Runtime reproduced | Malformed numeric settings are silently accepted and coerced |
| 55 | AUDIT-0028 | S4 | P2 | Source and retail-table proven | Weapon equip and reload sounds diverge from the retail tables |
| 56 | AUDIT-0010 | S4 | P2 | Source proven | Faithful presets retain the non-original FPS overlay |
| 57 | AUDIT-0047 | S4 | P2 | Source proven | Diagnostics export ignores the resolved engine config path |
| 58 | AUDIT-0066 | S4 | P2 | Source proven | Guard debug dumps hard-code the POSIX /tmp directory |
| 59 | AUDIT-0036 | S4 | P2 | Source proven | Settings UI cannot report failed or suppressed persistence |
| 60 | AUDIT-0039 | S4 | P2 | Source proven | App preferences save is non-atomic and silently fallible |
| 61 | AUDIT-0049 | S4 | P2 | Source proven | Keyboard and gamepad binding files save non-atomically |
| 62 | AUDIT-0008 | S3 | P3 | Analyzer and source proven | Vertical-only auto-aim uses uninitialized horizontal state |
| 63 | AUDIT-0004 | S3 | P3 | Analyzer and source proven | Multiplayer awards evaluate uninitialized inactive metrics |
| 64 | AUDIT-0009 | S3 | P3 | Source proven | Low-32 pointer registry silently evicts live mappings |
| 65 | AUDIT-0016 | S4 | P3 | Runtime reproduced | FPS-toggle help publishes the wrong SDL keycode for F10 |
| 66 | AUDIT-0057 | S4 | P3 | Source and mechanism proven; impact unmeasured | Metal shadow bias copies OpenGL constants across depth formats |
| 67 | AUDIT-0029 | S4 | P3 | Source and mechanism proven | Metal half-resolution SSAO uses a non-depth-aware upsample |
| 68 | AUDIT-0030 | S4 | P3 | Source proven; impact unmeasured | Metal texture uploads lack a discrete-GPU private-storage path |
| 69 | AUDIT-0031 | S4 | P3 | Source proven | Metal combiner diagnostics ignore OpenGL runtime controls |
| 70 | AUDIT-0021 | S4 | P3 | Source proven | Knife-impact help advertises the wrong default warp distance |
| 71 | AUDIT-0044 | S4 | P3 | Source proven | Update thread creation failure never completes the UI state |
| 72 | AUDIT-0056 | S4 | P3 | Source proven | Unknown config preservation silently drops forward-compatible data |
| 73 | AUDIT-0032 | S4 | P3 | Source and design-plan proven | Display frame cap is a 60 Hz alias without render interpolation |
