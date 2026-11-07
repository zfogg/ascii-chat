"""Check automatic video sizing and explicit font-size overrides."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import _environment  # noqa: F401,E402

import json
import shutil
import subprocess
import tempfile


def main() -> None:
    if not shutil.which("ffprobe"):
        print("ffprobe unavailable; skipping")
        sys.exit(77)
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="ascii-render-size-") as directory:
        root = Path(directory)
        cases = [
            ("auto.mp4", 200, 100, None, (1200, 1200)),
            ("explicit.mp4", 200, 100, "12", (2000, 2000)),
            ("snapshot.png", 200, 100, None, (2000, 2000)),
            ("small.mp4", 80, 24, None, (800, 480)),
        ]
        for name, width, height, font_size, expected in cases:
            output = root / name
            command = [str(binary), "--quiet", "--no-check-update", "mirror", "--test-pattern=0",
                       "--snapshot", "--snapshot-delay=0", "--audio=false", "--fps=30",
                       f"--width={width}", f"--height={height}", f"--render-file={output}"]
            if font_size is not None:
                command.append(f"--render-font-size={font_size}")
            subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=60)
            probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0",
                                    "-show_entries", "stream=width,height", "-of", "json", str(output)],
                                   check=True, stdout=subprocess.PIPE, text=True, timeout=20)
            stream = json.loads(probe.stdout)["streams"][0]
            actual = stream["width"], stream["height"]
            assert actual == expected, f"{name}: {actual} != {expected}"
            print(f"{name}: {actual[0]}x{actual[1]}")


if __name__ == "__main__":
    main()
