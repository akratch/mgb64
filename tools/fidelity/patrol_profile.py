#!/usr/bin/env python3
"""patrol_profile.py — per-guard patrol movement-profile analyzer (FID-0014).

Reads a combat_oracle trace (jsonl from --trace-state / the movement-oracle
capture harness), canonicalizes one record per advancing ``move.global`` sim
tick (last max-roster record per tick, empty rosters dropped — the FID-0062
Sec.12.1 rule), and reports per-guard movement profiles over a tick window:

  - ``paused_pct``: fraction of ticks whose XZ step rate is below the pause
    threshold (default 0.05 u/tick) — retail WAYMODE_MAGIC guards freeze
    between pad warps, so unseen patrollers sit high here;
  - ``warps`` / ``warp_sizes`` / ``warp_max``: steps larger than the warp
    threshold (default 50 u) — the chrlvTravelTickMagic (US 0x7F028600)
    pad-teleports;
  - ``xz_dist``: total XZ path length.

Used by tools/fidelity/patrol_magic_profile_smoke.sh to gate the FID-0014
faithful magic-travel semantics (stock pause/warp profile natively) and its
GE007_NO_PATROL_MAGIC_FIX negative control. Evidence anchor:
docs/fidelity/derivations/FID-0014-patrol-magic.md Sec.4.
"""
import argparse
import json
import math


def load_canonical(path, tick_lo=None, tick_hi=None):
    per_tick = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            co = rec.get("combat_oracle")
            mv = rec.get("move")
            if not co or not mv:
                continue
            g = mv.get("global")
            if g is None:
                continue
            if tick_lo is not None and g < tick_lo:
                continue
            if tick_hi is not None and g > tick_hi:
                continue
            guards = co.get("guards") or []
            if not guards:
                continue
            cur = per_tick.get(g)
            if cur is None or len(guards) >= len(cur):
                per_tick[g] = guards
    return per_tick


def profile(per_tick, chrnums, pause_thresh, warp_thresh):
    ticks = sorted(per_tick.keys())
    out = {}
    for cn in chrnums:
        samples = []
        for t in ticks:
            for gd in per_tick[t]:
                if gd.get("chrnum") == cn:
                    p = gd.get("pos")
                    if p and len(p) == 3:
                        samples.append((t, p))
                    break
        if len(samples) < 2:
            out[cn] = None
            continue
        paused = moved = warps = 0
        warp_sizes = []
        dist = 0.0
        for i in range(1, len(samples)):
            t0, p0 = samples[i - 1]
            t1, p1 = samples[i]
            dt = t1 - t0
            if dt <= 0:
                continue
            dxz = math.hypot(p1[0] - p0[0], p1[2] - p0[2])
            if dxz / dt < pause_thresh:
                paused += dt
            else:
                moved += dt
            if dxz > warp_thresh:
                warps += 1
                warp_sizes.append(round(dxz, 1))
            dist += dxz
        total = paused + moved
        out[cn] = {
            "samples": len(samples),
            "ticks": total,
            "paused_ticks": paused,
            "paused_pct": round(100.0 * paused / total, 1) if total else 0.0,
            "warps": warps,
            "warp_sizes": warp_sizes[:16],
            "warp_max": max(warp_sizes) if warp_sizes else 0.0,
            "xz_dist": round(dist, 1),
            "first_tick": samples[0][0],
            "last_tick": samples[-1][0],
        }
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("trace")
    ap.add_argument("--chrnums", default="2,39,40,41,42,43,44,45",
                    help="comma-separated guard chrnums (default: Dam patrol set)")
    ap.add_argument("--tick-lo", type=int, default=None)
    ap.add_argument("--tick-hi", type=int, default=None)
    ap.add_argument("--pause-thresh", type=float, default=0.05,
                    help="XZ u/tick below which a step counts as paused")
    ap.add_argument("--warp-thresh", type=float, default=50.0,
                    help="XZ u/step above which a step counts as a warp")
    ap.add_argument("--json-out", default=None)
    args = ap.parse_args()

    chrnums = [int(x) for x in args.chrnums.split(",")]
    per_tick = load_canonical(args.trace, args.tick_lo, args.tick_hi)
    prof = profile(per_tick, chrnums, args.pause_thresh, args.warp_thresh)
    ticks = sorted(per_tick.keys())
    hdr = {
        "trace": args.trace,
        "tick_window": [ticks[0], ticks[-1]] if ticks else None,
        "canonical_ticks": len(ticks),
        "pause_thresh": args.pause_thresh,
        "warp_thresh": args.warp_thresh,
    }
    print(json.dumps(hdr))
    for cn in chrnums:
        print(f"chr {cn:3d}: " + (json.dumps(prof[cn]) if prof[cn] else "ABSENT"))
    if args.json_out:
        with open(args.json_out, "w") as f:
            json.dump({"header": hdr,
                       "guards": {str(k): v for k, v in prof.items()}}, f, indent=1)


if __name__ == "__main__":
    main()
