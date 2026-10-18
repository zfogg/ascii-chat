"""Real CLI security negotiation, authentication failures, and rendered frames.

Run: python tests/integration/crypto_policy.py /absolute/path/to/ascii-chat
Uses temporary identities and loopback TCP/WS/WSS listeners only.
"""
import os
from contextlib import contextmanager
from pathlib import Path
import subprocess
import sys
import tempfile

from server_lifecycle import Service, free_port, server_options


@contextmanager
def running(*args):
    service = Service(*args)
    try:
        yield service
    finally:
        # Security tests own isolated processes; graceful shutdown is covered by server_lifecycle.
        if service.process.poll() is None:
            service.process.terminate()
        service.process.wait(timeout=20)
        service.stream.close()


def run_client(binary, directory, name, address, options, mode, success=True):
    env = {k: v for k, v in os.environ.items() if not k.startswith("ASCII_CHAT_")}
    env.update(CLAUDECODE="1", HOME=str(directory), USERPROFILE=str(directory),
               APPDATA=str(directory), XDG_CONFIG_HOME=str(directory),
               ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK="1")
    log = directory / (name + "-client.log")
    args = [str(binary), "--log-file", str(log), "--log-level", "debug", "client", address,
            "--test-pattern", "--video-codec", "raw", "--audio=false", "--snapshot",
            "--snapshot-delay", "0", "--width", "40", "--height", "12",
            "--color-mode", "none", "--splash-screen=false", *options]
    result = subprocess.run(args, cwd=directory, env=env, capture_output=True, timeout=35)
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    if success:
        assert result.returncode == 0, (name, result.returncode, "\n".join(line for line in text.splitlines() if "[ERROR" in line or "[WARN" in line)[-6000:])
        assert len(result.stdout.strip()) > 100, (name, "No rendered frame", text[-6000:])
        assert f"crypto mode: 0x{mode:02x}" in text, (name, "Wrong negotiated mode", text[-6000:])
    else:
        assert result.returncode != 0, (name, "Rejected credentials connected")
        assert "Crypto handshake completed successfully" not in text, (name, text[-6000:])
    print(f"PASS {name}: mode={mode}, accepted={success}", flush=True)


def main():
    binary = Path(sys.argv[1]).resolve()
    temp = tempfile.mkdtemp(prefix="ascii-crypto-policy-")
    print(f"Test logs: {temp}", flush=True)
    directory = Path(temp)
    for identity in ("server", "client", "wrong"):
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", str(directory / identity)],
                       check=True, capture_output=True)
    certificate, tls_key = directory / "tls.pem", directory / "tls.key"
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                    "-subj", "/CN=localhost", "-keyout", str(tls_key), "-out", str(certificate)],
                   check=True, capture_output=True)
    server_key = ["--key", str(directory / "server")]
    client_key = ["--key", str(directory / "client"), "--server-key", str(directory / "server.pub")]
    whitelist = ["--client-keys", str(directory / "client.pub")]
    cases = [
        ("tcp-default", "tcp", [], [], 1, True),
        ("tcp-none", "tcp", ["--no-encrypt", "--no-auth"], ["--no-encrypt", "--no-auth"], 0, True),
        ("ws-default", "ws", [], [], 1, True),
        ("wss-default", "wss", [], [], 0, True),
        ("wss-explicit", "wss", [], ["--encrypt"], 1, True),
        ("wss-password", "wss", ["--password", "test-password"], ["--password", "test-password"], 3, True),
        ("wss-password-auth-only", "wss", ["--password", "test-password"], ["--password", "test-password", "--no-encrypt"], 2, True),
        ("tcp-auth-only", "tcp", server_key + whitelist + ["--no-encrypt"], client_key + ["--no-encrypt"], 2, True),
        ("ws-auth-only", "ws", server_key + whitelist + ["--no-encrypt"], client_key + ["--no-encrypt"], 2, True),
        ("wss-auth-only", "wss", server_key + whitelist, client_key + ["--no-encrypt"], 2, True),
        ("wss-key-default", "wss", server_key + whitelist, client_key, 2, True),
        ("wss-wrong-key", "wss", server_key + whitelist,
         ["--key", str(directory / "wrong"), "--server-key", str(directory / "server.pub"), "--no-encrypt"], 2, False),
        ("wss-wrong-pin", "wss", server_key,
         ["--server-key", str(directory / "wrong.pub"), "--no-encrypt"], 2, False),
        ("wss-wrong-password", "wss", ["--password", "test-password"], ["--password", "wrong-password"], 3, False),
        ("wss-missing-password", "wss", ["--password", "test-password"], [], 0, False),
    ]
    for name, scheme, server_extra, client_extra, mode, success in cases:
        tcp, ws = free_port(), free_port()
        tls = ["--websocket-tls-cert", str(certificate), "--websocket-tls-key", str(tls_key)] if scheme == "wss" else []
        with running(binary, directory, name, "server", server_options(tcp, ws) + server_extra + tls) as service:
            service.wait_for("Server accepting connections", timeout=40)
            address = f"127.0.0.1:{tcp}" if scheme == "tcp" else f"{scheme}://127.0.0.1:{ws}"
            run_client(binary, directory, name, address, client_extra, mode, success)


if __name__ == "__main__":
    main()
