"""Run against a built binary: python tests/integration/test_zalgo_cli.py build/bin/ascii-chat.exe."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401


import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unicodedata


binary = str(Path(sys.argv[1]).resolve())
env = dict(os.environ)
env.pop("ASCII_CHAT_ZALGO", None)
base = [
    binary, "--quiet", "--log-file", str(Path(tempfile.gettempdir()) / "zalgo-cli-test.log"),
    "mirror", "--test-pattern", "--snapshot", "--snapshot-delay=0",
    "--splash-screen=false", "--width=80", "--height=24", "--color-mode=none",
    "--render-mode=foreground", "--strip-ansi",
]


def run(args, environment=env):
    """Capture one synthetic frame with optional CLI/environment overrides."""
    return subprocess.run(base + args, env=environment, capture_output=True, timeout=30)


def marks(data):
    """Count combining marks without counting base glyphs or whitespace."""
    return sum(bool(unicodedata.combining(c)) for c in data.decode("utf-8"))


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
print("PASS: disabled/default, levels 1-10, spaced argument, environment/CLI precedence, invalid values, whitespace and rows")
