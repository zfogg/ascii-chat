"""Exercise native Kitty output in an isolated interpreter."""
import ctypes as c
import os
from pathlib import Path
import sys
import tempfile
from ascii_chat_tests.media import reconstruct


def main():
    with tempfile.TemporaryDirectory(prefix="ascii-kitty-native-") as directory:
        root = Path(directory)
        api = c.CDLL(str(Path(sys.argv[1]).resolve()))
        libc = c.CDLL(None)
        libc.fopen.argtypes = [c.c_char_p, c.c_char_p]
        libc.fopen.restype = c.c_void_p
        libc.fclose.argtypes = [c.c_void_p]
        api.kitty_image_write.argtypes = [c.c_void_p, c.c_void_p, c.c_int, c.c_int, c.c_int]
        for w, h in [(1, 1), (768, 1), (769, 1), (767, 2)]:
            pitch = w * 4 + 7
            pixels = bytes((i * 37) % 256 for i in range(pitch * h))
            target = root / "packet"
            stream = libc.fopen(os.fsencode(target), b"wb")
            assert stream
            try:
                assert api.kitty_image_write(stream, pixels, w, h, pitch) == 0
                assert api.kitty_image_write(stream, pixels, w, h, w * 4 - 1) != 0
            finally:
                libc.fclose(stream)
            actual = reconstruct(target.read_bytes())
            assert actual[:2] == (w, h)
            assert actual[2] == b"".join(pixels[y * pitch:y * pitch + w * 4] for y in range(h))
        stream = libc.fopen(b"/dev/full", b"wb")
        if stream:
            try:
                assert api.kitty_image_write(stream, bytes(4), 1, 1, 4) != 0
            finally:
                libc.fclose(stream)
        print("C API: chunk/base64/row padding boundaries, invalid pitch, write failure")


if __name__ == "__main__":
    main()
