# Terminal presentation

`lib/ui/controller.c` owns interactive terminal presentation and keyboard reads.
Capture, encoding, networking, and status producers publish screen snapshots;
they do not decide when to show the terminal-size warning.

Each screen supplies an output descriptor, its minimum rows/columns, a renderer,
and a snapshot. `ui_controller_submit()` copies the snapshot. Renderers receive
physical dimensions from the output descriptor, independent of media dimension
overrides. Screens with pointer members must remove their slot before destroying
the pointed-to objects. Removal waits for any in-progress rendering to finish.

The controller selects the highest-priority registered screen: update prompt,
mDNS selection, help, splash, media, then status. It retains covered screens and
the latest media frame. A 16 ms tick detects resizing even when no producer has a
new frame. Below the active screen's minimum it presents `ui/too_small.h` and
discards keyboard input; recovery restores the underlying screen. Interrupts
still terminate the process while the warning is visible.

Media uses the same 20-column and 10-row minimum as the option registry. Help's
height comes from the rows built by its layout, including conditional controls.
Splash and status provide their header geometry. Paused media retains its last
frame; if that frame is larger than the resized terminal, presentation clips it
without resuming capture.

`ui_controller_read_key()` routes queued input only to the active screen's
consumer. The media consumer also handles help keys. Screen callbacks must not
submit another screen or acquire locks held by their producer; they may remove
themselves. The stop flag, blocked state, and lifecycle state use the project's
custom atomics and named registry. Other controller state is mutex-protected.

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
