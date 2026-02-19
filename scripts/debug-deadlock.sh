#!/bin/bash
# Debug script to identify server/client deadlock issues
# Starts server and client with logging, attaches lldb to both, captures traces and lock state

set -e

# Configuration
SERVER_LOG="/tmp/ascii-chat-server.log"
CLIENT_LOG="/tmp/ascii-chat-client.log"
TIMEOUT=5
BIN="./build/bin/ascii-chat"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}=== ASCII-Chat Deadlock Debug Script ===${NC}"
echo "Server log: $SERVER_LOG"
echo "Client log: $CLIENT_LOG"
echo "Timeout: ${TIMEOUT}s"
echo ""

# Clean up old processes
pkill -f "ascii-chat.*server" || true
pkill -f "ascii-chat.*client" || true
sleep 1

# Clean up old logs
rm -f "$SERVER_LOG" "$CLIENT_LOG"

# Function to cleanup on exit
cleanup() {
    echo -e "${YELLOW}Cleaning up processes...${NC}"
    pkill -P $$ || true
    wait 2>/dev/null || true
}
trap cleanup EXIT

# Start server
echo -e "${GREEN}[1/4] Starting server...${NC}"
"$BIN" --log-level dev --log-file "$SERVER_LOG" server \
    --port 27224 \
    &
SERVER_PID=$!
echo "Server PID: $SERVER_PID"

# Give server a moment to potentially fail or make progress
sleep 1

# Check if server is still running
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}Server died immediately!${NC}"
    echo "Server log contents:"
    cat "$SERVER_LOG"
    exit 1
fi

# Start client
echo -e "${GREEN}[2/4] Starting client...${NC}"
"$BIN" --log-level dev --log-file "$CLIENT_LOG" client \
    &
CLIENT_PID=$!
echo "Client PID: $CLIENT_PID"

# Wait for potential deadlock
echo -e "${YELLOW}[3/4] Waiting ${TIMEOUT} seconds for deadlock...${NC}"
sleep "$TIMEOUT"

# Check if processes are still running
SERVER_ALIVE=$(kill -0 $SERVER_PID 2>/dev/null && echo "yes" || echo "no")
CLIENT_ALIVE=$(kill -0 $CLIENT_PID 2>/dev/null && echo "yes" || echo "no")

echo ""
echo -e "${YELLOW}=== Process Status ===${NC}"
echo "Server: $SERVER_ALIVE (PID: $SERVER_PID)"
echo "Client: $CLIENT_ALIVE (PID: $CLIENT_PID)"
echo ""

# Trigger lock_debug_print_state() via SIGUSR1 and capture lock information
echo -e "${GREEN}[4/4] Capturing lock state via SIGUSR1...${NC}"
echo ""

if [ "$SERVER_ALIVE" = "yes" ]; then
    echo -e "${YELLOW}=== SERVER LOCK STATE ===${NC}"
    echo "Sending SIGUSR1 to trigger lock_debug_print_state()..."
    kill -USR1 $SERVER_PID 2>/dev/null || echo "Failed to send SIGUSR1"
    sleep 2

    echo ""
    echo -e "${YELLOW}=== SERVER LOG ===${NC}"
    cat "$SERVER_LOG"
fi

echo ""
echo ""

if [ "$CLIENT_ALIVE" = "yes" ]; then
    echo -e "${YELLOW}=== CLIENT LOCK STATE ===${NC}"
    echo "Sending SIGUSR1 to trigger lock_debug_print_state()..."
    kill -USR1 $CLIENT_PID 2>/dev/null || echo "Failed to send SIGUSR1"
    sleep 2

    echo ""
    echo -e "${YELLOW}=== CLIENT LOG ===${NC}"
    cat "$CLIENT_LOG"
fi

echo ""
echo -e "${YELLOW}=== Full Log Files ===${NC}"
echo "Server log: $SERVER_LOG"
echo "Client log: $CLIENT_LOG"
echo ""
echo "View with:"
echo "  cat $SERVER_LOG"
echo "  cat $CLIENT_LOG"
