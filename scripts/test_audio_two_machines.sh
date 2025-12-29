#!/bin/bash
#
# Cross-machine audio test for ascii-chat
# Detects which host it's running on and runs local commands there, SSH for the other
#

set -e

# Cleanup function for Ctrl+C
cleanup() {
  echo ""
  echo "Caught interrupt, cleaning up..."
  run_local "pkill -x ascii-chat" 2>/dev/null || true
  run_remote "pkill -x ascii-chat" 2>/dev/null || true
  # Pop stash if we created one on the remote machine
  if [[ $STASHED_REMOTE -eq 1 ]]; then
    echo "Restoring stashed changes on remote..."
    run_remote "git stash pop" 2>/dev/null || true
  fi
  echo "Cleanup complete."
  exit 130
}

# Track if we stashed on remote (for cleanup)
STASHED_REMOTE=0

PORT=27228
HOST_ONE="workbook-pro"
HOST_ONE_IP="100.113.140.51"  # Tailscale IP for cross-machine connectivity
HOST_TWO="manjaro-twopal"
HOST_TWO_IP="100.89.125.127"  # Tailscale IP for HOST_TWO
DURATION=30

# Paths are CONSTANT regardless of which host we run from
REPO_ONE="/Users/zfogg/src/github.com/zfogg/ascii-chat"  # Path on HOST_ONE (macOS)
REPO_TWO="/home/zfogg/src/github.com/zfogg/ascii-chat"   # Path on HOST_TWO (Linux)
BIN_ONE="$REPO_ONE/build/bin/ascii-chat"
BIN_TWO="$REPO_TWO/build/bin/ascii-chat"

# Detect which host we're on
CURRENT_HOST=$(hostname)
shopt -s nocasematch
if [[ "$CURRENT_HOST" == "WorkBook-Pro" ]] || [[ "$CURRENT_HOST" == "$HOST_ONE" ]]; then
  LOCAL_IS_ONE=1
  LOCAL_REPO="$REPO_ONE"
  LOCAL_BIN="$BIN_ONE"
  REMOTE_HOST="$HOST_TWO"
  REMOTE_REPO="$REPO_TWO"
  REMOTE_BIN="$BIN_TWO"
  echo "Running on HOST_ONE ($CURRENT_HOST) - local commands here, SSH to $HOST_TWO"
elif [[ "$CURRENT_HOST" == "manjaro-twopal" ]] || [[ "$CURRENT_HOST" == "$HOST_TWO" ]]; then
  LOCAL_IS_ONE=0
  LOCAL_REPO="$REPO_TWO"
  LOCAL_BIN="$BIN_TWO"
  REMOTE_HOST="$HOST_ONE"
  REMOTE_REPO="$REPO_ONE"
  REMOTE_BIN="$BIN_ONE"
  echo "Running on HOST_TWO ($CURRENT_HOST) - local commands here, SSH to $HOST_ONE"
else
  echo "ERROR: Unknown host: $CURRENT_HOST"
  echo "This script only works on $HOST_ONE or $HOST_TWO"
  exit 1
fi
shopt -u nocasematch

# Now set up the trap since LOCAL_IS_ONE is determined
trap cleanup SIGINT SIGTERM

echo ""
echo "Cross-machine audio analysis test"
echo "=================================="
echo "Current host: $CURRENT_HOST"
echo "HOST 1 (server + client 1): $HOST_ONE"
echo "HOST 2 (client 2):          $HOST_TWO"
echo "Duration: $DURATION seconds"
echo ""

# Helper functions - local/remote based on where script is running
run_local() {
  (cd $LOCAL_REPO && eval "$1")
}

run_remote() {
  ssh $REMOTE_HOST "cd $REMOTE_REPO && $1"
}

run_bg_local() {
  (cd $LOCAL_REPO && nohup bash -c "$1" > /dev/null 2>&1 &)
}

run_bg_remote() {
  ssh -f $REMOTE_HOST "cd $REMOTE_REPO && nohup bash -c '$1' > /dev/null 2>&1"
}

# Helper functions - target specific host regardless of where we're running
run_on_one() {
  if [[ $LOCAL_IS_ONE -eq 1 ]]; then
    run_local "$1"
  else
    ssh $HOST_ONE "cd $REPO_ONE && $1"
  fi
}

run_on_two() {
  if [[ $LOCAL_IS_ONE -eq 0 ]]; then
    run_local "$1"
  else
    ssh $HOST_TWO "cd $REPO_TWO && $1"
  fi
}

run_bg_on_one() {
  if [[ $LOCAL_IS_ONE -eq 1 ]]; then
    run_bg_local "$1"
  else
    ssh -f $HOST_ONE "cd $REPO_ONE && nohup bash -c '$1' > /dev/null 2>&1"
  fi
}

run_bg_on_two() {
  if [[ $LOCAL_IS_ONE -eq 0 ]]; then
    run_bg_local "$1"
  else
    ssh -f $HOST_TWO "cd $REPO_TWO && nohup bash -c '$1' > /dev/null 2>&1"
  fi
}

# Clean up old logs on both hosts
echo "[1/8] Cleaning up old logs..."
run_on_one "rm -f /tmp/server_debug.log /tmp/client1_debug.log /tmp/audio_*.wav /tmp/aec3_*.wav" 2>/dev/null || true
run_on_two "rm -f /tmp/client2_debug.log /tmp/audio_*.wav /tmp/aec3_*.wav" 2>/dev/null || true

# Kill any existing processes on both hosts
echo "[2/8] Killing existing ascii-chat processes..."
run_on_one "pkill -x ascii-chat" 2>/dev/null || true
run_on_two "pkill -x ascii-chat" 2>/dev/null || true
sleep 1

# Function to safely pull with stash handling
safe_git_pull() {
  local repo="$1"
  echo "cd $repo && {
    # Check for uncommitted changes
    if git diff --quiet && git diff --staged --quiet; then
      STASHED=0
    else
      echo 'Stashing local changes...'
      git stash push -m 'auto-stash for test' && STASHED=1 || STASHED=0
    fi

    # Pull with fast-forward or rebase
    git pull --ff-only 2>&1 | tail -3 || git pull --rebase 2>&1 | tail -3

    # Restore stash if we stashed
    if [ \"\$STASHED\" = \"1\" ]; then
      echo 'Restoring stashed changes...'
      git stash pop || echo 'Warning: stash pop failed, changes remain in stash'
    fi
  }"
}

# Pull latest code on remote machine only (local already has latest)
echo "[3/8] Pulling latest code on remote machine..."
# Check if remote has uncommitted changes (will need stash)
if ! run_remote "git diff --quiet && git diff --staged --quiet" 2>/dev/null; then
  STASHED_REMOTE=1
fi
run_remote "$(safe_git_pull $REMOTE_REPO)"
# If we get here, safe_git_pull succeeded and popped the stash
STASHED_REMOTE=0

# Rebuild on HOST_ONE
echo "[4/8] Rebuilding on $HOST_ONE..."
run_on_one "cmake --build build --target ascii-chat 2>&1 | tail -5" || {
  echo "WARNING: Build failed on $HOST_ONE, continuing with existing binary"
}

# Rebuild on HOST_TWO
echo "[5/8] Rebuilding on $HOST_TWO (Dev build)..."
run_on_two "cmake -B build -DCMAKE_BUILD_TYPE=Dev && cmake --build build --target ascii-chat 2>&1 | tail -10" || {
  echo "WARNING: Build failed on $HOST_TWO, continuing with existing binary"
}

# Start server on HOST_ONE
echo "[6/8] Starting server on $HOST_ONE:$PORT..."
run_bg_on_one "ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK=1 timeout $((DURATION + 10)) $BIN_ONE server --address 0.0.0.0 --port $PORT --log-file /tmp/server_debug.log"
sleep 2  # Wait for server to start

# Start client 1 on HOST_ONE
echo "[7/8] Starting client 1 on $HOST_ONE with audio analysis..."
run_bg_on_one "ASCIICHAT_DUMP_AUDIO=1 ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK=1 COLUMNS=40 LINES=12 timeout $((DURATION + 5)) $BIN_ONE client localhost:$PORT --test-pattern --audio --audio-analysis --snapshot --snapshot-delay $DURATION --log-file /tmp/client1_debug.log"

# Start client 2 on HOST_TWO
echo "[8/8] Starting client 2 on $HOST_TWO..."
run_bg_on_two "ASCIICHAT_DUMP_AUDIO=1 ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK=1 COLUMNS=40 LINES=12 timeout $((DURATION + 5)) $BIN_TWO client $HOST_ONE:$PORT --test-pattern --microphone-index 9 --audio --audio-analysis --snapshot --snapshot-delay $DURATION --log-file /tmp/client2_debug.log"

echo ""
echo "Running test for $DURATION seconds..."
echo "Play music or speak on BOTH machines!"
echo "========================================"
echo ""

# Wait for the test duration
# Note: We can't use 'wait' on PIDs from SSH/subshells - they're remote PIDs
# The processes will self-terminate via 'timeout' command
sleep $((DURATION))

# Give clients time to write analysis report on graceful exit
echo "Waiting for clients to finish and write analysis reports..."
sleep 3

# Clean up any remaining processes on both hosts (use SIGTERM for graceful shutdown)
echo "Cleaning up any remaining processes..."
run_on_one "pkill -x ascii-chat" 2>/dev/null || true
run_on_two "pkill -x ascii-chat" 2>/dev/null || true
sleep 2

echo ""
echo "=========================================================================="
echo "                    CLIENT 1 ($HOST_ONE) AUDIO ANALYSIS"
echo "=========================================================================="
run_on_one "grep -A 80 'AUDIO ANALYSIS REPORT' /tmp/client1_debug.log 2>/dev/null" || echo "Report not found"

echo ""
echo "=========================================================================="
echo "                    CLIENT 2 ($HOST_TWO) AUDIO ANALYSIS"
echo "=========================================================================="
run_on_two "grep -A 80 'AUDIO ANALYSIS REPORT' /tmp/client2_debug.log 2>/dev/null" || echo "Report not found"

echo ""
echo "=========================================================================="
echo "                              AEC3 METRICS"
echo "=========================================================================="
echo "Client 1 AEC3:"
run_on_one "grep 'AEC3:' /tmp/client1_debug.log 2>/dev/null | tail -5" || echo "Not found"
echo ""
echo "Client 2 AEC3:"
run_on_two "grep 'AEC3:' /tmp/client2_debug.log 2>/dev/null | tail -5" || echo "Not found"

echo ""
echo "Full logs available at:"
echo "  $HOST_ONE:/tmp/server_debug.log"
echo "  $HOST_ONE:/tmp/client1_debug.log"
echo "  $HOST_TWO:/tmp/client2_debug.log"
