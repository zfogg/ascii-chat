"""Exercise CLI discovery, per-peer security, and WebRTC media on loopback."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
from crypto_policy import running
from server_lifecycle import free_port, server_options

binary = Path(sys.argv[1]).resolve()
directory = Path(tempfile.mkdtemp(prefix="ascii-webrtc-cli-"))
print(f"Test logs: {directory}", flush=True)
identity = directory / "acds-key"
for key in (identity, directory / "host-key", directory / "client-key"):
    subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", str(key)], check=True)
require_identity = "--require-identity" in sys.argv[2:]
port = free_port()
with running(binary, directory, "acds", "discovery-service", ["--key", str(identity), "--port", str(port), "--websocket-port", str(free_port()), "--database", str(directory / "acds.db"), "--status-screen=false"] + (["--require-client-identity", "--require-server-identity"] if require_identity else [])) as acds:
    acds.wait_for("Server accepting connections", 40)
    discovery = ["--discovery-service", "127.0.0.1", "--discovery-service-port", str(port), "--stun-servers", "stun:127.0.0.1:9", "--turn-servers", "turn:127.0.0.1:9", "--turn-username", "test", "--turn-credential", "test"]
    password = ["--password", "peer-test-password"]
    host_keys = ["--key", str(directory / "host-key"), "--client-keys", str(directory / "client-key.pub")]
    client_keys = ["--key", str(directory / "client-key"), "--server-key", str(directory / "host-key.pub")]
    cases = [
        ("default", [], []),
        ("encrypt", ["--encrypt"], ["--encrypt"]),
        ("password", password, password),
        ("auth-only", password, password + ["--no-encrypt"]),
        ("keys-auth-only", host_keys, client_keys),
        ("keys-encrypted", host_keys + ["--encrypt"], client_keys + ["--encrypt"]),
    ]
    for name, host_extra, client_extra in cases:
        if require_identity and not name.startswith("keys-"):
            continue
        with running(binary, directory, name + "-host", "server", server_options(free_port(), free_port()) + ["--discovery", "--webrtc", "--discovery-expose-ip"] + discovery + host_extra) as host:
            host.wait_for("Session created:", 40)
            session = re.search(r"Session created: ([a-z0-9-]+)", host.text()).group(1)
            with running(binary, directory, name + "-client", session, discovery + client_extra + ["--prefer-webrtc", "--test-pattern", "--audio=false", "--splash-screen=false", "--width", "40", "--height", "12"]) as client:
                client.wait_for("WebRTC Media Channel ACTIVE", 45)
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    rates = re.findall(r"Discovery ASCII receive/render rate: received=([\d.]+) fps rendered=([\d.]+)", client.text())
                    if any(float(received) > 0 and float(rendered) > 0 for received, rendered in rates):
                        break
                    time.sleep(0.1)
                else:
                    raise AssertionError(f"No rendered WebRTC media: {client.text()[-6000:]}")
                assert "actual_type=6005" in client.text(), "Discovery JOIN response was not decrypted"
                assert "actual_type=6001" in host.text(), "Host discovery CREATE response was not decrypted"
                print(f"PASS WebRTC CLI {name}: negotiated media, receive/render FPS {rates[-1]}", flush=True)
