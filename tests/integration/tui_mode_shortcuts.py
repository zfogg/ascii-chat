"""Exercise mode help and diagnostic switches in real native PTYs.

Uses synthetic video and loopback services; no camera, microphone or internet.
Exports terminal cells and ANSI streams alongside screenshots when requested.
"""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import argparse
import os
from pathlib import Path
import tempfile
import time

from terminal_ui import Terminal, free_port
from sync_tui_modes import capture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--artifacts", required=True, type=Path)
    parser.add_argument("--profile", choices=("mirror", "server", "discovery-service"))
    args = parser.parse_args()
    root = args.artifacts.resolve()
    root.mkdir(parents=True, exist_ok=True)
    profiles = [args.profile] if args.profile else ["mirror", "server", "discovery-service"]
    with tempfile.TemporaryDirectory(prefix="ascii-tui-modes-") as directory:
        for suffix in ("ascii-chat", "AppData/Roaming/ascii-chat", ".config/ascii-chat"):
            (Path(directory) / suffix).mkdir(parents=True, exist_ok=True)
        env = {"HOME": directory, "USERPROFILE": directory, "APPDATA": directory, "XDG_CONFIG_HOME": directory,
               "ASCII_CHAT_QUESTION_PROMPT_RESPONSE": "y;y;y;y;y", "TERM": "xterm-256color"}
        for mode in profiles:
            for status in ([False] if mode == "mirror" else [False, True]):
                name = mode + ("-status" if status else "-stdout")
                log = root / f"{name}.log"
                command = [str(args.binary.resolve()), "--no-check-update", "--color=true", "--color-mode", "truecolor", "--utf8=true", "--log-file", str(log), mode]
                if mode == "mirror":
                    command += ["--test-pattern", "1", "--fps", "10", "--audio=false", "--splash-screen=false"]
                else:
                    command += ["127.0.0.1", "--port", str(free_port()),
                                "--websocket-port", str(free_port()), "--stats-interval", "1", f"--status-screen={'true' if status else 'false'}"]
                    if mode == "discovery-service":
                        command += ["--database", str(Path(directory) / (name + ".db"))]
                term = Terminal(command, rows=55, cols=180, environment=env)
                try:
                    if mode == "mirror":
                        term.expect(lambda text: len(text.strip()) > 300 and not any(level in text for level in ("[DEBUG", "[INFO", "[WARN")), "Missing synthetic video", timeout=30)
                    else:
                        status_title = "ascii-chat " + ("Discovery Service" if mode == "discovery-service" else "Server")
                        deadline = time.monotonic() + 30
                        while time.monotonic() < deadline:
                            text = term.pump(.1)
                            if "Server accepting connections" in text or (status and term.screen.display[1].strip().startswith(status_title)) or (log.exists() and "Server accepting connections" in log.read_text(encoding="utf-8", errors="replace")):
                                break
                            assert term.process.isalive(), term.pump()
                        else:
                            raise AssertionError("Server did not begin accepting connections")
                    if status:
                        term.expect(lambda text: term.screen.display[1].strip().startswith(status_title), "Status header")
                    capture(term, root, name + "-base")

                    def show(key, label, artifact):
                        term.write(key)
                        term.expect(lambda text: label in text, f"{name}: {artifact}", timeout=15)
                        term.expect(lambda text: label in text, f"{name}: complete {artifact}", timeout=15)
                        capture(term, root, name + "-" + artifact)

                    show("?", "Keyboard Shortcuts", "help")
                    text = term.pump()
                    if mode != "mirror":
                        assert "Flip webcam" not in text and "Audio Waveform" not in text, text
                        assert ("Discovery Service" if mode == "discovery-service" else "Server") in text, text
                    # Key tokens share the existing help palette; separators do not.
                    key_rows = [i for i, row in enumerate(term.screen.display) if "F12" in row or "0 " in row]
                    for row in key_rows:
                        line = term.screen.display[row]
                        for token in ("F12",):
                            if token in line:
                                col = line.index(token)
                                assert term.screen.buffer[row][col].fg != "default", line
                    show("=", "LIVE STATS", "stats-from-help")
                    show("0", "Sync primitives", "sync-from-stats")
                    show("?", "Sync Viewer Keyboard Shortcuts", "sync-help")
                    show("=", "LIVE STATS", "stats-from-sync-help")
                    show("?", "Statistics Keyboard Shortcuts", "stats-help")
                    show("?", "LIVE STATS", "return-to-stats")
                    if mode == "mirror":
                        term.resize(12, 180)
                        term.expect(lambda text: "LIVE STATS" in text and "Terminal too small" not in text,
                                    "Scrollable stats table did not fit")
                        term.write("\x1b[F")
                        term.expect(lambda text: "Audio:" in text and "Uptime" not in text,
                                    "Stats table did not scroll to its last row")
                        capture(term, root, name + "-stats-scrolled")
                        show("\t", "CONNECTIONS & MODE DETAILS", "stats-data-page")
                        term.resize(55, 180)
                        term.write("\t")
                        term.expect(lambda text: "OVERVIEW" in text, "Stats overview did not restore")
                    term.write("=")
                    term.expect(lambda text: "LIVE STATS" not in text, "Stats toggle closes")
                    show("]", "search logs", "logs")
                    show("?", "Log Viewer Keyboard Controls", "logs-help")
                    show("=", "LIVE STATS", "stats-from-log-help")
                    term.write("=")
                    term.expect(lambda text: "LIVE STATS" not in text, "Stats closes to base")
                    show("`", "errno stacks", "errors")
                    show("?", "Error Stacks Keyboard Shortcuts", "errors-help")
                    show("=", "LIVE STATS", "stats-from-errors-help")
                    term.write("=")
                    term.expect(lambda text: "LIVE STATS" not in text, "Stats closes")
                    if mode != "mirror" and not status:
                        term.expect(lambda text: "stats mode=" + mode in text,
                                    "Closing TUIs did not restore stdout reporting", timeout=8)
                    if mode == "mirror":
                        show("o", "Open media:", "media-picker")
                        show("?", "Media Input Keyboard Shortcuts", "media-picker-help")
                        term.write("?")
                        term.expect(lambda text: "Open media:" in text and "Media Input Keyboard Shortcuts" not in text,
                                    "Media input help did not return to editing")
                        term.write("photo0?clip=1.mp4")
                        term.expect(lambda text: "> photo0?clip=1.mp4" in text and "LIVE STATS" not in text,
                                    "Media query equals sign was consumed as a shortcut")
                        capture(term, root, name + "-media-picker-query")
                        term.write("\x1b")
                        term.expect(lambda text: "Open media:" not in text, "Media picker closes")
                    term.interrupt()
                    print(f"PASS {name}: mode help, logs, sync, statistics, error stacks and cross-TUI shortcuts", flush=True)
                except BaseException:
                    capture(term, root, name + "-failure")
                    raise
                finally:
                    term.close()


if __name__ == "__main__":
    main()
