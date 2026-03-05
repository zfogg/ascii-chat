#!/bin/bash
set -euo pipefail

PORT=$(((RANDOM % 6000) + 2000))
TMPDIR=$(mktemp -d "/tmp/wss-debug-XXXXXX")

echo "=== WSS TLS Handshake Debug Session ==="
echo "Port: $PORT"
echo "Temp dir: $TMPDIR"
echo ""

# Generate certs
CERT_FILE="/tmp/wss-test-cert.pem"
KEY_FILE="/tmp/wss-test-key.pem"
if [ ! -f "$CERT_FILE" ]; then
    openssl req -x509 -newkey rsa:2048 -keyout "$KEY_FILE" -out "$CERT_FILE" \
      -days 1 -nodes -subj "/CN=localhost" 2>/dev/null
fi

# Kill any existing processes on this port
pkill -f "ascii-chat.*server.*$PORT" || true
sleep 0.5

# Start tcpdump in background (requires sudo)
echo "Starting tcpdump on port $PORT..."
sudo tcpdump -i lo "port $PORT" -w "$TMPDIR/tls.pcap" 2>/dev/null &
TCPDUMP_PID=$!
sleep 1

# Start server with strace
echo "Starting server with strace..."
strace -f -e trace=network,poll,epoll_wait,epoll_ctl,socket,bind,listen,accept,read,write \
  -o "$TMPDIR/server.strace" \
  ./build/bin/ascii-chat --log-level debug --log-file "$TMPDIR/server.log" \
  server --port "$PORT" --websocket-port "$PORT" \
  --websocket-tls-cert "$CERT_FILE" --websocket-tls-key "$KEY_FILE" \
  >/dev/null 2>&1 &
SERVER_PID=$!
sleep 0.5

# Run client with strace
echo "Running client with strace..."
timeout -k1 5 strace -f -e trace=network,poll,epoll_wait,epoll_ctl,socket,connect,read,write,close \
  -o "$TMPDIR/client.strace" \
  ./build/bin/ascii-chat --log-level debug --log-file "$TMPDIR/client.log" \
  client "wss://localhost:$PORT" \
  --test-pattern --snapshot --snapshot-delay 1 \
  >/dev/null 2>&1 || true

sleep 1

# Clean up
echo "Stopping tcpdump..."
sudo kill $TCPDUMP_PID 2>/dev/null || true
pkill -f "ascii-chat.*server.*$PORT" || true

echo ""
echo "=== Debug Output Files ==="
echo "Server strace:  $TMPDIR/server.strace"
echo "Client strace:  $TMPDIR/client.strace"
echo "Server log:     $TMPDIR/server.log"
echo "Client log:     $TMPDIR/client.log"
echo "TLS pcap:       $TMPDIR/tls.pcap"
echo ""

# Extract key strace lines
echo "=== Server network syscalls ==="
grep -E "poll|accept|read|write|SSL|TLS" "$TMPDIR/server.strace" | head -30

echo ""
echo "=== Client network syscalls ==="
grep -E "poll|connect|read|write|SSL|TLS" "$TMPDIR/client.strace" | head -30

echo ""
echo "=== TLS packet info ==="
tcpdump -r "$TMPDIR/tls.pcap" 2>/dev/null | head -20

