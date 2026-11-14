"""Discovery session snapshot rendering."""

import pytest
from ascii_chat_tests.patterns import require_match
from ascii_chat_tests.process import Service, free_port


pytestmark = [pytest.mark.integration, pytest.mark.native, pytest.mark.network]


def test_discovery_snapshot_renders_in_terminal(ensure_can_render, binary, tmp_path):
    acds_port, host_port = free_port(), free_port()
    identity = tmp_path / "acds-key"
    identity.write_bytes(bytes.fromhex(
        "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"
        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"
    ))
    environment = {"ASCII_CHAT_QUESTION_PROMPT_RESPONSE": "y;y;y",
                   "ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK": "1"}
    acds = Service(binary, tmp_path, "render-acds", "discovery-service", [
        "127.0.0.1", "--key", str(identity), "--port", str(acds_port),
        "--database", str(tmp_path / "acds.db"), "--status-screen=false",
    ], environment)
    host = None
    try:
        acds.wait_for("Server accepting connections")
        common = ["--discovery-service", "127.0.0.1", "--discovery-service-port", str(acds_port)]
        host = Service(binary, tmp_path, "render-discovery-host", "--test-pattern",
                       common + ["--port", str(host_port)], environment)
        host.wait_for("Session created:")
        session = require_match(r"Session created: ([a-z]+-[a-z]+-[a-z]+)", host.text())[1]
        ensure_can_render(None, "standard", "0", session, *common)
    finally:
        if host:
            host.wait_for("Client removed:", timeout=10)
            host.stop()
        acds.stop()
