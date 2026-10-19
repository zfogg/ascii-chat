"""Connection ownership regression tests against the real native client.

Uses portable subprocesses, loopback TCP/WebSocket, deterministic test video,
and debug-only startup fault injection. Reconnects reuse one client process.
"""
from concurrent.futures import ThreadPoolExecutor
from contextlib import nullcontext
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time

from server_lifecycle import Service, check_client, free_port, interrupt, running, server_options

STAGES = ("send-mutex", "worker-pool", "crypto", "transport", "receive", "capture",
          "audio-pipeline", "audio-duplex", "audio-queue-mutex", "audio-queue", "audio-sender",
          "audio-capture", "keepalive")


def wait_count(service, message, count, timeout=25):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if service.text().count(message) >= count:
            return
        if service.process.poll() is not None:
            raise AssertionError(f"Client exited {service.process.returncode}: {service.text()[-5000:]}")
        time.sleep(0.05)
    raise AssertionError(f"Missing {count} occurrences of {message}: {service.text()[-5000:]}")


def client_options(destination):
    return [destination, "--no-keepawake", "--test-pattern", "--video-codec", "raw", "--audio=true", "--test-audio", "--no-audio-playback",
            "--width", "40", "--height", "12", "--fps", "10", "--splash-screen=false"]


def failures(binary, directory, destination, prefix):
    for stage in STAGES:
        name = f"{prefix}-fail-{stage}"
        env = {k: v for k, v in os.environ.items() if not k.startswith("ASCII_CHAT_")}
        env.update(CLAUDECODE="1", HOME=str(directory), USERPROFILE=str(directory),
                   APPDATA=str(directory), XDG_CONFIG_HOME=str(directory),
                   ASCII_CHAT_TEST_CLIENT_FAIL_STAGE=stage)
        log = directory / f"{name}.log"
        result = subprocess.run(
            [str(binary), "--no-check-update", "--log-file", str(log), "--log-level", "debug", "client",
             *client_options(destination), "--reconnect-attempts", "0"],
            env=env, cwd=directory, capture_output=True, timeout=25)
        text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
        assert result.returncode != 0, f"{name}: failure injection did not fire (use a Debug binary)"
        assert f"Injected client startup failure after {stage}" in text, text[-5000:]
        assert text.count("CLIENT_CONNECTION_RELEASED") == 1, text[-5000:]
        assert "CLIENT_CONNECTION_STOPPED: workers=0" in text, text[-5000:]
        # A timed join may retain a worker temporarily. Final cleanup must
        # prove it joined before any connection resources are released.
        assert text.rfind("CLIENT_CONNECTION_STOPPED: workers=0") > text.rfind("retaining its pool entry"), text[-5000:]
    # Reuse one process after partial worker startup, rather than relying only
    # on process exit to clear state between injected failures.
    env["ASCII_CHAT_TEST_CLIENT_FAIL_STAGE"] = "audio-sender"
    log = directory / f"{prefix}-repeated-failure.log"
    result = subprocess.run(
        [str(binary), "--no-check-update", "--log-file", str(log), "--log-level", "debug", "client",
         *client_options(destination), "--reconnect-attempts", "3"],
        env=env, cwd=directory, capture_output=True, timeout=30)
    text = log.read_text(encoding="utf-8", errors="replace")
    assert result.returncode != 0, text[-5000:]
    assert text.count("CLIENT_CONNECTION_RELEASED") == 3, text[-5000:]
    assert text.count("CLIENT_CONNECTION_CREATED: workers=0 frames=0") == 3, text[-5000:]
    check_client(binary, directory, destination, f"{prefix}-after-failures")
    print(f"PASS: {prefix}: all {len(STAGES)} startup stages unwind and next client succeeds", flush=True)


def stop_peer(service):
    # Abrupt peer loss exercises client cancellation without depending on the
    # server's graceful shutdown of an active WebSocket session.
    if service.process.poll() is None:
        service.process.kill()
    service.process.wait(timeout=10)
    service.stream.close()


def reconnect(binary, directory, websocket):
    tcp, ws = free_port(), free_port()
    prefix = "ws" if websocket else "tcp"
    destination = f"ws://127.0.0.1:{ws}" if websocket else f"127.0.0.1:{tcp}"
    server = None
    client = None
    try:
        server = Service(binary, directory, f"{prefix}-server-0", "server", server_options(tcp, ws))
        server.wait_for("Server accepting connections")
        previous_dump = os.environ.get("ASCIICHAT_DUMP_AUDIO")
        os.environ["ASCIICHAT_DUMP_AUDIO"] = "1"
        try:
            client = Service(binary, directory, f"{prefix}-persistent", "client", client_options(destination))
        finally:
            if previous_dump is None:
                os.environ.pop("ASCIICHAT_DUMP_AUDIO", None)
            else:
                os.environ["ASCIICHAT_DUMP_AUDIO"] = previous_dump
        for cycle in range(3):
            wait_count(client, "CLIENT_CONNECTION_RUNNING", cycle + 1)
            # Prove the connection sent real video before forcing peer loss.
            server.wait_for("announced video stream")
            wait_count(client, "CLIENT_DISPLAY: Display cleared, ready for ASCII frames", cycle + 1)
            stop_peer(server)
            server = None
            wait_count(client, "CLIENT_CONNECTION_RELEASED", cycle + 1)
            server = Service(binary, directory, f"{prefix}-server-{cycle+1}", "server", server_options(tcp, ws))
            server.wait_for("Server accepting connections")
        wait_count(client, "CLIENT_CONNECTION_RUNNING", 4)
        assert client.process.poll() is None
        # Race peer shutdown against the client's real local interrupt handler.
        wait_count(client, "CLIENT_DISPLAY: Display cleared, ready for ASCII frames", 4)
        with ThreadPoolExecutor(max_workers=2) as executor:
            peer_stop = executor.submit(stop_peer, server)
            local_stop = executor.submit(interrupt, client.process)
            peer_stop.result()
            local_stop.result()
        client.process.wait(timeout=20)
        client.stop()
        server = None
        text = client.text()
        assert text.count("Audio debugging enabled: dumping to") == 1, text[-5000:]
        assert text.count("CLIENT_CONNECTION_RELEASED") >= 4, text[-5000:]
        assert text.count("CLIENT_CONNECTION_CREATED: workers=0 frames=0") == text.count("CLIENT_CONNECTION_RELEASED"), text[-5000:]
        # A timed join may retain a worker temporarily. Final cleanup must
        # prove it joined before any connection resources are released.
        assert text.rfind("CLIENT_CONNECTION_STOPPED: workers=0") > text.rfind("retaining its pool entry"), text[-5000:]
        client = server = None
        print(f"PASS: {prefix}: three reconnects in one process and concurrent peer/local shutdown", flush=True)
    finally:
        if client is not None:
            client.stop()
        if server is not None:
            stop_peer(server)


def stalled_handshake(binary, directory):
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        listener.settimeout(15)
        client = Service(binary, directory, "stalled-handshake", "client",
                         [*client_options(f"127.0.0.1:{listener.getsockname()[1]}"),
                          "--reconnect-attempts", "0"])
        try:
            with listener.accept()[0] as peer:
                # Keep the peer open and silent while the real client waits for crypto.
                client.wait_for("Performing crypto handshake with server")
                started = time.monotonic()
                interrupt(client.process)
                wait_count(client, "CLIENT_CONNECTION_RELEASED", 1, timeout=5)
                assert time.monotonic() - started < 5
                client.process.wait(timeout=20)
            print("PASS: silent TCP handshake connection released within five seconds", flush=True)
        finally:
            if client.process.poll() is None:
                client.process.kill()
                client.process.wait()
            client.stream.close()


def delayed_handshake(binary, directory):
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        listener.settimeout(15)
        client = Service(binary, directory, "delayed-handshake", "client",
                         [*client_options(f"127.0.0.1:{listener.getsockname()[1]}"),
                          "--reconnect-attempts", "0"])
        try:
            with listener.accept()[0] as peer:
                peer.settimeout(5)

                def receive_exact(length):
                    data = bytearray()
                    while len(data) < length:
                        chunk = peer.recv(length - len(data))
                        assert chunk, "Client closed before sending its protocol version"
                        data.extend(chunk)
                    return bytes(data)

                header = receive_exact(22)
                _, packet_type, length, _, _ = struct.unpack("!QHIII", header)
                assert packet_type == 1 and length == 16
                version = receive_exact(length)
                # Echo a valid protocol reply after the normal one-second poll
                # window. Initialization must allow the connection budget.
                time.sleep(1.25)
                peer.sendall(header + version)
                wait_count(client, "Server protocol version: 1.0", 1, timeout=5)
                interrupt(client.process)
                wait_count(client, "CLIENT_CONNECTION_RELEASED", 1, timeout=5)
                client.process.wait(timeout=20)
            print("PASS: delayed TCP protocol reply accepted and handshake remains cancellable", flush=True)
        finally:
            if client.process.poll() is None:
                client.process.kill()
                client.process.wait()
            client.stream.close()


def main():
    binary = Path(sys.argv[1]).resolve()
    output = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None
    if output:
        output.mkdir(parents=True, exist_ok=True)
    with nullcontext(str(output)) if output else tempfile.TemporaryDirectory(prefix="ascii-connection-owner-") as tmp:
        directory = Path(tmp)
        stalled_handshake(binary, directory)
        delayed_handshake(binary, directory)
        tcp, ws = free_port(), free_port()
        with running(binary, directory, "failure-server", "server", server_options(tcp, ws)) as server:
            server.wait_for("Server accepting connections")
            failures(binary, directory, f"127.0.0.1:{tcp}", "tcp")
            failures(binary, directory, f"ws://127.0.0.1:{ws}", "ws")
        reconnect(binary, directory, False)
        reconnect(binary, directory, True)


if __name__ == "__main__":
    main()
