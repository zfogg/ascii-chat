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

Screen callbacks run outside the controller mutex and may submit or remove screens.
Producers must not hold locks needed by a callback while removing its screen. The stop, blocked, live-screen, and lifecycle states use custom atomics
and named registry entries. Other controller state is mutex-protected.

Non-terminal output and finite snapshots use synchronous output, without the
interactive warning. Recording happens before terminal presentation and is not
suppressed by the warning. Shutdown joins the presentation thread before keyboard,
log, option, and named-registry teardown.

## Sync diagnostics (Debug builds)

Press `0` to replace media or help with the sync screen; `0` or Escape restores
what was underneath. `?` switches to help. Matrix rain uses `1`.
Left/Right pages through every registered mutex, rwlock, condition variable,
wrapped atomic, and atomic pointer. Up/Down selects a row for its address, owner/waiter, source location, and
last-operation age. Source locations start at `lib/`, `src/`, or `include/`
and use forward slashes; media paths are omitted from the details. Home/End selects
the first/last page. Higher-priority prompts still cover diagnostics.
The list repaginates on live resize. The Name column fits the longest registered primitive name across all pages.
The minimum width is measured from the rendered header, columns, details, and
footer; the minimum height fits those lines plus one entry. The shared
"Terminal too small" screen appears below that size; resizing back restores the list.

The table shows lock/unlock rates (wait/signal rates for conditions), atomic
values, and actual value changes per second. Repeated stores of the same value,
failed compare/exchanges, and zero-delta fetches do not count as changes. Rates
are red at 60 or more, yellow at 10 or more, green when nonzero, and blue at zero.

`lib/ui/sync.c` collects protected copies on its own thread using try-locks.
The presentation thread renders a separate owned mailbox; terminal output never
holds a mailbox lock. Input continues polling independently of the main loop.
Registry contention retains the last complete sample and displays its growing
age. The registry snapshot grows dynamically without a 256-entry limit.
Mutex wait cycles use the existing stack graph detector; its bounded thread,
stack, and cycle capacities are reported when reached. An idle condition wait
is shown as a wait, not proof of a deadlock. These are sampled diagnostics, not
an atomic snapshot of the entire process.

## Verification

`tests/integration/terminal_ui.py` drives the real executable in a resizable PTY
(ConPTY on Windows), interprets its output using pyte, and checks startup-small,
resize recovery, a live WebSocket handshake while covered, ignored keyboard input,
help restoration, paused playback, explicit dimensions, recording while covered,
interrupt shutdown, and redirected snapshots.
It requires `pyte`, `pywinpty` on Windows or `pexpect` on POSIX, and `ffmpeg`.

```sh
python tests/integration/terminal_ui.py --binary build/bin/ascii-chat --debug-sync
cmake --build build --target test-sync-regressions
python tests/integration/sync_regressions.py build/bin/test-sync-regressions
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

### Stdio capture regression

Live screens retain a duplicate of their output descriptor until removal. Media
libraries temporarily redirect process stdout/stderr through `LOG_IO`; presentation
uses the retained terminal for geometry and writes, and keeps accepting snapshots
as live output during capture. This prevents a one-time size warning from being
captured as a diagnostic instead of displayed. Keyboard initialization also retries
when stdout is temporarily redirected, rather than committing an unconfigured
keyboard.

The additional deterministic fixture holds `LOG_IO` active for five seconds and
requires both the small-terminal warning and restored frame before capture ends:

```sh
python tests/integration/tmux_capture.py --probe /tmp/ui-native-probe \
  --artifacts /tmp/render-evidence
python tests/integration/tmux_startup.py --binary build/bin/ascii-chat \
  --artifacts /tmp/render-evidence --count 40 --color-mode 16
```

Startup stress defaults to warning-level logging so debug logging does not mask
initialization races. These checks supplement the 131-case matrix above.

### Half-block geometry and playback regression

`tmux_animation.py` samples 36 ANSI frames over more than 14 seconds from the
10-second `testsrc2` fixture above. It requires changing frames before resize,
after restoration, and after the loop boundary. Run it with the ordinary fixture
and again with an `-an -c:v copy` video-only copy to cover both decoder layouts.

```sh
python tests/integration/tmux_animation.py --binary build/bin/ascii-chat \
  --artifacts /tmp/render-evidence
```

The `halfblock_cover_uses_cell_geometry` Criterion test verifies that half-block
sampling does not double the viewport height used for aspect-ratio cropping.
These longer animation and geometry checks supplement the 131-case interaction
matrix; that matrix alone does not establish continuous playback across EOF.

## Procedural animation controller

`video/anim/controller.h` provides typed dispatch for splash rainbow, digital rain,
rainbow filtering, and the generated test pattern. Instances are independent; there
are no groups or background animation threads in this layer. Existing producers own
and serialize their instances, registered by name for diagnostics. No new shared
atomics are needed. Immutable samples can be copied to the presentation thread.

`animation_update()` accepts monotonic timestamps in a caller-selected domain,
advances the local clock, and returns frame-change and next-deadline information.
Pause, visibility policy, and speed are per instance. Update at a control-change
boundary before changing controls; continue ticking suspended instances so hidden
or paused time is discarded. Call `animation_reset()` before seeking backwards.
`animation_sample_at()` samples an already-scaled elapsed time without allocating
an instance; the rainbow adapter uses the renderer's existing timestamps. Both shared test patterns use elapsed timestamps from their native or browser
producer; pattern 0 is the gradient/square and pattern 1 the rainbow/circle.

`animation_apply()` switches on effect type and validates the target: splash writes
RGB colors, rainbow writes RGB or allocated ANSI, digital rain writes allocated
ANSI, and test patterns render into their reusable pattern context. ANSI outputs belong to the caller and
must be released with `SAFE_FREE`. Digital rain's target includes its producer-owned
column state. Application is stateful for rain (brightness smoothing); sample values
are immutable, but a rain target must not be shared across concurrent renders.

Native and WASM renderers retain their existing public effect entry points, which
now delegate to this controller. Existing effect ordering, terminal presentation,
and video/GIF decoding, playback clocks, and audio synchronization are unchanged.
The presentation scheduler still controls terminal refreshes; deadline metadata is
available for producers without adding a global animation registry or scheduler.

Validation: `tests/unit/video/animation_test.c` covers clocks, independent instances,
visibility, reset, typed targets, both shared patterns, and rain/rainbow composition.
`tests/integration/tmux_animations.py` records all four animations and combined
Matrix/rainbow from real tmux panes before and after resize. Stationary video input
isolates effect motion from source motion. Captures/reports are generated outside
Git, not checked into the repository.
