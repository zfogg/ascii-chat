"""Check automatic video sizing and explicit font-size overrides."""
import json
import shutil
import subprocess

import pytest

pytestmark = [pytest.mark.integration, pytest.mark.media]


def test_render_file_sizing(binary, tmp_path, test_environment):
    if not shutil.which("ffprobe"):
        pytest.skip("ffprobe unavailable")
    dimensions = {}
    cases = [("auto.mp4", 200, 100, None), ("explicit.mp4", 200, 100, "12"),
             ("snapshot.png", 200, 100, None), ("small.mp4", 80, 24, None),
             ("large.mp4", 1000, 500, None)]
    for name, width, height, font_size in cases:
        output = tmp_path / name
        command = [str(binary), "--quiet", "--no-check-update", "mirror", "--test-pattern=0",
                   "--snapshot", "--snapshot-delay=0", "--audio=false", "--fps=30",
                   f"--width={width}", f"--height={height}", f"--render-file={output}"]
        if font_size is not None:
            command.append(f"--render-font-size={font_size}")
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                       timeout=60, env=test_environment, cwd=tmp_path)
        probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0",
                                "-show_entries", "stream=width,height", "-of", "json", str(output)],
                               check=True, stdout=subprocess.PIPE, text=True, timeout=20)
        stream = json.loads(probe.stdout)["streams"][0]
        dimensions[name] = stream["width"], stream["height"]
    auto = dimensions["auto.mp4"]
    explicit = dimensions["explicit.mp4"]
    assert all(0 < size <= 1280 for size in auto)
    assert all(0 < size <= 1280 for size in dimensions["large.mp4"])
    assert all(a < b for a, b in zip(auto, explicit))
    assert dimensions["snapshot.png"] == explicit
    small = dimensions["small.mp4"]
    assert small[0] * 200 == explicit[0] * 80
    assert small[1] * 100 == explicit[1] * 24
