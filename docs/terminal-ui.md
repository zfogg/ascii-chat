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
