#!/usr/bin/env python3
"""Opt-in Windows-to-macOS physical-LAN discovery and handshake test."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401


import argparse
import base64
import os
from pathlib import Path, PurePosixPath
import shlex
import subprocess
import tempfile
import time

try:
    from mdns_terminal import capture
    from terminal_ui import Terminal
except ImportError as error:
    print(f"SKIP: terminal prerequisites unavailable: {error}")
    raise SystemExit(77)


def ssh(target, command, *, check=True):
    return subprocess.run(
        ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", target, command],
        check=check, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=20,
    )


def remote_text(target, path):
    return ssh(target, f"cat {shlex.quote(path)} 2>/dev/null || true", check=False).stdout


def wait_remote(target, path, message, timeout=30):
    deadline = time.monotonic() + timeout
    text = ""
    while time.monotonic() < deadline:
        text = remote_text(target, path)
        if message in text:
            return text
        time.sleep(.1)
    raise AssertionError(f"Remote log omitted {message!r}:\n{text[-8000:]}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--ssh", default=os.environ.get("ASCIICHAT_MAC_SSH"))
    parser.add_argument("--remote-binary", default=os.environ.get("ASCIICHAT_MAC_BINARY"))
    parser.add_argument("--server-address", default=os.environ.get("ASCIICHAT_MAC_LAN_ADDRESS"))
    args = parser.parse_args()
    if not all((args.ssh, args.remote_binary, args.server_address)):
        print("SKIP: set ASCIICHAT_MAC_SSH, ASCIICHAT_MAC_BINARY, and ASCIICHAT_MAC_LAN_ADDRESS")
        raise SystemExit(77)

    binary = args.binary.resolve()
    port_probe = ("python3 -c 'import socket; s=socket.socket(); s.bind((\"0.0.0.0\",0)); "
                  "print(s.getsockname()[1]); s.close()'")
    tcp_port = int(ssh(args.ssh, port_probe).stdout.strip())
    websocket_port = int(ssh(args.ssh, port_probe).stdout.strip())
    token = f"{tcp_port}-{int(time.time())}"
    remote_log = f"/tmp/ascii-chat-mdns-cross-host-{token}.log"
    remote_pid = f"/tmp/ascii-chat-mdns-cross-host-{token}.pid"
    remote_script = f"/tmp/ascii-chat-mdns-cross-host-{token}.sh"
    library = str(PurePosixPath(args.remote_binary).parents[1] / "lib")
    command = (
        f"echo $$ > {shlex.quote(remote_pid)}; exec env DYLD_LIBRARY_PATH={shlex.quote(library)} "
        f"{shlex.quote(args.remote_binary)} --no-check-update --log-file {shlex.quote(remote_log)} "
        f"--log-level debug server 0.0.0.0 --port {tcp_port} --websocket-port {websocket_port} "
        "--status-screen=false"
    )
    encoded = base64.b64encode(command.encode()).decode()
    prepare = (
        f"echo {encoded} | /usr/bin/base64 -D > {shlex.quote(remote_script)} && "
        f"chmod 700 {shlex.quote(remote_script)} && "
        f"nohup {shlex.quote(remote_script)} >/dev/null 2>&1 &"
    )

    terminal = None
    prepared = False
    with tempfile.TemporaryDirectory(prefix="ascii-mdns-cross-host-") as temporary:
        root = Path(temporary)
        local_log = root / "client.log"
        try:
            ssh(args.ssh, prepare)
            prepared = True
            wait_remote(args.ssh, remote_log, "Server accepting connections")
            wait_remote(args.ssh, remote_log, "mDNS: Server advertised")
            # Bonjour registration is asynchronous; allow the multicast record
            # to reach the other host before the client's finite scan window.
            time.sleep(3)
            for key in list(os.environ):
                if key.startswith("ASCII_CHAT_"):
                    del os.environ[key]
            os.environ.update(TERM="xterm-256color", ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK="1")
            for attempt in range(3):
                local_log.write_text("", encoding="utf-8")
                terminal = Terminal([
                    str(binary), "--no-check-update", "--log-file", str(local_log), "--log-level", "debug",
                    "client", "--scan", "--test-pattern", "--audio=false", "--video-codec", "raw", "--fps", "5",
                    "--splash-screen=true", "--reconnect-attempts", "0",
                ], rows=24, cols=110)
                try:
                    picker = terminal.expect(
                        lambda text: "LAN servers" in text and args.server_address in text and str(tcp_port) in text,
                        "physical Mac server did not appear in the LAN picker", timeout=8,
                    )
                    break
                except AssertionError:
                    terminal.close()
                    terminal = None
                    if attempt == 2:
                        raise
                    time.sleep(.5)
            capture(terminal, args.artifacts, "macbook-physical-lan-picker")
            selected = next(line for line in picker.splitlines() if str(tcp_port) in line)
            terminal.write(selected.split("[", 1)[1].split("]", 1)[0] + "\r")
            deadline = time.monotonic() + 30
            client_text = ""
            while time.monotonic() < deadline:
                terminal.pump(.05)
                client_text = local_log.read_text(errors="replace") if local_log.exists() else ""
                if "Crypto handshake completed successfully" in client_text:
                    break
            else:
                raise AssertionError(f"Physical-LAN selection did not complete its handshake:\n{client_text[-8000:]}")
            wait_remote(args.ssh, remote_log, "Crypto handshake completed successfully", timeout=10)
            wait_remote(args.ssh, remote_log, "Frame committed", timeout=20)
            wait_remote(args.ssh, remote_log, "SEND_ASCII_FRAME", timeout=20)
            terminal.expect(
                lambda text: "LAN servers" not in text
                and sum(character not in " \r\n" for character in text) > 300,
                "Mac server did not render and deliver an ASCII-art frame", timeout=20,
            )
            capture(terminal, args.artifacts, "macbook-physical-lan-rendered")
            print(f"PASS: discovered Mac at {args.server_address}:{tcp_port}; Mac rendered and delivered ASCII art",
                  flush=True)
        finally:
            if terminal is not None:
                terminal.close()
            if prepared:
                cleanup = (
                    f"pid=$(cat {shlex.quote(remote_pid)} 2>/dev/null); "
                    f"if test -n \"$pid\"; then kill \"$pid\" 2>/dev/null || true; fi; sleep 1; "
                    f"rm -f {shlex.quote(remote_pid)} {shlex.quote(remote_script)}"
                )
                ssh(args.ssh, cleanup, check=False)


if __name__ == "__main__":
    main()
