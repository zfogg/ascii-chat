"""Loopback server startup, discovery policy, media clients, and cleanup."""
import contextlib
import json
import sqlite3
import time
import urllib.request

import pytest
from ascii_chat_tests.process import check_client, free_port, server_options

pytestmark = [pytest.mark.integration, pytest.mark.network]


def check_server(binary, directory, service_factory, name, extra, expected, clients=False, before_stop=None):
    tcp_port, ws_port = free_port(), free_port()
    with service_factory(name, "server", server_options(tcp_port, ws_port) + extra) as service:
        service.wait_for("Server accepting connections")
        for message in expected:
            service.wait_for(message)
        with urllib.request.urlopen(f"http://127.0.0.1:{ws_port}/health", timeout=3) as response:
            assert json.load(response)["status"] == "ok"
        if clients:
            check_client(binary, directory, f"127.0.0.1:{tcp_port}", "tcp-pattern")
            check_client(binary, directory, f"ws://127.0.0.1:{ws_port}", "ws-pattern")
        if before_stop:
            before_stop()
    assert "Server shutdown complete" in service.text()


def test_basic_server_and_media_clients(binary, tmp_path, service_factory):
    check_server(binary, tmp_path, service_factory, "basic", [], ["ACDS registration disabled"], clients=True)


@pytest.mark.parametrize("password,expected", [(False, "Cannot create ACDS session"),
                                               (True, "Failed to connect to ACDS server")])
def test_discovery_unavailable(binary, tmp_path, service_factory, password, expected):
    discovery = ["--discovery", "--discovery-service", "127.0.0.1", "--discovery-service-port", str(free_port())]
    if password:
        discovery += ["--password", "test-password"]
    check_server(binary, tmp_path, service_factory, "unavailable", discovery, [expected])


@pytest.mark.parametrize("public", [False, True])
def test_discovery_privacy_and_host_join(binary, tmp_path, service_factory, public):
    # Public Ed25519 test vector, used only by this local fixture.
    identity = tmp_path / "acds-test-key"
    identity.write_bytes(bytes.fromhex(
        "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"
        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"))
    acds_port, acds_ws = free_port(), free_port()
    with service_factory("acds", "discovery-service", ["--key", str(identity), "--port", str(acds_port),
                         "--websocket-port", str(acds_ws), "--database", str(tmp_path / "acds.db"),
                         "--status-screen=false"]) as acds:
        acds.wait_for("Server accepting connections")
        discovery = ["--discovery", "--discovery-service", "127.0.0.1", "--discovery-service-port", str(acds_port),
                     "--stun-servers", "stun:127.0.0.1:9"]
        auth = (["--discovery-expose-ip", "--webrtc", "--turn-servers", "turn:127.0.0.1:9",
                 "--turn-username", "test", "--turn-credential", "test"] if public else ["--password", "test-password"])

        def verify_and_stop_acds():
            with contextlib.closing(sqlite3.connect(tmp_path / "acds.db")) as db:
                rows = db.execute("SELECT has_password, expose_ip_publicly FROM sessions").fetchall()
                assert rows == ([(0, 1)] if public else [(1, 0)])
                deadline = time.monotonic() + 20
                participants = db.execute("SELECT COUNT(*) FROM participants").fetchone()[0]
                while participants == 0 and time.monotonic() < deadline:
                    assert acds.process.poll() is None, "Discovery service exited before the host joined"
                    time.sleep(0.05)
                    participants = db.execute("SELECT COUNT(*) FROM participants").fetchone()[0]
                assert participants == 1
            # The receive thread needs the signaling peer closed before it joins.
            acds.stop()

        check_server(binary, tmp_path, service_factory, "server", discovery + auth,
                     ["Session created:", "ACDS session type: WebRTC"], before_stop=verify_and_stop_acds)
