"""Native event backend interoperability and slow-peer isolation checks."""
from pathlib import Path
import base64
import socket
import struct
import sys
import tempfile

from server_lifecycle import check_client, free_port, running, server_options


def main():
    binary = Path(sys.argv[1]).resolve()
    directory = Path(tempfile.mkdtemp(prefix="ascii-event-io-"))
    print(f"Event I/O logs: {directory}", flush=True)
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
    with running(binary, directory, "identity", "server",
                 server_options(port, ws_port) + ["--key", str(identity)]) as server:
        server.wait_for("TCP event backend:")
        check_client(binary, directory, f"127.0.0.1:{port}", "pinned-client", ["--server-key", str(pinned)])
    print("PASS: stalled peers, encrypted snapshots, password, pinned identity, plaintext, WebSocket, shutdown", flush=True)


if __name__ == "__main__":
    main()
