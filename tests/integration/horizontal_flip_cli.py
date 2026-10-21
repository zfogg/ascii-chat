"""Verify horizontal orientation through real native snapshot rendering."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="ascii-horizontal-flip-") as temporary:
    directory = Path(temporary)
    image = Path(__file__).resolve().parents[2] / "web/web/tests/fixtures/media/still.png"
    env = {k: v for k, v in os.environ.items() if not k.startswith("ASCII_CHAT_")}
    env.update(CLAUDECODE="1", XDG_CONFIG_HOME=str(directory), APPDATA=str(directory))
    def snapshot(*flags, extra_env=None, webcam=False):
        result = subprocess.run([
            str(binary), "--no-check-update", "--log-level", "error",
            "mirror",
            *(["--test-pattern"] if webcam else ["--file", str(image)]), "--audio=false", "--snapshot", "--snapshot-delay", "0",
            "--width", "32", "--height", "12", "--color-mode", "none", "--strip-ansi",
            "--splash-screen=false", "--render-mode", "foreground", "--palette", "standard", *flags,
        ], cwd=directory, env={**env, **(extra_env or {})}, capture_output=True, timeout=30)
        assert result.returncode == 0, result.stderr.decode("utf-8", errors="replace")
        assert len(result.stdout) > 100
        return result.stdout
    normal = snapshot("--flip-x=false")
    flipped = snapshot("--flip-x=true")
    assert normal != flipped, "Explicit flip must change asymmetric media output"
    assert snapshot() == normal, "Media defaults to unmirrored"
    assert snapshot("--flip-x") == flipped, "Bare flag explicitly enables horizontal flip"
    assert snapshot(extra_env={"ASCII_CHAT_FLIP_X": "true"}) == flipped
    assert snapshot("--flip-x=false", extra_env={"ASCII_CHAT_FLIP_X": "true"}) == normal
    assert snapshot(extra_env={"ASCII_CHAT_FLIP_X": "false"}) == normal
    webcam_default = snapshot(webcam=True)
    webcam_mirrored = snapshot("--flip-x=true", webcam=True)
    webcam_normal = snapshot("--flip-x=false", webcam=True)
    assert webcam_default == webcam_mirrored
    assert webcam_default != webcam_normal
    print("PASS: native webcam/test-pattern and asymmetric file snapshots: default, on, off, bare CLI flag, environment and CLI precedence")
