"""Exercise native media source replacement and cancellation."""
import shutil
import subprocess

import pytest
from ascii_chat_tests.media import lavfi_fixture

pytestmark = [pytest.mark.integration, pytest.mark.media]


def test_media_capture_open(media_capture_probe, tmp_path, test_environment):
    if shutil.which("ffmpeg") is None:
        pytest.skip("ffmpeg is required")
    encoders = subprocess.run(["ffmpeg", "-hide_banner", "-encoders"], capture_output=True, text=True, timeout=10)
    assert encoders.returncode == 0, encoders.stderr
    if not all(name in encoders.stdout for name in ("libx264", "aac")):
        pytest.skip("ffmpeg requires libx264 and aac encoders")
    media = tmp_path / "movie with spaces.mp4"
    lavfi_fixture(["color=c=red:s=64x48:r=30", "sine=frequency=440:sample_rate=48000"], str(media),
                  ffmpeg_options=["-v", "error", "-y"],
                  output_options=["-t", "1", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac"], timeout=30)
    result = subprocess.run([str(media_capture_probe), str(media)], capture_output=True, timeout=60,
                            env=test_environment)
    assert result.returncode == 0, (result.stdout + result.stderr).decode("utf-8", errors="replace")
