"""Verify PNG decoding by comparing a snapshot with equivalent uncompressed pixels.

Usage: python tests/integration/png_snapshot.py /path/to/ascii-chat
"""

import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib


def chunk(kind, data):
    return (struct.pack(">I", len(data)) + kind + data
            + struct.pack(">I", zlib.crc32(kind + data)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    binary = str(args.binary.resolve())
    width, height = 64, 48
    rows = [bytes(c for x in range(width) for c in (x * 4, y * 5, 128))
            for y in range(height)]
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(b"".join(b"\0" + row for row in rows)))
           + chunk(b"IEND", b""))
    ppm = f"P6\n{width} {height}\n255\n".encode() + b"".join(rows)
    env = dict(os.environ, TERM="xterm-256color", NO_COLOR="1")
    with tempfile.TemporaryDirectory(prefix="ascii-png-") as temporary:
        directory = Path(temporary)
        snapshots = []
        for extension, data in (("ppm", ppm), ("png", png)):
            source = directory / f"pixels.{extension}"
            source.write_bytes(data)
            command = [binary, "--no-check-update", "--log-file", str(directory / f"{extension}.log"),
                       "mirror", "--file", str(source), "--snapshot", "--snapshot-delay", "0",
                       "--splash-screen=false", "--audio=false", "--width", "64", "--height", "24",
                       "--color-mode", "none", "--strip-ansi"]
            result = subprocess.run(command, env=env, capture_output=True, timeout=30)
            if result.returncode or not result.stdout.strip():
                raise RuntimeError(f"{extension} snapshot failed ({result.returncode}):\n"
                                   + result.stderr.decode(errors="replace"))
            snapshots.append(result.stdout)
        if snapshots[0] != snapshots[1]:
            raise RuntimeError("PNG snapshot differs from equivalent uncompressed pixels")
        print(f"PASS: PNG and PPM snapshots match ({len(snapshots[0])} bytes)")


if __name__ == "__main__":
    main()
