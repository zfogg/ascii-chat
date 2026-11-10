"""Regression coverage for discovery-owned session creation and rendering."""

import re
import time

import pytest
from ascii_chat_tests.patterns import require_match
from ascii_chat_tests.process import Service, free_port


pytestmark = [pytest.mark.integration, pytest.mark.network]


def _rendered_rows(service, width=40):
    text = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", service.output.read_text(errors="replace"))
    return [row for row in text.splitlines() if len(row) == width and row.strip()]


def _wait_for_rendered_rows(service, minimum=12, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if len(_rendered_rows(service)) >= minimum:
            return
        if service.process.poll() is not None:
            raise AssertionError(f"{service.log.name} exited before rendering ASCII art")
        time.sleep(0.05)
    raise AssertionError(f"{service.log.name} did not render {minimum} ASCII rows")


def test_discovery_session_is_owned_by_discovery_mode(binary, tmp_path):
    identity = tmp_path / "identity"
    identity.write_bytes(bytes.fromhex(
        "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"
        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"
    ))
    services = []
    try:
        acds_port, websocket_port, host_port = (free_port(), free_port(), free_port())
        env = {
            "ASCII_CHAT_QUESTION_PROMPT_RESPONSE": "y;y;y",
            "ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK": "1",
        }
        acds = Service(binary, tmp_path, "acds", "discovery-service", [
            "127.0.0.1", "--key", str(identity), "--port", str(acds_port),
            "--websocket-port", str(websocket_port), "--database", str(tmp_path / "acds.db"),
            "--status-screen=false",
        ], env)
        services.append(acds)
        acds.wait_for("Server accepting connections")

        common = [
            "--discovery-service", "127.0.0.1", "--discovery-service-port", str(acds_port),
            "--audio=false", "--width", "40", "--height", "12", "--color-mode", "none",
            "--splash-screen=false", "--webrtc-skip-stun", "--webrtc-disable-turn", "--fps", "60",
        ]
        # No explicit mode means discovery mode. It creates the share code.
        host = Service(binary, tmp_path, "host", "--test-pattern", common + ["--port", str(host_port)], env)
        services.append(host)
        host.wait_for("Session created:", timeout=25)
        host.wait_for("Using user-specified FPS: 60")
        session = require_match(r"Session created: ([a-z]+-[a-z]+-[a-z]+)", host.text())[1]

        client = Service(binary, tmp_path, "client", session, common + ["--test-pattern"], env)
        services.append(client)
        client.wait_for("WebRTC Media Channel ACTIVE", timeout=35)
        client.wait_for("Using user-specified FPS: 60")
        host.wait_for("Frame received from client", timeout=20)

        # Both discovery peers must render, not merely complete the handshake.
        _wait_for_rendered_rows(host)
        _wait_for_rendered_rows(client)

        # The synthetic source is explicitly requested at 60 FPS. Count
        # complete terminal frames on both peers over a five-second window;
        # Captured Windows terminal output adds overhead, so use a conservative
        # sustained floor while rejecting a stalled/very slow stream.
        host_before = len(_rendered_rows(host)) // 12
        client_before = len(_rendered_rows(client)) // 12
        time.sleep(5)
        host_fps = (len(_rendered_rows(host)) // 12 - host_before) / 5
        client_fps = (len(_rendered_rows(client)) // 12 - client_before) / 5
        assert host_fps >= 25, f"discovery host rendered only {host_fps:.1f} FPS"
        assert client_fps >= 25, f"discovery client rendered only {client_fps:.1f} FPS"
    finally:
        failures = []
        for service in reversed(services):
            try:
                service.stop()
            except Exception as error:
                failures.append(error)
        if failures:
            raise AssertionError("Discovery cleanup failed: " + "; ".join(map(str, failures)))


def test_server_mode_does_not_create_or_announce_session(binary, tmp_path):
    service = Service(binary, tmp_path, "server", "server", [
        "127.0.0.1", "--port", str(free_port()), "--status-screen=false",
    ])
    try:
        service.wait_for("Server accepting connections")
        time.sleep(2)
        output = service.text()
        assert "SESSION READY" not in output
        assert "Session String:" not in output
        assert "Session created:" not in output
    finally:
        service.stop()

