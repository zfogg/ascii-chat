"""Exercise server startup, discovery fallbacks, media clients, and graceful cleanup.

Runs against a built binary, using only loopback services and temporary files.
On Windows each service gets a hidden console so Ctrl+C reaches its real handler.
"""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import contextlib
import json
import os
from pathlib import Path
import signal
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
import urllib.request


from ascii_chat_tests.process import Service, check_client, free_port, interrupt, running, server_options


def check_server(binary, directory, name, extra, expected, clients=False, before_stop=None):
    tcp_port, ws_port = free_port(), free_port()
    with running(binary, directory, name, "server", server_options(tcp_port, ws_port) + extra) as service:
        service.wait_for("Server accepting connections")
        for message in expected:
            service.wait_for(message)
        with urllib.request.urlopen(f"http://127.0.0.1:{ws_port}/health", timeout=3) as response:
            if json.load(response)["status"] != "ok":
                raise AssertionError("WebSocket health failed")
        if clients:
            check_client(binary, directory, f"127.0.0.1:{tcp_port}", "tcp-pattern")
            check_client(binary, directory, f"ws://127.0.0.1:{ws_port}", "ws-pattern")
        if before_stop:
            before_stop()
    if "Server shutdown complete" not in service.text():
        raise AssertionError("Server cleanup callback did not complete")
    print("PASS:", name, flush=True)


def main():
    binary = Path(sys.argv[1]).resolve()
    output = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None
    if output:
        output.mkdir(parents=True, exist_ok=True)
    with contextlib.nullcontext(str(output)) if output else tempfile.TemporaryDirectory(prefix="ascii-server-lifecycle-") as tmp:
        directory = Path(tmp)
        # Public Ed25519 test vector; never use this key outside the fixture.
        identity = directory / "acds-test-key"
        identity.write_bytes(bytes.fromhex(
            "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"
            "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"
        ))
        check_server(binary, directory, "basic", [], ["ACDS registration disabled"], clients=True)
        discovery = ["--discovery", "--discovery-service", "127.0.0.1", "--discovery-service-port", str(free_port())]
        check_server(binary, directory, "privacy-required", discovery, ["Cannot create ACDS session"])
        check_server(binary, directory, "discovery-unavailable", discovery + ["--password", "test-password"],
                     ["Failed to connect to ACDS server"])
        for name, auth in [("discovery-password", ["--password", "test-password"]),
                           ("discovery-public", ["--discovery-expose-ip", "--webrtc",
                                                  "--turn-servers", "turn:127.0.0.1:9",
                                                  "--turn-username", "test", "--turn-credential", "test"])]:
            acds_port, acds_ws = free_port(), free_port()
            with running(binary, directory, name + "-acds", "discovery-service",
                         ["--key", str(identity), "--port", str(acds_port), "--websocket-port", str(acds_ws),
                          "--database", str(directory / (name + ".db")), "--status-screen=false"]) as acds:
                acds.wait_for("Server accepting connections")
                discovery = ["--discovery", "--discovery-service", "127.0.0.1", "--discovery-service-port", str(acds_port),
                             "--stun-servers", "stun:127.0.0.1:9"]
                def verify_and_stop_acds():
                    with contextlib.closing(sqlite3.connect(directory / (name + ".db"))) as db:
                        rows = db.execute("SELECT has_password, expose_ip_publicly FROM sessions").fetchall()
                        expected = [(1, 0)] if name == "discovery-password" else [(0, 1)]
                        if rows != expected:
                            raise AssertionError(f"Incorrect discovery privacy policy: {rows}")
                        participants = db.execute("SELECT COUNT(*) FROM participants").fetchone()[0]
                        # Session creation precedes the asynchronous host join.
                        deadline = time.monotonic() + 20
                        while participants == 0 and time.monotonic() < deadline:
                            if acds.process.poll() is not None:
                                raise AssertionError("Discovery service exited before the host joined")
                            time.sleep(0.05)
                            participants = db.execute("SELECT COUNT(*) FROM participants").fetchone()[0]
                        if participants != 1:
                            raise AssertionError(f"Server did not join its session: {participants}")
                    acds.stop()

                # The existing receive-thread join needs the signaling peer to
                # close first. Keep this regression probe scoped to extraction.
                check_server(binary, directory, name, discovery + auth,
                             ["Session created:", "ACDS session type: WebRTC"],
                             before_stop=verify_and_stop_acds)



if __name__ == "__main__":
    main()
