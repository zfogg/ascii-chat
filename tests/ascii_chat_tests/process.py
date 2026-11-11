"""Process, network, and CLI fixtures for integration tests."""
import contextlib
import os
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path
import tempfile

from .cli import clean_environment, cli_command

def hidden_console_options():
    """Start a child in a private hidden console on Windows."""
    if os.name != "nt":
        return {}
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    return {"creationflags": subprocess.CREATE_NEW_CONSOLE, "startupinfo": startup}


def free_port():
    """Return a loopback port reserved across all xdist workers in this run."""
    from filelock import FileLock

    run_id = os.environ.get("ASCII_CHAT_TEST_RUN_ID", str(os.getpid()))
    root = Path(tempfile.gettempdir()) / "ascii-chat-test-ports"
    root.mkdir(parents=True, exist_ok=True)
    registry = root / f"{run_id}.ports"
    lock = FileLock(str(registry) + ".lock")
    for _ in range(100):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        with lock:
            used = {int(line) for line in registry.read_text(encoding="ascii").splitlines()} if registry.exists() else set()
            if port not in used:
                with registry.open("a", encoding="ascii") as stream:
                    stream.write(f"{port}\n")
                return port
    raise RuntimeError("Unable to reserve a unique loopback test port")


def interrupt(process):
    if os.name != "nt":
        process.send_signal(signal.SIGINT)
        return
    # Attach a short-lived helper to this service's private console. Never send
    # console events to the terminal running the test or to other services.
    helper = r"""
import ctypes, sys, time
kernel = ctypes.WinDLL('kernel32', use_last_error=True)
kernel.FreeConsole()
if not kernel.AttachConsole(int(sys.argv[1])):
    raise ctypes.WinError(ctypes.get_last_error())
if not kernel.SetConsoleCtrlHandler(None, True):
    raise ctypes.WinError(ctypes.get_last_error())
if not kernel.GenerateConsoleCtrlEvent(0, 0):
    raise ctypes.WinError(ctypes.get_last_error())
time.sleep(0.2)
kernel.FreeConsole()
"""
    subprocess.run([sys.executable, "-c", helper, str(process.pid)], check=True, timeout=5)


class Service:
    def __init__(self, binary, directory, name, mode, options, environment=None, log_level="debug", context=None):
        self.context = context
        self.log = context.log_path() if context else directory / (name + ".log")
        command = (context.command(mode, *options, log_file=self.log) if context else
                   cli_command(binary, "--log-file", str(self.log), "--log-level", log_level, mode, *options))
        if "--log-file" in command:
            self.log = Path(command[command.index("--log-file") + 1])
        self.output = directory / (name + ".txt")
        # A reused output directory must not satisfy readiness from an older run.
        self.log.write_text("", encoding="utf-8")
        self.stream = self.output.open("wb")
        flags = hidden_console_options()
        env = context.environment.copy() if context else clean_environment()
        env.update(CLAUDECODE="1", HOME=str(directory), USERPROFILE=str(directory),
                   APPDATA=str(directory), XDG_CONFIG_HOME=str(directory),
                   ASCII_CHAT_OTEL_ENABLED_TELEMETRY="none")
        env.update(environment or {})
        self.process = subprocess.Popen(command, cwd=directory, stdout=self.stream,
                                        stderr=subprocess.STDOUT, env=env, **flags)

    def text(self):
        return self.log.read_text(encoding="utf-8", errors="replace") if self.log.exists() else ""

    def wait_for(self, message, timeout=20):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = self.text()
            output = self.output.read_text(encoding="utf-8", errors="replace") if self.output.exists() else ""
            if message in text or message in output:
                return
            if self.process.poll() is not None:
                raise AssertionError(f"Service exited {self.process.returncode}: {self.output.read_text(errors='replace')[-20000:]}")
            time.sleep(0.05)
        raise AssertionError(f"Missing {message!r}: {self.text()[-3000:]}")

    def stop(self):
        try:
            if self.process.poll() is None:
                interrupt(self.process)
            code = self.process.wait(timeout=20)
            if code != 0:
                raise AssertionError(f"Shutdown exit {code}: {self.text()[-3000:]}")
        finally:
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait()
            self.stream.close()


@contextlib.contextmanager
def running(binary, directory, name, mode, options, context=None):
    service = Service(binary, directory, name, mode, options, context=context)
    try:
        yield service
    finally:
        service.stop()


def server_options(tcp_port, ws_port):
    # Omitting the bind positional uses ascii-chat's localhost dual-stack
    # default: 127.0.0.1 and ::1, never wildcard addresses.
    return ["--port", str(tcp_port), "--websocket-port", str(ws_port),
            "--status-screen=false"]


def check_client(binary, directory, destination, name, extra=()):
    env = clean_environment()
    env.update(CLAUDECODE="1", HOME=str(directory), USERPROFILE=str(directory),
                   APPDATA=str(directory), XDG_CONFIG_HOME=str(directory),
                   ASCII_CHAT_OTEL_ENABLED_TELEMETRY="none")
    log = directory / (name + ".log")
    command = cli_command(binary, "--log-file", str(log), "client", destination,
         "--test-pattern", "--video-codec", "raw", "--audio=false", "--snapshot",
         "--snapshot-delay", "0", "--width", "40", "--height", "12",
         "--color-mode", "none", "--splash-screen=false", *extra)
    try:
        result = subprocess.run(command, cwd=directory, env=env, capture_output=True, timeout=30)
    except subprocess.TimeoutExpired as error:
        text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else "No client log"
        raise AssertionError(f"{name} timed out: {text[-12000:]}") from error
    if result.returncode != 0 or len(result.stdout.strip()) < 100:
        raise AssertionError(f"{name} failed ({result.returncode}): {result.stderr.decode(errors='replace')[-2000:]}")
