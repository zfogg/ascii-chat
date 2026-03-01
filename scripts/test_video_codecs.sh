#!/bin/bash
# Test script for --video-codec option (raw vs hevc)

set -e

BIN="./build/bin/ascii-chat"
SERVER_PORT=27224
TEST_DURATION=3

echo "================================"
echo "H.265/HEVC Video Codec Tests"
echo "================================"

# Cleanup function
cleanup() {
    echo "Cleaning up..."
    pkill -f "ascii-chat server" || true
    sleep 1
}

trap cleanup EXIT

# Test 1: Server + Client with HEVC (default)
echo ""
echo "TEST 1: Default codec (HEVC)"
echo "=============================="
$BIN server --port $SERVER_PORT > /tmp/server_hevc.log 2>&1 &
SERVER_PID=$!
sleep 1

timeout $TEST_DURATION $BIN client localhost:$SERVER_PORT --test-pattern --snapshot --snapshot-delay 0 2>&1 | grep -E "IMAGE_FRAME|codec|delivered" | head -5 || true

kill $SERVER_PID 2>/dev/null || true
sleep 1

# Test 2: Server + Client with explicit HEVC
echo ""
echo "TEST 2: Explicit HEVC codec"
echo "============================="
$BIN server --port $SERVER_PORT > /tmp/server_hevc2.log 2>&1 &
SERVER_PID=$!
sleep 1

timeout $TEST_DURATION $BIN client localhost:$SERVER_PORT --test-pattern --video-codec hevc --snapshot --snapshot-delay 0 2>&1 | grep -E "IMAGE_FRAME|codec|delivered" | head -5 || true

kill $SERVER_PID 2>/dev/null || true
sleep 1

# Test 3: Server + Client with RAW codec
echo ""
echo "TEST 3: RAW codec (uncompressed)"
echo "=================================="
$BIN server --port $SERVER_PORT > /tmp/server_raw.log 2>&1 &
SERVER_PID=$!
sleep 1

timeout $TEST_DURATION $BIN client localhost:$SERVER_PORT --test-pattern --video-codec raw --snapshot --snapshot-delay 0 2>&1 | grep -E "IMAGE_FRAME|codec|delivered" | head -5 || true

kill $SERVER_PID 2>/dev/null || true
sleep 1

echo ""
echo "================================"
echo "Tests Complete!"
echo "================================"
echo ""
echo "Log files:"
echo "  /tmp/server_hevc.log"
echo "  /tmp/server_hevc2.log"
echo "  /tmp/server_raw.log"
echo ""
echo "To check codec usage:"
echo "  grep -i 'IMAGE_FRAME' /tmp/server*.log"
echo "  grep -i 'CODEC' /tmp/server*.log"
