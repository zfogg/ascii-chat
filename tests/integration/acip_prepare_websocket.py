"""Echo real WebSocket frames to exercise the production ACIP client transport."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import base64
import hashlib
import socket
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor


def exact(peer, length):
    data = bytearray()
    while len(data) < length:
        part = peer.recv(length - len(data))
        if not part:
            raise EOFError("WebSocket closed before the expected frame")
        data.extend(part)
    return bytes(data)


def echo(listener):
    peer, _ = listener.accept()
    with peer:
        peer.settimeout(15)
        request = bytearray()
        while not request.endswith(b"\r\n\r\n"):
            request.extend(exact(peer, 1))
        headers = dict(line.split(":", 1) for line in request.decode().split("\r\n")[1:] if ":" in line)
        headers = {key.lower(): value.strip() for key, value in headers.items()}
        accept = base64.b64encode(hashlib.sha1((headers["sec-websocket-key"] +
                                  "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        response = ("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                    f"Sec-WebSocket-Accept: {accept}\r\n")
        if "sec-websocket-protocol" in headers:
            response += f"Sec-WebSocket-Protocol: {headers['sec-websocket-protocol'].split(',')[0]}\r\n"
        peer.sendall((response + "\r\n").encode())
        first, second = exact(peer, 2)
        assert first == 0x82 and second & 0x80, "Expected one masked binary message"
        length = second & 0x7f
        if length == 126:
            length = struct.unpack("!H", exact(peer, 2))[0]
        elif length == 127:
            length = struct.unpack("!Q", exact(peer, 8))[0]
        assert length < 1024 * 1024
        mask = exact(peer, 4)
        message = bytes(value ^ mask[i % 4] for i, value in enumerate(exact(peer, length)))
        prefix = bytes([0x82, length]) if length < 126 else b"\x82\x7e" + struct.pack("!H", length)
        peer.sendall(prefix + message)
        # Keep the peer alive until the client has checked and consumed the echo.
        try:
            peer.recv(128)
        except ConnectionResetError:
            pass


def main():
    with socket.socket() as listener, ThreadPoolExecutor(max_workers=1) as executor:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(20)
        task = executor.submit(echo, listener)
        result = subprocess.run([sys.argv[1], f"ws://127.0.0.1:{listener.getsockname()[1]}"], timeout=30)
        task.result(timeout=20)
        if result.returncode:
            raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
