# Recording session audio and ASCII video

`--render-file recording.mp4` records the session's ASCII video and its audio mix
in client, discovery, and mirror modes. Mirror is a solo session: it records local
audio without playing the microphone back through the speakers.

Local capture and media playback follow `--audio-capture-source`:

- `auto`: file/URL audio when available, otherwise the microphone.
- `mic`: the microphone.
- `media`: file/URL audio.
- `both`: microphone and file/URL audio mixed together.

Network sessions also record received participant audio. Speaker playback remains
separate from recording, so recording does not consume playback or outgoing audio
buffers. Local file audio plays through the speakers in mirror mode; in a call it
is sent to other participants and included in the recording.

When `--file` or `--url` contains only audio, the selected webcam supplies the video.
The webcam is opened when capture starts, rather than during metadata probing.
The file retains its usual pause, seek, and loop controls. Without a usable webcam,
capture reports an error identifying the selected camera.

Examples:

```sh
ascii-chat mirror --render-file solo.mp4
ascii-chat mirror --file music.wav --render-file music-with-webcam.mp4
ascii-chat client localhost --file clip.mp4 --render-file call.mp4
ascii-chat client localhost --file music.wav --audio-capture-source both --render-file call.mp4
```

Video containers such as MP4 and WebM support audio. Still images and GIF do not.
Audio is mixed at 48 kHz mono and clipped after summing inputs. Temporary missing
samples become silence, and subsequent samples resume normally. Live video uses
elapsed session time across source changes and pauses. Stdin conversion retains
frame-count timing.

The recording regression tests include PCM mixing, microphone and file worker
paths, independent playback/transmission queues, audio-only webcam selection,
mixed encoded audio, encoder-tail flushing, and live frame timing. A separate
mirror and two-client test checks the file tone in mirror output, both input tones
in each call output, and A/V synchronization:

```sh
python tests/integration/render_file_audio.py build/bin/ascii-chat
```


## Media transition behavior

All native client, discovery and mirror sessions use the shared capture/display
pipeline. Browser mirror, client and discovery share `useSessionRecording`,
`LocalRecording` and the `AudioPipeline` recording bus. Committed source changes do not
create a new recording or reset its session clock.

| Input / transition | Native sessions | Browser sessions | MCP `render_file` |
| --- | --- | --- | --- |
| Webcam + microphone | Elapsed-time video; selected capture audio plus received audio | Displayed ASCII canvas; enabled local inputs plus received audio | One bounded render invocation; no live source controls |
| Local file / URL | Shared FFmpeg/resolver and playlist loader; failed/cancelled replacement keeps prior source | Media element capture feeds the stable recording bus; failed audio tap replacement retains the old tap until finalization | Uses the native render path and reports tool/render errors |
| Audio-only file / URL | Webcam supplies video; file pause/seek still applies | File loader currently requires a decodable video track; audio-only file import remains unsupported | Native fallback requires a camera unless a waveform/FFT visualization is selected |
| Test pattern / waveform / FFT | Elapsed session clock, including switching to media with a different FPS | Same displayed canvas and enabled audio graph as other sources | Only sources exposed by the tool schema; no interactive switching |
| Pause | Retained video continues recording without advancing capture freshness; media audio is silent | Canvas remains visible and recording clock runs; media element pause silences its tap | No interactive pause control |
| Seek | Decoder position changes; recording timestamps remain monotonic | Retains the element tap and encoder; dynamic audio tracks reconnect to the same bus | Initial seek, where exposed, uses native options |
| Successful source change | Loader commits video/audio together; recording stays open | Canvas dimensions stay fixed for export; audio graph changes independently of the encoder | Separate invocations produce separate outputs |
| Failure / cancellation | Loader rollback retains current source; teardown drains accepted recording frames and finalizes encoder | Failed capture/start releases owned resources; disconnect invalidates pending starts and finalizes active recording; unmount discards export | Bounded invocation cleanup; no persistent recording owner |

Paused native recordings emit the last raster until recording ends, even if
playback never resumes. Their frame freshness counter remains unchanged. At
normal file EOF, native file recording still finalizes rather than extending the
last frame indefinitely. Stdin retains its frame-count conversion semantics.
Browser pause/seek preserves the stable destination audio track so changing file
tracks does not end the encoder. A repeated source notification for the same
media element does not recapture or stop its tracks.

Browser exports remain bounded to ten minutes / 128 MiB, with a ten-second
finalization watchdog. Encoder failure produces an error rather than a partial
download; a subsequent recording starts with fresh failure state. Browser source
controls and native capture policy are distinct host adapters around their shared
session APIs; MCP is a bounded render tool, not a live session controller.

Focused regression coverage:

- `tests/integration/render_file_audio.py BINARY --transitions-only`: hardware-free
  paused-file finalization, synthetic A/V duration and playlist source switching
  with both tones retained in one recording.
- `tests/standalone/media/capture_open_test.c`: retained paused recording frames,
  seek while paused, unchanged freshness, and existing load rollback/cancellation.
- `web/web/tests/network/audio-pipeline.test.ts`: stable file tap through pause,
  seek, dynamic audio track replacement and failed replacement rollback.
- `web/web/tests/hooks/session-recording.test.tsx`: disconnect/reconnect during
  preparation cannot resurrect a stale recording start.
- `web/web/tests/recording/local-recording.test.ts`: final chunks, watchdog,
  cancellation, limits and recovery after encoder failure.

Hardware-specific camera availability, public URL extraction, and real browser
codec output require their existing platform/browser smoke tests; synthetic unit
fixtures do not establish those claims.
