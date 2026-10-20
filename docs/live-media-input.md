# Open media during a session

In native mirror, client, and discovery sessions:

- Press **o** (or **Ctrl+O**) to open an editable path/URL prompt.
- Paste a URL or drop a file into the terminal to prefill that prompt.
- Press **v** to prefill it from the clipboard, including a file copied in Explorer or Finder.
- Press **Enter** to load the source or **Escape** to cancel.

The current source stays active while the replacement loads. A failed load retains
the previous source. The centered editor and loading panel share the animated
rainbow splash header. The loading panel remains until the new frame is ready,
so the old source never flashes back after submission. Escape cancels a pending load; an external URL extractor may
need to finish before another load can begin. Once loaded, Space pauses playback,
the left and right arrow keys seek, and **[** switches between media and the webcam.
**<** and **>** open the previous/next artifact from the beginning. At the ends of
the list they do nothing. Finishing an artifact automatically opens the next;
**--loop** repeats the complete playlist after the final artifact.

Drop additional files into the open prompt to append quoted paths on separate
lines. Explorer's trailing separator space is removed, leaving the cursor directly
after the closing quote. Enter plays the complete list in order.

The prompt accepts local paths, quoted paths, shell-escaped
spaces, local `file://` URIs, and URLs supported by the existing FFmpeg/yt-dlp
resolver. Pasted content is never executed as a shell command.

Bracketed paste is the reliable automatic path. Some terminals insert dropped
paths as ordinary keystrokes; complete path-shaped bursts are recognized too.
When a terminal splits those keystrokes across reads, press **o** before dropping
the file so every character goes into the prompt instead of session shortcuts.

Windows reads Unicode text and copied-file lists. macOS uses the system
pasteboard. Linux uses `wl-paste` from wl-clipboard or `xclip`, including
`text/uri-list` file selections. Clipboard access happens only when **v** is
pressed. Multiple copied files become the same ordered playlist.

CLI input uses the same playlist model. Repeat **--file** and **--url** in playback
order, including a mixture of both:

```powershell
ascii-chat mirror --file "first video.mp4" --url "https://example.com/second.webm" --file "third.mp4"
```

A playlist supports up to 64 artifacts, each up to 255 UTF-8 bytes; the interactive
editor accepts up to 8191 bytes of quoted text. Stdin (`--file -`) must be the only
artifact. A failed replacement preserves the current source. The final artifact
stays visible when playback ends. Snapshots and recording keep their configured
session duration; skipping always opens a new decoder at the beginning.

Live input is polled independently of ASCII conversion. The FPS overlay counts
newly acquired and successfully presented frames, including identical images;
retained/paused redraws do not count.

## Regression checks

Run the native playlist smoke test against the rebuilt binary:

```powershell
python tests/integration/media_playlist.py --binary build/bin/ascii-chat.exe --artifacts build/playlist-smoke
python tests/integration/media_responsiveness.py --binary build/bin/ascii-chat.exe --artifacts build/pattern-response
python tests/integration/media_responsiveness.py --binary build/bin/ascii-chat.exe --webcam --artifacts build/webcam-response
```

Each responsiveness run renders for two minutes before measuring three open-prompt
responses, with a 250 ms limit. The playlist test checks the rendered keyboard help,
rainbow panels, cursor position, repeated drops, sequential EOF playback, looping,
and previous/next restart behavior. These scripts require Python `pyte`, FFmpeg,
and `pexpect` on POSIX; Pillow optionally saves terminal screenshots.

On Windows, `media_audio_continuity.py` additionally requires NumPy and
PyAudioWPatch. Pass `--binary`, `--media`, and `--artifacts` to compare actual speaker
loopback against an FFmpeg-decoded reference. Captured audio stays in the local
artifact directory.

To trace native frame freshness and verify the FPS overlay on every complete
terminal update, run:

```powershell
python tests/integration/live_frame_latency.py --binary build/bin/ascii-chat.exe --webcam --with-audio --duration 125 --check-input --artifacts build/live-frame-latency
python tests/integration/live_frame_latency.py --binary build/bin/ascii-chat.exe --webcam --fps 5 --duration 15 --artifacts build/throttled-camera
python tests/integration/frame_presentation.py --binary build/bin/ascii-chat.exe --artifacts build/frame-presentation
```

The first check also measures `o` responsiveness after sustained rendering. The
throttled run checks that the camera backend discards queued old samples instead
of replaying them. On Windows, `CAMERA_FRAME` logs include backlog relative to the
fastest delivery and the driver timestamp's frame age when the driver supplies
it (`device_age_ms=-1` means unavailable). `LIVE_FRAME` separates acquisition,
queue wait, conversion, and publication; `FRAME_PRESENT` measures submission
through completed terminal output. These software timings do not measure monitor
scanout or the camera's exposure time.

## Native terminal backpressure and frame tracing

A completed stdout write does not mean a terminal frontend has displayed it.
On a large terminal, each truecolor frame can be hundreds of KB. Sending the next
frame as soon as Windows accepts the previous one lets a frontend accumulate old
frames even when the C capture and presentation queues keep only the latest frame.

The native presenter sends an OSC 11 read-only color query after a fresh media
frame. When the terminal supports this query, its reply establishes that the
frontend has consumed the preceding output. Only one frame can await a reply;
capture continues and replaces the latest mailbox snapshot while presentation
waits. Input and modal screens remain responsive during that wait. FPS counts
completed, acknowledged fresh frames; retained redraws never count. Terminals
that do not answer the initial probe fall back to completed local writes.

The counter's seven cells on the first row are reserved during every media write.
This keeps it visible while a terminal without synchronized output support draws
parts of a frame, rather than erasing it and restoring it only after the art ends.

With `--log-level debug --log-file PATH`, each frame now records acquisition and
copy timing (`CAMERA_FRAME`, `CAMERA_COPY`), pipeline read/queue/conversion and
publication (`LIVE_FRAME`), and completed output (`FRAME_PRESENT`, including
`capture_to_stdout_ms`, geometry, composition, write, drain, dimensions and bytes).
`TERMINAL_ACK` separately records frontend response and capture-to-frontend age.
These measurements do not claim to measure monitor scanout.

The FPS check now uses a 286 by 73 terminal and checks intermediate 4 KB chunks,
as well as complete frames and toggle-off behavior. The frontend emulator replies
after parsing output, so its own rendering speed applies backpressure too:

```powershell
python tests/integration/frame_presentation.py --binary build/bin/ascii-chat.exe --artifacts build/frame-presentation
python tests/integration/terminal_backpressure.py --binary build/bin/ascii-chat.exe --artifacts build/terminal-backpressure
```

The backpressure regression delays replies, holds one for 800 ms, verifies that
no additional media frame is queued, checks responsive open-prompt input during
the hold, and asserts that the FPS count follows actual completed presentations.
