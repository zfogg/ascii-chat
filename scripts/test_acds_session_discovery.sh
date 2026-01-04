#!/bin/bash
#
# ACDS session discovery test
# Tests the RCU hash table fix for session creation and lookup
#

set -e

# Configuration
REMOTE_HOST="sidechain"
REMOTE_REPO="/opt/ascii-chat"
LOCAL_REPO="/home/zfogg/src/github.com/zfogg/ascii-chat"
ACDS_PORT=27225
SERVER_PORT=27224
TEST_PASSWORD="discovery-test-$(date +%s)"
DURATION=30

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
echo "ACDS Session Discovery Test"
echo "============================"
echo "Local:  $LOCAL_REPO"
echo "Remote: $REMOTE_HOST:$REMOTE_REPO"
echo "Duration: $DURATION seconds"
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

# [3/9] Copy latest session.c to remote
echo "[3/9] Copying fixed session.c to remote..."
scp lib/acds/session.c $REMOTE_HOST:$REMOTE_REPO/lib/acds/session.c

# [4/9] Rebuild ACDS on remote
echo "[4/9] Rebuilding ACDS on remote..."
run_remote "cmake --build build --target acds 2>&1 | tail -5" || {
  echo "ERROR: ACDS build failed on remote"
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
run_bg_remote "WEBCAM_DISABLED=1 ./build/bin/ascii-chat server --log-level debug --log-file /tmp/server_test.log --acds --acds-expose-ip --password $TEST_PASSWORD > /tmp/server_output.txt 2>&1"

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

# [8/9] Test client discovery from local machine
echo "[8/9] Testing client discovery from local (behind NAT)..."
echo "   Running: ascii-chat $SESSION --password $TEST_PASSWORD --acds-server 135.181.27.224 --acds-insecure"

# Start client with snapshot mode (exits after connection)
timeout 10 ./build/bin/ascii-chat $SESSION \
  --password "$TEST_PASSWORD" \
  --acds-server 135.181.27.224 \
  --acds-insecure \
  --snapshot \
  --snapshot-delay 2 \
  > /tmp/client_discovery_test.log 2>&1 || true

# Check if client connected successfully
if grep -q "Connected to server" /tmp/client_discovery_test.log 2>/dev/null; then
  echo "   ✅ Client connected successfully!"
  CLIENT_CONNECTED=1
elif grep -q "Session '$SESSION' not found" /tmp/client_discovery_test.log 2>/dev/null; then
  echo "   ❌ Session lookup FAILED (not found)"
  CLIENT_CONNECTED=0
else
  echo "   ⚠️  Client connection status unknown"
  CLIENT_CONNECTED=0
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
echo "Session string: $SESSION"
echo "Session created: $SESSION_CREATED times"
echo "Total lookups: $SESSION_LOOKUPS"
echo "  - Successful: $LOOKUP_SUCCESSES"
echo "  - Failed: $LOOKUP_FAILURES"
echo ""

# Determine test result
if [ "$SESSION_CREATED" -ge 1 ] && [ "$LOOKUP_SUCCESSES" -ge 1 ] && [ "$LOOKUP_FAILURES" -eq 0 ]; then
  echo "✅ TEST PASSED: RCU hash table fix working!"
  echo "   - Sessions are created successfully"
  echo "   - Lookups find the sessions"
  echo "   - No lookup failures"
  RESULT=0
elif [ "$SESSION_CREATED" -ge 1 ] && [ "$LOOKUP_FAILURES" -gt 0 ]; then
  echo "❌ TEST FAILED: Lookup failures detected"
  echo "   - Session was created but lookups are failing"
  echo "   - This indicates the RCU match function bug"
  RESULT=1
else
  echo "⚠️  TEST INCONCLUSIVE"
  echo "   - Check logs for details"
  RESULT=2
fi

echo ""
echo "========================================="
echo ""
echo "Detailed logs available at:"
echo "  Remote ACDS: $REMOTE_HOST:/tmp/acds_test.log"
echo "  Remote Server: $REMOTE_HOST:/tmp/server_test.log"
echo "  Local Client: /tmp/client_discovery_test.log"
echo ""

# Show recent ACDS activity for debugging
if [ "$RESULT" -ne 0 ]; then
  echo "=== Recent ACDS Activity (last 20 lines) ==="
  run_remote "grep '$SESSION' /tmp/acds_test.log | tail -20" || echo "No activity found"
  echo ""
fi

# Cleanup
echo "Cleaning up..."
run_remote "pkill -9 ascii-chat; pkill -9 acds" 2>/dev/null || true

exit $RESULT
