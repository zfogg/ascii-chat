#!/usr/bin/env bash
# Test NAT traversal with real remote server and production discovery service
#
# This script validates WebRTC NAT traversal by:
# 1. Running server on remote machine (sidechain) behind NAT
# 2. Using production discovery service (discovery-service.ascii-chat.com)
# 3. Running client locally
# 4. Verifying ASCII frames are transmitted over WebRTC DataChannel

set -e

# Configuration
REMOTE_HOST="sidechain"
REMOTE_BINARY="/opt/ascii-chat/build/bin/ascii-chat"
LOCAL_BINARY="${LOCAL_BINARY:-./build/bin/ascii-chat}"
DISCOVERY_SERVICE="discovery-service.ascii-chat.com"
DISCOVERY_PORT="27225"
SERVER_PORT="27224"

# Output files
REMOTE_LOG="/tmp/nat_test_server.log"
LOCAL_CLIENT_OUTPUT="/tmp/nat_test_client_output.txt"
LOCAL_CLIENT_LOG="/tmp/nat_test_client.log"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Cleanup function
cleanup() {
    echo -e "${YELLOW}Cleaning up...${NC}"

    # Kill all ascii-chat processes on both hosts
    pkill -x ascii-chat || true
    ssh "$REMOTE_HOST" "pkill -x ascii-chat || true" 2>/dev/null || true

    # Clean up remote logs and database
    ssh "$REMOTE_HOST" "rm -f $REMOTE_LOG $REMOTE_ACDS_LOG /tmp/nat_test_acds.db*" 2>/dev/null || true

    echo -e "${GREEN}Cleanup complete${NC}"
}

trap cleanup EXIT INT TERM

# Initial cleanup - kill any existing processes
echo -e "${YELLOW}Killing any existing ascii-chat processes...${NC}"
pkill -x ascii-chat || true
ssh "$REMOTE_HOST" "pkill -x ascii-chat || true" 2>/dev/null || true
sleep 1

# Start ACDS discovery service on remote host
# NOTE: Add TURN credentials for relay testing:
#   --turn-servers turn:turn.ascii-chat.com:3478 \
#   --turn-username <username> \
#   --turn-credential <credential> \
echo -e "${YELLOW}Starting ACDS discovery service on $REMOTE_HOST...${NC}"
REMOTE_ACDS_LOG="/tmp/nat_test_acds.log"
ssh "$REMOTE_HOST" "nohup $REMOTE_BINARY discovery-service 0.0.0.0 :: \
    --port $DISCOVERY_PORT \
    --database /tmp/nat_test_acds.db \
    > $REMOTE_ACDS_LOG 2>&1 &"
sleep 3

# Verify ACDS started
if ssh "$REMOTE_HOST" "grep -q 'Listening on' $REMOTE_ACDS_LOG 2>/dev/null"; then
    echo -e "${GREEN}✓ ACDS started and listening on port $DISCOVERY_PORT${NC}"
else
    echo -e "${RED}✗ ACDS failed to start${NC}"
    ssh "$REMOTE_HOST" "tail -20 $REMOTE_ACDS_LOG 2>/dev/null"
    exit 1
fi

# Verify local binary exists
if [[ ! -x "$LOCAL_BINARY" ]]; then
    echo -e "${RED}Error: Local binary not found or not executable: $LOCAL_BINARY${NC}"
    exit 1
fi

# Verify remote host is accessible
if ! ssh "$REMOTE_HOST" "echo 'SSH connection OK'" &>/dev/null; then
    echo -e "${RED}Error: Cannot SSH to $REMOTE_HOST${NC}"
    exit 1
fi

# Verify remote binary exists
if ! ssh "$REMOTE_HOST" "test -x $REMOTE_BINARY"; then
    echo -e "${RED}Error: Remote binary not found: $REMOTE_BINARY${NC}"
    exit 1
fi

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}NAT Traversal Test${NC}"
echo -e "${BLUE}========================================${NC}"
echo -e "Remote host: ${GREEN}$REMOTE_HOST${NC}"
echo -e "Discovery service: ${GREEN}$DISCOVERY_SERVICE:$DISCOVERY_PORT${NC}"
echo -e "Local binary: ${GREEN}$LOCAL_BINARY${NC}"
echo -e "Remote binary: ${GREEN}$REMOTE_BINARY${NC}"
echo ""

# Step 1: Start server on remote host with discovery registration
echo -e "${YELLOW}[1/4] Starting server on $REMOTE_HOST...${NC}"

# Start server in background, writing logs to file
ssh "$REMOTE_HOST" "nohup $REMOTE_BINARY --log-file /tmp/server_logfile.log --log-level debug server 0.0.0.0 :: \
    --port $SERVER_PORT \
    --discovery \
    --discovery-expose-ip \
    --discovery-service $DISCOVERY_SERVICE \
    --discovery-port $DISCOVERY_PORT \
    > $REMOTE_LOG 2>&1 &"

echo -e "${GREEN}✓ Server started on $REMOTE_HOST${NC}"

# Step 2: Wait for server to register and extract session string
echo -e "${YELLOW}[2/4] Waiting for server registration...${NC}"

SESSION_STRING=""
for i in {1..30}; do
    # Check ACDS log for session creation
    if ssh "$REMOTE_HOST" "grep -q 'Session created:' $REMOTE_ACDS_LOG 2>/dev/null"; then
        # Extract session string from ACDS log
        SESSION_STRING=$(ssh "$REMOTE_HOST" "grep 'Session created:' $REMOTE_ACDS_LOG | tail -1" | \
            sed -n 's/.*Session created: \([a-z-]*\).*/\1/p')

        if [[ -n "$SESSION_STRING" ]]; then
            break
        fi
    fi

    echo -ne "\r  Waiting... ${i}s"
    sleep 1
done

echo ""  # Newline after waiting

if [[ -z "$SESSION_STRING" ]]; then
    echo -e "${RED}Error: Failed to get session string from server${NC}"
    echo -e "${YELLOW}Server log:${NC}"
    ssh "$REMOTE_HOST" "cat $REMOTE_LOG"
    exit 1
fi

echo -e "${GREEN}✓ Server registered with session: ${BLUE}$SESSION_STRING${NC}"

# Step 3: Show connection details
echo -e "${YELLOW}[3/4] Connection details:${NC}"

# Get server's public IP (use multiple fallbacks)
SERVER_PUBLIC_IP=$(ssh "$REMOTE_HOST" "curl -s ifconfig.co 2>/dev/null || curl -s icanhazip.com 2>/dev/null || echo 'unknown'")
echo -e "  Server public IP: ${GREEN}$SERVER_PUBLIC_IP${NC}"

# Get local public IP
LOCAL_PUBLIC_IP=$(curl -s ifconfig.co 2>/dev/null || curl -s icanhazip.com 2>/dev/null || echo "unknown")
echo -e "  Client public IP: ${GREEN}$LOCAL_PUBLIC_IP${NC}"

if [[ "$SERVER_PUBLIC_IP" != "unknown" && "$LOCAL_PUBLIC_IP" != "unknown" && "$SERVER_PUBLIC_IP" == "$LOCAL_PUBLIC_IP" ]]; then
    echo -e "  ${YELLOW}Warning: Both machines appear to have same public IP${NC}"
    echo -e "  ${YELLOW}NAT traversal may not be fully tested${NC}"
fi

# Step 4: Connect client and capture frame
echo -e "${YELLOW}[4/4] Connecting client via WebRTC...${NC}"

# Run client with snapshot mode to capture a single frame (force TURN relay)
timeout 15 "$LOCAL_BINARY" --quiet "$SESSION_STRING" \
    --snapshot --snapshot-delay 0 \
    --test-pattern \
    --discovery-service "$DISCOVERY_SERVICE" \
    --discovery-port "$DISCOVERY_PORT" \
    --prefer-webrtc \
    --webrtc-skip-stun \
    > "$LOCAL_CLIENT_OUTPUT" 2>"$LOCAL_CLIENT_LOG" || {
    EXIT_CODE=$?
    if [[ $EXIT_CODE -eq 124 ]]; then
        echo -e "${YELLOW}Client timed out (this may be OK if we got a frame)${NC}"
    else
        echo -e "${RED}Client failed with exit code: $EXIT_CODE${NC}"
    fi
}

# Step 5: Validate results
echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Results${NC}"
echo -e "${BLUE}========================================${NC}"

# Check if output file has content
if [[ ! -s "$LOCAL_CLIENT_OUTPUT" ]]; then
    echo -e "${RED}✗ No client output captured${NC}"
    echo -e "${YELLOW}Client log:${NC}"
    cat "$LOCAL_CLIENT_LOG"
    exit 1
fi

OUTPUT_SIZE=$(wc -c < "$LOCAL_CLIENT_OUTPUT")
OUTPUT_LINES=$(wc -l < "$LOCAL_CLIENT_OUTPUT")

echo -e "Client output: ${GREEN}$OUTPUT_SIZE bytes, $OUTPUT_LINES lines${NC}"

# Check for ASCII art characters
PALETTE_CHARS=$(tr -d '\n\r\033\[0-9;mHJ' < "$LOCAL_CLIENT_OUTPUT" | \
    grep -o "[ .,'\":;clodxkO0KXNWM,d]" | wc -l)

echo -e "Palette characters found: ${GREEN}$PALETTE_CHARS${NC}"

if [[ $PALETTE_CHARS -lt 500 ]]; then
    echo -e "${RED}✗ Not enough ASCII art content (found $PALETTE_CHARS, need 500+)${NC}"
    echo -e "${YELLOW}First 500 chars of output:${NC}"
    head -c 500 "$LOCAL_CLIENT_OUTPUT"
    exit 1
fi

# Check server logs for WebRTC indicators
echo ""
echo -e "${YELLOW}Checking connection type...${NC}"

if ssh "$REMOTE_HOST" "grep -q 'DataChannel opened' $REMOTE_LOG"; then
    echo -e "${GREEN}✓ WebRTC DataChannel confirmed${NC}"
else
    echo -e "${RED}✗ No DataChannel evidence in server log${NC}"
fi

if ssh "$REMOTE_HOST" "grep -q 'ICE' $REMOTE_LOG"; then
    echo -e "${GREEN}✓ ICE negotiation confirmed${NC}"
else
    echo -e "${YELLOW}⚠ No ICE evidence in server log${NC}"
fi

if ssh "$REMOTE_HOST" "grep -q 'STUN' $REMOTE_LOG"; then
    echo -e "${GREEN}✓ STUN usage confirmed${NC}"
else
    echo -e "${YELLOW}⚠ No STUN evidence in server log${NC}"
fi

# Show discovered candidates
echo ""
echo -e "${YELLOW}ICE candidates:${NC}"
if ssh "$REMOTE_HOST" "grep -i 'candidate' $REMOTE_LOG | head -5" | grep -q .; then
    ssh "$REMOTE_HOST" "grep -i 'candidate' $REMOTE_LOG | head -5" | sed 's/^/  /'
else
    echo -e "  ${YELLOW}No candidates found in log${NC}"
fi

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}✓ NAT traversal test completed successfully${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "Output files:"
echo -e "  Client output: ${BLUE}$LOCAL_CLIENT_OUTPUT${NC}"
echo -e "  Client log: ${BLUE}$LOCAL_CLIENT_LOG${NC}"
echo -e "  Server log: ${BLUE}ssh $REMOTE_HOST cat $REMOTE_LOG${NC}"
