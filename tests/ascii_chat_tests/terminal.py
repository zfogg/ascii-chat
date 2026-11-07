"""PTY and ConPTY screen capture helpers."""
import importlib.util
import os
from pathlib import Path
import queue
import re
import subprocess
import threading
import time

missing = [name for name in ["pyte"] + ([] if os.name == "nt" else ["pexpect"]) if importlib.util.find_spec(name) is None]
if missing:
    print("SKIP: terminal tests need " + ", ".join(missing) + "; run uv sync --project tests/ascii_chat_tests")
    raise SystemExit(77)
import pyte

def load_terminal_font(size):
    """Use an installed monospace font on each native test platform."""
    from PIL import ImageFont
    candidates = ("C:/Windows/Fonts/consola.ttf", "/System/Library/Fonts/Menlo.ttc",
                  "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
                  "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", "DejaVuSansMono.ttf")
    for candidate in candidates:
        try:
            return ImageFont.truetype(candidate, size)
        except OSError:
            continue
    raise RuntimeError("Install a monospace font to capture terminal screenshots")


class CaptureScreen(pyte.Screen):
    """Handle REP, used by the renderer and ConPTY to compress repeated cells."""

    media_leaks: list[str]
    forbid_media: bool
    preserved: list[tuple[int, int, str]]
    preservation_failures: list[tuple[int, str, str]]

    def reset(self):
        self.last_character = ""
        super().reset()

    def draw(self, data):
        super().draw(data)
        if data:
            self.last_character = data[-1]

    def set_mode(self, *modes, **kwargs):
        if kwargs.get("private") and 2026 in modes:
            self.synchronized = True
        super().set_mode(*modes, **kwargs)

    def reset_mode(self, *modes, **kwargs):
        super().reset_mode(*modes, **kwargs)
        if kwargs.get("private") and 2026 in modes:
            self.synchronized = False

    def repeat_character(self, count=1):
        if self.last_character:
            self.draw(self.last_character * (count or 1))


class CaptureStream(pyte.Stream):
    csi = dict(pyte.Stream.csi, b="repeat_character")
    events = pyte.Stream.events | {"repeat_character"}

    def feed(self, data):
        # Publish synchronized output as a single update, even when ConPTY splits
        # its start/end markers across reads. Plain output remains immediate.
        start, end = "\x1b[?2026h", "\x1b[?2026l"
        self.pending_frame = getattr(self, "pending_frame", "") + data
        while self.pending_frame:
            first = self.pending_frame.find(start)
            if first < 0:
                tail = next((n for n in range(len(start) - 1, 0, -1)
                             if self.pending_frame.endswith(start[:n])), 0)
                ready = self.pending_frame[:-tail] if tail else self.pending_frame
                self.pending_frame = self.pending_frame[-tail:] if tail else ""
                if ready:
                    super().feed(ready)
                return
            if first:
                super().feed(self.pending_frame[:first])
                self.pending_frame = self.pending_frame[first:]
            last = self.pending_frame.find(end, len(start))
            if last < 0:
                return
            last += len(end)
            super().feed(self.pending_frame[:last])
            self.pending_frame = self.pending_frame[last:]


class Terminal:
    def __init__(self, argv, rows=12, cols=19, terminal_replies=False, code_page=65001, environment=None):
        self.terminal_replies = terminal_replies
        self.background_reply = "\x1b]11;rgb:0000/0000/0000\x1b\\"
        self.query_tail = ""
        self.screen = CaptureScreen(cols, rows)
        self.stream = CaptureStream(self.screen)
        self.output = queue.Queue()
        self.raw = []
        child_environment = os.environ.copy()
        child_environment.update(environment or {})
        if os.name == "nt":
            from winpty import PtyProcess

            # Match a UTF-8 terminal, including its console code page.
            command = f"chcp {code_page}>nul & " + subprocess.list2cmdline(argv)
            self.process = PtyProcess.spawn(
                ["cmd.exe", "/d", "/c", command], dimensions=(rows, cols), env=child_environment
            )
        else:
            import pexpect

            self.process = pexpect.spawn(
                argv[0], argv[1:], dimensions=(rows, cols), encoding="utf-8", env=child_environment
            )
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        try:
            while True:
                if os.name == "nt":
                    data = self.process.read(65536)
                else:
                    data = self.process.read_nonblocking(65536, timeout=None)
                self.output.put(data)
        except (EOFError, OSError):
            pass
        except Exception as exc:
            if type(exc).__name__ != "EOF":
                self.output.put(str(exc))

    def pump(self, seconds=0.2):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            try:
                chunk = self.output.get(timeout=0.02)
                self.raw.append(chunk)
                self.stream.feed(chunk)
                if self.terminal_replies:
                    queries = self.query_tail + chunk
                    query = "\x1b]11;?\x1b\\"
                    for _ in range(queries.count(query)):
                        try:
                            self.write(self.background_reply)
                        except (EOFError, OSError):
                            # A queued query can outlive the exiting PTY child.
                            pass
                    self.query_tail = queries[-(len(query) - 1):]
            except queue.Empty:
                pass
        return "\n".join(self.screen.display)

    def expect(self, predicate, description, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = self.pump()
            if predicate(text):
                return text
        raise AssertionError(f"{description}\nRendered terminal:\n{text}")

    def resize(self, rows, cols):
        self.process.setwinsize(rows, cols)
        self.screen.resize(rows, cols)

    def write(self, text):
        if os.name == "nt":
            self.process.write(text)
        else:
            self.process.send(text)

    def interrupt(self):
        self.write("\x03")
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline and self.process.isalive():
            # The reader continues draining the PTY. Retain output without
            # parsing queued frames while measuring the process exit deadline.
            try:
                self.raw.append(self.output.get(timeout=0.05))
            except queue.Empty:
                pass
        if self.process.isalive():
            diagnostics = ""
            if os.name != "nt":
                import termios
                attributes = termios.tcgetattr(self.process.child_fd)
                diagnostics = (f"; terminal ISIG={bool(attributes[3] & termios.ISIG)}"
                               f", VINTR={attributes[6][termios.VINTR]!r}")
            raise AssertionError(f"UI shutdown did not join its worker{diagnostics}\n"
                                 + "".join(self.raw)[-4000:])

    def close(self):
        if os.name == "nt" and self.process.isalive():
            try:
                subprocess.run(["taskkill", "/PID", str(self.process.pid), "/T", "/F"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               timeout=5, check=False)
            except subprocess.TimeoutExpired:
                pass
        if os.name != "nt":
            self.process.terminate(force=True)
        self.reader.join(timeout=.2 if os.name == "nt" else 2)




OSC_TITLE = re.compile(r"\x1b\](?:0|2);([^\x07\x1b]*)(?:\x07|\x1b\\)")


class TitleTerminal(Terminal):
    def __init__(self, *args, **kwargs):
        self.titles = []
        self.title_count = 0
        super().__init__(*args, **kwargs)

    def pump(self, seconds=0.1):
        text = super().pump(seconds)
        titles = OSC_TITLE.findall("".join(self.raw))
        self.titles.extend((time.monotonic(), title) for title in titles[self.title_count:])
        self.title_count = len(titles)
        return text

    def expect_title(self, expected, timeout=12):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.pump()
            if self.titles and self.titles[-1][1] == expected:
                return self.titles[-1][0]
        raise AssertionError(f"Expected {expected!r}; observed {self.titles!r}")




def capture(term, directory, name):
    directory.mkdir(parents=True, exist_ok=True)
    (directory / f"{name}.txt").write_text("\n".join(term.screen.display), encoding="utf-8")
    (directory / f"{name}.ansi").write_text("".join(term.raw), encoding="utf-8")
    from PIL import Image, ImageDraw, ImageFont
    fonts = [Path("C:/Windows/Fonts/consola.ttf"), Path("/System/Library/Fonts/Menlo.ttc"),
             Path("/usr/share/fonts/TTF/DejaVuSansMono.ttf"),
             Path("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf")]
    font_path = next((p for p in fonts if p.exists()), None)
    if font_path is None:
        raise RuntimeError("Install a monospace font to capture terminal screenshots")
    font = ImageFont.truetype(str(font_path), 16)
    cell_width = max(1, round(font.getlength("M")))
    line_height = 21
    image = Image.new("RGB", (term.screen.columns * cell_width + 24, term.screen.lines * line_height + 24), "#10151c")
    draw = ImageDraw.Draw(image)
    palette = {"black": "#10151c", "red": "#cd3131", "green": "#0dbc79",
               "yellow": "#e5e510", "blue": "#2472c8", "magenta": "#bc3fbc", "cyan": "#11a8cd", "white": "#e5e5e5"}

    def color(value, fallback):
        return palette.get(value, f"#{value}" if isinstance(value, str) and len(value) == 6 else fallback)

    for row, text in enumerate(term.screen.display):
        for col, character in enumerate(text):
            cell = term.screen.buffer[row][col]
            fg, bg = color(cell.fg, "#e1e8ed"), color(cell.bg, "#10151c")
            if cell.reverse:
                fg, bg = bg, fg
            x, y = 12 + col * cell_width, 12 + row * line_height
            draw.rectangle((x, y, x + cell_width - 1, y + line_height - 1), fill=bg)
            draw.text((x, y), character, font=font, fill=fg)
    image.save(directory / f"{name}.png")


def capture_sync(term, root, name):
    root.mkdir(parents=True, exist_ok=True)
    (root / f"{name}.txt").write_text("\n".join(term.screen.display), encoding="utf-8")
    (root / f"{name}.ansi").write_text("".join(term.raw), encoding="utf-8")
    from PIL import Image, ImageDraw, ImageFont
    font = load_terminal_font(16)
    symbol_paths = ("C:/Windows/Fonts/seguisym.ttf",
                    "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf")
    symbol_font = next((ImageFont.truetype(path, 16) for path in symbol_paths if Path(path).exists()), font)
    cw, ch = round(font.getlength("M")), 21
    image = Image.new("RGB", (term.screen.columns * cw + 24, term.screen.lines * ch + 24), "#0d141a")
    draw = ImageDraw.Draw(image)
    palette = {"default": "#dedbd0", "black": "#000000", "red": "#e85d6f", "green": "#90e070",
               "brown": "#f0cc91", "blue": "#65ace1", "magenta": "#c8a0d8", "cyan": "#65ace1",
               "white": "#dedbd0"}
    def color(value):
        return palette.get(value, "#" + value if len(value) == 6 else "#dedbd0")
    for y in range(term.screen.lines):
        for x in range(term.screen.columns):
            cell = term.screen.buffer[y][x]
            fg, bg = color(cell.fg), "#0d141a" if cell.bg == "default" else color(cell.bg)
            if cell.reverse:
                fg, bg = bg, fg
            if bg != "#0d141a":
                draw.rectangle((12+x*cw, 12+y*ch, 12+(x+1)*cw, 12+(y+1)*ch), fill=bg)
            draw.text((12+x*cw, 12+y*ch), cell.data,
                      font=symbol_font if "❌" in cell.data else font, fill=fg)
    image.save(root / f"{name}.png")


def capture_mdns(term, directory, name):
    if directory is None:
        return
    directory.mkdir(parents=True, exist_ok=True)
    (directory / f"{name}.txt").write_text("\n".join(term.screen.display), encoding="utf-8")
    (directory / f"{name}.ansi").write_text("".join(term.raw), encoding="utf-8")
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        return
    configured_font = os.environ.get("TERMINAL_CAPTURE_FONT")
    font = ImageFont.truetype(configured_font, 16) if configured_font else load_terminal_font(16)
    divider_mask = font.getmask("━")
    if not divider_mask.getbbox():
        raise AssertionError("Terminal capture font does not contain the UTF-8 divider glyph")
    image = Image.new("RGB", (term.screen.columns * 10 + 24, term.screen.lines * 21 + 24), "#10151c")
    draw = ImageDraw.Draw(image)
    for row, text in enumerate(term.screen.display):
        for column, character in enumerate(text):
            foreground = term.screen.buffer[row][column].fg
            color = f"#{foreground}" if re.fullmatch(r"[0-9a-fA-F]{6}", foreground) else "#e1e8ed"
            draw.text((12 + column * 10, 12 + row * 21), character, font=font, fill=color)
    image.save(directory / f"{name}.png")
