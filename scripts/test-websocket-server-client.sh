#!/bin/bash
# Test websocket server and client with auto-kill after 8 seconds
# Starts server on random port, connects client, waits 8 seconds, kills both

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# Generate random port between 30000-40000
WS_PORT=$((30000 + RANDOM % 10000))

# Log files
SERVER_LOG="/tmp/ascii-chat-server-$WS_PORT.log"
CLIENT_LOG="/tmp/ascii-chat-client-$WS_PORT.log"

echo "🚀 Starting websocket server on port $WS_PORT"
echo "   Server log: $SERVER_LOG"
echo "   Client log: $CLIENT_LOG"

# Start server with websocket port
./build/bin/ascii-chat \
  --log-file "$SERVER_LOG" \
  --log-level debug \
  server 0.0.0.0 "::" \
  --websocket-port "$WS_PORT" \
  &
SERVER_PID=$!
echo "   Server PID: $SERVER_PID"

# Give server a moment to start
sleep 0.5

# Start client connecting to server's websocket with snapshot mode, tee stdout to file
CLIENT_STDOUT="/tmp/ascii-chat-client-stdout-$WS_PORT.txt"
./build/bin/ascii-chat \
  --log-file "$CLIENT_LOG" \
  --log-level debug \
  client \
  "ws://localhost:$WS_PORT" \
  -S -D 1 \
  2>&1 | tee "$CLIENT_STDOUT" \
  &
CLIENT_PID=$!
echo "   Client PID: $CLIENT_PID"
echo "   Client stdout: $CLIENT_STDOUT"

echo "⏱️  Waiting 8 seconds..."
sleep 8

echo "💥 Killing both processes..."
kill -9 "$SERVER_PID" "$CLIENT_PID" 2>/dev/null || true

echo "✅ Done"
echo ""
echo "📋 Logs:"
echo "   Server: $SERVER_LOG"
echo "   Client: $CLIENT_LOG"
echo "   Client stdout: $CLIENT_STDOUT"
echo ""

# Check for crashes in client output
if grep -q "AddressSanitizer\|SUMMARY:" "$CLIENT_STDOUT" 2>/dev/null; then
  echo "⚠️  CRASH DETECTED in client output!"
  echo ""
  grep -A 50 "AddressSanitizer" "$CLIENT_STDOUT" | head -60
else
  echo "✅ No crashes detected"
fi
