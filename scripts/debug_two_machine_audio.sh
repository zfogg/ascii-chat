#!/bin/bash
#
# Debug what's actually failing in test_audio_two_machines.sh
#

set -e

HOST_ONE_IP="100.79.232.55"
HOST_ONE_USER="debian"
HOST_TWO_IP="100.89.125.127"
HOST_TWO_USER="zfogg"
REPO_ONE="/home/debian/ascii-chat"
REPO_TWO="/home/zfogg/src/github.com/zfogg/ascii-chat"

CURRENT_HOST=$(hostname)
shopt -s nocasematch
if [[ "$CURRENT_HOST" == "BeaglePlay" ]] || [[ "$CURRENT_HOST" =~ beagleplay ]]; then
  LOCAL_IS_ONE=1
  REMOTE_HOST="$HOST_TWO_USER@$HOST_TWO_IP"
  REMOTE_REPO="$REPO_TWO"
elif [[ "$CURRENT_HOST" == "manjaro-twopal" ]] || [[ "$CURRENT_HOST" =~ manjaro ]]; then
  LOCAL_IS_ONE=0
  REMOTE_HOST="$HOST_ONE_USER@$HOST_ONE_IP"
  REMOTE_REPO="$REPO_ONE"
else
  echo "ERROR: Unknown host: $CURRENT_HOST"
  exit 1
fi
shopt -u nocasematch

echo "=========================================="
echo "Debugging Two-Machine Audio Test"
echo "=========================================="
echo "Local: $CURRENT_HOST"
echo "Remote: $REMOTE_HOST"
echo ""

# Check 1: Are processes actually running?
echo "[1] Checking if ascii-chat processes are running..."
echo "Local processes:"
pgrep -a ascii-chat || echo "  None running"
echo ""
echo "Remote processes:"
ssh $REMOTE_HOST "pgrep -a ascii-chat" || echo "  None running"
echo ""

# Check 2: Look at actual errors in logs
echo "[2] Checking for errors in client logs..."
echo ""
echo "=== HOST_ONE client log errors ==="
if [[ $LOCAL_IS_ONE -eq 1 ]]; then
  tail -50 /tmp/client1_debug.log 2>/dev/null | grep -i "error\|fail\|warn" || echo "No errors found (or log doesn't exist)"
else
  ssh $REMOTE_HOST "tail -50 /tmp/client1_debug.log 2>/dev/null | grep -i 'error\|fail\|warn'" || echo "No errors found (or log doesn't exist)"
fi
echo ""

echo "=== HOST_TWO client log errors ==="
if [[ $LOCAL_IS_ONE -eq 0 ]]; then
  tail -50 /tmp/client2_debug.log 2>/dev/null | grep -i "error\|fail\|warn" || echo "No errors found (or log doesn't exist)"
else
  ssh $REMOTE_HOST "tail -50 /tmp/client2_debug.log 2>/dev/null | grep -i 'error\|fail\|warn'" || echo "No errors found (or log doesn't exist)"
fi
echo ""

# Check 3: Audio initialization specifically
echo "[3] Checking audio initialization..."
echo ""
echo "=== HOST_ONE audio init ==="
if [[ $LOCAL_IS_ONE -eq 1 ]]; then
  grep -i "portaudio\|audio init\|audio device" /tmp/client1_debug.log 2>/dev/null | head -20 || echo "No audio logs found"
else
  ssh $REMOTE_HOST "grep -i 'portaudio\|audio init\|audio device' /tmp/client1_debug.log 2>/dev/null | head -20" || echo "No audio logs found"
fi
echo ""

echo "=== HOST_TWO audio init ==="
if [[ $LOCAL_IS_ONE -eq 0 ]]; then
  grep -i "portaudio\|audio init\|audio device" /tmp/client2_debug.log 2>/dev/null | head -20 || echo "No audio logs found"
else
  ssh $REMOTE_HOST "grep -i 'portaudio\|audio init\|audio device' /tmp/client2_debug.log 2>/dev/null | head -20" || echo "No audio logs found"
fi
echo ""

# Check 4: Network connectivity
echo "[4] Checking network connectivity..."
echo "Can HOST_ONE reach HOST_TWO?"
if [[ $LOCAL_IS_ONE -eq 1 ]]; then
  nc -zv $HOST_TWO_IP 37228 2>&1 || echo "Cannot connect"
else
  ssh $REMOTE_HOST "nc -zv $HOST_TWO_IP 37228 2>&1" || echo "Cannot connect"
fi
echo ""

echo "Can HOST_TWO reach HOST_ONE?"
if [[ $LOCAL_IS_ONE -eq 0 ]]; then
  nc -zv $HOST_ONE_IP 37228 2>&1 || echo "Cannot connect"
else
  ssh $REMOTE_HOST "nc -zv $HOST_ONE_IP 37228 2>&1" || echo "Cannot connect"
fi
echo ""

# Check 5: Full last 100 lines of each log
echo "[5] Last 100 lines of each log file..."
echo ""
echo "=== /tmp/server_debug.log ==="
if [[ $LOCAL_IS_ONE -eq 1 ]]; then
  tail -100 /tmp/server_debug.log 2>/dev/null || echo "Log doesn't exist"
else
  ssh $REMOTE_HOST "tail -100 /tmp/server_debug.log 2>/dev/null" || echo "Log doesn't exist"
fi
echo ""

echo "=== /tmp/client1_debug.log ==="
if [[ $LOCAL_IS_ONE -eq 1 ]]; then
  tail -100 /tmp/client1_debug.log 2>/dev/null || echo "Log doesn't exist"
else
  ssh $REMOTE_HOST "tail -100 /tmp/client1_debug.log 2>/dev/null" || echo "Log doesn't exist"
fi
echo ""

echo "=== /tmp/client2_debug.log ==="
if [[ $LOCAL_IS_ONE -eq 0 ]]; then
  tail -100 /tmp/client2_debug.log 2>/dev/null || echo "Log doesn't exist"
else
  ssh $REMOTE_HOST "tail -100 /tmp/client2_debug.log 2>/dev/null" || echo "Log doesn't exist"
fi
echo ""

# Check 6: nohup logs (stderr/stdout might go here instead)
echo "[6] Checking nohup logs..."
echo "=== /tmp/client1_nohup.log ==="
if [[ $LOCAL_IS_ONE -eq 1 ]]; then
  cat /tmp/client1_nohup.log 2>/dev/null || echo "Log doesn't exist"
else
  ssh $REMOTE_HOST "cat /tmp/client1_nohup.log 2>/dev/null" || echo "Log doesn't exist"
fi
echo ""

echo "=== /tmp/client2_nohup.log ==="
if [[ $LOCAL_IS_ONE -eq 0 ]]; then
  cat /tmp/client2_nohup.log 2>/dev/null || echo "Log doesn't exist"
else
  ssh $REMOTE_HOST "cat /tmp/client2_nohup.log 2>/dev/null" || echo "Log doesn't exist"
fi

echo ""
echo "=========================================="
echo "Debug complete - review output above"
echo "=========================================="
