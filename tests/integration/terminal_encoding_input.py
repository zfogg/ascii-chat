"""Regress legacy Windows code pages and Escape interleaved with terminal replies."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import re
import socket
import threading
import time

from terminal_ui import Terminal


def save(term, directory, name):
    raw = "".join(term.raw)
    (directory / (name + ".ansi")).write_bytes(raw.encode("utf-8"))
    (directory / (name + ".txt")).write_text("\n".join(term.screen.display), encoding="utf-8")
    return raw


def exited(term, timeout=15):
    deadline = time.monotonic() + timeout
    while term.process.isalive() and time.monotonic() < deadline:
        term.pump(.05)
    assert not term.process.isalive(), "Escape/cancellation did not exit the native session"
    term.pump(.2)


def assert_unicode(raw):
    for broken in ("Γÿ", "Γö", "Γò", "â”", "Ã", "�"):
        assert broken not in raw, ("Mojibake in native console output", broken)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    binary = str(args.binary.resolve())
    base = [binary, "--no-check-update", "--log-level", "debug"]

    # Hold media opening so cancellation happens during startup, before any art.
    requested = threading.Event()
    release = threading.Event()

    class SlowMedia(BaseHTTPRequestHandler):
        def do_GET(self):
            requested.set()
            release.wait(15)
            try:
                self.send_error(404)
            except (ConnectionError, OSError):
                pass

        def log_message(self, *unused):
            pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), SlowMedia)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    term = Terminal(base + ["--log-file", str((args.artifacts / "startup.log").resolve()),
                    "mirror", "--url", f"http://127.0.0.1:{server.server_port}/slow.png",
                    "--audio=false", "--fps", "60", "--utf8", "true", "--splash-screen=true"],
                    rows=30, cols=100, code_page=437)
    try:
        deadline = time.monotonic() + 20
        while not requested.is_set() and time.monotonic() < deadline:
            term.pump(.05)
        assert requested.is_set(), "Never reached the controlled startup media read"
        term.pump(.3)
        raw = save(term, args.artifacts, "startup")
        assert "★" in raw, "The startup Unicode log marker was not captured"
        assert_unicode(raw)
        term.write("\x03")
        exited(term)
        assert_unicode(save(term, args.artifacts, "startup-cancel"))
    finally:
        release.set()
        term.close()
        server.shutdown()
        server.server_close()

    # A connected peer that withholds the handshake keeps the real client
    # splash visible without depending on webcam initialization speed.
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen()
    held = threading.Event()
    release_peer = threading.Event()

    def hold_peer():
        try:
            peer, _ = listener.accept()
        except OSError:
            return
        with peer:
            held.set()
            release_peer.wait(15)

    threading.Thread(target=hold_peer, daemon=True).start()
    term = Terminal(base + ["--log-file", str((args.artifacts / "client-splash.log").resolve()),
                    "client", f"127.0.0.1:{listener.getsockname()[1]}", "--test-pattern", "--audio=false",
                    "--utf8", "true", "--splash-screen=true"], rows=30, cols=100, code_page=437)
    try:
        term.expect(lambda text: "Video chat" in text, "Client splash did not appear", timeout=20)
        raw = save(term, args.artifacts, "client-splash")
        assert re.search("[─━═╭┌╔]", raw), "No Unicode splash border captured"
        assert_unicode(raw)
        # Stop the deliberately unresponsive peer in fixture cleanup. Startup
        # Ctrl-C behavior is exercised independently by the mirror case above.
    finally:
        release_peer.set()
        listener.close()
        term.close()

    for name, escape in (("escape", "\x1b"),
                         ("escape-reply", "\x1b\x1b]11;rgb:1717/1717/1717\x1b\\")):
        term = Terminal(base + ["--log-file", str((args.artifacts / (name + ".log")).resolve()),
                        "mirror", "--test-pattern", "--audio=false", "--splash-screen=false"],
                        rows=24, cols=80, code_page=437, terminal_replies=True)
        try:
            term.expect(lambda text: re.search(r"([.:;clodxkXN])\1{8}", text), "Media was not rendered", timeout=30)
            term.write("-")
            term.expect(lambda text: re.search(r"FPS:\s*\d+", text), "FPS did not enable")
            raw = "".join(term.raw)
            assert "★" in raw
            assert_unicode(raw)
            term.write(escape)
            exited(term)
            raw = save(term, args.artifacts, name)
            assert "search logs" not in raw, "Escape/OSC reply opened the logs TUI"
            assert_unicode(raw)
        finally:
            term.close()
    # The designated shortcut still opens logs, and Escape closes that overlay.
    term = Terminal(base + ["--log-file", str((args.artifacts / "logs-shortcut.log").resolve()),
                    "mirror", "--test-pattern", "--audio=false", "--fps-counter",
                    "--splash-screen=false"], rows=24, cols=80, code_page=437,
                    terminal_replies=True)
    try:
        term.expect(lambda text: "FPS:" in text, "Media did not start", timeout=30)
        term.write("]")
        term.expect(lambda text: "search logs" in text, "Log shortcut did not open logs")
        assert_unicode(save(term, args.artifacts, "logs-shortcut"))
        term.write("\x1b")
        term.expect(lambda text: "FPS:" in text and "search logs" not in text,
                    "Escape did not return from logs to media")
        term.write("\x1b")
        exited(term)
    finally:
        term.close()
    print("PASS code page 437: Unicode startup/splash/early cancellation; FPS then Escape; interleaved OSC replies")


if __name__ == "__main__":
    main()
