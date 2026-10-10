"""Run native synchronization regressions, including real PTY presentation.

Build: cmake --build build --target test-sync-regressions
Run: python tests/integration/sync_regressions.py build/bin/test-sync-regressions.exe
Requires the same pyte/pywinpty (Windows) or pexpect (POSIX) as terminal_ui.py.
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from terminal_ui import Terminal


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="ascii-sync-") as directory:
        log = str(Path(directory) / "sync.log")
        result = subprocess.run([binary, "sync", log], capture_output=True, timeout=20)
        assert result.returncode == 0, result.stderr.decode(errors="replace")
        print("PASS registry capacity, nonblocking contention, mutex/condition tracking and atomic changes")
        terminal = Terminal([binary, "ui", log], rows=30, cols=100)
        try:
            terminal.expect(lambda text: "PASS UI OWNERSHIP" in text,
                            "Controller callback ownership regression failed", timeout=20)
            print("PASS callback reentry, concurrent replacement/removal, self-removal, finish")
        finally:
            terminal.close()
        for mode in ("deadlock", "stale"):
            terminal = Terminal([binary, mode, log], rows=30, cols=120)
            try:
                terminal.expect(lambda text: "SYNC FIXTURE READY" in text, "Fixture did not start")
                terminal.pump(2)
                terminal.write("0")
                text = terminal.expect(lambda text: "Sync primitives" in text and "sync_fixture_value" in text,
                                       "Sync screen did not open with the main thread blocked")
                assert re.search(r"buffer_pool\.global_allocations_bypassing_pool\.\d+\s+0x", text), text
                state_column = text.splitlines()[2].index("State/value")
                if mode == "deadlock":
                    text = terminal.expect(lambda text: "2 mutexes in wait cycles" in text, "Missing circular wait")
                    rows = text.splitlines()
                    assert "cycle_a" in rows[3] and "DEADLOCK" in rows[3], text
                    assert "cycle_b" in rows[4] and "DEADLOCK" in rows[4], text
                    terminal.write("\x1b[C")
                    terminal.expect(lambda text: "page 2/" in text, "Right advances a page")
                    terminal.write("\x1b[D")
                    terminal.expect(lambda text: "page 1/" in text, "Left returns a page")
                    terminal.write("\x1b[B")
                    terminal.expect(lambda text: text.splitlines()[4].startswith(">mutex"), "Down selects next row")
                    terminal.write("\x1b[A")
                    terminal.expect(lambda text: text.splitlines()[3].startswith(">mutex"), "Up selects previous row")
                    terminal.write("\x1b[F")
                    last_page = terminal.expect(lambda text: "named_registry_lock" in text, "Cannot reach final page")
                    assert last_page.splitlines()[2].index("State/value") == state_column, last_page
                    terminal.write("\x1b[H")
                    terminal.expect(lambda text: "cycle_a" in text and "DEADLOCK" in text, "Missing named cycle detail")
                else:
                    terminal.expect(lambda text: "STALE: collection unavailable" in text, "Missing stale sample status")
                    before = terminal.pump()
                    assert terminal.pump(1.2) != before, "Rendering stopped with registry writer held"
                terminal.resize(12, 60)
                warning = terminal.expect(lambda text: "Terminal too small" in text and "need " in text,
                                          "Missing measured size warning")
                min_cols, min_rows = map(int, re.search(r"need (\d+)x(\d+)", warning).groups())
                for rows, cols in ((12, min_cols - 1), (min_rows - 1, 120)):
                    terminal.resize(rows, cols)
                    terminal.expect(lambda text: "Terminal too small" in text, "Missing sync size warning")
                terminal.resize(min_rows, min_cols)
                terminal.expect(lambda text: "Sync primitives" in text and "Terminal too small" not in text,
                                "Sync screen did not recover at measured minimum size")
                terminal.resize(30, 120)
                terminal.expect(lambda text: "Sync primitives" in text and "page 1/" in text,
                                "Sync screen did not recover after resize")
                print(f"PASS independent sync UI: {mode}, keyboard, pagination, resize")
            finally:
                terminal.close()


if __name__ == "__main__":
    main()
