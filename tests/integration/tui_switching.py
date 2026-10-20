"""Verify exclusive TUI switches and detect even transient media writes in a real PTY.

Run: python tests/integration/tui_switching.py --binary build/bin/ascii-chat.exe
Requires the same PTY dependencies as terminal_ui.py and ffmpeg.
"""
import argparse
import itertools
import os
from pathlib import Path
import subprocess
import tempfile

from terminal_ui import CaptureScreen, Terminal


MARKER = "Q"
original_draw = CaptureScreen.draw
original_erase_display = CaptureScreen.erase_in_display
original_erase_line = CaptureScreen.erase_in_line


def check_preserved(screen):
    for row, col, label in getattr(screen, "preserved", ()):
        actual = "".join(screen.buffer[row][col + i].data for i in range(len(label)))
        if actual != label:
            screen.preservation_failures.append((row, label, actual))


def observe_erase_display(self, *args, **kwargs):
    original_erase_display(self, *args, **kwargs)
    check_preserved(self)


def observe_erase_line(self, *args, **kwargs):
    original_erase_line(self, *args, **kwargs)
    check_preserved(self)


def observe_draw(self, data):
    probe = getattr(self, "media_probe_tail", "") + data
    if getattr(self, "forbid_media", False) and MARKER * 8 in probe:
        self.media_leaks.append(data)
    self.media_probe_tail = probe[-7:]
    original_draw(self, data)
    check_preserved(self)


CaptureScreen.draw = observe_draw
CaptureScreen.erase_in_display = observe_erase_display
CaptureScreen.erase_in_line = observe_erase_line


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    args = parser.parse_args()
    binary = str(args.binary.resolve())
    with tempfile.TemporaryDirectory(prefix="ascii-tui-switch-") as directory:
        root = Path(directory)
        media = root / "video.mkv"
        subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
                        "testsrc2=size=160x120:rate=30", "-t", "2", "-c:v", "libx264", str(media)], check=True)
        previous = os.environ.copy()
        for key in list(os.environ):
            if key.startswith("ASCII_CHAT_"):
                del os.environ[key]
        for suffix in ("ascii-chat", "AppData/Roaming/ascii-chat", ".config/ascii-chat"):
            (root / suffix).mkdir(parents=True, exist_ok=True)
        os.environ.update(HOME=str(root), USERPROFILE=str(root), APPDATA=str(root), XDG_CONFIG_HOME=str(root))
        term = None
        try:
            term = Terminal([binary, "--no-check-update", "--log-file", str(root / "application.log"),
                             "mirror", "--file", str(media), "--loop", "--audio=false",
                             "--splash-screen=false", "--palette", "custom", "--palette-chars", MARKER * 2,
                             "--color-mode", "none", "--utf8", "true", "--fps", "30"], rows=55, cols=180)
            term.screen.media_leaks = []
            labels = {"0": "Sync primitives |", "?": "Keyboard Shortcuts", "]": "] / Esc: close", "o": "Open media:"}

            def expect_screen(key):
                return term.expect(lambda t: labels[key] in t and
                                   all(label not in t for other, label in labels.items() if other != key),
                                   f"Missing exclusive {key!r} TUI")

            def media_visible(timeout=2):
                term.screen.forbid_media = False
                term.expect(lambda t: MARKER * 8 in t and not any(label in t for label in labels.values()),
                            "Media did not resume, or a previous TUI resurfaced", timeout=timeout)
                term.pump(0.3)

            def switch(key):
                term.screen.forbid_media = True
                term.write(key)
                expect_screen(key)
                term.pump(0.15)
                assert not term.screen.media_leaks, f"Media appeared between TUIs: {term.screen.media_leaks[:2]}"

            media_visible(timeout=30)
            def watch(*labels):
                term.screen.preservation_failures = []
                term.screen.preserved = []
                for label in labels:
                    row = next(i for i, line in enumerate(term.screen.display) if label in line)
                    term.screen.preserved.append((row, term.screen.display[row].index(label), label))

            def preserved():
                assert not term.screen.preservation_failures, term.screen.preservation_failures[:3]

            def stop_watch():
                preserved()
                term.screen.preserved = []

            def quiet_help():
                term.pump(0.3)
                before = len(term.raw)
                term.pump(2)
                assert len(term.raw) == before, "Idle help was repainted without a setting or resize change"
                preserved()

            term.write("?")
            expect_screen("?")
            term.pump(0.3)
            watch("Keyboard Shortcuts")
            quiet_help()
            before_volume = next(line for line in term.screen.display if "Volume :" in line)
            term.write("\x1b[B")
            term.expect(lambda t: "Volume :" in t and before_volume not in t, "Help did not reflect changed volume")
            quiet_help()
            stop_watch()
            term.resize(57, 182)
            expect_screen("?")
            term.pump(0.5)
            watch("Keyboard Shortcuts")
            quiet_help()
            stop_watch()
            term.resize(55, 180)
            term.pump(0.5)
            print("PASS idle help emits no terminal output, settings update in place, resize recovers", flush=True)

            term.write("0")
            term.expect(lambda t: "Sync primitives |" in t and "0/Esc close" in t, "Missing sync footer")
            term.pump(0.3)
            watch("Sync primitives |", "0/Esc close")
            before = len(term.raw)
            term.pump(2)
            assert len(term.raw) > before, "Live sync data stopped refreshing"
            term.write("\x1b[F")
            term.pump(0.5)
            term.write("\x1b[H")
            term.pump(0.5)
            stop_watch()
            print("PASS sync heading/footer never erased during live updates or page changes", flush=True)

            for key, label in (("]", "] / Esc: close"), ("o", "Open media:")):
                term.write(key)
                expect_screen(key)
                term.pump(0.3)
                watch(label)
                term.pump(2)
                if key == "o":
                    term.write("abc")
                    term.expect(lambda t: "> abc" in t, "Media input did not accept text")
                    term.write("\x7f\x7f")
                    term.expect(lambda t: "> a" in t and "> abc" not in t, "Deleted media text was left on screen")
                stop_watch()
            term.write("o")
            media_visible()
            print("PASS logs/media picker preserve headings while refreshing and editing", flush=True)

            for source, target in itertools.permutations(labels, 2):
                term.write(source)
                expect_screen(source)
                term.pump(0.2)
                switch(target)
                term.screen.forbid_media = False
                term.write(target)
                media_visible()
            print("PASS all 12 directed TUI switches, no intermediate media, all four self-toggles", flush=True)

            for sequence in ("]o?00", "0?00"):
                term.write(sequence[0])
                expect_screen(sequence[0])
                term.pump(0.2)
                for key in sequence[1:-1]:
                    switch(key)
                term.screen.forbid_media = False
                term.write(sequence[-1])
                media_visible()
                # Also exercise the same sequence buffered as one input burst.
                for _ in range(5):
                    term.write(sequence)
                    term.pump(0.8)
                    media_visible()
                print(f"PASS {sequence!r} paced and buffered, immediate media recovery", flush=True)

            term.write("?")
            expect_screen("?")
            term.resize(12, 24)
            term.expect(lambda t: "Terminal too small" in t, "Missing size cover")
            term.resize(55, 180)
            expect_screen("?")
            term.pump(0.3)
            switch("0")
            term.screen.forbid_media = False
            term.write("0")
            media_visible()
            print("PASS too-small cover retains current TUI and recovers without resurrecting older screens", flush=True)
            term.interrupt()
        finally:
            if term:
                term.close()
            os.environ.clear()
            os.environ.update(previous)


if __name__ == "__main__":
    main()
