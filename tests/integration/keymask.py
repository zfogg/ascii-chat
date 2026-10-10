"""Test the compiled Keymask implementation, imports, trust decisions and terminal UI.

python tests/integration/keymask.py --library build/bin/asciichat.dll
Optional: --terminal (pyte + pywinpty/pexpect), --contact-sheet PATH (Pillow).
All keys and trust files are disposable fixtures; no personal keys are read.
"""

import argparse
import base64
import ctypes as C
import hashlib
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time


class PublicKey(C.Structure):
    _fields_ = [("type", C.c_int), ("key", C.c_ubyte * 32), ("comment", C.c_char * 256)]


def public_key(data, kind=1):
    return PublicKey(kind, (C.c_ubyte * 32).from_buffer_copy(data), b"")


def load(library, directory, art="on"):
    # Keep the DLL search handle alive for the duration of the tests.
    handle = os.add_dll_directory(str(library.parent)) if os.name == "nt" else None
    native = C.CDLL(str(library))
    native._directory_handle = handle
    native.asciichat_shared_init.argtypes = [C.c_char_p, C.c_bool, C.c_bool]
    assert native.asciichat_shared_init(str(directory / "keymask.log").encode(), True, True) == 0
    args = [b"keymask-test", b"--no-check-update", b"--log-file", str(directory / "keymask.log").encode(),
            b"--key-art", art.encode(), b"client"]
    argv = (C.c_char_p * (len(args) + 1))(*args, None)
    assert native.options_init(len(args), argv) == 0
    native.log_set_terminal_output(False)
    native.keymask_render.argtypes = [C.c_void_p, C.c_bool, C.c_void_p, C.c_size_t]
    native.key_fingerprint_digest.argtypes = [C.c_void_p, C.c_void_p]
    native.key_identity_format.argtypes = [C.POINTER(PublicKey), C.c_bool, C.c_bool, C.c_int, C.c_void_p, C.c_size_t]
    native.key_identity_format_terminal.argtypes = [C.POINTER(PublicKey), C.c_void_p, C.c_size_t]
    native.parse_public_key.argtypes = [C.c_char_p, C.POINTER(PublicKey)]
    native.prompt_unknown_host.argtypes = [C.c_char_p, C.c_uint16, C.c_void_p]
    native.prompt_unknown_host.restype = C.c_bool
    native.display_mitm_warning.argtypes = [C.c_char_p, C.c_uint16, C.c_void_p, C.c_void_p]
    native.display_mitm_warning.restype = C.c_bool
    native.check_known_host_with_key.argtypes = [C.c_char_p, C.c_uint16, C.c_void_p, C.c_void_p, C.POINTER(C.c_bool)]
    native.add_known_host.argtypes = [C.c_char_p, C.c_uint16, C.c_void_p]
    return native


def render(native, digest, unicode=False):
    out = C.create_string_buffer(1655)
    assert native.keymask_render(digest, unicode, out, len(out)) == 0
    return out.value.decode("utf-8")


def identity(native, key, art=True, unicode=False, cols=80):
    out = C.create_string_buffer(2048)
    assert native.key_identity_format(C.byref(key), art, unicode, cols, out, len(out)) == 0
    return out.value.decode("utf-8")


def check_core(native, directory):
    # Deterministic masks, full-input sensitivity, geometry and encoding parity.
    pictures = set()
    for bit in range(256):
        digest = (1 << (255 - bit)).to_bytes(32, "big")
        art = render(native, digest)
        assert render(native, digest) == art
        rows = art.splitlines()
        assert len(rows) == 18 and all(len(row) == 34 for row in rows)
        assert rows[0] == rows[-1] == "+" + "-" * 32 + "+"
        assert all(row.startswith("|") and row.endswith("|") for row in rows[1:-1])
        assert render(native, digest, True).replace("░", "#") == art
        assert len(render(native, digest, True).encode()) < 1655
        pictures.add(art)
    assert len(pictures) == 256
    golden = Path(__file__).with_name("keymask-v2-zero.txt").read_text(encoding="utf-8")
    assert render(native, bytes(32)) == golden

    # Reject undersized buffers before writing beyond them; exact-size buffers work.
    for unicode in (False, True):
        digest = b"\xff" * 32
        needed = len(render(native, digest, unicode).encode()) + 1
        for size in (1, needed - 1, needed):
            out = C.create_string_buffer(b"Z" * (needed + 16), needed + 17)
            result = native.keymask_render(digest, unicode, out, size)
            assert (result == 0) == (size == needed)
            assert out.raw[size:needed + 16] == b"Z" * (needed + 16 - size)
            if result:
                assert out.raw[0] == 0
    out = C.create_string_buffer(1655)
    assert native.keymask_render(None, False, out, len(out)) != 0
    assert native.keymask_render(bytes(32), False, None, 0) != 0
    assert native.key_fingerprint_digest(None, out) != 0

    # Real Ed25519 test-vector public key, imported through supported encodings.
    data = bytes.fromhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")
    digest = C.create_string_buffer(32)
    assert native.key_fingerprint_digest(data, digest) == 0
    assert digest.raw == hashlib.sha256(data).digest()
    blob = struct.pack(">I", 11) + b"ssh-ed25519" + struct.pack(">I", 32) + data
    b64 = base64.b64encode(blob)
    ssh = b"ssh-ed25519 " + b64 + b" fixture"
    path = directory / "fixture.pub"
    path.write_bytes(ssh + b"\n")
    # Minimal v4 OpenPGP Ed25519 public-key packet containing the same key.
    body = b"\x04\x00\x00\x00\x00\x16\x09" + bytes.fromhex("2b06010401da470f01") + b"\x01\x07\x40" + data
    packet = bytes((0xc6, len(body))) + body
    crc = 0xb704ce
    for byte in packet:
        crc ^= byte << 16
        for _ in range(8):
            crc <<= 1
            if crc & 0x1000000:
                crc ^= 0x1864cfb
    armor = (b"-----BEGIN PGP PUBLIC KEY BLOCK-----\n\n" + base64.b64encode(packet) + b"\n="
             + base64.b64encode((crc & 0xffffff).to_bytes(3, "big")) + b"\n-----END PGP PUBLIC KEY BLOCK-----\n")
    gpg_path = directory / "fixture.gpg"
    gpg_path.write_bytes(packet)
    imported = []
    for source in (ssh, b64, data.hex().encode(), str(path).encode(), armor, str(gpg_path).encode()):
        key = PublicKey()
        assert native.parse_public_key(source, C.byref(key)) == 0
        assert bytes(key.key) == data
        imported.append(identity(native, key).split("Keymask v2 / SHA-256\n")[1])
    assert len(set(imported)) == 1
    for kind in (1, 2, 3):
        key = public_key(data, kind)
        text = identity(native, key)
        assert "SHA256:" + hashlib.sha256(data).hexdigest() in text
        assert text.split("Keymask v2 / SHA-256\n")[1] == imported[0]
        assert "Keymask" not in identity(native, key, cols=33)
        assert "Keymask" in identity(native, key, cols=34)
        assert "Keymask" not in identity(native, key, art=False)
    key = public_key(data)
    assert native.key_identity_format(None, True, False, 80, out, len(out)) != 0
    assert native.key_identity_format(C.byref(key), True, False, 80, out, 1) != 0
    assert not native.prompt_unknown_host(b"127.0.0.234", 54321, data), "Noninteractive TOFU must still reject"
    # Exercise the actual known_hosts scan, including multiple candidate keys.
    expected, other = data, bytes(reversed(data))
    stored = C.create_string_buffer(32)
    found = C.c_bool(True)
    host, port = b"127.0.0.234", 54321
    assert native.check_known_host_with_key(host, port, data, stored, C.byref(found)) == 0
    assert not found.value
    native.get_known_hosts_path.restype = C.c_char_p
    trust_path = Path(native.get_known_hosts_path().decode())
    assert trust_path.is_relative_to(directory), trust_path
    trust_path.parent.mkdir(parents=True, exist_ok=True)
    first_entry = f"127.0.0.234:54321 ssh-ed25519 {expected.hex()} fixture\n"
    trust_path.write_text(first_entry, encoding="utf-8")
    assert native.check_known_host_with_key(host, port, other, stored, C.byref(found)) != 0
    assert found.value and stored.raw == expected
    assert not native.display_mitm_warning(host, port, stored, other)
    assert not native.display_mitm_warning(host, port, None, other)
    trust_path.write_text(first_entry + f"127.0.0.234:54321 ssh-ed25519 {other.hex()} fixture\n", encoding="utf-8")
    assert native.check_known_host_with_key(host, port, other, stored, C.byref(found)) == 1
    print("PASS: v2 golden vector, 256 single-bit mutations, UTF-8 parity, bounds, SHA-256, imports, narrow layout and trust decisions", flush=True)


def contact_sheet(native, destination):
    from PIL import Image, ImageDraw, ImageFont
    font_path = "C:/Windows/Fonts/consola.ttf" if os.name == "nt" else "DejaVuSansMono.ttf"
    font = ImageFont.truetype(font_path, 12)
    image = Image.new("RGB", (6 * 280, 6 * 300), "#101820")
    draw = ImageDraw.Draw(image)
    for i in range(36):
        digest = hashlib.sha256(f"keymask-contact-sheet-{i}".encode()).digest()
        text = f"Keymask v2  /  sample {i + 1:02}\n" + render(native, digest) + digest.hex()[:32]
        draw.multiline_text(((i % 6) * 280 + 14, (i // 6) * 300 + 12), text, font=font, fill="#d8e9e8", spacing=0)
    image.save(destination)
    print(f"Contact sheet: {destination}")


def probe(args, directory):
    native = load(args.library, directory, args.art)
    key = public_key(bytes(range(32)))
    out = C.create_string_buffer(2048)
    assert native.key_identity_format_terminal(C.byref(key), out, len(out)) == 0
    if args.probe == "format":
        print(out.value.decode("utf-8"), flush=True)
    elif args.probe == "prompt":
        native.log_set_terminal_output(True)
        result = native.prompt_unknown_host(b"127.0.0.234", 54321, key.key)
    elif args.probe == "notice":
        native.log_set_terminal_output(True)
        native.key_identity_announce.argtypes = [C.c_char_p, C.POINTER(PublicKey)]
        native.key_identity_announce(b"SERVER PUBLIC IDENTITY", C.byref(key))
        time.sleep(3)
    native.asciichat_shared_destroy()
    if args.probe == "prompt":
        print(f"PROMPT_RESULT={int(result)}", flush=True)
        time.sleep(0.5)


def check_terminal(args):
    from terminal_ui import Terminal
    command = [sys.executable, str(Path(__file__).resolve()), "--library", str(args.library)]
    for mode, expected in (("auto", False), ("on", True), ("off", False)):
        result = subprocess.run(command + ["--probe", "format", "--art", mode], capture_output=True, text=True, encoding="utf-8", timeout=30)
        assert result.returncode == 0, result.stderr
        assert ("Keymask v2" in result.stdout) == expected
        assert "SHA256:" in result.stdout
    for cols, rows, mode, expected in ((80, 40, "auto", True), (38, 40, "auto", False), (80, 40, "off", False)):
        terminal = Terminal(command + ["--probe", "notice", "--art", mode], rows=rows, cols=cols)
        try:
            text = terminal.expect(lambda text: "SHA256:" in text, "identity notice")
            assert ("Keymask v2" in text) == expected, text
            if expected:
                mask_rows = [line for line in text.splitlines() if "|" in line and ("░" in line or "#" in line)]
                assert len(mask_rows) >= 10, text
            terminal.pump(3.5)
        finally:
            terminal.close()
    for rows, expected in ((40, True), (24, False)):
        terminal = Terminal(command + ["--probe", "prompt", "--art", "auto"], rows=rows, cols=80)
        try:
            text = terminal.expect(lambda text: "continue connecting" in text, "unknown-host prompt")
            assert ("Keymask v2" in text) == expected, text
            terminal.write("n\r")
            terminal.expect(lambda text: "PROMPT_RESULT=0" in text, "TOFU rejection")
        finally:
            terminal.close()
    print("PASS: redirected auto/on/off, ConPTY/PTY notices, narrow terminals and interactive TOFU rejection")


def check_live(binary, directory):
    """Loopback handshake with temporary SSH keys; inspect both actual processes."""
    server_key, client_key = directory / "server-key", directory / "client-key"
    for key in (server_key, client_key):
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", str(key)], check=True, timeout=15)
    raw_server = base64.b64decode(server_key.with_suffix(".pub").read_text().split()[1])[-32:]
    raw_client = base64.b64decode(client_key.with_suffix(".pub").read_text().split()[1])[-32:]
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        websocket_port = reservation.getsockname()[1]
    config = directory / ("AppData/Roaming/ascii-chat" if os.name == "nt" else "ascii-chat")
    config.mkdir(parents=True, exist_ok=True)
    (config / "known_hosts").write_text(f"127.0.0.1:{port} ssh-ed25519 {raw_server.hex()} fixture\n")
    common = [str(binary), "--no-check-update", "--key-art=on", "--log-level=error"]
    server_log = directory / "server.log"
    server_output = directory / "server-output.txt"
    with server_output.open("w", encoding="utf-8") as output:
        server = subprocess.Popen(common + [f"--log-file={server_log}", "server", "127.0.0.1", f"--websocket-port={websocket_port}",
                                  f"--port={port}", "--key", str(server_key), "--client-keys", str(client_key) + ".pub",
                                  "--status-screen=false"], stdout=output, stderr=output, cwd=directory)
        try:
            time.sleep(1)
            assert server.poll() is None, server_output.read_text(encoding="utf-8", errors="replace")
            client = subprocess.run(common + [f"--log-file={directory / 'client.log'}", "client",
                                    f"127.0.0.1:{port}", "--key", str(client_key),
                                    "--test-pattern", "--snapshot", "--snapshot-delay=0", "--width=32", "--height=16",
                                    "--audio=false", "--splash-screen=false"],
                                    capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=45, cwd=directory)
            client_text = client.stdout + client.stderr
            assert client.returncode == 0, client_text
            assert "UNVERIFIED SERVER PUBLIC IDENTITY" in client_text, client_text
            assert "Keymask v2" in client_text, client_text
            assert "Keymask v2" not in client.stdout, "Identity art must not contaminate snapshot stdout"
            assert "SHA256:" + hashlib.sha256(raw_server).hexdigest() in client_text, client_text
            server_text = server_output.read_text(encoding="utf-8", errors="replace")
            assert "AUTHENTICATED CLIENT PUBLIC IDENTITY" in server_text, server_text
            assert "SHA256:" + hashlib.sha256(raw_client).hexdigest() in server_text, server_text
            assert "Keymask v2" in server_text, server_text
        finally:
            server.terminate()
            server.wait(timeout=10)
    print("PASS: real server/client authenticated snapshot, both public identity cards", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=lambda p: Path(p).resolve(), required=True)
    parser.add_argument("--terminal", action="store_true")
    parser.add_argument("--binary", type=lambda p: Path(p).resolve())
    parser.add_argument("--contact-sheet", type=Path)
    parser.add_argument("--probe", choices=("format", "prompt", "notice"))
    parser.add_argument("--art", default="on")
    args = parser.parse_args()
    for name in ("CLAUDECODE", "ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK", "ASCII_CHAT_QUESTION_PROMPT_RESPONSE"):
        os.environ.pop(name, None)
    with tempfile.TemporaryDirectory(prefix="ascii-chat-keymask-") as directory:
        directory = Path(directory)
        # Isolate trust storage and config loading from the developer's machine.
        for name in ("APPDATA", "XDG_CONFIG_HOME", "HOME", "USERPROFILE"):
            os.environ[name] = str(directory)
        for relative in ("AppData/Roaming", "AppData/Local"):
            (directory / relative).mkdir(parents=True)
        if args.probe:
            probe(args, directory)
            return
        native = load(args.library, directory)
        try:
            check_core(native, directory)
            if args.contact_sheet:
                contact_sheet(native, args.contact_sheet)
        finally:
            native.asciichat_shared_destroy()
        if args.terminal:
            check_terminal(args)
        if args.binary:
            check_live(args.binary, directory)


if __name__ == "__main__":
    main()
