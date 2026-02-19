#!/bin/bash
# Debug script: Start server and client, let them run 5s, attach lldb, print stack traces

pkill -f "ascii-chat" || true
sleep 1

echo "Starting server..."
./build/bin/ascii-chat --log-level dev --log-file /tmp/ascii-chat-server.log server &
SERVER_PID=$!
echo "Server PID: $SERVER_PID"
sleep 1

echo "Starting client with snapshot mode (-S -D 5 to capture ASCII art)..."
./build/bin/ascii-chat --log-level dev --log-file /tmp/ascii-chat-client.log client -S -D 5 2>&1 | tee /tmp/ascii-chat-client-stdout.txt &
CLIENT_PID=$!
echo "Client PID: $CLIENT_PID"

echo ""
echo "Waiting 5 seconds for interaction (watch for hangs)..."
sleep 5

echo ""
echo "=== CLIENT STDOUT (ASCII ART FRAMES) ==="
cat /tmp/ascii-chat-client-stdout.txt 2>/dev/null | grep -v "^\[" | head -100
echo ""

echo ""
if kill -0 $SERVER_PID 2>/dev/null; then
  echo "=== ATTACHING LLDB TO SERVER (PID $SERVER_PID) ==="
  lldb -p $SERVER_PID -o "bt all" -o "quit" 2>&1 | head -150
else
  echo "Server process (PID $SERVER_PID) is not running"
fi

echo ""
if kill -0 $CLIENT_PID 2>/dev/null; then
  echo "=== ATTACHING LLDB TO CLIENT (PID $CLIENT_PID) ==="
  lldb -p $CLIENT_PID -o "bt all" -o "quit" 2>&1 | head -150
else
  echo "Client process (PID $CLIENT_PID) is not running"
fi

echo ""
echo "=== SERVER LOG ==="
cat /tmp/ascii-chat-server.log 2>/dev/null || echo "(no server log)"

echo ""
echo "=== CLIENT LOG ==="
cat /tmp/ascii-chat-client.log 2>/dev/null || echo "(no client log)"

echo ""
echo "Cleaning up processes..."
kill $SERVER_PID $CLIENT_PID 2>/dev/null || true
pkill -f "ascii-chat" || true
