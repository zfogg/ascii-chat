#!/bin/bash
#
# ACDS NAT Traversal Integration Test
# Tests RCU hash table fix + full NAT traversal from client behind NAT to public server
#

set -e

# Configuration
REMOTE_HOST="sidechain"
REMOTE_HOST_IP="135.181.27.224"  # Public IP
REMOTE_REPO="/opt/ascii-chat"
LOCAL_REPO="/home/zfogg/src/github.com/zfogg/ascii-chat"
ACDS_PORT=27225
SERVER_PORT=27224
TEST_PASSWORD="nat-traversal-$(date +%s)"
CONNECTION_TEST_DURATION=5  # How long to keep client connected
SNAPSHOT_DELAY=3  # Capture frames for this long

# Cleanup function for Ctrl+C
cleanup() {
  echo ""
  echo "Caught interrupt, cleaning up..."
  ssh $REMOTE_HOST "pkill -9 ascii-chat; pkill -9 acds" 2>/dev/null || true
  echo "Cleanup complete."
  exit 130
}

trap cleanup SIGINT SIGTERM

echo ""
echo "ACDS NAT Traversal Integration Test"
echo "===================================="
echo "Local:  $LOCAL_REPO (behind NAT)"
echo "Remote: $REMOTE_HOST:$REMOTE_REPO ($REMOTE_HOST_IP - public IP)"
echo "Test duration: $CONNECTION_TEST_DURATION seconds"
echo ""

# Helper functions
run_remote() {
  ssh $REMOTE_HOST "cd $REMOTE_REPO && $1"
}

run_bg_remote() {
  ssh -f $REMOTE_HOST "cd $REMOTE_REPO && nohup bash -c '$1' > /dev/null 2>&1"
}

# [1/9] Clean up old processes
echo "[1/9] Killing existing processes on remote..."
run_remote "pkill -9 ascii-chat; pkill -9 acds" 2>/dev/null || true
sleep 1

# [2/9] Clean up old logs
echo "[2/9] Cleaning up old logs..."
run_remote "rm -f /tmp/acds_test.log /tmp/server_test.log /tmp/server_output.txt"

# [3/9] Copy latest code to remote
echo "[3/9] Copying fixed code to remote..."
scp lib/acds/session.c $REMOTE_HOST:$REMOTE_REPO/lib/acds/session.c
scp src/acds/server.c $REMOTE_HOST:$REMOTE_REPO/src/acds/server.c
scp src/server/main.c $REMOTE_HOST:$REMOTE_REPO/src/server/main.c
scp src/client/main.c $REMOTE_HOST:$REMOTE_REPO/src/client/main.c
scp src/client/connection_state.h $REMOTE_HOST:$REMOTE_REPO/src/client/connection_state.h
scp src/client/connection_attempt.c $REMOTE_HOST:$REMOTE_REPO/src/client/connection_attempt.c

# [4/9] Rebuild binaries on remote
echo "[4/9] Rebuilding binaries on remote..."
run_remote "cmake --build build --target acds ascii-chat 2>&1 | tail -10" || {
  echo "ERROR: Build failed on remote"
  exit 1
}

# [5/9] Start ACDS server
echo "[5/9] Starting ACDS server on remote..."
run_bg_remote "./build/bin/acds --log-level debug --log-file /tmp/acds_test.log"

# Verify ACDS is listening
if ! run_remote "lsof -i :$ACDS_PORT" > /dev/null 2>&1; then
  echo "ERROR: ACDS failed to start"
  run_remote "tail -20 /tmp/acds_test.log" || true
  exit 1
fi
echo "   ✓ ACDS listening on port $ACDS_PORT"

# [6/9] Start ascii-chat server with ACDS
echo "[6/9] Starting server with ACDS registration..."
ssh -f $REMOTE_HOST "cd $REMOTE_REPO && WEBCAM_DISABLED=1 nohup ./build/bin/ascii-chat server 0.0.0.0 --log-level debug --log-file /tmp/server_test.log --acds --acds-server $REMOTE_HOST_IP --acds-expose-ip --no-upnp --password \"$TEST_PASSWORD\" > /tmp/server_output.txt 2>&1"

# Verify server is listening
if ! run_remote "lsof -i :$SERVER_PORT" > /dev/null 2>&1; then
  echo "ERROR: Server failed to start"
  run_remote "tail -40 /tmp/server_output.txt" || true
  exit 1
fi
echo "   ✓ Server listening on port $SERVER_PORT"

# [7/9] Extract session string
echo "[7/9] Extracting session string from server..."
SESSION=$(run_remote "grep '📋 Session string:' /tmp/server_output.txt 2>/dev/null | head -1 | awk '{print \$NF}' || echo ''")

if [ -z "$SESSION" ]; then
  echo "ERROR: Failed to extract session string"
  echo "=== Server Output ==="
  run_remote "cat /tmp/server_output.txt"
  exit 1
fi
echo "   Session: $SESSION"

# [8/9] Test NAT traversal - client behind NAT connects to public server
echo "[8/9] Testing NAT traversal (client behind NAT → public server)..."
echo "   Session: $SESSION"
echo "   Server: $REMOTE_HOST_IP:$SERVER_PORT"
echo "   Duration: ${SNAPSHOT_DELAY}s capture"

# Start client with snapshot mode to capture frames and verify connection
echo "   Starting client with snapshot mode..."
timeout $((SNAPSHOT_DELAY + 5)) ./build/bin/ascii-chat $SESSION \
  --password "$TEST_PASSWORD" \
  --acds-server $REMOTE_HOST_IP \
  --acds-insecure \
  --snapshot \
  --snapshot-delay $SNAPSHOT_DELAY \
  > /tmp/client_nat_test.log 2>&1 || true

# Analyze connection results
echo ""
echo "   Analyzing connection results..."

# Check for session discovery
if grep -q "Session found: $SESSION" /tmp/client_nat_test.log 2>/dev/null; then
  echo "   ✅ Session discovery succeeded"
  SESSION_DISCOVERY_OK=1
elif grep -q "Session '$SESSION' not found" /tmp/client_nat_test.log 2>/dev/null; then
  echo "   ❌ Session discovery FAILED (not found)"
  SESSION_DISCOVERY_OK=0
else
  echo "   ⚠️  Session discovery status unknown"
  SESSION_DISCOVERY_OK=0
fi

# Check for successful connection (password auth + TCP connection)
if grep -q "Session joined successfully" /tmp/client_nat_test.log 2>/dev/null; then
  echo "   ✅ Session join succeeded"
  SESSION_JOIN_OK=1
else
  echo "   ⚠️  Session join status unknown"
  SESSION_JOIN_OK=0
fi

# Check for TCP connection establishment
if grep -q "TCP handshake completed" /tmp/client_nat_test.log 2>/dev/null || \
   grep -q "Connected to.*:$SERVER_PORT" /tmp/client_nat_test.log 2>/dev/null; then
  echo "   ✅ TCP connection established"
  CLIENT_CONNECTED=1
else
  echo "   ⚠️  TCP connection status unknown (check logs)"
  CLIENT_CONNECTED=0
fi

# Check for frame reception (indicates working video stream)
FRAMES_RECEIVED=$(grep -c "Received ASCII frame" /tmp/client_nat_test.log 2>/dev/null || echo 0)
if [ "$FRAMES_RECEIVED" -gt 0 ]; then
  echo "   ✅ Received $FRAMES_RECEIVED video frames"
  FRAMES_OK=1
else
  echo "   ⚠️  No video frames received"
  FRAMES_OK=0
fi

# Check connection duration
CONNECTION_DURATION=$(grep "Connection duration" /tmp/client_nat_test.log 2>/dev/null | awk '{print $NF}' | tr -d 's' || echo 0)
if [ -n "$CONNECTION_DURATION" ] && [ "$CONNECTION_DURATION" -ge $SNAPSHOT_DELAY ]; then
  echo "   ✅ Connection lasted ${CONNECTION_DURATION}s (stable)"
  CONNECTION_STABLE=1
else
  echo "   ⚠️  Connection duration: ${CONNECTION_DURATION}s"
  CONNECTION_STABLE=0
fi

# [9/9] Analyze ACDS logs
echo "[9/9] Analyzing ACDS session activity..."

SESSION_CREATED=$(run_remote "grep -c 'Session created: $SESSION' /tmp/acds_test.log 2>/dev/null || echo 0" | tr -d '\n\r ')
SESSION_LOOKUPS=$(run_remote "grep -c 'Session lookup.*$SESSION' /tmp/acds_test.log 2>/dev/null || echo 0" | tr -d '\n\r ')
LOOKUP_FAILURES=$(run_remote "grep -c 'Session lookup.*$SESSION.*(not found)' /tmp/acds_test.log 2>/dev/null || echo 0" | tr -d '\n\r ')
LOOKUP_SUCCESSES=$(run_remote "grep -c 'Session lookup.*$SESSION.*(found' /tmp/acds_test.log 2>/dev/null || echo 0" | tr -d '\n\r ')

echo ""
echo "========================================="
echo "         TEST RESULTS"
echo "========================================="
echo ""
echo "ACDS Discovery Service:"
echo "  Session string: $SESSION"
echo "  Sessions created: $SESSION_CREATED"
echo "  Lookups total: $SESSION_LOOKUPS"
echo "  Lookups successful: $LOOKUP_SUCCESSES"
echo "  Lookup failures: $LOOKUP_FAILURES"
echo ""
echo "NAT Traversal (Client behind NAT → Public Server):"
echo "  Session discovery: $([ "$SESSION_DISCOVERY_OK" -eq 1 ] && echo "✅ Success" || echo "❌ Failed")"
echo "  Connection established: $([ "$CLIENT_CONNECTED" -eq 1 ] && echo "✅ Yes" || echo "❌ No")"
echo "  Video frames received: $FRAMES_RECEIVED frames $([ "$FRAMES_OK" -eq 1 ] && echo "✅" || echo "⚠️")"
echo "  Connection stability: $([ "$CONNECTION_STABLE" -eq 1 ] && echo "✅ Stable (${CONNECTION_DURATION}s)" || echo "⚠️ ${CONNECTION_DURATION}s")"
echo ""

# Determine overall test result
ACDS_OK=0
NAT_OK=0

# ACDS test passes if sessions created and lookups succeed
if [ "$SESSION_CREATED" -ge 1 ] && [ "$LOOKUP_SUCCESSES" -ge 1 ] && [ "$LOOKUP_FAILURES" -eq 0 ]; then
  ACDS_OK=1
fi

# NAT traversal test passes if discovery + join + connection + frames all succeed
if [ "$SESSION_DISCOVERY_OK" -eq 1 ] && [ "$SESSION_JOIN_OK" -eq 1 ] && \
   [ "$CLIENT_CONNECTED" -eq 1 ] && [ "$FRAMES_OK" -eq 1 ]; then
  NAT_OK=1
fi

# Overall result
if [ "$ACDS_OK" -eq 1 ] && [ "$NAT_OK" -eq 1 ]; then
  echo "✅ ALL TESTS PASSED!"
  echo "   [✓] ACDS RCU hash table fix working"
  echo "   [✓] Session discovery across NAT working"
  echo "   [✓] Client connection from behind NAT working"
  echo "   [✓] Video streaming working"
  RESULT=0
elif [ "$ACDS_OK" -eq 1 ] && [ "$NAT_OK" -eq 0 ]; then
  echo "⚠️  PARTIAL SUCCESS"
  echo "   [✓] ACDS session discovery working"
  echo "   [✗] NAT traversal connection failed"
  RESULT=1
elif [ "$ACDS_OK" -eq 0 ]; then
  echo "❌ TEST FAILED"
  echo "   [✗] ACDS session discovery not working"
  echo "   - Check RCU hash table implementation"
  RESULT=1
else
  echo "⚠️  TEST INCONCLUSIVE"
  RESULT=2
fi

echo ""
echo "========================================="
echo ""
echo "Detailed logs available at:"
echo "  Remote ACDS: $REMOTE_HOST:/tmp/acds_test.log"
echo "  Remote Server: $REMOTE_HOST:/tmp/server_test.log"
echo "  Local Client: /tmp/client_nat_test.log"
echo ""

# Show debug logs if test failed
if [ "$RESULT" -ne 0 ]; then
  echo "=== Debug Information ==="
  echo ""

  if [ "$ACDS_OK" -eq 0 ]; then
    echo "ACDS Activity (last 20 lines):"
    run_remote "grep '$SESSION' /tmp/acds_test.log | tail -20" || echo "No activity found"
    echo ""
  fi

  if [ "$NAT_OK" -eq 0 ]; then
    echo "Client Connection Logs (last 30 lines):"
    tail -30 /tmp/client_nat_test.log 2>/dev/null || echo "No client log found"
    echo ""

    echo "Server Logs (client connection attempts):"
    run_remote "grep -i 'client\|connection' /tmp/server_test.log | tail -20" 2>/dev/null || echo "No server logs found"
    echo ""
  fi
fi

# Cleanup
echo "Cleaning up..."
run_remote "pkill -9 ascii-chat; pkill -9 acds" 2>/dev/null || true

exit $RESULT
