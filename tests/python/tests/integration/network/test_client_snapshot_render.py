"""Client snapshot rendering over a local server."""

import pytest
from ascii_chat_tests.process import Service, free_port, server_options


pytestmark = [pytest.mark.integration, pytest.mark.native, pytest.mark.network]


def test_client_snapshot_renders_in_terminal(ensure_can_render, binary, tmp_path):
    port, websocket_port = free_port(), free_port()
    server = Service(binary, tmp_path, "render-server", "server", server_options(port, websocket_port))
    try:
        server.wait_for("Server accepting connections")
        ensure_can_render("client", "standard", "0", "127.0.0.1:" + str(port))
    finally:
        server.wait_for("Client removed:", timeout=10)
        server.stop()
