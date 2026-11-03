"""Exercise the native QUIC listener in the real server process."""

from pathlib import Path
import os
import socket
import subprocess
import sys
import tempfile
import time


def free_port(kind):
    with socket.socket(socket.AF_INET, kind) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


with tempfile.TemporaryDirectory(prefix="ascii-chat-quic-cli-") as temporary:
    root = Path(temporary)
    certificate, key, other_cert, other_key = (
        root / name for name in ("server.pem", "server.key", "other.pem", "other.key")
    )
    binary, fixture = sys.argv[1:3]
    subprocess.run([fixture, str(certificate), str(key), str(other_cert), str(other_key)], check=True, timeout=45)
    tcp_port = free_port(socket.SOCK_STREAM)
    websocket_port = free_port(socket.SOCK_STREAM)
    while websocket_port == tcp_port:
        websocket_port = free_port(socket.SOCK_STREAM)
    udp_port = free_port(socket.SOCK_DGRAM)
    command = [binary, "server", "127.0.0.1", "--port", str(tcp_port),
               "--quic-bind-address", "127.0.0.1", "--quic-port", str(udp_port),
               "--quic-tls-cert", str(certificate), "--quic-tls-key", str(key),
               "--websocket-port", str(websocket_port), "--password", "test-password", "--status-screen=false"]
    flags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
    incomplete = subprocess.run([binary, "server", "127.0.0.1", "--quic-bind-address", "127.0.0.1",
                                 "--status-screen=false"], timeout=15, creationflags=flags, capture_output=True,
                                text=True, errors="replace", check=False)
    if incomplete.returncode == 0 or "Failed to join thread" in incomplete.stdout + incomplete.stderr:
        raise RuntimeError("Incomplete QUIC configuration did not fail cleanly")
    with (root / "server.log").open("wb") as log:
        server = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, creationflags=flags)
        try:
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                if server.poll() is not None:
                    break
                if "Server accepting connections" in (root / "server.log").read_text(errors="replace"):
                    break
                time.sleep(0.05)
            else:
                raise RuntimeError("Server did not become ready")
            if server.poll() is not None:
                raise RuntimeError((root / "server.log").read_text(errors="replace"))
            probe_env = os.environ.copy()
            probe_env["ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK"] = "1"
            result = subprocess.run([fixture, "--cli-probe", str(certificate), str(udp_port)], timeout=30,
                                    creationflags=flags, check=False, capture_output=True, text=True, errors="replace",
                                    env=probe_env)
            if result.returncode:
                log.flush()
                sys.stderr.write(f"QUIC probe exited {result.returncode}\n{result.stdout}\n{result.stderr}\n")
                sys.stderr.write((root / "server.log").read_text(errors="replace"))
                raise SystemExit(result.returncode)
        finally:
            if server.poll() is None:
                server.terminate()
            try:
                server.wait(timeout=10)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=10)
