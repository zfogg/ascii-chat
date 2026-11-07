# Audio reliability and validation

Opus is the wire codec at 48 kHz with 20 ms packets. Native and browser paths
use stereo codecs for centered speech and stereo media; see [audio channels](audio-channels.md) for
device routing, stereo negotiation, and hardware-free regression coverage.

## Behavior

- Microphones use the speech Opus application at 32 kbit/s. Media and generated
  test audio use the audio application at 128 kbit/s. Native encoders share
  checked bitrate/VBR/DTX configuration; DTX is off by default. Both explicit
  client and discovery paths select this profile using the opened media source:
  video-only media with automatic microphone fallback stays in speech mode.
- Decoded playback is no longer noise-gated. Quiet media is preserved; microphone
  capture processing, including the default spectral stage, remains separate.
- Playback starts with 60 ms buffered audio. Actual underruns raise the refill
  target by 20 ms, bounded at 160 ms; stable playback lowers it toward the initial
  target. Short tails fade out and refilled playback fades in.
- Bounded sample interpolation compensates for independent device clocks, up to
  1000 ppm. Capture queues remain sample-exact.
- Invalid Opus frames can use decoder concealment instead of abandoning the rest
  of their batch. This is not recovery of missing transport packets.
- Configuration fields now say milliseconds where milliseconds are actually used.
  The echo-filter hint remains reserved; AEC3 controls its own filter configuration.

## Reproducible compiled-library checks

Install NumPy for the Python interpreter, build the native shared library, then run:

```sh
python tests/audio/probe_opus_profiles.py PATH_TO_SHARED_LIBRARY
python tests/audio/probe_playout_recovery.py PATH_TO_SHARED_LIBRARY
python tests/audio/probe_spectral_integration.py PATH_TO_SHARED_LIBRARY
python tests/audio/probe_aec3_echo.py PATH_TO_SHARED_LIBRARY
```

The library is `build/bin/asciichat.dll` on Windows,
`build/lib/libasciichat.dylib` on macOS, and `build/lib/libasciichat.so` on Linux.
ASan builds loaded into Python require the matching sanitizer runtime preloaded.

The checks cover ten accelerated minutes at each of -200/+200 ppm, bursty
10–40 ms delivery, actual underrun/refill, restart, unchanged capture queues,
independent mixed recipient streams, the production capture pipeline, quiet
playback compared sample-for-sample with an independent Opus decoder, decoder
concealment, and AEC3 echo-only/double-talk signals.

## Earlier mono baseline measurements

Native builds and compiled audio checks passed on Windows, Intel macOS, and
Linux. The clock-skew queues stayed within 2399–3361 samples without underruns.
The deterministic acoustic model measured approximately 16.7 dB and 22.8 dB echo
reduction for 40 ms and 100 ms echoes; near-end correlation was 0.976 and 0.937.
These synthetic results are not physical-room certification.

A five-minute Windows ↔ Intel Mac call used generated test audio on both ends
and an SSH-forwarded TCP connection. Both clients exited 0 and the server stayed
running. Received PCM contained 293.84 seconds on Windows and 293.26 seconds on
macOS at 48 kHz, with finite nonzero samples. Startup and shutdown account for
part of the difference from wall time; these recordings do not prove gap-free
speaker output. Recordings are local test artifacts and are not committed.

A headless Chromium browser connected over WebSocket to the updated native
server alongside a native TCP client. With generated input enabled, a 15-second
measurement sent 725 browser Opus frames and played 739 received frames
(709,440 samples), with zero reported playback underruns and no page errors.
The browser screenshot showed a connected client with active capture/playback
levels. Canonical client/mirror WASM artifacts were rebuilt from this source and
their manifest was regenerated and verified before the check.

## Boundaries

The packet format still has no sequence/timestamp contract for unreliable audio;
FEC and a lossy transport require a separate protocol change. Physical Bluetooth, device unplug/replug,
speaker-to-microphone AEC, and overnight soak coverage are not established by the
synthetic tests or the five-minute call. The SSH Mac test intentionally bypasses
microphone permission restrictions with generated audio.
