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

        # Find rows with content (brightness > threshold)
        # Threshold: 10 means background + minimal content
        threshold = 10
        content_rows = [i for i, b in enumerate(brightness_data) if b > threshold]

        if not content_rows:
            print("⚠️  Image appears to be completely blank/black")
            return

        first_content = content_rows[0]
        last_content = content_rows[-1]
        content_height = last_content - first_content + 1
        wasted_top = first_content
        wasted_bottom = height - last_content - 1

        print(f"Content analysis:")
        print(f"  Content range: rows {first_content} to {last_content}")
        print(f"  Content height: {content_height} rows ({100*content_height/height:.1f}%)")
        print()

        print(f"Wasted space:")
        print(f"  Top: {wasted_top} rows ({100*wasted_top/height:.1f}%)")
        print(f"  Bottom: {wasted_bottom} rows ({100*wasted_bottom/height:.1f}%)")
        print()

        # Sample rows throughout
        print("Sample brightness by row:")
        for row_idx in [
            first_content,
            first_content + 10,
            last_content - 10,
            last_content,
            height - 1,
        ]:
            if 0 <= row_idx < len(brightness_data):
                b = brightness_data[row_idx]
                pct = 100 * row_idx / height
                print(f"  Row {row_idx:4d} ({pct:5.1f}%): brightness {b:6.1f}")

        print()

        # Verdict
        if wasted_bottom > height * 0.1:
            print(f"❌ PROBLEM DETECTED: {wasted_bottom} rows ({100*wasted_bottom/height:.1f}%) wasted at BOTTOM")
            print(f"   Canvas should use all {height} rows but only uses {content_height}")
            print(f"   Black bar detected at bottom of image")
        elif wasted_bottom == 0:
            print(f"✅ GOOD: Canvas fully utilized ({content_height}/{height} rows)")
        else:
            print(f"⚠️  Minor waste: {wasted_bottom} rows at bottom ({100*wasted_bottom/height:.1f}%)")


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
