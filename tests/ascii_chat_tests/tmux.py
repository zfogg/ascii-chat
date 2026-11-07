"""tmux-backed terminal fixtures."""
import shlex
import subprocess
import time

SOCKET: str = "ascii390"


def tmux(*args):
    return subprocess.check_output(["tmux", "-L", SOCKET, *map(str, args)], text=True)


class Pane:
    def __init__(self, name, argv, root, cols=80, rows=8):
        self.name, self.root = name, root
        self.allowed_exit_codes = {"0"}
        self.directory = root / name
        self.directory.mkdir(parents=True, exist_ok=True)
        previous = self.directory / ("previous-" + str(time.time_ns()))
        for old in [*self.directory.glob("sanitizer.*"), self.directory / "application.log"]:
            if old.exists():
                previous.mkdir(exist_ok=True)
                old.rename(previous / old.name)
        (self.directory / "exit").unlink(missing_ok=True)
        # Only synthetic fixtures and loopback services are used.
        command = "env -u CLAUDECODE -u ASCII_CHAT_QUESTION_PROMPT_RESPONSE "
        command += shlex.quote("ASAN_OPTIONS=log_path=" + str(self.directory / "sanitizer")) + " " + shlex.join(argv)
        command += "; rc=$?; printf '%s' \"$rc\" > " + shlex.quote(str(self.directory / "exit"))
        tmux("new-session", "-d", "-s", name, "-x", cols, "-y", rows, "bash", "-c", command)

    def capture(self, label=None):
        text = tmux("capture-pane", "-p", "-t", self.name)
        if label:
            (self.directory / (label + ".txt")).write_text(text)
            (self.directory / (label + ".ansi")).write_text(tmux("capture-pane", "-e", "-p", "-t", self.name))
        return text

    def expect(self, predicate, label, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = self.capture()
            if predicate(text):
                self.capture(label)
                return text
            if (self.directory / "exit").exists():
                break
            time.sleep(.1)
        raise AssertionError(label + "\n" + self.capture("failure"))

    def resize(self, cols, rows):
        tmux("resize-window", "-t", self.name, "-x", cols, "-y", rows)

    def key(self, *keys):
        tmux("send-keys", "-t", self.name, *keys)

    def text(self, text):
        tmux("send-keys", "-t", self.name, "-l", text)

    def close(self):
        if not (self.directory / "exit").exists():
            self.key("C-c")
            deadline = time.monotonic() + 20
            while not (self.directory / "exit").exists() and time.monotonic() < deadline:
                time.sleep(.1)
        self.capture("final")
        (self.directory / "history.txt").write_text(tmux("capture-pane", "-p", "-S", "-", "-t", self.name))
        tmux("kill-session", "-t", self.name)
        status = (self.directory / "exit").read_text() if (self.directory / "exit").exists() else "timeout"
        log = (self.directory / "application.log").read_text(errors="replace") if (self.directory / "application.log").exists() else ""
        assert status in self.allowed_exit_codes, "Exit status: " + status
        assert "ERROR: AddressSanitizer" not in log and "runtime error:" not in log, "Sanitizer finding"
        assert not list(self.directory.glob("sanitizer.*")), "Sanitizer report emitted"
