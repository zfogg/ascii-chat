"""Exercise zalgo levels, environment precedence, and validation."""
import subprocess
import unicodedata

import pytest

pytestmark = [pytest.mark.integration, pytest.mark.cli]


def marks(data):
    return sum(bool(unicodedata.combining(c)) for c in data.decode("utf-8"))


def test_zalgo_cli(binary, tmp_path, test_environment):
    env = dict(test_environment)
    base = [str(binary), "--quiet", "--log-file", str(tmp_path / "zalgo.log"),
            "mirror", "--test-pattern", "--snapshot", "--snapshot-delay=0",
            "--splash-screen=false", "--width=80", "--height=24", "--color-mode=none",
            "--render-mode=foreground", "--strip-ansi"]

    def run(args, environment=env):
        return subprocess.run(base + args, env=environment, capture_output=True, timeout=30)

    plain = run([])
    assert plain.returncode == 0 and marks(plain.stdout) == 0
    counts = []
    for level in range(1, 11):
        result = run([f"--zalgo={level}"])
        assert result.returncode == 0, result.stderr
        counts.append(marks(result.stdout))
        # Combining marks must never add rows or occupy a whitespace cell.
        text = result.stdout.decode("utf-8")
        previous = ""
        for character in text:
            if unicodedata.combining(character):
                assert previous and not previous.isspace()
            else:
                previous = character
        assert text.count("\n") == plain.stdout.count(b"\n")
    assert counts[0] > 0 and counts == sorted(counts)
    assert marks(run(["--zalgo"]).stdout) == counts[6]
    assert marks(run(["--zalgo", "7"]).stdout) == counts[6]
    assert marks(run([], dict(env, ASCII_CHAT_ZALGO="5")).stdout) == counts[4]
    assert marks(run(["--zalgo=2"], dict(env, ASCII_CHAT_ZALGO="8")).stdout) == counts[1]
    assert marks(run(["--zalgo=3", "--utf8=true"]).stdout) == counts[2]
    disabled_utf8 = run(["--zalgo=3", "--utf8=false"])
    assert disabled_utf8.returncode != 0
    assert marks(disabled_utf8.stdout) == 0
    for value in ("0", "11", "-1", "1.5", "abc", "", "99999999999999999999"):
        result = run([f"--zalgo={value}"])
        assert result.returncode != 0, value
