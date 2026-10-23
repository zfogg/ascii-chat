"""Join a WebRTC-advertised host over authenticated TCP when WebRTC is disabled."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

from pathlib import Path
import re
import sys
import tempfile

from server_lifecycle import Service, free_port, server_options


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="ascii-discovery-tcp-") as temporary:
        directory = Path(temporary)
        for suffix in ["ascii-chat", "AppData/Roaming/ascii-chat", ".config/ascii-chat"]:
            (directory / suffix).mkdir(parents=True, exist_ok=True)
        identity = directory / "acds-test-key"
        # Public RFC 8032 test vector, restricted to the loopback fixture.
        identity.write_bytes(bytes.fromhex(
            "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"
            "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"))
        acds_port, acds_ws, tcp_port, ws_port = [free_port() for _ in range(4)]
        # This route/media fixture authenticates the password. Identity/TOFU
        # policy is exercised separately by the crypto and connection suites.
        environment = {"ASCII_CHAT_QUESTION_PROMPT_RESPONSE": "y;y;y",
                       "ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK": "1"}
        acds = host = client = None
        try:
            acds = Service(binary, directory, "acds", "discovery-service",
                           ["127.0.0.1", "--key", str(identity), "--port", str(acds_port),
                            "--websocket-port", str(acds_ws), "--database", str(directory / "acds.db"),
                            "--status-screen=false"], environment)
            acds.wait_for("Server accepting connections")
            discovery = ["--discovery-service", "127.0.0.1", "--discovery-service-port", str(acds_port)]
            host = Service(binary, directory, "host", "server",
                           server_options(tcp_port, ws_port) + discovery +
                           ["--discovery", "--webrtc", "--password", "route-test-password"], environment)
            host.wait_for("Session created:")
            session = re.search(r"Session created: ([a-z]+-[a-z]+-[a-z]+)", host.text())[1]
            client = Service(binary, directory, "client", session,
                             discovery + ["--no-webrtc", "--password", "route-test-password", "--test-pattern",
                                          "--audio=false", "--width", "40", "--height", "12",
                                          "--color-mode", "none", "--splash-screen=false"], environment)
            client.wait_for("Discovery connection route: direct TCP (--no-webrtc)")
            host.wait_for("state=changing")
            client.wait_for("inner_type=3000")
            text = client.text()
            assert re.search(r"Added new transport client [^\n]+type=1,", host.text()), host.text()[-4000:]
            assert "Authenticated WebRTC connection adopted" not in host.text(), host.text()[-4000:]
            assert "AddressSanitizer" not in text + host.text() + acds.text()
            print("PASS: discovery honors --no-webrtc and receives authenticated TCP video from a WebRTC-advertised host")
        finally:
            if client:
                client.stop()
            if acds:
                acds.stop()
            if host:
                host.stop()


if __name__ == "__main__":
    main()
