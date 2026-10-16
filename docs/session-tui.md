# Shared session TUI

The native terminal and browser use the C views and key handling in
`include/ascii-chat/ui/session.h`. The browser does not implement its own splash,
help layout, status/log feed, or settings-key switch in React.

`session_splash.c` renders the existing rainbow logo and connection header into
a frame buffer. `session_help.c` renders the keyboard/settings screen without a
native display context. `session_input.c` changes the common C options and returns
host actions for pause, seek, close, and help. Native `session/keyboard_handler.c`
executes those actions against native capture/audio resources; the browser adapter
executes them against HTML media and Web Audio. Keyboard settings are read back
into the React controls and URL state.

Both hosts present through `ui_session_render()`. It owns screen transitions,
resize recovery, minimum-size coverage, redraw decisions, refresh deadlines,
and the splash animation clock. Both hosts select screens using the common
priority order through `ui_session_select_screen()`.

`ui/controller.c` is the native adapter: it retains immutable snapshots and
terminal descriptors, synchronizes producers with the presentation thread,
and supplies a borrowed view to the shared runtime. Existing screen callbacks
write into the runtime's frame buffer; the adapter then writes the completed
frame to the terminal. This includes native status, help, media, progress,
notices, and prompts. Native terminal FPS/write instrumentation and the
independent deadlock-debug mailbox stay in the adapter. Batch output keeps
its synchronous semantics.

The native splash monitor handles startup completion, connection metadata,
and keyboard/log-search input. It no longer advances a separate animation or
publishes every animation frame: the shared runtime supplies the current
animation sample when rendering its header.

The browser owns a `ui_session_t` per canvas. `ui_session_tick()` advances the
same C animation engine and returns a complete ANSI frame without sleeping or
writing to a terminal. JavaScript supplies monotonic timestamps, dimensions,
media frames, and focused canvas key events. Help covers splash, and splash
covers media. Incoming media is retained while covered, so closing help restores
the latest frame. Frames go through the existing libvterm/FreeType canvas renderer.
Resize and renderer recreation invalidate presentation without resetting options.

The browser's `ui_session_tick()` assembles its retained media/splash/help view
and calls that same compositor. Rendering a native view is serialized by its
host; its snapshot and callback context must remain alive through the call.
Neither host holds producer locks while running screen callbacks.

The C options registry and logger are scoped to a WASM module, not to each canvas.
The logger owns the bounded history from initialization through shutdown. Status,
and client overlays borrow it, so closing a screen does not stop capture or
erase earlier messages. Native-only debug/statistics and blocking
prompt workflows are not exposed as browser controls.

The WASM build includes the real session log buffer rather than its former
no-op implementation. Its provenance hash includes `src/common/session` as well
as the shared library, headers, and web adapter. Rebuild and publish both WASM
modules after changing these sources.

`web/web/tests/e2e/session-ui.spec.ts` exercises the actual compiled C runtime and
canvas: animated splash pixels, keyboard/settings synchronization, overlay
ownership over video, resize recovery, latest-frame restoration, bounds checking,
and repeated teardown. Run it with the web app's Playwright configuration and
fresh WASM artifacts.

## Server status UI in client modes

`]` toggles the existing server status UI over client, discovery-client, and mirror
media. Client, discovery, and mirror startup show the animated rainbow splash
without logs; only server CLI modes show logs by default. Logs remain captured
while hidden and appear only after `]`. `?` switches to keyboard help; `]` switches back. Escape cancels a search
first, then closes the overlay. Covered media continues and its newest frame is
restored on close. The keyboard help lists the shortcut.

`session_status.c` contains the server's four-line bordered header and the shared
`terminal_screen_compose()` implementation, extracted from `ui/status.c` and
`ui/terminal_screen.c`. Both server modes, native client overlays, and WASM call
these same functions. Client headers show mode/uptime and controls in place of
server listener metadata. Colors, ANSI-aware wrapping, latest-log selection, and
`/pattern/flags` filtering/highlighting all use the existing C implementations.
The native adapter supplies terminal I/O; the web adapter feeds the resulting
ANSI frame to its existing canvas renderer.

All emitted C application log routes retain history, including console-disabled,
file-only, JSON, and mmap output. Configured severity still determines which
messages are emitted. The history holds the latest 8,192 lines; the view displays
the newest lines that fit. Emergency signal-handler output remains direct because
it cannot safely acquire a history mutex. Search uses PCRE2's portable matching
entry point, including when JIT is unavailable in WASM. Decoded search-edit keys
use the same C line editor on Windows, POSIX, and in the browser.

`tests/integration/terminal_logs.py` exercises mirror and TCP client modes through
real terminals and captures their cell grids. `tests/ui/test_log_history.py`
checks retained C log routes with console output disabled. The browser suite also
checks real C logs, search, overlay exclusivity, resize, and connected-client logs;
set `SESSION_UI_SERVER_URL` to a local server's WebSocket URL for its network case.

The browser FPS counter counts successful canvas paints over the last second for
all views: splash, help, logs, and media. Only actual canvas paints contribute; a view settles to zero when it needs
no repaint. The displayed target is the configured
video FPS, not a forced redraw rate for static overlays.
