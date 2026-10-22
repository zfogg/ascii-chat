"""Check mirror audio starts on visualization/media demand and stops when idle.

Needs ffmpeg, terminal dependencies, and a microphone/output device for the
positive transitions. Missing devices return 77; video-only startup is checked first.
"""
import argparse
import functools
import http.server
import json
from pathlib import Path
import subprocess
import tempfile
import threading
import time
from terminal_ui import Terminal


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mirror-lazy-audio-") as directory:
        root = Path(directory)
        media = root / "tone.mp4"
        subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i", "testsrc2=size=160x120:rate=33",
                        "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000", "-t", "5",
                        "-c:v", "libx264", "-c:a", "aac", "-movflags", "+faststart", str(media)], check=True, timeout=30)
        handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(root))
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            for kind, path in (("file", str(media)), ("url", f"http://127.0.0.1:{server.server_port}/tone.mp4")):
                log = args.artifacts / f"{kind}.log"
                term = Terminal([str(args.binary.resolve()), "--no-check-update", "--log-file", str(log.resolve()),
                                 "--log-level", "info", "mirror", "--test-pattern", "--fps", "33",
                                 "--splash-screen=false", "--color-mode", "truecolor"], rows=35, cols=140)
                def logs():
                    return log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
                def await_log(predicate, message):
                    deadline = time.monotonic() + 15
                    while time.monotonic() < deadline:
                        term.pump(.05)
                        text = logs()
                        if ("Cannot start mirror audio" in text or "Cannot initialize mirror audio" in text or
                                "CAMERA UNAVAILABLE" in text):
                            raise SystemExit(77)
                        if predicate(text):
                            return
                    raise AssertionError(message + "\n" + logs()[-4000:])
                try:
                    term.expect(lambda text: len(text.strip()) > 400, "Mirror video missing", timeout=30)
                    term.pump(.5)
                    term.write("0")
                    term.expect(lambda text: "Sync primitives" in text, "Initial sync screen")
                    term.resize(144, 140)
                    inventory = term.pump(1)
                    assert "audio_processing" not in inventory, inventory
                    assert not any(row.lstrip("> ").startswith("mutex") and "display." in row
                                   for row in inventory.splitlines()), inventory
                    memory_row = next(row for row in inventory.splitlines() if "debug_memory." in row)
                    assert max(map(float, memory_row.split()[-4:-2])) < 1, memory_row
                    assert "Mirror audio started on demand" not in logs()
                    term.write("0")
                    term.resize(35, 140)
                    term.expect(lambda text: "Sync primitives" not in text, "Sync closes")
                    if kind == "file":
                        for count, key in enumerate(("2", "3"), start=1):
                            term.write(key)
                            await_log(lambda text: text.count("Mirror audio started on demand") == count,
                                      "Visualization did not start audio")
                            if count == 1:
                                term.write("0")
                                term.expect(lambda text: "Sync primitives" in text, "Active audio sync screen")
                                term.resize(144, 140)
                                active = term.pump(2)
                                (args.artifacts / "active-audio.txt").write_text(active, encoding="utf-8")
                                rates = {}
                                for name, limit in (("audio_processing_wait", 70), ("audio_processing_wakeup", 70),
                                                    ("capture_load.", 1),
                                                    ("stats_duration.", 8), ("ui_controller.", 50)):
                                    row = next(row for row in active.splitlines() if name in row)
                                    rate = max(map(float, row.split()[-4:-2]))
                                    assert rate < limit, (name, rate, row)
                                    rates[name] = rate
                                assert rates["audio_processing_wakeup"] > 0, "Audio worker did not run"
                                (args.artifacts / "active-rates.json").write_text(json.dumps(rates, indent=2))
                                term.write("0")
                                term.resize(35, 140)
                                term.expect(lambda text: "Sync primitives" not in text, "Active audio sync closes")
                            term.pump(.25)
                            term.write(key)
                            await_log(lambda text: text.count("Mirror audio idle") == count,
                                      "Disabling visualization did not stop audio")
                    prior = len(logs())
                    term.write("o")
                    term.expect(lambda text: "Open media:" in text, "Media prompt")
                    term.write(path)
                    term.expect(lambda text: media.name in text, "Media input was not entered")
                    term.write("\r")
                    await_log(lambda text: "Opened media:" in text and
                              ("Mirror audio started on demand" in text[prior:] or "Output stream opened" in text[prior:]),
                              f"Opening {kind} did not start audio")
                    term.pump(1)
                    assert term.process.isalive(), logs()
                    assert "Failed to start audio" not in logs(), logs()
                    term.interrupt()
                    print(f"PASS lazy mirror audio: {kind}, idle startup, on-demand transitions", flush=True)
                finally:
                    term.close()
            for flag in ("--fft", "--waveform"):
                log = args.artifacts / f"startup-{flag[2:]}.log"
                term = Terminal([str(args.binary.resolve()), "--no-check-update", "--log-file", str(log.resolve()),
                                 "--log-level", "info", "mirror", flag, "--fps", "33",
                                 "--splash-screen=false"], rows=45, cols=140)
                try:
                    await_log(lambda text: "Mirror audio started on demand" in text,
                              f"{flag} did not start audio at startup")
                    await_log(lambda text: "running main loop" in text, "Visualization pipeline did not start")
                    term.pump(.25)
                    term.write("?")
                    term.expect(lambda text: "Keyboard Shortcuts" in text, "Visualization input did not become ready")
                    term.write("?")
                    term.expect(lambda text: "Keyboard Shortcuts" not in text, "Visualization help did not close")
                    term.interrupt()
                    print(f"PASS mirror startup audio: {flag}", flush=True)
                finally:
                    term.close()
        finally:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    main()
