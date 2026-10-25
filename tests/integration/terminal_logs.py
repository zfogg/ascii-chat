"""Exercise the shared log overlay in real mirror and TCP client terminals."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import argparse
import os
import socket
import shutil
import subprocess
import tempfile
from pathlib import Path
from terminal_ui import Terminal, free_port, load_terminal_font


def capture(term, path):
    """Render the observed ConPTY cell grid, preserving terminal colors."""
    from PIL import Image, ImageDraw, ImageFont
    font = load_terminal_font(16)
    image = Image.new("RGB", (term.screen.columns * 10, term.screen.lines * 20), "black")
    draw = ImageDraw.Draw(image)
    colors = {"default": "#dddddd", "cyan": "#00aaaa", "brightcyan": "#55ffff"}
    for row in range(term.screen.lines):
        for col in range(term.screen.columns):
            cell = term.screen.buffer[row][col]
            color = colors.get(cell.fg, cell.fg)
            if len(color) == 6:
                color = "#" + color
            try:
                draw.text((col * 10, row * 20), cell.data, font=font, fill=color)
            except ValueError:
                draw.text((col * 10, row * 20), cell.data, font=font, fill="white")
    image.save(path)
    path.with_suffix(".txt").write_text("\n".join(term.screen.display), encoding="utf-8")


def exercise(term, name, artifacts):
    term.expect(lambda s: len(s.strip()) > 100, "No media output", timeout=30)
    initial = term.pump(1)
    if "IDENTITY" in initial:
        term.write("\r")
        term.pump(.5)
    term.write("\x1b[B")
    term.pump(.3)
    term.write("]")
    term.expect(lambda s: "/: search logs" in s,
                "C log emitted while hidden did not reach overlay")
    assert term.screen.cursor.hidden, "Idle log viewer left the cursor visible"
    logs_before_search = term.pump(.3).splitlines()
    term.write("/")
    search_open = term.expect(lambda s: s.splitlines()[-1].strip() == "/", "Search prompt did not open")
    assert not term.screen.cursor.hidden, "Search input did not show its editing cursor"
    before = [line[:-1].rstrip() for line in logs_before_search[4:] if line[:-1].strip()]
    after = [line[:-1].rstrip() for line in search_open.splitlines()[4:-1] if line[:-1].strip()]
    assert after == before[-len(after):], "Search prompt changed the retained log rows"
    term.write("Volume DOWN")
    typed = term.pump(.15)
    assert typed.splitlines()[-1].strip() == "/Volume DOWN", "Search input was delayed"
    term.expect(lambda s: "KEYBOARD INPUT" not in s, "Debounced search did not filter")
    term.write("\r")
    term.expect(lambda s: "Volume DOWN" in s, "Search did not remain active after accepting input")
    capture(term, artifacts / f"session-search-{name}.png")
    term.write("\x1b")
    term.expect(lambda s: "/: search logs" in s, "Escape closed overlay instead of search")
    assert term.screen.cursor.hidden, "Leaving search did not hide its editing cursor"
    capture(term, artifacts / f"session-logs-{name}.png")
    term.write("k")
    term.expect(lambda s: "1 rows from tail" in s, "k did not scroll one log row older")
    term.write("j")
    term.expect(lambda s: "0 rows from tail" in s, "j did not scroll one log row newer")
    term.write("\x15")
    term.expect(lambda s: "rows from tail" in s and "0 rows from tail" not in s,
                "Ctrl-U did not scroll a half page older")
    term.write("\x04")
    term.expect(lambda s: "0 rows from tail" in s, "Ctrl-D did not scroll a half page newer")
    term.write("?")
    term.expect(lambda s: "Log Viewer Keyboard Controls" in s and "/: search logs" in s,
                "Log help did not overlay the viewer")
    assert term.screen.cursor.hidden, "Log viewer help left the cursor visible"
    help_rows = term.screen.display
    title_row = next(i for i, row in enumerate(help_rows) if "Log Viewer Keyboard Controls" in row)
    first_binding = next(i for i, row in enumerate(help_rows) if "newer line" in row)
    assert first_binding == title_row + 2, "Log help needs a blank row below its header"
    capture(term, artifacts / f"session-log-help-{name}.png")
    term.write("?")
    term.expect(lambda s: "/: search logs" in s and "Log Viewer Keyboard Controls" not in s,
                "Log help did not close back to the viewer")
    term.resize(10, 140)
    term.expect(lambda s: "/: search logs" in s, "Log viewer did not survive compact resize")
    term.write("?")
    term.expect(lambda s: "Help needs at least 12 terminal rows" in s, "Compact help warning missing")
    term.resize(12, 140)
    term.expect(lambda s: s.splitlines()[-1].startswith("Help needs at least 12 terminal rows"),
                "Log viewer did not observe the restored terminal height")
    term.write("?")
    term.expect(lambda s: "Log Viewer Keyboard Controls" in s, "Log help did not reopen after resize")
    assert term.screen.cursor.hidden, "Resized log viewer help left the cursor visible"
    term.write("?")
    term.expect(lambda s: "Log Viewer Keyboard Controls" not in s and "/: search logs" in s,
                "Resized log help did not close before shrinking the terminal")
    term.resize(4, 20)
    term.expect(lambda s: "Terminal too small" in s, "Log size guard missing")
    term.resize(30, 140)
    term.expect(lambda s: "/: search logs" in s, "Log resize did not recover")
    term.write("\x1b")
    term.expect(lambda s: "/: search logs" not in s and "Keyboard Shortcuts" not in s and len(s.strip()) > 100,
                "Escape did not restore media")
    term.write("]")
    term.expect(lambda s: "/: search logs" in s, "Logs did not reopen")
    term.write("]")
    term.expect(lambda s: "/: search logs" not in s and len(s.strip()) > 100, "Toggle did not restore media")
    print(f"PASS {name}: C logs, help exclusivity, resize, Escape, toggle, screenshot", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--search-regression-only", action="store_true",
                        help="Run only the focused log-search terminal smoke test")
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    binary = str(args.binary.resolve())
    with tempfile.TemporaryDirectory(prefix="ascii-logs-") as directory:
        root = Path(directory)
        media = root / "test.mp4"
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                        "testsrc2=size=160x120:rate=10", "-t", "3", "-c:v", "libx264", str(media)], check=True)
        common = ["--file", str(media), "--loop", "--splash-screen=false", "--audio=false", "--color-mode",
                  "truecolor", "--utf8=true"]
        base = [binary, "--no-check-update", "--log-level", "info"]
        if args.search_regression_only:
            mirror = Terminal(base + ["--log-file", str(root / "mirror.log"), "mirror"] + common, rows=40, cols=140)
            try:
                exercise(mirror, "mirror", args.artifacts)
            finally:
                mirror.close()
                shutil.copyfile(root / "mirror.log", args.artifacts / "mirror-debug.log")
            return
        # Keep the transport open without a handshake so startup remains observable.
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen()
            startup = Terminal(base + ["--log-file", str(root / "startup.log"), "client", "127.0.0.1",
                                       "--port", str(listener.getsockname()[1]), "--splash-screen=true"],
                               rows=30, cols=140)
            try:
                startup.expect(lambda text: "Video chat in your terminal" in text, "Startup splash missing")
                text = startup.pump(.3)
                assert "/: search logs" not in text and "[INFO]" not in text and "[DEBUG]" not in text
                capture(startup, args.artifacts / "session-splash-startup-client.png")
                startup.write("]")
                startup.expect(lambda text: "/: search logs" in text, "Startup log toggle failed")
                startup.write("]")
                startup.expect(lambda text: "Video chat in your terminal" in text and "/: search logs" not in text,
                               "Closing logs did not restore startup splash")
                print("PASS client startup: rainbow splash, no automatic logs, explicit toggle and restoration", flush=True)
            finally:
                startup.close()
        mirror = Terminal(base + ["--log-file", str(root / "mirror.log"), "mirror"] + common, rows=40, cols=140)
        try:
            exercise(mirror, "mirror", args.artifacts)
        finally:
            mirror.close()
            shutil.copyfile(root / "mirror.log", args.artifacts / "mirror-debug.log")
        port, ws_port = free_port(), free_port()
        server = Terminal(base + ["--log-file", str(root / "server.log"), "server", "127.0.0.1", "--port", str(port),
                                  "--websocket-port", str(ws_port), "--status-screen=false", "--no-encrypt", "--no-auth"], rows=30, cols=140)
        try:
            import time
            deadline = time.monotonic() + 20
            while True:
                server.pump(.1)
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=.5):
                        break
                except OSError:
                    if time.monotonic() > deadline:
                        raise
            client = Terminal(base + ["--log-file", str(root / "client.log"), "client", "127.0.0.1", "--port", str(port), "--no-encrypt", "--no-auth"] + common,
                              rows=40, cols=140)
            try:
                exercise(client, "client", args.artifacts)
            finally:
                client.close()
                shutil.copyfile(root / "client.log", args.artifacts / "client-debug.log")
        finally:
            (args.artifacts / "server.txt").write_text(server.pump(.1), encoding="utf-8")
            server.close()


if __name__ == "__main__":
    main()



