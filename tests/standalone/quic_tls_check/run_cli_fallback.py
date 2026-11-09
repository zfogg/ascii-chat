"""Verify QUIC-to-TCP fallback requires and preserves application identity."""

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


binary, fixture = sys.argv[1:3]
flags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
# macOS resolves its default temporary directory outside accepted key paths;
# Linux containers may run as root, whose home is a protected system path.
temporary_parent = Path.home() if sys.platform == "darwin" else None
with tempfile.TemporaryDirectory(prefix="ascii-chat-quic-fallback-", dir=temporary_parent) as temporary:
    root = Path(temporary)
    cert, tls_key, wrong_cert, wrong_tls_key = (
        root / name for name in ("server.pem", "server.key", "other.pem", "other.key")
    )
    subprocess.run([fixture, str(cert), str(tls_key), str(wrong_cert), str(wrong_tls_key)],
                   check=True, timeout=45, creationflags=flags)
    key, wrong_key = root / "identity", root / "wrong-identity"
    for identity in (key, wrong_key):
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", str(identity)],
                       check=True, timeout=15, creationflags=flags)
    tcp_port = free_port(socket.SOCK_STREAM)
    websocket_port = free_port(socket.SOCK_STREAM)
    while websocket_port == tcp_port:
        websocket_port = free_port(socket.SOCK_STREAM)
    udp_port = free_port(socket.SOCK_DGRAM)
    unavailable_port = free_port(socket.SOCK_DGRAM)
    while unavailable_port == udp_port:
        unavailable_port = free_port(socket.SOCK_DGRAM)
    server_log = root / "server.log"
    env = os.environ.copy()
    # The test still verifies the explicitly pinned key and signature.
    env["ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK"] = "1"
    with server_log.open("wb") as output:
        server = subprocess.Popen(
            [binary, "server", "127.0.0.1", "--port", str(tcp_port), "--websocket-port", str(websocket_port),
             "--quic-bind-address", "127.0.0.1", "--quic-port", str(udp_port), "--tls-cert", str(cert),
             "--tls-key", str(tls_key), "--key", str(key), "--password", "test-password",
             "--status-screen=false"], stdout=output, stderr=subprocess.STDOUT, creationflags=flags, env=env,
        )
        try:
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                if server.poll() is not None:
                    raise RuntimeError(server_log.read_text(errors="replace"))
                if "Server accepting connections" in server_log.read_text(errors="replace"):
                    break
                time.sleep(0.05)
            else:
                raise RuntimeError("Server did not become ready")

            def client(name, port, trust, identity=None, extra=()):
                log = root / f"{name}.log"
                command = [binary, "--log-file", str(log), "client", f"quic://localhost:{port}",
                           "--tls-ca-file", str(trust), "--quic-fallback-port", str(tcp_port),
                           "--password", "test-password", "--test-pattern", "--audio=false", "--snapshot",
                           "--snapshot-delay", "0", "--splash-screen=false", "--color-mode", "none",
                           "--reconnect-attempts", "0"]
                if identity:
                    command += ["--server-key", str(identity) + ".pub"]
                command += list(extra)
                result = subprocess.run(command, timeout=35, creationflags=flags, env=env, capture_output=True,
                                        text=True, errors="replace", check=False)
                return result, log.read_text(errors="replace") if log.exists() else ""

            accepted, logs = client("accepted", unavailable_port, cert, key)
            if (accepted.returncode or not accepted.stdout.strip() or "trying authenticated TCP fallback" not in logs or
                    "Server identity key verified against --server-key" not in logs or "using native QUIC" in logs):
                raise RuntimeError(f"Authenticated TCP fallback failed: {accepted.stderr}\n{logs}")
            rejected, logs = client("wrong-key", unavailable_port, cert, wrong_key)
            if (not rejected.returncode or "trying authenticated TCP fallback" not in logs or
                    "Server identity key verified against --server-key" in logs or
                    "Authentication failed" not in logs):
                raise RuntimeError(f"Fallback did not reject the wrong ACIP identity: {rejected.stderr}\n{logs}")
            accepted, logs = client("accepted-again", unavailable_port, cert, key)
            if (accepted.returncode or not accepted.stdout.strip() or "trying authenticated TCP fallback" not in logs or
                    "Server identity key verified against --server-key" not in logs):
                raise RuntimeError(f"Correct identity failed after rejection: {accepted.stderr}\n{logs}")
            rejected, logs = client("no-key", unavailable_port, cert)
            if not rejected.returncode or "trying authenticated TCP fallback" in logs:
                raise RuntimeError(f"Fallback accepted an unpinned identity: {logs}")
            rejected, logs = client("no-encryption", unavailable_port, cert, key, ("--no-encrypt",))
            if not rejected.returncode or "trying authenticated TCP fallback" in logs:
                raise RuntimeError(f"Fallback accepted disabled encryption: {logs}")
            accepted, logs = client("quic-available", udp_port, cert, key)
            if (accepted.returncode or not accepted.stdout.strip() or "using native QUIC" not in logs or
                    "trying authenticated TCP fallback" in logs):
                raise RuntimeError(f"Available QUIC endpoint was not used: {logs}")
            rejected, logs = client("bad-quic-key", udp_port, cert, wrong_key)
            if (not rejected.returncode or "using native QUIC" not in logs or
                    "trying authenticated TCP fallback" in logs):
                raise RuntimeError(f"QUIC ACIP identity failure caused fallback: {logs}")
            rejected, logs = client("bad-tls", udp_port, wrong_cert, key)
            if (not rejected.returncode or "trying authenticated TCP fallback" in logs or
                    "QUIC connection establishment failed: Cryptographic error" not in logs):
                raise RuntimeError(f"TLS authentication failure caused fallback: {logs}")
        finally:
            if server.poll() is None:
                server.terminate()
            try:
                server.wait(timeout=10)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=10)
