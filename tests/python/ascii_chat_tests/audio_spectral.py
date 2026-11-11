"""ctypes and NumPy helpers for the native spectral processor."""
import ctypes as c
import os
from pathlib import Path
import numpy as np


dll_dirs = []


class Config(c.Structure):
    _fields_ = [
        ("sample_rate", c.c_int),
        ("fft_size", c.c_int),
        ("noise_gate", c.c_bool),
        ("compressor", c.c_bool),
        ("adaptive_eq", c.c_bool),
        ("pitch_correct", c.c_bool),
        ("auto_gain", c.c_bool),
        ("reduction_db", c.c_float),
        ("crossovers", c.c_float * 3),
        ("thresholds_db", c.c_float * 4),
        ("ratios", c.c_float * 4),
    ]


class Processor:
    def __init__(
        self,
        library,
        size=1024,
        gate=False,
        compressor=False,
        eq=False,
        pitch=False,
        agc=False,
    ):
        self.lib = library
        self.config = library.spectral_default_config(48000)
        self.config.fft_size = size
        self.config.noise_gate = gate
        self.config.compressor = compressor
        self.config.adaptive_eq = eq
        self.config.pitch_correct = pitch
        self.config.auto_gain = agc
        self.handle = c.c_void_p()
        assert library.spectral_create(c.byref(self.config), c.byref(self.handle)) == 0

    def close(self):
        if self.handle:
            self.lib.spectral_destroy(self.handle)
            self.handle = c.c_void_p()

    def process(self, samples, blocks=(480,), inplace=False):
        samples = np.ascontiguousarray(samples, dtype=np.float32)
        out = samples.copy() if inplace else np.zeros_like(samples)
        pos = block = 0
        while pos < len(samples):
            n = min(blocks[block % len(blocks)], len(samples) - pos)
            source = out[pos : pos + n] if inplace else samples[pos : pos + n]
            assert (
                self.lib.spectral_process(
                    self.handle,
                    source.ctypes.data_as(c.POINTER(c.c_float)),
                    out[pos : pos + n].ctypes.data_as(c.POINTER(c.c_float)),
                    n,
                )
                == 0
            )
            pos += n
            block += 1
        return out


def load(path):
    path = Path(path).resolve()
    if os.name == "nt":
        global dll_dirs
        dirs = [
            path.parent,
            path.parent.parent / "vcpkg_installed/x64-windows/debug/bin",
            path.parent.parent / "vcpkg_installed/x64-windows/bin",
        ]
        dll_dirs = [os.add_dll_directory(str(p)) for p in dirs if p.is_dir()]
    lib = c.CDLL(str(path))
    lib.spectral_default_config.argtypes = [c.c_int]
    lib.spectral_default_config.restype = Config
    lib.spectral_create.argtypes = [c.POINTER(Config), c.POINTER(c.c_void_p)]
    lib.spectral_process.argtypes = [
        c.c_void_p,
        c.POINTER(c.c_float),
        c.POINTER(c.c_float),
        c.c_size_t,
    ]
    lib.spectral_destroy.argtypes = [c.c_void_p]
    lib.spectral_reset.argtypes = [c.c_void_p]
    lib.spectral_pitch_hz.argtypes = [c.c_void_p]
    lib.spectral_pitch_hz.restype = c.c_float
    lib.spectral_spectrum.argtypes = [c.c_void_p, c.POINTER(c.c_size_t)]
    lib.spectral_spectrum.restype = c.POINTER(c.c_float)
    return lib
