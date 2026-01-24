#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
BIN="$REPO_DIR/build/bin"
SERVER_PORT=27224
DISCOVERY_PORT=27225
DISCOVERY_HOST="${1:-sidechain}"
DISCOVERY_SERVER="${2:-discovery-service.ascii-chat.com}"
FRAME_FILE="/tmp/webrtc_ascii_frame.txt"

cleanup() {
  pkill -9 ascii-chat 2>/dev/null || true
  ssh "$DISCOVERY_HOST" "pkill -9 ascii-chat" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# Clean up old output files
rm -f /tmp/server_startup.txt /tmp/client_stderr.log "$FRAME_FILE"

echo "Starting discovery server on $DISCOVERY_HOST..."
ssh "$DISCOVERY_HOST" "pkill -9 ascii-chat 2>/dev/null || true; sleep 1; rm -f ~/.ascii-chat/acds.db* /tmp/acds.log 2>/dev/null || true"
ssh "$DISCOVERY_HOST" "nohup bash -c 'timeout 120 /opt/ascii-chat/build/bin/ascii-chat discovery-service 0.0.0.0 :: --port $DISCOVERY_PORT 2>&1' > /tmp/acds_debug.log 2>&1 &"
sleep 3

DISCOVERY_CONNECT="$DISCOVERY_HOST"

echo "Starting server..."
echo "y" | timeout 25 $BIN/ascii-chat \
  server 127.0.0.1 :: \
  --port $SERVER_PORT \
  --discovery \
  --discovery-expose-ip \
  --discovery-server "$DISCOVERY_CONNECT" \
  --discovery-port $DISCOVERY_PORT \
  2>&1 | tee /tmp/server_startup.txt &

sleep 2

# Verify ACDS server is listening on sidechain
echo "Verifying ACDS server is listening on $DISCOVERY_CONNECT:$DISCOVERY_PORT..."
if timeout 2 nc -zv $DISCOVERY_CONNECT $DISCOVERY_PORT 2>&1 | head -3; then
  echo "✓ ACDS server is listening"
else
  echo "✗ Failed to connect to ACDS server"
fi

# Check socket state on sidechain
echo ""
echo "Socket state on $DISCOVERY_HOST:"
ssh "$DISCOVERY_HOST" "ss -tlnp | grep -E ':$DISCOVERY_PORT|LISTEN' && echo '---' && lsof -i :$DISCOVERY_PORT 2>/dev/null || echo 'lsof not available'"

# Check socket state locally
echo ""
echo "Socket state on local machine:"
ss -tlnp 2>/dev/null | grep -E ':27224|LISTEN' || echo "ss not available"

# Wait for session string to appear in server output
SESSION=""
for i in {1..10}; do
  SESSION=$(grep -oE "Session String: [a-z-]+" /tmp/server_startup.txt | tail -1 | awk '{print $NF}')
  if [ -n "$SESSION" ]; then
    break
  fi
  echo "Waiting for session string... ($i/10)"
  sleep 1
done

if [ -z "$SESSION" ]; then
  echo "ERROR: No session string found"
  echo "=== Server output ==="
  cat /tmp/server_startup.txt
  exit 1
fi

echo "Server session: $SESSION"
sleep 1

# Check established connections before client attempts connection
echo ""
echo "All connections on port $DISCOVERY_PORT on $DISCOVERY_HOST (before client):"
ssh "$DISCOVERY_HOST" "ss -tnp | grep ':$DISCOVERY_PORT' | head -20"

echo ""
echo "All connections on $DISCOVERY_HOST involving 27225:"
ssh "$DISCOVERY_HOST" "netstat -tan 2>/dev/null | grep 27225 || ss -tan | grep 27225"

echo ""
echo "All open sockets on ACDS process on $DISCOVERY_HOST:"
ssh "$DISCOVERY_HOST" "ACDS_PID=\$(pgrep -f 'ascii-chat discovery-service'); lsof -p \$ACDS_PID 2>/dev/null | grep -E 'IPv4|IPv6|TCP|sock|27225' | head -15"

echo ""
echo "Capturing frame via WebRTC snapshot..."

# Run client and capture frame output
# Snapshot mode will output ASCII frame to stdout, errors to stderr
timeout 6 $BIN/ascii-chat \
  "$SESSION" \
  --test-pattern \
  --prefer-webrtc \
  --discovery-insecure \
  --discovery-server "$DISCOVERY_CONNECT" \
  --discovery-port $DISCOVERY_PORT \
  --snapshot \
  --snapshot-delay 0 \
  2>/tmp/client_stderr.log | tee "$FRAME_FILE"

echo ""
echo "=== ASCII FRAME TRANSMITTED OVER WEBRTC ==="
echo ""

# Check socket state after client timeout
echo "Socket state on $DISCOVERY_HOST (after client timeout):"
ssh "$DISCOVERY_HOST" "ss -tnp | grep -E 'ESTAB|TIME_WAIT|100.121' | head -10"

echo ""
echo "Socket state locally (after client timeout):"
ss -tnp 2>/dev/null | grep -E 'ESTAB|TIME_WAIT|sidechain' | head -10

echo ""
if [ -s "$FRAME_FILE" ]; then
  cat "$FRAME_FILE"
else
  echo "ERROR: Frame file is empty!"
  echo ""
  echo "=== CLIENT STDERR ==="
  cat /tmp/client_stderr.log 2>/dev/null || echo "No stderr log"
fi
