"""Exercise server startup, discovery fallbacks, media clients, and graceful cleanup.

Runs against a built binary, using only loopback services and temporary files.
On Windows each service gets a hidden console so Ctrl+C reaches its real handler.
"""
import contextlib
import json
import os
from pathlib import Path
import signal
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
import urllib.request


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


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
    def __init__(self, binary, directory, name, mode, options):
        self.log = directory / (name + ".log")
        self.output = directory / (name + ".txt")
        # A reused output directory must not satisfy readiness from an older run.
        self.log.write_text("", encoding="utf-8")
        self.stream = self.output.open("wb")
        flags = {}
        if os.name == "nt":
            startup = subprocess.STARTUPINFO()
            startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = subprocess.SW_HIDE
            flags = {"creationflags": subprocess.CREATE_NEW_CONSOLE, "startupinfo": startup}
        env = {key: value for key, value in os.environ.items() if not key.startswith("ASCII_CHAT_")}
        env.update(CLAUDECODE="1", HOME=str(directory), USERPROFILE=str(directory),
                   APPDATA=str(directory), XDG_CONFIG_HOME=str(directory))
        self.process = subprocess.Popen(
            [str(binary), "--no-check-update", "--log-file", str(self.log), "--log-level", "debug", mode, *options],
            cwd=directory, stdout=self.stream, stderr=subprocess.STDOUT, env=env, **flags,
        )

    def text(self):
        return self.log.read_text(encoding="utf-8", errors="replace") if self.log.exists() else ""

    def wait_for(self, message, timeout=20):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if message in self.text():
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
def running(binary, directory, name, mode, options):
    service = Service(binary, directory, name, mode, options)
    try:
        yield service
    finally:
        service.stop()


def server_options(tcp_port, ws_port):
    return ["127.0.0.1", "--port", str(tcp_port), "--websocket-port", str(ws_port),
            "--status-screen=false"]


def check_client(binary, directory, destination, name, extra=()):
    env = {key: value for key, value in os.environ.items() if not key.startswith("ASCII_CHAT_")}
    env.update(CLAUDECODE="1", HOME=str(directory), USERPROFILE=str(directory),
                   APPDATA=str(directory), XDG_CONFIG_HOME=str(directory))
    result = subprocess.run(
        [str(binary), "--no-check-update", "--log-file", str(directory / (name + ".log")), "client", destination,
         "--test-pattern", "--video-codec", "raw", "--audio=false", "--snapshot",
         "--snapshot-delay", "0", "--width", "40", "--height", "12",
         "--color-mode", "none", "--splash-screen=false", *extra],
        cwd=directory, env=env, capture_output=True, timeout=30,
    )
    if result.returncode != 0 or len(result.stdout.strip()) < 100:
        raise AssertionError(f"{name} failed ({result.returncode}): {result.stderr.decode(errors='replace')[-2000:]}")


def check_server(binary, directory, name, extra, expected, clients=False, before_stop=None):
    tcp_port, ws_port = free_port(), free_port()
    with running(binary, directory, name, "server", server_options(tcp_port, ws_port) + extra) as service:
        service.wait_for("Server accepting connections")
        for message in expected:
            service.wait_for(message)
        with urllib.request.urlopen(f"http://127.0.0.1:{ws_port}/health", timeout=3) as response:
            if json.load(response)["status"] != "ok":
                raise AssertionError("WebSocket health failed")
        if clients:
            check_client(binary, directory, f"127.0.0.1:{tcp_port}", "tcp-pattern")
            check_client(binary, directory, f"ws://127.0.0.1:{ws_port}", "ws-pattern")
        if before_stop:
            before_stop()
    if "Server shutdown complete" not in service.text():
        raise AssertionError("Server cleanup callback did not complete")
    print("PASS:", name, flush=True)


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="ascii-server-lifecycle-") as tmp:
        directory = Path(tmp)
        # Public Ed25519 test vector; never use this key outside the fixture.
        identity = directory / "acds-test-key"
        identity.write_bytes(bytes.fromhex(
            "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"
            "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"
        ))
        check_server(binary, directory, "basic", [], ["ACDS registration disabled"], clients=True)
        discovery = ["--discovery", "--discovery-service", "127.0.0.1", "--discovery-service-port", str(free_port())]
        check_server(binary, directory, "privacy-required", discovery, ["Cannot create ACDS session"])
        check_server(binary, directory, "discovery-unavailable", discovery + ["--password", "test-password"],
                     ["Failed to connect to ACDS server"])
        for name, auth in [("discovery-password", ["--password", "test-password"]),
                           ("discovery-public", ["--discovery-expose-ip", "--webrtc",
                                                  "--turn-servers", "turn:127.0.0.1:9",
                                                  "--turn-username", "test", "--turn-credential", "test"])]:
            acds_port, acds_ws = free_port(), free_port()
            with running(binary, directory, name + "-acds", "discovery-service",
                         ["--key", str(identity), "--port", str(acds_port), "--websocket-port", str(acds_ws),
                          "--database", str(directory / (name + ".db")), "--status-screen=false"]) as acds:
                acds.wait_for("Server accepting connections")
                discovery = ["--discovery", "--discovery-service", "127.0.0.1", "--discovery-service-port", str(acds_port),
                             "--stun-servers", "stun:127.0.0.1:9"]
                def verify_and_stop_acds():
                    with contextlib.closing(sqlite3.connect(directory / (name + ".db"))) as db:
                        rows = db.execute("SELECT has_password, expose_ip_publicly FROM sessions").fetchall()
                        expected = [(1, 0)] if name == "discovery-password" else [(0, 1)]
                        if rows != expected:
                            raise AssertionError(f"Incorrect discovery privacy policy: {rows}")
                        participants = db.execute("SELECT COUNT(*) FROM participants").fetchone()[0]
                        if participants != 1:
                            raise AssertionError(f"Server did not join its session: {participants}")
                    acds.stop()

                # The existing receive-thread join needs the signaling peer to
                # close first. Keep this regression probe scoped to extraction.
                check_server(binary, directory, name, discovery + auth,
                             ["Session created:", "ACDS session type: WebRTC"],
                             before_stop=verify_and_stop_acds)



if __name__ == "__main__":
    main()
