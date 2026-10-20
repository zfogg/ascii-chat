"""Verify every complete native terminal update retains its enabled FPS overlay."""
import argparse
from pathlib import Path
import re
import time

from terminal_ui import CaptureScreen, CaptureStream, Terminal


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--render-mode", choices=["foreground", "background", "half-block"], default="foreground")
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    term = Terminal([str(args.binary.resolve()), "--no-check-update", "--log-level", "warn",
                     "--log-file", str((args.artifacts / "application.log").resolve()), "mirror",
                     "--test-pattern", "--audio=false", "--fps", "60", "--fps-counter",
                     "--splash-screen=false", "--color-mode", "truecolor", "--render-mode", args.render_mode],
                    rows=40, cols=120)
    total = 0
    try:
        for attempt in range(2):
            term.expect(lambda text: re.search(r"FPS:\s*\d+", text.splitlines()[0]), "Counter enabled", timeout=45)
            term.raw.clear()
            deadline = time.monotonic() + 6
            while time.monotonic() < deadline:
                term.pump(0.05)
            raw = "".join(term.raw)
            (args.artifacts / f"updates-{attempt}.ansi").write_bytes(raw.encode())
            # Apply each complete transaction to a terminal emulator. Checking
            # only occasional screenshots misses counters erased between frames.
            screen = CaptureScreen(120, 40)
            stream = CaptureStream(screen)
            updates = re.findall(r"\x1b\[\?2026h(.*?)\x1b\[\?2026l", raw, re.S)
            assert len(updates) >= 10, ("Missing synchronized media updates", len(updates))
            for update in updates:
                stream.feed(update)
                assert re.search(r"FPS:\s*\d+", screen.display[0]), screen.display[0]
            total += len(updates)
            term.write("-")
            term.expect(lambda text: "FPS:" not in text.splitlines()[0], "Counter disabled")
            if attempt == 0:
                term.write("-")
        term.interrupt()
    finally:
        term.close()
    result = f"PASS {args.render_mode}: enabled FPS overlay retained in all {total} complete terminal updates; toggle-off clears it\n"
    (args.artifacts / "result.txt").write_text(result, encoding="utf-8")
    print(result, end="")


if __name__ == "__main__":
    main()
