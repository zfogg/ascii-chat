"""Drive the real native media prompt, paste/drop, URL loading and clipboard through a PTY.

Run with --binary build/bin/ascii-chat.exe. Windows clipboard data is restored in finally.
Requires pyte and ffmpeg, plus pexpect on POSIX. Windows uses a hidden real console.
"""
import argparse
import contextlib
import functools
import http.server
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import threading
import time

from terminal_ui import Terminal

if os.name == "nt":
    from windows_console import WindowsConsole as Terminal


@contextlib.contextmanager
def copied_file(path):
    """Preserve memory-backed clipboard formats; refuse non-serializable clipboard objects."""
    import ctypes as c
    from ctypes import wintypes as w

    user, kernel = c.windll.user32, c.windll.kernel32
    user.GetClipboardData.argtypes, user.GetClipboardData.restype = [w.UINT], w.HANDLE
    user.SetClipboardData.argtypes, user.SetClipboardData.restype = [w.UINT, w.HANDLE], w.HANDLE
    kernel.GlobalAlloc.argtypes, kernel.GlobalAlloc.restype = [w.UINT, c.c_size_t], w.HGLOBAL
    kernel.GlobalLock.argtypes, kernel.GlobalLock.restype = [w.HGLOBAL], c.c_void_p
    kernel.GlobalUnlock.argtypes = [w.HGLOBAL]
    kernel.GlobalSize.argtypes, kernel.GlobalSize.restype = [w.HGLOBAL], c.c_size_t

    def open_clipboard():
        for _ in range(100):
            if user.OpenClipboard(None):
                return
            time.sleep(0.01)
        raise OSError("Cannot acquire clipboard")

    def replace(formats):
        assert user.EmptyClipboard()
        for format_id, data in formats:
            handle = kernel.GlobalAlloc(2, len(data))
            assert handle
            address = kernel.GlobalLock(handle)
            assert address
            c.memmove(address, data, len(data))
            kernel.GlobalUnlock(handle)
            assert user.SetClipboardData(format_id, handle)

    original = []
    changed = False
    open_clipboard()
    try:
        format_id = 0
        while True:
            format_id = user.EnumClipboardFormats(format_id)
            if not format_id:
                break
            if format_id in {2, 3, 9, 14, 0x80, 0x82, 0x83, 0x8E}:
                raise RuntimeError("Clipboard contains an object this fixture cannot preserve")
            handle = user.GetClipboardData(format_id)
            size = kernel.GlobalSize(handle)
            if not size:
                raise RuntimeError("Clipboard format cannot be safely preserved")
            address = kernel.GlobalLock(handle)
            if not address:
                raise RuntimeError("Clipboard format cannot be read safely")
            original.append((format_id, c.string_at(address, size)))
            kernel.GlobalUnlock(handle)
        payload = struct.pack("<IiiII", 20, 0, 0, 0, 1) + (str(path) + "\0\0").encode("utf-16le")
        changed = True
        replace([(15, payload)])
    except BaseException:
        if changed:
            replace(original)
        raise
    finally:
        user.CloseClipboard()
    try:
        yield
    finally:
        open_clipboard()
        try:
            replace(original)
        finally:
            user.CloseClipboard()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--without-audio", action="store_true", help="Run visual checks without an audio device")
    parser.add_argument("--verify-audio", action="store_true", help="Measure Windows WASAPI loopback (requires PyAudioWPatch and numpy)")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="ascii-live-media-") as directory:
        root = Path(directory)
        red, blue = root / "café movie.mp4", root / "mrc-blue.mp4"
        for path, color, frequency in [(red, "red", 440), (blue, "blue", 880)]:
            red_expression = "random(1)*255" if color == "red" else "0"
            blue_expression = "random(1)*255" if color == "blue" else "0"
            video = f"nullsrc=s=160x120:r=30,geq=r='{red_expression}':g=0:b='{blue_expression}'"
            subprocess.run([
                "ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", video,
                "-f", "lavfi", "-i", f"sine=frequency={frequency}:sample_rate=48000", "-t", "10",
                "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", "-movflags", "+faststart", str(path),
            ], check=True, timeout=30)
        damaged = root / "damaged.mp4"
        damaged_bytes = bytearray(red.read_bytes())
        payload = damaged_bytes.index(b"mdat") + 4
        damaged_bytes[payload:] = b"\xff" * (len(damaged_bytes) - payload)
        damaged.write_bytes(damaged_bytes)
        slow_entered, slow_done = threading.Event(), threading.Event()

        class Handler(http.server.SimpleHTTPRequestHandler):
            def do_GET(self):
                if self.path == "/slow.mp4":
                    slow_entered.set()
                    try:
                        time.sleep(5)
                        self.path = "/mrc-blue.mp4"
                        super().do_GET()
                    except (BrokenPipeError, ConnectionResetError):
                        pass
                    finally:
                        slow_done.set()
                else:
                    super().do_GET()

        handler = functools.partial(Handler, directory=str(root))
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        log = root / "application.log"
        previous_path = os.environ.get("PATH", "")
        if os.name != "nt":
            provider = root / "clipboard-provider"
            provider.mkdir()
            executable = provider / "wl-paste"
            executable.write_text("#!/usr/bin/env python3\nimport sys\nsys.stdout.write(" + repr(red.as_uri()) + ")\n", encoding="utf-8")
            executable.chmod(0o755)
            os.environ["PATH"] = str(provider) + os.pathsep + previous_path
        term = Terminal([
            str(args.binary.resolve()), "--log-level", "debug", "--no-check-update", "--log-file", str(log), "mirror",
            "--test-pattern", "--fps", "5", "--audio-source", "media", "--volume", "0.7" if args.verify_audio else "0",
            "--splash-screen=false", "--color-mode", "truecolor",
            *( ["--audio=false"] if args.without_audio else [] ),
        ], rows=20, cols=60)
        os.environ["PATH"] = previous_path

        def colored(channel):
            def matches(text):
                if "Open media:" in text or "MEDIA COULD NOT BE OPENED" in text or "MEDIA INPUT" in text:
                    return False
                count = 0
                for row in term.screen.buffer.values():
                    for cell in row.values():
                        fg = cell.fg
                        if fg in ({"red", "brightred"} if channel == 0 else {"blue", "brightblue"}):
                            count += 1
                        if len(fg) == 6 and all(c in "0123456789abcdef" for c in fg.lower()):
                            rgb = [int(fg[i:i + 2], 16) for i in (0, 2, 4)]
                            if rgb[channel] > 100 and rgb[channel] > max(rgb[(channel + 1) % 3], rgb[(channel + 2) % 3]) * 2:
                                count += 1
                return count > 100
            return matches

        def verify_tone(frequency):
            if not args.verify_audio:
                return
            import numpy as np
            import pyaudiowpatch as pa
            with pa.PyAudio() as audio:
                device = audio.get_default_wasapi_loopback()
                rate, channels = int(device["defaultSampleRate"]), int(device["maxInputChannels"])
                with audio.open(format=pa.paFloat32, channels=channels, rate=rate, input=True,
                                input_device_index=device["index"], frames_per_buffer=1024) as stream:
                    chunks = [stream.read(1024, exception_on_overflow=False) for _ in range(rate * 2 // 1024)]
                samples = np.frombuffer(b"".join(chunks), dtype=np.float32).reshape(-1, channels)[:, 0]
                spectrum = np.abs(np.fft.rfft(samples * np.hanning(len(samples))))
                frequencies = np.fft.rfftfreq(len(samples), 1 / rate)
                peak = float(frequencies[int(np.argmax(spectrum[1:])) + 1])
                rms = float(np.sqrt(np.mean(samples ** 2)))
                assert rms > 0.001 and abs(peak - frequency) < 5, (frequency, peak, rms)
                print(f"PASS audible output: expected={frequency}Hz measured={peak:.1f}Hz rms={rms:.4f}")

        try:
            term.expect(lambda s: s.count("oo") > 15, "Initial test pattern is visible", timeout=30)
            term.write("o")
            term.expect(lambda s: "Open media:" in s, "Open shortcut displays editable prompt")
            term.write(str(red))
            term.pump(0.3)
            term.write("\r")
            term.expect(colored(0), "Local Unicode path renders red video")
            verify_tone(440)
            term.write("\x1b[200~http://127.0.0.1:%d/mrc-blue.mp4\x1b[201~" % server.server_port)
            term.expect(lambda s: "Open media:" in s and "mrc-blue.mp4" in s, "Pasted URL prefills prompt")
            term.write("\r")
            term.expect(colored(2), "URL replacement renders blue video")
            verify_tone(880)
            term.write("o")
            term.expect(lambda s: "Open media:" in s, "Reopen prompt")
            term.write("\x1b")
            term.expect(colored(2), "Cancelling prompt preserves blue video")
            term.write("o")
            term.expect(lambda s: "Open media:" in s, "Prompt for invalid media")
            term.write(str(root / "missing.mp4"))
            term.pump(0.2)
            term.write("\r")
            term.expect(lambda s: "MEDIA COULD NOT BE OPENED" in s, "Invalid media displays error")
            term.write("\r")
            term.expect(colored(2), "Failed load preserves blue video")
            term.write("o")
            term.expect(lambda s: "Open media:" in s, "Prompt for damaged media")
            term.write(str(damaged))
            term.pump(0.3)
            term.write("\r")
            term.expect(lambda s: "MEDIA COULD NOT BE OPENED" in s, "Decoder failure is rejected before replacement")
            term.write("\r")
            term.expect(colored(2), "Damaged media preserves blue video")
            slow_url = "http://127.0.0.1:%d/slow.mp4" % server.server_port
            term.write("\x1b[200~" + slow_url + "\x1b[201~")
            term.expect(lambda s: "Open media:" in s and "slow.mp4" in s, "Prompt for slow media")
            term.write("\r")
            term.expect(lambda s: "Loading media..." in s, "Loading progress is visible")
            assert slow_entered.wait(timeout=5), "Slow request did not start"
            term.write("\x1b")
            term.expect(colored(2), "Escape cancels loading and restores previous video")
            assert slow_done.wait(timeout=6), "Slow fixture did not finish"
            term.pump(0.3)
            assert "Opened media: " + slow_url not in log.read_text(encoding="utf-8"), "Cancelled source became active"
            with copied_file(red) if os.name == "nt" else contextlib.nullcontext():
                term.write("v")
                term.expect(lambda s: "Open media:" in s and "movie.mp4" in s, "Copied file prefills prompt")
                term.write("\r")
                term.expect(colored(0), "Clipboard file renders red video")
            term.interrupt()
            print("PASS live prompt, Unicode file, bracketed URL paste, failed-load preservation, cancellation and clipboard")
        finally:
            pending = []
            while not term.output.empty():
                pending.append(term.output.get_nowait())
            if pending:
                print("PTY pending bytes:", sum(map(len, pending)), "contains prompt:", "Open media:" in "".join(pending))
            if args.artifacts:
                args.artifacts.mkdir(parents=True, exist_ok=True)
                (args.artifacts / "terminal.txt").write_text("\n".join(term.screen.display), encoding="utf-8")
                (args.artifacts / "terminal.ansi").write_text("".join(term.raw), encoding="utf-8")
                if log.exists():
                    (args.artifacts / "application.log").write_bytes(log.read_bytes())
            term.close()
            server.shutdown()


if __name__ == "__main__":
    main()
