"""Compare actual WASAPI loopback playback against FFmpeg-decoded media.

Requires Windows, PyAudioWPatch and numpy. Artifacts stay local.
"""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import argparse
import json
import os
from pathlib import Path
import subprocess
import threading
import time
import numpy as np
import pyaudiowpatch as pa
from windows_console import WindowsConsole


def correlation(reference, sample):
    sample = sample - np.mean(sample)
    size = 1 << (len(reference) + len(sample) - 1).bit_length()
    raw = np.fft.irfft(np.fft.rfft(reference, size) * np.fft.rfft(sample[::-1], size), size)
    raw = raw[len(sample)-1:len(reference)]
    energy = np.concatenate(([0.0], np.cumsum(reference.astype(np.float64) ** 2)))
    sums = np.concatenate(([0.0], np.cumsum(reference.astype(np.float64))))
    var = energy[len(sample):] - energy[:-len(sample)] - (sums[len(sample):] - sums[:-len(sample)]) ** 2 / len(sample)
    score = raw / np.sqrt(np.maximum(var, 1e-12) * np.sum(sample ** 2) + 1e-20)
    index = int(np.argmax(np.abs(score)))
    return index, float(np.abs(score[index]))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--media", type=Path, required=True)
    parser.add_argument("--duration", type=float, default=20)
    parser.add_argument("--analyze-existing", action="store_true")
    parser.add_argument("--artifacts", type=Path, required=True)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    if args.analyze_existing:
        metadata = json.loads((args.artifacts / "capture.json").read_text(encoding="utf-8"))
        rate, channels = metadata["rate"], metadata["channels"]
        output = np.load(args.artifacts / "loopback.npy")
    else:
        term = WindowsConsole([str(args.binary.resolve()), "--no-check-update", "--log-level", "info",
                               "--log-file", str((args.artifacts / "application.log").resolve()), "mirror",
                               "--test-pattern", "--volume", "0.6",
                               "--fps", "60", "--splash-screen=false"], rows=30, cols=100)
        chunks = []
        try:
            term.expect(lambda text: text.count("oo") > 15, "Initial source", timeout=90)
            term.write("o")
            term.expect(lambda text: "Open media:" in text, "Media editor")
            term.write(str(args.media.resolve()))
            term.expect(lambda text: args.media.name in text, "Media path is fully entered")
            term.write("\r")
            term.expect(lambda text: "Open media:" not in text and "Loading media" not in text and text.count("oo") < 15,
                        "Replacement video renders", timeout=60)
            log = args.artifacts / "application.log"
            assert "Opened media:" in log.read_text(encoding="utf-8"), "Playback never opened the requested file"
            with pa.PyAudio() as audio:
                device = audio.get_default_wasapi_loopback()
                rate = int(device["defaultSampleRate"])
                channels = int(device["maxInputChannels"])
                ready = threading.Event()
                def capture():
                    with audio.open(format=pa.paFloat32, channels=channels, rate=rate, input=True,
                                    input_device_index=device["index"], frames_per_buffer=1024) as stream:
                        ready.set()
                        for _ in range(int(rate * args.duration / 1024)):
                            chunks.append(stream.read(1024, exception_on_overflow=False))
                worker = threading.Thread(target=capture)
                worker.start()
                assert ready.wait(10)
                worker.join(timeout=args.duration + 20)
                assert not worker.is_alive()
            term.interrupt()
        finally:
            term.close()
        output = np.frombuffer(b"".join(chunks), dtype=np.float32).reshape(-1, channels).mean(axis=1)
        np.save(args.artifacts / "loopback.npy", output)
        (args.artifacts / "capture.json").write_text(json.dumps({"rate":rate,"channels":channels}),encoding="utf-8")
    # Match device rate and the production mono downmix. Compare at 12kHz to
    # bound FFT cost; timing still resolves individual audio blocks.
    reference = np.frombuffer(subprocess.check_output([
        "ffmpeg", "-v", "error", "-i", str(args.media), "-t", str(args.duration + 30),
        "-vn", "-ac", "1", "-ar", str(rate), "-f", "f32le", "-"], timeout=60, stdin=subprocess.DEVNULL, stderr=subprocess.PIPE), dtype=np.float32)
    stride = max(1, rate // 12000)
    full_reference, full_output = reference, output
    reference, output = reference[::stride], output[::stride]
    reduced_rate = rate / stride
    points = []
    spectral_scores = []
    for second in range(5, int(args.duration)-2):
        start = int(second * reduced_rate)
        window = output[start:start + int(reduced_rate)]
        if np.sqrt(np.mean(window ** 2)) < 0.001: continue
        position, score = correlation(reference, window)
        reference_seconds = position / reduced_rate
        points.append((second, reference_seconds, score))
        # Device conversion and enhancement can change phase. Compare the
        # spectrum of every overlapping 43 ms block as well as source timing;
        # inserted silence, beeps or static fail this content comparison.
        count = rate
        actual = full_output[second * rate:(second + 1) * rate]
        start = round(reference_seconds * rate)
        expected = full_reference[start:start + count]
        size = 2048
        taper = np.hanning(size)
        def spectrum(samples):
            frames = np.lib.stride_tricks.sliding_window_view(samples, size)[::size // 2]
            return np.abs(np.fft.rfft(frames * taper))
        left, right = spectrum(actual), spectrum(expected)
        similarity = np.sum(left * right, axis=1) / (
            np.linalg.norm(left, axis=1) * np.linalg.norm(right, axis=1) + 1e-20)
        # Resampling/device filters can shift a transient a few milliseconds
        # relative to its dominant tone. Retain the independent whole-stream
        # timing check and allow at most 5 ms of local spectral alignment.
        for index in np.flatnonzero(similarity < 0.95):
            center = start + int(index) * (size // 2)
            for shift in range(-rate // 200, rate // 200 + 1, max(1, rate // 4000)):
                candidate = full_reference[center + shift:center + shift + size]
                if len(candidate) != size:
                    continue
                other = np.abs(np.fft.rfft(candidate * taper))
                score = np.dot(left[index], other) / (
                    np.linalg.norm(left[index]) * np.linalg.norm(other) + 1e-20)
                similarity[index] = max(similarity[index], score)
        spectral_scores.extend(similarity.tolist())
    assert len(points) >= 6, ("Too much missing audio", points)
    reference_offset = float(np.median([p-t for t,p,c in points]))
    for second in range(5, int(args.duration)-2):
        start = round((second + reference_offset) * rate)
        expected = full_reference[start:start + rate]
        actual = full_output[second * rate:(second + 1) * rate]
        if np.sqrt(np.mean(expected ** 2)) >= 0.005:
            assert np.sqrt(np.mean(actual ** 2)) >= 0.001, f"Unexpected silence at {second}s"
    result = "\n".join(f"capture_s={t} reference_s={p:.4f} correlation={c:.4f}" for t,p,c in points)
    (args.artifacts / "result.txt").write_text(result + "\n", encoding="utf-8")
    print(result)
    assert min(c for _,_,c in points) > 0.5, "Cannot identify source audio in loopback"
    spectral = f"spectral_min={min(spectral_scores):.4f} spectral_mean={np.mean(spectral_scores):.4f}"
    print(spectral)
    with (args.artifacts / "result.txt").open("a", encoding="utf-8") as result_file:
        result_file.write(spectral + "\n")
    assert min(spectral_scores) > 0.9 and np.mean(spectral_scores) > 0.98, "Playback contains spectral glitches"
    offsets = [t-p for t,p,c in points]
    assert max(offsets)-min(offsets) < 0.10, ("Playback skips, repeats or inserts gaps", offsets)
    print("PASS continuous media audio matches FFmpeg reference")

if __name__ == "__main__":
    main()
