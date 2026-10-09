# Terminal presentation

`lib/ui/controller.c` owns terminal rendering only. It never reads keys, queues input,
edits answers, or interprets commands.
Capture, encoding, networking, and status producers publish screen snapshots;
they do not decide when to show the terminal-size warning.

Each screen supplies an output descriptor, its minimum rows/columns, a renderer,
and a snapshot. `ui_controller_submit()` copies the snapshot. Renderers receive
physical dimensions from the output descriptor, independent of media dimension
overrides. Screens with pointer members must remove their slot before destroying
the pointed-to objects. Removal waits for any in-progress rendering to finish.

The controller selects the highest-priority registered screen: authentication/text prompt, update prompt,
mDNS selection, help, splash, media, then status. It retains covered screens and
the latest media frame. A 16 ms tick detects resizing even when no producer has a
new frame. Below the active screen's minimum it presents `ui/too_small.h`;
recovery restores the underlying screen. Interrupts
still terminate the process while the warning is visible.

Media uses the same 20-column and 10-row minimum as the option registry. Help's
height comes from the rows built by its layout, including conditional controls.
Splash and status provide their header geometry. Paused media retains its last
frame; if that frame is larger than the resized terminal, presentation clips it
without resuming capture.

`lib/ui/input.c` owns keyboard polling, covered-input suppression, and prompt
editing. It reads the controller's presentation state, then lets the appropriate
consumer handle the key. It has no presentation thread or rendering callbacks.
The media consumer also handles help keys. Prompt editing publishes only visible
text; passwords are masked before publication and raw key bytes are not logged.
Authentication warnings and fingerprints are part of the prompt snapshot so they
survive resize and cannot be covered by the splash or media screen.

All session and UI writes (including ASCII writer compatibility calls, grep,
logging, prompt text, and final upgrade instructions) use the controller output
sink. A callback may write its selected screen; unrelated terminal text cannot
paint over an active screen. `ui_controller_finish()` retires the live screens
and writes final text synchronously. The obsolete standalone FPS writer is gone;
FPS is part of the media snapshot. CLI document generation, files, and emergency
crash output are not live screen rendering.

Screen callbacks must not submit another screen or acquire locks held by their
producer. The stop, blocked, live-screen, and lifecycle states use custom atomics
and named registry entries. Other controller state is mutex-protected.

Non-terminal output and finite snapshots use synchronous output, without the
interactive warning. Recording happens before terminal presentation and is not
suppressed by the warning. Shutdown joins the presentation thread before keyboard,
log, option, and named-registry teardown.

## Verification

`tests/integration/terminal_ui.py` drives the real executable in a resizable PTY
(ConPTY on Windows), interprets its output using pyte, and checks startup-small,
resize recovery, a live WebSocket handshake while covered, ignored keyboard input,
help restoration, paused playback, explicit dimensions, recording while covered,
interrupt shutdown, and redirected snapshots.
It requires `pyte`, `pywinpty` on Windows or `pexpect` on POSIX, and `ffmpeg`.

```sh
python tests/integration/terminal_ui.py --binary build/bin/ascii-chat
```

The Criterion `ui_too_small` suite checks exact boundaries and the 1x1/unknown-size
formatting cases. Criterion runs on supported POSIX hosts; the PTY suite also runs
on Windows.

`tests/integration/terminal_prompt.py --library build/bin/asciichat.dll` drives
native Windows prompt APIs over a live media screen and checks text editing,
password masking, covered input, resize restoration, visible host fingerprints,
and absence of plaintext passwords in terminal output and logs.

### Docker and tmux matrix

The `tmux_*.py` drivers run the native executable in detached tmux PTYs and save
plain/ANSI pane captures, exit status, application logs, and sanitizer reports.
They require Python 3, tmux, ffmpeg/ffprobe, and ssh-keygen inside the Linux test
container. Use a private tmux socket; these tests use `ascii390`.

```sh
tmux -L ascii390 new-session -d -s setup -x 80 -y 40
tmux -L ascii390 set-option -g remain-on-exit on
mkdir -p /tmp/render-evidence
ffmpeg -y -v error -f lavfi -i testsrc2=size=160x120:rate=10 \
  -f lavfi -i sine=frequency=440:sample_rate=48000 -t 10 \
  -c:v libx264 -pix_fmt yuv420p -c:a aac /tmp/render-evidence/media.mp4

python tests/integration/tmux_rendering.py --binary build/bin/ascii-chat \
  --artifacts /tmp/render-evidence
python tests/integration/tmux_flows.py --binary build/bin/ascii-chat \
  --artifacts /tmp/render-evidence
python tests/integration/tmux_auth.py --binary build/bin/ascii-chat \
  --artifacts /tmp/render-evidence
python tests/integration/tmux_exports.py --binary build/bin/ascii-chat \
  --artifacts /tmp/render-evidence
```

For the debug ASan/UBSan build, compile the native prompt fixture against the
same shared library (replace `build` below if using a different build directory):

```sh
clang -g -fsanitize=address,undefined -shared-libasan \
  -Iinclude -Ideps -Isrc/common -Ibuild/generated \
  tests/integration/ui_native_probe.c -Lbuild/lib -lasciichat -lm \
  -Wl,-rpath,"$PWD/build/lib" \
  -Wl,-rpath,"$(clang -print-resource-dir)/lib/linux" -o /tmp/ui-native-probe
python tests/integration/tmux_prompts.py --probe /tmp/ui-native-probe \
  --artifacts /tmp/render-evidence
```

Coverage is explicit rather than every possible Cartesian product:

- 84 live cases: all three render modes, six color spellings, render aliases,
  six palettes, all 13 filter spellings across render modes, Matrix, FPS,
  waveform/FFT with all four audio selections, flips/stretch, and UTF-8 off.
  Each checks startup-small, recovery, blocked keys, help, and shutdown.
- 13 flows: server status/grep with a live WebSocket handshake while covered,
  ACDS startup, accept/decline for three overwrite prompts, IP disclosure
  refusal, paused/help recovery with explicit dimensions, and recording while
  covered with all three render themes.
- Four loopback authentication flows: encrypted SSH key, server password,
  missing client identity, and host-key acceptance.
- 16 native prompt/TUI fixtures: text editing, password variants, yes/no
  defaults, cancellation, host/ACDS fingerprints, update choices, mDNS
  selection/cancellation, and splash. Four additional PCM fixtures exercise
  microphone/remote waveform and FFT with a nonzero 440 Hz signal.
- Seven advertised file extensions and three redirected plain snapshots.

mDNS, update availability, GPG passphrase entry, and changed ACDS identities use
deterministic native API fixtures; they do not prove external discovery/update
services or GPG decryption. The Docker mirror's microphone/call sources are
silent; injected PCM fixtures cover their nonzero rendering. ACDS currently has
no status callback, so its startup output is tested without claiming a status
TUI. Hardware cameras, physical audio devices, browser/WASM rendering, and every
combination of independent options are outside this matrix.

Expected refusal exit codes are checked explicitly. Interactive client shutdown
currently returns 1 for an interrupted connection. Sanitizers remain enabled;
the harness treats any sanitizer report as a failure and retains previous-run
logs when retrying a case.
