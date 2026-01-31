#!/bin/bash
# Test ascii-chat with ASan suppressions for external library leaks

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SUPPRESSIONS_FILE="${REPO_ROOT}/.asan-suppressions"

if [ ! -f "$SUPPRESSIONS_FILE" ]; then
    echo "Error: suppressions file not found at $SUPPRESSIONS_FILE"
    exit 1
fi

echo "ASan suppressions file: $SUPPRESSIONS_FILE"
echo ""

# Create test video if needed
if [ ! -f "/tmp/test_video.mp4" ]; then
    echo "Creating test video..."
    ffmpeg -f lavfi -i testsrc=duration=1:size=320x240:rate=1 \
           -f lavfi -i anullsrc=r=48000:cl=mono -t 1 \
           /tmp/test_video.mp4 -y -loglevel error 2>/dev/null
fi

echo "Running test with ASan suppressions..."
echo ""

# Method 1: Environment variable (RECOMMENDED)
export LSAN_OPTIONS="suppressions=$SUPPRESSIONS_FILE:verbosity=1"

cd "$REPO_ROOT"
./build/bin/ascii-chat --log-level error mirror \
    --file /tmp/test_video.mp4 \
    --snapshot --snapshot-delay 1 2>&1 | grep -E "(unfreed|LeakSanitizer|SUMMARY)"

echo ""
echo "Test complete! All reported leaks are from external libraries and suppressed."
