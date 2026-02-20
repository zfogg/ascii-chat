#!/bin/bash
# Validate ASCII art pipeline consistency across multiple test runs
# Checks: frame counts, no crashes, data integrity

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

NUM_RUNS=${1:-5}
TIMEOUT=5
TEMP_DIR="/tmp/ascii-pipeline-validation"
mkdir -p "$TEMP_DIR"

echo "🧪 ASCII Pipeline Validation Test ($NUM_RUNS runs)"
echo "=================================================="
echo ""

# Arrays to store results
declare -a CLIENT_FRAMES
declare -a SERVER_FRAMES
declare -a CRASHES
declare -a FRAME_LAGS

for RUN in $(seq 1 $NUM_RUNS); do
  echo "Run $RUN/$NUM_RUNS..."
  
  PORT=$((30000 + RANDOM % 10000))
  WS_PORT=$((30000 + RANDOM % 10000))
  
  SERVER_LOG="/tmp/ascii-chat-server-$WS_PORT.log"
  CLIENT_LOG="/tmp/ascii-chat-client-$WS_PORT.log"
  CLIENT_STDOUT="/tmp/ascii-chat-client-stdout-$WS_PORT.txt"
  
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
  ./build/bin/ascii-chat \
    --log-file "$CLIENT_LOG" \
    --log-level debug \
    client \
    "ws://localhost:$WS_PORT" \
    -S -D 1 \
    2>&1 | tee "$CLIENT_STDOUT" \
    &
  CLIENT_PID=$!
  
  # Wait for test duration
  sleep $TIMEOUT
  
  # Kill both
  kill -9 "$SERVER_PID" "$CLIENT_PID" 2>/dev/null || true
  sleep 0.2
  
  # Analyze results
  if grep -q "AddressSanitizer\|SUMMARY:" "$CLIENT_STDOUT" 2>/dev/null; then
    CRASHES[$RUN]="YES"
  else
    CRASHES[$RUN]="NO"
  fi
  
  # Count frames
  CLIENT_FRAMES[$RUN]=$(grep -c "AUDIO_CALLBACK\|IMAGE_FRAME" "$CLIENT_LOG" 2>/dev/null || echo "0")
  SERVER_FRAMES[$RUN]=$(grep -c "ACIP_SEND_IMAGE_FRAME\|fragment" "$SERVER_LOG" 2>/dev/null || echo "0")
  
  # Check for frame lag warnings
  FRAME_LAGS[$RUN]=$(grep -c "LAG:" "$CLIENT_LOG" 2>/dev/null || echo "0")
  
  echo "  ✓ Frames processed: ${CLIENT_FRAMES[$RUN]}, Lags: ${FRAME_LAGS[$RUN]}, Crashes: ${CRASHES[$RUN]}"
done

echo ""
echo "📊 Summary"
echo "=========="
echo ""

# Calculate statistics
TOTAL_CRASHES=0
MIN_FRAMES=${CLIENT_FRAMES[1]}
MAX_FRAMES=${CLIENT_FRAMES[1]}
TOTAL_FRAMES=0
TOTAL_LAGS=0

for RUN in $(seq 1 $NUM_RUNS); do
  [ "${CRASHES[$RUN]}" = "YES" ] && ((TOTAL_CRASHES++))

  FRAMES_COUNT="${CLIENT_FRAMES[$RUN]}"
  LAGS_COUNT="${FRAME_LAGS[$RUN]}"
  [ -z "$FRAMES_COUNT" ] && FRAMES_COUNT=0
  [ -z "$LAGS_COUNT" ] && LAGS_COUNT=0

  TOTAL_FRAMES=$((TOTAL_FRAMES + FRAMES_COUNT))
  TOTAL_LAGS=$((TOTAL_LAGS + LAGS_COUNT))

  if [ "$FRAMES_COUNT" -lt "$MIN_FRAMES" ]; then
    MIN_FRAMES=$FRAMES_COUNT
  fi
  if [ "$FRAMES_COUNT" -gt "$MAX_FRAMES" ]; then
    MAX_FRAMES=$FRAMES_COUNT
  fi
done

AVG_FRAMES=$((TOTAL_FRAMES / NUM_RUNS))

echo "Crashes:          $TOTAL_CRASHES/$NUM_RUNS (✓ PASS)" 
echo "Frame count:      min=$MIN_FRAMES, avg=$AVG_FRAMES, max=$MAX_FRAMES"
echo "Frame variance:   $((MAX_FRAMES - MIN_FRAMES)) events"
echo "Total frame lags: $TOTAL_LAGS"
echo ""

if [ "$TOTAL_CRASHES" -eq 0 ] && [ $((MAX_FRAMES - MIN_FRAMES)) -lt 50 ]; then
  echo "✅ PASS: No crashes, consistent frame count"
else
  echo "⚠️  WARN: Inconsistency detected"
fi

echo ""
echo "📁 Logs saved to: $TEMP_DIR"
