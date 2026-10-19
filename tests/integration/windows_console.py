"""Hidden console fixture: inject real key records and read the active screen buffer."""
import ctypes as c
from ctypes import wintypes as w
import queue
import subprocess
import time

from pyte.screens import Char
from terminal_ui import CaptureScreen


class Coord(c.Structure):
    _fields_ = [("X", c.c_short), ("Y", c.c_short)]


class Rect(c.Structure):
    _fields_ = [("Left", c.c_short), ("Top", c.c_short), ("Right", c.c_short), ("Bottom", c.c_short)]


class BufferInfo(c.Structure):
    _fields_ = [("size", Coord), ("cursor", Coord), ("attributes", w.WORD), ("window", Rect), ("maximum", Coord)]


class Cell(c.Structure):
    _fields_ = [("character", c.c_wchar), ("attributes", w.WORD)]


class Key(c.Structure):
    _fields_ = [("down", w.BOOL), ("repeat", w.WORD), ("virtual", w.WORD),
                ("scan", w.WORD), ("character", c.c_wchar), ("controls", w.DWORD)]


class Event(c.Structure):
    _fields_ = [("type", w.WORD), ("padding", w.WORD), ("key", Key)]


class WindowsConsole:
    def __init__(self, argv, rows=20, cols=60):
        self.kernel = c.windll.kernel32
        self.kernel.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p, w.DWORD, w.DWORD, w.HANDLE]
        self.kernel.CreateFileW.restype = w.HANDLE
        self.kernel.CloseHandle.argtypes = [w.HANDLE]
        self.kernel.GetConsoleScreenBufferInfo.argtypes = [w.HANDLE, c.POINTER(BufferInfo)]
        self.kernel.ReadConsoleOutputW.argtypes = [w.HANDLE, c.POINTER(Cell), Coord, Coord, c.POINTER(Rect)]
        self.kernel.WriteConsoleInputW.argtypes = [w.HANDLE, c.POINTER(Event), w.DWORD, c.POINTER(w.DWORD)]
        self.kernel.SetConsoleWindowInfo.argtypes = [w.HANDLE, w.BOOL, c.POINTER(Rect)]
        self.screen = CaptureScreen(cols, rows)
        self.raw, self.output = [], queue.Queue()
        self.rows, self.cols = rows, cols
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        self.process = subprocess.Popen(argv, startupinfo=startup, creationflags=subprocess.CREATE_NEW_CONSOLE)
        self.kernel.FreeConsole()
        for _ in range(100):
            if self.kernel.AttachConsole(self.process.pid):
                break
            time.sleep(0.01)
        else:
            self.process.terminate()
            raise OSError("Cannot attach test console")
        self.kernel.SetConsoleCtrlHandler(None, True)
        handle = self._open("CONOUT$")
        try:
            rect = Rect(0, 0, cols - 1, rows - 1)
            assert self.kernel.SetConsoleWindowInfo(handle, True, c.byref(rect))
        finally:
            self.kernel.CloseHandle(handle)

    def _open(self, name):
        handle = self.kernel.CreateFileW(name, 0xC0000000, 3, None, 3, 0, None)
        if handle == c.c_void_p(-1).value:
            raise OSError("Cannot open test console")
        return handle

    def pump(self, seconds=0.2):
        time.sleep(seconds)
        handle = self._open("CONOUT$")
        try:
            info = BufferInfo()
            assert self.kernel.GetConsoleScreenBufferInfo(handle, c.byref(info))
            cols = info.window.Right - info.window.Left + 1
            rows = info.window.Bottom - info.window.Top + 1
            cells = (Cell * (cols * rows))()
            rect = info.window
            assert self.kernel.ReadConsoleOutputW(handle, cells, Coord(cols, rows), Coord(0, 0), c.byref(rect))
            self.screen.resize(rows, cols)
            colors = ["black", "blue", "green", "cyan", "red", "magenta", "brown", "white",
                      "brightblack", "brightblue", "brightgreen", "brightcyan", "brightred", "brightmagenta", "brightyellow", "brightwhite"]
            for y in range(rows):
                for x in range(cols):
                    cell = cells[y * cols + x]
                    self.screen.buffer[y][x] = Char(cell.character or " ", fg=colors[cell.attributes & 15],
                                                     bg=colors[(cell.attributes >> 4) & 15])
            text = "\n".join(self.screen.display)
            self.raw.append(text)
            return text
        finally:
            self.kernel.CloseHandle(handle)

    def expect(self, predicate, description, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = self.pump()
            if predicate(text):
                return text
        raise AssertionError(description + "\n" + text)

    def write(self, text):
        handle = self._open("CONIN$")
        try:
            units = text.encode("utf-16le")
            events = (Event * (len(units) // 2))()
            for i in range(len(events)):
                ch = int.from_bytes(units[2 * i:2 * i + 2], "little")
                virtual = {13: 13, 27: 27, 3: 67}.get(ch, 0)
                events[i] = Event(1, 0, Key(True, 1, virtual, 0, chr(ch), 8 if ch == 3 else 0))
            written = w.DWORD()
            assert self.kernel.WriteConsoleInputW(handle, events, len(events), c.byref(written))
            assert written.value == len(events)
        finally:
            self.kernel.CloseHandle(handle)

    def interrupt(self):
        self.kernel.GenerateConsoleCtrlEvent(0, 0)
        self.process.wait(timeout=15)
        assert self.process.returncode == 0, self.process.returncode

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
            self.process.wait(timeout=10)
        self.kernel.FreeConsole()
