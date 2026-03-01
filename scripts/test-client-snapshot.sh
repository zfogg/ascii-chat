#!/bin/bash
set -e

# Test client ASCII art output with timeout
# Usage: ./scripts/test-client-snapshot.sh [snapshot-delay-seconds]

SNAPSHOT_DELAY=0
BUILD_DIR="${BUILD_DIR:-.}/build"
BINARY="$BUILD_DIR/bin/ascii-chat"

if [ ! -f "$BINARY" ]; then
    echo "Error: Binary not found at $BINARY"
    echo "Please build first: cmake --preset default -B build && cmake --build build"
    exit 1
fi

echo "=== ASCII Chat Client Snapshot Test ==="
echo "Snapshot delay: ${SNAPSHOT_DELAY}s"
echo ""

# Clean up log files
rm -f /tmp/client_snapshot_{server,client}.log

# Start server in background
echo "[1/4] Starting server..."
"$BINARY" --log-level debug --log-file /tmp/client_snapshot_server.log server > /dev/null 2>&1 &
SERVER_PID=$!
sleep 0.25

trap "kill $SERVER_PID 2>/dev/null || true" EXIT

# Run client with timeout: 4 second timeout, kill after 1 second if stuck, snapshot mode with delay
echo "[2/4] Running client with timeout (4s timeout, kill -9 after 1s)..."
echo "      Command: timeout -k1 4 $BINARY client -S -D $SNAPSHOT_DELAY"
echo ""

timeout -k1 4 "$BINARY" --log-level debug --log-file /tmp/client_snapshot_client.log client -S -D "$SNAPSHOT_DELAY" 2>&1 | tee /tmp/client_snapshot_output.txt

EXIT_CODE=${PIPESTATUS[0]}
echo ""
echo "[3/4] Timeout exit code: $EXIT_CODE"
echo "  (0 = success, 124 = timeout, 137 = killed, >0 = error)"
echo ""

# Show ASCII art output
echo "[4/4] ASCII art output (first 60 lines):"
echo "========================================"
head -60 /tmp/client_snapshot_output.txt
echo ""

# Grep for interesting log entries
echo "========================================"
echo "Server logs - key events:"
grep -i "listening\|accept\|client.*connect" /tmp/client_snapshot_server.log 2>/dev/null | head -10 || echo "  (no matches)"

echo ""
echo "Client logs - key events:"
grep -i "connect\|handshake\|render\|frame\|ascii" /tmp/client_snapshot_client.log 2>/dev/null | head -20 || echo "  (no matches)"

echo ""
echo "Full log locations:"
echo "  Server: /tmp/client_snapshot_server.log"
echo "  Client: /tmp/client_snapshot_client.log"
echo "  Output: /tmp/client_snapshot_output.txt"
