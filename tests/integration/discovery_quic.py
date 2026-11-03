"""Exercise QUIC discovery against a disclosed server endpoint."""
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

from pathlib import Path
import re
import os
import time
import socket
import subprocess
import sys
import tempfile
from server_lifecycle import Service, free_port, server_options


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    binary, fixture = map(lambda p: Path(p).resolve(), sys.argv[1:3])
    with tempfile.TemporaryDirectory(prefix="ascii-discovery-quic-") as temporary:
        directory = Path(temporary)
        for suffix in ["ascii-chat", "AppData/Roaming/ascii-chat", ".config/ascii-chat"]:
            (directory / suffix).mkdir(parents=True, exist_ok=True)
        identity = directory / "acds-test-key"
        identity.write_bytes(bytes.fromhex(
            "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"
            "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"))
        cert, key, other_cert, other_key = (directory / name for name in ("cert.pem", "key.pem", "other.pem", "other.key"))
        subprocess.run([str(fixture), str(cert), str(key), str(other_cert), str(other_key)], check=True, timeout=45)
        host_key = directory / "host-id"
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", str(host_key)], check=True, timeout=15)
        acds_port, acds_ws, ws_port = [free_port() for _ in range(3)]
        # The same number must be available for TCP and UDP; the listener binds later.
        for _ in range(100):
            tcp_port = free_port()
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                    probe.bind(("127.0.0.1", tcp_port))
                break
            except OSError:
                continue
        else:
            raise RuntimeError("No shared TCP/UDP port is available")
        environment = {"ASCII_CHAT_QUESTION_PROMPT_RESPONSE": "y;y;y", "ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK": "1"}
        acds = host = None
        clients = []
        try:
            acds = Service(binary, directory, "acds", "discovery-service",
                           ["127.0.0.1", "--key", str(identity), "--port", str(acds_port),
                            "--websocket-port", str(acds_ws), "--database", str(directory / "acds.db"),
                            "--status-screen=false"], environment)
            acds.wait_for("Server accepting connections")
            discovery = ["--discovery-service", "127.0.0.1", "--discovery-service-port", str(acds_port)]
            host = Service(binary, directory, "host", "server",
                           server_options(tcp_port, ws_port) + discovery +
                           ["--discovery", "--webrtc", "--discovery-expose-ip", "--password", "route-test-password",
                            "--key", str(host_key), "--quic-bind-address", "127.0.0.1",
                            "--quic-tls-cert", str(cert), "--quic-tls-key", str(key)], environment)
            host.wait_for("Session created:")
            session = re.search(r"Session created: ([a-z]+-[a-z]+-[a-z]+)", host.text())[1]
            for index in range(2):
                client = Service(binary, directory, f"client-{index}", session,
                                 discovery + ["--discovery-quic", "--quic-ca-file", str(cert),
                                              "--server-key", str(host_key) + ".pub", "--password", "route-test-password",
                                              "--quic-audio-datagrams", "--quic-visual-datagrams", "--test-pattern",
                                              "--audio=false", "--width", "40", "--height", "12",
                                              "--color-mode", "none", "--splash-screen=false"], environment)
                clients.append(client)
                client.wait_for("Discovery connection route: direct QUIC (--discovery-quic)")
                client.wait_for("using native QUIC")
                client.wait_for("Server identity key verified against --server-key")
                client.wait_for("Discovery decoded video: frames=1")
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                quic_ids = set(re.findall(r"Added new transport client (\S+).*type=5,", host.text()))
                video_ids = set(re.findall(r"Server image ingress client=(\S+)", host.text()))
                if len(quic_ids & video_ids) >= 2:
                    break
                time.sleep(0.05)
            else:
                raise AssertionError(f"Both QUIC participants must send media: {host.text()[-8000:]}")
            # A failed QUIC certificate must not trigger the configured TCP fallback.
            negative_log = directory / "wrong-ca.log"
            env = {key: value for key, value in os.environ.items() if not key.startswith("ASCII_CHAT_")}
            env.update(CLAUDECODE="1", HOME=str(directory), USERPROFILE=str(directory), APPDATA=str(directory),
                       XDG_CONFIG_HOME=str(directory), ASCII_CHAT_OTEL_ENABLED_TELEMETRY="none", **environment)
            result = subprocess.run([str(binary), "--no-check-update", "--log-file", str(negative_log),
                                     "--log-level", "debug", session, *discovery, "--discovery-quic",
                                     "--quic-ca-file", str(other_cert), "--quic-fallback-port", str(tcp_port),
                                     "--server-key", str(host_key) + ".pub", "--password", "route-test-password",
                                     "--test-pattern", "--audio=false", "--splash-screen=false"],
                                    cwd=directory, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                    timeout=35, check=False)
            negative = negative_log.read_text(encoding="utf-8", errors="replace") if negative_log.exists() else ""
            if (result.returncode == 0 or "trying authenticated TCP fallback" in negative or
                    "Discovery connection route: direct QUIC" not in negative or
                    "QUIC connection establishment failed" not in negative):
                raise AssertionError(f"Wrong certificate unexpectedly succeeded or downgraded: {negative[-8000:]}")
            assert "AddressSanitizer" not in host.text() + acds.text() + "".join(c.text() for c in clients) + negative
            print("PASS: two discovery participants use authenticated QUIC and receive video")
        finally:
            for client in clients:
                client.stop()
            if host:
                host.stop()
            if acds:
                acds.stop()


if __name__ == "__main__":
    main()
