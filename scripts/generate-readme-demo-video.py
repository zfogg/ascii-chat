#!/usr/bin/env python3
"""Record a real, keyboard-driven ascii-chat session and export a README MP4.

Dependencies: Python 3.10+, Pillow, pyte, FFmpeg/ffprobe, and pywinpty on Windows
or pexpect on Unix. Install Python packages with:
    python -m pip install Pillow pyte pywinpty   # Windows
    python -m pip install Pillow pyte pexpect    # Linux/macOS

The terminal recording is retained alongside logs, sampled screen text, chapter
images, commands, and timings in --work-dir. No desktop/camera recording or
GitHub upload occurs. The source media is rendered by the actual native binary;
Pillow only rasterizes its terminal output. Requires a debug build for 0/= TUIs
and the 4/z shortcuts (face follow / Zalgo) for uninterrupted effect changes.
"""

from __future__ import annotations

import argparse
import functools
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

The opening plays 3x faster for 3 seconds, then returns to normal speed.
--speed retimes the completed movie (including music and every chapter).
Dimensions are output pixels: the terminal gets floor(width/8) columns and
floor(height/16) rows, centered in the output. 1280x720 gives 160x45 cells.
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
    p.add_argument("--width", type=int, default=1280, help="output width in pixels; also determines CLI columns")
    p.add_argument("--height", type=int, default=720, help="output height in pixels; also determines CLI rows")
    p.add_argument("--speed", type=positive, default=1.0, help="whole-movie playback rate; 1.1 is 10%% faster")
    p.add_argument("--opt-text-overlay", type=boolean, nargs="?", const=True, default=False, metavar="BOOL", help="label featured flags/keys at bottom center")
    p.add_argument("--optimize", type=boolean, nargs="?", const=True, default=True, metavar="BOOL", help="FFmpeg slow CRF 20 / AAC 160k; false uses CRF 16 / AAC 192k")
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


def chapters():
    result = [
        Chapter("fast", 3, "Default rendering / media 3x"),
        Chapter("normal", 4, "Default rendering / media 1x"),
        Chapter("flip-x", .75, "--flip-x", "x"),
        Chapter("flip-xy", .75, "--flip-x --flip-y", "y"),
        Chapter("flip-y", .75, "--flip-y", "x"),
        Chapter("unflip", .75, "Default rendering", "y"),
    ]
    filters = ("black", "white", "green", "magenta", "fuchsia", "orange", "teal", "cyan", "pink", "red", "yellow", "rainbow")
    for i, color in enumerate(filters):
        result.append(Chapter("filter-" + color, 3 / len(filters), "--color-filter " + color, "f"))
    result += [
        Chapter("waveform", 2, "--waveform --audio-source media", "f2"),
        Chapter("fft", 2, "--fft --audio-source media", "23"),
        Chapter("foreground", 1, "--render-mode foreground", "3"),
        Chapter("background", 1, "--render-mode background", "r"),
        Chapter("half-block", 1, "--render-mode half-block", "r"),
        Chapter("matrix", 2, "--matrix", "r1"),
        Chapter("zalgo", 2, "--zalgo 8", "1z"),
        Chapter("face-follow", 3, "--face-follow", "z4"),
    ]
    return result


def tui_chapters():
    return [
        Chapter("open-media", 3, '"o" / Drop, paste, or open media', "o"),
        Chapter("logs", 3, '"]" / Session logs', "\x1b]"),
        Chapter("statistics", 3, '"=" / Live statistics', "]="),
        Chapter("sync", 3, '"0" / C sync primitives and deadlocks', "=0"),
        Chapter("help", 3, '"?" / Keyboard help', "0?"),
        Chapter("too-small", 3, "Responsive TUI / resize to continue", resize=(58, 12)),
        Chapter("outro", 2, "ascii-chat / video calls in your terminal", "?", resize=None),
    ]


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
    music_length = length + 6 + sum(c.seconds for c in tui_chapters())
    required = args.media_file_start + 9 + music_length - 3
    if float(info["format"]["duration"]) < required:
        raise ValueError(f"Media is too short after --media-file-start; need through {required:.2f} seconds")
    face_start = length - 3
    # All dog-video effects share one uninterrupted native capture. The face
    # source is opened through the real media picker after that performance.
    graph = (
        "[0:v]split=2[v0][v1];[0:a]asplit=2[a0][a1];"
        "[v0]trim=duration=9,setpts=(PTS-STARTPTS)/3,fps=33,scale=640:360,setsar=1[vfast];"
        "[v1]trim=start=9,setpts=PTS-STARTPTS,fps=33,scale=640:360,setsar=1[vnormal];"
        "[a0]atrim=duration=9,asetpts=PTS-STARTPTS,atempo=1.5,atempo=2[afast];"
        "[a1]atrim=start=9,asetpts=PTS-STARTPTS[anormal];"
        "[vfast][afast][vnormal][anormal]concat=n=2:v=1:a=1[vall][aall];"
        f"[vall]trim=duration={music_length},setpts=PTS-STARTPTS[v];"
        # Stereo-to-mono analysis can add 3 dB on correlated channels. Keep
        # headroom through time stretching, downmixing, and final AAC encoding.
        f"[aall]atrim=duration={music_length},asetpts=PTS-STARTPTS,volume=0.5[a]"
    )
    run([args.ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-filter_complex_threads", "2", "-ss", args.media_file_start, "-i", args.media_file,
         "-i", face, "-filter_complex", graph, "-map", "[v]", "-map", "[a]", "-c:v", "libx264", "-preset", "fast",
         "-threads", "4", "-crf", "16", "-pix_fmt", "yuv420p", "-c:a", "pcm_f32le", work / "performance.mkv"])
    run([args.ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-filter_complex_threads", "2", "-i", face,
         "-ss", face_start, "-i", work / "performance.mkv", "-filter_complex",
         "[0:v]trim=duration=3,setpts=PTS-STARTPTS,fps=33,scale=640:360:force_original_aspect_ratio=decrease,"
         "pad=640:360:(ow-iw)/2:(oh-ih)/2,setsar=1,tpad=stop_mode=clone:stop_duration=30[v]",
         "-map", "[v]", "-map", "1:a", "-c:v", "libx264", "-preset", "fast", "-threads", "2", "-crf", "16",
         "-pix_fmt", "yuv420p", "-c:a", "pcm_f32le", "-shortest", work / "face-performance.mkv"])
    return face


class Terminal:
    """A separate real PTY, never the user's active console or webcam session."""

    def __init__(self, command, cols, rows, capture):
        env = {**os.environ, "TERM": "xterm-256color", "COLORTERM": "truecolor", "LANG": "C.UTF-8", "LOG_LEVEL": "INFO"}
        env.pop("ASCII_CHAT_LOG_LEVEL", None)
        COMMANDS.append(command)
        if os.name == "nt":
            from winpty import PtyProcess
        else:
            from pexpect import spawn
            PtyProcess = None
        self.child = (PtyProcess.spawn(command, cwd=str(ROOT), env=env, dimensions=(rows, cols)) if PtyProcess else
                      spawn(command[0], command[1:], cwd=str(ROOT), env=env, dimensions=(rows, cols), encoding="utf-8", codec_errors="replace", timeout=None))
        self.start = time.monotonic()
        self.process_handle = None
        if os.name == "nt":
            import ctypes
            self.kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            self.kernel32.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
            self.kernel32.OpenProcess.restype = ctypes.c_void_p
            self.kernel32.GetExitCodeProcess.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)]
            self.kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
            self.process_handle = self.kernel32.OpenProcess(0x1000, False, self.child.pid)
            if not self.process_handle:
                raise ctypes.WinError(ctypes.get_last_error())
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
        if self.process_handle:
            import ctypes
            code = ctypes.c_ulong()
            if not self.kernel32.GetExitCodeProcess(self.process_handle, ctypes.byref(code)):
                raise ctypes.WinError(ctypes.get_last_error())
            return code.value == 259  # STILL_ACTIVE
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
            # Keep draining output until native cleanup completes. Closing the
            # ConPTY pipe first races pywinpty's later process signal on Windows.
            deadline = time.monotonic() + 10
            while self.alive() and time.monotonic() < deadline:
                self.thread.join(.1)
        finally:
            if self.alive():
                try:
                    self.child.terminate(force=True)
                except OSError:
                    # pywinpty closes the pipe before signaling the process;
                    # it can exit between that close and os.kill on Windows.
                    if self.alive():
                        raise
            # pywinpty can report its proxy alive after the actual process has
            # exited. Do not signal an already terminated Windows process.
            if not self.alive():
                self.child.closed = True
            self.child.close(force=True)
            self.thread.join(3)
            if self.process_handle:
                self.kernel32.CloseHandle(self.process_handle)
                self.process_handle = None


def native_command(args, name, source, splash):
    # The binary appends logs. A fresh file makes readiness markers unambiguous
    # when re-recording in the same work directory.
    (args.work_dir / (name + ".log")).write_text("", encoding="utf-8")
    return [str(args.binary), "--log-file", str(args.work_dir / (name + ".log")), "--log-level", "info", "mirror",
            *source, "--width", str(args.cols), "--height", str(args.rows), "--fps", str(FPS),
            "--utf8", "true", "--color-mode", "truecolor", "--splash-screen=" + str(splash).lower(),
            # Analysis uses decoded media before speaker gain. Silence the live
            # monitor while recording; mux the clean source soundtrack later.
            "--audio=true" if "--file" in source else "--audio=false", "--audio-source", "media", "--volume", "0"]


def record(args, main):
    path = args.work_dir / "main.cast.jsonl.gz"
    p = Terminal(native_command(args, "main", ["--file", str(args.work_dir / "performance.mkv"), "--pause"], True), args.cols, args.rows, path)
    evidence = {"main": [], "patterns": []}
    try:
        # Let initialization finish, recording all real splash frames. The edit
        # selects its cleanest two seconds and stretches that animation to 3s.
        p.wait_log(args.work_dir / "main.log", "Paused (--pause flag)")
        p.wait(.15)
        evidence["splash_end"] = time.monotonic() - p.start
        p.send(" ")
        evidence["play"] = p.wait_log(args.work_dir / "main.log", "@handle_keyboard_input(): Playing")
        scheduled = evidence["play"]
        for c in main:
            now = scheduled
            p.wait(max(0, now - (time.monotonic() - p.start)))
            print(f"  {now:5.1f}s  {c.name}", flush=True)
            for key in c.keys:
                p.send(key)
                # Distinct events allow a frame to observe adjacent toggles.
                if len(c.keys) > 1:
                    p.wait(.025)
            if c.name == "face-follow":
                # Explicitly load the face rather than assume a slow terminal
                # reached a particular video PTS on a wall-clock deadline. The
                # picker also clears combining marks left by Windows ConPTY.
                face_path = str(args.work_dir / "face-performance.mkv")
                p.send("o")
                p.wait(.15)
                p.send("\x1b[200~" + face_path + "\x1b[201~")
                p.wait(.1)
                p.send("\r")
                p.wait_log(args.work_dir / "main.log", "Opened media: " + face_path)
                p.wait(.25)
                now = scheduled = time.monotonic() - p.start
            evidence["main"].append({**asdict(c), "start": now, "end": now + c.seconds, "cols": args.cols, "rows": args.rows})
            p.wait(max(0, c.seconds - (time.monotonic() - p.start - now)))
            scheduled += c.seconds
        p.send("4")
        for c in tui_chapters():
            if c.name == "outro":
                p.resize(args.cols, args.rows)
            if c.resize:
                p.resize(*c.resize)
            for key in c.keys:
                p.send(key)
                p.wait(.08)
            # Cut input/resize propagation out of the final montage.
            p.wait(.3)
            start = time.monotonic() - p.start
            evidence["main"].append({**asdict(c), "start": start, "end": start + c.seconds,
                                     "cols": c.resize[0] if c.resize else args.cols,
                                     "rows": c.resize[1] if c.resize else args.rows})
            print(f"  {start:5.1f}s  {c.name}", flush=True)
            p.wait(c.seconds)
    finally:
        p.close()
    for pattern in (0, 1):
        name = f"pattern-{pattern}"
        p = Terminal(native_command(args, name, [f"--test-pattern={pattern}"], False), args.cols, args.rows, args.work_dir / (name + ".cast.jsonl.gz"))
        try:
            p.wait_log(args.work_dir / (name + ".log"), "Pipeline created successfully, running main loop")
            p.wait(.3)
            start = time.monotonic() - p.start
            p.wait(3)
            evidence["patterns"].append({"name": name, "start": start, "end": start + 3, "seconds": 3,
                                         "label": f"--test-pattern={pattern}", "cols": args.cols, "rows": args.rows})
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
        self.args = args
        self.font = ImageFont.truetype(str(find_font(args)), 13)
        self.label_font = ImageFont.truetype(str(find_font(args)), 19)
        self.colors = {"default": "#dedede", "black": "#0c0c0c", "red": "#e06c75", "green": "#98c379", "brown": "#e5c07b", "blue": "#61afef", "magenta": "#c678dd", "cyan": "#56b6c2", "white": "#dcdfe4", "brightblack": "#676e7b", "brightred": "#ff757f", "brightgreen": "#b9e68c", "brightbrown": "#ffd68a", "brightblue": "#82baff", "brightmagenta": "#e3a1f6", "brightcyan": "#83e2ea", "brightwhite": "#ffffff"}

    def color(self, value, background=False):
        if value == "default" and background:
            return "#0c0c0c"
        return "#" + value if re.fullmatch("[0-9a-fA-F]{6}", value) else self.colors.get(value, "#dedede")

    @functools.lru_cache(maxsize=8192)
    def mask(self, char):
        # Extra vertical room retains Zalgo's combining marks across cell rows.
        mask = self.Image.new("L", (CELL_W * 2, CELL_H * 3))
        self.ImageDraw.Draw(mask).text((0, CELL_H - 1), char, font=self.font, fill=255)
        return mask

    def draw(self, screen, label):
        args = self.args
        image = self.Image.new("RGB", (args.width, args.height), "#0c0c0c")
        draw = self.ImageDraw.Draw(image)
        dx, dy = (args.width - screen.columns * CELL_W) // 2, (args.height - screen.lines * CELL_H) // 2
        glyphs = []
        for y, row in screen.buffer.items():
            for x, char in row.items():
                fg, bg = self.color(char.fg), self.color(char.bg, True)
                if char.reverse:
                    fg, bg = bg, fg
                px, py = dx + x * CELL_W, dy + y * CELL_H
                if bg != "#0c0c0c":
                    draw.rectangle((px, py, px + CELL_W - 1, py + CELL_H - 1), fill=bg)
                if char.data.strip():
                    glyphs.append((px, py, char.data, fg))
        for px, py, char, fg in glyphs:
            mask = self.mask(char)
            image.paste(fg, (px, py - CELL_H, px + CELL_W * 2, py + CELL_H * 2), mask)
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
                # Retain a possible escape sequence split across PTY reads.
                complete, self.pending = self.pending[:-16], self.pending[-16:]
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


def render_capture(args, name, selected, raster):
    import pyte
    screen = pyte.Screen(args.cols, args.rows)
    stream = TerminalReplay(pyte.Stream(screen))
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
        # Quantize chapter boundaries cumulatively: twelve 0.25s filters must
        # total exactly 99 frames, rather than rounding each to eight frames.
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
                    last_image = raster.draw(screen, chapter["label"])
                encoder.stdin.write(last_image.tobytes())
                if index == count // 2:
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
    return {"recipe": 3, "binary": str(args.binary), "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
            "dll_sha256": hashlib.sha256(args.binary.with_name("asciichat.dll").read_bytes()).hexdigest() if os.name == "nt" else None,
            "media": str(args.media_file), "media_sha256": hashlib.sha256(args.media_file.read_bytes()).hexdigest(),
            "face": str(args.face_file), "face_sha256": hashlib.sha256(args.face_file.read_bytes()).hexdigest() if args.face_file else FACE_URL,
            "start": args.media_file_start, "cols": args.cols, "rows": args.rows,
            "chapters": [asdict(c) for c in chapters() + tui_chapters()]}


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
    # Silence under splash, then the same accelerated/normal music used by the
    # native audio visualizer. Music continues smoothly beneath patterns/TUIs.
    graph = (f"[0:v]setpts=(PTS-STARTPTS)/{args.speed},fps={FPS}[v];"
             f"[1:a]adelay=3000:all=1,apad,atrim=duration={length},asetpts=PTS-STARTPTS,"
             f"{atempo(args.speed)},alimiter=limit=0.84:level=false:latency=true,afade=t=out:st={max(0, length / args.speed - 1.5)}:d=1.5[a]")
    run([args.ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-filter_complex_threads", "2", "-f", "concat", "-safe", "0", "-i", concat,
         "-i", args.work_dir / "performance.mkv", "-filter_complex", graph, "-map", "[v]", "-map", "[a]",
         "-c:v", "libx264", "-preset", "slow" if args.optimize else "fast", "-crf", "20" if args.optimize else "16",
         "-threads", "4", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "160k" if args.optimize else "192k", "-ar", "48000",
         "-movflags", "+faststart", "-t", f"{length / args.speed:.8f}", args.output])
    result = probe(args.output, args)
    video = next(s for s in result["streams"] if s["codec_type"] == "video")
    audio = next(s for s in result["streams"] if s["codec_type"] == "audio")
    if video["avg_frame_rate"] != "33/1" or audio["codec_name"] != "aac":
        raise RuntimeError("Output verification failed: expected 33fps video and AAC audio")
    quality = audio_quality(args)
    manifest = {"options": {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
                "output_probe": result, "recording": evidence, "face_source": FACE_URL,
                "commands": COMMANDS, "audio_quality": quality, "chapters": [p.stem for p in files]}
    (args.work_dir / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"\nReady: {args.output}\n{result['format']['duration']} seconds / 33 fps / {int(result['format']['size']) / 1e6:.2f} MB", flush=True)
    print(f"Chapter images, text, logs, recordings and manifest: {args.work_dir}", flush=True)


def main():
    args = parser().parse_args()
    if args.width < 960 or args.height < 576 or args.width % 2 or args.height % 2:
        parser().error("use even pixel dimensions of at least 960x576 so every TUI fits")
    if args.output.suffix.lower() != ".mp4":
        parser().error("--output must end in .mp4 (GitHub-compatible H.264/AAC)")
    args.cols, args.rows = args.width // CELL_W, args.height // CELL_H
    main_chapters = chapters()
    if args.dry_run:
        print(json.dumps({"fps": FPS, "terminal": [args.cols, args.rows], "output_pixels": [args.width, args.height],
                          "speed": args.speed, "optimize": args.optimize, "overlay": args.opt_text_overlay,
                          "media_start": args.media_file_start, "chapters": [asdict(c) for c in main_chapters + tui_chapters()],
                          "seconds": (3 + sum(c.seconds for c in main_chapters + tui_chapters()) + 6) / args.speed}, indent=2))
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
    # Find clean splash frames from its actual output, before normal media.
    # The logo's first box-drawing output follows initialization diagnostics.
    splash_times = [t for t, _, text in events(args.work_dir / "main.cast.jsonl.gz")
                    if t < evidence["play"] and "Video chat in your terminal" in text]
    if len(splash_times) < 2:
        raise RuntimeError("No complete rainbow splash captured; inspect main.cast.jsonl.gz")
    splash_start, splash_end = splash_times[0], splash_times[-1]
    splash = {"name": "splash", "label": "ascii-chat / terminal video, reimagined", "seconds": 3,
              "start": splash_start + .03, "end": splash_end, "cols": args.cols, "rows": args.rows}
    selected = [splash] + evidence["main"]
    insert = 1 + len(main_chapters)
    expected_names = [c["name"] for c in selected]
    expected_names[insert:insert] = [c["name"] for c in evidence["patterns"]]
    files = None
    manifest_path = args.work_dir / "manifest.json"
    if args.reuse_capture and manifest_path.is_file():
        previous = json.loads(manifest_path.read_text(encoding="utf-8"))
        raster_options = ("width", "height", "cols", "rows", "font", "opt_text_overlay")
        current = {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()}
        candidates = [args.work_dir / (name + ".mp4") for name in expected_names]
        if (previous["recording"] == evidence and previous["chapters"] == expected_names and
                all(previous["options"].get(k) == current[k] for k in raster_options) and
                all(p.is_file() for p in candidates)):
            print("Reusing rendered chapters; applying export settings...", flush=True)
            files = candidates
    if files is None:
        files = render_capture(args, "main", selected, raster)
        patterns = []
        for c in evidence["patterns"]:
            patterns += render_capture(args, c["name"], [c], raster)
        files[insert:insert] = patterns
    assemble(args, files, evidence)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(f"Error: {error}")
