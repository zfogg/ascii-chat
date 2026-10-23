#!/usr/bin/env python3
"""Keep receiving TCP frames while a terminal reader stops draining output.

POSIX only; no camera or audio device is needed. By default this starts an
isolated loopback server. --address can exercise an existing LAN server.
"""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401


import argparse
import json
import os
from pathlib import Path
import re
import select
import signal
import socket
import struct
import subprocess
import time


def unused_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def receive_times(text):
    """Read actual ASCII receive callbacks, independently of lag warnings."""
    return [
        (int(hours) * 3600 + int(minutes) * 60 + float(seconds)) * 1000
        for hours, minutes, seconds in re.findall(
            r"\[(\d{2}):(\d{2}):(\d{2}\.\d+)\][^\n]*CALLING_DISPLAY_RENDER:", text
        )
    ]


def receive_intervals(previous, samples):
    """Require observable reception and handle a measurement crossing midnight."""
    assert previous, "No pre-stall receive samples; client DEBUG frame logs are required"
    assert len(samples) >= 2, "No sustained receive samples during the measurement"
    times = [previous[-1], *samples]
    return [(current - before) % 86400000 for before, current in zip(times, times[1:])]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--address")
    parser.add_argument("--server-key", type=Path)
    args = parser.parse_args()
    assert os.name == "posix", "This test requires a POSIX pseudo-terminal"
    import fcntl
    import pty
    import termios

    binary = str(args.binary.resolve())
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TERM="xterm-256color")
    server = None
    server_output = None
    address = args.address
    if not address:
        tcp, ws = unused_port(), unused_port()
        address = f"127.0.0.1:{tcp}"
        # This bypass is confined to the loopback fixture created by this test.
        env["ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK"] = "1"
        server_output = (output / "server.out").open("wb")
        server = subprocess.Popen(
            [binary, "--no-check-update", "--log-level", "warn", "server",
             "127.0.0.1", "--port", str(tcp), "--websocket-port", str(ws),
             "--status-screen=false"],
            env=env, stdin=subprocess.DEVNULL, stdout=server_output,
            stderr=server_output,
        )
        deadline = time.monotonic() + 20
        while True:
            assert server.poll() is None, f"Server exited; see {output / 'server.out'}"
            try:
                with socket.create_connection(("127.0.0.1", tcp), timeout=0.2):
                    break
            except OSError:
                assert time.monotonic() < deadline, "Server did not start listening"
                time.sleep(0.1)

    log = output / "client.log"
    log.unlink(missing_ok=True)
    command = [
        binary, "--no-check-update", "--log-file", str(log), "--log-level",
        "debug", "client", address,
        "--test-pattern", "--video-codec", "raw", "--fps", "60", "--width",
        "80", "--height", "25", "--color-mode", "truecolor", "--audio=false",
        "--splash-screen=false",
    ]
    if args.server_key:
        command += ["--server-key", str(args.server_key.resolve())]
    pid, descriptor = pty.fork()
    if pid == 0:
        os.execve(binary, command, env)
    fcntl.ioctl(descriptor, termios.TIOCSWINSZ, struct.pack("HHHH", 25, 80, 0, 0))
    ended = False

    def drain(seconds):
        nonlocal ended
        received = bytearray()
        deadline = time.monotonic() + seconds
        while not ended and time.monotonic() < deadline:
            readable, _, _ = select.select([descriptor], [], [], 0.02)
            if readable:
                try:
                    chunk = os.read(descriptor, 65536)
                    ended = not chunk
                    received.extend(chunk)
                except OSError:
                    ended = True
        return received

    try:
        before = bytearray()
        warmup_deadline = time.monotonic() + 20
        while before.count(b"\x1b[H") < 30 and time.monotonic() < warmup_deadline and not ended:
            before.extend(drain(0.25))
        assert not ended, "Client exited before the terminal stall"
        assert before.count(b"\x1b[H") >= 30, "No sustained media presentation before the stall"
        drain(1)
        previous = receive_times(log.read_text(errors="replace"))
        assert previous, "No pre-stall receive samples; client DEBUG frame logs are required"
        log_offset = log.stat().st_size
        # Fill the PTY's finite output queue, as a slow/minimized terminal can.
        time.sleep(2)
        with log.open("rb") as logfile:
            logfile.seek(log_offset)
            stalled_samples = receive_times(logfile.read().decode(errors="replace"))
        after = drain(4)
        assert after.count(b"\x1b[H") >= 8, "Media did not resume after draining the terminal"
        with log.open("rb") as logfile:
            logfile.seek(log_offset)
            text = logfile.read().decode(errors="replace")
        samples = receive_times(text)
        intervals = receive_intervals(previous, samples)
        result = {
            "presentations_before_stall": before.count(b"\x1b[H"),
            "presentations_after_stall": after.count(b"\x1b[H"),
            "frames_received_while_terminal_stalled": len(stalled_samples),
            "receive_samples": len(samples),
            "maximum_receive_interval_ms": max(intervals),
            "command": command,
        }
        (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result), flush=True)
        assert len(stalled_samples) >= 8, "Reception did not continue while the terminal was stalled"
        assert max(intervals) < 500, "A terminal stall delayed network frames"
    finally:
        try:
            os.kill(pid, signal.SIGINT)
            drain(0.5)
            if os.waitpid(pid, os.WNOHANG)[0] == 0:
                os.kill(pid, signal.SIGKILL)
                os.waitpid(pid, 0)
        except ProcessLookupError:
            pass
        os.close(descriptor)
        if server:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
        if server_output:
            server_output.close()


if __name__ == "__main__":
    main()
