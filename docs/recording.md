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
elapsed time, while video-file conversion retains the input's frame-rate timing.

The recording regression tests include PCM mixing, microphone and file worker
paths, independent playback/transmission queues, audio-only webcam selection,
mixed encoded audio, encoder-tail flushing, and live frame timing. A separate
mirror and two-client test checks the file tone in mirror output, both input tones
in each call output, and A/V synchronization:

```sh
python tests/integration/render_file_audio.py build/bin/ascii-chat
```
