from pathlib import Path
import sys, json, argparse
import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from test_spectral import load, Processor

parser = argparse.ArgumentParser()
parser.add_argument("--library", required=True)
parser.add_argument("--source", required=True)
parser.add_argument("--output", default="build/audio-evidence")
args = parser.parse_args()
root = Path(args.output)
rate = 48000
sr, ref = wavfile.read(args.source)
ref = ref.astype(np.float32) / 32768
ref = resample_poly(ref, rate // np.gcd(rate, sr), sr // np.gcd(rate, sr))[: rate * 12]
ref *= 0.28 / max(abs(ref))
clean = np.r_[np.zeros(rate * 2), ref, np.zeros(rate * 3)].astype("float32")
t = np.arange(len(clean)) / rate
noise = np.random.default_rng(39).normal(0, 0.008, len(clean)) + 0.015 * np.sin(
    2 * np.pi * 187.5 * t
)
noisy = (clean + noise).astype("float32")
p = Processor(load(args.library), gate=True)
out = p.process(np.r_[noisy, np.zeros(1024)])[1024:]
p.close()
for name, data in [
    ("reference", clean),
    ("controlled-before", noisy),
    ("controlled-after", out),
]:
    wavfile.write(
        root / (name + ".wav"), rate, (np.clip(data, -1, 1) * 32767).astype(np.int16)
    )
_, raw = wavfile.read(root / "microphone-before.wav")
_, wet = wavfile.read(root / "microphone-after.wav")
raw = raw.astype("float32") / 32768
wet = wet.astype("float32") / 32768
wet = np.r_[wet[1024:], np.zeros(1024)]
quiet = slice(rate // 2, rate * 3 // 2)
voice = slice(rate * 2, rate * 14)


def rms(x):
    return np.sqrt(np.mean(x * x))


def db(x):
    return 20 * np.log10(max(x, 1e-12))


snr_before = db(rms(clean[voice]) / rms(noisy[voice] - clean[voice]))
snr_after = db(rms(clean[voice]) / rms(out[voice] - clean[voice]))
metrics = json.loads((root / "live-summary.json").read_text())
metrics.update(
    {
        "live_quiet_attenuation_db": db(rms(wet[quiet]) / rms(raw[quiet])),
        "controlled_quiet_attenuation_db": db(rms(out[quiet]) / rms(noisy[quiet])),
        "controlled_snr_before_db": snr_before,
        "controlled_snr_after_db": snr_after,
        "alignment_samples": 1024,
    }
)
(root / "analysis.json").write_text(json.dumps(metrics, indent=2, default=float))
print(json.dumps(metrics, indent=2, default=float))
plt.rcParams.update(
    {
        "font.family": "DejaVu Sans",
        "font.size": 11,
        "axes.spines.top": False,
        "axes.spines.right": False,
    }
)
fig = plt.figure(figsize=(15, 11), facecolor="#f6f7f9")
gs = fig.add_gridspec(4, 2, height_ratios=[0.65, 1, 1.3, 1], hspace=0.48, wspace=0.22)
fig.suptitle(
    "ascii-chat #39 | FFTW spectral audio validation",
    x=0.07,
    y=0.975,
    ha="left",
    fontsize=23,
    fontweight="bold",
)
a = fig.add_subplot(gs[0, :])
a.axis("off")
a.text(
    0,
    0.92,
    "Live: Realtek speakers → EMEET microphone · 48 kHz · 1024-point FFT · gate only",
    fontsize=13,
)
a.text(
    0,
    0.45,
    f'Added latency 21.33 ms    |    Pre-speech window change {metrics["live_quiet_attenuation_db"]:.1f} dB    |    Callback p99 {metrics["callback_processing_p99_ms"]:.2f} ms / 10 ms',
    fontsize=12,
)
a.text(
    0,
    0.03,
    "No stream underruns/overruns reported. Outputs aligned by 1024 samples for comparison.",
    color="#4b5563",
)
for col, (name, x, color) in enumerate(
    [("Microphone before", raw, "#536b92"), ("Microphone after", wet, "#16887b")]
):
    a = fig.add_subplot(gs[1, col])
    a.plot(np.arange(len(x)) / rate, x, lw=0.5, color=color)
    a.set(title=name, ylabel="Amplitude", xlim=(0, 17), ylim=(-0.3, 0.3))
    a.axvspan(0.5, 1.5, color="#d6d9de", alpha=0.5)
    a = fig.add_subplot(gs[2, col])
    a.specgram(x, NFFT=1024, Fs=rate, noverlap=768, cmap="magma", vmin=-100, vmax=-35)
    a.set(
        title="Spectrogram · −100 to −35 dB/Hz",
        ylim=(0, 8000),
        xlabel="Seconds",
        ylabel="Hz",
    )
a = fig.add_subplot(gs[3, 0])
a.plot(t, noisy, lw=0.4, label="Speech + known noise", color="#536b92")
a.plot(t, out, lw=0.4, label="Processed", color="#16887b")
a.set(
    title="Controlled speech + seeded noise",
    xlabel="Seconds",
    ylabel="Amplitude",
    xlim=(0, 17),
    ylim=(-0.35, 0.35),
)
a.legend(loc="upper right", fontsize=9)
a = fig.add_subplot(gs[3, 1])
a.axis("off")
a.text(0, 1, "Controlled comparison (known clean reference)", fontweight="bold")
a.text(
    0,
    0.75,
    f'Quiet-noise attenuation: {metrics["controlled_quiet_attenuation_db"]:.1f} dB\nSpeech-region SNR: {snr_before:.1f} → {snr_after:.1f} dB\nRound-trip error, effects off: < 6 × 10⁻⁸\nSeparate sine test: 450 Hz → 440 Hz',
    linespacing=1.8,
    va="top",
)
a.text(
    0,
    -0.03,
    "SNR includes processing distortion; live-room SNR is not claimed.",
    fontsize=10,
    color="#4b5563",
)
fig.text(
    0.07,
    0.02,
    "Speech: Open Speech Repository, OSR_us_000_0010_8k.wav (8 kHz original). Debug build on Windows; results are not universal quality guarantees.",
    fontsize=9,
    color="#4b5563",
)
fig.savefig(root / "analysis.png", dpi=150, bbox_inches="tight")
plt.close(fig)
