"""Client admission, failed handshakes, and slot reuse."""
import socket
import time

import pytest
from ascii_chat_tests.cli import clean_environment, run_cli
from ascii_chat_tests.process import check_client, free_port, running, server_options

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
