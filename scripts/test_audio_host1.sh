#!/bin/bash
#
# Run on HOST_ONE (BeaglePlay) - Server + Client 1
#

set -e

PORT=37228
DURATION=30

echo "Starting server + client on HOST_ONE (BeaglePlay)"

# Kill existing processes
pkill -x ascii-chat 2>/dev/null || true
sleep 1

# Clean logs
rm -f /tmp/server_debug.log /tmp/client1_debug.log /tmp/audio_*.wav /tmp/aec3_*.wav

# Rebuild
cmake --build build --target ascii-chat 2>&1 | tail -5

# Start server on all interfaces
echo "Starting server on 0.0.0.0:$PORT"
timeout $((DURATION + 10)) ./build/bin/ascii-chat --log-file /tmp/server_debug.log server 0.0.0.0 --port $PORT > /dev/null 2>&1 &
SERVER_PID=$!

# Wait for server
sleep 3
if ! nc -z localhost $PORT; then
  echo "ERROR: Server not listening"
  tail -20 /tmp/server_debug.log
  exit 1
fi

echo "Server running on 0.0.0.0:$PORT"
echo "HOST_TWO should now run: ./scripts/test_audio_host2.sh"
echo ""
echo "Starting local client in 5 seconds..."
sleep 5

# Start client 1
echo "Starting client 1 with audio"
ASCIICHAT_DUMP_AUDIO=1 ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK=1 \
  timeout $((DURATION + 5)) ./build/bin/ascii-chat \
  --log-file /tmp/client1_debug.log \
  client localhost:$PORT \
  --test-pattern --audio --audio-analysis

echo ""
echo "Test complete. Analyzing..."

# Show results
echo ""
echo "=========================================="
echo "CLIENT 1 AUDIO ANALYSIS"
echo "=========================================="
grep -A 80 'AUDIO ANALYSIS REPORT' /tmp/client1_debug.log 2>/dev/null || echo "Report not found"

echo ""
echo "Full logs: /tmp/server_debug.log, /tmp/client1_debug.log"
