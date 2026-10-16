# Shared session TUI

The native terminal and browser use the C views and key handling in
`include/ascii-chat/ui/session.h`. The browser does not implement its own splash,
help layout, or settings-key switch in React.

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
One active session canvas owns log capture in a module. Its destructor detaches
the logger before freeing the buffer. Native-only debug/statistics and blocking
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
