#!/usr/bin/env zsh

set -euo pipefail

# Optional --build-dir and --proto parameters
# Usage: ./debug-acds.zsh [--build-dir <dir>] [--proto <tcp|ws|wss>]
BUILD_DIR="build"
PROTO="tcp"  # Default protocol

export ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK='1'
export ASCII_CHAT_QUESTION_PROMPT_RESPONSE='y'

SNAPSHOT_DELAY=3.5

ACDS_PORT=$(((RANDOM % 6000) + 2000))
ACDS_WS_PORT=$(((RANDOM % 6000) + 2000))

tmpdir=$(mktemp -d "/tmp/XXX-ascii-chat-debug-acds")

acds_log="$tmpdir/acds-logfile-$ACDS_PORT.log"
initiator_log="$tmpdir/initiator-logfile-$ACDS_PORT.log"
initiator_stdout="$tmpdir/initiator-stdout-$ACDS_PORT.log"
initiator_strace="$tmpdir/initiator-strace-$ACDS_PORT.log"
joiner_log="$tmpdir/joiner-logfile-$ACDS_PORT.log"
joiner_stdout="$tmpdir/joiner-stdout-$ACDS_PORT.log"
joiner_strace="$tmpdir/joiner-strace-$ACDS_PORT.log"

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


echo "Starting ACDS discovery test on port: $ACDS_PORT"
echo "Proto: $PROTO"
echo "Using build directory: $BUILD_DIR"
echo ""

pkill -f "ascii-chat.*(discovery-service|discovery).*$ACDS_PORT" && sleep 0.5 || true

cmake --build "$BUILD_DIR"

# Protocol-specific configuration
declare -a acds_args=()
declare -a discovery_service_args=()

case "$PROTO" in
  tcp)
    PROTO_PREFIX="tcp"
    echo "Testing TCP discovery connectivity (snapshot delay: ${SNAPSHOT_DELAY}s)"
    discovery_service_args=(--discovery-service 127.0.0.1 --discovery-service-port "$ACDS_PORT")
    ;;

  ws|wss)
    if [[ "$PROTO" == "wss" ]]; then
      PROTO_PREFIX="wss"
      echo "Testing WebSocket Secure (WSS) discovery connectivity (snapshot delay: ${SNAPSHOT_DELAY}s)"
      echo "Generating self-signed certificate..."
      if [ ! -f "$cert_file" ] || [ ! -f "$key_file" ]; then
        openssl req -x509 -newkey rsa:2048 -keyout "$key_file" -out "$cert_file" \
          -days 1 -nodes -subj "/CN=localhost"
      fi
      acds_args=(--websocket-tls-cert "$cert_file" --websocket-tls-key "$key_file")
    else
      PROTO_PREFIX="ws"
      echo "Testing WebSocket (WS) discovery connectivity (snapshot delay: ${SNAPSHOT_DELAY}s)"
    fi
    discovery_service_args=(--discovery-service-url "${PROTO_PREFIX}://localhost:$ACDS_WS_PORT")
    ;;
esac

# Start ACDS with optional TLS for WebSocket
echo "Starting ACDS (ascii-chat Discovery Service) on port $ACDS_PORT..."
"$BUILD_DIR"/bin/ascii-chat --log-file "$acds_log" --log-level debug \
  discovery-service --port "$ACDS_PORT" --websocket-port "$ACDS_WS_PORT" "${acds_args[@]}" \
  >/dev/null 2>&1 &
ACDS_PID=$!
sleep 0.5

# Start initiator (create session)
echo "Starting initiator to create session..."
START_TIME=$(date +%s%N)
EXIT_CODE=0

timeout -k0.5 "$((SNAPSHOT_DELAY + 1))" "$STRACE_CMD" -f -o "$initiator_strace" \
  "$BUILD_DIR"/bin/ascii-chat \
  --log-level debug --log-file "$initiator_log" --sync-state 1 \
  --color true --color-mode truecolor \
  "${discovery_service_args[@]}" \
  --test-pattern \
  --snapshot --snapshot-delay "$SNAPSHOT_DELAY" \
  2>/dev/null \
  | tee "$initiator_stdout" || EXIT_CODE=$?

# Extract session string from initiator log
SESSION_STRING=$(grep -i "Session created" "$initiator_log" 2>/dev/null | grep -oE "[a-z]+-[a-z]+-[a-z]+" | head -1 || echo "")

if [ -z "$SESSION_STRING" ]; then
  echo "❌ Failed to get session string from initiator"
  SESSION_STRING="fallback-session-string"
fi

echo "Session string: $SESSION_STRING"
sleep 0.5

# Start joiner (join session)
echo "Starting joiner to join session..."
timeout -k0.5 "$((SNAPSHOT_DELAY + 1))" "$STRACE_CMD" -f -o "$joiner_strace" \
  "$BUILD_DIR"/bin/ascii-chat \
  --log-level debug --log-file "$joiner_log" --sync-state 1 \
  --color true --color-mode truecolor \
  "${discovery_service_args[@]}" \
  "$SESSION_STRING" \
  --test-pattern \
  --snapshot --snapshot-delay "$SNAPSHOT_DELAY" \
  2>/dev/null \
  | tee "$joiner_stdout" || EXIT_CODE=$?

END_TIME=$(date +%s%N)

# Calculate elapsed time in seconds
ELAPSED_NS=$((END_TIME - START_TIME))
ELAPSED_SEC=$(echo "scale=2; $ELAPSED_NS / 1000000000" | bc)

# Extract and display frame statistics
echo ""
echo "=== ACDS DISCOVERY TEST RESULTS ==="
echo "Protocol: ${(U)PROTO}"
echo "ACDS Port: $ACDS_PORT"
echo "ACDS WebSocket Port: $ACDS_WS_PORT"
echo "Session String: $SESSION_STRING"
echo "Exit code: $EXIT_CODE (0=success, 124=timeout, 137=deadlock, 139=segfault, 1=error)"
echo "Elapsed time: ${ELAPSED_SEC}s"
echo ""

# Count actual frames rendered by grepping final count from logs
INITIATOR_FRAME_COUNT=$(grep -i 'FRAMES_RENDERED_TOTAL' "$initiator_log" 2>/dev/null | tail -1 | awk '{print $NF}' || echo "0")
JOINER_FRAME_COUNT=$(grep -i 'FRAMES_RENDERED_TOTAL' "$joiner_log" 2>/dev/null | tail -1 | awk '{print $NF}' || echo "0")

echo "🎬 INITIATOR FRAME COUNT: $INITIATOR_FRAME_COUNT frames"
echo "🎬 JOINER FRAME COUNT: $JOINER_FRAME_COUNT frames"

# Calculate actual FPS based on snapshot rendering time (not total elapsed time)
if (( $(echo "$SNAPSHOT_DELAY > 0" | bc -l) )); then
    INITIATOR_FPS=$(echo "scale=2; $INITIATOR_FRAME_COUNT / $SNAPSHOT_DELAY" | bc)
    JOINER_FPS=$(echo "scale=2; $JOINER_FRAME_COUNT / $SNAPSHOT_DELAY" | bc)
    echo "📊 INITIATOR FPS: $INITIATOR_FPS"
    echo "📊 JOINER FPS: $JOINER_FPS"
else
    echo "📊 FPS: 0 (insufficient snapshot delay)"
fi

echo ""

# Check for memory errors in initiator
if grep -q "AddressSanitizer" "$initiator_log" 2>/dev/null; then
    echo "⚠️  ASAN errors detected in initiator:"
    grep "SUMMARY: AddressSanitizer" "$initiator_log" | head -1 || true
else
    echo "✓ No memory errors detected in initiator"
fi

# Check for memory errors in joiner
if grep -q "AddressSanitizer" "$joiner_log" 2>/dev/null; then
    echo "⚠️  ASAN errors detected in joiner:"
    grep "SUMMARY: AddressSanitizer" "$joiner_log" | head -1 || true
else
    echo "✓ No memory errors detected in joiner"
fi

echo ""
EXPECTED_60=$(echo "scale=0; 60 * $SNAPSHOT_DELAY" | bc | cut -d. -f1)
EXPECTED_30=$(echo "scale=0; 30 * $SNAPSHOT_DELAY" | bc | cut -d. -f1)

echo "Expected at 60 FPS for ${SNAPSHOT_DELAY}s: ~$EXPECTED_60 frames"
echo "Expected at 30 FPS for ${SNAPSHOT_DELAY}s: ~$EXPECTED_30 frames"
echo "Initiator actual frames: $INITIATOR_FRAME_COUNT (with SNAPSHOT_DELAY=${SNAPSHOT_DELAY}s of rendering time)"
echo "Joiner actual frames: $JOINER_FRAME_COUNT (with SNAPSHOT_DELAY=${SNAPSHOT_DELAY}s of rendering time)"

echo ""
echo "📋 Log files:"
echo "  ACDS log:            $acds_log"
echo "  Initiator log:       $initiator_log"
echo "  Initiator stdout:    $initiator_stdout"
echo "  Initiator strace:    $initiator_strace"
echo "  Joiner log:          $joiner_log"
echo "  Joiner stdout:       $joiner_stdout"
echo "  Joiner strace:       $joiner_strace"

echo ""
pkill -f "ascii-chat.*(discovery-service|discovery).*$ACDS_PORT" && sleep 0.5 || true
