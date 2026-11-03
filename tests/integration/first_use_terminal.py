"""Exercise first-use notices and per-mode persistence in a real terminal."""
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import argparse
import importlib.util
import os
import re
import socket
import subprocess
import tempfile
import time
from pathlib import Path
if os.name == "nt" and importlib.util.find_spec("winpty") is None:
    print("SKIP: first-use terminal tests need pywinpty")
    raise SystemExit(77)
from terminal_ui import Terminal, free_port


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    binary = str(parser.parse_args().binary.resolve())
    with tempfile.TemporaryDirectory(prefix="ascii-first-use-") as temp:
        root = Path(temp)
        env = {"ASCII_CHAT_DISABLE_FIRST_USE": "0", "ASCII_CHAT_FIRST_USE_DIR": str(root)}
        base = [binary, "--no-check-update", "--utf8=true", "--color=true", "--color-mode=truecolor",
                "--log-file", str(root / "app.log")]
        mirror = ["mirror", "--test-pattern", "--audio=false", "--splash-screen=false"]

        def finish(term):
            term.close()
            if os.name == "nt":
                # ConPTY may release a terminated child's log handle asynchronously.
                time.sleep(.5)

        def run(args, expected, marker):
            term = Terminal(base + args, rows=40, cols=120, environment=env)
            try:
                term.expect(lambda text: expected in text and "Enter done" in text,
                            "First-use instructions missing", timeout=30)
                assert not marker.exists(), "Notice was marked before acknowledgement"
                if expected == "Mirror mode":
                    lines = term.screen.display
                    top = next(i for i, line in enumerate(lines) if "┌" in line and "┐" in line)
                    bottom = next(i for i, line in enumerate(lines) if "└" in line and "┘" in line)
                    left, right = lines[top].index("┌"), lines[top].index("┐")
                    assert abs(left - (term.screen.columns - right - 1)) <= 1, "Popup is not horizontally centered"
                    assert top >= 4, "Not enough room for the logo above the popup"
                    logo = lines[top - 4:top]
                    assert any("___" in line for line in logo), "Animated logo missing above popup"
                    assert abs((top - 4) - (term.screen.lines - bottom - 1)) <= 1, "Logo and popup are not vertically centered"
                    assert any("│" in line for line in lines[top + 1:bottom]), "UTF-8 side borders missing"
                    row = next(i for i, line in enumerate(lines) if "?" in line and "keyboard shortcuts" in line)
                    col = lines[row].index("?")
                    assert term.screen.buffer[row][col].fg != "default", "Shortcut is not colorized"
                    term.write("?")
                    term.expect(lambda text: "Keyboard Shortcuts" in text, "Help did not open", timeout=10)
                    term.write("?")
                    term.expect(lambda text: expected in text and "Enter done" in text,
                                "First-use notice did not return from help", timeout=10)
                deadline = time.monotonic() + 8
                while not marker.exists() and time.monotonic() < deadline:
                    text = term.pump(.25)
                    if expected in text and "Enter done" in text:
                        term.write("\r")
                assert marker.exists(), "Notice was not marked after acknowledgement"
            finally:
                finish(term)

        marker = root / "first-use-mirror"
        tiny_port = free_port()
        tiny_marker = root / "first-use-server"
        tiny = Terminal(base + ["server", "--port", str(tiny_port), "--websocket-port", str(free_port()),
                                "--status-screen=false"], rows=7, cols=19, environment=env)
        try:
            deadline = time.monotonic() + 30
            while True:
                tiny.pump(.1)
                try:
                    with socket.create_connection(("127.0.0.1", tiny_port), timeout=.2):
                        break
                except OSError:
                    assert time.monotonic() < deadline, "Server did not start in a too-small terminal"
            tiny.write("\x1b")
            tiny.pump(.5)
            assert not tiny_marker.exists(), "Covered notice was marked as seen"
            tiny.resize(40, 120)
            tiny.expect(lambda text: "Server mode" in text and "Enter done" in text,
                        "Covered notice was dismissed or lost", timeout=15)
            tiny.write("\r")
            deadline = time.monotonic() + 10
            while not tiny_marker.exists() and time.monotonic() < deadline:
                tiny.pump(.1)
            assert tiny_marker.exists(), "Resized notice could not be acknowledged"
        finally:
            finish(tiny)
        tiny_marker.unlink()
        small = Terminal(base + mirror, rows=8, cols=25, environment=env)
        try:
            first = small.expect(lambda text: re.search(r"1/(\d+) Enter next", text) is not None,
                                 "Small terminal did not show a paged notice", timeout=30)
            pages = int(re.search(r"1/(\d+) Enter next", first).group(1))
            assert pages > 1
            time.sleep(11)
            text = small.pump(.5)
            assert re.search(r"1/\d+ Enter next", text), "Notice advanced without input"
            assert not marker.exists(), "Incomplete notice was marked as seen"
            for page in range(2, pages + 1):
                small.write("\r")
                small.expect(lambda text, page=page: f"{page}/{pages} Enter " in text,
                             f"Page {page} did not appear", timeout=10)
                assert not marker.exists(), "Notice was marked before final acknowledgement"
            small.write("\r")
            deadline = time.monotonic() + 5
            while not marker.exists() and time.monotonic() < deadline:
                small.pump(.1)
            assert marker.exists(), "Final page acknowledgement was not recorded"
        finally:
            finish(small)
        marker.unlink()
        dismissed = Terminal(base + mirror, rows=40, cols=120, environment=env)
        try:
            dismissed.expect(lambda text: "Mirror mode" in text,
                             "Notice did not reopen after marker removal", timeout=30)
            dismissed.write("\x1b")
            dismissed.expect(lambda text: "Mirror mode" not in text,
                             "Escape did not dismiss the notice", timeout=10)
            assert not marker.exists(), "Escape marked the notice complete"
        finally:
            finish(dismissed)
        run(mirror, "Mirror mode", marker)
        assert not (root / "first-use-server").exists()
        run(["server", "--port", str(free_port()), "--websocket-port", str(free_port()), "--status-screen"],
            "Server mode", root / "first-use-server")
        (root / "first-use-server").unlink()
        run(["server", "--port", str(free_port()), "--websocket-port", str(free_port()), "--status-screen=false"],
            "Server mode", root / "first-use-server")
        run(["client", "127.0.0.1", "--port", str(free_port()), "--test-pattern", "--audio=false"],
            "Client mode", root / "first-use-client")
        run(["--discovery-service", "127.0.0.1", "--discovery-service-port", str(free_port()), "--test-pattern", "--audio=false"],
            "Discovery mode", root / "first-use-discovery")
        run(["discovery-service", "--port", str(free_port()), "--websocket-port", str(free_port()),
             "--database", str(root / "discovery.db"), "--status-screen"],
            "Discovery service mode", root / "first-use-discovery-service")
        run(["mcp-server", "--transport", "http", "--address", "127.0.0.1", "--port", str(free_port())],
            "MCP server mode", root / "first-use-mcp-server")
        term = Terminal(base + mirror, rows=40, cols=120, environment=env)
        try:
            text = term.pump(3)
            assert "Mirror mode" not in text, "Notice repeated on subsequent use"
        finally:
            finish(term)
        marker.unlink()
        result = subprocess.run(base + ["mirror", "--test-pattern", "--audio=false", "--snapshot", "--snapshot-delay", "0", "--width", "20", "--height", "10"],
                                env={**os.environ, **env}, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)
        assert result.returncode == 0, result.returncode
        assert not marker.exists(), "Snapshot invocation wrote a first-use marker"
    print("First-use terminal checks passed")


if __name__ == "__main__":
    main()
