"""Decode encoder output across formats and exercise stdout and setup failures.

Run: python tests/integration/encoder_video_output.py build/bin/asciichat.dll
Requires ffmpeg/ffprobe; also accepts a POSIX shared library.
"""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import ctypes as c
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    library = Path(sys.argv[1]).resolve()
    if os.name == "nt":
        dll_directory = os.add_dll_directory(str(library.parent))
    api = c.CDLL(str(library))
    api.log_init.argtypes = [c.c_char_p, c.c_int, c.c_bool, c.c_bool]
    api.log_init(None, 4, True, False)
    api.options_init.argtypes = [c.c_int, c.POINTER(c.c_char_p)]
    api.ffmpeg_encoder_create.argtypes = [c.c_char_p, c.c_int, c.c_int, c.c_int, c.POINTER(c.c_void_p)]
    api.ffmpeg_encoder_write_frame.argtypes = [c.c_void_p, c.POINTER(c.c_uint8), c.c_int, c.c_uint64]
    api.ffmpeg_encoder_destroy.argtypes = [c.c_void_p]
    argv = (c.c_char_p * 5)(b"encoder-test", b"--no-check-update", b"--log-level", b"error", b"mirror")
    assert api.options_init(len(argv), argv) == 0

    def encode(path, count, fps=30):
        enc = c.c_void_p()
        assert api.ffmpeg_encoder_create(os.fsencode(path), 64, 48, fps, c.byref(enc)) == 0
        try:
            for i in range(count):
                pixels = (c.c_uint8 * (64 * 48 * 4))(*([i * 20, 100, 200, 255] * (64 * 48)))
                assert api.ffmpeg_encoder_write_frame(enc, pixels, 64 * 4, 1_000_000_000 + i * 33_333_333) == 0
        finally:
            result = api.ffmpeg_encoder_destroy(enc)
        assert result == 0, result

    if "--pipe" in sys.argv:
        encode("-", 4)
        return

    def verify(path, codec, count):
        probe = subprocess.run(["ffprobe", "-v", "error", "-count_frames", "-show_streams", "-of", "json", str(path)],
                               check=True, capture_output=True, timeout=20)
        video = next(s for s in json.loads(probe.stdout)["streams"] if s["codec_type"] == "video")
        assert (video["codec_name"], video["width"], video["height"], int(video["nb_read_frames"])) == (codec, 64, 48, count), video
        decoded = subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i", str(path), "-map", "0:v:0",
                                  "-fps_mode", "passthrough", "-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1"],
                                 check=True, capture_output=True, timeout=20)
        assert len(decoded.stdout) == count * 64 * 48 * 3
        print(f"PASS: {path.name}: {codec}, {count} decoded frames", flush=True)

    with tempfile.TemporaryDirectory(prefix="encoder-video-") as tmp:
        root = Path(tmp)
        for extension, codec, count in (("mp4", "h264", 4), ("webm", "vp9", 4), ("avi", "mpeg4", 4),
                                         ("png", "png", 1), ("jpg", "mjpeg", 1), ("gif", "gif", 4)):
            path = root / ("video." + extension)
            encode(path, count)
            verify(path, codec, count)
        fallback = root / "default.mp4"
        encode(fallback, 4, fps=0)
        verify(fallback, "h264", 4)
        pipe = subprocess.run([sys.executable, __file__, str(library), "--pipe"], capture_output=True, timeout=30)
        assert pipe.returncode == 0, pipe.stderr[-2000:]
        path = root / "pipe.mp4"
        path.write_bytes(pipe.stdout)
        verify(path, "h264", 4)
        enc = c.c_void_p()
        assert api.ffmpeg_encoder_create(None, 64, 48, 30, c.byref(enc)) != 0
        assert api.ffmpeg_encoder_create(os.fsencode(root / "bad.png"), 0, 48, 30, c.byref(enc)) != 0
        assert api.ffmpeg_encoder_create(os.fsencode(root / "missing" / "bad.mp4"), 64, 48, 30, c.byref(enc)) != 0
        assert api.ffmpeg_encoder_destroy(None) == 0
        print("PASS: invalid arguments, output-open failure, null cleanup", flush=True)


if __name__ == "__main__":
    main()
