import pytest
#!/usr/bin/env python3
"""Real ACIP TCP packets -> native CLI callbacks -> PTY/ConPTY presentation."""
from pathlib import Path
import sys
from ascii_chat_tests.cli import cli_command

import argparse
import re
import socket
import struct
import subprocess
import tempfile
import time
from ascii_chat_tests.protocol import crc32c
from ascii_chat_tests.process import free_port


pytestmark = [pytest.mark.integration, pytest.mark.network, pytest.mark.terminal]
HEADER = struct.Struct("!QHIII")
MAGIC = 0xA5C11C4A1
VERSION = struct.pack("!HHBBB H7x", 1, 0, 0, 0, 0, 4)
# terminal_capabilities_packet_t: monochrome 40x12 viewer, 1 FPS, no media upload.
CAPABILITIES = struct.pack("!IIIIHH32s32sBII64sBBBII", 0, 0, 2, 0, 40, 12,
                           b"xterm", b"", 1, 1, 0, b"", 1, 0, 0, 0, 0)
UNTRUSTED = b"REMOTE_PRIVATE_DETAIL\x1b[2J"


def send(sock, kind, payload=b""):
    sock.sendall(HEADER.pack(MAGIC, kind, len(payload), crc32c(payload), 0) + payload)


def exact(sock, size):
    result = b""
    while len(result) < size:
        chunk = sock.recv(size - len(result))
        assert chunk, "Peer closed before expected protocol response"
        result += chunk
    return result


def receive(sock, wanted):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        magic, kind, size, crc, _ = HEADER.unpack(exact(sock, HEADER.size))
        assert magic == MAGIC and size <= 16 * 1024 * 1024
        payload = exact(sock, size)
        assert crc == crc32c(payload)
        if kind == wanted:
            return payload
    raise AssertionError(f"Missing packet {wanted}")


def error(sock, code):
    send(sock, 2003, struct.pack("!II", code, len(UNTRUSTED)) + UNTRUSTED)


def expect(term, text):
    term.expect(lambda value: text in value, f"Missing {text}", timeout=20)


def check_sanitizers(output):
    for marker in ("ERROR: AddressSanitizer", "runtime error:"):
        position = output.find(marker)
        assert position < 0, output[max(0, position - 200):position + 800]


def exited(term):
    deadline = time.monotonic() + 15
    while term.process.isalive() and time.monotonic() < deadline:
        term.pump(.05)
    assert not term.process.isalive(), "Remote fatal error did not exit promptly"
    term.pump(.2)
    check_sanitizers("".join(term.raw))


def test_remote_error_popup(binary, tmp_path, terminal_factory):
    binary = str(binary)
    directory = tmp_path
    def command(name, *options):
        arguments = cli_command(binary, "--log-file", str(directory / f"{name}.log"),
                     *options, "--no-encrypt", "--no-auth")
        if options[0] != "server":
            arguments.extend(["--audio=false", "--fps", "1", "--width", "40", "--height", "12"])
        if options[0] == "client":
            arguments.extend(["--video-codec", "raw"])
        return arguments

    # A real server must keep serving a peer while its error popup is visible.
    port = free_port()
    server = terminal_factory(command("server", "server", "127.0.0.1", "--port", str(port),
                              "--websocket-port", str(free_port()), "--status-screen=true"),
                      rows=40, cols=120)
    try:
        expect(server, "ascii-chat Server")
        with socket.create_connection(("127.0.0.1", port), timeout=10) as peer:
            send(peer, 1, VERSION)
            assert receive(peer, 1) == VERSION
            send(peer, 5000, CAPABILITIES)
            receive(peer, 6064)  # Admission and render settings completed.
            error(peer, 23)  # ERROR_AUDIO: still connected.
            expect(server, "ERROR_AUDIO")
            expect(server, "Client ")
            expect(server, " notification:")
            expect(server, "Dismiss all")
            attribution = re.search(r"Client (\S+) notification:", "\n".join(server.screen.display))
            assert attribution, "Popup omitted the reporting peer"
            assert f"Client {attribution[1]} reported Audio device error" in (directory / "server.log").read_text(
                encoding="utf-8", errors="replace")
            assert "REMOTE_PRIVATE_DETAIL" not in "".join(server.raw)
            send(peer, 5001)
            receive(peer, 5002)  # PONG while the modal owns input.
            server.write("\r")
            server.expect(lambda value: "Dismiss all" not in value, "Popup did not dismiss")
            error(peer, 0x7fffffff)
            expect(server, "ERROR_INTERNAL")
            expect(server, "Dismiss all")
            send(peer, 5001)
            receive(peer, 5002)  # Remote fatal code must not stop the server.
        server.interrupt()
        check_sanitizers("".join(server.raw))
    finally:
        server.close()

    # Echo the plaintext negotiation, then inject errors after STREAM_START.
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        listener.settimeout(15)
        address = f"127.0.0.1:{listener.getsockname()[1]}"
        client = terminal_factory(command("client", "client", address, "--test-pattern",
                                  "--splash-screen=true", "--reconnect-attempts", "2"), rows=40, cols=120)
        try:
            peer, _ = listener.accept()
            with peer:
                peer.settimeout(10)
                version = receive(peer, 1)
                send(peer, 1, version)
                receive(peer, 5005)
                error(peer, 23)
                expect(client, "Server notification:")
                expect(client, "ERROR_AUDIO")
                expect(client, "Dismiss all")
                send(peer, 5001)
                receive(peer, 5002)
                client.write("\r")
                client.expect(lambda value: "Dismiss all" not in value, "Popup did not dismiss")
                error(peer, 48)
            retry_peer, _ = listener.accept()
            with retry_peer:
                retry_peer.settimeout(10)
                send(retry_peer, 1, receive(retry_peer, 1))
                receive(retry_peer, 5005)
                # The owner has now propagated the first connection's rejection.
                # Its errno context must not duplicate the receive worker's popup.
                expect(client, "ERROR_SESSION_FULL")
                expect(client, "Error (1)   1 of 1")
                client.write("\r")
                client.expect(lambda value: "Dismiss all" not in value, "Retry popup did not dismiss")
                error(retry_peer, 44)  # ERROR_NETWORK_PROTOCOL: immediate exit.
                exited(client)
                assert client.process.exitstatus == 44, client.process.exitstatus
                # Inspect final screen, not merely a transient escape-stream match.
                expect(client, "ERROR_NETWORK_PROTOCOL")
                assert "REMOTE_PRIVATE_DETAIL" not in "".join(client.raw)
        finally:
            client.close()

    # Native discovery shares the public rejection decoder during negotiation.
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        listener.settimeout(15)
        discovery = terminal_factory(command("discovery", "blue-mountain-tiger", "--discovery-service", "127.0.0.1",
            "--discovery-service-port", str(listener.getsockname()[1]), "--test-pattern",
            "--splash-screen=true"), rows=40, cols=120)
        try:
            peer, _ = listener.accept()
            with peer:
                peer.settimeout(10)
                receive(peer, 1)
                error(peer, 47)
                exited(discovery)
                expect(discovery, "ERROR_SESSION_NOT_FOUND")
                assert "REMOTE_PRIVATE_DETAIL" not in "".join(discovery.raw)
        finally:
            discovery.close()

    # Piped CLI uses normal diagnostics and exit status, with no modal or wait.
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        listener.settimeout(15)
        process = subprocess.Popen(command("headless", "client",
            f"127.0.0.1:{listener.getsockname()[1]}", "--test-pattern", "--splash-screen=false",
            "--reconnect-attempts", "2"), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            peer, _ = listener.accept()
            with peer:
                peer.settimeout(10)
                send(peer, 1, receive(peer, 1))
                receive(peer, 5005)
                error(peer, 48)  # ERROR_SESSION_FULL: retain the retry policy.
            peer, _ = listener.accept()
            assert process.poll() is None, "Retryable rejection stopped the client"
            with peer:
                peer.settimeout(10)
                send(peer, 1, receive(peer, 1))
                receive(peer, 5005)
                error(peer, 44)
                stdout, stderr = process.communicate(timeout=15)
            assert process.returncode == 44, (process.returncode, stderr[-4000:])
            log = (directory / "headless.log").read_text(encoding="utf-8", errors="replace")
            assert "Server reported Network protocol error (code=44)" in log
            assert "Dismiss all" not in (stdout + stderr).decode(errors="replace")
            check_sanitizers((stdout + stderr).decode(errors="replace"))
            assert "REMOTE_PRIVATE_DETAIL" not in log
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate(timeout=10)
    print("PASS remote errors: both directions, public text, peer attribution, live PONG, fatal final output")
