#!/bin/bash
# Detailed WebRTC frame flow diagnostic test
# Helps identify where frames are being dropped

set -e

REMOTE_HOST="${REMOTE_HOST:-sidechain}"
REMOTE_HOST_IP="${REMOTE_HOST_IP:-135.181.27.224}"
REMOTE_REPO="${REMOTE_REPO:-/opt/ascii-chat}"
LOCAL_REPO="$(pwd)"
SERVER_PORT=27224
ACDS_PORT=27225
TEST_PASSWORD="webrtc-$(date +%s)"
TEST_TIMEOUT=20

STUN_SERVERS="stun:stun.ascii-chat.com:3478"
TURN_SERVERS="turn:turn.ascii-chat.com:3478"
TURN_USER="ascii"
TURN_CRED="0aa9917b4dad1b01631e87a32b875e09"

cleanup() {
  timeout 5 ssh -n $REMOTE_HOST "pkill -9 ascii-chat" 2>/dev/null || true
  kill $(jobs -p) 2>/dev/null || true
  exit ${1:-130}
}
trap cleanup SIGINT SIGTERM

echo "╔════════════════════════════════════════════════════════════════════════╗"
echo "║  WebRTC Frame Flow Diagnostic Test                                    ║"
echo "╚════════════════════════════════════════════════════════════════════════╝"
echo ""

# Rebuild locally
#echo "[1/5] Rebuilding locally with latest changes..."
#cd "$LOCAL_REPO"
#cmake --preset default -B build 2>&1 | grep -E "error|warning" | head -5 || echo "Build OK"
#cmake --build build 2>&1 | tail -3 | grep -E "error" || echo "Build succeeded"
#echo "✓ Local build done"
#echo ""

# Cleanup and rebuild remote
echo "[2/5] Rebuilding remote server..."
timeout 60 ssh -n $REMOTE_HOST "cd $REMOTE_REPO && cmake --preset default -B build 2>&1 | grep -E 'error|Error' | head -5 || true && cmake --build build --target ascii-chat 2>&1 | tail -3" || true
echo "✓ Remote build done"
echo ""

# Cleanup logs
echo "[3/5] Cleaning up old logs..."
timeout 5 ssh -n $REMOTE_HOST "rm -f /tmp/*.log /tmp/server_output.txt" || true
rm -f /tmp/client_webrtc_detailed.log 2>/dev/null || true
echo "✓ Logs cleaned"
echo ""

# Start ACDS
echo "[4/5] Starting ACDS..."
ssh -n $REMOTE_HOST "cd $REMOTE_REPO && ./build/bin/ascii-chat discovery-service --port $ACDS_PORT > /tmp/acds.log 2>&1 &" &
sleep 1
echo "✓ ACDS started"
echo ""

# Start server with verbose logging
echo "[5/5] Starting server with VERBOSE logging..."
ssh -n $REMOTE_HOST bash -c "cd $REMOTE_REPO && timeout $((TEST_TIMEOUT + 10)) ./build/bin/ascii-chat --log-level debug --log-file /tmp/server_detailed.log server 0.0.0.0 --port $SERVER_PORT --password '$TEST_PASSWORD' --webrtc --discovery > /tmp/server_stdout.log 2>&1 &" &
SERVER_PID=$!
sleep 0.1
echo "✓ Server started"
echo ""

# Get session string
echo "Extracting session string..."
SESSION_STRING=""
for i in {1..15}; do
  SESSION_STRING=$(timeout 5 ssh -n $REMOTE_HOST "grep -i 'Session String:' /tmp/server_output.txt 2>/dev/null | tail -1 | awk '{print \$NF}'" || echo "")
  if [ -z "$SESSION_STRING" ]; then
    # Try alternate log location
    SESSION_STRING=$(timeout 5 ssh -n $REMOTE_HOST "grep -i 'Session String:' /tmp/server_stdout.log 2>/dev/null | tail -1 | awk '{print \$NF}'" || echo "")
  fi
  if [ -n "$SESSION_STRING" ]; then
    break
  fi
  sleep 0.5
done

if [ -z "$SESSION_STRING" ]; then
  echo "ERROR: Failed to get session string. Server logs:"
  timeout 5 ssh -n $REMOTE_HOST "tail -30 /tmp/server_stdout.log" || true
  cleanup 1
fi
echo "✓ Session string: $SESSION_STRING"
echo ""

# Run WebRTC + STUN test with verbose client logging
echo "═══════════════════════════════════════════════════════════════════════"
echo "RUNNING WEBRTC+STUN TEST WITH DETAILED LOGGING"
echo "═══════════════════════════════════════════════════════════════════════"
echo ""

WEBCAM_DISABLED=1 timeout $TEST_TIMEOUT ./build/bin/ascii-chat --log-level debug --log-file /tmp/client_webrtc_detailed.log client "$SESSION_STRING" \
  --password "$TEST_PASSWORD" \
  --no-encrypt \
  --discovery-insecure --discovery-server "$REMOTE_HOST_IP" --discovery-port "$ACDS_PORT" \
  --snapshot --snapshot-delay 0 2>&1 | tee /tmp/client_ascii_output.txt

echo ""
echo "═══════════════════════════════════════════════════════════════════════"
echo "CAPTURED ASCII ART OUTPUT"
echo "═══════════════════════════════════════════════════════════════════════"
echo ""
if [ -f /tmp/client_ascii_output.txt ]; then
  echo "Client ASCII output (first 50 lines):"
  head -50 /tmp/client_ascii_output.txt
  echo ""
  echo "(Full output saved to /tmp/client_ascii_output.txt)"
else
  echo "No ASCII output captured"
fi

echo ""
echo "═══════════════════════════════════════════════════════════════════════"
echo "DIAGNOSTIC OUTPUT"
echo "═══════════════════════════════════════════════════════════════════════"
echo ""

# Check client logs
echo "CLIENT LOGS - Frame transmission:"
echo "─────────────────────────────────────────────────────────────────────"
if [ -f /tmp/client_webrtc_detailed.log ]; then
  echo "✓ client_webrtc_detailed.log exists ($(wc -l < /tmp/client_webrtc_detailed.log) lines)"
  echo ""

  echo "1. Connection establishment:"
  grep -i "connected\|transport set\|server_connection_set_transport" /tmp/client_webrtc_detailed.log 2>/dev/null | head -10 || echo "   (no connection logs)"
  echo ""

  echo "2. Capture thread status:"
  grep -i "capture thread.*active\|capture thread.*waiting\|capture thread.*sending" /tmp/client_webrtc_detailed.log 2>/dev/null | head -20 || echo "   (no capture logs)"
  echo ""

  echo "3. IMAGE_FRAME send attempts:"
  grep -i "IMAGE_FRAME\|acip_send_image_frame" /tmp/client_webrtc_detailed.log 2>/dev/null | head -20 || echo "   (no IMAGE_FRAME send logs)"
  echo ""
else
  echo "✗ client_webrtc_detailed.log NOT FOUND"
fi

# Check server logs
echo "SERVER LOGS - Frame reception:"
echo "─────────────────────────────────────────────────────────────────────"
echo ""

echo "1. Client connection:"
timeout 5 ssh -n $REMOTE_HOST "grep -i 'client.*connected\|webrtc.*connected\|New client' /tmp/server_detailed.log 2>/dev/null | head -10" || echo "   (no client connection logs)"
echo ""

echo "2. WebRTC transport setup:"
timeout 5 ssh -n $REMOTE_HOST "grep -i 'webrtc.*transport\|datachannel\|ice.*state' /tmp/server_detailed.log 2>/dev/null | head -15" || echo "   (no transport logs)"
echo ""

echo "3. Receive thread status:"
timeout 5 ssh -n $REMOTE_HOST "grep -i 'receive thread\|recv.*thread' /tmp/server_detailed.log 2>/dev/null | head -15" || echo "   (no receive thread logs)"
echo ""

echo "4. IMAGE_FRAME packet reception:"
timeout 5 ssh -n $REMOTE_HOST "grep -i 'IMAGE_FRAME\|auto-enabled video\|image.*frame' /tmp/server_detailed.log 2>/dev/null | head -20" || echo "   (no IMAGE_FRAME logs)"
echo ""

echo "5. Render thread status:"
timeout 5 ssh -n $REMOTE_HOST "grep -i 'render thread\|has_video_sources\|create_mixed_ascii' /tmp/server_detailed.log 2>/dev/null | head -15" || echo "   (no render logs)"
echo ""

echo "═══════════════════════════════════════════════════════════════════════"
echo "TEST COMPLETE"
echo "═══════════════════════════════════════════════════════════════════════"
echo ""
echo "Output files:"
echo "  ASCII art:       /tmp/client_ascii_output.txt"
echo "  Client logs:     /tmp/client_webrtc_detailed.log ($([ -f /tmp/client_webrtc_detailed.log ] && wc -l < /tmp/client_webrtc_detailed.log || echo "not found") lines)"
echo "  Remote server:   /tmp/server_detailed.log (use: ssh sidechain tail -100 /tmp/server_detailed.log)"
echo ""

cleanup 0
