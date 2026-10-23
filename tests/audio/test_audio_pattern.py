"""Validate the compiled test signal and optionally export one loop to WAV."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401


import argparse
import ctypes as c
from pathlib import Path
import wave

import numpy as np
from test_spectral import load

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("library")
parser.add_argument("--wav")
args = parser.parse_args()
lib = load(Path(args.library).resolve())
lib.audio_test_pattern.restype = None
lib.audio_test_pattern.argtypes = [
    c.POINTER(c.c_float),
    c.c_size_t,
    c.c_uint64,
    c.c_uint32,
]


def render(position, count, rate):
    out = np.empty(count, dtype=np.float32)
    lib.audio_test_pattern(
        out.ctypes.data_as(c.POINTER(c.c_float)), count, position, rate
    )
    return out


for rate in (44100, 48000):
    signal = render(0, 20 * rate, rate)
    assert np.isfinite(signal).all()
    assert np.max(np.abs(signal)) <= 0.18
    assert np.array_equal(signal, render(20 * rate, 20 * rate, rate))
    assert np.max(np.abs(signal[18 * rate :])) == 0
    for start in (0, 127, 4 * rate - 13, 12 * rate - 11, 20 * rate - 31):
        assert np.array_equal(
            render(start, 1024, rate), np.r_[signal, signal][start : start + 1024]
        )
    # Every audible section has meaningful energy, at conservative levels.
    for start, end in ((0, 4), (4, 8), (8, 12), (12, 16), (16, 18)):
        rms = np.sqrt(np.mean(signal[start * rate : end * rate] ** 2))
        assert 0.005 < rms < 0.12, (start, rms)
    # Verify the swept tone actually traverses the intended frequency range.
    for second, expected in ((8.5, 218), (11.5, 2063)):
        window = signal[int((second - 0.05) * rate) : int((second + 0.05) * rate)]
        peak = (
            np.argmax(abs(np.fft.rfft(window * np.hanning(len(window)))))
            * rate
            / len(window)
        )
        assert abs(peak - expected) < 60, (second, peak)
if args.wav:
    with wave.open(args.wav, "wb") as wav:
        wav.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
        wav.writeframes((signal * 32767).astype("<i2").tobytes())
print(
    "PASS: peak/RMS bounds, quiet rest, repeat, arbitrary block boundaries, sweep, 44.1/48 kHz"
)
