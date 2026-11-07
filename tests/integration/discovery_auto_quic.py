"""Authenticate QUIC media against a host created by discovery mode itself."""
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

from pathlib import Path
import re
import io
import socket
import subprocess
import sys
import tempfile
from ascii_chat_tests.process import Service, free_port


def main():
    if isinstance(sys.stdout, io.TextIOWrapper):
        sys.stdout.reconfigure(encoding="utf-8")
    binary, fixture = (Path(p).resolve() for p in sys.argv[1:3])
    with tempfile.TemporaryDirectory(prefix="ascii-auto-quic-") as temporary:
        directory = Path(temporary)
        for suffix in ("ascii-chat", "AppData/Roaming/ascii-chat", ".config/ascii-chat"):
            (directory / suffix).mkdir(parents=True, exist_ok=True)
        identity = directory / "identity"
        identity.write_bytes(bytes.fromhex(
            "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"
            "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"))
        cert, key, other_cert, other_key = (directory / n for n in ("cert.pem", "key.pem", "other.pem", "other.key"))
        subprocess.run([str(fixture), str(cert), str(key), str(other_cert), str(other_key)], check=True, timeout=45)
        host_key = directory / "host-id"
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", str(host_key)], check=True, timeout=15)
        ap, wp, hp = free_port(), free_port(), free_port()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(("127.0.0.1", 0))
            qp = probe.getsockname()[1]
        while hp == qp:
            hp = free_port()
        environment = {"ASCII_CHAT_DISABLE_FIRST_USE": "1", "ASCII_CHAT_QUESTION_PROMPT_RESPONSE": "y;y;y",
                       "ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK": "1"}
        services = []
        try:
            acds = Service(binary, directory, "acds", "discovery-service",
                           ["127.0.0.1", "--key", str(identity), "--port", str(ap), "--websocket-port", str(wp),
                            "--database", str(directory / "acds.db"), "--status-screen=false"], environment)
            services.append(acds)
            acds.wait_for("Server accepting connections")
            common = ["--discovery-service", "127.0.0.1", "--discovery-service-port", str(ap),
                      "--width", "40", "--height", "12", "--color-mode", "none", "--splash-screen=false",
                      "--audio=false", "--webrtc-skip-stun", "--webrtc-disable-turn", "--no-webrtc",
                      "--password", "automatic-host-password"]
            host = Service(binary, directory, "host", "--test-pattern", common +
                           ["--port", str(hp), "--key", str(host_key), "--discovery-expose-ip",
                            "--quic-bind-address", "127.0.0.1", "--quic-port", str(qp),
                            "--quic-tls-cert", str(cert), "--quic-tls-key", str(key)], environment)
            services.append(host)
            host.wait_for("Session created:", timeout=30)
            match = re.search(r"Session created: ([a-z]+-[a-z]+-[a-z]+)", host.text())
            assert match, host.text()[-8000:]
            name = match[1]
            client = Service(binary, directory, "client", name, common +
                             ["--test-pattern", "--discovery-quic", "--quic-ca-file", str(cert),
                              "--server-key", str(host_key) + ".pub"], environment)
            services.append(client)
            host.wait_for(f"Native QUIC listener initialized on 127.0.0.1 UDP port {qp}", timeout=40)
            client.wait_for("using native QUIC", timeout=40)
            client.wait_for("Server identity key verified against --server-key", timeout=30)
            client.wait_for("Discovery decoded video: frames=1", timeout=30)
            assert "trying authenticated TCP fallback" not in client.text()
            assert "AddressSanitizer" not in "".join(s.text() for s in services)
            print("PASS: discovery-created host advertises a distinct QUIC port and exchanges identity-pinned encrypted video")
        except BaseException:
            for service in services:
                print(service.text()[-8000:])
            raise
        finally:
            errors = []
            for service in reversed(services):
                try:
                    service.stop()
                except Exception as error:
                    errors.append(error)
            if errors:
                raise errors[0]


if __name__ == "__main__":
    main()
