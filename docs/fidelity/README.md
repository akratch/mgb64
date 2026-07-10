# Fidelity program

The **Fidelity Flywheel** is a closed loop that finds where the port diverges
from the real Nintendo 64 game, files each divergence as evidence-backed
finding, and gates every fix behind a test that fails if the fix is reverted.
Its purpose: drive the port toward byte-faithful behavior without regressing,
and never claim a fix that isn't machine-proven.

The master plan is [`../design/FAITHFULNESS_S_TIER_PLAN.md`](../design/FAITHFULNESS_S_TIER_PLAN.md).
The binding rules are [`CHARTER.md`](CHARTER.md) — read that before touching a
finding or landing a fix.

## The loop

```
 Oracles ─────────────► Sense lanes ─────► Ledger ─────► Act / Verify
 (ground truth)         (find divergence)  (evidence)    (fix + ratchet)
 · retail ASM (ROM)     S1 trace sweep     FID-NNNN.json  fix behind A/B flag
 · ares emulator        S2 pixel sweep     one finding    → oracle-verify
 · matched decomp       S4 soak / fuzz     per file,      → regression lane
                        S5 ASM audit       transitioned   → verify ratchet
                        S6 coverage critic with artifacts  only tightens
```

**Authority order is absolute:** retail ASM > ares oracle > decomp C. The
`#else` reference bodies in the source *lie* — never transcribe them; derive
from the assembly.

## Where things live

| What | Path |
|------|------|
| Binding rules | [`CHARTER.md`](CHARTER.md) |
| Findings (source of truth) | `ledger/FID-NNNN.json` |
| Findings index (generated) | [`LEDGER.md`](LEDGER.md) — do not hand-edit |
| ASM-audit queue + method | [`ASM_AUDIT.md`](ASM_AUDIT.md) |
| Combat/floor oracle fields | [`combat_oracle_fields.md`](combat_oracle_fields.md) |
| Sim-hash coverage | [`HASH_COVERAGE_AUDIT.md`](HASH_COVERAGE_AUDIT.md) |
| Documented approximations | [`APPROXIMATIONS.md`](APPROXIMATIONS.md) |
| Per-fix derivations | `derivations/FID-*.md` |
| Determinism input tapes | `../../baselines/tapes/*.ge7tape` |
| Tooling | `../../tools/fidelity/` |

## The ledger

Every divergence is one JSON file under `ledger/`, moved through a fixed
lifecycle by `tools/fidelity/ledger.py` — never hand-edit the JSON's status:

```
discovered → triaged → root-caused → fix-in-progress → landed → verified
                                    ↘ documented / refuted / waived
```

A transition requires a machine artifact (a trace diff, a pixel diff, an ASM
citation, a passing gate log). No evidence, no transition.

```sh
tools/fidelity/ledger.py stats                 # counts by status
tools/fidelity/ledger.py list --actionable     # what's ready to work
tools/fidelity/ledger.py new --title ... --class ... --surface ... \
    --priority ... --evidence ...              # file a finding
tools/fidelity/ledger.py transition FID-0056 --to landed --evidence ...
tools/fidelity/ledger.py render                # regenerate LEDGER.md
tools/fidelity/ledger.py validate              # schema + transition integrity
```

`class` ∈ {port-defect, parity-divergence, n64-quirk, instrumentation-gap,
coverage-gap}. `surface` ∈ {sim, renderer, audio, converter, infra}.

## A/B flag polarity (charter rule)

Every behavior change ships behind a flag so it can be A/B'd against the old
behavior, and so byte-identity is provable:

- **Port-defect / parity fix** → default **ON** (faithful), with a
  `GE007_NO_<FIX>` / `Input.<X>=0` opt-out that restores the legacy behavior.
  Byte-identity must hold under the opt-out.
- **N64-quirk mitigation** → default **OFF** (opt-in).

## Verifying

The sim-state hash is **floating-point-optimization sensitive**, so all
baselines are recorded under a **Release** build (`CMakeLists.txt` defaults
`CMAKE_BUILD_TYPE=Release`). A Debug/stale binary reddens the sim-hash lanes
with a misleading "sim diverged" — rebuild Release before trusting a red lane.

```sh
# Build with the fidelity lanes, then run them (mute audio for concurrent work):
cmake -B build -DCMAKE_BUILD_TYPE=Release -DPORT_VALIDATION_TESTS=ON
cmake --build build -j8
SDL_AUDIODRIVER=dummy GE007_MUTE=1 ctest --test-dir build

tools/fidelity/verify_all.sh          # the ratchet — reads the manifest
```

Each landed fix owns a permanent **regression lane** (a ctest) that fails if
the fix is reverted — that's the ratchet, and it only ever tightens. Examples:
`fire_rate_authentic`, `weapon_bullet_type`, `projectile_endpoint_clamp`,
`combat_oracle_contract`, `port_combat_route_capture_smoke`, `struct_layout`,
`fidelity_ledger_valid`, `fidelity_ledger_index_current`.

## Sense lanes (finding divergences)

| Lane | Tool | Finds |
|------|------|-------|
| S1 trace | `sense_trace_sweep.sh` | state-trace divergence vs the oracle |
| S2 pixel | `sense_pixel_sweep.sh`, `pixel_diff.py` | visible per-stage artifacts |
| S4 soak/fuzz | `sense_soak.sh`, `uncap_purity_gate.sh` | non-determinism, crashes |
| S5 ASM audit | `asm_audit.py` | ASM-vs-C behavior divergence (388 bodies) |
| S6 coverage | `sense_coverage.py` | gaps in what the loop has actually checked |
| combat oracle | `combat_route_capture_smoke.sh`, `compare_combat_trace.py` | guard/floor/combat-field divergence vs ares |

## Adding a finding, end to end

1. A sense lane (or a code read against the ASM) surfaces a divergence.
2. `ledger.py new …` — file it with the reproducing artifact.
3. Root-cause against the **retail ASM** (cite instructions); write a
   `derivations/FID-*.md` if the mechanism is non-trivial.
4. Fix behind an A/B flag with the correct polarity; keep byte-identity under
   the opt-out; factor the logic into a pure, unit-testable helper where you
   can.
5. Add a regression lane (ctest) and **prove it fails on revert**.
6. Verify against the oracle; `ledger.py transition … --to landed` with the
   evidence; regenerate `LEDGER.md`.
