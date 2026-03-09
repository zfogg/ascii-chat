#!/usr/bin/env zsh

set -euo pipefail

# Optional --build-dir and --proto parameters
# Usage: ./debug-connectivity.sh [--build-dir <dir>] [--proto <tcp|ws|wss>]
BUILD_DIR="build"
PROTO="tcp"  # Default protocol

export ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK='1'
export ASCII_CHAT_QUESTION_PROMPT_RESPONSE='y'

extra_client_args=""
server_wss_args=""

SNAPSHOT_DELAY=3.5

PORT=$(((RANDOM % 6000) + 2000))
PORT_WS=$(((RANDOM % 6000) + 2000))

tmpdir=$(mktemp -d "/tmp/XXX-ascii-chat-debug-fps")

client_log="$tmpdir/client-logfile-$PORT.log"
client_stdout="$tmpdir/client-stdout-$PORT.log"
client_strace="$tmpdir/client-strace-$PORT.log"
server_log="$tmpdir/server-logfile-$PORT.log"
server_strace="$tmpdir/server-strace-$PORT.log"

cert_file="/tmp/wss-test-cert.pem"
key_file="/tmp/wss-test-key.pem"

# Detect if macOS or Linux for strace/dstrace
if [[ "$OSTYPE" == "darwin"* ]]; then
  STRACE_CMD="dstrace"
else
  STRACE_CMD="strace"
fi


while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      if [[ -z "${2:-}" ]]; then
        echo "Error: --build-dir requires a directory argument" >&2
        exit 1
      fi
      BUILD_DIR="$2"
      shift 2
      ;;
    --proto)
      if [[ -z "${2:-}" ]]; then
        echo "Error: --proto requires tcp, ws, or wss" >&2
        exit 1
      fi
      case "$2" in
        tcp|ws|wss)
          PROTO="$2"
          shift 2
          ;;
        *)
          echo "Error: Invalid protocol '$2'. Must be tcp, ws, or wss" >&2
          exit 1
          ;;
      esac
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 1
      ;;
  esac
done


echo "Starting connectivity test on port: $PORT"
echo "Proto: $PROTO"
echo "Using build directory: $BUILD_DIR"
echo ""

pkill -f "ascii-chat.*(server|client).*$PORT" && sleep 0.5 || true

cmake --build "$BUILD_DIR"

# Protocol-specific configuration
declare -a server_args=()
case "$PROTO" in
  tcp)
    PROTO_PREFIX="tcp"
    echo "Testing TCP connectivity (snapshot delay: ${SNAPSHOT_DELAY}s)"
    ;;

  ws|wss)
    if [[ "$PROTO" == "wss" ]]; then
      PROTO_PREFIX="wss"
      echo "Testing WebSocket Secure (WSS) connectivity (snapshot delay: ${SNAPSHOT_DELAY}s)"
      echo "Generating self-signed certificate..."
      if [ ! -f "$cert_file" ] || [ ! -f "$key_file" ]; then
        openssl req -x509 -newkey rsa:2048 -keyout "$key_file" -out "$cert_file" \
          -days 1 -nodes -subj "/CN=localhost"
      fi
      server_args=(--websocket-tls-cert "$cert_file" --websocket-tls-key "$key_file")
    else
      PROTO_PREFIX="ws"
      echo "Testing WebSocket (WS) connectivity (snapshot delay: ${SNAPSHOT_DELAY}s)"
    fi
    ;;
esac

# Start WebSocket server with strace/dstrace
"$STRACE_CMD" -f -o "$server_strace" \
  "$BUILD_DIR"/bin/ascii-chat --log-file "$server_log" --log-level debug \
  server --port "$PORT" --websocket-port "$PORT_WS" "${server_args[@]}" \
  >/dev/null 2>&1 &
SERVER_PID=$!
sleep 0.25

# Start client
START_TIME=$(date +%s%N)
EXIT_CODE=0
# Use PORT for TCP, PORT_WS for WebSocket protocols
case "$PROTO" in
  tcp)
    PROTO_PORT="$PORT"
    ;;
  ws|wss)
    PROTO_PORT="$PORT_WS"
    ;;
esac
timeout -k0.5 "$((SNAPSHOT_DELAY + 1))" "$STRACE_CMD" -f -o "$client_strace" \
  "$BUILD_DIR"/bin/ascii-chat \
  --log-level debug --log-file "$client_log" --sync-state 1 \
  client "${PROTO_PREFIX}://localhost:$PROTO_PORT" \
  --test-pattern \
  --snapshot --snapshot-delay "$SNAPSHOT_DELAY" \
  2>/dev/null \
  | tee "$client_stdout" || EXIT_CODE=$?
END_TIME=$(date +%s%N)

# Calculate elapsed time in seconds
ELAPSED_NS=$((END_TIME - START_TIME))
ELAPSED_SEC=$(echo "scale=2; $ELAPSED_NS / 1000000000" | bc)

# Extract and display frame statistics
echo ""
echo "=== CONNECTIVITY TEST RESULTS ==="
echo "Protocol: ${(U)PROTO}"
echo "Exit code: $EXIT_CODE (0=success, 124=timeout, 137=deadlock, 139=segfault, 1=error)"
echo "Elapsed time: ${ELAPSED_SEC}s"
echo ""

# Count actual frames rendered by grepping final count from logs
# Last frame total = final FPS sample
FRAME_COUNT=$(grep -i 'FRAMES_RENDERED_TOTAL' "$client_log" 2>/dev/null | tail -1 | awk '{print $NF}' || echo "0")
echo "🎬 FRAME COUNT: $FRAME_COUNT frames"

# Calculate actual FPS based on snapshot rendering time (not total elapsed time)
if (( $(echo "$SNAPSHOT_DELAY > 0" | bc -l) )); then
    ACTUAL_FPS=$(echo "scale=2; $FRAME_COUNT / $SNAPSHOT_DELAY" | bc)
    echo "📊 ACTUAL FPS: $ACTUAL_FPS"
else
    ACTUAL_FPS=0
    echo "📊 ACTUAL FPS: 0 (insufficient snapshot delay)"
fi

echo ""

# Check for memory errors
if grep -q "AddressSanitizer" "$client_log" 2>/dev/null; then
    echo "⚠️  ASAN errors detected:"
    grep "SUMMARY: AddressSanitizer" "$client_log" | head -1 || true
else
    echo "✓ No memory errors detected"
fi

# Show expected vs actual
case "$PROTO" in
  tcp)
    EXPECTED_60=$(echo "scale=0; 60 * $SNAPSHOT_DELAY" | bc | cut -d. -f1)
    EXPECTED_30=$(echo "scale=0; 30 * $SNAPSHOT_DELAY" | bc | cut -d. -f1)
    ;;
  ws|wss)
    EXPECTED_60=$(echo "scale=0; 60 * $SNAPSHOT_DELAY" | bc | cut -d. -f1)
    EXPECTED_30=$(echo "scale=0; 30 * $SNAPSHOT_DELAY" | bc | cut -d. -f1)
    ;;
esac

echo ""
echo "Expected at 60 FPS for ${SNAPSHOT_DELAY}s: ~$EXPECTED_60 frames"
echo "Expected at 30 FPS for ${SNAPSHOT_DELAY}s: ~$EXPECTED_30 frames"
echo "Actual frames rendered: $FRAME_COUNT (with SNAPSHOT_DELAY=${SNAPSHOT_DELAY}s of rendering time)"

echo ""
echo "📋 Log files:"
echo "  Client log:      $client_log"
echo "  Client stdout:   $client_stdout"
echo "  Client strace:   $client_strace"
echo "  Server log:      $server_log"
echo "  Server strace:   $server_strace"

echo ""
pkill -f "ascii-chat.*(server|client).*$PORT" && sleep 0.5 || true
