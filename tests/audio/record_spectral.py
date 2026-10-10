from pathlib import Path
import time
import json
import argparse
import sounddevice as sd
import soundfile as sf
import numpy as np
from test_spectral import load, Processor

parser = argparse.ArgumentParser(
    description="Play attributed speech and record the microphone through the compiled DSP"
)
parser.add_argument("--library", required=True)
parser.add_argument("--source", required=True)
parser.add_argument("--output", default="build/audio-evidence")
parser.add_argument("--input-device", type=int)
parser.add_argument("--output-device", type=int)
args = parser.parse_args()
root = Path(args.output)
root.mkdir(parents=True, exist_ok=True)
rate = 48000
input_device = (
    args.input_device if args.input_device is not None else sd.default.device[0]
)
output_device = (
    args.output_device if args.output_device is not None else sd.default.device[1]
)
source, sr = sf.read(args.source, dtype="float32")
# Linear interpolation preserves the source's 8 kHz bandwidth.
speech = np.interp(
    np.arange(12 * rate) / rate, np.arange(len(source)) / sr, source
).astype("float32")
speech *= 0.28 / max(np.max(np.abs(speech)), 1e-6)
play = np.r_[np.zeros(2 * rate), speech, np.zeros(3 * rate)].astype("float32")
sf.write(root / "speaker-speech.wav", play, rate, subtype="PCM_16")
lib = load(args.library)
p = Processor(lib, gate=True)
captured = []
processed = []
timing = []
status_log = []
position = 0


def callback(indata, outdata, frames, when, status):
    global position
    if status:
        status_log.append(str(status))
    mono = np.ascontiguousarray(indata.mean(axis=1), dtype="float32")
    before = time.perf_counter()
    output = p.process(mono)
    timing.append(time.perf_counter() - before)
    captured.append(mono.copy())
    processed.append(output)
    outdata.fill(0)
    n = min(frames, len(play) - position)
    if n > 0:
        outdata[:n, :] = play[position : position + n, None]
    position += n
    if position >= len(play):
        raise sd.CallbackStop


with sd.Stream(
    device=(input_device, output_device),
    samplerate=rate,
    blocksize=480,
    channels=(
        min(2, sd.query_devices(input_device)["max_input_channels"]),
        min(2, sd.query_devices(output_device)["max_output_channels"]),
    ),
    dtype="float32",
    callback=callback,
) as stream:
    while stream.active:
        sd.sleep(100)
raw = np.concatenate(captured)
wet = np.concatenate(processed)
sf.write(root / "microphone-before.wav", raw, rate, subtype="PCM_16")
sf.write(root / "microphone-after.wav", wet, rate, subtype="PCM_16")
summary = {
    "input_device": str(sd.query_devices(input_device)["name"]),
    "output_device": str(sd.query_devices(output_device)["name"]),
    "sample_rate": rate,
    "duration_seconds": len(raw) / rate,
    "input_rms": float(np.std(raw)),
    "input_peak": float(np.max(np.abs(raw))),
    "callback_processing_max_ms": max(timing) * 1000,
    "callback_processing_p99_ms": float(np.percentile(timing, 99) * 1000),
    "stream_status": status_log,
    "source": str(Path(args.source).name),
    "source_url": "https://www.voiptroubleshooter.com/open_speech/american/OSR_us_000_0010_8k.wav",
}
(root / "live-summary.json").write_text(json.dumps(summary, indent=2))
print(json.dumps(summary, indent=2))
p.close()
