"""Shared ctypes layout for the native audio pipeline configuration."""

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
