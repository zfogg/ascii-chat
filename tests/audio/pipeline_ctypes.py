"""Shared ctypes layout for the native audio pipeline configuration."""

import ctypes as c


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
        ("noise_suppress_db", c.c_int),
        ("agc_level", c.c_int),
        ("agc_max_gain", c.c_int),
        ("jitter_margin_ms", c.c_uint64),
        ("highpass_hz", c.c_float),
        ("lowpass_hz", c.c_float),
        ("comp_threshold_db", c.c_float),
        ("comp_ratio", c.c_float),
        ("comp_attack_ns", c.c_uint64),
        ("comp_release_ns", c.c_uint64),
        ("comp_makeup_db", c.c_float),
        ("gate_threshold", c.c_float),
        ("gate_attack_ns", c.c_uint64),
        ("gate_release_ns", c.c_uint64),
        ("gate_hysteresis", c.c_float),
        ("flags", Flags),
    ]
