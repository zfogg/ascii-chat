#!/bin/bash
# Analyze performance bottlenecks in ASCII pipeline

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

echo "🔍 ASCII Pipeline Performance Analysis"
echo "====================================="
echo ""

# Run one test and capture logs
PORT=$((30000 + RANDOM % 10000))
WS_PORT=$((30000 + RANDOM % 10000))

SERVER_LOG="/tmp/perf-server-$WS_PORT.log"
CLIENT_LOG="/tmp/perf-client-$WS_PORT.log"
CLIENT_STDOUT="/tmp/perf-client-stdout-$WS_PORT.txt"

echo "Starting test run on WS_PORT=$WS_PORT..."

# Start server
./build/bin/ascii-chat \
  --log-file "$SERVER_LOG" \
  --log-level debug \
  server 0.0.0.0 "::" \
  --websocket-port "$WS_PORT" \
  --no-status-screen \
  --port "$PORT" \
  &
SERVER_PID=$!

sleep 0.5

# Start client
./build/bin/ascii-chat \
  --log-file "$CLIENT_LOG" \
  --log-level debug \
  client \
  "ws://localhost:$WS_PORT" \
  -S -D 5 \
  2>&1 | tee "$CLIENT_STDOUT" \
  &
CLIENT_PID=$!

sleep 6
kill -9 "$SERVER_PID" "$CLIENT_PID" 2>/dev/null || true
sleep 0.2

echo ""
echo "📊 Performance Metrics"
echo "===================="
echo ""

# WebSocket callback timings
echo "WebSocket Callback Duration (should be < 200µs):"
grep "WS_CALLBACK_DURATION" "$SERVER_LOG" | tail -5 | sed 's/.*took //g' | sed 's/ µs.*/µs/' | sort -n

echo ""
echo "Lock contention (rwlock_rdlock):"
grep "rwlock_rdlock took" "$SERVER_LOG" | tail -5 | sed 's/.*took //g' | sed 's/ms.*/ms/' | sort -n

echo ""
echo "Frame lag events:"
grep "LAG:" "$CLIENT_LOG" | wc -l

echo ""
echo "Audio buffer high water marks:"
grep "high water mark" "$CLIENT_LOG" | wc -l

echo ""
echo "Fragment processing stats:"
echo "  Total fragments received:"
grep -c "WS_FRAG\|Fragment" "$SERVER_LOG" || echo "  0"

echo ""
echo "📁 Full logs saved to:"
echo "  Server: $SERVER_LOG"
echo "  Client: $CLIENT_LOG"
