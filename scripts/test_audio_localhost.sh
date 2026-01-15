#!/bin/bash
#
# Local audio test - two clients on same machine
# Each client captures from microphone
# Server mixes and sends audio between clients
# Checks that received audio WAV file has good quality
#

set -e

PORT=27224
DURATION=15
BIN="./build/bin/ascii-chat"

echo "Local Audio Test (2 Clients)"
echo "============================"
echo "Duration: $DURATION seconds"
echo ""

# Cleanup function
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

# Clean up old logs and audio files
echo "[1/6] Cleaning up old logs and audio files..."
rm -f /tmp/server_local.log /tmp/client1_local.log /tmp/client2_local.log /tmp/audio_*.wav 2>/dev/null || true

# Kill any existing processes
echo "[2/6] Killing existing ascii-chat processes..."
pkill -x ascii-chat 2>/dev/null || true
sleep 1

# Build if needed
if [ ! -f "$BIN" ]; then
  echo "[ERROR] Binary not found: $BIN"
  echo "Please run: cmake --build build --target ascii-chat"
  exit 1
fi

# Start server on localhost
echo "[3/6] Starting server on localhost:$PORT..."
$BIN --log-file /tmp/server_local.log server 127.0.0.1 --port $PORT > /dev/null 2>&1 &
SERVER_PID=$!
sleep 2

# Verify server is listening
if ! nc -z localhost $PORT 2>/dev/null; then
  echo "[ERROR] Server failed to start"
  kill $SERVER_PID 2>/dev/null || true
  exit 1
fi
echo "Server started (PID: $SERVER_PID)"

# Start client 1 with audio capture and analysis
echo "[4/6] Starting client 1 with audio capture..."
timeout $((DURATION + 5)) $BIN --log-file /tmp/client1_local.log client localhost:$PORT --test-pattern --audio --audio-analysis > /dev/null 2>&1 &
CLIENT1_PID=$!
sleep 2

# Start client 2 with audio capture (no analysis to avoid confusion)
echo "[5/6] Starting client 2 with audio capture..."
timeout $((DURATION + 5)) $BIN --log-file /tmp/client2_local.log client localhost:$PORT --test-pattern --audio > /dev/null 2>&1 &
CLIENT2_PID=$!
sleep 2

echo "Both clients started, recording for $DURATION seconds..."
sleep $DURATION

# Wait for clients to finish and write analysis report
echo "[6/6] Waiting for clients to finish..."
sleep 5

# Kill processes gracefully
echo "Stopping clients and server..."
kill $CLIENT1_PID 2>/dev/null || true
kill $CLIENT2_PID 2>/dev/null || true
sleep 1
kill $SERVER_PID 2>/dev/null || true
sleep 1

# Check for received audio WAV files
echo ""
echo "========================================="
echo "Audio files (Client 1):"
if [ -f /tmp/audio_playback_received.wav ]; then
  echo "✓ Received audio: /tmp/audio_playback_received.wav"
  ls -lh /tmp/audio_playback_received.wav
else
  echo "✗ No received audio file found"
fi

if [ -f /tmp/audio_input_captured.wav ]; then
  echo "✓ Captured audio: /tmp/audio_input_captured.wav"
  ls -lh /tmp/audio_input_captured.wav
else
  echo "✗ No captured audio file found"
fi

echo ""
echo "========================================="
echo "            CLIENT 1 AUDIO ANALYSIS"
echo "========================================="
grep -A 80 'AUDIO ANALYSIS REPORT' /tmp/client1_local.log 2>/dev/null || echo "No analysis report found"

echo ""
echo "Full logs available at:"
echo "  /tmp/server_local.log"
echo "  /tmp/client1_local.log"
echo "  /tmp/client2_local.log"
