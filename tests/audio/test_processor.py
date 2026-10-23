"""Exercise the production PCM processor and optionally emit native/WASM parity fixtures."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import ctypes as c
from pathlib import Path
import sys
import numpy as np
from processor_ctypes import Config, Flags
from test_spectral import load


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def test(path, fixtures=None):
    lib = load(path)
    lib.audio_processor_default_config.argtypes = [c.c_int]
    lib.audio_processor_default_config.restype = Config
    lib.audio_processor_create.argtypes = [c.POINTER(Config), c.POINTER(c.c_void_p)]
    lib.audio_processor_process.argtypes = [c.c_void_p, c.POINTER(c.c_float), c.POINTER(c.c_float), c.c_size_t]
    lib.audio_processor_reset.argtypes = [c.c_void_p]
    lib.audio_processor_destroy.argtypes = [c.c_void_p]
    config = lib.audio_processor_default_config(48000)
    t = np.arange(48000, dtype="float32") / 48000
    signal = (0.12 * np.sin(2 * np.pi * 700 * t) + 0.02 * np.sin(2 * np.pi * 50 * t)).astype("float32")
    signal[:12000] = 0
    pointer = lambda a: a.ctypes.data_as(c.POINTER(c.c_float))

    def process(blocks, inplace=False):
        handle = c.c_void_p()
        result = lib.audio_processor_create(c.byref(config), c.byref(handle))
        check(result == 0, f"Processor creation failed: {result}")
        try:
            def run():
                output = signal.copy() if inplace else np.zeros_like(signal)
                position = index = 0
                while position < len(signal):
                    count = min(blocks[index % len(blocks)], len(signal) - position)
                    source = output if inplace else signal
                    result = lib.audio_processor_process(handle, pointer(source[position:]), pointer(output[position:]), count)
                    check(result == 0, f"Processor processing failed at sample {position}: {result}")
                    position += count
                    index += 1
                return output
            output = run()
            lib.audio_processor_reset(handle)
            np.testing.assert_array_equal(output, run())
            return output
        finally:
            lib.audio_processor_destroy(handle)

    reference = process([480])
    check(np.isfinite(reference).all() and np.max(np.abs(reference)) <= 1, "Processor output must be finite and bounded")
    check(np.max(np.abs(reference[24000:])) > 0.01, "Processor must preserve the input signal")
    np.testing.assert_array_equal(reference, process([128, 960, 13, 1, 127]))
    np.testing.assert_array_equal(reference, process([960], inplace=True))
    if fixtures:
        target = Path(fixtures)
        target.mkdir(parents=True, exist_ok=True)
        signal.tofile(target / "input.f32")
        reference.tofile(target / "native.f32")
    config.spectral_enabled = False
    config.flags = Flags()
    np.testing.assert_array_equal(signal, process([128, 960, 7], inplace=True))
    print("PASS: PCM processor block invariance, in-place processing, reset, bounds, and media bypass")


if __name__ == "__main__":
    test(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
