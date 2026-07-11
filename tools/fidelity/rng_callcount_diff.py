#!/usr/bin/env python3
"""Per-sim-tick randomGetNext() CALL-COUNT differential between a native capture
and an ares/stock capture of the same route (FID-0063).

The mgb64 PRNG (``src/random.c`` ``randomGetNext``, bit-faithful to the retail
``GLOBAL_ASM``) is a deterministic step function of the 64-bit seed, and after a
single step the state collapses into 33 bits (the update is a rotate of the low
33 bits XORed with a 32-bit term). Both trace emitters already record the FULL
64-bit seed once per record (native ``rng.seed`` + exact ``rng.call_count``;
ares ``rng.seed`` via ``readU64(0x80024460)``), so the number of randomGetNext()
calls between two consecutive samples equals the number of PRNG steps from
seed(t) to seed(t+1) — recoverable by stepping the generator forward (bounded
walk; the whole trace is one chain, so total work is O(total draws)).

Method / sampling semantics (validated 2026-07-10, see
docs/fidelity/derivations/FID-0063-rng-phase.md):

* native: ``portTraceFrame()`` runs at END of each game frame (after the sim
  tick, ``platform_sdl.c``), 1 record per frame -> seed/call_count are exact
  end-of-tick values.
* ares/stock: the tracer hooks the VI refresh (~3 samples per game tick at
  speedframes 3), asynchronous to the game frame boundary. We take the LAST
  record per ``move.global`` tick (the sample closest to the tick end). Per-tick
  attribution can therefore smear by +-1 tick when the sim burst straddles the
  last VI sample; CUMULATIVE counts between any two samples are exact.
* alignment: motion-onset anchor + relative sim-tick, exactly the FID-0062
  ``--align tick`` rule (reused from tools/compare_combat_trace.py).

Output: per-tick call counts both sides, per-tick delta, cumulative delta,
first divergent tick, and validation blocks (native chain-walk vs the exact
call_count counter; stock chain continuity; boot-seed check).

Usage:
  tools/fidelity/rng_callcount_diff.py \
      --native /tmp/cap/native_dam_combat_guard6.jsonl \
      --stock  /tmp/cap/stock_dam_combat_guard6.jsonl \
      --json-out /tmp/cap/rng_callcount_diff.json
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.compare_combat_trace import (  # noqa: E402
    first_moving_index,
    load_trace,
    parse_int,
)

MASK64 = (1 << 64) - 1

# g_randomSeed compile-time boot value (src/random.c:30); the first stock record
# samples it before the game's first randomSetSeed/randomGetNext.
BOOT_SEED = 0xAB8D9F7781280783


def prng_step(seed: int) -> int:
    """One randomGetNext() state update (src/random.c:279, u64 semantics)."""
    seed &= MASK64
    t = ((seed << 63) & MASK64) >> 31
    t |= ((seed << 31) & MASK64) >> 32
    t ^= ((seed << 44) & MASK64) >> 32
    return (t ^ ((t >> 20) & 0xFFF)) & MASK64


def prng_value(seed: int) -> int:
    """The u32 randomGetNext() returns when the post-step state is ``seed``."""
    return seed & 0xFFFFFFFF


def steps_between(seed_a: int, seed_b: int, max_steps: int) -> int | None:
    """Number of PRNG steps from state seed_a to state seed_b.

    Returns 0 if they are already equal, None if seed_b is not reached within
    ``max_steps`` (reseed via randomSetSeed, or corrupt sample).
    """
    if seed_a == seed_b:
        return 0
    s = seed_a
    for k in range(1, max_steps + 1):
        s = prng_step(s)
        if s == seed_b:
            return k
    return None


def parse_seed(record: dict[str, Any]) -> int | None:
    rng = record.get("rng")
    if not isinstance(rng, dict):
        return None
    return parse_int(rng.get("seed"))


def parse_call_count(record: dict[str, Any]) -> int | None:
    rng = record.get("rng")
    if not isinstance(rng, dict):
        return None
    return parse_int(rng.get("call_count"))


def rel_tick_samples(
    records: list[dict[str, Any]], start: int, onset_global: int
) -> list[dict[str, Any]]:
    """One sample per relative sim-tick: the LAST record with each move.global.

    Unlike the comparator's canonical_by_tick (which needs a roster-bearing
    record for guard-field comparison), the rng block is present on EVERY
    record, so the chronologically last sample per tick is the one closest to
    the tick boundary — the right choice for seed sampling.

    SEGMENT GATE: g_GlobalTimer resets to 0 on mission (re)start — a stock
    capture can contain several gameplay segments (e.g. dam_combat_guard6:
    Bond dies to the engaged guards late in the route and the game resets,
    f=4802 in the reference capture). A later segment re-visits the same
    move.global values with unrelated sim state, so last-wins bucketing across
    segments would silently mix two different runs (the comparator dodges this
    only because the post-reset records carry empty rosters). We therefore stop
    at the first global DROP after the onset record: only the contiguous
    non-decreasing-global segment containing the onset is sampled.
    """
    buckets: dict[int, dict[str, Any]] = {}
    prev_g: int | None = None
    for record in records[start:]:
        move = record.get("move")
        if not isinstance(move, dict):
            continue
        g = parse_int(move.get("global"))
        if g is None:
            continue
        if prev_g is not None and g < prev_g:
            break  # segment boundary (g_GlobalTimer reset): stop sampling
        prev_g = g
        if parse_seed(record) is None:
            continue
        buckets[g - onset_global] = record  # later records overwrite: last wins
    out = []
    for rel in sorted(buckets):
        r = buckets[rel]
        out.append(
            {
                "rel_tick": rel,
                "f": r.get("f"),
                "global": parse_int(r.get("move", {}).get("global")),
                "seed": parse_seed(r),
                "call_count": parse_call_count(r),
            }
        )
    return out


def chain_walk(samples: list[dict[str, Any]], max_steps: int) -> dict[str, Any]:
    """Walk the PRNG chain across ordered per-tick samples.

    Returns cumulative step counts per tick (cum[0] anchored at 0 on the first
    sample) plus discontinuities (ticks whose seed was not reachable within
    max_steps from the previous sample — reseed or corrupt; the chain restarts
    there and the cumulative offset is carried, with the gap counted as 0).
    """
    cums: dict[int, int] = {}
    discontinuities: list[dict[str, Any]] = []
    if not samples:
        return {"cum": cums, "discontinuities": discontinuities}
    cum = 0
    cums[samples[0]["rel_tick"]] = 0
    prev_seed = samples[0]["seed"]
    for s in samples[1:]:
        k = steps_between(prev_seed, s["seed"], max_steps)
        if k is None:
            discontinuities.append(
                {
                    "rel_tick": s["rel_tick"],
                    "from_seed": f"0x{prev_seed:016X}",
                    "to_seed": f"0x{s['seed']:016X}",
                    "max_steps": max_steps,
                }
            )
            k = 0  # restart chain; cumulative carries through with a 0 gap
        cum += k
        cums[s["rel_tick"]] = cum
        prev_seed = s["seed"]
    return {"cum": cums, "discontinuities": discontinuities}


def native_lock_frame(
    records: list[dict[str, Any]], lock_seed: int, scan_steps: int
) -> dict[str, Any] | None:
    """First-locked-frame draw count on the NATIVE side.

    The native lock (GE007_AUTO_RNG_SEED_SCRIPT) is applied at an input-poll
    boundary, so no native record ever SAMPLES the lock seed itself; the first
    record whose end-of-frame seed is chain-reachable from the lock within
    ``scan_steps`` is the frame that consumed the lock, and the step count is
    exactly that frame's randomGetNext() draws (native records are 1/frame,
    exact end-of-tick).
    """
    for record in records:
        seed = parse_seed(record)
        if seed is None:
            continue
        k = steps_between(lock_seed, seed, scan_steps)
        # k == 0 is a LEGITIMATE answer, not "not yet reached": a paused/menu
        # frame that consumes the lock without drawing at all still samples
        # the lock seed exactly. `k is not None` (not the old falsy `k > 0`)
        # accepts that zero-draw frame instead of skipping past it and
        # misattributing a LATER frame's cumulative count as the lock frame's
        # (unreachable on gameplay routes, where shuffle_player_ids guarantees
        # >=3 draws/frame, but wrong on menu/paused routes). Tie-break:
        # iteration is in record order, so the FIRST reachable record (lowest
        # chain position) always wins -- there is no later record to prefer.
        if k is not None:
            return {
                "first_locked_frame_calls": k,
                "f": record.get("f"),
                "global": parse_int(record.get("move", {}).get("global")),
                "call_count": parse_call_count(record),
            }
    return None


def stock_lock_frame(
    records: list[dict[str, Any]],
    lock_seed: int,
    max_steps: int,
    frame_buckets: int,
) -> dict[str, Any] | None:
    """Post-lock per-frame-bucket cumulative draw counts on the STOCK side.

    Anchors at the first record whose sampled seed EQUALS the lock (the ares
    seed-script application sample), then buckets last-wins per ``move.global``
    within the contiguous non-decreasing-global segment (same segment gate as
    rel_tick_samples) and chain-walks the bucket seeds from the lock.

    CAVEAT (FID-0063 round 2, derivation doc section 9.1): the ares tracer reads
    g_randomSeed from RDRAM, which lags the guest CPU's dcache by a few draws
    until a writeback lands, so per-bucket counts here are the RDRAM-writeback
    view and can smear by a few calls across adjacent buckets; cumulative
    counts across several buckets are exact. The per-call ground truth is the
    MGB64_ARES_RNG_PC_TRACE caller-PC log, not this sampler.
    """
    lock_idx = None
    for idx, record in enumerate(records):
        if parse_seed(record) == lock_seed:
            lock_idx = idx
            break
    if lock_idx is None:
        return None
    lock_global = parse_int(records[lock_idx].get("move", {}).get("global"))
    if lock_global is None:
        return None

    buckets: dict[int, int] = {}
    prev_g: int | None = None
    for record in records[lock_idx:]:
        g = parse_int(record.get("move", {}).get("global"))
        if g is None:
            continue
        if prev_g is not None and g < prev_g:
            break  # segment gate: g_GlobalTimer reset
        prev_g = g
        seed = parse_seed(record)
        if seed is None:
            continue
        buckets[g] = seed  # last record per global wins

    cum = 0
    prev_seed = lock_seed
    series: list[dict[str, Any]] = []
    for g in sorted(buckets):
        k = steps_between(prev_seed, buckets[g], max_steps)
        if k is None:
            series.append({"global": g, "cum": None, "discontinuity": True})
            break
        cum += k
        series.append({"global": g, "cum": cum})
        prev_seed = buckets[g]
        if len(series) >= frame_buckets:
            break

    # The lock frame's draws land under the lock global (the sim burst runs
    # before the global increments for the next frame), so the LAST sample at
    # lock_global is the closest boundary estimate of the first locked frame.
    # Validated against the per-call PC-trace ground truth on the reference
    # capture (both = 12); can undercount by the writeback lag when the last
    # VI sample lands mid-burst.
    # `row.get("cum") is not None` (not the old falsy truthiness check): a
    # legitimate 0-draw lock bucket (e.g. a paused/menu frame sampled at the
    # lock global) must not be treated as "no bucket here" and skipped in
    # favor of the next frame's cumulative count. Tie-break: `series` has at
    # most one row per distinct `global` (bucketed by dict key before this
    # walk), so `next(...)` never actually has to choose between two rows at
    # the same global -- it only needs to stop skipping the one legitimate
    # row whose `cum` happens to be 0.
    lock_bucket = next(
        (row for row in series if row["global"] == lock_global and row.get("cum") is not None),
        None,
    )
    first_after = next(
        (row for row in series if row["global"] > lock_global and row.get("cum") is not None),
        None,
    )
    return {
        "lock_global": lock_global,
        "buckets": series,
        "first_locked_frame_calls": (
            lock_bucket["cum"]
            if lock_bucket
            else (first_after["cum"] if first_after else None)
        ),
        "note": (
            "RDRAM-writeback view (seed sampling lags the guest dcache by a few "
            "draws); use MGB64_ARES_RNG_PC_TRACE for per-call ground truth"
        ),
    }


def native_cums_from_counter(samples: list[dict[str, Any]]) -> dict[int, int] | None:
    """Cumulative counts straight from the native exact rng.call_count field."""
    if not samples or any(s["call_count"] is None for s in samples):
        return None
    base = samples[0]["call_count"]
    return {s["rel_tick"]: s["call_count"] - base for s in samples}


def per_span_series(cums: dict[int, int], ticks: list[int]) -> dict[int, int]:
    """Call counts per span between CONSECUTIVE ticks of ``ticks``.

    The two sides sample at different tick granularities (native once per game
    frame = every g_ClockTimer globals; ares/stock ~once per global unit), so
    per-tick deltas are only comparable when both sides are resampled onto the
    SAME tick grid — the sorted common ticks. The count at tick t covers the
    span (previous common tick, t].
    """
    out: dict[int, int] = {}
    for prev, cur in zip(ticks, ticks[1:]):
        if prev in cums and cur in cums:
            out[cur] = cums[cur] - cums[prev]
    return out


def analyze(args: argparse.Namespace) -> dict[str, Any]:
    native_all = load_trace(Path(args.native))
    stock_all = load_trace(Path(args.stock))

    validation: dict[str, Any] = {}

    # Boot-seed check (stock capture samples g_randomSeed before first use).
    stock_first_seed = next(
        (parse_seed(r) for r in stock_all if parse_seed(r) is not None), None
    )
    validation["stock_first_seed"] = (
        f"0x{stock_first_seed:016X}" if stock_first_seed is not None else None
    )
    validation["stock_first_seed_is_boot_constant"] = stock_first_seed == BOOT_SEED

    # Motion-onset anchor (FID-0062 --align tick rule).
    n_start = first_moving_index(native_all, args.motion_threshold)
    s_start = first_moving_index(stock_all, args.motion_threshold)
    n_onset = parse_int(native_all[n_start].get("move", {}).get("global")) if n_start < len(native_all) else None
    s_onset = parse_int(stock_all[s_start].get("move", {}).get("global")) if s_start < len(stock_all) else None
    if n_onset is None or s_onset is None:
        raise SystemExit("FAIL: could not locate motion onset on both sides")

    n_samples = rel_tick_samples(native_all, n_start, n_onset)
    s_samples = rel_tick_samples(stock_all, s_start, s_onset)
    n_seed_by_tick = {s["rel_tick"]: s["seed"] for s in n_samples}
    s_seed_by_tick = {s["rel_tick"]: s["seed"] for s in s_samples}

    # Native: exact counter, cross-validated by the chain walk.
    n_cum_counter = native_cums_from_counter(n_samples)
    n_walk = chain_walk(n_samples, args.max_steps)
    n_cum = n_cum_counter if n_cum_counter is not None else n_walk["cum"]
    if n_cum_counter is not None:
        common = set(n_cum_counter) & set(n_walk["cum"])
        mismatches = [
            t for t in sorted(common) if n_cum_counter[t] != n_walk["cum"][t]
        ]
        validation["native_counter_vs_chainwalk"] = {
            "ticks_checked": len(common),
            "mismatch_ticks": mismatches[:20],
            "mismatch_count": len(mismatches),
            "chain_discontinuities": n_walk["discontinuities"],
        }

    # Stock: chain walk over the full-seed samples.
    s_walk = chain_walk(s_samples, args.max_steps)
    s_cum = s_walk["cum"]
    validation["stock_chain_discontinuities"] = s_walk["discontinuities"]

    # Pre-onset totals (context only; boot paths differ — menu vs direct boot).
    pre = {}
    if n_samples and n_samples[0]["call_count"] is not None:
        pre["native_calls_before_onset"] = n_samples[0]["call_count"]
    pre["stock_note"] = (
        "stock pre-onset totals include N64 boot+menu (different path); "
        "differential is anchored at motion onset"
    )

    common_ticks = sorted(set(n_cum) & set(s_cum))
    if not common_ticks:
        raise SystemExit("FAIL: no common sim-ticks between the two sides")

    # Re-anchor both cumulative series at the FIRST COMMON tick so cum_delta
    # measures divergence accumulated SINCE the shared onset (the alignment
    # hypothesis: both sides are defined in-sync at the anchor).
    anchor = common_ticks[0]
    n_base, s_base = n_cum[anchor], s_cum[anchor]

    n_per = per_span_series(n_cum, common_ticks)
    s_per = per_span_series(s_cum, common_ticks)

    timeline = []
    first_cum_divergent = None
    first_pertick_divergent = None
    first_seed_mismatch = None
    seeds_matching = 0
    for t in common_ticks:
        row: dict[str, Any] = {
            "rel_tick": t,
            "native_cum": n_cum[t] - n_base,
            "stock_cum": s_cum[t] - s_base,
            "cum_delta": (n_cum[t] - n_base) - (s_cum[t] - s_base),
        }
        # Seed equality per tick: meaningless on natural runs (different boot
        # seeds), decisive on SEED-LOCKED runs (both sides scripted to the same
        # seed at the same gameplay frame via GE007_AUTO_RNG_SEED_SCRIPT /
        # MGB64_ARES_RNG_SEED_SCRIPT): after the lock, end-of-tick seeds match
        # while and only while both sides made the same calls in the same order
        # count-wise; the first mismatch tick is the first call-count break.
        sm = n_seed_by_tick.get(t) == s_seed_by_tick.get(t)
        row["seeds_match"] = sm
        if sm:
            seeds_matching += 1
        elif first_seed_mismatch is None:
            first_seed_mismatch = t
        if t in n_per and t in s_per:
            row["native_calls"] = n_per[t]
            row["stock_calls"] = s_per[t]
            row["delta"] = n_per[t] - s_per[t]
            if row["delta"] != 0 and first_pertick_divergent is None:
                first_pertick_divergent = t
        if row["cum_delta"] != 0 and first_cum_divergent is None:
            first_cum_divergent = t
        timeline.append(row)

    deltas = [r["delta"] for r in timeline if "delta" in r]
    summary = {
        "common_ticks": len(common_ticks),
        "anchor_tick": anchor,
        "native_ticks": len(n_cum),
        "stock_ticks": len(s_cum),
        "native_onset_global": n_onset,
        "stock_onset_global": s_onset,
        "first_cum_divergent_tick": first_cum_divergent,
        "first_pertick_divergent_tick": first_pertick_divergent,
        "first_seed_mismatch_tick": first_seed_mismatch,
        "seed_match_ticks": seeds_matching,
        "final_cum_delta": timeline[-1]["cum_delta"] if timeline else None,
        "pertick_delta_nonzero": sum(1 for d in deltas if d != 0),
        "pertick_delta_sum": sum(deltas),
        "pertick_delta_min": min(deltas) if deltas else None,
        "pertick_delta_max": max(deltas) if deltas else None,
        "native_calls_per_span_mean": (
            round(sum(n_per.values()) / len(n_per), 3) if n_per else None
        ),
        "stock_calls_per_span_mean": (
            round(sum(s_per.values()) / len(s_per), 3) if s_per else None
        ),
        "pre_onset": pre,
    }

    result = {
        "native": str(args.native),
        "stock": str(args.stock),
        "motion_threshold": args.motion_threshold,
        "max_steps": args.max_steps,
        "validation": validation,
        "summary": summary,
        "timeline": timeline,
    }

    # Lock-frame mode (FID-0063): machine-reproducible first-locked-frame
    # counts on a seed-locked route (GE007_AUTO_RNG_SEED_SCRIPT /
    # MGB64_ARES_RNG_SEED_SCRIPT), independent of the motion-onset anchor —
    # the lock lands PRE-onset, which the rel-tick extractor never samples.
    lock_seed = parse_int(args.lock_seed) if args.lock_seed else None
    if lock_seed is not None:
        result["lock_frame"] = {
            "lock_seed": f"0x{lock_seed:016X}",
            "native": native_lock_frame(native_all, lock_seed, args.lock_scan_steps),
            "stock": stock_lock_frame(
                stock_all, lock_seed, args.max_steps, args.lock_frame_buckets
            ),
        }

    return result


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--native", required=True, help="native JSONL trace (rng.seed + rng.call_count)")
    p.add_argument("--stock", required=True, help="ares/stock JSONL trace (rng.seed full u64)")
    p.add_argument("--motion-threshold", type=float, default=0.01,
                   help="motion-onset threshold (matches compare_combat_trace)")
    p.add_argument("--max-steps", type=int, default=100000,
                   help="bounded PRNG walk per tick pair before declaring a discontinuity")
    p.add_argument("--json-out", help="write the full result JSON to this path")
    p.add_argument("--print-ticks", type=int, default=24,
                   help="rows of the timeline to print around the first divergence")
    p.add_argument("--lock-seed",
                   help="seed-locked route: the scripted g_randomSeed value "
                        "(hex); adds the lock_frame block (first-locked-frame "
                        "draw counts both sides)")
    p.add_argument("--lock-scan-steps", type=int, default=4096,
                   help="bounded chain scan for the native lock-consuming frame")
    p.add_argument("--lock-frame-buckets", type=int, default=8,
                   help="stock per-global buckets to report after the lock")
    return p


def main(argv: list[str]) -> int:
    args = build_parser().parse_args(argv)
    result = analyze(args)

    if args.json_out:
        Path(args.json_out).write_text(
            json.dumps(result, indent=2) + "\n", encoding="utf-8"
        )

    s = result["summary"]
    v = result["validation"]
    print(json.dumps({"summary": s}, indent=2))
    ncv = v.get("native_counter_vs_chainwalk")
    if ncv is not None:
        ok = ncv["mismatch_count"] == 0 and not ncv["chain_discontinuities"]
        print(f"validation: native counter vs chain-walk: "
              f"{'OK' if ok else 'MISMATCH'} ({ncv['ticks_checked']} ticks)")
    print(f"validation: stock chain discontinuities: "
          f"{len(v['stock_chain_discontinuities'])}")

    lock = result.get("lock_frame")
    if lock is not None:
        print(json.dumps({"lock_frame": lock}, indent=2))

    first = s["first_pertick_divergent_tick"]
    if first is not None:
        rows = result["timeline"]
        idx = next(i for i, r in enumerate(rows) if r["rel_tick"] == first)
        lo = max(0, idx - args.print_ticks // 2)
        print(f"\nfirst per-tick divergence at rel_tick {first}; window:")
        print(f"{'tick':>6} {'nat':>5} {'stk':>5} {'d':>4} {'cum_d':>6}")
        for r in rows[lo:lo + args.print_ticks]:
            print(f"{r['rel_tick']:>6} {r.get('native_calls','-'):>5} "
                  f"{r.get('stock_calls','-'):>5} {r.get('delta','-'):>4} "
                  f"{r['cum_delta']:>6}")
    else:
        print("\nno per-tick divergence on common ticks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
