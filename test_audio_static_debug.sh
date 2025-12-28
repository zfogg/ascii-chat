#!/bin/bash
# Test script to debug static/beeps audio issue on Linux
# Run this on the MANJARO client to diagnose audio issues
#
# SYMPTOMS: "loud static and beeps" when receiving audio from another client
# LIKELY CAUSES:
#   1. Sample rate mismatch (device at 44100Hz, we need 48000Hz)
#   2. PulseAudio/PipeWire resampling issues
#   3. Opus decoder state corruption
#   4. PortAudio device format issues

set -e

echo "=== Audio Static/Beeps Debug Script ==="
echo "This script diagnoses 'loud static and beeps' audio issues on Linux"
echo ""

# Check PulseAudio/PipeWire sample rate configuration
echo "=== 1. Checking Audio Subsystem Configuration ==="

# Check if using PipeWire or PulseAudio
if command -v pw-cli &> /dev/null && pw-cli info 0 &> /dev/null; then
    echo "Audio backend: PipeWire"
    echo "Default sample rate: $(pw-cli info 0 2>/dev/null | grep -i rate || echo 'unknown')"
    # Check PipeWire configuration
    if [ -f /etc/pipewire/pipewire.conf ]; then
        echo "PipeWire config sample rate:"
        grep -i "rate" /etc/pipewire/pipewire.conf 2>/dev/null || echo "  (not found)"
    fi
else
    echo "Audio backend: PulseAudio"
    # Check PulseAudio configuration
    if [ -f /etc/pulse/daemon.conf ]; then
        echo "PulseAudio daemon.conf sample rates:"
        grep -i "sample-rate" /etc/pulse/daemon.conf 2>/dev/null || echo "  (default)"
    fi
fi

echo ""

# Check current audio device settings
echo "=== 2. Current Audio Device Info ==="
if command -v pactl &> /dev/null; then
    echo "Default sink:"
    pactl info 2>/dev/null | grep -E "(Default Sink|Server Sample Spec)" || echo "  (couldn't get info)"
    echo ""
    echo "Sink sample rates:"
    pactl list sinks 2>/dev/null | grep -E "(Name:|Sample Spec:|State:)" | head -20 || echo "  (couldn't list sinks)"
fi

echo ""

# Check ALSA configuration
echo "=== 3. ALSA Configuration ==="
if command -v aplay &> /dev/null; then
    echo "Default ALSA device capabilities:"
    aplay -L 2>/dev/null | head -20 || echo "  (couldn't list devices)"
fi

echo ""
echo "=== 4. Potential Fixes ==="
echo ""
echo "If sample rate mismatch is detected, try these fixes:"
echo ""
echo "For PulseAudio:"
echo "  1. Edit /etc/pulse/daemon.conf"
echo "  2. Set: default-sample-rate = 48000"
echo "  3. Set: alternate-sample-rate = 48000"
echo "  4. Restart PulseAudio: pulseaudio -k"
echo ""
echo "For PipeWire:"
echo "  1. Copy /usr/share/pipewire/pipewire.conf to ~/.config/pipewire/"
echo "  2. Edit ~/.config/pipewire/pipewire.conf"
echo "  3. Set: default.clock.rate = 48000"
echo "  4. Restart PipeWire: systemctl --user restart pipewire"
echo ""

# Now run the actual audio test
echo "=== 5. Running Audio Test ==="
echo "Starting server on port 9200..."

# Start server in background with verbose logging
timeout 30 ./build/bin/ascii-chat server --port 9200 --log-file /tmp/audio_static_server.log &
SERVER_PID=$!
sleep 2

echo "Starting client with audio enabled..."
echo "(Press Ctrl+C to stop after testing)"

# Run client with detailed logging
WEBCAM_DISABLED=1 COLUMNS=40 LINES=12 timeout 20 ./build/bin/ascii-chat client \
    --port 9200 \
    --audio \
    --log-file /tmp/audio_static_client.log \
    --snapshot --snapshot-delay 15

# Kill server
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo ""
echo "=== 6. Log Analysis ==="

# Check for sample rate warnings
echo "Sample rate warnings:"
grep -i "sample rate" /tmp/audio_static_server.log /tmp/audio_static_client.log 2>/dev/null || echo "  (none found)"

echo ""
echo "Audio output stats:"
grep -i "Audio output stats" /tmp/audio_static_client.log 2>/dev/null | tail -5 || echo "  (none found)"

echo ""
echo "Client audio receive samples:"
grep "CLIENT AUDIO RECV" /tmp/audio_static_client.log 2>/dev/null | tail -10 || echo "  (none found)"

echo ""
echo "Any clipping detected:"
grep -i "CLIPPING\|clip" /tmp/audio_static_client.log 2>/dev/null | tail -5 || echo "  (none found)"

echo ""
echo "=== Done ==="
echo "Full logs available at:"
echo "  Server: /tmp/audio_static_server.log"
echo "  Client: /tmp/audio_static_client.log"
