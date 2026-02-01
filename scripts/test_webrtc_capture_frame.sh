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

echo "Starting discovery server locally..."
# Kill any existing ACDS servers
pkill -9 -f "ascii-chat server" 2>/dev/null || true
pkill -9 -f "ascii-chat discovery-service" 2>/dev/null || true
sleep 0.5

# Clean up old ACDS database
rm -f ~/.ascii-chat/acds.db* /tmp/acds_local.log 2>/dev/null || true

# Start ACDS server locally (127.0.0.1 for testing)
timeout 30 $BIN/ascii-chat discovery-service 127.0.0.1 :: --port $DISCOVERY_PORT 2>&1 | tee /tmp/acds_local.log &
ACDS_PID=$!
sleep 0.25

DISCOVERY_CONNECT="127.0.0.1"

echo "Starting server..."
# Bind to 0.0.0.0 and :: (all interfaces) so ACDS can auto-detect the public IP
# Use --discovery-expose-ip flag to allow public IP disclosure (required for non-interactive mode)
# WebRTC is now the default for discovery sessions
timeout 25 $BIN/ascii-chat \
  --log-level debug \
  server 0.0.0.0 :: \
  --port $SERVER_PORT \
  --discovery \
  --discovery-expose-ip \
  --discovery-server "$DISCOVERY_CONNECT" \
  --discovery-port $DISCOVERY_PORT \
  2>&1 | tee /tmp/server_startup.txt &
SERVER_PID=$!
sleep 0.25

# Verify ACDS server is listening locally
echo "Verifying ACDS server is listening on $DISCOVERY_CONNECT:$DISCOVERY_PORT..."
if timeout 2 nc -zv $DISCOVERY_CONNECT $DISCOVERY_PORT 2>&1 | head -3; then
  echo "✓ ACDS server is listening"
else
  echo "✗ Failed to connect to ACDS server"
fi

# Check socket state locally
echo ""
echo "Socket state on local machine:"
ss -tlnp 2>/dev/null | grep -E ':27224|:27225|LISTEN' || echo "ss not available"

# Wait for session string to appear in server output
SESSION=""
for i in {1..100}; do
  SESSION=$(grep -oE "Session String: [a-z-]+" /tmp/server_startup.txt | tail -1 | awk '{print $NF}')
  if [ -n "$SESSION" ]; then
    break
  fi
  echo "Waiting for session string... ($i/100)"
  sleep 0.1
done

if [ -z "$SESSION" ]; then
  echo "ERROR: No session string found"
  echo "=== Server output ==="
  cat /tmp/server_startup.txt
fi

echo "Server session: $SESSION"

# Check established connections before client attempts connection
echo ""
echo "All connections on ACDS port locally (before client):"
ss -tnp 2>/dev/null | grep ':27225' | head -20 || echo "ss not available"

echo ""
echo "All open sockets on local ACDS process:"
lsof -p $ACDS_PID 2>/dev/null | grep -E 'IPv4|IPv6|TCP|sock|27225' | head -15 || echo "lsof not available"

echo ""
echo "Capturing frame via WebRTC snapshot..."
echo "DEBUG: SESSION='$SESSION' DISCOVERY_CONNECT='$DISCOVERY_CONNECT' DISCOVERY_PORT='$DISCOVERY_PORT'"
echo "DEBUG: Client command:"
echo "  timeout 6 $BIN/ascii-chat '$SESSION' --snapshot --snapshot-delay 0 --discovery-server '$DISCOVERY_CONNECT' --discovery-port $DISCOVERY_PORT --prefer-webrtc"
echo ""

# Run client and capture frame output
# Discovery mode with session string, snapshot mode will output ASCII frame to stdout, errors to stderr
# Use DISCOVERY_CONNECT (sidechain) to connect to same ACDS server as the server
# Use --prefer-webrtc to use WebRTC instead of direct TCP
# Use --snapshot-delay 2 to give WebRTC connection time to establish (SDP exchange + ICE candidates + DataChannel setup)
# Use --log-level debug to see detailed WebRTC connection logs
timeout 6 $BIN/ascii-chat \
  --log-level debug \
  "$SESSION" \
  --snapshot \
  --snapshot-delay 0 \
  --discovery-server "$DISCOVERY_CONNECT" \
  --discovery-port $DISCOVERY_PORT \
  --prefer-webrtc \
  2>/tmp/client_stderr.log | tee "$FRAME_FILE"


kill $SERVER_PID $ACDS_PID || true
sleep 0.25
kill -9 $SERVER_PID $ACDS_PID || true

echo ""
echo "=== ASCII FRAME TRANSMITTED OVER WEBRTC ==="
echo ""

# Check socket state after client timeout
echo "Socket state locally (after client timeout):"
ss -tnp 2>/dev/null | grep -E 'ESTAB|TIME_WAIT|27224|27225' | head -10 || echo "ss not available"

echo ""
if [ -s "$FRAME_FILE" ]; then
  cat "$FRAME_FILE"
else
  echo "ERROR: Frame file is empty!"
  echo ""
  echo "=== CLIENT STDERR ==="
  cat /tmp/client_stderr.log 2>/dev/null || echo "No stderr log"
fi
