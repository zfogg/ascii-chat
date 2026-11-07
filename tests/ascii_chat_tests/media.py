"""Synthetic FFmpeg media fixtures used by integration tests."""
import base64
import re
import subprocess


def lavfi_fixture(inputs, output, *, ffmpeg="ffmpeg", ffmpeg_options=(), output_options=(), **run_options):
    """Generate a fixture from lavfi sources, forwarding options to subprocess.run."""
    command = [str(ffmpeg), *ffmpeg_options]
    for source in inputs:
        command.extend(("-f", "lavfi", "-i", source))
    command.extend((*output_options, str(output)))
    return subprocess.run(command, check=True, **run_options)


def ppm_image(width, height, pixels):
    """Return a binary RGB PPM fixture with dimensions checked."""
    pixels = bytes(pixels)
    if width <= 0 or height <= 0 or len(pixels) != width * height * 3:
        raise ValueError("PPM pixels must contain exactly three bytes per pixel")
    return f"P6\n{width} {height}\n255\n".encode("ascii") + pixels


def write_pcm_wav(path, samples, *, sample_rate, channels=1, sample_width=2):
    """Write interleaved PCM bytes to a WAV fixture."""
    import wave
    with wave.open(str(path), "wb") as output:
        output.setnchannels(channels)
        output.setsampwidth(sample_width)
        output.setframerate(sample_rate)
        output.writeframes(samples)


def reconstruct(data):
    packets = list(re.finditer(rb"\x1b_G([^;]*);([A-Za-z0-9+/=]*)\x1b\\", data))
    assert packets and b"".join(p.group() for p in packets) == data, repr(data[:200])
    for i, packet in enumerate(packets):
        assert len(packet[2]) <= 4096
        assert (b"m=0" if i == len(packets) - 1 else b"m=1") in packet[1]
        if i:
            assert packet[1] in (b"m=0", b"m=1")
        else:
            assert b"a=T,f=32" in packet[1] and b"t=d,q=2" in packet[1]
    width = int(re.search(rb"s=(\d+)", packets[0][1])[1])
    height = int(re.search(rb"v=(\d+)", packets[0][1])[1])
    raw = base64.b64decode(b"".join(p[2] for p in packets), validate=True)
    assert len(raw) == width * height * 4
    return width, height, raw, len(packets)
