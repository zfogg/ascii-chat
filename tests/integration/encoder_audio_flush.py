"""Exercise final partial audio frames through the real shared-library API.

Run: python tests/integration/encoder_audio_flush.py build/bin/asciichat.dll
Requires ffmpeg and ffprobe. Also accepts a built libasciichat shared library.
"""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401


import argparse
import ctypes as c
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile


class Backtrace(c.Structure):
    _fields_ = [("ptrs", c.c_void_p * 32), ("symbols", c.POINTER(c.c_char_p)), ("count", c.c_int),
               ("tried_symbolize", c.c_bool)]


class ErrorContext(c.Structure):
    _fields_ = [("code", c.c_int), ("file", c.c_char_p), ("line", c.c_int), ("function", c.c_char_p),
               ("context_message", c.c_char * 1024), ("timestamp", c.c_uint64), ("created_ns", c.c_uint64),
               ("error_id", c.c_uint64), ("cause_id", c.c_uint64), ("thread_id", c.c_uint64),
               ("generation", c.c_uint64), ("system_errno", c.c_int), ("wsa_error", c.c_int),
               ("backtrace", Backtrace), ("has_system_error", c.c_bool), ("has_wsa_error", c.c_bool),
               ("message_truncated", c.c_bool)]


def webm_encoder_unavailable(api):
    context = ErrorContext()
    api.errno_has.argtypes = [c.POINTER(ErrorContext)]
    api.errno_has.restype = c.c_bool
    return api.errno_has(c.byref(context)) and context.code == 8 and \
        b"encoder 'libvpx-vp9' not found" in context.context_message


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    parser.add_argument("--formats", nargs="+", choices=("mp4", "webm"), default=("mp4", "webm"))
    args = parser.parse_args()
    library = args.library.resolve()
    if os.name == "nt":
        dll_directory = os.add_dll_directory(str(library.parent))
    api = c.CDLL(str(library))
    api.log_init.argtypes = [c.c_char_p, c.c_int, c.c_bool, c.c_bool]
    api.log_init(None, 4, True, False)
    api.options_init.argtypes = [c.c_int, c.POINTER(c.c_char_p)]
    api.ffmpeg_encoder_create.argtypes = [c.c_char_p, c.c_int, c.c_int, c.c_int, c.POINTER(c.c_void_p)]
    api.ffmpeg_encoder_write_frame.argtypes = [c.c_void_p, c.POINTER(c.c_uint8), c.c_int, c.c_uint64]
    api.ffmpeg_encoder_write_audio.argtypes = [c.c_void_p, c.POINTER(c.c_float), c.c_int]
    api.ffmpeg_encoder_destroy.argtypes = [c.c_void_p]
    argv = (c.c_char_p * 5)(b"encoder-test", b"--no-check-update", b"--log-level", b"error", b"mirror")
    assert api.options_init(len(argv), argv) == 0

    with tempfile.TemporaryDirectory(prefix="audio-flush-") as directory:
        for extension, codec, frame_size in (("mp4", "aac", 1024), ("webm", "opus", 960)):
            if extension not in args.formats:
                continue
            # Cover padding alone, an aligned end, and padding after full frames.
            for count in (1, frame_size, frame_size + 1, 48001):
                path = Path(directory) / f"{count}.{extension}"
                enc = c.c_void_p()
                created = api.ffmpeg_encoder_create(os.fsencode(path), 64, 48, 30, c.byref(enc))
                if created != 0 and extension == "webm" and webm_encoder_unavailable(api):
                    print("SKIP opus/WebM: this FFmpeg build has no compatible WebM video encoder", flush=True)
                    break
                assert created == 0
                try:
                    pixels = (c.c_uint8 * (64 * 48 * 4))(*([128, 128, 128, 255] * (64 * 48)))
                    assert api.ffmpeg_encoder_write_frame(enc, pixels, 64 * 4, 1_000_000_000) == 0
                    samples = (c.c_float * count)(*(0.2 * math.sin(2 * math.pi * 440 * i / 48000)
                                                   for i in range(count)))
                    assert api.ffmpeg_encoder_write_audio(enc, samples, count) == 0
                finally:
                    result = api.ffmpeg_encoder_destroy(enc)
                assert result == 0, f"{extension}/{count}: finalization returned {result}"

                probe = subprocess.run(["ffprobe", "-v", "error", "-show_streams", "-of", "json", str(path)],
                                       check=True, capture_output=True)
                audio = next(s for s in json.loads(probe.stdout)["streams"] if s["codec_type"] == "audio")
                assert audio["codec_name"] == codec, audio
                decoded = subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i", str(path), "-vn",
                                          "-ac", "1", "-ar", "48000", "-f", "f32le", "pipe:1"],
                                         check=True, capture_output=True)
                decoded_count = len(decoded.stdout) // 4
                padded_count = math.ceil(count / frame_size) * frame_size
                # MP4's millisecond duration rounding can trim up to 48 samples.
                assert abs(decoded_count - padded_count) <= 48, (extension, count, decoded_count)
                print(f"PASS {codec}: {count} input samples, {decoded_count} decoded samples", flush=True)


if __name__ == "__main__":
    main()
