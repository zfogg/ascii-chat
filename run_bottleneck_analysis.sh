#!/bin/bash
# Frame Queueing Bottleneck Analysis Script
# Runs multiple test cycles and analyzes frame delivery metrics

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR" && pwd)"
cd "$REPO_ROOT"

OUTPUT_DIR="/tmp/bottleneck_analysis"
mkdir -p "$OUTPUT_DIR"

TEST_RUNS=3
TEST_DURATION=5

echo "🔍 FRAME QUEUEING BOTTLENECK ANALYSIS"
echo "======================================"
echo "Output directory: $OUTPUT_DIR"
echo "Test runs: $TEST_RUNS"
echo "Test duration: ${TEST_DURATION}s per run"
echo ""

for run in $(seq 1 $TEST_RUNS); do
  echo "📊 Run $run/$TEST_RUNS..."

  PORT=$((30000 + RANDOM % 10000))
  WS_PORT=$((30000 + RANDOM % 10000))

  SERVER_LOG="$OUTPUT_DIR/server_run${run}_port${WS_PORT}.log"
  CLIENT_LOG="$OUTPUT_DIR/client_run${run}_port${WS_PORT}.log"
  CLIENT_STDOUT="$OUTPUT_DIR/client_stdout_run${run}_port${WS_PORT}.txt"

  echo "   Server: $SERVER_LOG"
  echo "   Client: $CLIENT_LOG"

  # Start server
  ./build/bin/ascii-chat \
    --log-file "$SERVER_LOG" \
    --log-level debug \
    server 0.0.0.0 "::" \
    --websocket-port "$WS_PORT" \
    --no-status-screen \
    --port "$PORT" \
    &
  SERVER_PID=$!

  sleep 0.5

  # Start client
  CLIENT_STDOUT_FILE="$CLIENT_STDOUT"
  ./build/bin/ascii-chat \
    --log-file "$CLIENT_LOG" \
    --log-level debug \
    --color true \
    --color-mode truecolor \
    client \
    "ws://localhost:$WS_PORT" \
    -S -D 1 \
    2>&1 | tee "$CLIENT_STDOUT_FILE" \
    &
  CLIENT_PID=$!

  echo "   Server PID: $SERVER_PID, Client PID: $CLIENT_PID"

  # Wait for test duration
  sleep "$TEST_DURATION"

  # Kill both
  kill -9 "$SERVER_PID" "$CLIENT_PID" 2>/dev/null || true
  sleep 0.2

  echo "   ✅ Run $run complete"
done

echo ""
echo "📈 ANALYZING METRICS..."
echo ""

# Parse metrics from all runs
ANALYSIS_FILE="$OUTPUT_DIR/METRICS_SUMMARY.txt"
cat > "$ANALYSIS_FILE" <<'EOF'
# FRAME QUEUEING BOTTLENECK ANALYSIS
Generated: $(date)

## TEST CONFIGURATION
- Test Duration: 5 seconds per run
- Number of Test Runs: 3
- Mode: WebSocket Server + Client
- Log Level: DEBUG

## METRICS COLLECTED

### Frame Generation Metrics
EOF

echo "Analysis file: $ANALYSIS_FILE"

# Extract server metrics from all server logs
for log in "$OUTPUT_DIR"/server_*.log; do
  run_name=$(basename "$log" .log)
  echo "Analyzing $run_name..." >&2

  echo "" >> "$ANALYSIS_FILE"
  echo "### Run: $run_name" >> "$ANALYSIS_FILE"

  # Count frame commits
  frame_commits=$(grep -c "FRAME_COMMIT_TIMING\|UNIQUE frames being sent" "$log" 2>/dev/null || echo "0")
  echo "- Frame commits: $frame_commits" >> "$ANALYSIS_FILE"

  # Extract FPS from diagnostic logs
  if grep -q "LOOP running at" "$log"; then
    grep "LOOP running at" "$log" | tail -5 >> "$ANALYSIS_FILE"
  fi

  # Count audio packets
  audio_packets=$(grep -c "SEND_AUDIO: client" "$log" 2>/dev/null || echo "0")
  echo "- Audio packet batches: $audio_packets" >> "$ANALYSIS_FILE"

  # Count frame drops (if any)
  dropped=$(grep -c "Dropped packet from queue" "$log" 2>/dev/null || echo "0")
  if [ "$dropped" -gt 0 ]; then
    echo "- ⚠️  Dropped packets: $dropped" >> "$ANALYSIS_FILE"
  fi

  # Extract queue stats
  if grep -q "queue_depth\|queue size" "$log"; then
    echo "- Queue statistics found" >> "$ANALYSIS_FILE"
  fi
done

echo ""
echo "📋 Summary Analysis Complete"
echo "Detailed metrics in: $ANALYSIS_FILE"
echo ""

# Display summary
echo "=== QUICK SUMMARY ==="
echo ""
cat "$ANALYSIS_FILE"

echo ""
echo "📂 All data collected in: $OUTPUT_DIR"
ls -lh "$OUTPUT_DIR"/*.log 2>/dev/null | head -10
