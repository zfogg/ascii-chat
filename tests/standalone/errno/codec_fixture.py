"""Generate the optional-codec smoke fixture: python codec_fixture.py OUTPUT.mkv."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--broken-video", action="store_true")
    args = parser.parse_args()
    subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i", "testsrc=size=64x64:rate=10",
                    "-f", "lavfi", "-i", "sine=frequency=440", "-t", "1", "-c:v", "ffv1",
                    "-c:a", "pcm_s16le", "-y", str(args.output)], check=True)
    data = args.output.read_bytes()
    if args.broken_video:
        offset = data.index(b"00dc", data.index(b"movi") + 4)
        size = int.from_bytes(data[offset + 4:offset + 8], "little")
        assert 0 < size <= len(data) - offset - 8
        data = data[:offset + 8] + b"\xff" * size + data[offset + 8 + size:]
    else:
        assert b"A_PCM/INT/LIT" in data
        data = data.replace(b"A_PCM/INT/LIT", b"A_BAD/INT/LIT")
    args.output.write_bytes(data)


if __name__ == "__main__":
    main()
