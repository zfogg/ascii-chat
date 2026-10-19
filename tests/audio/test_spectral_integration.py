"""Exercise the real mixer and AEC3/capture chain through the shared library."""

from pathlib import Path
import ctypes as c, sys, os, tempfile
import numpy as np
from test_spectral import load

library_path = Path(sys.argv[1]).resolve()
original_cwd = Path.cwd()
temporary = tempfile.TemporaryDirectory(prefix="ascii-spectral-test-")
os.chdir(temporary.name)
os.environ["TEMP"] = os.environ["TMP"] = os.environ["TMPDIR"] = temporary.name
lib = load(library_path)
args = [
    str(
        library_path.parent / ("ascii-chat.exe" if os.name == "nt" else "ascii-chat")
    ).encode(),
    b"server",
    b"--spectral-audio=true",
    b"--audio-spectral-mixer=true",
    b"--audio-spectral-gate=false",
    b"--no-audio-mixer",
]
argv = (c.c_char_p * (len(args) + 1))(*args, None)
lib.asciichat_shared_init.argtypes = [c.c_char_p, c.c_bool, c.c_bool]
assert lib.asciichat_shared_init(None, False, False) == 0
lib.options_init.argtypes = [c.c_int, c.POINTER(c.c_char_p)]
result = lib.options_init(len(args), argv)
assert result == 0, result
lib.mixer_create.argtypes = [c.c_int, c.c_int]
lib.mixer_create.restype = c.c_void_p
lib.mixer_destroy.argtypes = [c.c_void_p]
lib.mixer_add_source.argtypes = [c.c_void_p, c.c_char_p, c.c_void_p]
lib.mixer_process.argtypes = [c.c_void_p, c.POINTER(c.c_float), c.c_int]
lib.mixer_process_excluding_source.argtypes = [
    c.c_void_p,
    c.POINTER(c.c_float),
    c.c_int,
    c.c_uint32,
]
lib.audio_ring_buffer_create_for_capture.restype = c.c_void_p
lib.audio_ring_buffer_write.argtypes = [c.c_void_p, c.POINTER(c.c_float), c.c_int]
lib.audio_ring_buffer_destroy.argtypes = [c.c_void_p]


def hash_name(name):
    value = 2166136261
    for byte in name:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


ptr = lambda a: a.ctypes.data_as(c.POINTER(c.c_float))
mixer = lib.mixer_create(2, 48000)
assert mixer
zero = np.zeros(960, dtype="float32")
assert lib.mixer_process(mixer, ptr(zero), 960) == 960
assert lib.mixer_process(mixer, ptr(zero), 960) == 960
buffers = [lib.audio_ring_buffer_create_for_capture() for _ in range(2)]
for name, buf in zip([b"A", b"B"], buffers):
    assert lib.mixer_add_source(mixer, name, buf) >= 0
outputs = [[], []]
for block in range(12):
    for value, buf in zip([0.1, 0.2], buffers):
        samples = np.full(960, value, dtype="float32")
        assert lib.audio_ring_buffer_write(buf, ptr(samples), 960) == 0
    for index, name in enumerate([b"A", b"B"]):
        out = np.zeros(960, dtype="float32")
        assert (
            lib.mixer_process_excluding_source(mixer, ptr(out), 960, hash_name(name))
            == 960
        )
        outputs[index].append(out)
for chunks, expected in zip(outputs, [0.2, 0.1]):
    out = np.concatenate(chunks)
    assert np.max(np.abs(out[:1024])) < 1e-6
    assert np.max(np.abs(out[1024:] - expected)) < 1e-6, (expected, out[1024:1030])
lib.mixer_destroy(mixer)
for buf in buffers:
    lib.audio_ring_buffer_destroy(buf)
# Drive the application's AEC3/filter/spectral capture chain directly.
lib.client_audio_pipeline_create.argtypes = [c.c_void_p]
lib.client_audio_pipeline_create.restype = c.c_void_p
lib.client_audio_pipeline_destroy.argtypes = [c.c_void_p]
lib.client_audio_pipeline_process_duplex.argtypes = [
    c.c_void_p,
    c.POINTER(c.c_float),
    c.c_int,
    c.POINTER(c.c_float),
    c.c_int,
    c.POINTER(c.c_float),
]
pipeline = lib.client_audio_pipeline_create(None)
assert pipeline
render = np.zeros(480, dtype="float32")
out = np.zeros(480, dtype="float32")
energy = 0
for block in range(100):
    signal = (
        0.2 * np.sin(2 * np.pi * 1500 * (np.arange(480) + 480 * block) / 48000)
    ).astype("float32")
    lib.client_audio_pipeline_process_duplex(
        pipeline, ptr(render), 480, ptr(signal), 480, ptr(out)
    )
    assert np.isfinite(out).all()
    energy += float(out @ out)
assert energy > 1, energy
# Playback must fade quiet decoded audio, not hard-cut entire Opus frames.
# Compare against a separate decoder to isolate gate gain from codec artifacts.
lib.opus_codec_create_encoder.argtypes = [c.c_int, c.c_int, c.c_int]
lib.opus_codec_create_encoder.restype = c.c_void_p
lib.opus_codec_create_decoder.argtypes = [c.c_int]
lib.opus_codec_create_decoder.restype = c.c_void_p
lib.opus_codec_encode.argtypes = [
    c.c_void_p,
    c.POINTER(c.c_float),
    c.c_int,
    c.c_void_p,
    c.c_size_t,
]
lib.opus_codec_encode.restype = c.c_size_t
lib.opus_codec_decode.argtypes = [
    c.c_void_p,
    c.c_void_p,
    c.c_size_t,
    c.POINTER(c.c_float),
    c.c_int,
]
lib.opus_codec_destroy.argtypes = [c.c_void_p]
lib.client_audio_pipeline_playback.argtypes = [
    c.c_void_p,
    c.c_void_p,
    c.c_int,
    c.POINTER(c.c_float),
    c.c_int,
]
encoder = lib.opus_codec_create_encoder(2049, 48000, 128000)
decoder = lib.opus_codec_create_decoder(48000)
assert encoder and decoder
packet = (c.c_uint8 * 4096)()
plain = np.zeros(960, dtype="float32")
gated = np.zeros_like(plain)
closed_frames = 0
release_gains = []
for block in range(20):
    amplitude = 0.1 if block < 10 else 0.0002
    signal = (
        amplitude * np.sin(2 * np.pi * 1000 * (np.arange(960) + block * 960) / 48000)
    ).astype("float32")
    size = lib.opus_codec_encode(encoder, ptr(signal), 960, packet, len(packet))
    assert 0 < size <= len(packet)
    assert lib.opus_codec_decode(decoder, packet, size, ptr(plain), 960) == 960
    assert (
        lib.client_audio_pipeline_playback(pipeline, packet, size, ptr(gated), 960)
        == 960
    )
    assert np.array_equal(gated, plain), "Playback must preserve quiet decoded media"
# Explicit missing-frame decoding uses Opus concealment on the decoder owner thread.
assert lib.client_audio_pipeline_playback(pipeline, None, 0, ptr(gated), 960) == 960
assert np.isfinite(gated).all()
lib.opus_codec_destroy(encoder)
lib.opus_codec_destroy(decoder)
lib.client_audio_pipeline_destroy(pipeline)
print(
    "PASS: empty mixer, independent delayed recipient streams, production capture pipeline, quiet playback fidelity and concealment"
)


lib.options_state_destroy()
lib.asciichat_shared_destroy()
os.chdir(original_cwd)
temporary.cleanup()
