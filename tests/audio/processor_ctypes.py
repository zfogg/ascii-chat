"""Shared ctypes layout for the streaming PCM processor configuration."""

import ctypes as c
from test_spectral import Config as SpectralConfig


class Flags(c.Structure):
    _fields_ = [
        (name, c.c_bool)
        for name in ("agc", "compressor", "noise_gate", "highpass", "lowpass")
    ]


class Config(c.Structure):
    _fields_ = [
        ("sample_rate", c.c_int),
        ("flags", Flags),
        ("spectral_enabled", c.c_bool),
        ("spectral", SpectralConfig),
        ("agc_level", c.c_int),
        ("agc_max_gain", c.c_int),
        ("highpass_hz", c.c_float),
        ("lowpass_hz", c.c_float),
        ("comp_threshold_db", c.c_float),
        ("comp_ratio", c.c_float),
        ("comp_makeup_db", c.c_float),
        ("comp_attack_ns", c.c_uint64),
        ("comp_release_ns", c.c_uint64),
        ("gate_threshold", c.c_float),
        ("gate_hysteresis", c.c_float),
        ("gate_attack_ns", c.c_uint64),
        ("gate_release_ns", c.c_uint64),
    ]
