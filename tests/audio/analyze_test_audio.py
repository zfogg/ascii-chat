"""Plot generated and peer-received diagnostic audio on identical scales."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401


import argparse
import json
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import soundfile as sf
from scipy.signal import spectrogram

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", required=True)
parser.add_argument("--windows-received", required=True)
parser.add_argument("--mac-received", required=True)
parser.add_argument("--output", required=True)
args = parser.parse_args()
output = Path(args.output)
output.mkdir(parents=True, exist_ok=True)
fig, axes = plt.subplots(3, 2, figsize=(14, 9), layout="constrained")
measurements = {}
for row, (name, filename) in enumerate(
    (
        ("Generated source (one loop)", args.source),
        ("Windows received from Mac", args.windows_received),
        ("Mac received from Windows", args.mac_received),
    )
):
    signal, rate = sf.read(filename, dtype="float32")
    assert signal.ndim == 1 and np.isfinite(signal).all()
    duration = len(signal) / rate
    rms = float(np.sqrt(np.mean(signal**2)))
    peak = float(np.max(np.abs(signal)))
    assert rms > 0.001 and peak < 1, (name, rms, peak)
    if row:
        assert duration >= 55, (name, duration)
    measurements[name] = dict(seconds=duration, sample_rate=rate, rms=rms, peak=peak)
    time = np.arange(len(signal)) / rate
    axes[row, 0].plot(time[::96], signal[::96], linewidth=0.5)
    axes[row, 0].set(
        title=f"{name} — {duration:.2f} s", ylim=(-0.2, 0.2), ylabel="Amplitude"
    )
    axes[row, 0].grid(alpha=0.2)
    frequency, times, power = spectrogram(signal, rate, nperseg=1024, noverlap=768)
    db = 10 * np.log10(np.maximum(power, 1e-12))
    mesh = axes[row, 1].pcolormesh(
        times, frequency, db, vmin=-100, vmax=-35, shading="auto", cmap="magma"
    )
    axes[row, 1].set(
        ylim=(0, 4000),
        ylabel="Frequency (Hz)",
        title="Same color scale; silence uses the color floor",
    )
    for axis in axes[row]:
        axis.set(xlim=(0, 60), xlabel="Recording time (s)")
    # The reference contains only one loop; do not imply missing recorded data.
    if row == 0:
        for axis in axes[row]:
            axis.set_xlim(0, 20)
fig.colorbar(mesh, ax=axes[:, 1], label="Power spectral density (dB/Hz)")
fig.suptitle(
    "Repeating test audio • encrypted Windows ↔ Mac TCP call\n"
    "Independent recording clocks; received files exclude the local monitor. Final 2 s of each source loop are silent."
)
fig.savefig(output / "test-audio-analysis.png", dpi=150)
(output / "test-audio-results.json").write_text(
    json.dumps(measurements, indent=2) + "\n"
)
print(json.dumps(measurements, indent=2))
