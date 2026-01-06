#!/bin/bash
#
# Complete WebRTC Test with ACDS Signaling
# Tests the full Phase 3 WebRTC integration
#

set -e

REMOTE_HOST="sidechain"
REMOTE_HOST_IP="135.181.27.224"
REMOTE_REPO="/opt/ascii-chat"
ACDS_PORT=27225
SERVER_PORT=27224
TEST_PASSWORD="webrtc-full-$(date +%s)"

cleanup() {
  echo "Cleaning up..."
  ssh $REMOTE_HOST "pkill -9 acds; pkill -9 ascii-chat" 2>/dev/null || true
  exit ${1:-0}
}
trap cleanup SIGINT SIGTERM

echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║         WebRTC Integration Test with ACDS Signaling          ║"
echo "╚═══════════════════════════════════════════════════════════════╝"
echo ""

# [1/6] Cleanup
echo "[1/6] Cleanup..."
ssh $REMOTE_HOST "pkill -9 acds; pkill -9 ascii-chat; rm -f /tmp/*.log /tmp/server_output.txt" 2>/dev/null || true
sleep 2

# [2/6] Start ACDS
echo "[2/6] Starting ACDS on sidechain..."
ssh -f $REMOTE_HOST "cd $REMOTE_REPO && ./build/bin/acds --log-level debug --log-file /tmp/acds.log"
sleep 2

if ! ssh $REMOTE_HOST "lsof -i :$ACDS_PORT" > /dev/null 2>&1; then
  echo "ERROR: ACDS failed to start"
  cleanup 1
fi
echo "   ✓ ACDS listening on port $ACDS_PORT"

# [3/6] Start server with WebRTC + ACDS
echo "[3/6] Starting server with WebRTC + ACDS..."
ssh -f $REMOTE_HOST "cd $REMOTE_REPO && WEBCAM_DISABLED=1 timeout 60 ./build/bin/ascii-chat server 0.0.0.0 --port $SERVER_PORT --password '$TEST_PASSWORD' --webrtc --acds --acds-server $REMOTE_HOST_IP --acds-expose-ip > /tmp/server_output.txt 2>&1"
sleep 5

if ! ssh $REMOTE_HOST "lsof -i :$SERVER_PORT" > /dev/null 2>&1; then
  echo "ERROR: Server failed to start"
  ssh $REMOTE_HOST "cat /tmp/server_output.txt"
  cleanup 1
fi
echo "   ✓ Server running on port $SERVER_PORT"

# [4/6] Extract session string
echo "[4/6] Extracting session string..."
SESSION=$(ssh $REMOTE_HOST "grep 'Session string:' /tmp/server_output.txt | tail -1 | awk '{print \$NF}'" || echo "")
if [ -z "$SESSION" ]; then
  echo "ERROR: Failed to extract session string"
  ssh $REMOTE_HOST "cat /tmp/server_output.txt"
  cleanup 1
fi
echo "   Session: $SESSION"

# [5/6] Test WebRTC connection with session string
echo ""
echo "[5/6] Testing WebRTC connection via session..."
echo "   Method: Direct IP with --webrtc flag"
echo ""

WEBCAM_DISABLED=1 timeout 15 ./build/bin/ascii-chat client $REMOTE_HOST_IP:$SERVER_PORT \
  --password "$TEST_PASSWORD" \
  --webrtc \
  --stun-servers "stun:stun.ascii-chat.com:3478" \
  --snapshot --snapshot-delay 3 \
  --log-file /tmp/client_webrtc_test.log > /dev/null 2>&1 || true

# [6/6] Analyze results
echo ""
echo "[6/6] Results..."
echo ""

if [ -f /tmp/client_webrtc_test.log ]; then
  FRAMES=$(grep -c "Snapshot.*received\|First.*frame.*received" /tmp/client_webrtc_test.log 2>/dev/null || echo "0")
  TCP_CONN=$(grep -c "TCP.*established\|Connection established" /tmp/client_webrtc_test.log 2>/dev/null || echo "0")
  WEBRTC_INIT=$(grep -c "WebRTC.*initialized" /tmp/client_webrtc_test.log 2>/dev/null || echo "0")

  echo "Results:"
  echo "  WebRTC initialized: $([ "$WEBRTC_INIT" -gt 0 ] && echo "✅ Yes" || echo "❌ No")"
  echo "  TCP connection: $([ "$TCP_CONN" -gt 0 ] && echo "✅ Established" || echo "❌ Failed")"
  echo "  Frames received: $FRAMES $([ "$FRAMES" -gt 0 ] && echo "✅" || echo "❌")"

  if [ "$FRAMES" -gt 0 ]; then
    echo ""
    echo "✅ TEST PASSED - WebRTC integration working!"
    cleanup 0
  else
    echo ""
    echo "⚠️  Connection established but no frames received"
    echo ""
    echo "Last 30 lines of client log:"
    tail -30 /tmp/client_webrtc_test.log
    cleanup 1
  fi
else
  echo "❌ No client log found"
  cleanup 1
fi
