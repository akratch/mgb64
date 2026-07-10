# FID-0066 — NPC full-auto cadence unscaled (FID-0056 port-half)

- **Class:** port-defect · **Surface:** sim · **Priority:** P1
- **Flag:** `Input.FireRateAuthentic` (default **ON**; opt-out `GE007_FIRE_RATE_AUTHENTIC=0`),
  `Input.FireRateN64FrameCost` (default 3) — the **same** flag/pair as FID-0056.
- **Plan item:** P1a / decision **D2** (docs/design/FIDELITY_REVIEW_AND_PLAN_2026-07-10.md).

## Divergence statement

**Retail (anchor: `chrlvFireWeaponRelated`, VRAM `0x7F02D734`, matched decomp C body
at `src/game/chrlv.c`).** A guard's full-auto fire is gated on a per-AI-tick
counter:

```c
self->firecount[hand]++;                                  // advance once per AI tick
...
else if (((s32) self->firecount[hand] % auto_rate) == 0)  // fire on every auto_rate-th step
    sp268 = 1;                                            // (auto_rate = item AutomaticFiringRate)
```

`chrlvFireWeaponRelated` is called once per game-loop iteration
(`chrTickBeams` → `chrlvTriggerFireWeapon`, `src/game/chr.c:5253`, gated on
`CHRHIDDEN_FIRE_WEAPON_*`). On the N64 the game loop ran at the **rendered frame
rate** (~15–30 fps in combat), so `firecount` advanced ~15–30 Hz and the gate
fired every `auto_rate` rendered frames. This is the **identical mechanism**
FID-0056 documented for the player's `field_88C % fire_rate` gate (`gun.c`).

**Port (`src/game/chrlv.c:8350` / `:8357`, pre-fix).** The port runs the whole
game loop — player gun tick *and* every guard AI tick — at a **locked 60 Hz**
(`g_ClockTimer == 1`). `firecount` therefore advances at 60 Hz and the gate
fires ~3× faster than the N64's ~20 fps combat cadence.

**Player-visible effect.** FID-0056 tick-scaled *only* the player's gate (behind
`Input.FireRateAuthentic`, default ON). With that fix shipping, the **player
fires at the true N64 cadence while guards still fire ~3× it** — a
player-vs-guard balance skew that did **not** exist before the player fix. D2
(locked): fix symmetrically — retail gated *both* on the same per-rendered-frame
counter, so "enemies fire slower too" is the faithful outcome.

**Repro.** Replay the committed `dam_forward_30s` tape on the Dam under the
determinism envelope; guard chr=6 (KF7 Soviet, item=8, `AutomaticFiringRate=3`)
sustains full-auto at Bond. Count gate-fires with `GE007_TRACE_GUARD_AUTOFIRE=1`.

## ASM anchor & why divisor-only is the faithful guard mirror

`bondwalkItemGetAutomaticFiringRate` (`gun.c:1954`) returns the item's `s8`
`AutomaticFiringRate`: **< 0 ⇒ single-shot always-fire** (takes the first branch,
never reaches the modulo gate); **≥ 0 ⇒ automatic** (the modulo gate). `firecount`
is `u8 firecount[2]` (`bondtypes.h:2380`), reset to 0 on state entry
(`chr.c:3210`).

The player counter (`gun.c:17939-17945`) is advanced **inside `if (g_ClockTimer
> 0)`** and paired with `field_890 += g_ClockTimer`; FID-0056 tick-scaled that
advance (`fireRateCounterAdvance`) *and* the divisor (`fireRateEffectiveAutoRate`).
The guard counter (`chrlv.c:8350`) is advanced **unconditionally** (no
`g_ClockTimer` gate) and is paired with a **hardcoded `-1` LOS-retry decrement**
(`chrlv.c` `firecount[hand]--` when `stanTestLineUnobstructed` fails).

The symmetric fix D2 requires is the **divisor** scaling — that is what makes
"enemies fire slower". Applying the counter-*advance* scaling to the guard would
be **less** faithful, not more: it would (a) drop the counter on `g_ClockTimer==0`
frames where retail's ungated `++` still advances. So the fix keeps retail's
exact `firecount[hand]++` and scales **only** the gate divisor via the shared
`fireRateEffectiveAutoRate` helper. At locked 60 Hz this reproduces the N64
cadence exactly; under flag-OFF it is byte-identical. (This is a genuine
retail-semantics difference from the player path — not the "same frame-counter
model" plan assumption breaking, but a narrowing of *which half* of the
FID-0056 mechanism applies. The core assumption — guard uses the same per-frame
`firecount % rate` gate — holds.)

Review amendments (2026-07-11, independent ROM disassembly of `0x7F02D734`
confirmed the ungated `++`; see the P1a review for the full derivation):

- The original supporting argument "(b) desync from the unscaled `-1` LOS-retry
  decrement" was wrong-direction and is withdrawn: counter-gating would in fact
  MATCH retail's retry pacing more closely. Argument (a) and the shot-pattern
  equivalence (shots land at wall ticks {9,18,27,…} exactly where retail-at-20fps
  lands them, identical first-shot phase and ×2 sub-modulo alternation) are what
  carry the divisor-only decision.
- Two bounded second-order divergences of the accepted constant `frame_cost=3`
  model, documented rather than re-mechanized (both below its noise floor):
  (1) the `u8 firecount` wrap glitch recurs every ~4.27 s at 60 Hz vs retail's
  ~12.8 s at ~20 fps; (2) LOS-retry polling happens at 60 Hz vs retail ~20 Hz,
  so fire can resume up to ~33 ms earlier after an obstruction clears.

The `< 0` always-fire branch and the `|| ITEM_LASER` sp264 sub-flag keep exact
semantics (the raw `auto_rate` feeds the sign test; a non-positive rate passes
through `fireRateEffectiveAutoRate` untouched).

## Fix

`src/game/chrlv.c` `chrlvFireWeaponRelated`: cache `auto_rate =
bondwalkItemGetAutomaticFiringRate(...)` (pure lookup, previously called 4×);
compute `eff_rate = fireRateEffectiveAutoRate(auto_rate, g_pcFireRateAuthentic,
g_pcFireRateN64FrameCost)`; use `eff_rate` in the `% eff_rate` and `% (eff_rate *
2)` gates. Keeps `firecount[hand]++` verbatim.

## Both-sides evidence (dam_forward_30s, Dam, guard chr=6, KF7 rate=3)

Determinism envelope; measured 2026-07-10 on the canonical Release build.

| Measurement | fix-OFF (`GE007_FIRE_RATE_AUTHENTIC=0`, legacy) | fix-ON (default, authentic) |
|---|---|---|
| `eff_rate` (gate divisor) | 3 | 9 |
| firecount gate sequence | 3, 6, 9, 12, … (step 3) | 9, 18, 27, 36, … (step 9) |
| **within-burst inter-shot interval** | **3 frames** (167×) | **9 frames** (41×) → **exactly 3.0×** |
| gate-passes over the run (incl. LOS-retry stutter) | 219 | 49 |
| sim-state hash | `95944e2282a48178` (== recorded baseline) | `11f0b8efb41593a3` (new default) |

The **within-burst inter-shot interval 3 → 9 frames = 3.0×** is the authoritative
cadence proof (matches the player's `frame_cost=3` scale). The aggregate gate-pass
count (219 → 49, ~4.5×) is inflated by the retail **LOS-retry stutter** (firecount
stuck at a fire-multiple while `stanTestLineUnobstructed` fails → the unscaled `-1`
decrement re-hits the gate every frame; `delta=1` entries) — same behavior both
flags; fewer gate-passes ON means fewer stutter episodes too.

**Player cadence unchanged:** the diff touches only `chrlv.c` (guard path); `gun.c`
(player) is byte-untouched. The `dam_combat_guard6` combat route (which fires the
player's PP7) replays to **`3c8939968e0eb50e` unchanged** under both flag states,
and the `fire_rate_authentic` player unit lane stays green.

**Byte-identity opt-out:** `GE007_FIRE_RATE_AUTHENTIC=0` restores
`95944e2282a48178` for `dam_forward_30s` and `3c8939968e0eb50e` for
`dam_combat_guard6` — the pre-fix baselines, exactly.

## Ratchet

- **Regression lane:** `tools/fidelity/guard_fire_rate_symmetry_smoke.sh` (tier-3,
  ROM-gated) — replays `dam_forward_30s`, asserts (a) the default within-burst
  interval is `frame_cost`× the opt-out interval (the cadence scaling), and (b)
  the opt-out sim-hash is byte-identical to the recorded pre-fix baseline. Reddens
  if the `chrlv.c` divisor scaling is reverted (ON interval collapses to the OFF
  interval → ratio 1×).
- **Baseline re-record:** `baselines/tapes/dam_forward_30s.expected.json`
  `95944e2282a48178 → 11f0b8efb41593a3` (default now = faithful guard cadence),
  same-input tape; opt-out restores the old hash.
- Unit: `tests/test_fire_rate_authentic.c` gains guard-context assertions for the
  shared `fireRateEffectiveAutoRate` divisor (KF7 rate 3 → 9).
