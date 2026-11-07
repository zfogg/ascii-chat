#!/usr/bin/env python3
"""Exercise the optional renderer through mirror mode with a deterministic image."""
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

from pathlib import Path
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory() as temp:
    image = Path(temp) / "pattern.ppm"
    pixels = bytearray()
    for y in range(120):
        for x in range(160):
            pixels.extend((x * 255 // 159, y * 255 // 119, 255 if x > y else 0))
    image.write_bytes(b"P6\n160 120\n255\n" + pixels)
    common = [binary, "--quiet", "--no-check-update", "mirror", "--file=" + str(image),
              "--snapshot", "--snapshot-delay=0", "--audio=false", "--width=30", "--height=12",
              "--color-mode=none", "--palette=blocks"]
    outputs = []
    for extra in ([], ["--glyph-match"]):
        result = subprocess.run(common + extra, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=40, check=True)
        lines = [line for line in result.stdout.splitlines() if line and not line.startswith(b"[")]
        assert len(lines) >= 12, (extra, result.stdout, result.stderr)
        outputs.append(lines)
    assert outputs[0] != outputs[1], "glyph mode did not change the frame"
    # The palette is Unicode-only; ASCII letters in the result also verify that
    # the local frame contract accepts the matcher's printable glyph set.
    assert any(65 <= b <= 90 for b in b"".join(outputs[1])), outputs[1]
    # Oversized viewports must use the original Unicode palette instead of dropping frames.
    large = [arg for arg in common if not arg.startswith("--width=")]
    result = subprocess.run(large + ["--width=481", "--glyph-match"], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=40, check=True)
    lines = [line for line in result.stdout.splitlines() if line and not line.startswith(b"[")]
    assert len(lines) >= 12 and any(b >= 128 for b in b"".join(lines)), result.stdout
print("mirror glyph matching: baseline, printable glyph, and oversized fallback frames produced")
