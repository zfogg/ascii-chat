"""Run native synchronization regressions, including real PTY presentation.

Build: cmake --build build --target test-sync-regressions
Run: python tests/integration/sync_regressions.py build/bin/test-sync-regressions.exe
Requires the same pyte/pywinpty (Windows) or pexpect (POSIX) as terminal_ui.py.
"""
import importlib.util
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

prerequisites = ["pyte"] + ([] if os.name == "nt" else ["pexpect"])
missing = [name for name in prerequisites if importlib.util.find_spec(name) is None]
if missing:
    print("SKIP: sync terminal tests need " + ", ".join(missing) +
          "; install tests/requirements-terminal.txt")
    raise SystemExit(77)

from terminal_ui import Terminal


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="ascii-sync-") as directory:
        log = str(Path(directory) / "sync.log")
        result = subprocess.run([binary, "sync", log], capture_output=True, timeout=20)
        assert result.returncode == 0, result.stderr.decode(errors="replace")
        print("PASS compact number formatting, registry capacity, nonblocking contention, mutex/condition tracking and atomic changes")
        terminal = Terminal([binary, "ui", log], rows=30, cols=100)
        try:
            terminal.expect(lambda text: "PASS UI OWNERSHIP" in text,
                            "Controller callback ownership regression failed", timeout=20)
            print("PASS callback reentry, concurrent replacement/removal, self-removal, finish")
        finally:
            terminal.close()
        for mode in ("deadlock", "stale"):
            stop_path = Path(log + ".stop")
            stop_path.unlink(missing_ok=True)
            terminal = Terminal([binary, mode, log], rows=30, cols=120)
            try:
                terminal.expect(lambda text: "SYNC FIXTURE READY" in text, "Fixture did not start")
                terminal.pump(2)
                terminal.write("0")
                text = terminal.expect(lambda text: "Sync primitives" in text and "objects" in text,
                                       "Sync screen did not open with the main thread blocked")
                state_column = text.splitlines()[2].index("State/value")
                if mode == "deadlock":
                    text = terminal.expect(lambda text: "2 mutexes in wait cycles" in text, "Missing circular wait")
                    text = terminal.expect(lambda t: re.search(
                        r"zzz_rate_hot[^\n]*\s[1-9][0-9]*\.[0-9]\s+[0-9.]+\s+-\s+-\s*$", t, re.M) is not None,
                        "Missing warmed activity rates")
                    rows = text.splitlines()
                    state_column = rows[2].index("State/value")
                    assert "cycle_a" in rows[4] and "DEADLOCK" in rows[4], text
                    assert "cycle_b" in rows[5] and "DEADLOCK" in rows[5], text
                    body = rows[6:-4]
                    rates = [sum(float(value) for value in row.split()[-4:-1] if value != "-")
                             for row in body if row.strip()]
                    assert rates == sorted(rates, reverse=True), text
                    assert next(i for i, r in enumerate(body) if "zzz_rate_hot" in r) < next(
                        i for i, r in enumerate(body) if "aaa_rate_warm" in r), text
                    assert "Change/s" in rows[2] and rows[2].rstrip().endswith("Total Wait"), text
                    assert all(r.split()[-1] == "-" for r in rows[4:-4]
                               if re.match(r"[ >]*(?:mutex|rwlock|atomic|atomic_p)\s", r)), text
                    for row_index, row in enumerate(rows[4:-4], start=4):
                        fields = row.split()
                        if not fields:
                            continue
                        for token in fields[-5:]:
                            suffix = re.search(r"(?<=[0-9])(?:[kmbt]|ns|µs|ms|s)$", token)
                            if suffix:
                                start = row.rindex(token) + suffix.start()
                                for col in range(start, start + len(suffix.group())):
                                    assert terminal.screen.buffer[row_index][col].fg == "default", row
                    positions = {name: next(i for i, row in enumerate(body) if name in row)
                                 for name in ("sort_balanced", "sort_actions", "sort_wait")}
                    assert positions["sort_balanced"] < positions["sort_actions"], text
                    assert positions["sort_balanced"] < positions["sort_wait"], text
                    assert all(row.split()[-2] == "-" for row in rows[4:-4]
                               if re.match(r"[ >]*(?:mutex|rwlock|cond)\s", row) and "DEADLOCK" not in row), text
                    assert "Total Actions" in rows[2], text
                    atomic_row = next(row for row in rows[4:-4] if "aaa_rate_counter." in row)
                    assert float(atomic_row.split()[-2]) > 0, text
                    print("PASS deadlocks first, descending Change/s+L/s+U/s, atomic totals and change rates")
                    if len(sys.argv) > 2:
                        from sync_tui_modes import capture
                        capture(terminal, Path(sys.argv[2]), "deadlocks-first")
                    terminal.write("\x1b[C")
                    terminal.expect(lambda text: "page 2/" in text, "Right advances a page")
                    terminal.write("\x1b[D")
                    terminal.expect(lambda text: "page 1/" in text, "Left returns a page")
                    terminal.write("\x1b[B")
                    terminal.expect(lambda text: text.splitlines()[5].startswith(">mutex"), "Down selects next row")
                    terminal.write("\x1b[A")
                    terminal.expect(lambda text: text.splitlines()[4].startswith(">mutex"), "Up selects previous row")
                    # Start away from row zero: Home/End must move both the
                    # selection and page, including repeated presses.
                    terminal.write("\x1b[B" * 6)
                    terminal.expect(lambda text: "item 7/" in text, "Select seventh item")
                    terminal.write("\x1b[D")
                    terminal.expect(lambda text: "item 7/" in text, "Left on first page preserves selection")
                    terminal.write("\x1b[F")
                    last_page = terminal.expect(lambda text: re.search(r"item (\d+)/\1(?:\s|$)", text) is not None, "Cannot reach final page")
                    total = int(re.search(r"(\d+) objects", last_page).group(1))
                    terminal.expect(lambda text: f"item {total}/{total}" in text, "End selects last item")
                    terminal.write("\x1b[F")
                    terminal.expect(lambda text: f"item {total}/{total}" in text, "End remains at last item")
                    assert last_page.splitlines()[2].index("State/value") == state_column, last_page
                    terminal.write("\x1b[H")
                    terminal.expect(lambda text: "item 1/" in text and text.splitlines()[4].startswith(">mutex"),
                                    "Home selects first item")
                    terminal.write("\x1b[B" * 6 + "\x1b[H")
                    terminal.expect(lambda text: "item 1/" in text, "Home resets selection on first page")
                    terminal.write("\x1b[H")
                    terminal.expect(lambda text: "item 1/" in text, "Home remains at first item")
                    terminal.write("\x1b[B" * 22)
                    terminal.expect(lambda text: "page 2/" in text and "item 23/" in text,
                                    "Down crosses the page boundary")
                    terminal.write("\x1b[A")
                    terminal.expect(lambda text: "page 1/" in text and "item 22/" in text,
                                    "Up crosses back to previous page")
                    terminal.write("\x1b[H")
                    terminal.expect(lambda text: "item 1/" in text, "Home restores first item")
                    # The snapshot grows beyond both the old registry limits
                    # and one terminal page; every registered fixture is reachable.
                    fixtures = set()
                    pointer_total_seen = False
                    atomic_total_seen = False
                    reserved_buffer_seen = False
                    pages = int(re.search(r"page 1/(\d+)", terminal.pump()).group(1))
                    for page in range(1, pages + 1):
                        text = terminal.expect(lambda t: f"page {page}/{pages}" in t, "Missing inventory page")
                        # ConPTY can expose a partially repainted row even on a
                        # steady page. Collect successive views of this page;
                        # every one of the 600 distinct names must still appear.
                        for _ in range(4):
                            text = terminal.pump(0.2)
                            pointer_row = next((r for r in text.splitlines()[4:-4] if "aaa_pointer_uses." in r), None)
                            if pointer_row:
                                assert pointer_row.split()[-5] == "7", text
                                pointer_total_seen = True
                            total_row = next((r for r in text.splitlines()[4:-4] if "aaa_total_uses." in r), None)
                            if total_row:
                                assert total_row.split()[-5] == "9" and float(total_row.split()[-2]) == 0, text
                                atomic_total_seen = True
                            reserved_buffer_seen |= bool(re.search(r"buffer\.process_reserved_[^\s]*\s+0x", text))
                            fixtures.update(re.findall(r"sync_fixture_value\.\d+", "\n".join(text.splitlines()[4:-4])))
                        if page < pages:
                            terminal.write("\x1b[C")
                    assert atomic_total_seen, "Missing cumulative atomic total"
                    assert reserved_buffer_seen, "Missing reserved buffer value"
                    assert pointer_total_seen, "Missing cumulative pointer operations"
                    assert len(fixtures) == 600, (len(fixtures), sorted(fixtures))
                    terminal.write("\x1b[H")
                    terminal.expect(lambda text: "item 1/" in text, "Home restores inventory start")
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
                try:
                    stop_path.touch()
                    deadline = time.monotonic() + 10
                    while terminal.process.isalive() and time.monotonic() < deadline:
                        terminal.pump(0.1)
                    assert not terminal.process.isalive(), "Blocked fixture did not save its coverage and exit"
                    if os.name != "nt":
                        terminal.process.close()
                        assert terminal.process.exitstatus == 0, terminal.process.exitstatus
                finally:
                    terminal.close()


if __name__ == "__main__":
    main()
