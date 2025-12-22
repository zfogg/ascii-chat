#!/bin/bash
# Test script to reproduce the duplicate client reconnection issue

set -e

echo "Building ascii-chat..."
cmake -B build -DCMAKE_BUILD_TYPE=Debug > /dev/null 2>&1
cmake --build build > /dev/null 2>&1

echo "Starting server in background..."
./build/bin/ascii-chat server --log-file /tmp/server-test.log &
SERVER_PID=$!
sleep 1

# Function to cleanup
cleanup() {
    echo "Cleaning up..."
    kill $SERVER_PID 2>/dev/null || true
    pkill -f "ascii-chat client" || true
    sleep 1
}

trap cleanup EXIT

echo ""
echo "=== REPRODUCING ISSUE ==="
echo "1. Connecting Client 1..."
timeout 3 ./build/bin/ascii-chat client --address 127.0.0.1 --port 27224 --snapshot 2>&1 | head -10 &
CLIENT1_PID=$!
sleep 1

echo "2. Connecting Client 2..."
timeout 3 ./build/bin/ascii-chat client --address 127.0.0.1 --port 27224 --snapshot 2>&1 | head -10 &
CLIENT2_PID=$!
sleep 2

echo "3. Disconnecting Client 1 and reconnecting..."
kill $CLIENT1_PID 2>/dev/null || true
sleep 1
timeout 3 ./build/bin/ascii-chat client --address 127.0.0.1 --port 27224 --snapshot 2>&1 | head -10 &
sleep 2

echo ""
echo "=== CHECKING SERVER LOGS ==="
echo "Looking for duplicate client references..."
grep -i "duplicate\|source_count\|grid.*changing" /tmp/server-test.log | tail -20

echo ""
echo "Test complete. Check /tmp/server-test.log for detailed debugging info."
