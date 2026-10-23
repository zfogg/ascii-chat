"""Bound native output to one frame even when a terminal frontend is slow."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import argparse
from pathlib import Path
import re
import time
from terminal_ui import Terminal


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    term = Terminal([str(args.binary.resolve()), "--no-check-update", "--log-level", "debug",
                     "--log-file", str((args.artifacts / "application.log").resolve()), "mirror",
                     "--test-pattern", "--audio=false", "--fps", "60", "--fps-counter",
                     "--splash-screen=false"], rows=24, cols=80)
    query = "\x1b]11;?\x1b\\"
    pending = False
    reply_at = 0
    queries = replies = index = frames = 0
    tail = ""
    prompt_started = prompt_ms = None
    rates = []
    started = None
    deadline = time.monotonic() + 30
    try:
        while time.monotonic() < deadline:
            text = term.pump(0.01)
            chunks = term.raw[index:]
            index = len(term.raw)
            chunk = tail + "".join(chunks)
            for event in re.finditer(r"\x1b\[\?2026h|" + re.escape(query), chunk):
                if event.group() == query:
                    if not frames:
                        term.write("\x1b]11;rgb:0000/0000/0000\x1b\\")
                        continue
                    assert not pending, "More than one frontend acknowledgement is outstanding"
                    queries += 1
                    pending = True
                    reply_at = time.monotonic() + (0.8 if queries == 4 else 0.12)
                    if started is None:
                        started = time.monotonic()
                    if queries == 4:
                        prompt_started = time.monotonic()
                        term.write("o")
                else:
                    assert not pending, "A second media frame was sent before the frontend consumed the first"
                    frames += 1
            tail = chunk[-(len(query) - 1):]
            if prompt_started is not None and prompt_ms is None and "Open media:" in text:
                prompt_ms = (time.monotonic() - prompt_started) * 1000
                assert prompt_ms < 250, ("Modal input blocked behind terminal acknowledgement", prompt_ms)
                term.write("\x1b")
            match = re.search(r"FPS:\s*(\d+)", text.splitlines()[0])
            if match:
                rates.append(int(match[1]))
            if pending and time.monotonic() >= reply_at:
                term.write("\x1b]11;rgb:0000/0000/0000\x1b\\")
                pending = False
                replies += 1
            if started and time.monotonic() - started >= 5 and queries >= 12:
                break
        assert replies >= 12 and prompt_ms is not None, (queries, replies, prompt_ms)
        assert rates and 0 < max(rates) <= 10, ("FPS ignored frontend backpressure", rates)
        term.interrupt()
    finally:
        term.close()
    result = (f"PASS one outstanding media frame; {replies} delayed frontend replies; "
              f"800ms frontend stall preserved responsive input ({prompt_ms:.1f}ms); FPS <= 10\n")
    (args.artifacts / "result.txt").write_text(result, encoding="utf-8")
    print(result, end="")


if __name__ == "__main__":
    main()
