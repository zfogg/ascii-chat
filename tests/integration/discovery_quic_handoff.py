"""Recover a QUIC call onto a prepared successor with a distinct endpoint and identity."""
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

from pathlib import Path
import re
from contextlib import closing
import socket
import sqlite3
import time
import subprocess
import sys
import tempfile
from server_lifecycle import Service, free_port


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    binary, fixture = (Path(p).resolve() for p in sys.argv[1:3])
    with tempfile.TemporaryDirectory(prefix="ascii-quic-handoff-") as temporary:
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
        successor_key = directory / "successor-id"
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", str(successor_key)], check=True, timeout=15)
        trusted_keys = directory / "trusted.pub"
        trusted_keys.write_text(Path(str(host_key) + ".pub").read_text() + Path(str(successor_key) + ".pub").read_text())
        ap, wp, hp, successor_tcp = (free_port() for _ in range(4))
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(("127.0.0.1", 0))
            qp = probe.getsockname()[1]
        while hp == qp:
            hp = free_port()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(("127.0.0.1", 0))
            successor_udp = probe.getsockname()[1]
        while successor_tcp in (hp, successor_udp):
            successor_tcp = free_port()
        environment = {"ASCII_CHAT_DISABLE_FIRST_USE": "1", "ASCII_CHAT_QUESTION_PROMPT_RESPONSE": "y;y;y",
                       "ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK": "1"}
        services = []
        expected_failure = None
        try:
            acds = Service(binary, directory, "acds", "discovery-service",
                           ["127.0.0.1", "--key", str(identity), "--port", str(ap), "--websocket-port", str(wp),
                            "--database", str(directory / "acds.db"), "--status-screen=false"], environment)
            services.append(acds)
            acds.wait_for("Server accepting connections")
            common = ["--discovery-service", "127.0.0.1", "--discovery-service-port", str(ap),
                      "--width", "40", "--height", "12", "--color-mode", "none", "--splash-screen=false",
                      "--fps", "5", "--audio=false", "--webrtc-skip-stun", "--webrtc-disable-turn", "--no-webrtc",
                      "--password", "automatic-host-password"]
            host = Service(binary, directory, "host", "--test-pattern", common +
                           ["--port", str(hp), "--key", str(host_key), "--discovery-expose-ip",
                            "--quic-bind-address", "127.0.0.1", "--quic-port", str(qp),
                            "--quic-tls-cert", str(cert), "--quic-tls-key", str(key)], environment)
            services.append(host)
            host.wait_for("Session created:", timeout=30)
            name = re.search(r"Session created: ([a-z]+-[a-z]+-[a-z]+)", host.text())[1]
            route = ["--test-pattern", "--discovery-quic", "--quic-ca-file", str(cert),
                     "--server-key", str(trusted_keys)]
            successor = Service(binary, directory, "successor", name, common + route +
                                ["--port", str(successor_tcp), "--key", str(successor_key), "--discovery-expose-ip",
                                 "--quic-bind-address", "127.0.0.1", "--quic-port", str(successor_udp),
                                 "--quic-tls-cert", str(cert), "--quic-tls-key", str(key)], environment)
            services.append(successor)
            successor.wait_for("Discovery decoded video: frames=1", timeout=35)
            observer = Service(binary, directory, "observer", name, common + route, environment)
            services.append(observer)
            observer.wait_for("Discovery decoded video: frames=1", timeout=35)
            restricted = Service(binary, directory, "restricted", name, common +
                                 ["--test-pattern", "--discovery-quic", "--quic-ca-file", str(cert),
                                  "--server-key", str(host_key) + ".pub", "--quic-fallback-port", str(successor_tcp)], environment)
            services.append(restricted)
            restricted.wait_for("Discovery decoded video: frames=1", timeout=35)
            successor.wait_for(f"Native QUIC listener initialized on 127.0.0.1 UDP port {successor_udp}", timeout=35)
            acds.wait_for("Handoff action=5", timeout=20)
            first_pid = observer.process.pid
            host.stop()
            successor.wait_for("Hosting session - capturing and broadcasting", timeout=35)
            observer.wait_for(f":{successor_udp} using native QUIC", timeout=40)
            deadline = time.monotonic() + 25
            while time.monotonic() < deadline:
                if observer.process.poll() is not None:
                    raise AssertionError(observer.text()[-8000:])
                if (observer.text().count("Discovery decoded video: frames=1") >= 2 and
                        observer.text().count("Server identity key verified against --server-key") >= 2):
                    break
                time.sleep(0.05)
            else:
                raise AssertionError("The retained participant did not authenticate and decode after host handoff")
            assert observer.process.pid == first_pid
            code = restricted.process.wait(timeout=40)
            assert code != 0 and "Unknown pinned server identity" in successor.text(), restricted.text()[-8000:]
            assert "trying authenticated TCP fallback" not in restricted.text()
            expected_failure = restricted
            with closing(sqlite3.connect(directory / "acds.db")) as db:
                row = db.execute("SELECT s.host_port, s.host_participant_id, s.initiator_id, h.host_id, t.metadata "
                                 "FROM sessions s JOIN session_transports t USING(session_id) "
                                 "JOIN session_transport_hosts h USING(session_id) WHERE s.session_string = ?",
                                 (name,)).fetchone()
            assert row and row[0] == successor_tcp and row[1] == row[3] and row[1] != row[2], row
            assert int.from_bytes(row[4][6:8], "big") == successor_udp, row
            late = Service(binary, directory, "late", name, common + route, environment)
            services.append(late)
            late.wait_for(f":{successor_udp} using native QUIC", timeout=35)
            late.wait_for("Server identity key verified against --server-key", timeout=25)
            late.wait_for("Discovery decoded video: frames=1", timeout=25)
            assert "trying authenticated TCP fallback" not in observer.text() + late.text()
            assert "AddressSanitizer" not in "".join(s.text() for s in services)
            print("PASS: retained and late QUIC participants authenticate the prepared successor; an old-key-only peer fails without downgrade")
        except BaseException:
            for service in services:
                print(service.text()[-8000:])
                if len(sys.argv) > 3:
                    evidence = Path(sys.argv[3]).resolve()
                    evidence.mkdir(parents=True, exist_ok=True)
                    (evidence / service.log.name).write_text(service.text(), encoding="utf-8")
            raise
        finally:
            errors = []
            for service in reversed(services):
                try:
                    if service is expected_failure:
                        service.stream.close()
                    else:
                        service.stop()
                except Exception as error:
                    errors.append(error)
            if len(sys.argv) > 3:
                evidence = Path(sys.argv[3]).resolve()
                evidence.mkdir(parents=True, exist_ok=True)
                for service in services:
                    (evidence / service.log.name).write_text(service.text(), encoding="utf-8")
            if errors:
                raise errors[0]


if __name__ == "__main__":
    main()
