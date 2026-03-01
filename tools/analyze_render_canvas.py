#!/usr/bin/env python3
"""
Analyze render-file PNG output to detect wasted canvas space.

This script checks if the rendered image fully utilizes the output canvas
or if there's a black bar at the bottom indicating unused space.

Usage:
    python3 analyze_render_canvas.py <output.png>
"""
import struct
import zlib
import sys


def read_png_data(filepath):
    """Read raw PNG data and analyze pixel brightness."""
    with open(filepath, 'rb') as f:
        # Verify PNG signature
        signature = f.read(8)
        if signature != b'\x89PNG\r\n\x1a\n':
            print("Error: Not a valid PNG file")
            return

        # Read IHDR chunk
        chunk_len = struct.unpack('>I', f.read(4))[0]
        chunk_type = f.read(4)
        ihdr_data = f.read(chunk_len)
        crc = f.read(4)

        width = struct.unpack('>I', ihdr_data[0:4])[0]
        height = struct.unpack('>I', ihdr_data[4:8])[0]
        bit_depth = ihdr_data[8]
        color_type = ihdr_data[9]

        print(f"Image: {width}×{height} (bit_depth={bit_depth}, color_type={color_type})")
        print()

        # Collect IDAT (image data) chunks
        idat_data = b''
        while True:
            chunk_len_bytes = f.read(4)
            if not chunk_len_bytes:
                break
            chunk_len = struct.unpack('>I', chunk_len_bytes)[0]
            chunk_type = f.read(4)
            chunk_data = f.read(chunk_len)
            crc = f.read(4)

            if chunk_type == b'IDAT':
                idat_data += chunk_data
            elif chunk_type == b'IEND':
                break

        # Decompress image data
        try:
            uncompressed = zlib.decompress(idat_data)
        except Exception as e:
            print(f"Error: Failed to decompress PNG data: {e}")
            return

        # PNG scanlines: [filter_byte][pixel_data...]
        # For RGB: 3 bytes per pixel
        bytes_per_pixel = 3
        row_size = width * bytes_per_pixel + 1  # +1 for filter byte

        # Calculate brightness for each row
        brightness_data = []
        for row_idx in range(height):
            row_start = row_idx * row_size
            row_end = row_start + row_size
            row_data = uncompressed[row_start:row_end]

            if len(row_data) < row_size:
                break

            # Skip filter byte, average the RGB values
            pixels = row_data[1:]
            if pixels:
                avg_brightness = sum(pixels) / len(pixels)
                brightness_data.append(avg_brightness)

        # Analyze brightness distribution
        print("Brightness distribution:")
        print()

        # Print histogram of all rows
        for i in range(0, len(brightness_data), max(1, len(brightness_data) // 20)):
            row_idx = i
            b = brightness_data[row_idx]
            pct = 100 * row_idx / height
            bar_len = int(b / 255 * 50)
            bar = '█' * bar_len + '░' * (50 - bar_len)
            print(f"  Row {row_idx:4d} ({pct:5.1f}%): {bar} {b:6.1f}")

        print()

        # Find rows with ACTUAL content (bright pixels, not just faint text)
        # Use different thresholds to detect the problem
        # Thresholds adjusted for dark rendering (ASCII on dark background)
        threshold_high = 20    # Definitely has content (ASCII characters)
        threshold_med = 10     # Medium content (some pixels)
        threshold_low = 3      # Very faint (mostly black)

        rows_bright = [i for i, b in enumerate(brightness_data) if b > threshold_high]
        rows_medium = [i for i, b in enumerate(brightness_data) if b > threshold_med]
        rows_faint = [i for i, b in enumerate(brightness_data) if b > threshold_low]

        print(f"Content detection:")
        print(f"  Rows with BRIGHT pixels (>{threshold_high}): {len(rows_bright)} rows")
        if rows_bright:
            print(f"    Range: {rows_bright[0]} to {rows_bright[-1]}")

        print(f"  Rows with MEDIUM pixels (>{threshold_med}): {len(rows_medium)} rows")
        if rows_medium:
            print(f"    Range: {rows_medium[0]} to {rows_medium[-1]}")

        print(f"  Rows with FAINT pixels (>{threshold_low}): {len(rows_faint)} rows")
        if rows_faint:
            print(f"    Range: {rows_faint[0]} to {rows_faint[-1]}")

        print()

        # Use bright pixels as the real content indicator
        if rows_bright:
            first_content = rows_bright[0]
            last_content = rows_bright[-1]
            content_height = last_content - first_content + 1
            wasted_top = first_content
            wasted_bottom = height - last_content - 1

            print(f"ACTUAL content (bright pixels only):")
            print(f"  Range: rows {first_content} to {last_content}")
            print(f"  Content height: {content_height} rows ({100*content_height/height:.1f}%)")
            print()

            print(f"Wasted space:")
            print(f"  Top: {wasted_top} rows ({100*wasted_top/height:.1f}%)")
            print(f"  Bottom: {wasted_bottom} rows ({100*wasted_bottom/height:.1f}%)")
            print()

            # Detailed bottom analysis
            if wasted_bottom > 0:
                print(f"Bottom rows (potential black bar):")
                for row_idx in range(max(0, last_content - 10), height):
                    b = brightness_data[row_idx]
                    pct = 100 * row_idx / height
                    status = "█" if b < 5 else "░"
                    print(f"  Row {row_idx:4d} ({pct:5.1f}%): brightness {b:6.1f} {status}")

            print()

            # Check for black bar at bottom by comparing bottom sections
            print(f"Checking for BLACK BAR at bottom:")

            # Sample middle and bottom sections
            middle_start = (height // 2) - 50
            middle_end = (height // 2) + 50
            bottom_start = height - 100

            middle_brightness = sum(brightness_data[middle_start:middle_end]) / 100
            bottom_brightness = sum(brightness_data[bottom_start:]) / 100

            print(f"  Middle section brightness (rows {middle_start}-{middle_end}): {middle_brightness:.2f}")
            print(f"  Bottom section brightness (rows {bottom_start}-{height}): {bottom_brightness:.2f}")
            print()

            # Calculate percentage of truly black pixels in bottom section
            bottom_black_rows = sum(1 for i in range(bottom_start, height) if brightness_data[i] < 10)
            bottom_black_pct = 100 * bottom_black_rows / (height - bottom_start)

            print(f"  Bottom rows ({bottom_start}-{height-1}): {bottom_black_pct:.1f}% are mostly black")
            print()

            # Verdict
            if wasted_bottom > height * 0.05:
                print(f"❌ BLACK BAR DETECTED: {wasted_bottom} rows ({100*wasted_bottom/height:.1f}%) wasted at BOTTOM")
                print(f"   Expected: {height} rows utilized")
                print(f"   Actual: {content_height} rows with content")
                print(f"   Problem: Text isn't filling bottom of canvas")
            elif bottom_black_pct > 85:
                print(f"❌ BLACK BAR AT BOTTOM: {bottom_black_rows} rows are {bottom_black_pct:.1f}% black")
                print(f"   Rows {bottom_start}-{height-1} should have content but are mostly blank")
            elif wasted_bottom == 0:
                print(f"✅ GOOD: Canvas fully utilized (all {height} rows)")
            else:
                print(f"⚠️  Minor waste: {wasted_bottom} rows at bottom ({100*wasted_bottom/height:.1f}%)")
        else:
            print("⚠️  No bright pixels detected - image may be too dark")


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Analyze render-file PNG canvas usage")
        print()
        print("Usage: python3 analyze_render_canvas.py <output.png>")
        print()
        print("This script detects if the rendered image has a black bar at the")
        print("bottom indicating wasted canvas space.")
        sys.exit(1)

    read_png_data(sys.argv[1])
