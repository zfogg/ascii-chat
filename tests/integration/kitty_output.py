#!/usr/bin/env python3
"""Native export process IO and independent Kitty protocol reconstruction."""
import argparse
import base64
import ctypes as c
import os
from pathlib import Path
import re
import subprocess
import tempfile


def reconstruct(data):
    packets = list(re.finditer(rb"\x1b_G([^;]*);([A-Za-z0-9+/=]*)\x1b\\", data))
    assert packets and b"".join(p.group() for p in packets) == data, repr(data[:200])
    for i, packet in enumerate(packets):
        assert len(packet[2]) <= 4096
        assert (b"m=0" if i == len(packets) - 1 else b"m=1") in packet[1]
        if i:
            assert packet[1] in (b"m=0", b"m=1")
        else:
            assert b"a=T,f=32" in packet[1] and b"t=d,q=2" in packet[1]
    width = int(re.search(rb"s=(\d+)", packets[0][1])[1])
    height = int(re.search(rb"v=(\d+)", packets[0][1])[1])
    raw = base64.b64decode(b"".join(p[2] for p in packets), validate=True)
    assert len(raw) == width * height * 4
    return width, height, raw, len(packets)


def check_mp4_boxes(data):
    offset = 0
    while offset < len(data):
        assert len(data) - offset >= 8, "Trailing non-container bytes on binary stdout"
        size = int.from_bytes(data[offset:offset + 4], "big")
        kind = data[offset + 4:offset + 8]
        assert kind in (b"ftyp", b"moov", b"moof", b"mdat", b"mfra", b"sidx", b"styp", b"free", b"skip"), kind
        header = 8
        if size == 1:
            assert len(data) - offset >= 16
            size = int.from_bytes(data[offset + 8:offset + 16], "big")
            header = 16
        assert header <= size <= len(data) - offset, (offset, size, len(data))
        offset += size


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--library")
    opts = parser.parse_args()
    binary = str(Path(opts.binary).resolve())
    env = dict(os.environ, TERM="dumb", TESTING="1", ASAN_OPTIONS="detect_leaks=0")
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        ppm = b"P6\n8 8\n255\n" + bytes([220, 100, 35]) * 64
        common = [binary, "--log-file", os.devnull, "mirror", "--file", "-", "--snapshot", "--snapshot-delay", "0",
                  "--width", "20", "--height", "10", "--color-mode", "truecolor"]

        def launch(flags, success=True):
            proc = subprocess.run(common + flags, input=ppm, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  cwd=root, env=env, timeout=45)
            assert (proc.returncode == 0) == success, (proc.returncode, proc.stderr[-4000:])
            return proc

        protocol = launch(["--render-file=--kitty"]).stdout
        width, height, rgba, chunks = reconstruct(protocol)
        assert chunks > 1 and len(set(rgba)) > 2
        assert not list(root.iterdir()), list(root.iterdir())
        print(f"protocol-only: {width}x{height}, {chunks} chunks, no filesystem output")
        assert reconstruct(launch(["--render-kitty"]).stdout)[:3] == (width, height, rgba)
        saved = root / "snapshot.png"
        sw, sh, srgb, _ = reconstruct(launch(["--render-kitty", "--render-file", str(saved)]).stdout)
        assert (sw, sh, srgb) == (width, height, rgba)
        decoded = subprocess.run(["ffmpeg", "-v", "error", "-i", str(saved), "-f", "rawvideo", "-pix_fmt", "rgba", "-"],
                                 capture_output=True, check=True).stdout
        assert decoded == rgba, "Saved PNG and Kitty pixels differ"
        launch(["--render-kitty", "--render-file=-"], success=False)
        failed_save = launch(["--render-kitty", "--render-file", str(root / "absent" / "snapshot.png")], success=False)
        assert not failed_save.stdout, "Failed output must not display a successful snapshot"
        rejected = subprocess.run([binary, "mirror", "--render-file=--kitty"], capture_output=True, env=env, timeout=15)
        assert rejected.returncode != 0 and b"snapshot" in rejected.stderr.lower()
        # Repeat fresh processes so delayed codec startup diagnostics cannot hide
        # behind a successful earlier encoder initialization.
        for _ in range(3):
            video = launch(["--render-file=-"]).stdout
            assert video[4:8] == b"ftyp", repr(video[:100])
            check_mp4_boxes(video)
        output = root / "stdout.mp4"
        output.write_bytes(video)
        subprocess.run(["ffmpeg", "-v", "error", "-i", str(output), "-f", "null", "-"], check=True)
        print("saved PNG equals Kitty RGBA; stdin -> binary stdout MP4 decodes; invalid combinations rejected")
        for output_flag in ["--render-file=-", "--render-file=--kitty"]:
            proc = subprocess.Popen(common + [output_flag], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, cwd=root, env=env)
            proc.stdout.close()
            proc.stdout = None
            _, diagnostic = proc.communicate(ppm, timeout=45)
            assert proc.returncode != 0, (output_flag, diagnostic[-2000:])
        print("closed consumer: binary and Kitty export report failure")
        if os.name != "nt":
            import pty
            master, slave = pty.openpty()
            try:
                proc = subprocess.run(common + ["--render-file=--kitty"], input=ppm, stdout=slave,
                                      stderr=subprocess.PIPE, cwd=root, env=env, timeout=45)
                assert proc.returncode != 0 and b"capable terminal" in proc.stderr, proc.stderr[-2000:]
            finally:
                os.close(slave)
                os.close(master)
            print("unsupported TTY: rejected with capability diagnostic")

        if opts.library:
            api = c.CDLL(str(Path(opts.library).resolve()))
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
