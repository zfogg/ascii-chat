"""Exercise the shared log overlay in real mirror and TCP client terminals."""
import argparse
import os
import socket
import shutil
import subprocess
import tempfile
from pathlib import Path
from terminal_ui import Terminal, free_port


def capture(term, path):
    """Render the observed ConPTY cell grid, preserving terminal colors."""
    from PIL import Image, ImageDraw, ImageFont
    font = ImageFont.truetype("C:/Windows/Fonts/consola.ttf" if os.name == "nt" else "DejaVuSansMono.ttf", 16)
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
    term.pump(.3)
    pattern = ""
    for key in "/Volume DOWN":
        pattern += key
        term.write(key)
        term.expect(lambda s: pattern in s.splitlines()[-1], "Existing server search did not accept key")
    term.write("\r")
    term.expect(lambda s: "Volume DOWN" in s and "KEYBOARD INPUT" not in s, "Search did not filter")
    capture(term, artifacts / f"session-search-{name}.png")
    term.write("\x1b")
    term.expect(lambda s: "/: search logs" in s, "Escape closed overlay instead of search")
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
    capture(term, artifacts / f"session-log-help-{name}.png")
    term.write("?")
    term.expect(lambda s: "/: search logs" in s and "Log Viewer Keyboard Controls" not in s,
                "Log help did not close back to the viewer")
    term.resize(10, 140)
    term.expect(lambda s: "/: search logs" in s, "Log viewer did not survive compact resize")
    term.write("?")
    term.expect(lambda s: "Help needs at least 11 terminal rows" in s, "Compact help warning missing")
    term.resize(11, 140)
    term.write("?")
    term.expect(lambda s: "Log Viewer Keyboard Controls" in s, "Log help did not reopen after resize")
    term.write("?")
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



