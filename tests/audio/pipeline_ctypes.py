"""Shared ctypes layout for the native audio pipeline configuration."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401


import ctypes as c
from processor_ctypes import Config as ProcessorConfig


class Flags(c.Structure):
    _fields_ = [
        (name, c.c_bool)
        for name in [
            "echo_cancel",
            "noise_suppress",
            "agc",
            "vad",
            "jitter_buffer",
            "compressor",
            "noise_gate",
            "highpass",
            "lowpass",
        ]
    ]


class Config(c.Structure):
    _fields_ = [
        ("sample_rate", c.c_int),
        ("frame_duration_ms", c.c_uint64),
        ("opus_bitrate", c.c_int),
        ("opus_application", c.c_int),
        ("echo_filter_ms", c.c_uint64),
        ("processor", ProcessorConfig),
        ("jitter_margin_ms", c.c_uint64),
        ("flags", Flags),
    ]
