#!/bin/bash
# Test script for ACDS discovery mode with ASCII art rendering

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO_ROOT"

ACDS_PORT=27225
ACDS_DB="/tmp/acds-test-$$.db"
TEST_TIMEOUT=5
TEMP_DIR="/tmp/ascii-chat-test-$$"
mkdir -p "$TEMP_DIR"

# Cleanup function
cleanup() {
    echo "Cleaning up..."
    pkill -f "ascii-chat.*discovery-service" || true
    pkill -f "ascii-chat.*--discovery-service" || true
    rm -f "$ACDS_DB"
    rm -rf "$TEMP_DIR"
}

trap cleanup EXIT

# Clean database first
rm -f "$ACDS_DB"

echo "Starting ACDS server on port $ACDS_PORT..."
timeout $((TEST_TIMEOUT + 2)) ./build/bin/ascii-chat --log-level debug discovery-service --port $ACDS_PORT --database "$ACDS_DB" 2>&1 | tee "$TEMP_DIR/acds.log" &
ACDS_PID=$!
sleep 1

echo "Starting initiator (creates session, --snapshot, timeout=$TEST_TIMEOUT)..."
timeout $TEST_TIMEOUT ./build/bin/ascii-chat --discovery-service localhost --discovery-service-port $ACDS_PORT --snapshot --snapshot-delay 0 2>&1 | tee "$TEMP_DIR/initiator.log" &
INITIATOR_PID=$!

sleep 2

# Try to extract session string from initiator output
SESSION_STRING=$(grep -oE "[a-z]+-[a-z]+-[a-z]+" "$TEMP_DIR/initiator.log" 2>/dev/null | head -1)
if [ -z "$SESSION_STRING" ]; then
    echo "Warning: Could not find session string from initiator, using dummy session"
    SESSION_STRING="dummy-session-string"
else
    echo "Found session string: $SESSION_STRING"
fi

echo "Starting joiner with session string: $SESSION_STRING (timeout=$TEST_TIMEOUT)..."
timeout $TEST_TIMEOUT ./build/bin/ascii-chat "$SESSION_STRING" --discovery-service localhost --discovery-service-port $ACDS_PORT --snapshot --snapshot-delay 0 2>&1 | tee "$TEMP_DIR/joiner.log" &
JOINER_PID=$!

echo "Waiting for processes to complete..."
wait $INITIATOR_PID 2>/dev/null || INITIATOR_EXIT=$?
wait $JOINER_PID 2>/dev/null || JOINER_EXIT=$?

INITIATOR_EXIT=${INITIATOR_EXIT:-0}
JOINER_EXIT=${JOINER_EXIT:-0}

echo "Initiator exit code: $INITIATOR_EXIT"
echo "Joiner exit code: $JOINER_EXIT"

# Check for ASCII art frames in output
INITIATOR_FRAMES=$(grep -c "ascii art\|█\|▓\|░\|frame" "$TEMP_DIR/initiator.log" 2>/dev/null || echo 0)
JOINER_FRAMES=$(grep -c "ascii art\|█\|▓\|░\|frame" "$TEMP_DIR/joiner.log" 2>/dev/null || echo 0)

echo "Initiator ASCII art frames: $INITIATOR_FRAMES"
echo "Joiner ASCII art frames: $JOINER_FRAMES"

# Kill ACDS
kill $ACDS_PID 2>/dev/null || wait $ACDS_PID 2>/dev/null || true

# Wait a bit for process cleanup
sleep 0.5

# Check for NETWORK_QUALITY relay in ACDS logs
echo ""
echo "=== ACDS Relay Debug Info ==="
grep "ACDS_ON_NETWORK_QUALITY\|SIGNALING_BROADCAST\|BROADCAST_CALLBACK\|RECEIVE_NETWORK_QUALITY" "$TEMP_DIR/acds.log" 2>/dev/null || echo "No relay debug messages found"

echo ""
echo "Test completed"
