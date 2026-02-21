#!/bin/bash
# Simple websocket validation runner
# Runs test-websocket-server-client.sh multiple times and collects output

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

NUM_RUNS=25
TEST_SCRIPT="$SCRIPT_DIR/test-websocket-server-client.sh"

echo "🧪 WebSocket Validation - Running $NUM_RUNS iterations"
echo "=================================================="
echo ""

# Counters
success=0
failed=0
crashed=0

for i in $(seq 1 $NUM_RUNS); do
  printf "[%2d/$NUM_RUNS] " "$i"

  if bash "$TEST_SCRIPT" > /tmp/ws-test-$i.log 2>&1; then
    # Check for crashes in output
    if grep -q "AddressSanitizer\|SUMMARY:\|Segmentation\|CRASH DETECTED" "/tmp/ws-test-$i.log" 2>/dev/null; then
      echo "❌ CRASH"
      ((crashed++))
    else
      echo "✅ OK"
      ((success++))
    fi
  else
    echo "❌ FAILED"
    ((failed++))
  fi
done

echo ""
echo "📊 Results Summary"
echo "================="
echo "Successful runs: $success / $NUM_RUNS"
echo "Failed runs: $failed / $NUM_RUNS"
echo "Crashed runs: $crashed / $NUM_RUNS"
echo "Success rate: $(( success * 100 / NUM_RUNS ))%"
echo ""

# Find latest run's output for detailed inspection
LATEST_LOG=$(ls -t /tmp/ws-test-*.log 2>/dev/null | head -1)
if [ -n "$LATEST_LOG" ]; then
  echo "📋 Latest test output:"
  echo "====================="
  tail -50 "$LATEST_LOG"
fi
