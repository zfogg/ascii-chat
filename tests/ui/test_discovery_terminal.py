"""Windows ConPTY integration tests for the discovery invitation.

Install pywinpty and pyte in a test environment, then run:
  python tests/ui/test_discovery_terminal.py build/bin/ascii-chat.exe
Uses a private local ACDS, ephemeral ports, and generated test-pattern video.
"""
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import sys
import threading
import time
import ctypes as c

import pyte
from winpty import PtyProcess

EXE = Path(sys.argv[1]).resolve()
DLL_DIRECTORY = os.add_dll_directory(str(EXE.parent))
LIB = c.CDLL(str(EXE.parent / "asciichat.dll"))
LIB.asciichat_crc32_hw.argtypes = [c.c_void_p, c.c_size_t]
LIB.asciichat_crc32_hw.restype = c.c_uint32
OUT = EXE.parent.parent / "invitation-terminal-test"
OUT.mkdir(exist_ok=True)
ENV = os.environ.copy()
ENV.update(TERM="xterm-256color", COLORTERM="truecolor", ASCII_CHAT_QUESTION_PROMPT_RESPONSE="y;y;y;y")
for key in ("CLAUDECODE", "COLUMNS", "ROWS"):
    ENV.pop(key, None)


def port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def until(check, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if check():
            return
        time.sleep(.1)
    raise AssertionError("Timed out waiting for terminal condition")


class Terminal:
    def __init__(self, name, args, parse_screen=True):
        self.name = name
        self.parse_screen = parse_screen
        self.log = OUT / (name + ".log")
        self.screen = pyte.Screen(80, 24)
        self.stream = pyte.Stream(self.screen)
        self.lock = threading.Lock()
        self.raw = []
        self.process = PtyProcess.spawn([str(EXE), "--log-file", str(self.log), *args],
                                        env=ENV, dimensions=(24, 80))
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()

    def read(self):
        try:
            while True:
                chunk = self.process.read(65536)
                with self.lock:
                    self.raw.append(chunk)
                    if self.parse_screen:
                        self.stream.feed(chunk)
        except (EOFError, OSError):
            pass

    def text(self):
        with self.lock:
            return "\n".join(self.screen.display)

    def save(self, suffix):
        (OUT / (self.name + "-" + suffix + ".txt")).write_text(self.text(), encoding="utf-8")

    def resize(self, rows, cols):
        with self.lock:
            self.screen.resize(rows, cols)
            self.process.setwinsize(rows, cols)

    def stop(self):
        if self.process.isalive():
            self.process.write("\x03")
            until(lambda: not self.process.isalive(), 8)

    def close(self):
        if self.process.isalive():
            self.process.terminate(force=True)
        self.reader.join(timeout=2)
        (OUT / (self.name + ".raw")).write_bytes("".join(self.raw).encode("utf-8"))


acds_port, ws_port = port(), port()
service_output = open(OUT / "acds-output.txt", "w", encoding="utf-8")
service = subprocess.Popen([str(EXE), "--log-file", str(OUT / "acds.log"), "discovery-service",
                            "127.0.0.1", "--port", str(acds_port), "--websocket-port", str(ws_port),
                            "--database", str(OUT / "acds.db"), "--status-screen=false"],
                           stdout=service_output, stderr=subprocess.STDOUT, env=ENV,
                           creationflags=subprocess.CREATE_NO_WINDOW)
terminals = []
common = ["--discovery-service", "127.0.0.1", "--discovery-service-port", str(acds_port),
          "--test-pattern", "--audio=false", "--splash-screen=true"]


def launch(name, args, parse_screen=True):
    terminal = Terminal(name, args, parse_screen)
    terminals.append(terminal)
    return terminal


def invitation(terminal):
    until(lambda: "Run: ascii-chat " in terminal.text())
    lines = [line.strip() for line in terminal.text().splitlines()]
    command = next(line for line in lines if line.startswith("Run: ascii-chat "))
    session = command.removeprefix("Run: ascii-chat ")
    assert lines[lines.index(command) - 1] == session
    assert "[INFO" not in terminal.text() and "[DEBUG" not in terminal.text()
    return session


try:
    time.sleep(1)
    assert service.poll() is None, "Local discovery service failed to start"
    host_port = port()
    host = launch("waiting", [*common, "--port", str(host_port)])
    invitation(host)
    time.sleep(2)
    invitation(host)  # Host's own frames must not dismiss the invitation.
    host.save("80x24")
    host.resize(12, 50)
    time.sleep(2)
    invitation(host)
    assert "ascii-chat" in host.text() and "__" not in host.text()
    host.save("50x12")
    host.resize(24, 80)
    time.sleep(2)
    invitation(host)
    host.save("restored")
    print("PASS: waiting with local camera, adjacent instruction, shrink and grow", flush=True)

    # A protocol-capable remote with no camera must end the waiting screen.
    # This directly exercises the host's TCP transport, not ACDS host negotiation.
    with socket.create_connection(("127.0.0.1", host_port), timeout=5) as peer:
        def drain():
            try:
                while peer.recv(65536):
                    pass
            except OSError:
                pass
        threading.Thread(target=drain, daemon=True).start()
        time.sleep(.5)
        invitation(host)  # Merely accepting a socket is not protocol readiness.
        payload = struct.pack("!IIIIHH32s32sBII64sBBBII", 0, 0, 0, 0, 80, 24,
                              b"xterm", b"", 1, 0, 0, b"", 30, 0, 0, 0, 0)
        header = struct.pack("!QHIII", 0xA5C11C4A1, 5000, len(payload), LIB.asciichat_crc32_hw(payload, len(payload)), 0)
        peer.sendall(header + payload)
        until(lambda: "Run: ascii-chat " not in host.text())
        assert host.process.isalive()
        host.save("camera-disabled-peer")
    host.stop()
    print("PASS: TCP capabilities-only peer handoff and Ctrl+C", flush=True)

    rtc_host = launch("webrtc-host", [*common, "--prefer-webrtc", "--color-mode", "none", "--port", str(port())])
    session = invitation(rtc_host)
    rtc_peer = launch("webrtc-peer", [session, *common, "--prefer-webrtc", "--color-mode", "none", "--port", str(port())])
    until(lambda: "Client 2 requested render dimensions" in rtc_host.log.read_text(encoding="utf-8"), 30)
    until(lambda: "Run: ascii-chat " not in rtc_host.text())
    until(lambda: "Connecting to session:" not in rtc_peer.text())
    time.sleep(1)
    assert rtc_host.process.isalive() and rtc_peer.process.isalive()
    rtc_host.save("connected")
    rtc_peer.save("connected")
    assert "DataChannel opened" in rtc_peer.log.read_text(encoding="utf-8")
    rtc_peer.stop()
    rtc_host.stop()
    print("PASS: two native discovery peers connect over WebRTC and leave the splash", flush=True)

    disabled = launch("disabled", [*common[:-1], "--splash-screen=false", "--port", str(port())], parse_screen=False)
    until(lambda: "Run: ascii-chat " in "".join(disabled.raw))
    assert "Share this string" in "".join(disabled.raw)
    disabled.stop()
    print("PASS: splash-disabled text invitation", flush=True)

    snapshot = subprocess.run([str(EXE), *common, "--port", str(port()), "--snapshot", "--snapshot-delay", "0"],
                              capture_output=True, env=ENV, timeout=20, creationflags=subprocess.CREATE_NO_WINDOW)
    (OUT / "snapshot.stdout").write_bytes(snapshot.stdout)
    (OUT / "snapshot.stderr").write_bytes(snapshot.stderr)
    assert snapshot.returncode == 0, "Snapshot failed"
    assert b"Run: ascii-chat" not in snapshot.stdout
    assert b"Share this string" not in snapshot.stdout
    assert len(snapshot.stdout) > 0
    print("PASS: piped snapshot contains frame output without invitation", flush=True)

    failed = launch("connection-error", ["--discovery-service", "127.0.0.1", "--discovery-service-port", str(port()),
                                         "--test-pattern", "--audio=false", "--splash-screen=true"])
    until(lambda: not failed.process.isalive(), 15)
    until(lambda: "Discovery failed:" in "".join(failed.raw), 2)
    failed.save("exit")
    print("PASS: connection failure restores terminal and reports error", flush=True)
finally:
    for terminal in terminals:
        terminal.close()
    service.terminate()
    service.wait(timeout=10)
    service_output.close()
