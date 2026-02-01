#!/bin/bash
#
# Quick WebRTC Test - 15 seconds total with strict timeouts
#
set -e

REMOTE_HOST="sidechain"
REMOTE_REPO="/opt/ascii-chat"
PASSWORD="test-$(date +%s)"
SERVER_PORT=27224
ACDS_PORT=27225

echo "=== Quick WebRTC Test (15s max) ==="

# Cleanup
timeout 3 ssh -o ConnectTimeout=2 $REMOTE_HOST "pkill -9 ascii-chat" 2>/dev/null || true

# Start ACDS
echo "[1/3] Starting ACDS..."
timeout 5 ssh -f -o ConnectTimeout=2 $REMOTE_HOST "cd $REMOTE_REPO && nohup ./build/bin/ascii-chat discovery-server --port $ACDS_PORT > /tmp/acds.log 2>&1 &"
sleep 1

# Start server with WebRTC (use public IP for ACDS so both can connect)
echo "[2/3] Starting server..."
timeout 5 ssh -f -o ConnectTimeout=2 $REMOTE_HOST "cd $REMOTE_REPO && WEBCAM_DISABLED=1 nohup timeout 20 ./build/bin/ascii-chat server 0.0.0.0 --port $SERVER_PORT --password '$PASSWORD' --webrtc --acds --acds-server 135.181.27.224 --acds-port $ACDS_PORT > /tmp/server.log 2>&1 &"
sleep 2

# Get session string
SESSION_STRING=$(timeout 3 ssh -o ConnectTimeout=2 $REMOTE_HOST "grep -i 'Session String:' /tmp/server.log | tail -1 | awk '{print \$NF}'")
echo "Session: $SESSION_STRING"

# Test WebRTC+STUN (5 second timeout)
echo "[3/3] Testing WebRTC + STUN..."
timeout 10 ./build/bin/ascii-chat client "$SESSION_STRING" \
  --password "$PASSWORD" \
  --prefer-webrtc --stun-servers "stun:stun.ascii-chat.com:3478" \
  --turn-servers "turn:turn.ascii-chat.com:3478" \
  --turn-username "ascii" --turn-credential "0aa9917b4dad1b01631e87a32b875e09" \
  --acds-insecure --acds-server 135.181.27.224 --acds-port $ACDS_PORT \
  --test-pattern \
  --snapshot --snapshot-delay 2 \
  --log-file /tmp/webrtc_test.log > /dev/null 2>&1 || true

# Check results
if grep -q "WebRTC connection established" /tmp/webrtc_test.log 2>/dev/null; then
  echo "✅ WebRTC STUN test PASSED"
  EXIT_CODE=0
elif grep -q "Direct TCP connection established" /tmp/webrtc_test.log 2>/dev/null; then
  echo "⚠️  Fell back to TCP (WebRTC didn't work)"
  EXIT_CODE=1
else
  echo "❌ Connection failed"
  EXIT_CODE=2
fi

# Cleanup
timeout 3 ssh -o ConnectTimeout=2 $REMOTE_HOST "pkill -9 ascii-chat" 2>/dev/null || true

exit $EXIT_CODE
