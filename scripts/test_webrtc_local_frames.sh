#!/bin/bash
# Local WebRTC frame transmission test

set -e

cd "$(dirname "$0")/.."
REPO="$(pwd)"
BUILD="$REPO/build"
BIN="$BUILD/bin"

PASSWORD="test-$(date +%s)"
SERVER_PORT=27224
ACDS_PORT=27225
TEST_TIMEOUT=15
REMOTE_HOST="sidechain"
REMOTE_REPO="/opt/ascii-chat"

echo "╔════════════════════════════════════════════════════════════════════════╗"
echo "║  LOCAL WebRTC Frame Transmission Test (with Remote ACDS)             ║"
echo "╚════════════════════════════════════════════════════════════════════════╝"
echo ""

# Cleanup on exit
cleanup() {
  pkill -9 ascii-chat 2>/dev/null || true
  pkill -9 discovery-server 2>/dev/null || true
  timeout 5 ssh -n $REMOTE_HOST "pkill -9 acds" 2>/dev/null || true
  kill $(jobs -p) 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# Clean logs
rm -f /tmp/server_test.log /tmp/client_test.log /tmp/acds_test.log

echo "[1/4] Starting remote ACDS server on $REMOTE_HOST..."
timeout 5 ssh -n $REMOTE_HOST "pkill -9 acds" 2>/dev/null || true
sleep 1
ssh -n $REMOTE_HOST "cd $REMOTE_REPO && ./build/bin/acds --port $ACDS_PORT > /tmp/acds_remote.log 2>&1 &" &
sleep 2
echo "✓ Remote ACDS started"
echo ""

echo "[2/4] Starting server..."
WEBCAM_DISABLED=1 timeout $((TEST_TIMEOUT + 5)) $BIN/ascii-chat \
  --log-file /tmp/server_test.log \
  server 127.0.0.1 \
  --port $SERVER_PORT \
  --password "$PASSWORD" \
  > /dev/null 2>&1 &
SERVER_PID=$!
sleep 2
echo "✓ Server started (PID=$SERVER_PID)"
echo ""

echo "[3/5] Waiting for server to be ready..."
sleep 1
echo "✓ Server ready"
echo ""

echo "[4/5] Running client with frame transmission via WebRTC..."
echo "═════════════════════════════════════════════════════════════════"
WEBCAM_DISABLED=1 timeout $TEST_TIMEOUT $BIN/ascii-chat \
  --log-file /tmp/client_test.log \
  client 127.0.0.1:$SERVER_PORT \
  --password "$PASSWORD" \
  --no-encrypt \
  --test-pattern \
  --color-mode mono \
  --prefer-webrtc \
  --acds-insecure \
  --acds-server discovery-server.ascii-chat.com \
  --acds-port $ACDS_PORT \
  --snapshot \
  --snapshot-delay 5 \
  > /tmp/webrtc_frame_output.txt 2>&1 || true

echo "═════════════════════════════════════════════════════════════════"
echo ""
echo "Log Analysis:"
echo "───────────────────────────────────────────────────────────────────"

echo ""
echo "★ CLIENT SIDE - Packet Sending:"
grep "★" /tmp/client_test.log 2>/dev/null | grep -E "SEND|ACIP_SEND|PACKET_SEND" | head -20 || echo "  No send logs found"

echo ""
echo "★ SERVER SIDE - Packet Receiving:"
grep "★" /tmp/server_test.log 2>/dev/null | grep -E "WEBRTC_ON_MESSAGE|WEBRTC_RECV" | head -20 || echo "  No receive logs found"

echo ""
echo "Summary:"
echo "───────────────────────────────────────────────────────────────────"

SERVER_IMAGES=$(grep -c "PACKET_SEND_VIA_TRANSPORT: type=2" /tmp/client_test.log 2>/dev/null || echo "0")
echo "Client attempted to send IMAGE_FRAME packets: $SERVER_IMAGES times"

RECEIVED=$(grep "★ WEBRTC_ON_MESSAGE:" /tmp/server_test.log 2>/dev/null | wc -l || echo "0")
echo "Server received callback invocations: $RECEIVED times"

LARGE_PACKETS=$(grep "★ WEBRTC_ON_MESSAGE:" /tmp/server_test.log 2>/dev/null | grep -oP 'len=\K[0-9]+' | awk '$1 > 10000' | wc -l || echo "0")
echo "Large packets (>10KB) received: $LARGE_PACKETS times"

MAX_PACKET=$(grep "★ WEBRTC_ON_MESSAGE:" /tmp/server_test.log 2>/dev/null | grep -oP 'len=\K[0-9]+' | sort -n | tail -1 || echo "0")
echo "Maximum packet size received: $MAX_PACKET bytes"

echo ""
echo "Full logs:"
echo "  Client: /tmp/client_test.log"
echo "  Server: /tmp/server_test.log"
echo ""

echo ""
echo "[5/5] Frame Output:"
echo "───────────────────────────────────────────────────────────────────"
if [ -s /tmp/webrtc_frame_output.txt ]; then
  FRAME_SIZE=$(wc -c < /tmp/webrtc_frame_output.txt)
  FRAME_LINES=$(wc -l < /tmp/webrtc_frame_output.txt)
  echo "✓ Frame captured!"
  echo "  Size: $FRAME_SIZE bytes"
  echo "  Lines: $FRAME_LINES"
  echo ""
  echo "First 35 lines:"
  head -35 /tmp/webrtc_frame_output.txt | grep -v "^\["
else
  echo "✗ No frame output captured"
fi
echo ""
