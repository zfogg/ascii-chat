"""Exercise OpenSSH private-key parsing through the production shared library.

Requires ssh-keygen. All keys are temporary and the real SSH agent is isolated.
"""
import base64
import ctypes as c
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import uuid


class PrivateKey(c.Structure):
    _fields_ = [("type", c.c_int), ("key", c.c_ubyte * 64), ("ssh_agent", c.c_bool),
                ("gpg_agent", c.c_bool), ("public", c.c_ubyte * 32),
                ("comment", c.c_char * 256), ("grip", c.c_char * 64)]


def ssh_string(value):
    return struct.pack(">I", len(value)) + value


def read_string(blob, offset):
    length = struct.unpack_from(">I", blob, offset)[0]
    return blob[offset + 4:offset + 4 + length], offset + 4 + length


def unpack_key(path):
    blob = base64.b64decode(b"".join(path.read_bytes().splitlines()[1:-1]))
    offset = 15
    fields = []
    for _ in range(3):
        field, offset = read_string(blob, offset)
        fields.append(field)
    count = struct.unpack_from(">I", blob, offset)[0]
    public, offset = read_string(blob, offset + 4)
    private, _ = read_string(blob, offset)
    return fields, count, public, private


def pack_key(fields, count, public, private):
    return b"openssh-key-v1\0" + b"".join(map(ssh_string, fields)) + struct.pack(">I", count) + ssh_string(public) + ssh_string(private)


def main():
    library = Path(sys.argv[1]).resolve()
    # A unique nonexistent endpoint prevents lookup/add operations on the user's agent.
    os.environ["SSH_AUTH_SOCK"] = (r"\\.\pipe\ascii-key-test-" if os.name == "nt" else "/tmp/ascii-key-test-") + uuid.uuid4().hex
    os.environ.pop("TEST_GPG_KEY_ID", None)
    password = "temporary-parser-test-passphrase"
    os.environ["ASCII_CHAT_KEY_PASSWORD"] = password
    if os.name == "nt":
        dll_directory = os.add_dll_directory(str(library.parent))
    api = c.CDLL(str(library))
    api.log_init.argtypes = [c.c_char_p, c.c_int, c.c_bool, c.c_bool]
    api.log_init(None, 4, True, False)
    api.parse_ssh_private_key.argtypes = [c.c_char_p, c.POINTER(PrivateKey)]
    api.ed25519_sign_message.argtypes = [c.POINTER(PrivateKey), c.c_char_p, c.c_size_t, c.c_void_p]
    api.ed25519_verify_signature.argtypes = [c.c_void_p, c.c_char_p, c.c_size_t, c.c_void_p, c.c_char_p]
    cases = 0
    # macOS sets TMPDIR below /var/folders, which is intentionally outside the
    # accepted user-key locations. Keep this fixture in the portable /tmp
    # namespace so it exercises parsing instead of path-policy rejection.
    tmpdir = "/tmp" if os.name != "nt" else None
    with tempfile.TemporaryDirectory(prefix="ascii-key-parser-", dir=tmpdir) as tmp:
        root = Path(tmp)

        def check(path, success, expected=None):
            nonlocal cases
            key = PrivateKey()
            result = api.parse_ssh_private_key(os.fsencode(path), c.byref(key))
            assert (result == 0) == success, (path.name, result)
            if success:
                assert key.type == 1 and not key.ssh_agent and not key.gpg_agent
                assert bytes(key.key)[32:] == expected
                signature = (c.c_ubyte * 64)()
                message = b"parser regression signature"
                assert api.ed25519_sign_message(c.byref(key), message, len(message), signature) == 0
                public = (c.c_ubyte * 32).from_buffer_copy(expected)
                assert api.ed25519_verify_signature(public, message, len(message), signature, None) == 0
            cases += 1

        generated = {}
        for cipher in ("none", "aes256-ctr", "aes256-cbc"):
            path = root / cipher
            args = ["ssh-keygen", "-q", "-t", "ed25519", "-a", "1", "-N", "" if cipher == "none" else password,
                    "-C", "temporary-parser-test", "-f", str(path)]
            if cipher != "none":
                args += ["-Z", cipher]
            subprocess.run(args, check=True, capture_output=True, timeout=20)
            pubblob = base64.b64decode(path.with_suffix(".pub").read_text().split()[1])
            check(path, True, pubblob[-32:])
            generated[cipher] = unpack_key(path)

        fields, count, public, private = generated["none"]
        valid = pack_key(fields, count, public, private)
        keytype, pos = read_string(private, 8)
        private_public, privpos = read_string(private, pos)
        secret, _ = read_string(private, privpos)
        malformed = {
            "bad-magic": b"invalid-key-v1\0\0" + valid[15:],
            "truncated-header": valid[:15],
            "multiple-keys": pack_key(fields, 2, public, private),
            "wrong-type": pack_key(fields, 1, ssh_string(b"ssh-rsa") + ssh_string(public[-32:]), private),
            "short-public": pack_key(fields, 1, ssh_string(b"ssh-ed25519") + ssh_string(b"x" * 31), private),
            "checkints": pack_key(fields, 1, public, bytes([private[0] ^ 1]) + private[1:]),
            "type-overflow": pack_key(fields, 1, public, private[:8] + b"\xff" * 4 + private[12:]),
            "public-overflow": pack_key(fields, 1, public, private[:pos] + b"\xff" * 4 + private[pos + 4:]),
            "short-private": pack_key(fields, 1, public, private[:privpos] + ssh_string(secret[:63])),
            "public-mismatch": pack_key(fields, 1, public, private[:privpos] + ssh_string(secret[:32] + b"x" * 32)),
            "truncated-private": valid[:-8],
        }
        encrypted, _, enc_public, enc_private = generated["aes256-ctr"]
        malformed["unsupported-cipher"] = pack_key([b"aes128-ctr", *encrypted[1:]], 1, enc_public, enc_private)
        malformed["unsupported-kdf"] = pack_key([encrypted[0], b"unknown", encrypted[2]], 1, enc_public, enc_private)
        malformed["invalid-salt"] = pack_key([*encrypted[:2], struct.pack(">I", 15) + encrypted[2][4:]], 1, enc_public, enc_private)
        for name, blob in malformed.items():
            path = root / name
            encoded = base64.encodebytes(blob)
            path.write_bytes(b"-----BEGIN OPENSSH PRIVATE KEY-----\n" + encoded + b"-----END OPENSSH PRIVATE KEY-----\n")
            path.chmod(0o600)
            check(path, False)
        os.environ["ASCII_CHAT_KEY_PASSWORD"] = "wrong-passphrase"
        check(root / "aes256-ctr", False)
        check(root / "aes256-cbc", False)
        check(root / "missing", False)
        key = PrivateKey()
        assert api.parse_ssh_private_key(None, c.byref(key)) != 0
        assert api.parse_ssh_private_key(os.fsencode(root / "none"), None) != 0
        print(f"PASS: {cases + 2} SSH parser cases, including three signing/verification round trips")


if __name__ == "__main__":
    main()
