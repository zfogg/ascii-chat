"""Compare a release application's snapshots across runtime SIMD selections."""
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def gif_image(width, height):
    # Literal LZW codes with frequent dictionary resets keep the stream at nine bits.
    pixels = [(x * 37 + y * 11) % 256 for y in range(height) for x in range(width)]
    codes = []
    for start in range(0, len(pixels), 128):
        codes.extend([256, *pixels[start:start + 128]])
    codes.append(257)
    data = bytearray()
    bits = value = 0
    for code in codes:
        value |= code << bits
        bits += 9
        while bits >= 8:
            data.append(value & 255)
            value >>= 8
            bits -= 8
    if bits:
        data.append(value)
    blocks = b"".join(bytes([len(data[i:i + 255])]) + data[i:i + 255] for i in range(0, len(data), 255))
    palette = bytes(c for i in range(256) for c in (i, (i * 43) % 256, (i * 19) % 256))
    return (b"GIF89a" + struct.pack("<HHBBB", width, height, 0xf7, 0, 0) + palette +
            b"," + struct.pack("<HHHHB", 0, 0, width, height, 0) + b"\x08" + blocks + b"\x00;")


def main():
    binary = str(Path(sys.argv[1]).resolve())
    backends = sys.argv[2:] or ["auto"]
    env = dict(os.environ, TERM="xterm-256color", NO_COLOR="1")
    for flag in ("--version", "--help"):
        subprocess.run([binary, flag], env=env, capture_output=True, check=True, timeout=30)
    with tempfile.TemporaryDirectory(prefix="ascii-simd-") as temporary:
        source = Path(temporary) / "pixels.gif"
        source.write_bytes(gif_image(67, 19))
        for color, mode in [("none", "foreground"), ("truecolor", "foreground"),
                            ("truecolor", "background"), ("256", "foreground"), ("16", "foreground")]:
            args = [binary, "mirror", "--file", str(source), "--snapshot", "--snapshot-delay", "0",
                    "--splash-screen=false", "--audio=false", "--width", "65", "--height", "17",
                    "--color-mode", color, "--render-mode", mode, "--palette-chars", " .:+#\u2588"]
            reference = None
            for backend in ["scalar", *backends]:
                result = subprocess.run(args, env=dict(env, ASCII_CHAT_SIMD=backend),
                                        capture_output=True, timeout=45)
                if result.returncode:
                    raise RuntimeError(f"{backend}/{color}/{mode}: exit {result.returncode}\n"
                                       + result.stderr.decode(errors="replace"))
                if not result.stdout.strip():
                    raise RuntimeError(f"{backend}/{color}/{mode}: empty snapshot")
                if reference is None:
                    reference = result.stdout
                elif result.stdout != reference:
                    raise RuntimeError(f"{backend}/{color}/{mode}: differs from scalar")
            print(f"{color}/{mode}: {len(reference)} bytes, sha256={hashlib.sha256(reference).hexdigest()}, "
                  f"matched scalar for {', '.join(backends)}", flush=True)


if __name__ == "__main__":
    main()
