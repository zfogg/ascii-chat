#!/bin/bash
# WebSocket FPS regression test
# Measures frame delivery rate during WebSocket server-client communication
# and compares against configured baseline to detect performance regressions.
#
# Baseline metrics:
# - BASELINE_FPS: Minimum expected FPS during WebSocket operations (default: 1 fps)
#   This is very conservative to catch serious regressions (0fps issue, throttling bugs)
#
# To update baseline metrics after verified improvements:
#   1. Run the test and observe actual measured FPS
#   2. Update BASELINE_FPS value below with the new minimum threshold
#   3. Commit the change: git add ... && git commit -m "test: Update FPS baseline metrics"
#   4. Document the reason: "Observed improvement: X fps after fix"

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# Baseline metrics - minimum FPS threshold
# Conservative baseline: detect serious regressions like 0fps throttling issue
BASELINE_FPS=1

# Parse optional baseline override from command line
if [[ -n "$1" && "$1" =~ ^[0-9]+(\.[0-9]+)?$ ]]; then
    BASELINE_FPS="$1"
fi

echo "🎬 WebSocket FPS Regression Test"
echo "   Baseline FPS threshold: $BASELINE_FPS fps"
echo ""

# Run the websocket test
echo "📡 Running WebSocket server-client test..."
bash scripts/test-websocket-server-client.sh > /tmp/websocket-test-output.txt 2>&1

TEST_RESULT=$?
if [[ $TEST_RESULT -ne 0 ]]; then
    echo "❌ WebSocket test failed with exit code $TEST_RESULT"
    echo "   This may indicate a crash or connection issue"
    exit 1
fi

# Find the most recent client log (test may have created multiple runs)
CLIENT_LOG=$(ls -t /tmp/ascii-chat-client-*.log 2>/dev/null | head -1)
if [[ -z "$CLIENT_LOG" ]]; then
    echo "❌ No client log found - test may have failed"
    exit 1
fi

echo "📊 Analyzing FPS metrics from: $CLIENT_LOG"
echo ""

# Extract FPS measurements from the client log
# Look for patterns like "actual fps: X.XX" in capture lag events
FPS_VALUES=$(grep -oP "actual fps: \K[0-9]+(\.[0-9]+)?" "$CLIENT_LOG" 2>/dev/null || true)

# Also check for specific throttling/delivery issues (not general network errors)
# Look for: actual fps = 0, frame queue full, critical throttling
THROTTLE_ISSUES=$(grep -iE "actual fps: 0|frame.*queue.*full|critical.*throttl" "$CLIENT_LOG" 2>/dev/null || true)

if [[ -n "$THROTTLE_ISSUES" ]]; then
    echo "❌ REGRESSION DETECTED: Critical throttling found!"
    echo "   Throttling indicators:"
    echo "$THROTTLE_ISSUES"
    echo ""
    echo "   This indicates frame delivery failure (issue #305)"
    echo "   Full client log: $CLIENT_LOG"
    exit 1
fi

if [[ -z "$FPS_VALUES" ]]; then
    # Check if test ran at all by looking for successful protocol init
    if grep -q "STREAM_START_SENT\|DATA_THREAD_SPAWNED" "$CLIENT_LOG" 2>/dev/null; then
        echo "⚠️  Protocol initialized but no FPS lag events recorded"
        echo "   This typically means video capture ran smoothly (no lag detected)"
        echo "✅ Test completed (smooth operation, no throttling detected)"
        exit 0
    else
        echo "❌ No protocol initialization detected"
        echo "   WebSocket connection may have failed"
        echo "   Full client log: $CLIENT_LOG"
        exit 1
    fi
fi

# Calculate statistics from collected FPS values
echo "📈 Measured FPS values during test:"
echo "$FPS_VALUES"
echo ""

# Find minimum FPS (most conservative for regression detection)
MIN_FPS=$(echo "$FPS_VALUES" | sort -n | head -1)
# Find average FPS
AVG_FPS=$(echo "$FPS_VALUES" | awk '{sum+=$1; count++} END {if (count>0) printf "%.2f", sum/count; else print "0"}')
# Find maximum FPS
MAX_FPS=$(echo "$FPS_VALUES" | sort -rn | head -1)

echo "   Min FPS: $MIN_FPS"
echo "   Avg FPS: $AVG_FPS"
echo "   Max FPS: $MAX_FPS"
echo ""

# Check for regression: minimum FPS must be >= baseline
if (( $(echo "$MIN_FPS < $BASELINE_FPS" | bc -l) )); then
    echo "❌ REGRESSION DETECTED!"
    echo "   Minimum FPS ($MIN_FPS) is below baseline ($BASELINE_FPS)"
    echo ""
    echo "   Possible causes:"
    echo "   - Frame delivery throttling (WebSocket FPS issue #305)"
    echo "   - Connection latency or packet loss"
    echo "   - System resource constraints"
    echo ""
    echo "   To investigate:"
    echo "   1. Check server logs for throttling or packet errors"
    echo "   2. Verify WebSocket frame delivery in client protocol"
    echo "   3. Profile CPU/memory usage during test"
    echo ""
    echo "   Full client log: $CLIENT_LOG"
    exit 1
else
    echo "✅ No regression detected"
    echo "   Minimum FPS ($MIN_FPS) meets baseline threshold ($BASELINE_FPS)"
    echo ""
    echo "📋 Logs:"
    echo "   Client log: $CLIENT_LOG"
    echo "   Test output: /tmp/websocket-test-output.txt"
    exit 0
fi
