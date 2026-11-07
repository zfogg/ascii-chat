#!/usr/bin/env python3
"""Exercise the optional renderer through mirror mode with a deterministic image."""
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401
from ascii_chat_tests.media import ppm_image

from pathlib import Path
import os
import re
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
    image.write_bytes(ppm_image(160, 120, pixels))
    common = [binary, "--quiet", "--no-check-update", "mirror", "--file=" + str(image),
              "--snapshot", "--snapshot-delay=0", "--audio=false", "--width=30", "--height=12",
              "--color-mode=none"]

    def capture(extra, env=None, binary_args=()):
        result = subprocess.run([common[0], *binary_args, *common[1:], *extra], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, env=env, timeout=40, check=True)
        lines = [line for line in result.stdout.splitlines() if line and not line.startswith(b"[")]
        assert len(lines) >= 12, (extra, result.stdout, result.stderr)
        return lines

    default = capture([])
    assert default == capture(["--renderer=luminance"]), "default must be luminance"
    glyph = capture(["--renderer=glyph"])
    assert glyph != default, "glyph mode did not change the frame"
    standard_chars = set(b"   ...',;:clodxkO0KXNWM")
    glyph_chars = set(b"".join(re.sub(rb"\x1b\[[0-9;]*[a-zA-Z]", b"", line) for line in glyph))
    assert glyph_chars - standard_chars, "default glyph mode should use the 95-glyph atlas"
    restricted = capture(["--renderer=glyph", "--palette-chars=@#"])
    for line in restricted:
        # Monochrome rows may use CSI repeat to compress a run.
        assert set(re.sub(rb"\x1b\[[0-9;]*[a-zA-Z]", b"", line)) <= set(b"@#"), line
    standard = capture(["--renderer=glyph", "--palette=standard"])
    env = dict(os.environ, ASCII_CHAT_RENDERER="glyph", ASCII_CHAT_PALETTE="standard")
    assert standard == capture([], env=env), "environment renderer/palette selection differs"
    config = Path(temp) / "renderer.toml"
    config.write_text('[display]\nrenderer = "glyph"\npalette = "standard"\n')
    assert standard == capture([], binary_args=("--config", str(config))), "config renderer/palette selection differs"
    allowed = set(b"   ...',;:clodxkO0KXNWM")
    for line in standard:
        assert set(re.sub(rb"\x1b\[[0-9;]*[a-zA-Z]", b"", line)) <= allowed, line
    # A Unicode palette remains usable via the luminance fallback.
    unicode = capture(["--renderer=glyph", "--palette=blocks"])
    assert unicode == capture(["--renderer=luminance", "--palette=blocks"]), "Unicode fallback differs"
    large = [arg for arg in common if not arg.startswith("--width=")]
    result = subprocess.run(large + ["--width=481", "--renderer=glyph", "--palette=blocks"],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=40, check=True)
    lines = [line for line in result.stdout.splitlines() if line and not line.startswith(b"[")]
    assert len(lines) >= 12 and any(b >= 128 for b in b"".join(lines)), result.stdout
    invalid = subprocess.run(common + ["--renderer=unknown"], stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, timeout=40)
    assert invalid.returncode != 0, "invalid renderer must be rejected"
print("mirror glyph matching: baseline, printable glyph, and oversized fallback frames produced")
