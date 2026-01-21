#!/bin/bash
cd /home/zfogg/src/github.com/zfogg/ascii-chat
BIN="./build/bin"
SERVER_PORT=27224
DISCOVERY_PORT=27225
FRAME_FILE="/tmp/webrtc_ascii_frame.txt"

cleanup() {
  pkill -9 ascii-chat 2>/dev/null || true
  ssh sidechain "pkill -9 ascii-chat" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo "Starting discovery server..."
ssh sidechain "pkill -9 ascii-chat 2>/dev/null || true; sleep 1; rm -f ~/.ascii-chat/acds.db* 2>/dev/null || true"
ssh sidechain "nohup bash -c 'WEBCAM_DISABLED=1 timeout 120 /opt/ascii-chat/build/bin/ascii-chat discovery-server 0.0.0.0 :: --port $DISCOVERY_PORT > /tmp/acds.log 2>&1' > /dev/null 2>&1 &"
sleep 5

echo "Starting server..."
WEBCAM_DISABLED=1 timeout 15 $BIN/ascii-chat \
  --log-file /tmp/server.log \
  server 127.0.0.1 :: \
  --port $SERVER_PORT \
  --acds \
  --acds-expose-ip \
  --acds-server discovery-service.ascii-chat.com \
  --acds-port $DISCOVERY_PORT \
  > /dev/null 2>&1 &

SESSION=$(grep -oE "Session created: [a-z-]+" /tmp/server.log | head -1 | awk '{print $NF}')
if [ -z "$SESSION" ]; then
  echo "ERROR: No session created"
  exit 1
fi

echo "Server session: $SESSION"
echo "Capturing frame via WebRTC snapshot..."

# Run client and capture frame output
# Snapshot mode will output ASCII frame to stdout, errors to stderr
set -x
WEBCAM_DISABLED=1 timeout 6 $BIN/ascii-chat \
  "$SESSION" \
  --test-pattern \
  --prefer-webrtc \
  --acds-insecure \
  --acds-server discovery-service.ascii-chat.com \
  --acds-port $DISCOVERY_PORT \
  --snapshot \
  --snapshot-delay 0 \
  | tee "$FRAME_FILE" 2>/tmp/client_stderr.log

#WAIT_PID=$!
#sleep 6
#kill -9 $WAIT_PID 2>/dev/null || true
#sleep 0.25

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
