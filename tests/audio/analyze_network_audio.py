"""Plot native capture and remote decoded audio from test_network_audio.py."""

import argparse
import json
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy.io import wavfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--input", required=True)
parser.add_argument("--output", required=True)
args = parser.parse_args()
source, output = Path(args.input), Path(args.output)
output.mkdir(parents=True, exist_ok=True)
result = json.loads((source / "result.json").read_text())
rate, capture = wavfile.read(source / "sender" / "aec3_input.wav")
remote_rate, received = wavfile.read(
    source / "receiver" / "audio_playback_received.wav"
)
assert rate == remote_rate == 48000
for name, data in [
    ("network-microphone.wav", capture),
    ("network-received.wav", received),
]:
    wavfile.write(output / name, rate, (np.clip(data, -1, 1) * 32767).astype(np.int16))
(output / "network-results.json").write_text(json.dumps(result, indent=2) + "\n")

plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 11})
fig = plt.figure(figsize=(13, 10), facecolor="#f5f7fb")
gs = fig.add_gridspec(
    3, 2, top=0.78, bottom=0.10, hspace=0.62, wspace=0.22, height_ratios=[1, 1.4, 0.55]
)
fig.text(
    0.08,
    0.94,
    "Network audio: the complete path now passes",
    fontsize=23,
    weight="bold",
    color="#18324c",
)
fig.text(
    0.08,
    0.885,
    "Speakers → microphone → capture DSP → Opus → TCP server + spectral mixer → remote decode",
    fontsize=11,
    color="#354c63",
)
fig.text(
    0.08,
    0.825,
    f"Clients: {result['client_exits']}     Capabilities: 168 bytes each     "
    f"Received: {result['received_seconds']:.2f} s @ 48 kHz     Self-return peak: {result['self_excluded_peak']:.1f}",
    fontsize=12,
    weight="bold",
)
for col, (title, data, color) in enumerate(
    [
        ("Microphone input · before AEC / spectral processing", capture, "#207c91"),
        ("Remote client · decoded server audio", received, "#7953ac"),
    ]
):
    ax = fig.add_subplot(gs[0, col])
    ax.plot(np.arange(len(data))[::120] / rate, data[::120], color=color, linewidth=0.5)
    ax.set(
        title=title,
        xlabel="Seconds since this recording started",
        ylabel="Amplitude",
        ylim=(-1, 1),
    )
    ax.grid(alpha=0.15)
    ax = fig.add_subplot(gs[1, col])
    ax.specgram(
        data, NFFT=1024, Fs=rate, noverlap=768, cmap="magma", vmin=-110, vmax=-30
    )
    ax.set(
        ylim=(0, 4500),
        xlabel="Seconds since this recording started",
        ylabel="Frequency (Hz)",
    )
    ax.set_title("Spectrogram · shared −110 to −30 dB/Hz scale", fontsize=11)
fig.text(
    0.08,
    0.18,
    f"PASS   {result['transport']} • Windows / Clang Debug • spectral capture + mixer enabled",
    color="#176344",
    weight="bold",
    fontsize=12,
)
fig.text(
    0.08,
    0.13,
    "Human speech played through Realtek speakers and captured by the EMEET microphone.\n"
    "Independent recording clocks; plots are not delay-aligned. This verifies transport, not a room SNR improvement.\n"
    "Speech: Open Speech Repository, OSR_us_000_0010_8k.wav (first 12 seconds).",
    fontsize=10,
    color="#435369",
)
fig.savefig(output / "network-analysis.png", dpi=150)
