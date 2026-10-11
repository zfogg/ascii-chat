"""Measure the compiled AEC3 path with known echo and independent near-end speech-like signals."""

import ctypes as c
import json
import os
from pathlib import Path
import sys
import tempfile
import numpy as np
from test_spectral import load

lib = load(Path(sys.argv[1]).resolve())


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


lib.asciichat_shared_init.argtypes = [c.c_char_p, c.c_bool, c.c_bool]
lib.options_init.argtypes = [c.c_int, c.POINTER(c.c_char_p)]
lib.client_audio_pipeline_default_config.restype = Config
lib.client_audio_pipeline_create.argtypes = [c.POINTER(Config)]
lib.client_audio_pipeline_create.restype = c.c_void_p
lib.client_audio_pipeline_destroy.argtypes = [c.c_void_p]
lib.client_audio_pipeline_process_duplex.argtypes = [
    c.c_void_p,
    c.c_void_p,
    c.c_int,
    c.c_void_p,
    c.c_int,
    c.c_void_p,
]
previous = Path.cwd()
with tempfile.TemporaryDirectory() as directory:
    os.chdir(directory)
    assert lib.asciichat_shared_init(None, False, False) == 0
    args = [b"ascii-chat", b"--log-level=error", b"client", b"--spectral-audio=false"]
    argv = (c.c_char_p * (len(args) + 1))(*args, None)
    assert lib.options_init(len(args), argv) == 0
    results = []
    rate = 48000
    size = 18 * rate
    rng = np.random.default_rng(39)
    far = np.convolve(rng.normal(0, 0.2, size), np.ones(8) / 8, "same").astype(
        "float32"
    )
    time = np.arange(size) / rate
    pitch = 2 * np.pi * (170 * time + 3 * np.sin(2 * np.pi * 2 * time))
    near = (
        0.15
        * (0.6 + 0.4 * np.sin(2 * np.pi * 3 * time))
        * (np.sin(pitch) + 0.4 * np.sin(2 * pitch) + 0.2 * np.sin(3 * pitch))
    ).astype("float32")
    near[: 12 * rate] = 0
    for delay_ms, level in [(40, 0.4), (100, 0.8)]:
        config = lib.client_audio_pipeline_default_config()
        assert (
            config.sample_rate,
            config.frame_duration_ms,
            config.opus_bitrate,
            config.opus_application,
            config.jitter_margin_ms,
        ) == (48000, 20, 32000, 2048, 60)
        config.flags = Flags(echo_cancel=True)
        pipeline = lib.client_audio_pipeline_create(c.byref(config))
        assert pipeline
        delay = delay_ms * 48
        echo = np.zeros(size, dtype="float32")
        echo[delay:] = far[:-delay] * level
        echo[delay + 960 :] += far[: -delay - 960] * level * 0.2
        mic = echo + near
        out = np.zeros(size, dtype="float32")
        for start in range(0, size, 480):
            lib.client_audio_pipeline_process_duplex(
                pipeline,
                far[start:].ctypes.data,
                480,
                mic[start:].ctypes.data,
                480,
                out[start:].ctypes.data,
            )
        assert np.isfinite(out).all()
        erle = 10 * np.log10(
            np.mean(echo[8 * rate : 11 * rate] ** 2)
            / max(1e-12, np.mean(out[8 * rate : 11 * rate] ** 2))
        )
        # Find the analysis/synthesis filterbank delay without assuming zero latency.
        reference = near[14 * rate : 17 * rate]
        best = (-1, 0, 0)
        for lag in range(0, 2049):
            actual = out[14 * rate + lag : 17 * rate + lag]
            gain = float(reference @ actual / (reference @ reference))
            corr = float(
                reference
                @ actual
                / np.sqrt((reference @ reference) * (actual @ actual) + 1e-20)
            )
            if corr > best[0]:
                best = (corr, lag, gain)
        result = dict(
            delay_ms=delay_ms,
            echo_gain=level,
            erle_db=float(erle),
            near_correlation=best[0],
            near_delay_samples=best[1],
            near_gain=best[2],
        )
        results.append(result)
        print(json.dumps(result), flush=True)
        assert erle > 10, result
        assert best[0] > 0.5 and 0.25 < best[2] < 1.5, result
        lib.client_audio_pipeline_destroy(pipeline)
    lib.options_state_destroy()
    lib.asciichat_shared_destroy()
    os.chdir(previous)
print(
    "PASS: AEC3 echo reduction and double-talk preservation; synthetic acoustic model, not hardware certification"
)
