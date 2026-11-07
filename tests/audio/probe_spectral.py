"""Exercise the compiled spectral API (no Python DSP substitute).
Usage: python tests/audio/probe_spectral.py build/bin/asciichat.dll
"""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401


import ctypes as c
import json
from pathlib import Path
import sys
import time
import numpy as np


from ascii_chat_tests.audio_spectral import Processor, load


def test(lib):
    results = {}
    rng = np.random.default_rng(39)
    signal = rng.uniform(-0.4, 0.4, 48000).astype("float32")
    for size in (512, 1024, 2048):
        p = Processor(lib, size)
        x = np.r_[signal, np.zeros(size, dtype="float32")]
        start = time.perf_counter()
        out = p.process(x, (1, 127, 480, 960, 13))
        elapsed = time.perf_counter() - start
        error = float(np.max(np.abs(out[size:] - signal)))
        assert error < 2e-6, (size, error)
        assert np.max(np.abs(out[:size])) < 2e-6
        lib.spectral_reset(p.handle)
        inplace = p.process(x, (960,), inplace=True)
        assert np.max(np.abs(out - inplace)) < 1e-6
        p.close()
        results[str(size)] = {
            "max_reconstruction_error": error,
            "latency_ms": size / 48,
            "wall_seconds_per_audio_second": elapsed,
        }
    cfg = lib.spectral_default_config(48000)
    cfg.fft_size = 777
    pointer = c.c_void_p()
    assert (
        lib.spectral_create(c.byref(cfg), c.byref(pointer)) != 0 and not pointer.value
    )
    p = Processor(lib)
    tone = (0.3 * np.sin(2 * np.pi * 1500 * np.arange(48000) / 48000)).astype("float32")
    p.process(tone)
    bins = c.c_size_t()
    spectrum = np.ctypeslib.as_array(
        lib.spectral_spectrum(p.handle, c.byref(bins)), shape=(bins.value,)
    )
    assert np.argmax(spectrum) == 32
    assert abs(spectrum[32] - 0.3) < 1e-5
    p.close()
    # Stationary hum followed by a stronger voiced-frequency burst.
    t = np.arange(48000 * 4) / 48000
    hum = 0.02 * np.sin(2 * np.pi * 187.5 * t)
    speech_tone = np.where(t > 2, 0.2 * np.sin(2 * np.pi * 1500 * t), 0)
    p = Processor(lib, gate=True)
    out = p.process(np.r_[hum + speech_tone, np.zeros(1024)])[1024:]
    attenuation = 20 * np.log10(np.std(out[48000:90000]) / np.std(hum[48000:90000]))
    assert attenuation < -12, attenuation
    assert np.std(out[110000:]) > 0.09
    results["gate_attenuation_db"] = float(attenuation)
    p.close()
    p = Processor(lib, compressor=True)
    out = p.process(np.r_[tone * 2, np.zeros(1024)])[1024:]
    assert np.std(out[24000:]) < 0.2
    results["compressed_rms"] = float(np.std(out[24000:]))
    p.close()
    p = Processor(lib, eq=True)
    out = p.process(np.r_[signal, np.zeros(1024)])
    assert np.isfinite(out).all() and np.max(np.abs(out)) < 1
    p.close()
    # A slightly sharp A4 should be detected and corrected without changing duration.
    p = Processor(lib, size=2048, pitch=True)
    t = np.arange(96000) / 48000
    sharp = 0.2 * np.sin(2 * np.pi * 450 * t)
    out = p.process(np.r_[sharp, np.zeros(2048)])[2048:]
    lib.spectral_pitch_hz(p.handle)
    frequency = np.fft.rfftfreq(48000, 1 / 48000)[
        np.argmax(np.abs(np.fft.rfft(out[24000:72000])))
    ]
    assert abs(frequency - 440) <= 1, frequency
    results["corrected_pitch_hz"] = float(frequency)
    p.close()
    p = Processor(lib, agc=True)
    out = p.process(np.r_[0.02 * np.sin(2 * np.pi * 1500 * t), np.zeros(1024)])[1024:]
    assert 0.04 < np.std(out[48000:]) < 0.07
    p.close()
    p = Processor(lib, gate=True, compressor=True, eq=True)
    bad = np.array([np.nan, np.inf, -np.inf] + [0.0] * 5000, dtype="float32")
    assert np.isfinite(p.process(bad)).all()
    lib.spectral_reset(p.handle)
    assert np.max(np.abs(p.process(np.zeros(4096)))) == 0
    p.close()
    for size in (512, 1024, 2048):
        p = Processor(
            lib, size=size, gate=True, compressor=True, eq=True, pitch=True, agc=True
        )
        start = time.perf_counter()
        p.process(signal, (480,))
        size_result = results[str(size)]
        assert isinstance(size_result, dict)
        size_result["all_effects_wall_seconds_per_audio_second"] = (
            time.perf_counter() - start
        )
        p.close()
    return results


if __name__ == "__main__":
    result = test(load(sys.argv[1]))
    print(json.dumps(result, indent=2))
    if len(sys.argv) > 2:
        Path(sys.argv[2]).write_text(json.dumps(result, indent=2))
