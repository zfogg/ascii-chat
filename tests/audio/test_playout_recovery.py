"""Compiled playback buffering regression: stalls, restart, and device clock skew."""

import ctypes as c
from pathlib import Path
import sys
import numpy as np
from test_spectral import load

library = Path(sys.argv[1]).resolve()
lib = load(library)
lib.asciichat_shared_init.argtypes = [c.c_char_p, c.c_bool, c.c_bool]
assert lib.asciichat_shared_init(None, False, False) == 0
lib.audio_ring_buffer_create.restype = c.c_void_p
lib.audio_ring_buffer_create_for_capture.restype = c.c_void_p
lib.audio_ring_buffer_write.argtypes = [c.c_void_p, c.c_void_p, c.c_int]
lib.audio_ring_buffer_read.argtypes = [c.c_void_p, c.c_void_p, c.c_size_t]
lib.audio_ring_buffer_read.restype = c.c_size_t
lib.audio_ring_buffer_available_read.argtypes = [c.c_void_p]
lib.audio_ring_buffer_available_read.restype = c.c_size_t
lib.audio_ring_buffer_clear.argtypes = [c.c_void_p]
lib.audio_ring_buffer_destroy.argtypes = [c.c_void_p]
lib.audio_ring_buffer_set_jitter_target.argtypes = [c.c_void_p, c.c_uint32]
signal = np.full(12000, 0.1, dtype="float32")
out = np.zeros(480, dtype="float32")


def write(rb, n):
    assert lib.audio_ring_buffer_write(rb, signal.ctypes.data, n) == 0


def read(rb, n=480):
    result = lib.audio_ring_buffer_read(rb, out.ctypes.data, n)
    assert np.isfinite(out[:result]).all()
    return result


rb = lib.audio_ring_buffer_create()
write(rb, 2400)
assert read(rb) == 0
write(rb, 480)
assert read(rb) == 480
while lib.audio_ring_buffer_available_read(rb) >= 480:
    assert read(rb) == 480
assert read(rb) < 480
write(rb, 2880)
assert read(rb) == 0, "actual underrun must increase refill target"
write(rb, 960)
assert read(rb) == 480
lib.audio_ring_buffer_clear(rb)
write(rb, 2400)
assert read(rb) == 0, "clear must reset startup buffering"
write(rb, 480)
assert read(rb) == 480
lib.audio_ring_buffer_destroy(rb)
# Ten minutes each, accelerated: positive and negative 200 ppm source clock skew.
for ppm in (-200, 200):
    rb = lib.audio_ring_buffer_create()
    write(rb, 2880)
    phase = 0.0
    levels = []
    for tick in range(60000):
        assert read(rb) == 480, (ppm, tick)
        phase += 480 * (1 + ppm / 1e6)
        count = int(phase)
        phase -= count
        write(rb, count)
        levels.append(lib.audio_ring_buffer_available_read(rb))
    assert 1000 < min(levels) and max(levels) < 6000, (ppm, min(levels), max(levels))
    lib.audio_ring_buffer_destroy(rb)
    print(
        f"PASS: ten-minute {ppm:+} ppm clock skew, occupancy {min(levels)}..{max(levels)} samples"
    )
# Capture queues must remain sample-exact and never perform drift correction.
rb = lib.audio_ring_buffer_create_for_capture()
write(rb, 480)
assert read(rb) == 480
assert np.array_equal(out, signal[:480])
lib.audio_ring_buffer_destroy(rb)
lib.asciichat_shared_destroy()
print("PASS: adaptive refill, restart, clock skew, unchanged capture queues")
