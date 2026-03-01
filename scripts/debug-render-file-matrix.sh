#!/bin/bash
# Debug script for render-file matrix font rendering
# Compiles, generates a test PNG with matrix font, and analyzes it with ImageMagick

set -e

BUILD_DIR="${1:-build}"
OUTPUT_PNG="/tmp/matrix-test-render.png"
LOG_FILE="/tmp/matrix-render-debug.log"

echo "=== Matrix Font Render-File Debug Script ==="
echo "Build directory: $BUILD_DIR"
echo "Output PNG: $OUTPUT_PNG"
echo "Log file: $LOG_FILE"
echo ""

# Compile
echo "[1/4] Compiling..."
cmake --preset default -B "$BUILD_DIR" >/dev/null 2>&1
cmake --build "$BUILD_DIR" 2>&1 | grep -E "(error|warning.*matrix|Building C object.*renderer)" || true
echo "✓ Compiled"
echo ""

# Generate PNG with matrix font
echo "[2/4] Generating PNG with matrix font + test pattern..."
./"$BUILD_DIR"/bin/ascii-chat --log-level debug mirror \
  --snapshot --snapshot-delay 0 \
  --test-pattern \
  --matrix \
  --render-file "$OUTPUT_PNG" \
  --no-splash-screen \
  2>&1 | tee "$LOG_FILE" | grep -E "(MATRIX|Scaling|Final dims|Font resolved)" || true
echo "✓ PNG generated: $OUTPUT_PNG"
echo ""

# Check if file exists and has content
if [ ! -f "$OUTPUT_PNG" ]; then
  echo "✗ ERROR: Output file not created!"
  exit 1
fi

FILE_SIZE=$(stat -f%z "$OUTPUT_PNG" 2>/dev/null || stat -c%s "$OUTPUT_PNG" 2>/dev/null)
echo "[3/4] File Statistics:"
echo "  Size: $(echo "$FILE_SIZE" | awk '{printf "%.1f KB", $1/1024}')"
echo ""

# Use ImageMagick to analyze the image
echo "[4/4] ImageMagick Analysis:"

# Get basic image info
IDENTIFY=$(identify "$OUTPUT_PNG" 2>&1)
echo "  Image: $IDENTIFY"

# Check unique colors (0 unique colors = all black, 1 = probably all same color)
UNIQUE_COLORS=$(identify -format "%k" "$OUTPUT_PNG" 2>&1)
echo "  Unique colors: $UNIQUE_COLORS"

# Get mean color
MEAN_COLOR=$(identify -format "%[fx:mean.r*255],%[fx:mean.g*255],%[fx:mean.b*255]" "$OUTPUT_PNG" 2>&1)
echo "  Mean RGB: $MEAN_COLOR"

# Check if all black (R,G,B all close to 0)
IS_BLACK=$(python3 << 'EOF'
import re
try:
    colors = "$MEAN_COLOR"
    if colors and ',' in colors:
        r, g, b = [float(x) for x in colors.split(',')]
        # All black if all channels < 5
        if r < 5 and g < 5 and b < 5:
            print("YES - All Black!")
        else:
            print("NO - Has content (R:{:.0f}, G:{:.0f}, B:{:.0f})".format(r, g, b))
    else:
        print("ERROR - Could not parse colors")
except Exception as e:
    print(f"ERROR - {e}")
EOF
)
echo "  All Black?: $IS_BLACK"

# Show histogram
echo ""
echo "  Color Histogram:"
convert "$OUTPUT_PNG" -colors 8 histogram:- 2>/dev/null | identify - 2>/dev/null || echo "    (histogram generation skipped)"

echo ""
echo "=== Summary ==="
if [ "$UNIQUE_COLORS" -lt 3 ]; then
  echo "⚠ WARNING: Very few unique colors ($UNIQUE_COLORS) - may still be all black or nearly black"
else
  echo "✓ Good: Multiple colors detected ($UNIQUE_COLORS unique colors)"
fi

echo ""
echo "Full debug log saved to: $LOG_FILE"
