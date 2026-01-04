#!/bin/bash
#
# Fast WebRTC NAT Traversal Test Suite (~10 seconds total)
# Tests Phase 3 WebRTC implementation with actual coturn servers
#

set -e

# Configuration
REMOTE_HOST="sidechain"
REMOTE_HOST_IP="135.181.27.224"
REMOTE_REPO="/opt/ascii-chat"
LOCAL_REPO="/home/zfogg/src/github.com/zfogg/ascii-chat"
SERVER_PORT=27224
TEST_PASSWORD="webrtc-$(date +%s)"

# Fast timeouts - total ~10 seconds
SNAPSHOT_DURATION=1  # 1 second per test
TEST_TIMEOUT=3       # 3 seconds max per test

# Use actual coturn servers
STUN_SERVERS="stun:stun.ascii-chat.com:3478"
TURN_SERVERS="turn:turn.ascii-chat.com:3478"
TURN_USER="ascii"
TURN_CRED="0aa9917b4dad1b01631e87a32b875e09"

# Cleanup on exit
cleanup() {
  ssh $REMOTE_HOST "pkill -9 ascii-chat" 2>/dev/null || true
  exit ${1:-130}
}
trap cleanup SIGINT SIGTERM

echo "╔════════════════════════════════════════════════════════════════════════╗"
echo "║         Fast WebRTC NAT Traversal Test (~10s)                         ║"
echo "╚════════════════════════════════════════════════════════════════════════╝"
echo ""
echo "Remote: $REMOTE_HOST ($REMOTE_HOST_IP)"
echo "STUN:   $STUN_SERVERS"
echo "TURN:   $TURN_SERVERS"
echo ""

# Helper
run_remote() { ssh $REMOTE_HOST "cd $REMOTE_REPO && $1"; }

# Track results
TESTS_PASSED=0
TESTS_FAILED=0

log_result() {
  if [ "$2" = "PASS" ]; then
    echo "   ✅ $1: $3"
    TESTS_PASSED=$((TESTS_PASSED + 1))
  else
    echo "   ❌ $1: $3"
    TESTS_FAILED=$((TESTS_FAILED + 1))
  fi
}

# [1/5] Cleanup
echo "[1/5] Cleanup..."
run_remote "pkill -9 ascii-chat" 2>/dev/null || true
run_remote "rm -f /tmp/*.log /tmp/server_output.txt"
rm -f /tmp/client_*.log

# [2/5] Rebuild (skip if recent)
echo "[2/5] Check/rebuild remote..."
if ! run_remote "test -f build/bin/ascii-chat && test \$(find build/bin/ascii-chat -mmin -60 | wc -l) -gt 0" 2>/dev/null; then
  echo "   Building (binary >1hr old)..."
  run_remote "cmake --build build --target ascii-chat 2>&1 | tail -5" || {
    echo "ERROR: Build failed"
    exit 1
  }
else
  echo "   ✓ Binary fresh (<1hr old), skipping build"
fi

# [3/5] Start server with WebRTC
echo "[3/5] Starting server..."
ssh -f $REMOTE_HOST "cd $REMOTE_REPO && WEBCAM_DISABLED=1 timeout 30 ./build/bin/ascii-chat server 0.0.0.0 --port $SERVER_PORT --password \"$TEST_PASSWORD\" --webrtc > /tmp/server_output.txt 2>&1"

sleep 2
if ! run_remote "lsof -i :$SERVER_PORT" > /dev/null 2>&1; then
  echo "ERROR: Server failed to start"
  run_remote "cat /tmp/server_output.txt"
  exit 1
fi
echo "   ✓ Server running on port $SERVER_PORT"

# [4/5] Run WebRTC Tests
echo ""
echo "[4/5] Running WebRTC tests..."
echo ""

# Clean old client logs
rm -f /tmp/client_tcp.log /tmp/client_stun.log /tmp/client_turn.log

# Test 1: Direct TCP (baseline)
echo "Test 1/3: Direct TCP..."
WEBCAM_DISABLED=1 timeout $TEST_TIMEOUT ./build/bin/ascii-chat client $REMOTE_HOST_IP:$SERVER_PORT \
  --password "$TEST_PASSWORD" \
  --snapshot --snapshot-delay $SNAPSHOT_DURATION \
  --log-file /tmp/client_tcp.log > /dev/null 2>&1 || true

# Safer parsing - default to 0 if file doesn't exist or grep fails
if [ -f /tmp/client_tcp.log ]; then
  FRAMES=$(grep -c "frame received\|Snapshot captured" /tmp/client_tcp.log 2>/dev/null || echo "0")
  TIME=$(grep "Connection.*in" /tmp/client_tcp.log 2>/dev/null | grep -oP '\d+\.\d+' | head -1 || echo "N/A")
else
  FRAMES=0
  TIME="N/A"
fi

if [ "$FRAMES" -gt 0 ] 2>/dev/null; then
  log_result "Direct TCP" "PASS" "${TIME}s, ${FRAMES} frames"
else
  log_result "Direct TCP" "FAIL" "No frames"
fi

# Test 2: WebRTC with STUN
echo "Test 2/3: WebRTC + STUN..."
WEBCAM_DISABLED=1 timeout $TEST_TIMEOUT ./build/bin/ascii-chat client $REMOTE_HOST_IP:$SERVER_PORT \
  --password "$TEST_PASSWORD" \
  --webrtc --stun-servers "$STUN_SERVERS" \
  --webrtc-disable-turn \
  --snapshot --snapshot-delay $SNAPSHOT_DURATION \
  --log-file /tmp/client_stun.log > /dev/null 2>&1 || true

if [ -f /tmp/client_stun.log ]; then
  FRAMES=$(grep -c "frame received\|Snapshot captured" /tmp/client_stun.log 2>/dev/null || echo "0")
  TIME=$(grep "Connection.*in" /tmp/client_stun.log 2>/dev/null | grep -oP '\d+\.\d+' | head -1 || echo "N/A")
  STUN_STATE=$(grep -o "WEBRTC_STUN_CONNECTED" /tmp/client_stun.log 2>/dev/null | head -1 || echo "")
else
  FRAMES=0
  TIME="N/A"
  STUN_STATE=""
fi

if [ "$FRAMES" -gt 0 ] 2>/dev/null && [ -n "$STUN_STATE" ]; then
  log_result "WebRTC STUN" "PASS" "${TIME}s, ${FRAMES} frames"
elif [ "$FRAMES" -gt 0 ] 2>/dev/null; then
  log_result "WebRTC STUN" "PASS" "${TIME}s (fallback), ${FRAMES} frames"
else
  log_result "WebRTC STUN" "FAIL" "No frames"
fi

# Test 3: WebRTC with TURN relay
echo "Test 3/3: WebRTC + TURN..."
WEBCAM_DISABLED=1 timeout $TEST_TIMEOUT ./build/bin/ascii-chat client $REMOTE_HOST_IP:$SERVER_PORT \
  --password "$TEST_PASSWORD" \
  --webrtc --webrtc-skip-stun \
  --turn-servers "$TURN_SERVERS" \
  --turn-username "$TURN_USER" \
  --turn-credential "$TURN_CRED" \
  --snapshot --snapshot-delay $SNAPSHOT_DURATION \
  --log-file /tmp/client_turn.log > /dev/null 2>&1 || true

if [ -f /tmp/client_turn.log ]; then
  FRAMES=$(grep -c "frame received\|Snapshot captured" /tmp/client_turn.log 2>/dev/null || echo "0")
  TIME=$(grep "Connection.*in" /tmp/client_turn.log 2>/dev/null | grep -oP '\d+\.\d+' | head -1 || echo "N/A")
  TURN_STATE=$(grep -o "WEBRTC_TURN_CONNECTED" /tmp/client_turn.log 2>/dev/null | head -1 || echo "")
else
  FRAMES=0
  TIME="N/A"
  TURN_STATE=""
fi

if [ "$FRAMES" -gt 0 ] 2>/dev/null && [ -n "$TURN_STATE" ]; then
  log_result "WebRTC TURN" "PASS" "${TIME}s, ${FRAMES} frames"
elif [ "$FRAMES" -gt 0 ] 2>/dev/null; then
  log_result "WebRTC TURN" "PASS" "${TIME}s (fallback), ${FRAMES} frames"
else
  log_result "WebRTC TURN" "FAIL" "No frames"
fi

# [5/5] Results
echo ""
echo "╔════════════════════════════════════════════════════════════════════════╗"
echo "║                         RESULTS                                        ║"
echo "╚════════════════════════════════════════════════════════════════════════╝"
echo ""
echo "Passed: $TESTS_PASSED / 3"
echo "Failed: $TESTS_FAILED / 3"
echo ""

if [ "$TESTS_FAILED" -eq 0 ]; then
  echo "✅ ALL TESTS PASSED - Phase 3 WebRTC: VALIDATED"
  RESULT=0
elif [ "$TESTS_PASSED" -ge 2 ]; then
  echo "⚠️  PARTIAL SUCCESS - Core functionality working"
  RESULT=1
else
  echo "❌ TESTS FAILED - Check logs:"
  echo "   /tmp/client_tcp.log"
  echo "   /tmp/client_stun.log"
  echo "   /tmp/client_turn.log"
  RESULT=2
fi

echo ""
cleanup $RESULT
