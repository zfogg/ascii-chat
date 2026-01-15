#!/bin/bash
set -e

PORT=27225
pkill -f "ascii-chat.*--port $PORT" 2>/dev/null || true
sleep 0.5

echo "Starting server..."
/home/zfogg/src/github.com/zfogg/ascii-chat/build/bin/ascii-chat --log-level error --quiet server --port $PORT &
SERVER_PID=$!

echo "Capturing frame from real webcam..."
export ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK=1
timeout 5 /home/zfogg/src/github.com/zfogg/ascii-chat/build/bin/ascii-chat --log-level error --quiet client 127.0.0.1:$PORT --snapshot --snapshot-delay 1 1>/tmp/frame_webcam.txt 2>/tmp/frame_error.txt
CLIENT_PID=$!

# Give server time to shutdown cleanly, then force kill any remaining processes
sleep 1
killall -9 ascii-chat 2>/dev/null || true

if [ -s /tmp/frame_webcam.txt ]; then
    echo "Frame captured: $(wc -c < /tmp/frame_webcam.txt) bytes"
    echo ""
    echo "=== First 500 bytes (hex) ==="
    head -c 500 /tmp/frame_webcam.txt | hexdump -C | head -20
    echo ""
    echo "=== Last 500 bytes (hex) ==="
    tail -c 500 /tmp/frame_webcam.txt | hexdump -C | tail -20
    echo ""
    echo "=== Checking for ESC[0m (reset) sequences ==="
    grep -o $'\\033\\[0m' /tmp/frame_webcam.txt | wc -l
    echo "Found $(grep -o $'\\033\\[0m' /tmp/frame_webcam.txt | wc -l) reset sequences"
    echo ""
    echo "=== Checking for ESC[K (clear-to-EOL) sequences ==="
    grep -o $'\\033\\[K' /tmp/frame_webcam.txt | wc -l
    echo "Found $(grep -o $'\\033\\[K' /tmp/frame_webcam.txt | wc -l) clear-to-EOL sequences"
else
    cat /tmp/frame_error.txt
fi
