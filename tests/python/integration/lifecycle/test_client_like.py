"""Client-like session lifecycle and frame-rate source contracts."""
import subprocess

import pytest

pytestmark = pytest.mark.integration


@pytest.mark.parametrize("scenario", [
    "success", "mirror", "prepare-error", "shutdown", "retry-success",
    "backoff", "no-retry", "veto", "interrupt", "limit", "discovery-destroy",
    "fps-source-client", "fps-source-mirror", "fps-override-client",
    "fps-override-mirror", "fps-override-60",
])
def test_client_like_lifecycle(lifecycle_probe, tmp_path, test_environment, scenario):
    media = tmp_path / "source.y4m"
    if scenario.startswith("fps-"):
        frame = b"FRAME\n" + bytes([128]) * (16 * 16 * 3 // 2)
        media.write_bytes(b"YUV4MPEG2 W16 H16 F24:1 Ip A1:1 C420jpeg\n" + frame * 3)
    arguments = [str(lifecycle_probe), scenario]
    if scenario.startswith("fps-"):
        arguments.append(str(media))
    result = subprocess.run(arguments, capture_output=True, timeout=20,
                            env=dict(test_environment, CLAUDECODE="1"))
    assert result.returncode == 0, (result.stdout + result.stderr).decode("utf-8", errors="replace")
