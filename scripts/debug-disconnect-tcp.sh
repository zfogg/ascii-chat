#!/usr/bin/env bash

set -euo pipefail

PORT=$(((RANDOM + 2000) % 8000))
client_log=/tmp/client-logfile-"$PORT".log
server_log=/tmp/server-logfile-"$PORT".log

echo "Starting on port: $PORT"

pkill -f "ascii-chat.*(server|client).*$PORT" && sleep 0.5 || true
cmake --build build >/dev/null 2>&1
./build/bin/ascii-chat \
  --log-file "$server_log" --log-level debug \
  server --port "$PORT" \
  >/dev/null 2>&1 &
SERVER_PID=$!
sleep 0.25

EXIT_CODE=0
START_TIME=$(date +%s%N)
timeout -k1 3.25 ./build/bin/ascii-chat \
  --log-level debug --log-file "$client_log" --sync-state 3 \
  client \
  localhost:"$PORT" \
  --test-pattern \
  -S -D 1 \
  | tee /tmp/client-stdout-"$PORT".log \
  || EXIT_CODE=$?
END_TIME=$(date +%s%N)

# Calculate elapsed time in seconds
ELAPSED_NS=$((END_TIME - START_TIME))
ELAPSED_SEC=$(echo "scale=2; $ELAPSED_NS / 1000000000" | bc)

# Extract and display frame statistics
echo ""
echo "=== WEBSOCKET FPS TEST ==="
echo "Exit code: $EXIT_CODE (137=deadlock, 124=timeout/GOOD, 1=error)"
echo "Elapsed time: ${ELAPSED_SEC}s"
echo ""

# Count actual frames written by looking for frame completion markers
# These are the actual printf() calls that write frames to stdout
FRAME_COUNT=$(grep -c "ACTUAL_FRAME_WRITTEN\|frame.*written\|grid.*rendered" client.log 2>/dev/null || echo "0")
echo "🎬 FRAME COUNT: $FRAME_COUNT frames"

# Calculate actual FPS
if (( $(echo "$ELAPSED_SEC > 0" | bc -l) )); then
    ACTUAL_FPS=$(echo "scale=2; $FRAME_COUNT / $ELAPSED_SEC" | bc)
    echo "📊 ACTUAL FPS: $ACTUAL_FPS"
else
    ACTUAL_FPS=0
    echo "📊 ACTUAL FPS: 0 (insufficient time)"
fi

echo ""

# Show expected vs actual
EXPECTED_60=$(echo "scale=0; 60 * $ELAPSED_SEC" | bc | cut -d. -f1)
EXPECTED_30=$(echo "scale=0; 30 * $ELAPSED_SEC" | bc | cut -d. -f1)
echo "Expected at 60 FPS for ${ELAPSED_SEC}s: ~$EXPECTED_60 frames"
echo "Expected at 30 FPS for ${ELAPSED_SEC}s: ~$EXPECTED_30 frames"
echo "Actual frames rendered: $FRAME_COUNT"
echo ""

# Check for memory errors
if grep -q "AddressSanitizer" "$client_log" 2>/dev/null; then
    echo "⚠️  ASAN errors detected:"
    grep "SUMMARY: AddressSanitizer" "$client_log" | head -1 || true
else
    echo "✓ No memory errors detected"
fi

echo "Client log: $client_log"
echo "Server log: $server_log"

echo ""
pkill -f "ascii-chat.*(server|client).*$PORT" && sleep 0.5 || true
