#!/bin/bash
#
# Debug helper for test_audio_two_machines.sh
# Checks audio device accessibility on both hosts
#

set -e

PORT=37228
HOST_ONE="BeaglePlay"
HOST_ONE_IP="100.79.232.55"
HOST_ONE_USER="debian"
HOST_TWO="manjaro-twopal"
HOST_TWO_IP="100.89.125.127"
HOST_TWO_USER="zfogg"

REPO_ONE="/home/debian/ascii-chat"
REPO_TWO="/home/zfogg/src/github.com/zfogg/ascii-chat"

echo "=========================================="
echo "Audio Device Accessibility Check"
echo "=========================================="
echo ""

# Detect current host
CURRENT_HOST=$(hostname)
shopt -s nocasematch
if [[ "$CURRENT_HOST" == "BeaglePlay" ]] || [[ "$CURRENT_HOST" == "$HOST_ONE" ]]; then
  LOCAL_IS_ONE=1
  REMOTE_HOST="$HOST_TWO_USER@$HOST_TWO_IP"
  REMOTE_REPO="$REPO_TWO"
  echo "Running on HOST_ONE ($CURRENT_HOST)"
elif [[ "$CURRENT_HOST" == "manjaro-twopal" ]] || [[ "$CURRENT_HOST" == "$HOST_TWO" ]]; then
  LOCAL_IS_ONE=0
  REMOTE_HOST="$HOST_ONE_USER@$HOST_ONE_IP"
  REMOTE_REPO="$REPO_ONE"
  echo "Running on HOST_TWO ($CURRENT_HOST)"
else
  echo "ERROR: Unknown host: $CURRENT_HOST"
  exit 1
fi
shopt -u nocasematch

echo "Remote host: $REMOTE_HOST"
echo ""

# Check local audio devices
echo "=== LOCAL AUDIO DEVICES ==="
if command -v pactl &>/dev/null; then
  echo "PulseAudio sources (microphones):"
  pactl list sources short 2>/dev/null || echo "  PulseAudio not running"
  echo ""
  echo "PulseAudio sinks (speakers):"
  pactl list sinks short 2>/dev/null || echo "  PulseAudio not running"
elif command -v arecord &>/dev/null; then
  echo "ALSA recording devices:"
  arecord -L 2>/dev/null | head -20
  echo ""
  echo "ALSA playback devices:"
  aplay -L 2>/dev/null | head -20
else
  echo "  No audio tools found (pactl, arecord)"
fi
echo ""

# Check remote audio devices via SSH
echo "=== REMOTE AUDIO DEVICES (via SSH) ==="
if ssh -o ConnectTimeout=5 $REMOTE_HOST "command -v pactl" &>/dev/null; then
  echo "PulseAudio sources on $REMOTE_HOST:"
  ssh $REMOTE_HOST "pactl list sources short 2>/dev/null || echo '  PulseAudio not running'"
  echo ""
  echo "PulseAudio sinks on $REMOTE_HOST:"
  ssh $REMOTE_HOST "pactl list sinks short 2>/dev/null || echo '  PulseAudio not running'"
elif ssh -o ConnectTimeout=5 $REMOTE_HOST "command -v arecord" &>/dev/null; then
  echo "ALSA recording devices on $REMOTE_HOST:"
  ssh $REMOTE_HOST "arecord -L 2>/dev/null | head -20"
  echo ""
  echo "ALSA playback devices on $REMOTE_HOST:"
  ssh $REMOTE_HOST "aplay -L 2>/dev/null | head -20"
else
  echo "  No audio tools found on remote"
fi
echo ""

# Check for display/audio environment variables
echo "=== ENVIRONMENT CHECK ==="
echo "Local environment:"
echo "  DISPLAY=$DISPLAY"
echo "  PULSE_SERVER=$PULSE_SERVER"
echo "  XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR"
echo ""
echo "Remote environment (via SSH):"
ssh $REMOTE_HOST 'echo "  DISPLAY=$DISPLAY"; echo "  PULSE_SERVER=$PULSE_SERVER"; echo "  XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR"'
echo ""

# Check if ascii-chat can initialize audio locally
echo "=== ASCII-CHAT AUDIO INIT TEST ==="
echo "Testing local ascii-chat audio initialization..."
if [[ $LOCAL_IS_ONE -eq 1 ]]; then
  cd "$REPO_ONE"
else
  cd "$REPO_TWO"
fi

# Try to capture a single frame with audio
timeout 3 ./build/bin/ascii-chat client localhost:$PORT --snapshot --audio 2>&1 | grep -i "audio\|portaudio\|error" || echo "No audio errors found"
echo ""

echo "Testing remote ascii-chat audio via SSH..."
ssh $REMOTE_HOST "cd $REMOTE_REPO && timeout 3 ./build/bin/ascii-chat client localhost:$PORT --snapshot --audio 2>&1" | grep -i "audio\|portaudio\|error" || echo "No audio errors found"
echo ""

echo "=========================================="
echo "Recommended fixes:"
echo "=========================================="
echo "1. For PulseAudio: Set up audio forwarding"
echo "   - Add to ~/.ssh/config: StreamLocalBindUnlink yes"
echo "   - Run: ssh -R /tmp/pulse:$XDG_RUNTIME_DIR/pulse/native $REMOTE_HOST"
echo ""
echo "2. For ALSA: Ensure user is in 'audio' group"
echo "   - Run: groups (check for 'audio')"
echo "   - If missing: sudo usermod -aG audio \$USER"
echo ""
echo "3. Check client debug logs on both machines:"
echo "   - $HOST_ONE:/tmp/client1_debug.log"
echo "   - $HOST_TWO:/tmp/client2_debug.log"
echo "   - Look for PortAudio errors or 'No audio devices found'"
