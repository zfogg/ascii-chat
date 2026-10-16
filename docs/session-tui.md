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

Native presentation remains owned by `ui/controller.c`: its thread, snapshot
lifetime rules, modal screens, and terminal I/O remain native concerns. It calls
the shared views through the existing splash and help adapters.

The browser owns a `ui_session_t` per canvas. `ui_session_tick()` advances the
same C animation engine and returns a complete ANSI frame without sleeping or
writing to a terminal. JavaScript supplies monotonic timestamps, dimensions,
media frames, and focused canvas key events. Help covers splash, and splash
covers media. Incoming media is retained while covered, so closing help restores
the latest frame. Frames go through the existing libvterm/FreeType canvas renderer.
Resize and renderer recreation invalidate presentation without resetting options.

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
