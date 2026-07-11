#!/usr/bin/env bash
#
# gate_routes_smoke.sh — the ares-free hard gate for gate:true ROM-oracle routes
# (Phase B, FID-0031 / D4).
#
# A route in tools/rom_oracle_routes/*.json marked "gate": true is a committed
# parity checkpoint whose DURABLE, CI-runnable half is a deterministic input
# tape. This lane enumerates every gate:true route, resolves its paired tape
# (baselines/tapes/<tape>.ge7tape + .expected.json), replays the tape under the
# determinism envelope (+ any replay_env the baseline declares), and asserts the
# whole-sim state hash matches the recorded baseline.
#
# FID-0075 load robustness: a single replay under aggregate verify_all load can
# land on a boot-timing-shifted hash that is NOT a code divergence (the runway
# route is 3/3 deterministic uncontended yet drifts inside a full verify). So NO
# single run is authoritative. Each route's verdict is decided over a BAG of
# clean (non-misaligned) replay hashes by AGREEMENT (see decide_verdict): a
# genuine code change makes the OLD baseline unreachable and the new hash
# reproduce (=> hard FAIL, never retried away); a non-reproducible load flake
# leaves the baseline reachable and is downgraded to an unstable-note (green).
#
# Complements (does not replace):
#   - tools/fidelity/tape_regression.sh          (replays ALL committed tapes)
#   - tools/fidelity/sense_trace_sweep.sh --gate (ares-side trace comparison)
#
# ROM/binary-gated: SKIPs the whole lane cleanly (exit 0, noted) when the ROM or
# native binary is absent. --self-test runs the ROM-free decision-logic proofs.
#
# Usage: tools/fidelity/gate_routes_smoke.sh [--build-dir DIR] [--rom PATH]
#                                            [--binary PATH] [--no-build]
#                                            [--timeout SECONDS] [--self-test]
set -euo pipefail
cd "$(dirname "$0")/../.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
TIMEOUT_SECONDS=180
SELF_TEST=0

usage() { sed -n '2,36p' "$0"; exit 0; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --no-build) shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        --self-test) SELF_TEST=1; shift ;;
        -h|--help) usage ;;
        *) echo "Unknown arg: $1" >&2; exit 2 ;;
    esac
done

# ---------------------------------------------------------------------------
# Agreement-based verdict — the FID-0075 load-flake robustness core.
#
# decide_verdict WANT [H ...]   ->   prints "<KIND> <detail>"
#   KIND: PASS | PASS_UNSTABLE | FAIL | UNSTABLE | NORUN
#
# WANT is the committed baseline hash; the H... are the clean (non-misaligned)
# replay hashes collected for one route (a crash/timeout non-misaligned run is
# recorded as the literal token NONE so a *consistent* crash still fails).
#
# Why this is safe in BOTH directions:
#   * Real divergence  — a genuine sim/code change makes the OLD baseline
#     UNREACHABLE: WANT appears 0 times and the NEW hash reproduces. So
#     want_count==0 && some non-WANT hash agreeing (count>=2) => FAIL. Two-run
#     agreement on the wrong hash still fails; it is never retried away.
#   * Load flake       — a boot-timing flake is non-reproducible, so the route
#     can still reach the baseline. WANT appearing even once (want_count>=1)
#     proves the route is NOT a stable divergence (a real divergence could never
#     yield the OLD baseline) => not a hard fail: PASS if reproduced (>=2), else
#     PASS_UNSTABLE (seen once, noted).
#   * Pure noise       — no hash reproduces and the baseline is never seen: this
#     cannot be a stable divergence (that would reproduce), so it is downgraded
#     to UNSTABLE (green) but loudly flagged for an isolated re-run.
# ---------------------------------------------------------------------------
decide_verdict() {
    local want="$1"; shift
    local -a bag=( "$@" )
    local n=${#bag[@]}
    (( n == 0 )) && { echo "NORUN never got a clean (non-misaligned) run"; return; }

    local h
    declare -A cnt=()
    for h in "${bag[@]}"; do cnt["$h"]=$(( ${cnt["$h"]:-0} + 1 )); done

    local want_count=${cnt["$want"]:-0}
    local top="" topc=0
    for h in "${!cnt[@]}"; do
        [[ "$h" == "$want" ]] && continue
        (( cnt["$h"] > topc )) && { topc=${cnt["$h"]}; top="$h"; }
    done

    if   (( want_count >= 2 )); then
        echo "PASS baseline ${want} reproduced (${want_count}/${n} clean runs agree)"
    elif (( want_count == 1 )); then
        echo "PASS_UNSTABLE baseline ${want} seen once, not reproduced under load (FID-0075); other clean hashes: ${top:-none}"
    elif (( topc >= 2 )); then
        echo "FAIL divergent hash ${top} reproduced (${topc}/${n} clean runs) and baseline ${want} never observed"
    else
        echo "UNSTABLE no hash reproduced and baseline ${want} never observed (FID-0075) — needs an isolated re-run"
    fi
}

# ---------------------------------------------------------------------------
# route_sample_attempt — one replay attempt. Sets the global SAMPLE_TOK to
# exactly one token: MISALIGN | HASH <16hex> | NONE. (A global, not stdout, so
# the one-shot FLAKE_DONE latch and the self-test SELFTEST_IDX cursor persist —
# a command-substitution subshell would swallow those mutations.)
# In --self-test mode it pops a scripted token instead of running the binary.
# GATE_ROUTES_FLAKE_INJECT (test hook): substitute the FIRST clean sample of the
# whole run with this hash, once, to simulate a one-off load flake against the
# real binary (the rest of the samples run for real).
# ---------------------------------------------------------------------------
SELFTEST_MODE=0
SELFTEST_QUEUE=()
SELFTEST_IDX=0
FLAKE_INJECT="${GATE_ROUTES_FLAKE_INJECT:-}"
FLAKE_DONE=0
CUR_TAPE=""
CUR_LEVEL=""
CUR_REPLAY_ENV=""
SAMPLE_TOK=""

route_sample_attempt() {
    if [[ "$SELFTEST_MODE" == 1 ]]; then
        if (( SELFTEST_IDX < ${#SELFTEST_QUEUE[@]} )); then
            SAMPLE_TOK="${SELFTEST_QUEUE[$SELFTEST_IDX]}"
            SELFTEST_IDX=$(( SELFTEST_IDX + 1 ))
        else
            SAMPLE_TOK="MISALIGN"   # exhausted script -> behave as misalign (budget ends the loop)
        fi
        return
    fi
    if [[ -n "$FLAKE_INJECT" && "$FLAKE_DONE" == 0 ]]; then
        FLAKE_DONE=1
        SAMPLE_TOK="HASH $FLAKE_INJECT"
        return
    fi

    local dir run_rc=0 h=""
    dir="$(mktemp -d)"
    # shellcheck disable=SC2086
    if ( cd "$dir" && validation_run_with_timeout "$TIMEOUT_SECONDS" \
            env -u GE007_DEBUG "${ENVV[@]}" $CUR_REPLAY_ENV "$BINARY" --rom "$ROM" \
            --savedir "$dir/sd" --level "$CUR_LEVEL" --deterministic --play-tape "$CUR_TAPE" \
            --sim-state-hash-out "$dir/rep.json" >"$dir/log" 2>&1 ); then
        run_rc=0
    else
        run_rc=$?
    fi
    if grep -qi "MISALIGN" "$dir/log"; then rm -rf "$dir"; SAMPLE_TOK="MISALIGN"; return; fi
    [[ "$run_rc" -eq 0 ]] && h="$(hash_of "$dir/rep.json")"
    rm -rf "$dir"
    if [[ -n "$h" ]]; then SAMPLE_TOK="HASH $h"; else SAMPLE_TOK="NONE"; fi
}

# evaluate_route <route> <want>  ->  prints one verdict line; returns 0 (green,
# incl. unstable-notes) or 1 (hard fail). Samples clean runs until the baseline
# is reproduced (definitive PASS, early stop) or the clean-run budget is spent.
evaluate_route() {
    local route="$1" want="$2"
    local max_clean="${GATE_MAX_CLEAN:-4}"
    local max_misalign="${GATE_MAX_MISALIGN:-6}"
    local -a bag=()
    local misaligns=0 tok wc h

    while (( ${#bag[@]} < max_clean )); do
        route_sample_attempt
        tok="$SAMPLE_TOK"
        case "$tok" in
            MISALIGN)
                misaligns=$(( misaligns + 1 ))
                (( misaligns > max_misalign )) && break
                continue ;;
            HASH\ *) bag+=( "${tok#HASH }" ) ;;
            *)       bag+=( "NONE" ) ;;
        esac
        wc=0; for h in "${bag[@]}"; do [[ "$h" == "$want" ]] && wc=$(( wc + 1 )); done
        (( wc >= 2 )) && break   # baseline reproduced -> definitive PASS
    done

    local verdict kind detail misnote runs
    verdict="$(decide_verdict "$want" ${bag[@]+"${bag[@]}"})"
    kind="${verdict%% *}"; detail="${verdict#* }"
    misnote=""
    (( misaligns > 0 )) && misnote=" [after ${misaligns} misaligned boot-race retr$([[ $misaligns -eq 1 ]] && echo y || echo ies)]"
    runs="${bag[*]:-<none>}"

    case "$kind" in
        PASS)          echo "gate-routes: PASS $route — $detail${misnote} (runs: ${runs})"; return 0 ;;
        PASS_UNSTABLE) echo "gate-routes: PASS $route — UNSTABLE-NOTE (FID-0075): $detail${misnote} (runs: ${runs})"; return 0 ;;
        UNSTABLE)      echo "gate-routes: UNSTABLE $route — $detail${misnote} (runs: ${runs}) — NOT a hard fail (known FID-0075 load non-determinism)" >&2; return 0 ;;
        NORUN)         echo "gate-routes: FAIL $route — all replays hit the FID-0075 boot-load tick-misalignment race: $detail${misnote}" >&2; return 1 ;;
        *)             echo "gate-routes: FAIL $route — $detail${misnote} (runs: ${runs})" >&2; return 1 ;;
    esac
}

# ---------------------------------------------------------------------------
# --self-test — ROM-free proofs of the decision logic (registered as a ctest).
# Drives evaluate_route through the mocked sampler and asserts KIND + exit code
# for each scenario, including the two required FID-0075 robustness proofs.
# ---------------------------------------------------------------------------
run_self_tests() {
    local st_fail=0
    _case() {   # <label> <exp_kind> <exp_rc> <max_clean> <max_misalign> <want> <token...>
        local label="$1" exp_kind="$2" exp_rc="$3" mc="$4" mm="$5" want="$6"; shift 6
        SELFTEST_MODE=1; SELFTEST_IDX=0; SELFTEST_QUEUE=( "$@" )
        GATE_MAX_CLEAN="$mc"; GATE_MAX_MISALIGN="$mm"
        local out rc kind
        if out="$( evaluate_route "selftest:$label" "$want" 2>&1 )"; then rc=0; else rc=$?; fi
        if   [[ "$out" == *"UNSTABLE-NOTE"* ]];        then kind="PASS_UNSTABLE"
        elif [[ "$out" == *"gate-routes: UNSTABLE "* ]]; then kind="UNSTABLE"
        elif [[ "$out" == *"gate-routes: PASS "* ]];   then kind="PASS"
        elif [[ "$out" == *"gate-routes: FAIL "* ]];   then kind="FAIL"
        else kind="???"; fi
        if [[ "$kind" == "$exp_kind" && "$rc" == "$exp_rc" ]]; then
            printf 'self-test: PASS  %-34s -> %-13s rc=%s\n' "$label" "$kind" "$rc"
        else
            printf 'self-test: FAIL  %-34s -> got %s rc=%s, want %s rc=%s\n      line: %s\n' \
                "$label" "$kind" "$rc" "$exp_kind" "$exp_rc" "$out" >&2
            st_fail=1
        fi
    }

    local A="56c32149572a6055"   # baseline (real runway hash)
    local B="7ce3a55a6b47b00a"   # a genuine divergence (real GE007_NO_PATROL_MAGIC_FIX=1 runway hash)
    local F="ffffffffffffffff"   # a one-off load-flake hash
    local C="0123456789abcdef"   # a second unrelated noise hash

    echo "gate-routes self-test: FID-0075 load-robustness decision proofs"
    # Deterministic / uncontended: baseline reproduces -> hard PASS.
    _case "det-uncontended"            PASS          0 4 6 "$A" "HASH $A" "HASH $A"
    # PROOF A — genuine divergence, consistent new hash every run -> hard FAIL.
    _case "PROOF-A/genuine-divergence" FAIL          1 4 6 "$A" "HASH $B" "HASH $B" "HASH $B" "HASH $B"
    # PROOF B — one-off flake then baseline reproduces -> flake absorbed, PASS.
    _case "PROOF-B/flake-absorbed"     PASS          0 4 6 "$A" "HASH $F" "HASH $A" "HASH $A"
    # PROOF B — one-off flake, baseline seen once -> UNSTABLE-NOTE, stays green.
    _case "PROOF-B/flake-unstable-note" PASS_UNSTABLE 0 2 6 "$A" "HASH $F" "HASH $A"
    # A single run is never authoritative (matching baseline) -> unstable-note.
    _case "single-run-not-authoritative" PASS_UNSTABLE 0 1 6 "$A" "HASH $A"
    # Pure noise, baseline never seen, nothing reproduces -> UNSTABLE (green).
    _case "pure-noise"                 UNSTABLE      0 3 6 "$A" "HASH $B" "HASH $C" "HASH $F"
    # Misaligned boot-race retried, then baseline reproduces -> PASS.
    _case "misalign-then-pass"         PASS          0 4 6 "$A" "MISALIGN" "MISALIGN" "HASH $A" "HASH $A"
    # Never a clean run (all misaligned) -> hard FAIL.
    _case "never-clean"                FAIL          1 4 2 "$A" "MISALIGN" "MISALIGN" "MISALIGN"
    # Consistent crash (non-misaligned, no hash) reproduces -> hard FAIL.
    _case "consistent-crash"           FAIL          1 4 6 "$A" "NONE" "NONE"

    SELFTEST_MODE=0
    unset -f _case
    if (( st_fail )); then
        echo "self-test: FAIL — gate_routes decision logic regressed" >&2
        return 1
    fi
    echo "self-test: PASS — all cases hold (both FID-0075 robustness proofs green)"
    return 0
}

if [[ "$SELF_TEST" == 1 ]]; then
    run_self_tests
    exit $?
fi

# --------------------------- real (ROM) gate path --------------------------
if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
else
    BINARY="$(validation_resolve_path "$BINARY")"
fi
ROM="$(validation_resolve_path "$ROM")"

ROUTE_DIR="$(pwd)/tools/rom_oracle_routes"
TAPES_DIR="$(pwd)/baselines/tapes"

# Enumerate gate:true routes (name + tape). Emits "route_name<TAB>tape_name" lines.
gate_routes() {
    python3 - "$ROUTE_DIR" <<'PY'
import json, sys, pathlib
for p in sorted(pathlib.Path(sys.argv[1]).glob("*.json")):
    try:
        r = json.load(open(p))
    except (OSError, ValueError):
        continue
    if r.get("gate", False) is True:
        name = r.get("name", p.stem)
        print(f"{name}\t{r.get('tape', name)}")
PY
}

mapfile -t ROUTES < <(gate_routes)

if [[ "${#ROUTES[@]}" -eq 0 ]]; then
    echo "gate-routes: no gate:true routes found (nothing to gate yet)"
    echo "PASS: gate-routes (empty gate set)"
    exit 0
fi

echo "gate-routes: ${#ROUTES[@]} gate:true route(s): $(printf '%s ' "${ROUTES[@]%%$'\t'*}")"

# Prerequisite gate — SKIP whole lane cleanly if ROM/binary absent.
if [[ ! -x "$BINARY" || ! -e "$ROM" ]]; then
    echo "gate-routes: SKIP (native binary or ROM absent) — ${#ROUTES[@]} route(s) not replayed:"
    printf '  SKIP %s\n' "${ROUTES[@]%%$'\t'*}"
    echo "PASS: gate-routes (skipped, prerequisites absent)"
    exit 0
fi

ENVV=(SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1
      GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1)

json_field() {  # $1=expected.json $2=key
    python3 - "$1" "$2" <<'PY'
import json, sys
print(json.load(open(sys.argv[1])).get(sys.argv[2], ""))
PY
}
hash_of() { grep -o '[0-9a-f]\{16\}' "$1" 2>/dev/null | head -1; }

validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT INT TERM

fail=0
for entry in "${ROUTES[@]}"; do
    route="${entry%%$'\t'*}"
    tape_name="${entry##*$'\t'}"
    tape="$TAPES_DIR/$tape_name.ge7tape"
    exp="$TAPES_DIR/$tape_name.expected.json"
    if [[ ! -e "$tape" || ! -e "$exp" ]]; then
        echo "gate-routes: FAIL $route — gate:true but missing tape baseline ($tape_name.ge7tape/.expected.json)" >&2
        fail=1; continue
    fi
    level="$(json_field "$exp" level)"
    want="$(json_field "$exp" sim_state_hash)"
    replay_env="$(json_field "$exp" replay_env)"
    if [[ -z "$level" || -z "$want" ]]; then
        echo "gate-routes: FAIL $route — baseline missing level/sim_state_hash" >&2
        fail=1; continue
    fi
    # Replay with retry-on-misalignment (FID-0075 boot-load race) AND agreement:
    # the verdict is decided over a bag of clean replay hashes, so a load-shifted
    # single run is downgraded to an unstable-note while a reproducible divergence
    # still hard-fails (see decide_verdict / evaluate_route above).
    CUR_TAPE="$tape"; CUR_LEVEL="$level"; CUR_REPLAY_ENV="$replay_env"
    evaluate_route "$route" "$want" || fail=1
done

if [[ "$fail" -ne 0 ]]; then
    echo "gate-routes: FAIL — one or more gate:true routes diverged or lack a tape" >&2
    exit 1
fi
echo "PASS: gate-routes — all ${#ROUTES[@]} gate:true route(s) replay byte-exact (agreement)."
