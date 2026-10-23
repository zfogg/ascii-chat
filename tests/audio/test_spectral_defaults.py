"""Verify microphone defaults, test-source bypass, and explicit mixer opt-in."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401


import ctypes as c
import os
from pathlib import Path
import sys
import tempfile
import subprocess
import numpy as np
from test_spectral import load
from pipeline_ctypes import Config, Flags

library = Path(sys.argv[1]).resolve()
if len(sys.argv) == 2:
    for case in ("microphone", "test", "disabled"):
        subprocess.run([sys.executable, __file__, str(library), case], check=True)
    print(
        "PASS: microphone default, mixer default off, test bypass, microphone opt-out"
    )
    raise SystemExit(0)
case = sys.argv[2]
lib = load(library)


class Entry(c.Structure):
    _fields_ = [
        ("name", c.c_char_p),
        ("short", c.c_char),
        ("type", c.c_int),
        ("offset", c.c_size_t),
    ]


lib.registry_find_entry_by_name.argtypes = [c.c_char_p]
lib.registry_find_entry_by_name.restype = c.POINTER(Entry)
lib.options_get.restype = c.c_void_p
lib.asciichat_shared_init.argtypes = [c.c_char_p, c.c_bool, c.c_bool]
lib.options_init.argtypes = [c.c_int, c.POINTER(c.c_char_p)]
with tempfile.TemporaryDirectory() as directory:
    previous = Path.cwd()
    os.chdir(directory)
    assert lib.asciichat_shared_init(None, False, False) == 0
    args = [b"ascii-chat", b"client", b"--audio-spectral-gate=false"]
    if case == "test":
        args += [b"--test-audio"]
    elif case == "disabled":
        args += [b"--spectral-audio=false"]
    argv = (c.c_char_p * (len(args) + 1))(*args, None)
    assert lib.options_init(len(args), argv) == 0

    def option(name):
        offset = lib.registry_find_entry_by_name(name).contents.offset
        return c.c_bool.from_address(lib.options_get() + offset).value

    assert option(b"spectral-audio") == (case != "disabled")
    assert not option(b"audio-spectral-mixer")

    lib.client_audio_pipeline_config_for_source.argtypes = [c.c_bool]
    lib.client_audio_pipeline_config_for_source.restype = Config
    lib.client_audio_pipeline_create.argtypes = [c.POINTER(Config)]
    lib.client_audio_pipeline_create.restype = c.c_void_p
    lib.client_audio_pipeline_set_flags.argtypes = [c.c_void_p, Flags]
    lib.client_audio_pipeline_process_duplex.argtypes = [
        c.c_void_p,
        c.c_void_p,
        c.c_int,
        c.c_void_p,
        c.c_int,
        c.c_void_p,
    ]
    lib.client_audio_pipeline_destroy.argtypes = [c.c_void_p]

    def capture_start():
        config = lib.client_audio_pipeline_config_for_source(False)
        pipeline = lib.client_audio_pipeline_create(c.byref(config))
        assert pipeline
        lib.client_audio_pipeline_set_flags(pipeline, Flags())
        signal = np.full(480, 0.1, dtype="float32")
        out = np.zeros_like(signal)
        lib.client_audio_pipeline_process_duplex(
            pipeline, None, 0, signal.ctypes.data, 480, out.ctypes.data
        )
        lib.client_audio_pipeline_destroy(pipeline)
        return out

    peak = np.max(abs(capture_start()))
    if case == "microphone":
        assert peak < 1e-6, "default microphone must incur FFT latency"
    else:
        assert peak > 1e-7, "test source and explicit opt-out must bypass FFT"
    lib.options_state_destroy()
    lib.asciichat_shared_destroy()
    os.chdir(previous)
print(
    "PASS: microphone default, separate mixer opt-in, test source bypass, microphone opt-out"
)
