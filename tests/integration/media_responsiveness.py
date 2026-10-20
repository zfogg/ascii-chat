"""Measure open-media input latency after sustained native live rendering."""
import argparse
import os
from pathlib import Path
import time
from terminal_ui import Terminal
if os.name == "nt":
    from windows_console import WindowsConsole as Terminal


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--webcam", action="store_true")
    parser.add_argument("--without-audio", action="store_true")
    parser.add_argument("--idle", type=float, default=120)
    parser.add_argument("--max-latency", type=float, default=0.25)
    parser.add_argument("--artifacts", type=Path, required=True)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    term = Terminal([str(args.binary.resolve()), "--no-check-update", "--log-level", "warn",
                     "--log-file", str((args.artifacts / "application.log").resolve()), "mirror",
                     *([] if args.webcam else ["--test-pattern"]),
                     *(["--audio=false"] if args.without_audio else ["--audio-source", "media", "--volume", "0"]), "--fps", "60",
                     "--fps-counter", "--splash-screen=false"], rows=35, cols=110)
    try:
        term.expect(lambda text: "FPS:" in text and any(ch not in " \n" for ch in text[150:]),
                    "Initial live frame", timeout=90)
        deadline = time.monotonic() + args.idle
        while time.monotonic() < deadline:
            time.sleep(min(1, max(0, deadline - time.monotonic())))
        samples = []
        for attempt in range(3):
            start = time.monotonic()
            term.write("o")
            def visible_text():
                if os.name == "nt":
                    time.sleep(0.005)
                    return term.read_text()
                return term.pump(0.005)
            while "Open media:" not in visible_text():
                assert time.monotonic() - start <= 5, "Open prompt did not appear within five seconds"
            elapsed = time.monotonic() - start
            print(f"probe {attempt + 1}: {elapsed*1000:.1f}ms", flush=True)
            samples.append(elapsed)
            assert elapsed <= args.max_latency, ("Late open prompt", elapsed)
            term.write("\x1b")
            term.expect(lambda text: "Open media:" not in text, "Escape closes the media prompt")
            time.sleep(0.3)
        result = f"PASS {'webcam' if args.webcam else 'test-pattern'} after {args.idle:.0f}s: " + ", ".join(f"{v*1000:.1f}ms" for v in samples)
        (args.artifacts / "result.txt").write_text(result + "\n", encoding="utf-8")
        print(result)
        term.interrupt()
    finally:
        term.close()

if __name__ == "__main__":
    main()
