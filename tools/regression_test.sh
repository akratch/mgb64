#!/bin/bash
#
# regression_test.sh — Automated regression test for GoldenEye PC port.
#
# Runs deterministic captures on multiple levels and compares against
# saved baselines. Tests three lanes: screenshots, state traces, and audio.
#
# Usage:
#   ./tools/regression_test.sh              # test against baselines
#   ./tools/regression_test.sh --baseline   # capture new baselines
#   ./tools/regression_test.sh --level 33   # test single raw LEVELID
#   ./tools/regression_test.sh --allow-missing-baselines  # bootstrap/trace-only local runs
#   ./tools/regression_test.sh --accuracy-lane  # fail if stub-hit counters are non-zero
#
# Prerequisites: python3 with PIL (for screenshot comparison)
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
BASELINE_DIR="baselines"
TMP_DIR="/tmp/ge007_regtest_$$"
FRAME=180                    # frame to capture (enough for level load + room visibility settle)
PIXEL_THRESHOLD=3.0          # max % changed pixels
LEVELS="33 34 22 26 36 35 9 20 43 27 24 29 30 25 37 23 39 41 28 32"
# Raw LEVELIDs, not solo mission numbers.
# Current 20-stage corpus:
# Dam(33) Facility(34) Statue(22) Frigate(26) Surface1(36)
# Runway(35) Bunker1(9) Silo(20) Surface2(43) Bunker2(27)
# Archives(24) Streets(29) Depot(30) Train(25) Jungle(37)
# Control(23) Caverns(39) Cradle(41) Aztec(28) Egypt(32)
SINGLE_LEVEL=""
BASELINE_MODE=0
ACCURACY_LANE=0
DO_BUILD=1
KEEP_ARTIFACTS=0
ALLOW_MISSING_BASELINES=0
TIMEOUT_BIN="$(validation_resolve_timeout_cmd)"

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --baseline) BASELINE_MODE=1; shift ;;
        --level) SINGLE_LEVEL="$2"; shift 2 ;;
        --accuracy-lane) ACCURACY_LANE=1; shift ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --keep-artifacts) KEEP_ARTIFACTS=1; shift ;;
        --allow-missing-baselines) ALLOW_MISSING_BASELINES=1; shift ;;
        --no-build) DO_BUILD=0; shift ;;
        *) echo "Unknown arg: $1"; exit 2 ;;
    esac
done

if [[ -n "$SINGLE_LEVEL" ]]; then
    LEVELS="$SINGLE_LEVEL"
fi

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
else
    BINARY="$(validation_resolve_path "$BINARY")"
fi
ROM="$(validation_resolve_path "$ROM")"

if [[ "$DO_BUILD" -eq 1 ]]; then
    echo "=== Building ==="
    validation_configure_build "$BUILD_DIR" >/dev/null 2>&1 || {
        echo "FAIL: cmake configure failed"
        exit 2
    }
    validation_build "$BUILD_DIR" >/dev/null 2>&1 || {
        echo "FAIL: build failed"
        validation_build "$BUILD_DIR" 2>&1 | tail -5
        exit 2
    }
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"
validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT INT TERM

mkdir -p "$BASELINE_DIR" "$TMP_DIR"
FAILED=0
PASSED=0
TOTAL=0

run_with_timeout() {
    if [[ -n "$TIMEOUT_BIN" ]]; then
        "$TIMEOUT_BIN" --kill-after=5 30 "$@"
    else
        "$@"
    fi
}

for lvl in $LEVELS; do
    echo ""
    echo "=== Level $lvl ==="
    TOTAL=$((TOTAL + 1))

    # Run deterministic capture
    SCREENSHOT="$TMP_DIR/screenshot_${lvl}.bmp"
    TRACE="$TMP_DIR/trace_${lvl}.jsonl"
    AUDIO="$TMP_DIR/audio_${lvl}.raw"
    RUN_LOG="$TMP_DIR/run_${lvl}.log"
    SPAWN_LOGFILE="$TMP_DIR/spawn_${lvl}.log"

    # Remove stale screenshot from cwd before launch to prevent
    # a leftover from a previous run being mistaken for this run's output
    rm -f "screenshot_${lvl}.bmp"

    GAME_EXIT=0
    run_with_timeout env -u GE007_DEBUG GE007_DETERMINISTIC_STABLE_COUNT=1 GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 GE007_AUDIO_DUMP="$AUDIO" "$BINARY" \
        --rom "$ROM" \
        --level "$lvl" --deterministic \
        --trace-state "$TRACE" \
        --screenshot-frame "$FRAME" --screenshot-label "${lvl}" \
        --screenshot-exit >"$RUN_LOG" 2>&1 || GAME_EXIT=$?

    # Move screenshot (it's created in cwd)
    if [[ -f "screenshot_${lvl}.bmp" ]]; then
        mv "screenshot_${lvl}.bmp" "$SCREENSHOT"
    fi

    # Track process-level failure separately so lane comparisons
    # cannot reset it.  Both crashes AND timeouts are failures:
    # with --screenshot-exit, a timeout means the game hung.
    PROCESS_FAIL=0
    if [[ "$GAME_EXIT" -ne 0 ]]; then
        if [[ "$GAME_EXIT" -eq 124 ]]; then
            echo "  PROCESS FAIL: game timed out (exit 124 — hung)"
        else
            echo "  PROCESS FAIL: game exited with code $GAME_EXIT"
        fi
        if [[ -f "$RUN_LOG" ]]; then
            tail -20 "$RUN_LOG" | sed 's/^/    /'
        fi
        PROCESS_FAIL=1
    fi

    if [[ "$BASELINE_MODE" -eq 1 ]]; then
        # Save baselines — require all three artifacts
        MISSING=""
        [[ ! -f "$SCREENSHOT" ]] && MISSING="$MISSING screenshot"
        [[ ! -f "$TRACE" ]] && MISSING="$MISSING trace"
        [[ ! -f "$AUDIO" ]] && MISSING="$MISSING audio"
        if [[ -n "$MISSING" ]]; then
            echo "  BASELINE FAIL: missing artifacts:$MISSING"
            FAILED=$((FAILED + 1))
            continue
        fi
        echo "  Saving baselines..."
        cp "$SCREENSHOT" "$BASELINE_DIR/screenshot_${lvl}.bmp"
        cp "$TRACE" "$BASELINE_DIR/trace_${lvl}.jsonl"
        cp "$AUDIO" "$BASELINE_DIR/audio_${lvl}.raw"
        echo "  BASELINE SAVED"
        PASSED=$((PASSED + 1))
        continue
    fi

    # Initialize lane fail from process state — a crash/timeout
    # taints the entire level regardless of what comparisons show
    LANE_FAIL=$PROCESS_FAIL

    # Compare screenshots
    if [[ -f "$SCREENSHOT" && -f "$BASELINE_DIR/screenshot_${lvl}.bmp" ]]; then
        COMPARE_EXIT=0
        RESULT=$(python3 tools/compare_screenshots.py "$BASELINE_DIR/screenshot_${lvl}.bmp" "$SCREENSHOT" 2>&1) || COMPARE_EXIT=$?
        if [[ "$COMPARE_EXIT" -ne 0 ]]; then
            echo "  PIXEL FAIL: comparator error (exit $COMPARE_EXIT)"
            echo "    $RESULT" | head -3
            LANE_FAIL=1
        else
            PCT=$(echo "$RESULT" | grep "Changed pixels" | grep -oE '[0-9]+\.[0-9]+' | head -1)
            if [[ -z "$PCT" ]]; then
                echo "  PIXEL FAIL: comparator produced no percentage"
                echo "    $RESULT" | head -3
                LANE_FAIL=1
            elif python3 -c "import sys; sys.exit(0 if float('${PCT}') > ${PIXEL_THRESHOLD} else 1)" 2>/dev/null; then
                echo "  PIXEL FAIL: ${PCT}% changed (threshold ${PIXEL_THRESHOLD}%)"
                LANE_FAIL=1
            else
                echo "  pixel: PASS (${PCT}% delta)"
            fi
        fi
    elif [[ ! -f "$BASELINE_DIR/screenshot_${lvl}.bmp" ]]; then
        if [[ "$ALLOW_MISSING_BASELINES" -eq 1 ]]; then
            echo "  pixel: SKIP (no baseline — allow-missing-baselines)"
        else
            echo "  PIXEL FAIL: missing baseline $BASELINE_DIR/screenshot_${lvl}.bmp (run with --baseline first)"
            LANE_FAIL=1
        fi
    else
        echo "  PIXEL FAIL: no screenshot captured (game may have crashed)"
        LANE_FAIL=1
    fi

    # Compare state traces
    if [[ -f "$TRACE" && -f "$BASELINE_DIR/trace_${lvl}.jsonl" ]]; then
        STATE_EXIT=0
        STATE_RESULT=$(python3 tools/compare_state.py "$BASELINE_DIR/trace_${lvl}.jsonl" "$TRACE" 2>&1) || STATE_EXIT=$?
        if [[ "$STATE_EXIT" -eq 0 ]]; then
            LINES=$(wc -l < "$TRACE" | tr -d ' ')
            echo "  state: PASS ($LINES frames)"
        else
            echo "  STATE FAIL:"
            printf '%s\n' "$STATE_RESULT" | head -8 | sed 's/^/    /'
            LANE_FAIL=1
        fi
    elif [[ ! -f "$BASELINE_DIR/trace_${lvl}.jsonl" ]]; then
        if [[ "$ALLOW_MISSING_BASELINES" -eq 1 ]]; then
            echo "  state: SKIP (no baseline — allow-missing-baselines)"
        else
            echo "  STATE FAIL: missing baseline $BASELINE_DIR/trace_${lvl}.jsonl (run with --baseline first)"
            LANE_FAIL=1
        fi
    else
        echo "  STATE FAIL: no trace captured (game may have crashed)"
        LANE_FAIL=1
    fi

    if [[ -f "$TRACE" ]]; then
        RENDER_EXIT=0
        RENDER_RESULT=$(python3 tools/audit_render_trace.py --label "regression level $lvl" "$TRACE" 2>&1) || RENDER_EXIT=$?
        if [[ "$RENDER_EXIT" -ne 0 ]]; then
            echo "  RENDER HEALTH FAIL:"
            printf '%s\n' "$RENDER_RESULT" | head -8 | sed 's/^/    /'
            LANE_FAIL=1
        else
            echo "  render_health: PASS"
        fi
    fi

    if [[ "$ACCURACY_LANE" -eq 1 && -f "$TRACE" ]]; then
        STUB_EXIT=0
        STUB_RESULT=$(python3 tools/assert_stub_hits_zero.py "$TRACE" 2>&1) || STUB_EXIT=$?
        if [[ "$STUB_EXIT" -ne 0 ]]; then
            echo "  ACCURACY FAIL:"
            printf '%s\n' "$STUB_RESULT" | head -8 | sed 's/^/    /'
            LANE_FAIL=1
        else
            echo "  accuracy: PASS (stub hits = 0)"
        fi
    fi

    # Compare audio
    if [[ -f "$AUDIO" && -f "$BASELINE_DIR/audio_${lvl}.raw" ]]; then
        AUDIO_EXIT=0
        AUDIO_RESULT=$(python3 tools/compare_audio.py "$BASELINE_DIR/audio_${lvl}.raw" "$AUDIO" 2>&1) || AUDIO_EXIT=$?
        if [[ "$AUDIO_EXIT" -eq 0 ]]; then
            echo "  audio: PASS"
        else
            echo "  AUDIO FAIL:"
            printf '%s\n' "$AUDIO_RESULT" | head -8 | sed 's/^/    /'
            LANE_FAIL=1
        fi
    elif [[ ! -f "$BASELINE_DIR/audio_${lvl}.raw" ]]; then
        if [[ "$ALLOW_MISSING_BASELINES" -eq 1 ]]; then
            echo "  audio: SKIP (no baseline — allow-missing-baselines)"
        else
            echo "  AUDIO FAIL: missing baseline $BASELINE_DIR/audio_${lvl}.raw (run with --baseline first)"
            LANE_FAIL=1
        fi
    else
        echo "  AUDIO FAIL: no audio captured (game may have crashed)"
        LANE_FAIL=1
    fi

    # Spawn health lane (uses GE007_DEBUG diagnostics)
    SPAWN_EXIT=0
    (
        cd "$TMP_DIR"
        rm -f "screenshot_spawn_${lvl}.bmp"
        run_with_timeout env -u GE007_DEBUG GE007_DETERMINISTIC_STABLE_COUNT=1 GE007_NO_VSYNC=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 GE007_DEBUG=1 GE007_ASSERT_ON_FAIL=0 "$BINARY" \
            --rom "$ROM" \
            --level "$lvl" --deterministic \
            --screenshot-frame 30 --screenshot-label "spawn_${lvl}" --screenshot-exit
    ) >"$SPAWN_LOGFILE" 2>&1 || SPAWN_EXIT=$?

    SPAWN_ASSERTS=$(grep -cF "[GEASSERT]" "$SPAWN_LOGFILE" 2>/dev/null || true)
    SPAWN_ASSERTS="${SPAWN_ASSERTS:-0}"
    if [[ "$SPAWN_EXIT" -ne 0 && "$SPAWN_EXIT" -ne 124 ]]; then
        echo "  SPAWN FAIL: crash (exit $SPAWN_EXIT)"
        LANE_FAIL=1
    elif [[ "$SPAWN_ASSERTS" -gt 0 ]]; then
        echo "  SPAWN FAIL: $SPAWN_ASSERTS assertion(s)"
        grep -F "[GEASSERT]" "$SPAWN_LOGFILE" | head -3 | sed 's/^/    /'
        LANE_FAIL=1
    else
        echo "  spawn: PASS"
    fi

    if [[ "$LANE_FAIL" -eq 1 ]]; then
        FAILED=$((FAILED + 1))
    else
        PASSED=$((PASSED + 1))
    fi
done

echo ""
echo "=== Results: $PASSED/$TOTAL passed, $FAILED failed ==="
if [[ "$BASELINE_MODE" -eq 1 ]]; then
    echo "Baselines saved to $BASELINE_DIR/"
fi
if [[ "$FAILED" -ne 0 || "$KEEP_ARTIFACTS" -eq 1 ]]; then
    echo "Artifacts kept at $TMP_DIR/"
else
    rm -rf "$TMP_DIR"
fi
exit $FAILED
