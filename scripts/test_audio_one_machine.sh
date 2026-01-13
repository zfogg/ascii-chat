#!/bin/bash
#
# Single-machine audio test for ascii-chat
# Runs server and two clients all on localhost
#

set -e

# Cleanup function for Ctrl+C
cleanup() {
  echo ""
  echo "Caught interrupt, cleaning up..."
  pkill -x ascii-chat 2>/dev/null || true
  sleep 1
  pkill -9 -x ascii-chat 2>/dev/null || true
  echo "Cleanup complete."
  exit 130
}

trap cleanup SIGINT SIGTERM

PORT=27228
DURATION=30
CURRENT_HOST=$(hostname)

# Detect repo path based on OS
if [[ "$(uname)" == "Darwin" ]]; then
  REPO="/Users/zfogg/src/github.com/zfogg/ascii-chat"
else
  REPO="/home/zfogg/src/github.com/zfogg/ascii-chat"
fi
BIN="$REPO/build/bin/ascii-chat"

echo ""
echo "Single-machine audio analysis test"
echo "==================================="
echo "Current host: $CURRENT_HOST"
echo "Server: localhost:$PORT"
echo "Clients: 2 (both on localhost)"
echo "Duration: $DURATION seconds"
echo ""

# Helper functions
run_cmd() {
  (cd $REPO && eval "$1")
}

run_bg() {
  (cd $REPO && nohup bash -c "$1" > /dev/null 2>&1 &)
}

# Clean up old logs
echo "[1/6] Cleaning up old logs..."
rm -f /tmp/server_debug.log /tmp/client1_debug.log /tmp/client2_debug.log /tmp/audio_*.wav /tmp/aec3_*.wav 2>/dev/null || true

# Kill any existing processes
echo "[2/6] Killing existing ascii-chat processes..."
pkill -x ascii-chat 2>/dev/null || true
sleep 2
pkill -9 -x ascii-chat 2>/dev/null || true
sleep 1

# Rebuild
echo "[3/6] Rebuilding..."
run_cmd "cmake --build build --target ascii-chat 2>&1 | tail -5" || {
  echo "WARNING: Build failed, continuing with existing binary"
}

# Start server
echo "[4/6] Starting server on localhost:$PORT..."
run_bg "ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK=1 timeout $((DURATION + 10)) $BIN --log-file /tmp/server_debug.log server 0.0.0.0 --port $PORT --no-upnp"

# Give server time to start
sleep 2

# Wait for server to be listening
echo "Waiting for server to be listening on localhost:$PORT..."
MAX_WAIT=60
DEBUG_AFTER=5
for i in $(seq 1 $MAX_WAIT); do
  if nc -z localhost $PORT 2>/dev/null; then
    echo "Server is listening! (took ${i}s)"
    break
  fi

  # Start debugging after DEBUG_AFTER failures
  if [ $i -eq $DEBUG_AFTER ]; then
    echo ""
    echo "=== DEBUG: Server not listening after ${DEBUG_AFTER}s ==="
    pgrep -x ascii-chat && echo "Process is running" || echo "Process NOT running"
    lsof -i :$PORT 2>/dev/null | head -5 || echo "Nothing listening on port $PORT"
    echo "Last 30 lines of server log:"
    tail -30 /tmp/server_debug.log 2>/dev/null || echo "No log file"
    echo "=== END DEBUG ==="
    echo ""
    echo "Exiting due to server startup failure"
    exit 1
  fi

  sleep 1
done

# Verify process is still running
pgrep -x ascii-chat > /dev/null || { echo "ERROR: Server process not found"; exit 1; }
echo "Server verified running"

# Start client 1 (WITH audio)
echo "[5/6] Starting client 1 with audio..."
run_bg "ASCIICHAT_DUMP_AUDIO=1 ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK=1 COLUMNS=40 LINES=12 timeout $((DURATION + 5)) $BIN --log-file /tmp/client1_debug.log client localhost:$PORT --test-pattern --audio --audio-analysis --snapshot --snapshot-delay $DURATION"
sleep 2  # Give client time to connect

# Start client 2 (WITH audio - both clients need audio for mixing to work)
echo "[6/6] Starting client 2 with audio..."
run_bg "ASCIICHAT_DUMP_AUDIO=1 ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK=1 COLUMNS=40 LINES=12 timeout $((DURATION + 5)) $BIN --log-file /tmp/client2_debug.log client localhost:$PORT --test-pattern --audio --audio-analysis --snapshot --snapshot-delay $DURATION"
sleep 2  # Give client time to connect

echo ""
echo "Running test for $DURATION seconds..."
echo "Play music or speak into your microphone!"
echo "=========================================="
echo ""

# Wait for the test duration
sleep $((DURATION))

# Give clients time to write analysis report on graceful exit
echo "Waiting for clients to finish and write analysis reports..."
sleep 10

# Clean up any remaining processes (use SIGTERM for graceful shutdown)
echo "Cleaning up any remaining processes..."
pkill -x ascii-chat 2>/dev/null || true
sleep 3
pkill -9 -x ascii-chat 2>/dev/null || true
sleep 1

echo ""
echo "=========================================================================="
echo "                         CLIENT 1 AUDIO ANALYSIS"
echo "=========================================================================="
grep -A 80 'AUDIO ANALYSIS REPORT' /tmp/client1_debug.log 2>/dev/null || echo "Report not found"

echo ""
echo "=========================================================================="
echo "                         CLIENT 2 AUDIO ANALYSIS"
echo "=========================================================================="
grep -A 80 'AUDIO ANALYSIS REPORT' /tmp/client2_debug.log 2>/dev/null || echo "Report not found"

echo ""
echo "=========================================================================="
echo "                              AEC3 METRICS"
echo "=========================================================================="
echo "Client 1 AEC3:"
grep 'AEC3:' /tmp/client1_debug.log 2>/dev/null | tail -5 || echo "Not found"
echo ""
echo "Client 2 AEC3:"
grep 'AEC3:' /tmp/client2_debug.log 2>/dev/null | tail -5 || echo "Not found"

echo ""
echo "Full logs available at:"
echo "  /tmp/server_debug.log"
echo "  /tmp/client1_debug.log"
echo "  /tmp/client2_debug.log"
