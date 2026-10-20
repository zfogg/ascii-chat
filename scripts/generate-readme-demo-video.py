#!/usr/bin/env python3
"""Record a real, keyboard-driven ascii-chat session and export a README MP4.

Dependencies: Python 3.10+, Pillow, pyte, FFmpeg/ffprobe, and pywinpty on Windows
or pexpect on Unix. Install Python packages with:
    python -m pip install Pillow pyte "pywinpty>=3.0.5"  # Windows
    python -m pip install Pillow pyte pexpect    # Linux/macOS

The terminal recording is retained alongside logs, sampled screen text, chapter
images, commands, and timings in --work-dir. No desktop/camera recording or
GitHub upload occurs. The source media is rendered by the actual native binary;
Pillow only rasterizes its terminal output. Requires a debug build for 0/= TUIs
and the 4/z shortcuts (face follow / Zalgo) for uninterrupted effect changes.
"""

from __future__ import annotations

import argparse
import codecs
import functools
from fractions import Fraction
import gzip
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading
import time
import urllib.request
import unicodedata
from dataclasses import asdict, dataclass

ROOT = Path(__file__).resolve().parents[1]
FACE_URL = (
    "https://raw.githubusercontent.com/opencv/opencv_extra/"
    "cb09893cc0d1147787b254597cb78c5ae854185a/"
    "testdata/cv/tracking/faceocc2/data/faceocc2.webm"
)
FPS = 33
CELL_W, CELL_H = 8, 16


def boolean(value):
    if isinstance(value, bool):
        return value
    if value.lower() in ("true", "yes", "1", "on"):
        return True
    if value.lower() in ("false", "no", "0", "off"):
        return False
    raise argparse.ArgumentTypeError("expected true or false")


def positive(value):
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("must be a finite number greater than zero")
    return number


def timestamp(value):
    try:
        fields = value.split(":")
        if len(fields) > 3 or any(not x for x in fields):
            raise ValueError
        parts = [float(x) for x in fields]
        if any(not math.isfinite(x) or x < 0 for x in parts):
            raise ValueError
        if len(parts) > 1 and any(x >= 60 for x in parts[1:]):
            raise ValueError
        return sum(x * 60**i for i, x in enumerate(reversed(parts)))
    except ValueError:
        raise argparse.ArgumentTypeError("use seconds, M:SS, or H:MM:SS (nonnegative)") from None


def parser():
    class HelpFormatter(argparse.ArgumentDefaultsHelpFormatter, argparse.RawDescriptionHelpFormatter):
        pass

    p = argparse.ArgumentParser(
        description="Film ascii-chat's live keyboard controls and TUIs, with synchronized music, at 33 fps.",
        formatter_class=HelpFormatter,
        epilog="""Examples:
  python scripts/generate-readme-demo-video.py --opt-text-overlay
  python scripts/generate-readme-demo-video.py --media-file-start 0:28 --speed 1.1
  python scripts/generate-readme-demo-video.py --width 1600 --height 900 --optimize=false
  python scripts/generate-readme-demo-video.py --dry-run

The opening plays normally for 7 seconds, starting at --media-file-start.
--segment-duration-scale lengthens feature chapters without changing the music's
speed. Try 1.8 and 2.2 for approximately 1:55 and 2:20 feature tours.
--speed retimes the completed movie (including music and every chapter).
Dimensions are output pixels. The default 4x character cells give a 26x10 art
grid at 838x640; the splash and TUIs use a separate 104x40 grid. This makes art
characters four times larger while fitting GitHub's 838px desktop README width.
--optimize (on by default) automatically uses two-pass H.264, a 9.5 MB ceiling,
and 96k stereo AAC. --optimize=false disables the size ceiling and uses CRF 16.
--target-size-mb overrides the ceiling only when --optimize is enabled.
--output-fps 16.5 discards alternate captured frames without speeding up music.
The default face clip is OpenCV's single-face faceocc2 tracking sample.
MP4/H.264 + AAC + faststart is intended for GitHub video attachments. Upload
the reviewed result to GitHub and place its attachment URL in the README.
This script never edits README.md, commits media, or uploads anything.
""",
    )
    p.add_argument("--binary", type=Path, default=ROOT / "build/bin" / ("ascii-chat.exe" if os.name == "nt" else "ascii-chat"), help="native debug binary")
    p.add_argument("--media-file", type=Path, default=ROOT / "web/www/public/assets/demo-video-white-knuckles.webm", help="OK Go video with an audio track")
    p.add_argument("--face-file", type=Path, help="single-face video; otherwise download pinned OpenCV sample to work directory")
    p.add_argument("--media-file-start", type=timestamp, default="0:00", metavar="M:SS", help="starting position in the original music video")
    p.add_argument("--width", type=int, default=838, help="output width in pixels; also determines CLI columns")
    p.add_argument("--height", type=int, default=640, help="output height in pixels; also determines CLI rows")
    p.add_argument("--art-cell-scale", type=int, choices=(1, 2, 3, 4), default=4, help="art character size multiplier; 4 uses 75%% fewer columns/rows than 1 on the same canvas")
    p.add_argument("--output-fps", type=positive, default=33.0, help="export frame rate, at most 33; capture stays at 33 fps")
    p.add_argument("--target-size-mb", type=positive, default=9.5, help="decimal MB ceiling for --optimize; ignored with --optimize=false")
    p.add_argument("--speed", type=positive, default=1.0, help="whole-movie playback rate; 1.1 is 10%% faster")
    p.add_argument("--segment-duration-scale", type=positive, default=2.0, help="feature/TUI dwell-time multiplier; opening stays 7 seconds")
    p.add_argument("--opt-text-overlay", type=boolean, nargs="?", const=True, default=False, metavar="BOOL", help="label featured flags/keys at bottom center")
    p.add_argument("--optimize", type=boolean, nargs="?", const=True, default=True, metavar="BOOL", help="automatically fit below 10 MB with two-pass H.264 / 96k AAC; false uses uncapped CRF 16 / 192k AAC")
    p.add_argument("--output", type=Path, default=ROOT / "readme-demo.mp4", help="review movie (not committed)")
    p.add_argument("--work-dir", type=Path, default=ROOT / ".tmp/readme-demo-video", help="retained captures, sources, chapters, and evidence")
    p.add_argument("--font", type=Path, help="monospaced TrueType/OpenType font; otherwise find JetBrains Mono, Consolas, or DejaVu Sans Mono")
    p.add_argument("--ffmpeg", default="ffmpeg", help="FFmpeg executable")
    p.add_argument("--ffprobe", default="ffprobe", help="ffprobe executable")
    p.add_argument("--dry-run", action="store_true", help="print chapter plan and dimensions without recording/downloading")
    p.add_argument("--reuse-capture", action="store_true", help="re-render matching saved terminal captures; reject incompatible recording settings")
    return p


@dataclass
class Chapter:
    name: str
    seconds: float
    label: str
    keys: str = ""
    resize: tuple[int, int] | None = None


def chapters(scale=1.0):
    result = [
        Chapter("intro", 7, ""),
        Chapter("flip-x", .75, "--flip-x", "x"),
        Chapter("flip-xy", .75, "--flip-x --flip-y", "y"),
        Chapter("flip-y", .75, "--flip-y", "x"),
    ]
    filters = ("green", "magenta", "fuchsia", "orange", "teal", "cyan", "pink", "red", "yellow")
    for color in filters:
        result.append(Chapter("filter-" + color, 1, "--color-filter " + color, "yfff" if color == "green" else "f"))
    result += [
        Chapter("waveform", 2, "--waveform --audio-source media", "ff2"),
        Chapter("fft", 2, "--fft --audio-source media", "23"),
        Chapter("foreground", 1, "--render-mode foreground", "3"),
        Chapter("background", 1, "--render-mode background", "r"),
        Chapter("half-block", 1, "--render-mode half-block", "r"),
        Chapter("matrix", 2, "--matrix", "r1"),
        Chapter("zalgo", 2, "--zalgo 8", "1z"),
        Chapter("face-follow", 3, "--face-follow", "z4"),
    ]
    for chapter in result:
        if chapter.name != "intro":
            chapter.seconds *= scale
    return result


def tui_chapters(scale=1.0):
    result = [
        Chapter("open-media", 3, '"o" / Drop, paste, or open media', "o"),
        Chapter("logs", 3, '"]" / Session logs', "\x1b]"),
        Chapter("statistics", 3, '"=" / Live statistics', "]="),
        Chapter("sync", 3, '"0" / C sync primitives and deadlocks', "=0"),
        Chapter("help", 3, '"?" / Keyboard help', "0?"),
        Chapter("too-small", 3, "Responsive TUI / resize to continue", resize=(58, 12)),
        Chapter("outro", 2, "ascii-chat / video calls in your terminal", resize=None),
    ]
    for chapter in result:
        chapter.seconds *= scale
    return result


COMMANDS = []


def run(command, **kwargs):
    command = [str(x) for x in command]
    COMMANDS.append(command)
    subprocess.run(command, check=True, **kwargs)


def probe(path, args):
    return json.loads(subprocess.check_output([args.ffprobe, "-v", "error", "-show_format", "-show_streams", "-of", "json", str(path)]))


def atempo(speed):
    filters = []
    while speed > 2:
        filters.append("atempo=2")
        speed /= 2
    while speed < .5:
        filters.append("atempo=0.5")
        speed *= 2
    return ",".join(filters + [f"atempo={speed:.10g}"])


def prepare(args, main):
    work = args.work_dir
    face = args.face_file or work / "faceocc2.webm"
    if not face.is_file():
        if args.face_file:
            raise ValueError(f"Face video does not exist: {face}")
        print("Downloading pinned OpenCV face tracking sample...", flush=True)
        with urllib.request.urlopen(FACE_URL, timeout=60) as response, face.open("wb") as out:
            shutil.copyfileobj(response, out)
    info = probe(args.media_file, args)
    if not any(s["codec_type"] == "audio" for s in info["streams"]):
        raise ValueError("--media-file must include music/audio for the waveform and FFT chapters")
    length = sum(c.seconds for c in main)
    # Preserve extra music for the inserted patterns and final TUIs.
    music_length = length + 12 * args.segment_duration_scale + sum(c.seconds for c in tui_chapters(args.segment_duration_scale))
    required = args.media_file_start + music_length
    if float(info["format"]["duration"]) < required:
        raise ValueError(f"Media is too short after --media-file-start; need through {required:.2f} seconds")
    face_seconds = main[-1].seconds
    face_start = length - face_seconds
    # All dog-video effects share one uninterrupted native capture. The face
    # source is opened through the real media picker after that performance.
    graph = (
        f"[0:v]trim=duration={music_length},setpts=PTS-STARTPTS,fps=33,scale=640:360,setsar=1[v];"
        # Stereo-to-mono analysis can add 3 dB on correlated channels. Keep
        # headroom through time stretching, downmixing, and final AAC encoding.
        f"[0:a]atrim=duration={music_length},asetpts=PTS-STARTPTS,volume=0.5[a]"
    )
    run([args.ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-filter_complex_threads", "2", "-ss", args.media_file_start, "-i", args.media_file,
         "-filter_complex", graph, "-map", "[v]", "-map", "[a]", "-c:v", "libx264", "-preset", "fast",
         "-threads", "4", "-crf", "16", "-pix_fmt", "yuv420p", "-c:a", "pcm_f32le", work / "performance.mkv"])
    run([args.ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-filter_complex_threads", "2", "-i", face,
         "-ss", face_start, "-i", work / "performance.mkv", "-filter_complex",
         f"[0:v]trim=duration={face_seconds},setpts=PTS-STARTPTS,fps=33,scale=640:360:force_original_aspect_ratio=decrease,"
         f"pad=640:360:(ow-iw)/2:(oh-ih)/2,setsar=1,tpad=stop_mode=clone:stop_duration={music_length}[v]",
         "-map", "[v]", "-map", "1:a", "-c:v", "libx264", "-preset", "fast", "-threads", "2", "-crf", "16",
         "-pix_fmt", "yuv420p", "-c:a", "pcm_f32le", "-shortest", work / "face-performance.mkv"])
    return face


class WindowsTerminal:
    """Read ConPTY bytes with one UTF-8 decoder across every pipe boundary.

    pywinpty's underlying reader converts each pipe buffer separately, replacing
    split combining marks with two spacing replacement characters. Those extra
    cells wrap Zalgo rows. Keep the original bytes until a code point is complete.
    """

    def __init__(self, command, cols, rows, env):
        import ctypes as c
        from ctypes import wintypes as w

        class Coord(c.Structure):
            _fields_ = [("X", c.c_short), ("Y", c.c_short)]

        class Startup(c.Structure):
            _fields_ = [("cb", w.DWORD), ("reserved", w.LPWSTR), ("desktop", w.LPWSTR), ("title", w.LPWSTR),
                        ("x", w.DWORD), ("y", w.DWORD), ("xsize", w.DWORD), ("ysize", w.DWORD),
                        ("xchars", w.DWORD), ("ychars", w.DWORD), ("fill", w.DWORD), ("flags", w.DWORD),
                        ("show", w.WORD), ("reserved_size", w.WORD), ("reserved_bytes", c.c_void_p),
                        ("stdin", w.HANDLE), ("stdout", w.HANDLE), ("stderr", w.HANDLE)]

        class StartupEx(c.Structure):
            _fields_ = [("startup", Startup), ("attributes", c.c_void_p)]

        class Process(c.Structure):
            _fields_ = [("process", w.HANDLE), ("thread", w.HANDLE), ("pid", w.DWORD), ("tid", w.DWORD)]

        k = self.k = c.WinDLL("kernel32", use_last_error=True)
        # Use the same modern console host distributed with pywinpty, but read
        # its pipes as bytes ourselves. Older OS console hosts also mismeasure
        # stacked combining marks before their output reaches our decoder.
        import winpty
        console_dll = Path(winpty.__file__).parent / "conpty.dll"
        if not console_dll.is_file():
            raise ValueError("Install pywinpty>=3.0.5 for its bundled conpty.dll and OpenConsole.exe")
        self.console_library = c.WinDLL(str(console_dll), use_last_error=True)
        for name in ("CreatePseudoConsole", "ResizePseudoConsole", "ClosePseudoConsole"):
            setattr(k, name, getattr(self.console_library, "Conpty" + name))
        signatures = {
            "CreatePipe": ([c.POINTER(w.HANDLE), c.POINTER(w.HANDLE), c.c_void_p, w.DWORD], w.BOOL),
            "CloseHandle": ([w.HANDLE], w.BOOL),
            "CreatePseudoConsole": ([Coord, w.HANDLE, w.HANDLE, w.DWORD, c.POINTER(w.HANDLE)], c.c_long),
            "ResizePseudoConsole": ([w.HANDLE, Coord], c.c_long),
            "ClosePseudoConsole": ([w.HANDLE], None),
            "InitializeProcThreadAttributeList": ([c.c_void_p, w.DWORD, w.DWORD, c.POINTER(c.c_size_t)], w.BOOL),
            "UpdateProcThreadAttribute": ([c.c_void_p, w.DWORD, c.c_size_t, c.c_void_p, c.c_size_t, c.c_void_p, c.c_void_p], w.BOOL),
            "DeleteProcThreadAttributeList": ([c.c_void_p], None),
            "CreateProcessW": ([w.LPCWSTR, w.LPWSTR, c.c_void_p, c.c_void_p, w.BOOL, w.DWORD, c.c_void_p, w.LPCWSTR, c.POINTER(StartupEx), c.POINTER(Process)], w.BOOL),
            "ReadFile": ([w.HANDLE, c.c_void_p, w.DWORD, c.POINTER(w.DWORD), c.c_void_p], w.BOOL),
            "WriteFile": ([w.HANDLE, c.c_void_p, w.DWORD, c.POINTER(w.DWORD), c.c_void_p], w.BOOL),
            "GetExitCodeProcess": ([w.HANDLE, c.POINTER(w.DWORD)], w.BOOL),
            "TerminateProcess": ([w.HANDLE, w.UINT], w.BOOL),
        }
        for name, (types, result) in signatures.items():
            getattr(k, name).argtypes, getattr(k, name).restype = types, result
        self.coord, self.decoder = Coord, codecs.getincrementaldecoder("utf-8")("strict")
        self.input, self.output, self.console, self.process = None, None, None, None
        self.closed = False
        handles = []
        attributes = None
        try:
            input_read, input_write, output_read, output_write = (w.HANDLE() for _ in range(4))
            for reader, writer in ((input_read, input_write), (output_read, output_write)):
                if not k.CreatePipe(c.byref(reader), c.byref(writer), None, 0):
                    raise c.WinError(c.get_last_error())
                handles.extend((reader.value, writer.value))
            self.input, self.output = input_write.value, output_read.value
            console = w.HANDLE()
            if k.CreatePseudoConsole(Coord(cols, rows), input_read, output_write, 0, c.byref(console)) < 0:
                raise RuntimeError("CreatePseudoConsole failed")
            self.console = console.value
            size = c.c_size_t()
            k.InitializeProcThreadAttributeList(None, 1, 0, c.byref(size))
            attributes = c.create_string_buffer(size.value)
            if not k.InitializeProcThreadAttributeList(attributes, 1, 0, c.byref(size)):
                raise c.WinError(c.get_last_error())
            if not k.UpdateProcThreadAttribute(attributes, 0, 0x20016, self.console, c.sizeof(w.HANDLE), None, None):
                raise c.WinError(c.get_last_error())
            startup = StartupEx()
            startup.startup.cb, startup.attributes = c.sizeof(StartupEx), c.addressof(attributes)
            # Null standard handles let ConPTY supply console handles even
            # when this recorder's own stdout is redirected to a log file.
            startup.startup.flags = 0x100  # STARTF_USESTDHANDLES
            process = Process()
            environment = c.create_unicode_buffer("\0".join(f"{key}={value}" for key, value in sorted(env.items(), key=lambda item: item[0].upper())) + "\0\0")
            if not k.CreateProcessW(None, c.create_unicode_buffer(subprocess.list2cmdline(command)), None, None, False,
                                    0x80000 | 0x400, environment, str(ROOT), c.byref(startup), c.byref(process)):
                raise c.WinError(c.get_last_error())
            self.process, self.pid = process.process, process.pid
            k.CloseHandle(process.thread)
        except BaseException:
            for handle in handles:
                k.CloseHandle(handle)
            if self.console:
                k.ClosePseudoConsole(self.console)
            raise
        finally:
            if attributes:
                k.DeleteProcThreadAttributeList(attributes)
        k.CloseHandle(input_read)
        k.CloseHandle(output_write)

    def read(self, size=65536):
        import ctypes as c
        from ctypes import wintypes as w
        buffer, count = c.create_string_buffer(size), w.DWORD()
        while True:
            if not self.k.ReadFile(self.output, buffer, size, c.byref(count), None) or count.value == 0:
                tail = self.decoder.decode(b"", final=True)
                if tail:
                    return tail
                raise EOFError()
            text = self.decoder.decode(buffer.raw[:count.value])
            if text:
                return text

    def write(self, text):
        import ctypes as c
        from ctypes import wintypes as w
        data, count = text.encode("utf-8"), w.DWORD()
        while data:
            if not self.k.WriteFile(self.input, data, len(data), c.byref(count), None):
                raise c.WinError(c.get_last_error())
            data = data[count.value:]

    def isalive(self):
        import ctypes as c
        from ctypes import wintypes as w
        code = w.DWORD()
        return bool(self.process and self.k.GetExitCodeProcess(self.process, c.byref(code)) and code.value == 259)

    def setwinsize(self, rows, cols):
        if self.k.ResizePseudoConsole(self.console, self.coord(cols, rows)) < 0:
            raise RuntimeError("ResizePseudoConsole failed")

    def terminate(self, force=True):
        if self.isalive():
            self.k.TerminateProcess(self.process, 1)

    def close(self, force=True):
        if self.console:
            self.k.ClosePseudoConsole(self.console)
            self.console = None
        for name in ("input", "output", "process"):
            handle = getattr(self, name)
            if handle:
                self.k.CloseHandle(handle)
                setattr(self, name, None)
        self.closed = True


class Terminal:
    """A separate real PTY, never the user's active console or webcam session."""

    def __init__(self, command, cols, rows, capture):
        env = {**os.environ, "TERM": "xterm-256color", "COLORTERM": "truecolor", "LANG": "C.UTF-8", "LOG_LEVEL": "INFO"}
        env.pop("ASCII_CHAT_LOG_LEVEL", None)
        COMMANDS.append(command)
        if os.name == "nt":
            self.child = WindowsTerminal(command, cols, rows, env)
        else:
            from pexpect import spawn
            self.child = spawn(command[0], command[1:], cwd=str(ROOT), env=env, dimensions=(rows, cols), encoding="utf-8", codec_errors="strict", timeout=None)
        self.start = time.monotonic()
        self.capture = capture
        self.problem = None
        self.thread = threading.Thread(target=self.read, daemon=True)
        self.thread.start()

    def read(self):
        try:
            with gzip.open(self.capture, "wt", encoding="utf-8") as out:
                while True:
                    text = self.child.read(65536) if os.name == "nt" else self.child.read_nonblocking(65536, timeout=None)
                    if not text:
                        break
                    if "\ufffd" in text:
                        raise RuntimeError("Terminal capture contains replacement characters; refusing to render corrupted UTF-8")
                    event = [time.monotonic() - self.start, "out", text]
                    out.write(json.dumps(event, ensure_ascii=True) + "\n")
                    # ConPTY asks terminal capabilities; answer as a dark VT terminal.
                    if "\x1b[c" in text:
                        self.send("\x1b[?1;2c")
                    if "\x1b[6n" in text:
                        self.send("\x1b[1;1R")
                    if "\x1b]11;?" in text:
                        self.send("\x1b]11;rgb:0000/0000/0000\x1b\\")
        except (EOFError, OSError):
            pass
        except Exception as error:
            # pexpect's EOF exception is separate from the built-in EOFError.
            if type(error).__name__ != "EOF":
                self.problem = repr(error)

    def send(self, keys):
        if os.name == "nt":
            self.child.write(keys)
        else:
            self.child.send(keys)

    def resize(self, cols, rows):
        self.child.setwinsize(rows, cols)

    def wait(self, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if self.problem:
                raise RuntimeError(self.problem)
            if not self.child.isalive():
                raise RuntimeError(f"ascii-chat exited early; inspect {self.capture} and its .log file")
            time.sleep(min(.02, max(0, deadline - time.monotonic())))

    def alive(self):
        return self.child.isalive()

    def wait_log(self, path, marker, timeout=30):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if path.is_file() and marker in path.read_text(encoding="utf-8", errors="replace"):
                return time.monotonic() - self.start
            self.wait(.05)
        raise RuntimeError(f"Timed out waiting for {marker!r}; inspect {path}")

    def close(self):
        try:
            if self.alive():
                try:
                    self.send("\x03")
                except (EOFError, OSError):
                    pass
            # Keep the reader draining while native cleanup finishes.
            deadline = time.monotonic() + 10
            while self.alive() and time.monotonic() < deadline:
                self.thread.join(.1)
            if self.alive():
                self.child.terminate(force=True)
        finally:
            self.child.close(force=True)
            self.thread.join(3)
        if self.thread.is_alive():
            raise RuntimeError("Terminal reader did not finish draining its recording")
        if self.problem:
            raise RuntimeError(self.problem)


def native_command(args, name, source, splash, cols=None, rows=None):
    # The binary appends logs. A fresh file makes readiness markers unambiguous
    # when re-recording in the same work directory.
    (args.work_dir / (name + ".log")).write_text("", encoding="utf-8")
    return [str(args.binary), "--log-file", str(args.work_dir / (name + ".log")), "--log-level", "info", "mirror",
            *source, "--width", str(cols or args.cols), "--height", str(rows or args.rows), "--fps", str(FPS),
            "--utf8", "true", "--color-mode", "truecolor", "--splash-screen=" + str(splash).lower(),
            # Analysis uses decoded media before speaker gain. Silence the live
            # monitor while recording; mux the clean source soundtrack later.
            "--audio=true" if "--file" in source else "--audio=false", "--audio-source", "media", "--volume", "0"]


def record(args, main):
    path = args.work_dir / "main.cast.jsonl.gz"
    p = Terminal(native_command(args, "main", ["--file", str(args.work_dir / "performance.mkv"), "--pause"], True), args.ui_cols, args.ui_rows, path)
    evidence = {"main": [], "ui": [], "patterns": []}
    try:
        # Let initialization finish, recording all real splash frames. The edit
        # selects its cleanest animation and stretches it to the splash dwell.
        p.wait_log(args.work_dir / "main.log", "Paused (--pause flag)")
        p.wait(.15)
        evidence["splash_end"] = time.monotonic() - p.start
        p.resize(args.cols, args.rows)
        p.wait(.3)
        p.send(" ")
        evidence["play"] = p.wait_log(args.work_dir / "main.log", "@handle_keyboard_input(): Playing")
        scheduled = evidence["play"]
        for c in main:
            now = scheduled
            p.wait(max(0, now - (time.monotonic() - p.start)))
            print(f"  {now:5.1f}s  {c.name}", flush=True)
            skip_filters = c.keys.count("f") > 1
            if skip_filters:
                # Cut these paused transitions out, so skipped filters never
                # flash in the export and the soundtrack remains continuous.
                p.send(" ")
                p.wait(.06)
            for key in c.keys:
                p.send(key)
                # Distinct events allow a frame to observe adjacent toggles.
                if len(c.keys) > 1:
                    p.wait(.025)
            if skip_filters:
                p.wait(.06)
                p.send(" ")
                p.wait(.08)
                now = scheduled = time.monotonic() - p.start
            if c.name == "face-follow":
                # Explicitly load the face rather than assume a slow terminal
                # reached a particular video PTS on a wall-clock deadline. The
                # picker also clears combining marks left by Windows ConPTY.
                face_path = str(args.work_dir / "face-performance.mkv")
                p.resize(args.ui_cols, args.ui_rows)
                p.wait(.2)
                p.send("o")
                p.wait(.15)
                p.send("\x1b[200~" + face_path + "\x1b[201~")
                p.wait(.1)
                p.send("\r")
                p.wait_log(args.work_dir / "main.log", "Opened media: " + face_path)
                p.resize(args.cols, args.rows)
                p.wait(.25)
                now = scheduled = time.monotonic() - p.start
            evidence["main"].append({**asdict(c), "start": now, "end": now + c.seconds, "cols": args.cols, "rows": args.rows, "cell_scale": args.art_cell_scale})
            p.wait(max(0, c.seconds - (time.monotonic() - p.start - now)))
            scheduled += c.seconds
        p.send("4")
        p.wait(.15)
        outro = tui_chapters(args.segment_duration_scale)[-1]
        start = time.monotonic() - p.start
        evidence["main"].append({**asdict(outro), "start": start, "end": start + outro.seconds,
                                 "cols": args.cols, "rows": args.rows, "cell_scale": args.art_cell_scale})
        p.wait(outro.seconds)
    finally:
        p.close()
    # The native TUIs need more than 26 columns. A separate full-grid capture
    # preserves readable UI text while every media chapter keeps large glyphs.
    p = Terminal(native_command(args, "ui", ["--file", str(args.work_dir / "face-performance.mkv"), "--pause"], False,
                                args.ui_cols, args.ui_rows), args.ui_cols, args.ui_rows, args.work_dir / "ui.cast.jsonl.gz")
    try:
        p.wait_log(args.work_dir / "ui.log", "Paused (--pause flag)")
        p.send(" ")
        p.wait_log(args.work_dir / "ui.log", "@handle_keyboard_input(): Playing")
        for c in tui_chapters(args.segment_duration_scale)[:-1]:
            if c.resize:
                p.resize(*c.resize)
            for key in c.keys:
                p.send(key)
                p.wait(.08)
            # Cut input/resize propagation out of the final montage.
            p.wait(.3)
            start = time.monotonic() - p.start
            evidence["ui"].append({**asdict(c), "start": start, "end": start + c.seconds,
                                   "cols": c.resize[0] if c.resize else args.ui_cols,
                                   "rows": c.resize[1] if c.resize else args.ui_rows, "cell_scale": 1})
            print(f"  {start:5.1f}s  {c.name}", flush=True)
            p.wait(c.seconds)
        p.resize(args.ui_cols, args.ui_rows)
        p.wait(.4)
        p.send("?")
        p.wait(.15)
    finally:
        p.close()
    for pattern in (0, 1):
        name = f"pattern-{pattern}"
        p = Terminal(native_command(args, name, [f"--test-pattern={pattern}"], False), args.cols, args.rows, args.work_dir / (name + ".cast.jsonl.gz"))
        try:
            p.wait_log(args.work_dir / (name + ".log"), "Pipeline created successfully, running main loop")
            p.wait(.3)
            start = time.monotonic() - p.start
            seconds = 6 * args.segment_duration_scale
            p.wait(seconds)
            evidence["patterns"].append({"name": name, "start": start, "end": start + seconds, "seconds": seconds,
                                         "label": f"--test-pattern={pattern}", "cols": args.cols, "rows": args.rows, "cell_scale": args.art_cell_scale})
        finally:
            p.close()
    return evidence


def find_font(args):
    choices = [args.font] if args.font else []
    choices += [Path(x) for x in (
        "C:/Windows/Fonts/JetBrainsMonoNerdFont-Regular.ttf", "C:/Windows/Fonts/consola.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", "/System/Library/Fonts/Menlo.ttc")]
    for path in choices:
        if path and path.is_file():
            return path
    raise ValueError("No monospaced font found; pass --font /path/to/font.ttf")


class Rasterizer:
    def __init__(self, args):
        from PIL import Image, ImageDraw, ImageFont
        self.Image, self.ImageDraw = Image, ImageDraw
        self.ImageFont = ImageFont
        self.args = args
        self.font_path = find_font(args)
        self.font = ImageFont.truetype(str(self.font_path), 13)
        # JetBrains Mono Nerd Font lacks several Zalgo combining marks.
        # Pillow does not perform the font fallback a real terminal provides.
        marks_font = Path("C:/Windows/Fonts/consola.ttf")
        self.marks_path = marks_font if os.name == "nt" and marks_font.is_file() else self.font_path
        self.marks_font = ImageFont.truetype(str(self.marks_path), 13)
        self.label_font = ImageFont.truetype(str(find_font(args)), 22)
        self.colors = {"default": "#dedede", "black": "#0c0c0c", "red": "#e06c75", "green": "#98c379", "brown": "#e5c07b", "blue": "#61afef", "magenta": "#c678dd", "cyan": "#56b6c2", "white": "#dcdfe4", "brightblack": "#676e7b", "brightred": "#ff757f", "brightgreen": "#b9e68c", "brightbrown": "#ffd68a", "brightblue": "#82baff", "brightmagenta": "#e3a1f6", "brightcyan": "#83e2ea", "brightwhite": "#ffffff"}

    def color(self, value, background=False):
        if value == "default" and background:
            return "#0c0c0c"
        return "#" + value if re.fullmatch("[0-9a-fA-F]{6}", value) else self.colors.get(value, "#dedede")

    @functools.lru_cache(maxsize=4)
    def fonts(self, scale):
        return (self.ImageFont.truetype(str(self.font_path), 13 * scale),
                self.ImageFont.truetype(str(self.marks_path), 13 * scale))

    @functools.lru_cache(maxsize=8192)
    def mask(self, char, scale=1):
        # Extra vertical room retains Zalgo's combining marks across cell rows.
        mask = self.Image.new("L", (CELL_W * 2 * scale, CELL_H * 3 * scale))
        font, marks_font = self.fonts(scale)
        draw = self.ImageDraw.Draw(mask)
        decomposed = unicodedata.normalize("NFD", char)
        base = "".join(c for c in decomposed if not unicodedata.combining(c))
        baseline = (CELL_H + 13) * scale
        draw.text((0, baseline), base, font=font, anchor="ls", fill=255)
        above = below = 0
        for mark in decomposed:
            kind = unicodedata.combining(mark)
            if not kind:
                continue
            # Draw marks independently: BASIC Pillow layout advances combining
            # glyphs and switching the entire cell font moves its baseline.
            box = draw.textbbox((0, baseline), mark, font=marks_font, anchor="ls")
            x = (CELL_W * scale - (box[2] - box[0])) // 2 - box[0]
            shift = 0
            if kind == 230:
                shift = baseline - 10 * scale - above * 2 * scale - box[3]
                above += 1
            elif kind == 220:
                shift = baseline + scale + below * 2 * scale - box[1]
                below += 1
            draw.text((x, baseline + shift), mark, font=marks_font, anchor="ls", fill=255)
        return mask

    def draw(self, screen, label, cell_scale=1):
        args = self.args
        image = self.Image.new("RGB", (args.width, args.height), "#0c0c0c")
        draw = self.ImageDraw.Draw(image)
        cell_w, cell_h = CELL_W * cell_scale, CELL_H * cell_scale
        dx, dy = (args.width - screen.columns * cell_w) // 2, (args.height - screen.lines * cell_h) // 2
        glyphs = []
        for y, row in screen.buffer.items():
            for x, char in row.items():
                fg, bg = self.color(char.fg), self.color(char.bg, True)
                if char.reverse:
                    fg, bg = bg, fg
                px, py = dx + x * cell_w, dy + y * cell_h
                if bg != "#0c0c0c":
                    draw.rectangle((px, py, px + cell_w - 1, py + cell_h - 1), fill=bg)
                if char.data.strip():
                    glyphs.append((px, py, char.data, fg))
        for px, py, char, fg in glyphs:
            mask = self.mask(char, cell_scale)
            image.paste(fg, (px, py - cell_h, px + cell_w * 2, py + cell_h * 2), mask)
        if args.opt_text_overlay and label:
            box = draw.textbbox((0, 0), label, font=self.label_font)
            w, h = box[2] - box[0], box[3] - box[1]
            x, y = (args.width - w) // 2, args.height - h - 25
            draw.rounded_rectangle((x - 18, y - 10, x + w + 18, y + h + 12), radius=9, fill="#101923", outline="#42596d", width=1)
            draw.text((x, y - box[1]), label, font=self.label_font, fill="#f1f6fb")
        return image


def events(path):
    with gzip.open(path, "rt", encoding="utf-8") as stream:
        for line in stream:
            yield json.loads(line)


class TerminalReplay:
    """Commit synchronized VT updates atomically, avoiding half-drawn frames."""

    def __init__(self, stream):
        self.stream = stream
        self.pending = ""
        self.frame = None

    def feed(self, text):
        self.pending += text
        changed = False
        while True:
            match = re.search(r"\x1b\[\?2026([hl])", self.pending)
            if not match:
                # Retain only a possible synchronized-update marker prefix.
                # Keeping arbitrary trailing text truncates static TUIs that
                # emit nothing further until their next input event.
                markers = ("\x1b[?2026h", "\x1b[?2026l")
                suffix = max((n for n in range(1, min(len(self.pending), 8) + 1)
                              if any(marker.startswith(self.pending[-n:]) for marker in markers)), default=0)
                complete = self.pending[:-suffix] if suffix else self.pending
                self.pending = self.pending[-suffix:] if suffix else ""
                if self.frame is not None:
                    self.frame += complete
                elif complete:
                    self.stream.feed(complete)
                    changed = True
                return changed
            before, self.pending = self.pending[:match.start()], self.pending[match.end():]
            if self.frame is not None:
                self.frame += before
            elif before:
                self.stream.feed(before)
                changed = True
            if match[1] == "h":
                self.frame = ""
            elif self.frame is not None:
                self.stream.feed(self.frame)
                self.frame = None
                changed = True


def replay_terminal(cols, rows):
    """pyte omits ECMA-48 REP, which native ASCII RLE uses for long runs."""
    import pyte
    from wcwidth import wcwidth

    class RecordingScreen(pyte.Screen):
        def reset(self):
            super().reset()
            self.last_graphic = ""

        def draw(self, data):
            # pyte wraps before testing character width. A combining mark
            # following the rightmost cell must stay on that cell, preserving
            # pending auto-wrap until the next spacing character.
            if data.isascii():
                super().draw(data)
                if data:
                    self.last_graphic = data[-1]
                return
            for char in data:
                if (wcwidth(char) == 0 and unicodedata.combining(char) and self.cursor.x == self.columns):
                    line = self.buffer[self.cursor.y]
                    last = line[self.columns - 1]
                    line[self.columns - 1] = last._replace(data=unicodedata.normalize("NFC", last.data + char))
                    self.dirty.add(self.cursor.y)
                else:
                    super().draw(char)
                if wcwidth(char) > 0:
                    self.last_graphic = char
                elif self.last_graphic and (unicodedata.combining(char) or char == "\ufe0f"):
                    self.last_graphic += char

        def repeat_character(self, count=1):
            if self.last_graphic:
                self.draw(self.last_graphic * max(1, count))

    class RecordingStream(pyte.Stream):
        csi = {**pyte.Stream.csi, "b": "repeat_character"}
        events = pyte.Stream.events | {"repeat_character"}

    screen = RecordingScreen(cols, rows)
    return screen, TerminalReplay(RecordingStream(screen))


def verify_zalgo_capture(args, evidence):
    """Check every complete decorated frame, including intermittent failures."""
    chapter = next(c for c in evidence["main"] if c["name"] == "zalgo")
    pending, frame, checked = "", None, 0
    strip = lambda value: "".join(c for c in unicodedata.normalize("NFD", value) if not unicodedata.combining(c))
    for when, _, text in events(args.work_dir / "main.cast.jsonl.gz"):
        if when < chapter["start"] - .5:
            continue
        if when > chapter["end"]:
            break
        if "\ufffd" in text:
            raise RuntimeError("Zalgo capture contains replacement characters")
        pending += text
        while (match := re.search(r"\x1b\[\?2026([hl])", pending)):
            before, pending = pending[:match.start()], pending[match.end():]
            if frame is not None:
                frame += before
            if match[1] == "h":
                frame = ""
            elif frame is not None:
                if when >= chapter["start"] and any(unicodedata.combining(c) for c in frame):
                    expected = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", frame).splitlines()
                    if len(expected) != args.rows or any(len(strip(row)) != args.cols for row in expected):
                        raise RuntimeError(f"Zalgo source rows wrap at {when:.3f}s; inspect terminal capture")
                    screen, stream = replay_terminal(args.cols, args.rows)
                    stream.feed("\x1b[?2026h" + frame + "\x1b[?2026l")
                    if any(strip(actual) != strip(wanted) for actual, wanted in zip(screen.display, expected)):
                        raise RuntimeError(f"Zalgo replay moved base glyph rows at {when:.3f}s")
                    checked += 1
                frame = None
    if not checked:
        raise RuntimeError("No complete decorated Zalgo frames available to verify")
    result = {"decorated_frames_checked": checked, "columns": args.cols, "rows": args.rows,
              "replacement_characters": 0, "misplaced_rows": 0}
    (args.work_dir / "zalgo-verification.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(f"Verified {checked} Zalgo frames: complete rows and stable base glyph positions", flush=True)
    return result


def render_capture(args, name, selected, raster):
    screen, stream = replay_terminal(args.cols, args.rows)
    iterator = iter(events(args.work_dir / (name + ".cast.jsonl.gz")))
    pending = next(iterator, None)
    outputs = []
    elapsed = 0.0
    for chapter in selected:
        print("Rendering " + chapter["name"] + "...", flush=True)
        target_cols, target_rows = chapter["cols"], chapter["rows"]
        if screen.columns != target_cols or screen.lines != target_rows:
            screen.resize(lines=target_rows, columns=target_cols)
        file = args.work_dir / (chapter["name"] + ".mp4")
        command = [args.ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pixel_format", "rgb24",
                   "-video_size", f"{args.width}x{args.height}", "-framerate", str(FPS), "-i", "pipe:0", "-an", "-c:v", "libx264",
                   "-preset", "ultrafast", "-threads", "2", "-crf", "14", "-pix_fmt", "yuv420p", str(file)]
        COMMANDS.append(command)
        encoder = subprocess.Popen(command, stdin=subprocess.PIPE)
        # Quantize chapter boundaries cumulatively so fractional dwell times
        # do not accumulate a frame of timing error at every transition.
        count = max(1, round((elapsed + chapter["seconds"]) * FPS) - round(elapsed * FPS))
        elapsed += chapter["seconds"]
        last_image = None
        try:
            for index in range(count):
                when = chapter["start"] + index / FPS * ((chapter["end"] - chapter["start"]) / chapter["seconds"])
                changed = False
                while pending is not None and pending[0] <= when:
                    changed = stream.feed(pending[2]) or changed
                    pending = next(iterator, None)
                if changed or last_image is None:
                    last_image = raster.draw(screen, chapter["label"], chapter.get("cell_scale", 1))
                encoder.stdin.write(last_image.tobytes())
                if index == count // 2:
                    if chapter["name"] == "outro" and any("ascii-chat Keyboard Shortcuts" in line for line in screen.display):
                        raise RuntimeError("Outro still shows keyboard help; inspect the recording's resize/input timing")
                    last_image.save(args.work_dir / (chapter["name"] + ".png"))
                    (args.work_dir / (chapter["name"] + ".txt")).write_text("\n".join(screen.display), encoding="utf-8")
        finally:
            encoder.stdin.close()
            code = encoder.wait()
        if code:
            raise RuntimeError(f"FFmpeg encoder failed for {file} with exit {code}")
        outputs.append(file)
    return outputs


def fingerprint(args):
    # Encoding-only options can change while reusing the exact same recording.
    return {"recipe": 7, "segment_duration_scale": args.segment_duration_scale, "binary": str(args.binary), "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
            "dll_sha256": hashlib.sha256(args.binary.with_name("asciichat.dll").read_bytes()).hexdigest() if os.name == "nt" else None,
            "media": str(args.media_file), "media_sha256": hashlib.sha256(args.media_file.read_bytes()).hexdigest(),
            "face": str(args.face_file), "face_sha256": hashlib.sha256(args.face_file.read_bytes()).hexdigest() if args.face_file else FACE_URL,
            "start": args.media_file_start, "cols": args.cols, "rows": args.rows, "ui_cols": args.ui_cols, "ui_rows": args.ui_rows, "art_cell_scale": args.art_cell_scale,
            "chapters": [asdict(c) for c in chapters(args.segment_duration_scale) + tui_chapters(args.segment_duration_scale)]}


def audio_quality(args):
    command = [args.ffmpeg, "-hide_banner", "-nostats", "-i", str(args.output), "-vn", "-af", "astats", "-f", "null", "-"]
    COMMANDS.append(command)
    result = subprocess.run(command, capture_output=True, text=True, check=True)
    (args.work_dir / "audio-quality.txt").write_text(result.stderr, encoding="utf-8")
    overall = result.stderr.rsplit("Overall", 1)[-1]
    peak = re.search(r"Peak level dB: ([-+\d.]+)", overall)
    rms = re.search(r"RMS level dB: ([-+\d.]+)", overall)
    if not peak or not rms or float(peak[1]) >= 0 or float(rms[1]) < -50:
        raise RuntimeError("Encoded soundtrack is clipped or silent; inspect audio-quality.txt")
    return {"peak_dbfs": float(peak[1]), "rms_dbfs": float(rms[1])}


def assemble(args, files, evidence):
    concat = args.work_dir / "concat.txt"
    # Relative names avoid FFmpeg's platform-specific escaping of drive paths.
    concat.write_text("".join(f"file '{p.name}'\n" for p in files), encoding="utf-8")
    length = sum(float(probe(p, args)["format"]["duration"]) for p in files)
    # Silence under splash, then the same normal-speed music used by the
    # native audio visualizer. Music continues smoothly beneath patterns/TUIs.
    video_filter = f"setpts=(PTS-STARTPTS)/{args.speed},fps={args.output_fps}"
    graph = (f"[0:v]{video_filter}[v];"
             f"[1:a]adelay={round(3000 * args.segment_duration_scale)}:all=1,apad,atrim=duration={length},asetpts=PTS-STARTPTS,"
             f"{atempo(args.speed)},alimiter=limit=0.84:level=false:latency=true,afade=t=out:st={max(0, length / args.speed - 1.5)}:d=1.5[a]")
    encode = ["-preset", "slow" if args.optimize else "fast", "-crf", "20" if args.optimize else "16"]
    audio_rate, sample_rate = ("160k" if args.optimize else "192k"), "48000"
    if args.optimize and args.target_size_mb:
        bitrate = int(args.target_size_mb * 1e6 * .97 * 8 / (length / args.speed)) - 96000
        if bitrate < 100000:
            raise ValueError("Size budget is too small for this duration; use a larger --target-size-mb")
        pass_log = args.work_dir / ("size-pass-" + args.output.stem)
        preset = "slow" if args.optimize else "fast"
        print(f"Two-pass size target: {args.target_size_mb} MB / video {bitrate / 1000:.0f}k / audio 96k / {args.output_fps:g} fps", flush=True)
        run([args.ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "concat", "-safe", "0", "-i", concat,
             "-vf", video_filter, "-an", "-c:v", "libx264", "-threads", "4", "-preset", preset, "-pix_fmt", "yuv420p",
             "-b:v", bitrate, "-pass", "1", "-passlogfile", pass_log, "-f", "null", os.devnull])
        encode = ["-preset", preset, "-b:v", bitrate, "-pass", "2", "-passlogfile", pass_log]
        audio_rate, sample_rate = "96k", "44100"
    run([args.ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-filter_complex_threads", "2", "-f", "concat", "-safe", "0", "-i", concat,
         "-i", args.work_dir / "performance.mkv", "-filter_complex", graph, "-map", "[v]", "-map", "[a]",
         "-c:v", "libx264", *encode,
         "-threads", "4", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", audio_rate, "-ar", sample_rate,
         "-movflags", "+faststart", "-t", f"{length / args.speed:.8f}", args.output])
    result = probe(args.output, args)
    video = next(s for s in result["streams"] if s["codec_type"] == "video")
    audio = next(s for s in result["streams"] if s["codec_type"] == "audio")
    if not math.isclose(float(Fraction(video["avg_frame_rate"])), args.output_fps) or audio["codec_name"] != "aac":
        raise RuntimeError("Output verification failed: expected requested frame rate and AAC audio")
    if args.optimize and args.target_size_mb and int(result["format"]["size"]) > args.target_size_mb * 1e6:
        raise RuntimeError("Encoder exceeded requested size budget; inspect manifest/encode logs")
    quality = audio_quality(args)
    manifest = {"options": {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
                "output_probe": result, "recording": evidence, "face_source": FACE_URL,
                "commands": COMMANDS, "audio_quality": quality, "raster_version": 7, "chapters": [p.stem for p in files]}
    (args.work_dir / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    (args.work_dir / (args.output.stem + ".manifest.json")).write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"\nReady: {args.output}\n{result['format']['duration']} seconds / {args.output_fps:g} fps / {int(result['format']['size']) / 1e6:.2f} MB", flush=True)
    print(f"Chapter images, text, logs, recordings and manifest: {args.work_dir}", flush=True)


def main():
    args = parser().parse_args()
    if not args.optimize:
        args.target_size_mb = None
    if args.output_fps > FPS:
        parser().error("--output-fps cannot exceed the 33 fps capture rate")
    if args.width < 688 or args.height < 448 or args.width % 2 or args.height % 2:
        parser().error("use even pixel dimensions of at least 688x448 so the terminal has at least 86x28 cells")
    if args.output.suffix.lower() != ".mp4":
        parser().error("--output must end in .mp4 (GitHub-compatible H.264/AAC)")
    args.ui_cols, args.ui_rows = args.width // CELL_W, args.height // CELL_H
    args.cols, args.rows = args.ui_cols // args.art_cell_scale, args.ui_rows // args.art_cell_scale
    if args.cols < 20 or args.rows < 10:
        parser().error("native art requires at least 20 columns and 10 rows; increase pixel dimensions or reduce --art-cell-scale")
    main_chapters = chapters(args.segment_duration_scale)
    if args.dry_run:
        print(json.dumps({"fps": FPS, "output_fps": args.output_fps, "target_size_mb": args.target_size_mb, "terminal": [args.cols, args.rows], "output_pixels": [args.width, args.height],
                          "ui_terminal": [args.ui_cols, args.ui_rows], "art_cell_scale": args.art_cell_scale,
                          "test_patterns": [{"name": f"pattern-{i}", "seconds": 6 * args.segment_duration_scale} for i in (0, 1)],
                          "speed": args.speed, "optimize": args.optimize, "overlay": args.opt_text_overlay,
                          "media_start": args.media_file_start, "chapters": [asdict(c) for c in main_chapters + tui_chapters(args.segment_duration_scale)],
                          "seconds": (15 * args.segment_duration_scale + sum(c.seconds for c in main_chapters + tui_chapters(args.segment_duration_scale))) / args.speed}, indent=2))
        return
    for name in ("binary", "media_file", "work_dir", "output", "face_file", "font"):
        if getattr(args, name) is not None:
            setattr(args, name, getattr(args, name).resolve())
    for name in ("binary", "media_file"):
        if not getattr(args, name).is_file():
            raise ValueError(f"Missing --{name.replace('_', '-')}: {getattr(args, name)}")
    if args.binary == args.output or args.media_file == args.output or args.face_file == args.output:
        raise ValueError("--output must not overwrite an input")
    args.work_dir.mkdir(parents=True, exist_ok=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for program in (args.ffmpeg, args.ffprobe):
        if not shutil.which(program):
            raise ValueError(f"Executable not found: {program}")
    import pyte  # noqa: F401 -- fail before recording if dependencies are absent
    raster = Rasterizer(args)
    identity = fingerprint(args)
    evidence_file = args.work_dir / "recording.json"
    if args.reuse_capture:
        saved = json.loads(evidence_file.read_text(encoding="utf-8"))
        if saved["fingerprint"] != json.loads(json.dumps(identity)):
            raise ValueError("Saved recording settings do not match; omit --reuse-capture or choose its original binary/media/dimensions/start")
        evidence = saved["evidence"]
    else:
        prepare(args, main_chapters)
        evidence = record(args, main_chapters)
        evidence_file.write_text(json.dumps({"fingerprint": identity, "evidence": evidence}, indent=2), encoding="utf-8")
    verify_zalgo_capture(args, evidence)
    # Find clean splash frames from its actual output, before normal media.
    # The logo's first box-drawing output follows initialization diagnostics.
    splash_times = [t for t, _, text in events(args.work_dir / "main.cast.jsonl.gz")
                    if t < evidence["play"] and "Video chat in your terminal" in text]
    if len(splash_times) < 2:
        raise RuntimeError("No complete rainbow splash captured; inspect main.cast.jsonl.gz")
    splash_start, splash_end = splash_times[0], splash_times[-1]
    splash = {"name": "splash", "label": "ascii-chat / terminal video, reimagined", "seconds": 3 * args.segment_duration_scale,
              "start": splash_start + .03, "end": splash_end, "cols": args.ui_cols, "rows": args.ui_rows, "cell_scale": 1}
    selected = [splash] + evidence["main"]
    insert = 1 + len(main_chapters)
    expected_names = [c["name"] for c in selected]
    expected_names[insert:insert] = [c["name"] for c in evidence["patterns"] + evidence["ui"]]
    files = None
    manifest_path = args.work_dir / "manifest.json"
    if args.reuse_capture and manifest_path.is_file():
        previous = json.loads(manifest_path.read_text(encoding="utf-8"))
        raster_options = ("width", "height", "cols", "rows", "ui_cols", "ui_rows", "art_cell_scale", "font", "opt_text_overlay")
        current = {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()}
        candidates = [args.work_dir / (name + ".mp4") for name in expected_names]
        if (previous.get("raster_version") == 7 and previous["recording"] == evidence and previous["chapters"] == expected_names and
                all(previous["options"].get(k) == current[k] for k in raster_options) and
                all(p.is_file() for p in candidates)):
            print("Reusing rendered chapters; applying export settings...", flush=True)
            files = candidates
    if files is None:
        files = render_capture(args, "main", selected, raster)
        patterns = []
        for c in evidence["patterns"]:
            patterns += render_capture(args, c["name"], [c], raster)
        patterns += render_capture(args, "ui", evidence["ui"], raster)
        files[insert:insert] = patterns
    assemble(args, files, evidence)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(f"Error: {error}")
