#!/bin/bash
#
# Run on HOST_TWO (manjaro-twopal) - Client 2 only
#

set -e

PORT=37228
HOST_ONE_IP="100.79.232.55"  # BeaglePlay Tailscale IP
DURATION=30

echo "Starting client on HOST_TWO (manjaro-twopal)"
echo "Connecting to server at $HOST_ONE_IP:$PORT"

# Kill existing processes
pkill -x ascii-chat 2>/dev/null || true
sleep 1

# Clean logs
rm -f /tmp/client2_debug.log /tmp/audio_*.wav /tmp/aec3_*.wav

# Rebuild
cmake -B build -DCMAKE_BUILD_TYPE=Dev
cmake --build build --target ascii-chat 2>&1 | tail -5

# Wait for server to be reachable
echo "Waiting for server to be reachable..."
MAX_WAIT=30
for i in $(seq 1 $MAX_WAIT); do
  if nc -z $HOST_ONE_IP $PORT 2>/dev/null; then
    echo "Server is reachable! (took ${i}s)"
    break
  fi
  if [ $i -eq $MAX_WAIT ]; then
    echo "ERROR: Cannot reach server at $HOST_ONE_IP:$PORT"
    exit 1
  fi
  sleep 1
done

# Start client 2
echo "Starting client 2 with audio"
ASCIICHAT_DUMP_AUDIO=1 ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK=1 \
  timeout $((DURATION + 5)) ./build/bin/ascii-chat \
  --log-file /tmp/client2_debug.log \
  client $HOST_ONE_IP:$PORT \
  --test-pattern --audio --audio-analysis

echo ""
echo "Test complete. Analyzing..."

# Show results
echo ""
echo "=========================================="
echo "CLIENT 2 AUDIO ANALYSIS"
echo "=========================================="
grep -A 80 'AUDIO ANALYSIS REPORT' /tmp/client2_debug.log 2>/dev/null || echo "Report not found"

echo ""
echo "Full logs: /tmp/client2_debug.log"
