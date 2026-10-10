"""Generate the optional-codec smoke fixture: python codec_fixture.py OUTPUT.mkv."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i", "testsrc=size=64x64:rate=10",
                    "-f", "lavfi", "-i", "sine=frequency=440", "-t", "1", "-c:v", "ffv1",
                    "-c:a", "pcm_s16le", "-y", str(args.output)], check=True)
    data = args.output.read_bytes()
    assert b"A_PCM/INT/LIT" in data
    args.output.write_bytes(data.replace(b"A_PCM/INT/LIT", b"A_BAD/INT/LIT"))


if __name__ == "__main__":
    main()
