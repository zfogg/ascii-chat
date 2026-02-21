#!/bin/bash
# Run WebSocket validation tests and create results

cd "$(dirname "$0")"

NUM_RUNS=25
SUCCESS=0
FAILED=0
TOTAL_FRAMES=0

echo "Running $NUM_RUNS websocket tests..."
for i in $(seq 1 $NUM_RUNS); do
  printf "[%2d/$NUM_RUNS] " "$i"
  if bash scripts/test-websocket-server-client.sh > /tmp/test-$i.log 2>&1; then
    # Check if "✅ Done" appears - indicates successful test completion
    if grep -q "✅ Done" "/tmp/test-$i.log" && ! grep -q "AddressSanitizer" "/tmp/test-$i.log"; then
      echo "✅"
      ((SUCCESS++))
      # Count fragments delivered
      FRAGMENTS=$(grep -c "Fragment #" "/tmp/test-$i.log" || echo "0")
      TOTAL_FRAMES=$((TOTAL_FRAMES + FRAGMENTS))
    else
      echo "⚠️"
      ((FAILED++))
    fi
  else
    echo "❌"
    ((FAILED++))
  fi
done

echo ""
echo "Results: $SUCCESS successful, $FAILED failed out of $NUM_RUNS"
echo "Average fragments per run: $((TOTAL_FRAMES / NUM_RUNS))"
echo "Total fragments: $TOTAL_FRAMES"
