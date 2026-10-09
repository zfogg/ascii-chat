"""Exercise the production invitation renderer from a built shared library.

Usage: python tests/ui/test_invitation.py build/bin/asciichat.dll
Also accepts libasciichat.so / libasciichat.dylib on Unix.
"""
import ctypes as c
import os
from pathlib import Path
import re
import sys
import unittest

LIBRARY = Path(sys.argv.pop(1)).resolve()
DLL_DIRECTORY = os.add_dll_directory(str(LIBRARY.parent)) if os.name == "nt" else None
lib = c.CDLL(str(LIBRARY))


class Size(c.Structure):
    _fields_ = [("rows", c.c_int), ("cols", c.c_int)]


lib.frame_buffer_create.argtypes = [c.c_int, c.c_int]
lib.frame_buffer_create.restype = c.c_void_p
lib.frame_buffer_destroy.argtypes = [c.c_void_p]
lib.frame_buffer_get_content.argtypes = [c.c_void_p]
lib.frame_buffer_get_content.restype = c.c_void_p
lib.frame_buffer_get_length.argtypes = [c.c_void_p]
lib.frame_buffer_get_length.restype = c.c_size_t
lib.invitation_render.argtypes = [c.c_void_p, Size, c.c_char_p, c.c_bool, c.c_int, c.c_bool]
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
SESSION = "happy-sunset-ocean"


def render(cols=80, rows=24, session=SESSION, joining=False, frame=0, colors=False):
    buf = lib.frame_buffer_create(rows, cols)
    if not buf:
        raise MemoryError("frame_buffer_create")
    try:
        lib.invitation_render(buf, Size(rows, cols), session.encode(), joining, frame, colors)
        raw = c.string_at(lib.frame_buffer_get_content(buf), lib.frame_buffer_get_length(buf)).decode()
        return raw, ANSI.sub("", raw)
    finally:
        lib.frame_buffer_destroy(buf)


class InvitationTests(unittest.TestCase):
    def test_instruction_immediately_below_string(self):
        _, text = render()
        lines = [line.strip() for line in text.splitlines()]
        index = lines.index(SESSION)
        self.assertEqual(lines[index - 1], "Share this string to connect:")
        self.assertEqual(lines[index + 1], "Run: ascii-chat " + SESSION)
        self.assertEqual(len(lines[index + 2:]), 0)

    def test_centered_block(self):
        _, text = render()
        lines = text.splitlines()
        nonempty = [i for i, line in enumerate(lines) if line.strip()]
        self.assertLessEqual(abs(nonempty[0] - (23 - nonempty[-1] - 1)), 1)
        for line in lines[-3:]:
            left = len(line) - len(line.lstrip())
            right = 79 - len(line)
            self.assertLessEqual(abs(left - right), 1)

    def test_compact_terminal(self):
        _, text = render(40, 10)
        self.assertIn("ascii-chat", text)
        self.assertIn("Run: ascii-chat " + SESSION, text)
        self.assertLess(text.count("\n"), 10)

    def test_long_string_wraps_without_losing_characters(self):
        session = "affectionate-acquaintance-acquaintance"
        _, text = render(25, 18, session)
        joined = "".join(line.strip() for line in text.splitlines())
        self.assertIn(session + "Run: ascii-chat " + session, joined)

    def test_creation_and_joining(self):
        _, creating = render(session="")
        _, joining = render(joining=True)
        self.assertIn("Creating session...", creating)
        self.assertNotIn("Run:", creating)
        self.assertIn("Connecting to session:", joining)
        self.assertIn(SESSION, joining)
        self.assertNotIn("Share this", joining)
        self.assertNotIn("Run:", joining)

    def test_only_logo_animates(self):
        first, plain_first = render(colors=True, frame=0)
        second, plain_second = render(colors=True, frame=50)
        self.assertNotEqual(first, second)
        self.assertEqual(plain_first, plain_second)
        self.assertEqual(first[first.index("Share this"):], second[second.index("Share this"):])
        self.assertNotIn("\x1b[38;", render(colors=False)[0])

    def test_resize_bounds_and_stale_content_erasure(self):
        for cols, rows in [(120, 40), (80, 24), (40, 10), (25, 18), (20, 5), (2, 2)]:
            with self.subTest(cols=cols, rows=rows):
                raw, text = render(cols, rows)
                self.assertTrue(raw.endswith("\x1b[J"))
                self.assertLess(text.count("\n"), rows)
                self.assertTrue(all(len(line) < cols for line in text.splitlines()))

    def test_short_terminal_prioritizes_invitation(self):
        _, text = render(80, 4)
        self.assertIn(SESSION, text)
        self.assertIn("Run: ascii-chat " + SESSION, text)
        self.assertNotIn("__", text)


if __name__ == "__main__":
    unittest.main()
