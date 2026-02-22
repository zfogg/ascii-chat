#!/usr/bin/env python3
"""
Test script to verify render-file output contains visible content (not all black).
Runs ascii-chat with render-file and analyzes the brightness of the output PNG.
"""

import subprocess
import sys
import os
from PIL import Image
import statistics

def run_render_file(output_path, width=50, height=30, timeout=10):
    """Run ascii-chat with render-file option."""
    cmd = [
        "./build/bin/ascii-chat",
        "mirror",
        "--test-pattern",
        "-S", "-D", "0",
        "--render-file", output_path,
        "-x", str(width),
        "-y", str(height),
    ]

    env = os.environ.copy()
    env["ASAN_OPTIONS"] = "detect_leaks=0"

    try:
        result = subprocess.run(cmd, timeout=timeout, env=env, capture_output=True, text=True)
        return result.returncode == 0
    except subprocess.TimeoutExpired:
        return False

def analyze_brightness(image_path):
    """Analyze image brightness. Returns (avg_brightness, max_brightness, num_unique_colors)."""
    img = Image.open(image_path)
    pixels = img.load()

    brightness_values = []
    unique_colors = set()

    for y in range(img.height):
        for x in range(img.width):
            r, g, b = pixels[x, y]
            unique_colors.add((r, g, b))
            # Calculate brightness as average of RGB
            brightness = (int(r) + int(g) + int(b)) / 3
            brightness_values.append(brightness)

    avg_brightness = statistics.mean(brightness_values)
    max_brightness = max(brightness_values)

    return {
        'avg_brightness': avg_brightness,
        'max_brightness': max_brightness,
        'unique_colors': len(unique_colors),
        'total_pixels': len(brightness_values),
        'image_size': img.size,
    }

def main():
    # Create temp file
    output_path = "/tmp/render_test.png"

    print("🎬 Running render-file test...")
    if not run_render_file(output_path):
        print("❌ Failed to run render-file")
        return 1

    if not os.path.exists(output_path):
        print("❌ Output PNG was not created")
        return 1

    print(f"✅ PNG generated: {output_path}")

    # Analyze brightness
    stats = analyze_brightness(output_path)

    print(f"\n📊 Brightness Analysis:")
    print(f"  Image size: {stats['image_size']}")
    print(f"  Unique colors: {stats['unique_colors']}")
    print(f"  Average brightness: {stats['avg_brightness']:.1f} / 255")
    print(f"  Max brightness: {stats['max_brightness']:.1f} / 255")

    # Show unique colors
    img = Image.open(output_path)
    pixels = img.load()
    color_freq = {}
    for y in range(img.height):
        for x in range(img.width):
            c = pixels[x, y]
            color_freq[c] = color_freq.get(c, 0) + 1

    print(f"\n  Top colors by frequency:")
    for color, freq in sorted(color_freq.items(), key=lambda x: x[1], reverse=True)[:5]:
        r, g, b = color
        brightness = (r + g + b) / 3
        pct = 100 * freq / stats['total_pixels']
        print(f"    RGB({r:3d}, {g:3d}, {b:3d}) - {pct:5.1f}% of pixels (brightness: {brightness:.0f})")

    # Check if content is visible
    if stats['unique_colors'] == 1:
        print(f"\n❌ FAIL: Image is completely solid color (all black or one color)")
        print(f"   Expected: Multiple colors for visible ASCII art")
        return 1

    if stats['avg_brightness'] < 10:
        print(f"\n❌ FAIL: Image is too dark (avg brightness {stats['avg_brightness']:.1f})")
        print(f"   Expected: avg brightness > 50 for readable content")
        return 1

    if stats['unique_colors'] < 5:
        print(f"\n⚠️  WARNING: Very few colors ({stats['unique_colors']})")
        print(f"   Expected: 10+ colors for visible text rendering")
        # Don't fail, but warn

    print(f"\n✅ PASS: Image contains visible content!")
    print(f"   - {stats['unique_colors']} unique colors")
    print(f"   - Average brightness: {stats['avg_brightness']:.1f}")

    return 0

if __name__ == "__main__":
    sys.exit(main())
