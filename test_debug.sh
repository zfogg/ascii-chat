#!/bin/bash

# Debug test script for ASCII video issue
# This script runs server and client to capture debug output

set -e

echo "=== ASCII-Chat Debug Test ==="

# Kill any existing server/client processes
pkill -f "bin/server" || true
pkill -f "bin/client" || true
sleep 1

# Clean build
echo "Building with debug..."
make clean && make debug > /dev/null 2>&1

# Start server in background
echo "Starting server on port 9050..."
./bin/server --port 9050 > server_debug.log 2>&1 &
SERVER_PID=$!
sleep 2

# Start client
echo "Starting client..."
COLUMNS=80 LINES=24 timeout 10 ./bin/client --port 9050 > client_debug.log 2>&1 &
CLIENT_PID=$!

# Wait a bit for connection
echo "Waiting for connection and debug output..."
sleep 8

# Kill processes
kill $SERVER_PID $CLIENT_PID 2>/dev/null || true
sleep 1

# Show debug output
echo
echo "=== SERVER DEBUG OUTPUT ==="
cat server_debug.log

echo
echo "=== CLIENT DEBUG OUTPUT ==="
cat client_debug.log

echo
echo "=== SUMMARY ==="
echo "Server lines: $(wc -l < server_debug.log)"
echo "Client lines: $(wc -l < client_debug.log)"

# Check for key debug markers
if grep -q "BROADCAST_DEBUG.*alive" server_debug.log; then
    echo "✓ Video broadcast thread is running"
else
    echo "✗ Video broadcast thread not detected"
fi

if grep -q "ACTIVE_DEBUG.*IS ACTIVE" server_debug.log; then
    echo "✓ Active clients found"
else
    echo "✗ No active clients detected"
fi

if grep -q "VIDEO_DEBUG.*create_mixed_ascii_frame" server_debug.log; then
    echo "✓ ASCII frame creation attempted"
else
    echo "✗ No ASCII frame creation detected"
fi

if grep -q "Webcam opened successfully" client_debug.log; then
    echo "✓ Client webcam working"
else
    echo "✗ Client webcam failed"
fi

echo
echo "=== Test complete ==="