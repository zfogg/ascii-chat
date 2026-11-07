"""Exercise temporal stabilization through the native media pipeline."""

import re
import shutil
import statistics
import subprocess

import pytest

pytestmark = [pytest.mark.integration, pytest.mark.media]


@pytest.fixture(scope="module")
def flicker_video(tmp_path_factory):
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        pytest.skip("ffmpeg unavailable")
    output = tmp_path_factory.mktemp("stabilize-modes") / "flicker.mkv"
    subprocess.run(
        [ffmpeg, "-v", "error", "-f", "lavfi", "-i",
         "nullsrc=s=64x64:r=30:d=2,geq=lum='if(mod(N,2),104,100)':cb=128:cr=128",
         "-c:v", "ffv1", str(output)],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=30,
    )
    return output


@pytest.mark.parametrize("render_mode", ["foreground", "background", "half-block"])
@pytest.mark.parametrize("renderer", ["luminance", "glyph"])
def test_stabilization_reduces_flicker_in_every_mode(binary, test_environment, flicker_video, render_mode, renderer):
    command = [
        str(binary), "--quiet", "--no-check-update", "mirror", "--file", str(flicker_video),
        "--audio=false", "--snapshot", "--snapshot-delay", "0.8", "--width", "20", "--height", "10",
        "--color-mode", "truecolor", "--render-mode", render_mode, "--renderer", renderer,
        "--splash-screen=false",
    ]
    color = b"48" if render_mode == "background" else b"38"
    pattern = rb"\x1b\[" + color + rb";2;(\d+);\d+;\d+m"
    changes = []
    for enabled in (False, True):
        result = subprocess.run(
            command + (["--stabilize"] if enabled else []), env=test_environment,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20, check=True,
        )
        # A uniform frame emits one truecolor sequence for each of its ten rows.
        values = [int(value) for value in re.findall(pattern, result.stdout)][::10]
        assert len(values) >= 5, (render_mode, renderer, enabled, result.stderr[-1000:])
        changes.append(statistics.mean(abs(a - b) for a, b in zip(values, values[1:])))
    assert changes[0] > 0, (render_mode, renderer, changes)
    assert changes[1] < changes[0] * 0.7, (render_mode, renderer, changes)
