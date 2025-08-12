#!/bin/bash

echo "=== Testing Double-Free Fix ==="
echo "Starting server in background..."

# Kill any existing server process
pkill -f "./bin/server" 2>/dev/null || true
sleep 1

# Start server in background with memory debugging
./bin/server --width 80 --height 24 --color &
SERVER_PID=$!

echo "Server started with PID $SERVER_PID"
sleep 2

# Start a client briefly to generate frame cache activity
echo "Starting test client..."
timeout 3 ./bin/client --width 80 --height 24 --color > /dev/null 2>&1 &
CLIENT_PID=$!

sleep 2

# Stop client
kill $CLIENT_PID 2>/dev/null || true
echo "Test client stopped"

sleep 1

# Stop server cleanly
echo "Stopping server..."
kill -INT $SERVER_PID
sleep 2

# Force kill if still running
if kill -0 $SERVER_PID 2>/dev/null; then
    echo "Force killing server..."
    kill -9 $SERVER_PID
fi

echo "=== Memory Report Should Show malloc_calls >= free_calls ==="
echo "Check server.log for final memory report"