#!/bin/bash
# D43 intro regression check: capture one level's intro with the fix (default-on)
# and emit a single RESULT line. Native-only (no stock oracle). Usage:
#   scripts/intro_regression_check.sh <level_num> <out_dir>
set -u
cd "$(dirname "$0")/.."
LEVEL="$1"
OUT="${2:-/tmp/introreg}"
mkdir -p "$OUT"
TRACE="$OUT/intro_l${LEVEL}.jsonl"
SHOT="screenshot_introreg_l${LEVEL}"

SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
  GE007_ENABLE_LEVEL_INTRO=1 \
  timeout 150 ./build/ge007 --rom baserom.u.z64 --level "$LEVEL" --deterministic --faithful \
  --trace-state "$TRACE" --screenshot-frame 300 --screenshot-label "introreg_l${LEVEL}" \
  --screenshot-exit >"$OUT/l${LEVEL}.log" 2>&1
EXIT=$?
rm -f "${SHOT}.bmp"

python3 - "$TRACE" "$LEVEL" "$EXIT" <<'PY'
import sys,json
trace,level,exit_code=sys.argv[1],sys.argv[2],int(sys.argv[3])
phase3=False; positions=set(); anim_ok=False; frames=0; crashed = (exit_code!=0)
try:
    for line in open(trace):
        try:d=json.loads(line)
        except:continue
        frames+=1
        if d.get("cam")!=3:continue
        ba=(d.get("intro") or {}).get("bond_anim") or {}
        h=ba.get("hash") or ""
        if h.startswith("0x79F92FB0"):
            phase3=True; anim_ok=True
            c=d.get("col") or d.get("pos")
            if c: positions.add(tuple(round(x,1) for x in c[:3]))
except FileNotFoundError:
    pass
moved = len(positions)>1
print(f"RESULT level={level} exit={exit_code} crashed={crashed} frames={frames} "
      f"phase3_fired={phase3} anim99_hash_ok={anim_ok} bond_moved={moved} "
      f"distinct_positions={len(positions)}")
PY
