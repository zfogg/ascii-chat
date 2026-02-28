#!/usr/bin/env bash

set -euo pipefail

pkill -f "ascii-chat.*server" && sleep 0.5 || true
cmake --build build && ./build/bin/ascii-chat server >/dev/null &
EXIT_CODE=0
timeout -k1 8 ./build/bin/ascii-chat --log-level debug --log-file client.log --sync-state 3 client --matrix 2>/dev/null || EXIT_CODE=$?

# Extract and display frame statistics
echo ""
echo "=== TEST RESULTS ==="
echo "Exit code: $EXIT_CODE (137=deadlock, 124=timeout/GOOD, 1=error)"

# Check for frame rendering stats
if grep -q "CLIENT_FRAME\|SESSION STATS" client.log 2>/dev/null; then
    echo ""
    echo "🎬 FRAME STATS:"
    grep "CLIENT_FRAME\|SESSION STATS" client.log | tail -5 || true
else
    FRAMES=$(grep -c "🎬\|📊" client.log 2>/dev/null || echo "0")
    if [ "$FRAMES" -eq "0" ]; then
        echo "❌ No frames rendered - connection may have failed"
    fi
fi

# Check for memory errors
if grep -q "AddressSanitizer" client.log 2>/dev/null; then
    echo "⚠️  ASAN errors detected:"
    grep "SUMMARY: AddressSanitizer" client.log | head -1 || true
else
    echo "✓ No memory errors detected"
fi

echo ""
pkill -f "ascii-chat.*server" && sleep 0.5 || true
