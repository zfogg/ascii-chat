#!/bin/bash

set -e

PORT=27228
LOCAL_IP="127.0.0.1"
DURATION=10

pkill -7 -f "./build/bin/ascii-chat" || true

export ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK=1

echo "Local audio analysis test - SNAPSHOT mode"
echo "========================================="
echo "Local IP: $LOCAL_IP"
echo "Duration: $DURATION seconds"
echo ""

# Kill any existing processes first
for pid in $(lsof -ti :$PORT 2>/dev/null); do
  kill -15 $pid 2>/dev/null || true
done
sleep 0.5

# Start server locally
echo "[1/4] Starting server on port $PORT..."
timeout $((DURATION + 15)) ./build/bin/ascii-chat server --address 0.0.0.0 --port $PORT > /tmp/server_local_continuous.log 2>&1 &
SERVER_PID=$!
sleep 1

# Start client 1 locally with snapshot mode
echo "[2/4] Starting client 1 (LOCAL) with audio and analysis - SNAPSHOT..."
ASCIICHAT_DUMP_AUDIO=1 COLUMNS=40 LINES=12 timeout $((DURATION + 5)) ./build/bin/ascii-chat client \
  --address $LOCAL_IP \
  --port $PORT \
  --audio \
  --audio-analysis \
  --snapshot \
  --snapshot-delay $DURATION \
  --log-file /tmp/client1_local_continuous.log 2>&1 &
CLIENT1_PID=$!
sleep 0.5

# Start client 2 locally with snapshot mode
echo "[3/4] Starting client 2 (LOCAL) with audio and analysis - SNAPSHOT..."
ASCIICHAT_DUMP_AUDIO=1 COLUMNS=40 LINES=12 timeout $((DURATION + 5)) ./build/bin/ascii-chat client \
  --address $LOCAL_IP \
  --port $PORT \
  --audio \
  --audio-analysis \
  --snapshot \
  --snapshot-delay $DURATION \
  --log-file /tmp/client2_local_continuous.log 2>&1 &
CLIENT2_PID=$!

echo "[4/4] Running test for $DURATION seconds..."
echo "Playing test audio on macOS to be captured..."
echo "========================================"
echo ""

# Play test audio in background during the test
(
  sleep 0.5
  for i in $(seq 1 $((DURATION/2))); do
    afplay /System/Library/Sounds/Submarine.aiff 2>/dev/null || true
    sleep 1.8
  done
) &
AUDIO_PID=$!

# Wait for clients to complete naturally (snapshot mode will exit automatically)
wait $CLIENT1_PID 2>/dev/null || true
wait $CLIENT2_PID 2>/dev/null || true
kill $AUDIO_PID 2>/dev/null || true
kill $SERVER_PID 2>/dev/null || true

echo ""
echo "=========================================================================="
echo "                    CLIENT 1 (LOCAL) AUDIO ANALYSIS"
echo "=========================================================================="
grep -A 50 "AUDIO ANALYSIS REPORT" /tmp/client1_local_continuous.log 2>/dev/null || echo "Report not found"

echo ""
echo "=========================================================================="
echo "                    CLIENT 2 (LOCAL) AUDIO ANALYSIS"
echo "=========================================================================="
grep -A 50 "AUDIO ANALYSIS REPORT" /tmp/client2_local_continuous.log 2>/dev/null || echo "Report not found"

echo ""
echo "Full logs available at:"
echo "  Local server: /tmp/server_local_continuous.log"
echo "  Local client 1: /tmp/client1_local_continuous.log"
echo "  Local client 2: /tmp/client2_local_continuous.log"
