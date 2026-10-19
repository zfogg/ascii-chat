"""Measure native live-frame latency and the displayed FPS through a real terminal.

Use --webcam for hardware capture; the default uses the generated test pattern.
Requires pyte and pexpect on POSIX. Windows uses a hidden console.
"""
import argparse
import os
from pathlib import Path
import re
import statistics
import tempfile
import time
from terminal_ui import Terminal
if os.name == "nt":
    from windows_console import WindowsConsole as Terminal


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--webcam", action="store_true")
    parser.add_argument("--duration", type=float, default=10)
    parser.add_argument("--rows", type=int, default=30)
    parser.add_argument("--cols", type=int, default=100)
    parser.add_argument("--artifacts", type=Path, required=True)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ascii-frame-latency-") as directory:
        log = Path(directory) / "application.log"
        term = Terminal([str(args.binary.resolve()), "--log-level", "debug", "--no-check-update",
                         "--log-file", str(log), "mirror", *([] if args.webcam else ["--test-pattern"]),
                         "--fps", "60", "--audio=false", "--fps-counter", "--color-mode", "truecolor",
                         "--splash-screen=false"], rows=args.rows, cols=args.cols)
        rates = []
        try:
            term.expect(lambda text: re.search(r"FPS:\s*[1-9]", text), "Live source must present acquired frames", timeout=40)
            deadline = time.monotonic() + args.duration
            while time.monotonic() < deadline:
                text = term.pump(0.1)
                match = re.search(r"FPS:\s*(\d+)", text)
                if match:
                    rates.append(int(match[1]))
            term.interrupt()
        finally:
            term.close()
            if log.exists():
                (args.artifacts / "application.log").write_bytes(log.read_bytes())
        content = log.read_text(encoding="utf-8")
        acquired = [float(v) for v in re.findall(r"LIVE_FRAME: capture_to_publish_ms=([0-9.]+)", content)]
        presented = [float(v) for v in re.findall(r"FRAME_PRESENT: id=\d+ submit_to_draw_ms=([0-9.]+)", content)]
        assert len(acquired) >= 3 and len(presented) >= 3, "Insufficient completed-frame timing samples"
        p95 = lambda values: sorted(values)[int((len(values) - 1) * 0.95)]
        assert p95(acquired) + p95(presented) < 500, ("Live frame backlog exceeds 500ms", acquired, presented)
        assert rates and statistics.median(rates) > 5, ("Live rendering is too slow", rates)
        result = (f"PASS {'webcam' if args.webcam else 'test-pattern'}: "
                  f"displayed_fps_median={statistics.median(rates):.1f} "
                  f"capture_to_publish_p95_ms={p95(acquired):.2f} "
                  f"publish_to_draw_p95_ms={p95(presented):.2f}\n")
        (args.artifacts / "result.txt").write_text(result, encoding="utf-8")
        print(result, end="")


if __name__ == "__main__":
    main()
