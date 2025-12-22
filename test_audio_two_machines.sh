#!/bin/bash

set -e

PORT=27228
LOCAL_IP="192.168.1.190"
REMOTE_SSH="manjaro-twopal"
REMOTE_BIN="/home/zfogg/src/github.com/zfogg/ascii-chat/build_debug/bin/ascii-chat"
DURATION=10

# Disable crypto verification for testing
export ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK=1

echo "Cross-machine audio analysis test"
echo "=================================="
echo "Local IP: $LOCAL_IP"
echo "Remote: $REMOTE_SSH"
echo "Duration: $DURATION seconds"
echo ""

# Kill any existing processes
pkill -f "ascii-chat.*$PORT" || true
sleep 1

# Start server locally
echo "[1/4] Starting server on port $PORT (with --no-audio-mixer to bypass mixer bottleneck)..."
timeout $((DURATION + 15)) ./build/bin/ascii-chat server --address 0.0.0.0 --port $PORT --no-audio-mixer > /tmp/server_debug.log 2>&1 &
SERVER_PID=$!
sleep 2

# Start client 1 locally
echo "[2/4] Starting client 1 (LOCAL macOS) with audio and analysis..."
COLUMNS=40 LINES=12 timeout $DURATION ./build/bin/ascii-chat client \
  --address 127.0.0.1 \
  --port $PORT \
  --audio \
  --audio-analysis \
  --snapshot \
  --snapshot-delay 3 \
  --log-file /tmp/client1_debug.log 2>&1 &
CLIENT1_PID=$!
sleep 2

# Start client 2 on remote with Debug build
echo "[3/4] Starting client 2 (REMOTE Manjaro) with audio and analysis..."
ssh $REMOTE_SSH "ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK=1 timeout $DURATION $REMOTE_BIN client \
  --address $LOCAL_IP \
  --port $PORT \
  --audio \
  --audio-analysis \
  --snapshot \
  --snapshot-delay 3 \
  --log-file /tmp/client2_debug.log" > /tmp/remote_output_debug.log 2>&1 &
CLIENT2_PID=$!

echo "[4/4] Running test for $DURATION seconds..."
echo "Play music or speak on BOTH machines!"
echo "========================================"
echo ""

sleep $DURATION

wait $CLIENT1_PID 2>/dev/null || true
wait $CLIENT2_PID 2>/dev/null || true
kill $SERVER_PID 2>/dev/null || true
sleep 2

echo ""
echo "=========================================================================="
echo "                    CLIENT 1 (LOCAL macOS) AUDIO ANALYSIS"
echo "=========================================================================="
grep -A 20 "AUDIO ANALYSIS REPORT" /tmp/client1_debug.log 2>/dev/null || echo "Report not found"

echo ""
echo "=========================================================================="
echo "                   CLIENT 2 (REMOTE Manjaro) AUDIO ANALYSIS"
echo "=========================================================================="
ssh $REMOTE_SSH "grep -A 20 'AUDIO ANALYSIS REPORT' /tmp/client2_debug.log 2>/dev/null || echo 'Report not found'" | head -40

echo ""
echo "Full logs available at:"
echo "  Local server: /tmp/server_debug.log"
echo "  Local client: /tmp/client1_debug.log"
echo "  Remote client: $REMOTE_SSH:/tmp/client2_debug.log"
