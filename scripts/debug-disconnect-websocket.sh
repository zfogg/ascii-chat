#!/usr/bin/env bash

set -euo pipefail

PORT=$(((RANDOM % 6000) + 2000))
PORT_WS=$(((RANDOM % 6000) + 2000))
client_log=/tmp/client-logfile-"$PORT".log
client_stdout=/tmp/client-stdout-"$PORT".log
server_log=/tmp/server-logfile-"$PORT".log

SNAPSHOT_DELAY=3.25

rm -rf "$client_log" "$server_log" "$client_stdout"

echo "Gonna run on websocket port: $PORT"


pkill -f "ascii-chat.*(server|client).*$PORT" && sleep 0.5 || true

cmake --build build --target ascii-chat

./build/bin/ascii-chat --log-file "$server_log" --log-level debug \
  server --port "$PORT" --websocket-port "$PORT_WS" \
  >/dev/null 2>&1 &
SERVER_PID=$!
sleep 0.25

set -x
EXIT_CODE=0
START_TIME=$(date +%s%N)
export ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK='1'
export ASCII_CHAT_QUESTION_PROMPT_RESPONSE='y'
timeout -k1 10 ./build/bin/ascii-chat \
  --log-level debug --log-file "$client_log" --sync-state 3 \
  client ws://localhost:"$PORT_WS" \
  --test-pattern -S -D "$SNAPSHOT_DELAY" \
  | tee "$client_stdout" || true
EXIT_CODE=$?
END_TIME=$(date +%s%N)

# Wait for log file to be fully flushed to disk
# This is especially important in environments like tmux where buffering may delay writes
sleep 0.5

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
# Extract the number before "unique" using a more specific pattern
FRAME_COUNT=$(grep -i 'unique frames rendered' "$client_log" 2>/dev/null | tail -1 | grep -oE '[0-9]+ unique' | grep -oE '^[0-9]+' || echo "0")
echo "$FRAME_COUNT"
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

echo ""
echo "Running 'tail -20 $client_stdout'"
tail -20 "$client_stdout"

# Check for memory errors
echo ""
if grep -q "AddressSanitizer" "$client_stdout" 2>/dev/null; then
    echo "⚠️  ASAN errors detected:"
    grep "SUMMARY: AddressSanitizer" "$client_stdout" | head -1 || true
else
    echo "✓ No memory errors detected"
fi

# Show expected vs actual
EXPECTED_60=$(echo "scale=0; 60 * $SNAPSHOT_DELAY" | bc | cut -d. -f1)
EXPECTED_30=$(echo "scale=0; 30 * $SNAPSHOT_DELAY" | bc | cut -d. -f1)
echo ""
echo "Expected at 60 FPS for ${SNAPSHOT_DELAY}s: ~$EXPECTED_60 frames"
echo "Expected at 30 FPS for ${SNAPSHOT_DELAY}s: ~$EXPECTED_30 frames"
echo "Actual frames rendered: $FRAME_COUNT (with SNAPSHOT_DELAY=${SNAPSHOT_DELAY}s of rendering time)"
echo ""

echo ""
echo "Client log: $client_log"
echo "Client stdout: $client_stdout"
echo "Server log: $server_log"

echo ""
pkill -f "ascii-chat.*(server|client).*$PORT" && sleep 0.5 || true
