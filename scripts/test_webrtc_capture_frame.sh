#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
BIN="$REPO_DIR/build/bin"
SERVER_PORT=27224
DISCOVERY_PORT=27225
FRAME_FILE="/tmp/webrtc_ascii_frame.txt"

cleanup() {
  pkill -9 ascii-chat 2>/dev/null || true
  ssh sidechain "pkill -9 ascii-chat" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# Clean up old output files
rm -f /tmp/server_startup.txt /tmp/client_stderr.log "$FRAME_FILE"

echo "Starting discovery server..."
ssh sidechain "pkill -9 ascii-chat 2>/dev/null || true; sleep 1; rm -f ~/.ascii-chat/acds.db* /tmp/acds.log 2>/dev/null || true"
ssh sidechain "nohup bash -c 'timeout 120 /opt/ascii-chat/build/bin/ascii-chat discovery-service 0.0.0.0 :: --port $DISCOVERY_PORT 2>&1' > /dev/null 2>&1 &"
sleep 5

echo "Starting server..."
echo "y" | timeout 25 $BIN/ascii-chat \
  server 127.0.0.1 :: \
  --port $SERVER_PORT \
  --discovery \
  --discovery-expose-ip \
  --discovery-server discovery-service.ascii-chat.com \
  --discovery-port $DISCOVERY_PORT \
  2>&1 | tee /tmp/server_startup.txt &

sleep 2

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
echo "Capturing frame via WebRTC snapshot..."

# Run client and capture frame output
# Snapshot mode will output ASCII frame to stdout, errors to stderr
timeout 6 $BIN/ascii-chat \
  "$SESSION" \
  --test-pattern \
  --prefer-webrtc \
  --discovery-insecure \
  --discovery-server discovery-service.ascii-chat.com \
  --discovery-port $DISCOVERY_PORT \
  --snapshot \
  --snapshot-delay 0 \
  2>/tmp/client_stderr.log | tee "$FRAME_FILE"

echo ""
echo "=== ASCII FRAME TRANSMITTED OVER WEBRTC ==="
echo ""
if [ -s "$FRAME_FILE" ]; then
  cat "$FRAME_FILE"
else
  echo "ERROR: Frame file is empty!"
  echo ""
  echo "=== CLIENT STDERR ==="
  cat /tmp/client_stderr.log 2>/dev/null || echo "No stderr log"
fi
