#!/bin/bash
#
# Cross-machine audio test for ascii-chat
# Detects which host it's running on and runs local commands there, SSH for the other
#

set -e

PORT=27228
HOST_ONE="workbook-pro"
HOST_ONE_IP="100.121.182.110"  # Tailscale IP for cross-machine connectivity
HOST_TWO="manjaro-twopal"
DURATION=30

# Detect which host we're on
CURRENT_HOST=$(hostname)
shopt -s nocasematch
if [[ "$CURRENT_HOST" == "WorkBook-Pro" ]] || [[ "$CURRENT_HOST" == "$HOST_ONE" ]]; then
  LOCAL_IS_ONE=1
  REPO_ONE="/Users/zfogg/src/github.com/zfogg/ascii-chat"
  REPO_TWO="/home/zfogg/src/github.com/zfogg/ascii-chat"
  LOCAL_REPO="$REPO_ONE"
  BIN_ONE="$REPO_ONE/build/bin/ascii-chat"
  BIN_TWO="$REPO_TWO/build/bin/ascii-chat"
  LOCAL_BIN="$BIN_ONE"
  REMOTE_HOST="$HOST_TWO"
  REMOTE_REPO="$REPO_TWO"
  REMOTE_BIN="$BIN_TWO"
  echo "Running on HOST_ONE ($CURRENT_HOST) - local commands here, SSH to $HOST_TWO"
elif [[ "$CURRENT_HOST" == "manjaro-twopal" ]] || [[ "$CURRENT_HOST" == "$HOST_TWO" ]]; then
  LOCAL_IS_ONE=0
  REPO_ONE="/home/zfogg/src/github.com/zfogg/ascii-chat"
  REPO_TWO="/Users/zfogg/src/github.com/zfogg/ascii-chat"
  LOCAL_REPO="$REPO_TWO"
  BIN_ONE="$REPO_ONE/build/bin/ascii-chat"
  BIN_TWO="$REPO_TWO/build/bin/ascii-chat"
  LOCAL_BIN="$BIN_TWO"
  REMOTE_HOST="$HOST_ONE"
  REMOTE_REPO="$REPO_ONE"
  REMOTE_BIN="$BIN_ONE"
  echo "Running on HOST_TWO ($CURRENT_HOST) - local commands here, SSH to $HOST_ONE"
else
  echo "Unknown host: $CURRENT_HOST - defaulting to SSH for both"
  LOCAL_IS_ONE=-1
fi


echo ""
echo "Cross-machine audio analysis test"
echo "=================================="
echo "HOST 1 (server + client 1): $HOST_ONE"
echo "HOST 2 (client 2):          $HOST_TWO"
echo "Duration: $DURATION seconds"
echo ""

# Helper functions
run_on_one() {
  if [[ $LOCAL_IS_ONE -eq 1 ]]; then
    (cd $REPO_ONE && eval "$1")
  else
    ssh $HOST_ONE "cd $REPO_ONE && $1"
  fi
}

run_on_two() {
  if [[ $LOCAL_IS_ONE -eq 0 ]]; then
    (cd $REPO_TWO && eval "$1")
  else
    ssh $HOST_TWO "cd $REPO_TWO && $1"
  fi
}

# Background process helpers - use nohup to survive parent exit
run_bg_on_one() {
  if [[ $LOCAL_IS_ONE -eq 1 ]]; then
    (cd $REPO_ONE && nohup bash -c "$1" > /dev/null 2>&1 &)
  else
    ssh -f $HOST_ONE "cd $REPO_ONE && nohup bash -c '$1' > /dev/null 2>&1 &"
  fi
}

run_bg_on_two() {
  if [[ $LOCAL_IS_ONE -eq 0 ]]; then
    (cd $REPO_TWO && nohup bash -c "$1" > /dev/null 2>&1 &)
  else
    ssh -f $HOST_TWO "cd $REPO_TWO && nohup bash -c '$1' > /dev/null 2>&1 &"
  fi
}

# Clean up old logs on both hosts
echo "[0/6] Cleaning up old logs..."
run_on_one "rm -f /tmp/server_debug.log /tmp/client1_debug.log /tmp/audio_*.wav /tmp/aec3_*.wav" 2>/dev/null || true
run_on_two "rm -f /tmp/client2_debug.log /tmp/audio_*.wav /tmp/aec3_*.wav" 2>/dev/null || true

# Kill any existing processes on both hosts
echo "[1/6] Killing existing ascii-chat processes..."
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

# Pull latest code on both machines
echo "[2/6] Pulling latest code on both machines..."
run_on_one "$(safe_git_pull $REPO_ONE)"
run_on_two "$(safe_git_pull $REPO_TWO)"

# Rebuild on HOST_ONE
echo "[3/6] Rebuilding on $HOST_ONE..."
run_on_one "cmake --build build --target ascii-chat 2>&1 | tail -5" || {
  echo "WARNING: Build failed on $HOST_ONE, continuing with existing binary"
}

# Rebuild on HOST_TWO
echo "[4/6] Rebuilding on $HOST_TWO (Dev build)..."
run_on_two "cmake -B build -DCMAKE_BUILD_TYPE=Dev && cmake --build build --target ascii-chat 2>&1 | tail -10" || {
  echo "WARNING: Build failed on $HOST_TWO, continuing with existing binary"
}

# Start server on HOST_ONE
echo "[5/8] Starting server on $HOST_ONE:$PORT..."
run_bg_on_one "ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK=1 \
  timeout $((DURATION + 10)) $BIN_ONE server \
  --address 0.0.0.0 --port $PORT \
  --log-file /tmp/server_debug.log"
sleep 2  # Wait for server to start

# Start client 1 on HOST_ONE
echo "[6/8] Starting client 1 on $HOST_ONE with audio analysis..."
run_bg_on_one "ASCIICHAT_DUMP_AUDIO=1 ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK=1 \
  COLUMNS=40 LINES=12 \
  timeout $((DURATION + 5)) $BIN_ONE client \
  --address 127.0.0.1 --port $PORT \
  --audio --audio-analysis \
  --snapshot --snapshot-delay $DURATION \
  --log-file /tmp/client1_debug.log"
sleep 1  # Give client 1 time to connect

# Start client 2 on HOST_TWO
echo "[7/8] Starting client 2 on $HOST_TWO..."
run_bg_on_two "ASCIICHAT_DUMP_AUDIO=1 ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK=1 \
  COLUMNS=40 LINES=12 \
  timeout $((DURATION + 5)) $BIN_TWO client \
  --address $HOST_ONE_IP --port $PORT \
  --webcam-index 2 --microphone-index 9 \
  --audio --audio-analysis \
  --snapshot --snapshot-delay $DURATION \
  --log-file /tmp/client2_debug.log"

echo ""
echo "Running test for $DURATION seconds..."
echo "Play music or speak on BOTH machines!"
echo "========================================"
echo ""

# Wait for the test duration
# Note: We can't use 'wait' on PIDs from SSH/subshells - they're remote PIDs
# The processes will self-terminate via 'timeout' command
sleep $((DURATION + 5))

# Clean up any remaining processes on both hosts
echo "[8/8] Cleaning up processes..."
run_on_one "pkill -9 -x ascii-chat" 2>/dev/null || true
run_on_two "pkill -9 -x ascii-chat" 2>/dev/null || true
sleep 1

echo ""
echo "=========================================================================="
echo "                    CLIENT 1 ($HOST_ONE) AUDIO ANALYSIS"
echo "=========================================================================="
run_on_one "grep -A 30 'AUDIO ANALYSIS REPORT' /tmp/client1_debug.log 2>/dev/null" || echo "Report not found"

echo ""
echo "=========================================================================="
echo "                    CLIENT 2 ($HOST_TWO) AUDIO ANALYSIS"
echo "=========================================================================="
run_on_two "grep -A 30 'AUDIO ANALYSIS REPORT' /tmp/client2_debug.log 2>/dev/null" || echo "Report not found"

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
