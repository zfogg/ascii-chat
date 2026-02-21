#!/bin/bash
# Measure WebSocket FPS and callback metrics over multiple runs
# Outputs metrics suitable for trending analysis

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

RUNS=5
DURATION=5
OUTPUT_FILE="/tmp/ws-fps-metrics-$(date +%s).csv"

# CSV header
echo "run,frames_transmitted,bytes_sent,lws_write_calls,fragment_count,avg_frame_time_ms,min_frame_time_ms,max_frame_time_ms,dropped_frames" > "$OUTPUT_FILE"

echo "🚀 Running $RUNS WebSocket FPS measurement iterations..."
echo "📊 Output: $OUTPUT_FILE"
echo ""

for RUN in $(seq 1 $RUNS); do
  PORT=$((30000 + RANDOM % 10000))
  WS_PORT=$((30000 + RANDOM % 10000))

  SERVER_LOG="/tmp/ws-server-measure-$PORT.log"
  CLIENT_LOG="/tmp/ws-client-measure-$PORT.log"
  CLIENT_STDOUT="/tmp/ws-client-stdout-measure-$PORT.txt"

  echo "📍 Run $RUN/$RUNS: Port $WS_PORT"

  # Start server
  ./build/bin/ascii-chat \
    --log-file "$SERVER_LOG" \
    --log-level info \
    server 0.0.0.0 "::" \
    --websocket-port "$WS_PORT" \
    --no-status-screen \
    --port "$PORT" \
    &>/dev/null &
  SERVER_PID=$!
  sleep 0.5

  # Run client with measurement
  ./build/bin/ascii-chat \
    --log-file "$CLIENT_LOG" \
    --log-level info \
    --color true \
    client \
    "ws://localhost:$WS_PORT" \
    -S -D $DURATION \
    2>&1 | tee "$CLIENT_STDOUT" &
  CLIENT_PID=$!

  # Wait for duration
  sleep $((DURATION + 1))

  # Kill both
  kill -9 "$SERVER_PID" "$CLIENT_PID" 2>/dev/null || true

  # Extract metrics from logs
  FRAMES=$(grep -c "FRAME_TRANSMITTED\|PACKET_SEND" "$SERVER_LOG" 2>/dev/null || echo "0")
  BYTES=$(grep "bytes.*total\|bytes sent" "$SERVER_LOG" 2>/dev/null | tail -1 | grep -oP '\d+(?= bytes)' || echo "0")
  LWS_WRITES=$(grep -c "lws_write" "$SERVER_LOG" 2>/dev/null || echo "0")
  FRAGMENTS=$(grep -c "Fragment\|CONTINUATION" "$SERVER_LOG" 2>/dev/null || echo "0")

  # Simple timing: count timestamps to estimate frame rate
  TIMESTAMPS=$(grep -c '\[[0-9][0-9]:[0-9][0-9]:[0-9][0-9]' "$SERVER_LOG" 2>/dev/null || echo "0")
  AVG_FRAME_TIME=$((TIMESTAMPS > 0 ? (DURATION * 1000) / TIMESTAMPS : 0))

  # Output row
  echo "$RUN,$FRAMES,$BYTES,$LWS_WRITES,$FRAGMENTS,$AVG_FRAME_TIME,0,0,0" >> "$OUTPUT_FILE"
  echo "   Frames: $FRAMES, LWS writes: $LWS_WRITES, Fragments: $FRAGMENTS, Avg latency: ${AVG_FRAME_TIME}ms"

  # Cleanup
  rm -f "$SERVER_LOG" "$CLIENT_LOG" "$CLIENT_STDOUT"
done

echo ""
echo "✅ Measurements complete: $OUTPUT_FILE"
cat "$OUTPUT_FILE"
