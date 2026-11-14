"""The first remote frame must honor glyph settings without a later settings update."""

import pytest

from ascii_chat_tests.cli import clean_environment, run_cli
from ascii_chat_tests.process import free_port, running, server_options

pytestmark = [pytest.mark.integration, pytest.mark.network]


@pytest.mark.parametrize("transport", ["tcp", "websocket"])
def test_first_snapshot_uses_glyph_palette(binary, tmp_path, transport):
    tcp_port, ws_port = free_port(), free_port()
    destination = f"127.0.0.1:{tcp_port}" if transport == "tcp" else f"ws://127.0.0.1:{ws_port}"
    env = clean_environment()
    env.update(CLAUDECODE="1", HOME=str(tmp_path), USERPROFILE=str(tmp_path),
               APPDATA=str(tmp_path), XDG_CONFIG_HOME=str(tmp_path))
    with running(binary, tmp_path, f"server-{transport}", "server", server_options(tcp_port, ws_port)) as server:
        server.wait_for("Server accepting connections")
        for attempt in range(3):
            palette_args = ["--palette-chars", "@#"] if attempt == 2 else []
            result = run_cli(str(binary), "--quiet", "--log-file", str(tmp_path / f"client-{attempt}.log"),
                             "client", destination, "--test-pattern", "--video-codec", "raw", "--audio=false",
                             "--snapshot", "--snapshot-delay", "0", "--width", "40", "--height", "12",
                             "--color-mode", "none", "--strip-ansi", "--splash-screen=false",
                             "--renderer", "glyph", *palette_args, cwd=tmp_path, env=env,
                             capture_output=True, timeout=30)
            assert result.returncode == 0, result.stderr.decode(errors="replace")
            rows = result.stdout.splitlines()
            assert len(rows) == 12 and all(len(row) == 40 for row in rows), rows
            characters = set(b"".join(rows))
            if palette_args:
                assert characters <= {35, 64}, characters
            else:
                assert characters - set(b"   ...',;:clodxkO0KXNWM"), characters
