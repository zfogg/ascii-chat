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

SNAPSHOT_DELAY=3.5

PORT=$(((RANDOM % 6000) + 2000))
PORT_WS=$(((RANDOM % 6000) + 2000))

tmpdir=$(mktemp -d "/tmp/XXX-ascii-chat-debug-fps")

client_log="$tmpdir/client-logfile-$PORT.log"
client_stdout="$tmpdir/client-stdout-$PORT.log"
client_strace="$tmpdir/client-strace-$PORT.log"
client_lldb="$tmpdir/client-lldb-$PORT.log"
server_log="$tmpdir/server-logfile-$PORT.log"
server_strace="$tmpdir/server-strace-$PORT.log"
server_lldb="$tmpdir/server-lldb-$PORT.log"

# Detect if macOS or Linux for strace/dstrace
if [[ "$OSTYPE" == "darwin"* ]]; then
  STRACE_CMD="dstrace"
else
  STRACE_CMD="strace"
fi

cert_file="/tmp/wss-test-cert.pem"
key_file="/tmp/wss-test-key.pem"

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

# Get server process PID and attach lldb (will take backtrace at 3 seconds)
SERVER_PROCESS_PID=$(pgrep -f "ascii-chat.*server" | head -1)
if [ -n "$SERVER_PROCESS_PID" ]; then
  (sleep 3 && lldb -p "$SERVER_PROCESS_PID" -b -o "bt all" -o "quit" 2>&1) > "$server_lldb" &
fi

# Use PORT for TCP, PORT_WS for WebSocket protocols
case "$PROTO" in
  tcp)
    PROTO_PORT="$PORT"
    ;;
  ws|wss)
    PROTO_PORT="$PORT_WS"
    ;;
esac

# Create a temp file to capture the client PID
client_pid_file="$tmpdir/client-pid"
debug_log="$tmpdir/debug.log"

# Function to log debug info
debug_log_msg() {
  echo "[$(date '+%H:%M:%S.%3N')] $*" >> "$debug_log"
}

debug_log_msg "=== Starting test ==="
debug_log_msg "SNAPSHOT_DELAY=$SNAPSHOT_DELAY"
debug_log_msg "PROTO=$PROTO, PROTO_PREFIX=$PROTO_PREFIX"
debug_log_msg "PORT=$PORT, PORT_WS=$PORT_WS, PROTO_PORT=$PROTO_PORT"

# Start client in background (so we can attach lldb to it)
# NOTE: We skip strace for client because lldb cant attach to a process being traced
EXIT_CODE=0
START_TIME=$(date +%s%N)
TIMEOUT_SECS=$(echo "$SNAPSHOT_DELAY + 1" | bc)
{
  timeout -k1 "$TIMEOUT_SECS" \
    "$BUILD_DIR"/bin/ascii-chat \
    --log-level debug --log-file "$client_log" --sync-state 1 \
    client "${PROTO_PREFIX}://localhost:$PROTO_PORT" \
    --test-pattern \
    --snapshot --snapshot-delay "$SNAPSHOT_DELAY" \
    2>/dev/null | tee "$client_stdout"
  EXIT_CODE=$?
} &
CLIENT_BG_PID=$!

# Find the ascii-chat client process
# Strategy: find timeout process, then find ascii-chat as its child
debug_log_msg "Starting PID search for client process..."
for attempt in 1 2 3 4 5; do
  # Find the timeout process handling our client (will have ascii-chat and client in cmdline)
  TIMEOUT_PID=$(ps aux | grep "timeout" | grep "ascii-chat.*client" | grep -v grep | awk '{print $2}' | head -1)
  debug_log_msg "Attempt $attempt: Found timeout PID=$TIMEOUT_PID"

  # If not found that way, try searching for any timeout with our port
  if [ -z "$TIMEOUT_PID" ]; then
    TIMEOUT_PID=$(pgrep -f "timeout.*ascii-chat.*client" 2>/dev/null | head -1)
    debug_log_msg "Attempt $attempt (fallback): Found timeout PID=$TIMEOUT_PID"
  fi

  if [ -n "$TIMEOUT_PID" ]; then
    # Now find the ascii-chat child of this timeout process
    CLIENT_PROCESS_PID=$(pgrep -P "$TIMEOUT_PID" 2>/dev/null | head -1)
    debug_log_msg "Found client as child of $TIMEOUT_PID: CLIENT_PROCESS_PID=$CLIENT_PROCESS_PID"

    if [ -n "$CLIENT_PROCESS_PID" ] && [ -f "/proc/$CLIENT_PROCESS_PID/comm" ]; then
      COMM=$(cat "/proc/$CLIENT_PROCESS_PID/comm" 2>/dev/null)
      CMDLINE=$(tr '\0' ' ' < "/proc/$CLIENT_PROCESS_PID/cmdline" 2>/dev/null | head -c 150)
      debug_log_msg "PID $CLIENT_PROCESS_PID verified: comm=$COMM"
      debug_log_msg "Cmdline: $CMDLINE"
      break
    fi
  fi
  sleep 0.3
done

# If we found a valid PID, attach lldb to it
if [ -n "$CLIENT_PROCESS_PID" ]; then
  debug_log_msg "Will attach lldb to PID $CLIENT_PROCESS_PID"
  (
    sleep 1.5
    debug_log_msg "Attaching lldb to PID $CLIENT_PROCESS_PID at $(date '+%H:%M:%S.%3N')"
    timeout 5 lldb -p "$CLIENT_PROCESS_PID" -b -o "bt all" -o "quit" 2>&1 || echo "lldb timed out"
  ) > "$client_lldb" 2>&1 &
  debug_log_msg "Started lldb in background"
else
  debug_log_msg "ERROR: Could not find client PID"
fi

# Wait for client to finish
wait $CLIENT_BG_PID 2>/dev/null || true
END_TIME=$(date +%s%N)
echo "[DEBUG] Script continuing after wait" >&2

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

# Verify backtraces were captured in lldb logs
if [ -f "$server_lldb" ] && grep -q "frame" "$server_lldb" 2>/dev/null; then
    echo "✓ Server backtrace captured"
else
    echo "⚠️  Server backtrace not found"
fi

if [ -f "$client_lldb" ] && grep -q "frame" "$client_lldb" 2>/dev/null; then
    echo "✓ Client backtrace captured"
else
    echo "⚠️  Client backtrace not found"
fi

# Wait for logs to flush before displaying results
echo ""
echo "📋 Log files:"
echo "  Client log:      $client_log"
echo "  Client stdout:   $client_stdout"
echo "  Client strace:   $client_strace"
echo "  Client lldb:     $client_lldb"
echo "  Server log:      $server_log"
echo "  Server strace:   $server_strace"
echo "  Server lldb:     $server_lldb"

echo ""

# Show debug log if lldb failed
if [ -f "$debug_log" ]; then
  debug_log_msg "=== Test complete ==="
  echo "📋 Debug log:"
  cat "$debug_log"
  echo ""
fi

# Give processes time to flush logs before killing
sleep 2
pkill -f "ascii-chat.*(server|client).*$PORT" 2>/dev/null || true
sleep 0.5
pkill -f "lldb.*" 2>/dev/null || true
