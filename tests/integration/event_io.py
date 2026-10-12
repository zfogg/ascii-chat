"""Native event backend interoperability and slow-peer isolation checks."""
from pathlib import Path
import base64
import socket
import struct
import sys
import tempfile

from server_lifecycle import check_client, free_port, running, server_options


def crc32c(data):
    crc = 0xffffffff
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82f63b78 if crc & 1 else 0)
    return crc ^ 0xffffffff


def send_packet(peer, kind, payload):
    peer.sendall(struct.pack("!QHIII", 0xa5c11c4a1, kind, len(payload), crc32c(payload), 0) + payload)


def receive_packet(peer):
    def exact(length):
        data = b""
        while len(data) < length:
            part = peer.recv(length - len(data))
            if not part:
                raise AssertionError("Handshake closed before its response")
            data += part
        return data
    magic, kind, length, crc, _ = struct.unpack("!QHIII", exact(22))
    payload = exact(length)
    assert magic == 0xa5c11c4a1 and crc32c(payload) == crc
    return kind, payload


def legacy_identity_handshake(port):
    with socket.create_connection(("127.0.0.1", port), timeout=5) as peer:
        send_packet(peer, 1, struct.pack("!HHBBB H7x", 1, 0, 3, 0, 0, 0))
        assert receive_packet(peer)[0] == 1
        send_packet(peer, 1100, struct.pack("!HHHBBBB", 1, 1, 1, 1, 1, 1, 1))
        assert receive_packet(peer)[0] == 1101
        # RFC 7748 X25519 public key: the legacy client sends this without HELLO.
        key = bytes.fromhex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a")
        send_packet(peer, 1103, key)
        assert receive_packet(peer)[0] == 1102
        # The retained response must advance the handshake, not wait for a resend.
        assert receive_packet(peer)[0] in (1104, 1108)


def idle_rate_limit(binary, directory):
    port, ws_port = free_port(), free_port()
    with running(binary, directory, "idle-limit", "server", server_options(port, ws_port)) as server:
        server.wait_for("TCP event backend:")
        peers = []
        rejected = 0
        try:
            for _ in range(90):
                peer = socket.create_connection(("127.0.0.1", port), timeout=5)
                peers.append(peer)
                peer.settimeout(0.1)
                try:
                    if not peer.recv(1):
                        rejected += 1
                except ConnectionResetError:
                    rejected += 1
                except socket.timeout:
                    pass
            assert rejected >= 10, f"Idle connections bypassed rate limit: {rejected} rejected"
        finally:
            for peer in peers:
                peer.close()


def main():
    binary = Path(sys.argv[1]).resolve()
    directory = Path(tempfile.mkdtemp(prefix="ascii-event-io-"))
    print(f"Event I/O logs: {directory}", flush=True)
    idle_rate_limit(binary, directory)
    port, ws_port = free_port(), free_port()
    with running(binary, directory, "server", "server", server_options(port, ws_port)) as server:
        server.wait_for("TCP event backend:")
        # A peer that never finishes its header must not occupy the event loop
        # or prevent a complete encrypted handshake and media exchange.
        stalled = []
        try:
            for _ in range(24):
                peer = socket.create_connection(("127.0.0.1", port), timeout=5)
                peer.sendall(b"\x00")
                stalled.append(peer)
            for attempt in range(3):
                check_client(binary, directory, f"127.0.0.1:{port}", f"encrypted-{attempt}")
        finally:
            for peer in stalled:
                peer.close()
        check_client(binary, directory, f"ws://127.0.0.1:{ws_port}", "websocket")
    if "Server shutdown complete" not in server.text():
        raise AssertionError("Event server did not finish shutdown")
    for name, options in [("password", ["--password", "event-test-password"]),
                          ("plaintext", ["--no-encrypt", "--no-auth"])]:
        port, ws_port = free_port(), free_port()
        with running(binary, directory, name, "server", server_options(port, ws_port) + options) as server:
            server.wait_for("TCP event backend:")
            check_client(binary, directory, f"127.0.0.1:{port}", name + "-client", options)
    # Public RFC 8032 test vector, used only as a local fixture.
    public = bytes.fromhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")
    identity = directory / "server-identity"
    seed = bytes.fromhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60")
    pinned = directory / "server-identity.pub"
    wire = b"\x00\x00\x00\x0bssh-ed25519\x00\x00\x00\x20" + public
    def field(value):
        return struct.pack("!I", len(value)) + value
    private = struct.pack("!II", 1, 1) + field(b"ssh-ed25519") + field(public) + field(seed + public) + field(b"")
    private += bytes(range(1, 9 - len(private) % 8))
    key = b"openssh-key-v1\x00" + field(b"none") + field(b"none") + field(b"") + struct.pack("!I", 1)
    key += field(wire) + field(private)
    encoded = base64.b64encode(key).decode()
    identity.write_text("-----BEGIN OPENSSH PRIVATE KEY-----\n" +
                        "\n".join(encoded[i:i + 70] for i in range(0, len(encoded), 70)) +
                        "\n-----END OPENSSH PRIVATE KEY-----\n")
    identity.chmod(0o600)
    pinned.write_text("ssh-ed25519 " + base64.b64encode(wire).decode() + " event-test\n")
    port, ws_port = free_port(), free_port()
    # Seed trust for the fixture identity so Release does not need a TOFU prompt.
    config = directory / "ascii-chat"
    config.mkdir(exist_ok=True)
    known_hosts = config / "known_hosts"
    known_hosts.write_text(f"127.0.0.1:{port} x25519 {public.hex()} ascii-chat\n")
    known_hosts.chmod(0o600)
    with running(binary, directory, "identity", "server",
                 server_options(port, ws_port) + ["--key", str(identity)]) as server:
        server.wait_for("TCP event backend:")
        legacy_identity_handshake(port)
        check_client(binary, directory, f"127.0.0.1:{port}", "pinned-client", ["--server-key", str(pinned)])
    print("PASS: stalled peers, crypto modes, automatic copy avoidance, WebSocket, reconnects, shutdown", flush=True)


if __name__ == "__main__":
    main()
