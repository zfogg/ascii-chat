# FFTW spectral audio processing

## Shared microphone processing

Native and browser clients use `audio_processor_t` (`audio/processor.h`) for the
same PCM enhancement: spectral denoising, gain control, high/low-pass filters,
optional noise gating, compression and soft clipping. Configuration is explicit;
the processor does not read CLI options, capture devices, encode Opus or send
packets. Each instance has one processing owner. Processing accepts arbitrary
block sizes and in-place buffers, with no allocations or locks in the hot path.
Gain metering follows the sample clock so changing callback sizes does not change
the result. Spectral AGC/compression replace their traditional equivalents.

The native client first runs AEC3 using its actual speaker playback reference,
then calls the shared processor. Browser microphone capture requests native
`getUserMedia` echo cancellation, with browser noise suppression and gain control
disabled to avoid applying those effects twice. An AudioWorklet transfers bounded,
reusable 20 ms PCM buffers directly to a dedicated worker running the shared C
processor and Opus encoder. Audio processing does not run on the UI thread.
The worker uses its own WASM instance and codec state, independent of playback.
Synthetic test audio bypasses microphone enhancement.

The native server separately performs participant mixing, crowd scaling,
compression and clipping. Server spectral processing remains opt-in. A server mix
does not replace microphone enhancement or local echo cancellation.

Browser AEC must be assessed with real speakers and microphones, including
simultaneous speech and output-device changes, because received audio is rendered
through Web Audio rather than an incoming WebRTC audio track. Requested
constraints and `getSettings()` alone do not measure echo removal. A WASM AEC3
backend should only replace browser AEC if acoustic testing shows a need; running
both cancellers on the same microphone is not the intended configuration.

`tests/audio/test_processor.py` verifies the compiled PCM processor's callback
size invariance, reset, in-place processing and media bypass, and can emit raw
float32 native/WASM comparison fixtures with an optional output directory.

Microphone spectral noise reduction is enabled by default. Media files and
`--test-audio` bypass microphone processing. When microphone and media are mixed,
media samples are added after the microphone processor. Other spectral effects
remain opt-in.

```sh
ascii-chat client localhost --audio=true
ascii-chat client localhost --spectral-audio=false
```

Server mixing does not apply spectral processing by default, including when a
discovery client hosts the call. `--audio-spectral-mixer=true` explicitly enables
processing of the entire mixed output, including any media/test sources. This is
separate from `--spectral-audio`, which controls microphone capture only. Enabling
both adds both processors' latency. Existing server configurations that used
`--audio-spectral=true` for mix processing must switch to the new mixer flag.
The microphone flag is now `--spectral-audio` (`ASCII_CHAT_SPECTRAL_AUDIO` in
the environment).

| Option | Effect |
| --- | --- |
| `--audio-fft-size 512/1024/2048` | Frequency resolution versus delay; default 1024 |
| `--audio-spectral-gate=false` | Disable adaptive per-bin gating |
| `--audio-multiband` | Four-band compression at 250, 2000, and 8000 Hz |
| `--audio-adaptive-eq` | Slowly balance band energy, bounded to +/-3 dB |
| `--audio-spectral-agc` | Spectral RMS gain control, bounded to 0.25–4x |
| `--audio-pitch-correct` | Experimental monophonic correction to the nearest semitone (capture only) |

The normal `ASCII_CHAT_...` environment variables and registry configuration apply.
The FFT size is validated before audio startup. Effects are selected when creating
a pipeline; these are startup settings, not live controls.

## Processing contract

The mono processor uses double-precision FFTW transforms, a periodic Hann window,
75% overlap, and weighted overlap-add. It accepts arbitrary block sizes and in-place
buffers. It returns exactly the input sample count with exactly one FFT-size of
latency: 10.67/21.33/42.67 ms at 48 kHz. Feed that many trailing zeros to flush a
finite recording. With all effects disabled, it reconstructs the delayed input.
Turning off `--spectral-audio` bypasses the processor entirely, without added delay.

Plans and buffers are created before capture starts. Planning/destruction is
serialized because FFTW's planner is shared; processing uses no allocations or
locks. Each processor has one owner thread. Each mixer recipient has its own
history, and removing a recipient resets that history. Mixer access remains
serialized to protect mutable DSP state.

The gate learns a startup noise estimate over approximately 250 ms, then adapts
asymmetrically with attack/release smoothing and a default maximum reduction of
18 dB. A quiet startup helps calibration. It cannot reliably separate stationary
speech or music from noise in every situation. EQ is bounded spectral balancing,
not calibrated room-response inversion. Pitch correction is a simple phase
vocoder for monophonic input; it can color speech and should remain off for normal
calls or polyphonic music. Pitch detection requires at least two periods within
the FFT window (larger windows cover lower voices).

Existing WebRTC AEC3 remains before spectral capture processing. Enabling spectral
AGC or compression disables the corresponding broadband capture stage to avoid
stacking it. Server spectral effects follow its existing mixer chain.

The public API is in `include/ascii-chat/audio/spectral.h`. `spectral_config_t`
exposes crossover frequencies, thresholds, compression ratios, and reduction depth.
`spectral_spectrum()` exposes normalized input amplitudes for monitoring from the
owner thread. `spectral_pitch_hz()` returns the latest detected pitch when pitch
correction is enabled. Reset clears overlap, calibration, and effect state.

## Build and tests

FFTW3 is a required native dependency. Installers and vcpkg manifests include it;
CMake links it unconditionally. Musl/iOS source builds use the pinned FFTW 3.3.10
archive rather than a host library. FFTW is GPL-2.0-or-later; its license is included
in `docs/third-party/FFTW-COPYING.txt` and installed with the runtime. The dependency's
license applies in addition to this repository's source license.

```sh
cmake --preset default -B build
cmake --build build
python -m pip install numpy scipy matplotlib sounddevice soundfile
python tests/audio/test_spectral.py build/bin/asciichat.dll
python tests/audio/test_spectral_integration.py build/bin/asciichat.dll
```

Pass the shared library path for your platform. The tests call the compiled C/C++
implementation through ctypes; Python is not a substitute DSP implementation.
They cover reconstruction, latency, chunk partitioning, in-place processing,
frequency accuracy, gating, compression, EQ bounds, pitch correction, gain control,
non-finite input, reset, empty-mixer locking, recipient isolation, and the existing
AEC3/capture chain. Benchmarks include Python call overhead and report wall time,
not whole-application CPU utilization. Criterion is not supported on Windows.

For physical recording, list device indices with `python -m sounddevice`, obtain
the attributed speech fixture described in the evidence README, then run:

```sh
python tests/audio/record_spectral.py --library build/bin/asciichat.dll --source speech-source.wav --input-device 34 --output-device 27
python tests/audio/analyze_spectral.py --library build/bin/asciichat.dll --source speech-source.wav
```

The recorder plays 12 seconds of speech between silent intervals and saves raw
microphone and processed WAVs. Choose device indices for your machine. The analysis
adds a seeded noise test with a known clean reference, aligns the processor delay,
and renders the same-scale spectrogram comparison.

See [measured results and recordings](evidence/issue-39/README.md). Windows native
build and focused tests were run; musl/iOS and other host builds were not exercised.
The initial CLIENT_CAPABILITIES mismatch was fixed: pending handshake packets
already contain payload-only data and must not have another header stripped.
The native two-client TCP test now checks capability lengths, clean client exits,
nonzero remote audio, and exclusion of the sender's own audio. It plays human
speech through physical speakers into the microphone with spectral capture and
server mixing enabled. Explicit `--audio=true` avoids toggling the default on
state off. To reproduce on a machine with the selected audio devices:

```sh
python tests/audio/test_network_audio.py --binary build/bin/ascii-chat.exe --output build/network-audio --speech build/audio-evidence/speaker-speech.wav
python tests/audio/test_network_audio.py --binary build/bin/ascii-chat.exe --output build/network-encrypted --speech build/audio-evidence/speaker-speech.wav --encrypted
python tests/audio/analyze_network_audio.py --input build/network-audio --output build/network-report
```

The encrypted loopback test disables host identity verification only in its child
processes so it does not modify the user's known_hosts. This checks encrypted
transport, not server identity authentication. Network measurements and recordings
are linked in the evidence README.

## Repeating diagnostic audio

`--test-audio` enables audio, replaces microphone/file audio with a generated
20-second loop, and monitors that loop through the selected speakers. It works
in client, discovery, and mirror modes; no microphone permission or media file
is needed. `--speakers-volume` controls the local monitor and received audio.

```sh
ascii-chat client HOST --test-audio --speakers-volume 0.5
ascii-chat mirror --test-pattern 0 --test-audio --speakers-volume 0.5
```

The loop contains a soft major arpeggio (0–4 s), a three-tone chord (4–8 s),
a 150 Hz–3 kHz logarithmic sweep (8–12 s), quiet broadband noise bursts
(12–16 s), amplitude-modulated harmonics (16–18 s), and silence (18–20 s).
Soft envelopes avoid abrupt edges; the source peak is below 0.18 full scale.
The local monitor mixes with received audio without adding frames to its queue.

The synthetic source bypasses microphone capture/AEC processing, like media
input. It tests encoding, transport, server mixing, decoding, and speaker output;
it does not verify the microphone or its noise processing. The local monitor is
generated separately and is not proof that a peer received anything. Network
verification must inspect the peer's received recording.

```sh
python tests/audio/test_audio_pattern.py build/bin/asciichat.dll --wav test-audio.wav
```

This checks sample bounds, each section's energy, sweep frequency, silence,
repeatability, and arbitrary buffer boundaries at both 44.1 and 48 kHz.
