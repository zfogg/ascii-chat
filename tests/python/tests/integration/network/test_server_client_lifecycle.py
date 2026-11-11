"""Client admission, failed handshakes, and slot reuse."""
import socket
import time
import re

import pytest
from ascii_chat_tests.cli import clean_environment, run_cli
from ascii_chat_tests.process import Service, check_client, free_port, running, server_options

pytestmark = [pytest.mark.integration, pytest.mark.network]


def wait_new_log(service, message, previous, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if service.text().count(message) > previous:
            return
        if service.process.poll() is not None:
            raise AssertionError(f"Server exited: {service.process.returncode}")
        time.sleep(0.05)
    raise AssertionError(f"Missing new {message!r}: {service.text()[-2000:]}")


def reject_client(binary, directory, destination, name):
    env = clean_environment()
    env.update(CLAUDECODE="1", HOME=str(directory), USERPROFILE=str(directory),
               APPDATA=str(directory), XDG_CONFIG_HOME=str(directory))
    result = run_cli(str(binary), "--log-file", str(directory / (name + ".log")), "client", destination,
         "--test-pattern", "--audio=false", "--snapshot", "--snapshot-delay", "0",
         "--splash-screen=false", "--reconnect-attempts", "0",
        cwd=directory, env=env, capture_output=True, timeout=20,
    )
    if result.returncode == 0:
        raise AssertionError("Server admitted a client beyond --max-clients=1")


def check_capacity(binary, directory, server, destination, name):
    before = server.text().count("announced video stream")
    options = [destination, "--test-pattern", "--video-codec", "raw", "--audio=false",
               "--snapshot", "--snapshot-delay", "60", "--fps", "10", "--width", "40",
               "--height", "12", "--splash-screen=false", "--reconnect-attempts", "0"]
    with running(binary, directory, name + "-holder", "client", options) as holder:
        wait_new_log(server, "announced video stream", before)
        rejected = server.text().count("Maximum client limit reached")
        reject_client(binary, directory, destination, name + "-rejected")
        wait_new_log(server, "Maximum client limit reached", rejected)
        if holder.process.poll() is not None:
            raise AssertionError("Existing client disconnected during capacity rejection")
    # A fresh snapshot must succeed after the old client's cleanup, repeatedly.
    for attempt in range(3):
        check_client(binary, directory, destination, f"{name}-reuse-{attempt}")
    print("PASS:", name, "capacity rejection and three reconnects", flush=True)


def test_server_client_lifecycle(binary, tmp_path):
    directory = tmp_path
    tcp_port, ws_port = free_port(), free_port()
    with running(binary, directory, "server", "server",
                 server_options(tcp_port, ws_port) + ["--max-clients", "1"]) as server:
        server.wait_for("Server accepting connections")
        for attempt in range(3):
            with socket.create_connection(("127.0.0.1", tcp_port), timeout=3) as broken:
                # Disconnect during the crypto handshake, before worker startup.
                broken.sendall(b"\x00")
                broken.shutdown(socket.SHUT_RDWR)
            # The event backend does not allocate a client slot for an
            # incomplete header. A following snapshot proves admission works.
            check_client(binary, directory, f"127.0.0.1:{tcp_port}", f"after-failure-{attempt}")
        print("PASS: three aborted handshakes followed by successful snapshots", flush=True)
        check_capacity(binary, directory, server, f"127.0.0.1:{tcp_port}", "tcp")
        for attempt in range(3):
            check_client(binary, directory, f"ws://127.0.0.1:{ws_port}", f"websocket-reuse-{attempt}")
        print("PASS: three WebSocket reconnects", flush=True)
    if "Server shutdown complete" not in server.text():
        raise AssertionError("Server cleanup did not finish")


def test_default_client_server_authentication(binary, tmp_path):
    """Default peers must complete the shared, unauthenticated ACIP handshake."""
    tcp_port, ws_port = free_port(), free_port()
    log = tmp_path / "default-auth-client.log"
    with running(binary, tmp_path, "default-auth-server", "server",
                 server_options(tcp_port, ws_port)) as server:
        server.wait_for("Server accepting connections")
        env = clean_environment()
        env.update(CLAUDECODE="1", HOME=str(tmp_path), USERPROFILE=str(tmp_path),
                   APPDATA=str(tmp_path), XDG_CONFIG_HOME=str(tmp_path))
        result = run_cli(
            str(binary), "--log-file", str(log), "client", f"127.0.0.1:{tcp_port}",
            "--test-pattern", "--video-codec", "raw", "--audio=false", "--snapshot",
            "--snapshot-delay", "0", "--width", "40", "--height", "12",
            "--color-mode", "none", "--splash-screen=false", env=env, cwd=tmp_path,
            capture_output=True, timeout=30,
        )
        text = log.read_text(encoding="utf-8", errors="replace")
        assert result.returncode == 0, text[-8000:]
        assert len(result.stdout.strip()) >= 100, text[-8000:]
        assert "Authentication failed" not in text
        assert server.process.poll() is None, server.text()[-8000:]


def test_client_uses_glyph_renderer_for_first_server_frame(binary, tmp_path):
    """Initial settings must arrive before STREAM_START renders a frame."""
    tcp_port, ws_port = free_port(), free_port()
    with running(binary, tmp_path, "glyph-server", "server",
                 server_options(tcp_port, ws_port)) as server:
        server.wait_for("Server accepting connections")
        env = clean_environment()
        env.update(CLAUDECODE="1", HOME=str(tmp_path), USERPROFILE=str(tmp_path),
                   APPDATA=str(tmp_path), XDG_CONFIG_HOME=str(tmp_path))
        result = run_cli(
            str(binary), "client", f"127.0.0.1:{tcp_port}", "--test-pattern",
            "--audio=false", "--snapshot", "--snapshot-delay", "0",
            "--renderer=glyph", "--width", "40", "--height", "12",
            "--color-mode", "none", "--splash-screen=false", "--reconnect-attempts", "0",
            env=env, cwd=tmp_path, capture_output=True, timeout=30,
        )
        assert result.returncode == 0, server.text()[-8000:]
        # The glyph atlas contains printable characters outside the standard
        # luminance palette. This observes the first frame returned by the
        # server, not a later runtime settings update.
        output = re.sub(rb"\x1b\[[0-9;]*[a-zA-Z]", b"", result.stdout)
        standard = set(b"   ...',;:clodxkO0KXNWM")
        assert set(output) - standard, output[-4000:]


def test_client_started_before_server_recovers_without_auth_error(binary, tmp_path):
    """A default client must survive the first transient startup rejection."""
    tcp_port, ws_port = free_port(), free_port()
    client = Service(
        binary, tmp_path, "client-first", "client",
        [f"127.0.0.1:{tcp_port}", "--test-pattern", "--audio=false",
         "--reconnect-attempts", "5", "--splash-screen=false"],
    )
    try:
        time.sleep(0.25)
        with running(binary, tmp_path, "late-server", "server", server_options(tcp_port, ws_port)) as server:
            server.wait_for("Server accepting connections")
            client.wait_for("CLIENT_CONNECTION_RUNNING", timeout=20)
            assert "Remote request rejected: Authentication failed" not in client.text()
    finally:
        client.stop()
